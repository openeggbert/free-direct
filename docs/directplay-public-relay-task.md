# Future task: public relay server for Free Direct multiplayer

Status: **implemented on 2026-11-11**, with the user's decisions of that day replacing parts of
this proposal; see "Outcome" below. Recorded on 2026-11-08 at the user's request.
The source review below used Free Direct `5430a3d22b4f185a5e92b18fce8504ee8b79755a`
and blupi_recon `d2e2680edbce7c1dd184e61e521ddec5a4b0bfbe`.

Labels distinguish **[EVIDENCE]** (source or existing documented results),
**[DESIGN]** (proposed behavior), **[HYPOTHESIS]** (an implication to reproduce),
and **[OPEN]** (a decision or verification still needed). This document does not
claim that Free Direct currently supports public relay multiplayer.

## Outcome (2026-11-11)

Implemented as `docs/directplay-design.md` Decisions 28-30; operation in
`docs/free-direct-server.md`, protocol in `docs/directplay-relay-protocol.md`.

The user changed the model this proposal assumed: there is no public room directory on one fixed
server. A player enters any server's address or URL and port; the server may have a password;
rooms are mandatory, created by a player, never listed, and entered with their secret ID; a room
is a private virtual LAN in which the game's own session list works. Server and native client are
C++ with IXWebSocket; the connection dialog lives in Free API.

Done: the §3 prerequisites (callback flags, joining identity, waits that yield in the browser),
the fixed-width wire header, the server, native and WebAssembly clients, game-shaped
multi-process tests, and browser-to-browser play of Speedy Blupi 1 and 2 through a local server
with a native process joining the browser host's session (blupi_recon `freeapi/tools/net_web.mjs`).

Still open from §8-§9: a deployed public server and tests between different Internet
connections; latency, loss and slow-peer measurements; four players; the Eggbert editions in the
browser; a native game instance playing with a browser one (only the protocol-level join is
tested); host migration.

## 1. Goal and scope

[DESIGN] Make it possible to run a public server to which game instances built
against Free Direct connect over the Internet. The first consumers are the four
English blupi_recon builds: Speedy Blupi 1, Speedy Eggbert 1, Speedy Blupi 2 and
Speedy Eggbert 2. Each room contains compatible instances of one game and edition,
with up to four players. One server can serve many independent rooms concurrently.

Keep the DirectPlay API seen by the game. Implement Internet discovery, session
management and transport behind that API in Free Direct, with configuration in
the consuming build or web shell where needed. Do not edit the historical
`blupi_recon/src/` or `blupi_recon/games/sb2/src/` to add networking features, and
do not change matching-build results or the separate SDL3 port.

The compatibility target is the game-facing API and its behavior, not Microsoft's
DirectPlay wire protocol. Free Direct can use its own server and protocol while
the game continues calling `EnumSessions`, `Open`, `CreatePlayer`, `Send`,
`Receive` and `Close`. Original Microsoft DirectPlay clients are outside this task.

The first server is a room directory and message relay. Game simulation remains
in the clients. The player who creates a room remains the logical game host,
including the game's lobby/start and host-specific gameplay responsibilities.
A dedicated server that simulates the world, authoritative anti-cheat, automatic
host migration and mid-game reconnection are separate possible future tasks.

## 2. Existing foundation

- [EVIDENCE] [IDirectPlayTransport](../src/directplay/DirectPlayTransport.hpp)
  separates byte transport and connection events from DirectPlay session logic.
  It has listen/connect, send/receive, servicing, admission and disconnect methods.
- [EVIDENCE] [DirectPlayInternal.hpp](../src/directplay/DirectPlayInternal.hpp)
  implements the public API, session enumeration, player IDs, packet dispatch and
  host-side broadcast forwarding. Some comments still describe earlier scaffold
  states; review executable method bodies rather than relying on status comments.
- [EVIDENCE] The default backend is an in-process loopback. It cannot connect
  separate game processes or separate browser instances. The four existing web
  build caches have `FREE_DIRECT_ENABLE_ENET=OFF`.
- [EVIDENCE] The optional ENet backend provides real UDP connections and LAN
  broadcast discovery. Joining uses `FREE_DIRECT_ENET_HOST_ADDRESS`; the default
  game port is 51321 and discovery port is 51323. See
  [networking-backends.md](networking-backends.md) and
  [directplay-limitations.md](directplay-limitations.md).
- [EVIDENCE] An ENet connection is not inherently limited to a LAN. A reachable
  Internet host can be addressed, but LAN broadcast discovery does not provide an
  Internet room directory, and direct hosting does not solve client NAT/CGNAT.
- [EVIDENCE] The separate blupi_recon SDL3 port already documents multiplayer
  protocol analysis and two/four-instance scenarios. Its
  [multiplayer assessment and tests][port-multiplayer] are useful evidence for
  game behavior; their results do not establish that Free Direct multiplayer works.

## 3. Prerequisite correctness work

### Session enumeration callback flags

[EVIDENCE] In [dplay.h](../include/dplay.h), `DPENUMSESSIONS_AVAILABLE` and
`DPESC_TIMEDOUT` both have value `0x1`, in their respective input/callback flag
namespaces. `DirectPlay2AImpl::EnumSessions` currently passes the caller's input
`dwFlags` unchanged to its callback in both the registry and LAN paths.
The [SB1 callback][sb1-network] and [SB2 callback][sb2-network] return immediately
when `dwFlags & DPESC_TIMEDOUT` is set; both games enumerate with
`DPENUMSESSIONS_AVAILABLE`.

[EVIDENCE] This source-level combination makes an ordinary discovered-session
callback look like a timeout to these games. Add a regression using the actual
game callback pattern, then fix callback flags independently of backend work.
Do not change a game's callback to accommodate the implementation error.

### Joining and stable player identity

[EVIDENCE] Each `DirectPlaySession` starts its allocator at zero. `CreatePlayer`
allocates locally and immediately writes the ID to the caller. `Open` initiates
joining without waiting for acceptance, and `HandleJoinAcceptPacket`, serviced
later through `Receive`, adds the host-assigned ID to internal state without
updating the value already returned to the caller. The games call `CreatePlayer`
immediately after `Open` and retain that returned ID as `m_dpid` for later sends.

[HYPOTHESIS] A normal join sequence can therefore leave the game sending under a
different identity from the host-assigned member ID, including a collision with
the host's initial zero ID. Reproduce the complete game call sequence; a transport
connect test or a test that waits for acceptance before `CreatePlayer` is insufficient.

[DESIGN] Return a stable, session-unique identity that agrees with routing and
admission state. Establish how reserved IDs and broadcast are handled, and cover
host/client/client identities. Do not retain caller-owned output pointers for
later asynchronous writes or silently renumber an ID already visible to the game.
Admission rejection and failed sends must not be reported as successful joining.

### Async I/O behind synchronous legacy calls

[EVIDENCE] Current transport servicing is mainly driven by `Receive`, but games
also need discovery and admission before ordinary gameplay receive polling starts.
The session callback context and descriptor storage have bounded caller lifetimes.

[DESIGN] Specify how asynchronous server operations satisfy the existing API.
Discovery, opening and player creation need bounded completion/failure behavior.
Do not invoke callbacks after their legacy call/context lifetime ends. Do not block
the browser event loop waiting for a WebSocket callback that needs that same loop.
Use an appropriate yield/async bridge or a well-defined populated directory cache,
and test freshness, first use and errors. Existing web builds use Asyncify, but
that alone does not make every new networking wait safe.

## 4. Proposed architecture

```text
Game instance A -- Free Direct -- outbound WSS --+
Game instance B -- Free Direct -- outbound WSS --+-- Public room/relay server
Game instance C -- Free Direct -- outbound WSS --+     many independent rooms
```

[DESIGN] Every participant, including the logical game host, opens an outbound
connection to the public service. Hosting a room does not open a player-side
listening port. Players behind ordinary NAT or CGNAT do not need port forwarding;
the service itself must be publicly reachable and the network must permit its
outbound connection. The first version needs neither peer-to-peer hole punching
nor STUN/TURN infrastructure.

| Existing game operation | Required relay-mode behavior |
|---|---|
| Provider enumeration | Expose the configured Free Direct provider without requiring new game API calls. |
| `EnumSessions` | Query compatible, joinable rooms and invoke callbacks with valid descriptors and callback flags. |
| `Open(CREATE)` | Register a room, establish its creator as logical host and report creation failure accurately. |
| `Open(JOIN)` / `CreatePlayer` | Join the selected room, enforce compatibility/capacity and return a stable member identity. |
| `Send` / `Receive` | Preserve message boundaries, sender identity, broadcast behavior and required delivery/order semantics. |
| `Close` / destruction | Leave cleanly, release resources and update membership or remove an empty/closed room. |

Room discovery and lifecycle need integration above the byte transport; adding a
different implementation of `Connect` alone cannot provide these operations.
Keep backend-specific dependencies private and preserve the installed DirectPlay
API. The server may use its own internal member tokens; the session layer must
define their mapping to the `DPID` values visible to the game. The byte transport
must not become a parser of game objects or `DPSESSIONDESC2`.

### Routing and logical host

[DESIGN] Prefer server-side fan-out in relay mode: a broadcast reaches every other
member of that room exactly once, including the logical host when a client sends,
and never echoes to its sender. Keep source identity intact. Account explicitly
for Free Direct's existing host-side broadcast forwarding: relay mode must bypass
that second forwarding path, or otherwise use a proven single forwarding owner.
Do not combine both and duplicate every client broadcast. Existing ENet routing
must retain its behavior.

The server does not take over the game's host role merely by forwarding messages.
The original code caches its host status at create/join and has host-specific
lobby and gameplay duties. A relay surviving the host's departure does not make
the room playable without that host. Initially, host loss ends the room with an
explicit outcome. Define a failure path that the actual game or its compatibility
shell presents to the player; do not assume an ignored error return is sufficient.
Document `DPSESSION_MIGRATEHOST` as unsupported until the game-level behavior is
implemented and tested separately.

## 5. Transport and configuration

[DESIGN] Use secure WebSockets (`wss://`) as the first shared browser/native relay
transport. Browser builds can use Emscripten's WebSocket API; native builds need
a compatible WebSocket client. Both must use the same versioned application
protocol. Production HTTPS game pages connect to WSS; plain WS may be allowed
for explicitly configured local development.

[EVIDENCE] Browsers do not expose direct UDP sockets. Emscripten documents its
C/C++ WebSocket API, networking limitations, and WebRTC/WebTransport alternatives:
[Emscripten networking](https://emscripten.org/docs/porting/networking.html).
See also the [WebSocket API](https://developer.mozilla.org/en-US/docs/Web/API/WebSockets_API).
Merely enabling the existing ENet build option does not provide this WSS service.

WebSockets use reliable ordered delivery, including for game messages originally
sent without `DPSEND_GUARANTEED`. This is an explicit first-version tradeoff: packet
loss can delay newer traffic behind older data. Preserve guaranteed ordering,
bound outgoing queues and measure latency during gameplay. Do not advertise
unreliable datagram behavior over WSS.

[EVIDENCE] The game's MOVE bundles can contain events as well as poses; the
[existing assessment][port-multiplayer] records that old poses may be ignored while
their embedded events are still applied. Therefore, do not drop or coalesce an
apparently stale MOVE packet until its event semantics are proven safe. Reliable
events and the original application-level ordering rules must remain intact.

[DESIGN] Keep loopback for deterministic tests and retain the optional ENet backend.
The WSS proposal extends the existing ENet-first/build-time selection design; record
the resulting selection/lifecycle decisions in [directplay-design.md](directplay-design.md)
and update the backend/limitations documentation when implementation begins.

[OPEN] Finalize build selection, the native WebSocket dependency and the server
implementation language. Configure the public endpoint outside historical source:
for example, a native environment/config setting and web-shell configuration passed
to Free Direct before networking starts. Any new names, such as
`FREE_DIRECT_RELAY_URL`, are proposals, not existing options. Missing or unreachable
configuration must produce an actionable failure rather than silently selecting
loopback or leaving the game waiting indefinitely.

## 6. Room and protocol requirements

- [DESIGN] A room records its unique session identity, compatibility identity, name,
  logical host, capacity, members and lifecycle. Define whether joining after start
  is allowed by the actual game; the initial target is pre-start lobby admission.
  Recheck admission when joining even if a room was available when listed.
- [EVIDENCE] SB1 and SB2 use the same `APP_GUID` in their historical `network.cpp`.
  It is insufficient to distinguish compatible games. Add explicit metadata for
  game, edition, protocol version and multiplayer-content compatibility, supplied
  by the consuming adapter/build. Distinct editions remain isolated unless a later
  compatibility study explicitly permits mixing them.
- [DESIGN] Serialize a fixed-width, versioned envelope with explicit byte order,
  message type, room/session epoch, sender/destination information and payload
  length. Do not send native pointers, native `long`, or compiler-layout structs
  as an undocumented protocol. Audit existing header serialization and historical
  payload layouts on native 32/64-bit platforms and WASM before claiming cross-play.
- [DESIGN] Bind member identity to the admitted connection. Reject spoofed origins,
  unknown destinations, wrong room/epoch/version, invalid lengths and over-capacity
  joins. Never forward traffic between unrelated rooms. Keep protocol control
  messages separate from opaque game data.
- [DESIGN] Bound message sizes, queues, connection/admission timeouts and server
  resource use. Define backpressure and disconnect behavior. Validate game payloads
  before legacy code consumes them using evidenced per-game rules in an agreed
  adapter/validation boundary. Forwarding a well-formed envelope alone does not
  establish that its game payload is safe or meaningful.
- [OPEN] Decide room visibility, optional private-room codes/passwords and their
  lifecycle. Accounts, persistent matchmaking profiles and social features are
  not prerequisites for the first playable public server.

## 7. Deployment deliverables

[DESIGN] Provide a separately runnable server, deployment instructions, documented
configuration and a repeatable local integration setup. The server needs no game
assets or original executables. Document its listen address/port, public endpoint,
TLS termination, allowed web origins, room/connection limits, health check and
graceful shutdown. A reverse proxy may terminate TLS and forward WebSocket upgrades
to the service. Validate browser `Origin` at admission without mistaking it for
authentication; define a deliberate policy for native clients without that header.

Log connection/admission/disconnection reasons, room IDs and queue/latency metrics
without dumping credentials or arbitrary gameplay payloads by default. On restart,
close sessions explicitly; persistent room recovery is outside the initial scope.

The static game files may remain on GitHub Pages while WSS runs on another host
or subdomain. GitHub Pages is a static hosting service, not a runtime for the relay:
[GitHub Pages documentation](https://docs.github.com/en/pages/getting-started-with-github-pages/what-is-github-pages).
Test the connection from the actual HTTPS, cross-origin-isolated game pages, not
only a local HTTP example. A provider, hostname and operating budget remain [OPEN];
this document does not provision infrastructure or claim that an endpoint exists.

## 8. Implementation milestones

1. **Correct the existing API flow.** Reproduce/fix enumeration callback flags and
   the real `Open` -> `CreatePlayer` -> `Send` -> `Receive` identity sequence.
   Test timeout, rejection, room-full and close/reopen cases using game-shaped
   callers. Preserve existing loopback and ENet coverage.
2. **Specify the relay protocol and lifecycle.** Record discovery, admission,
   identity, room isolation, broadcast ownership, failure paths and async bridging.
   Select dependencies and record the design changes in the existing documents.
3. **Native pilot with a local server.** Run two separate rebuilt game processes
   through the same server: discover/create/join, lobby, ready, start, gameplay and
   leave. Verify sender identity and exactly-once broadcast delivery to recipients.
4. **Browser and native interoperability.** Add the Emscripten transport/configuration
   and test independent browser contexts, then browser/native peers of the same
   edition. Cover all four target games independently and expand to four players.
5. **Public Internet pilot.** Deploy WSS, test peers behind different routers and
   Internet connections without forwarding player ports, then measure latency,
   loss, slow-client behavior, abrupt host/client loss and server restart.
6. **Package and document.** Supply server operation instructions, client endpoint
   configuration, protocol/limitation documentation and bounded regression checks.
   Keep this task marked incomplete until the acceptance evidence is recorded.

Use existing build directories and the repository's shared build-cache rules.
Keep generated traces, packets and screenshots under ignored build output.
Only rebuilt game programs are runtime test subjects; do not run original game
executables or installers as part of this task.

## 9. Acceptance criteria and evidence

- Two, then four independent instances of each target edition can discover one
  compatible room, join, see distinct players, ready, start and play together.
- Peers on different Internet connections can play through the public service
  without opening an inbound port on any player machine, including the logical host.
- Browser/browser and native/browser sessions work for the same edition. Record
  tested browsers, operating systems, architectures and build revisions; include
  Chrome and Firefox in the browser validation matrix.
- Multiple rooms operate concurrently without discovery or payload leakage.
  Incompatible game/edition/content/protocol combinations are rejected explicitly.
- Reliable game events remain ordered and intact; broadcasts preserve source IDs,
  reach all intended recipients and are not echoed or forwarded twice. Verify the
  actual lobby and gameplay, including host-coordinated objects, not only echo packets.
- Full rooms, missing/unreachable endpoints, failed TLS/connection/admission,
  malformed messages, client departure, host departure, browser reload and server
  shutdown have bounded outcomes. No infinite joins, stale room entries or orphaned
  server/client resources remain after the documented timeout.
- Run bounded latency/loss/slow-peer scenarios and record measured playability,
  queue growth and recovery behavior. Define a latency budget during the pilot;
  a localhost success alone is not evidence of acceptable Internet gameplay.
- Existing loopback/ENet tests and web single-player checks still pass. Verify that
  historical game source files and matching-build inputs are unchanged.
- Publish concise per-game results and reproducible commands, with versions and
  limits. Automated transport/API tests, real game scenarios and manual Internet
  results are separate kinds of evidence; none is a historical matching proof.

## References

- [DirectPlay backend architecture](networking-backends.md)
- [Existing design decisions](directplay-design.md)
- [Current limitations](directplay-limitations.md)
- [Current protocol](directplay-protocol.md)
- [DirectPlay call-site audit](directplay-callsite-audit.md)
- [blupi_recon multiplayer analysis and native-port evidence][port-multiplayer]

[sb1-network]: https://github.com/openeggbert/blupi_recon/blob/d2e2680edbce7c1dd184e61e521ddec5a4b0bfbe/src/network.cpp
[sb2-network]: https://github.com/openeggbert/blupi_recon/blob/d2e2680edbce7c1dd184e61e521ddec5a4b0bfbe/games/sb2/src/network.cpp
[port-multiplayer]: https://github.com/openeggbert/blupi_recon/blob/d2e2680edbce7c1dd184e61e521ddec5a4b0bfbe/port/docs/MULTIPLAYER.md
