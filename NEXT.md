# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is explicitly not an attempt at full DirectX compatibility —
scope is bounded by what the two target games' real call sites need (see `CLAUDE.md`).

- **Target games** (sibling repos): `../free-eggbert` (*Speedy Blupi* — DirectDraw + DirectSound +
  DirectPlay) and `../planetblupi` (*Planet Blupi* — DirectDraw + DirectSound only; confirmed zero
  DirectPlay usage).
- **Current development phase**: `plan.md` Phases 0-5 complete. Phase 6 ("Session hosting") is
  essentially done — its only remaining tasks are blocked on later phases (Section 8). Phase 7
  ("Session joining") got as far as it can go without more of Phase 10: a real loopback host +
  client connect end-to-end, `dwMaxPlayers` cap enforcement works with client-side rejection
  observability (Decisions 10-13) — but the join is connection-establishment only (no DPID/session-
  descriptor exchange), since that needs the join-request/accepted wire handshake. Phase 10
  ("Send/Receive networking") now has real, working, end-to-end unicast delivery over loopback:
  a local player can `Send()` to one specific real remote player and the recipient's `Receive()`
  gets the exact payload (Decisions 14-15) — but **host role and loopback only**: a joining role
  cannot yet address any specific remote player (it has no way to learn any remote DPID before the
  join handshake exists), host-side routing/forwarding to a non-host recipient doesn't exist, and
  broadcast (`idTo == 0`) isn't implemented (Decision 15 flags a real, unresolved ambiguity there -
  DPID `0` is both a valid real player ID, per Decision 3, and `free-eggbert`'s own broadcast
  convention). The ENet backend's receive-side buffering also remains unimplemented. Phases 8, 9,
  11 onward (enumeration, player management, error semantics, hardening, CI, docs) have not
  started.
- **Key architectural decisions** (`docs/directplay-design.md` has the full record, Decisions
  1-15):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only — no SDL3/ENet/SDL3_net symbol may ever appear in them.
  - SDL3 backs DirectDraw/DirectSound. ENet is the preferred DirectPlay network transport (over
    SDL3_net), chosen for built-in reliable UDP, ordering, and peer management.
  - DirectPlay is explicitly **not** Microsoft-wire-compatible. The only compatibility goal is
    FreeDirect-to-FreeDirect: two programs built against this same implementation can host/join/
    exchange messages with each other.
  - Transport is abstracted behind `IDirectPlayTransport` (`src/directplay/DirectPlayTransport.hpp`),
    with two concrete backends: `LoopbackDirectPlayTransport` (in-process, no sockets) and
    `EnetDirectPlayTransport` (real ENet). `Open()` selects between them at **build time** via
    `FREE_DIRECT_ENABLE_ENET` (Decision 4).

## 2. Current status

**Build status: working.** Both configurations were built clean (zero warnings on changed files)
as of the last verification in this session:

```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```
```bash
cmake -B cmake-build-enet -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build cmake-build-enet -j4
```
(The `cmake-build-enet` directory is a scratch build dir, not committed — recreate and delete it
as needed; it is not part of the repository.)

**Test status: 37/37 passing.** `tests/directplay_tests.cpp` is a standalone file with its own
`main()`, **not yet wired into CMake/CTest** (`plan.md` Phase 15, not started). Build/run command
in Section 7. No other automated tests exist in the repository.

**Available artifacts**:
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`) exercising DirectDraw surfaces/blits/palette;
  compiles, but has not been run/observed graphically in recent sessions (no display verification
  performed).
- `tests/directplay_tests.cpp` — build/run manually per its own header comment; not CTest-wired.

**Recently implemented** (DirectPlay/Phase 6, all verified — see Section 3 for detail): real ENet
session hosting (`Listen()` on a fixed default port), event servicing via `Service()`, real
multi-peer connection tracking with DPID assignment up to `dwMaxPlayers`, disconnect notification
(`dwCurrentPlayers`/player-list bookkeeping updates on peer disconnect), rejection of connections
once `dwMaxPlayers` is reached, session-instance GUID generation, and a corrected 4-byte `DPID`
type starting at `0`.

**What does not work yet**:
- Transport-level `Receive()` on the ENet backend unconditionally returns `false` — received
  packets are discarded, not delivered (no `plan.md` task covers this yet; implied by Phase 10).
- `Send()`/`Receive()` on a *hosting*-role transport instance always return `false` — there is no
  way yet to address one specific connected peer among potentially many (`plan.md` Phase 10).
- No join-accepted/join-rejected wire packets are ever sent to a connecting client — blocked on
  the per-DPID `Send()` capability above.
- `EnumSessions()` always returns zero results (`plan.md` Phase 8, not started) — currently the
  honestly-correct answer, since nothing implements real discovery yet.
- No FreeDirect-driven client has ever joined a FreeDirect-hosted session end-to-end; all ENet
  verification so far used a raw external ENet test client, because `IDirectPlay2A::Connect()`
  doesn't exist yet (Phase 7).
- DirectSound/DirectDraw hardening (`plan.md` Phases 13-14) not started.
- CTest/CI integration (`plan.md` Phase 15) not started.

## 3. Recent changes

Most recent commits (newest first):
- (uncommitted, this session) — `plan.md` Phase 10: wired `DirectPlay2AImpl::Send()`/`Receive()`
  (`DirectPlay.cpp`) to Decision 14's transport capability (`docs/directplay-design.md`
  Decision 15) - the direct, already-scoped continuation of the previous commit, not a fresh user
  question. `Send()` for `idTo != idFrom` now validates `idFrom` is local and `idTo` is an
  already-assigned remote player (`DPERR_INVALIDPLAYER` otherwise), rejects oversized payloads
  (`DPERR_SENDTOOBIG`, checked before the transport - an oversized packet on the wire would
  otherwise wedge the receiver's fixed-size read buffer forever), serializes a
  `DirectPlayWirePacketHeader` (`DirectPlayWireProtocol.hpp`, built in Phase 5, unused until now) +
  payload, and calls `transport->Send(idTo, ...)`. `Receive()` gained a drain loop (both roles)
  that pulls real transport-delivered blobs, deserializes them, and enqueues into
  `session_.messageQueue`. Only the hosting role can address a specific remote player - confirmed
  via `docs/directplay-callsite-audit.md` that `free-eggbert` never sends to a specific DPID
  anyway (always broadcasts, `Send(m_dpid, 0, ...)`), and the joining role has no way to learn any
  remote DPID yet (blocked on the join-accepted handshake), so it gets `DPERR_INVALIDPLAYER` for
  any non-self send. Found and flagged (not resolved) a real ambiguity for whoever implements
  broadcast next: Decision 3 assigned DPID `0` to the host's own first local player rather than
  reserving it as `DPID_ALLPLAYERS`, so `free-eggbert`'s own `Send(m_dpid, 0, ...)` is genuinely
  ambiguous between "broadcast" and "the specific player whose DPID is `0`" under current
  semantics. Verified: 37/37 `tests/directplay_tests.cpp` suite passes (five new end-to-end tests,
  via the real public API, not whitebox: a host delivering to a specific assigned client, and four
  validation-failure cases); both CMake configs (`ENET=OFF`/`ON`) build clean; `include/dplay.h`
  still has zero ENet/SDL identifiers.
- `faf65fe` — `plan.md` Phase 10 groundwork: `IDirectPlayTransport::Send()`
  gained a `DPID targetId` parameter (`docs/directplay-design.md` Decision 14), asked of and
  confirmed by the user (over adding a separate new `SendTo()` method) - `Send()` had zero callers
  left in `DirectPlay.cpp` after Decision 12 removed the self-send round-trip, so this interface
  change broke no production call site. `LoopbackDirectPlayTransport` now delivers real payloads
  for all three modes (hosting role routes to `connectedPeers_[targetId]`'s own inbox; joining role
  routes to the sole `hostPeer_`; self-send-only mode unchanged) by reaching into the *target
  instance's* private `buffered_` directly - the same mechanism Decision 10 already used for
  `Connect()`/`RejectPendingConnection()`/`Shutdown()`. `Receive()` no longer has any role-based
  guard - it just pops from `this->buffered_` unconditionally now, since only legitimate senders
  ever push into it. `EnetDirectPlayTransport::Send()` got the equivalent `connectedPeers_` lookup;
  its receive-side buffering (`Service()`/`Receive()`) is explicitly left unimplemented, matching
  this session's loopback-first pattern. Replaced the now-obsolete
  `Test_LoopbackSend_ReturnsFalseForHostingAndJoiningRoles` (its premise no longer holds) with three
  new tests covering host→client delivery, client→host delivery, and unknown-target failure.
  **Not done**: `DirectPlay2AImpl::Send()`/`Receive()` still don't use this new capability at all -
  no wire-header construction/parsing, no recipient lookup, no host routing/broadcast. Verified:
  32/32 `tests/directplay_tests.cpp` suite passes; both CMake configs (`ENET=OFF`/`ON`) build clean;
  `include/dplay.h` still has zero ENet/SDL identifiers.
- `60afb4b` — Added client-side rejection/disconnection observability (Decision
  13), asked of and confirmed by the user: promoted `LoopbackDirectPlayTransport`'s and
  `EnetDirectPlayTransport`'s existing test-only `HasHostConnection()`/`HasPeer()` accessors into a
  real `IDirectPlayTransport::IsConnectedToHost()` method (both backends' implementation is
  unchanged - `hostPeer_ != nullptr` - this is a rename/promotion, not new logic).
  `DirectPlay2AImpl::Receive()`'s joining-role path now checks it, but only after the local
  message queue comes back `DPERR_NOMESSAGES` - preserving Decision 12's guarantee that a
  self-sent message is always deliverable regardless of host-connection state; only once the
  queue is genuinely empty does a lost host connection surface as `DPERR_NOCONNECTION`. This
  closes `plan.md` Phase 7's last remaining reachable task: `dwMaxPlayers` cap enforcement
  (Decision 9's assignment/rejection loop, already backend-agnostic) now works over loopback
  end-to-end, and the rejected client can actually observe it. Verified: 30/30
  `tests/directplay_tests.cpp` suite passes (new
  `Test_OpenAsJoinOverMaxPlayers_ThirdClientReceivesNoConnection`); both CMake configs
  (`ENET=OFF`/`ON`) build clean; `include/dplay.h` still has zero ENet/SDL identifiers.
- `0ad0833` — Resolved the self-send-vs-hosting conflict (Decision 12), asked of
  and confirmed by the user: `DirectPlay2AImpl::Send()`'s self-send branch (`idFrom == idTo`) no
  longer routes through `session_.transport->Send()`/`Receive()` at all - it enqueues a
  `DirectPlayMessagePacket` directly into `session_.messageQueue` instead. Reasoning: sending a
  message to yourself is always a purely local operation and was never conceptually a network
  concern; also, `IDirectPlayTransport::Send()` has no recipient parameter, so the transport layer
  structurally cannot distinguish self-send from any other traffic, ruling out a
  transport-level fix. With that fixed, `Open(..., DPOPEN_CREATE)` now safely calls
  `transport->Listen(kDefaultDirectPlayLoopbackPort)` over loopback too (mirroring the ENet
  branch's existing shape) - completing Decision 11's deferred half. A real loopback host +
  client can now connect end-to-end: `Test_OpenAsJoinWithHostPresent_Succeeds` (new) opens a real
  host then a real joining client, asserting `Open(..., DPOPEN_JOIN)` returns `DP_OK`. This is
  connection-establishment only, not a completed join (no DPID/session-descriptor exchange yet -
  still blocked on Phase 10's per-DPID `Send()`). One pre-existing test needed adjusting as a
  direct consequence of the single fixed port now being genuinely enforced:
  `Test_OpenAsHostWithZeroGuidInstance_GeneratesNonZeroGuid` used to hold two loopback hosts open
  simultaneously; changed to open/capture/close the first before opening the second (doesn't
  weaken what it actually verifies - GUID uniqueness across generations). Verified: 29/29
  `tests/directplay_tests.cpp` suite passes; both CMake configs (`ENET=OFF`/`ON`) build clean;
  `include/dplay.h` still has zero ENet/SDL identifiers.
- `fb63887` — Wired `DirectPlay2AImpl::Open(..., DPOPEN_JOIN/DPOPEN_OPENSESSION)`
  to call `transport->Connect()` over loopback (`docs/directplay-design.md` Decision 11), returning
  `DPERR_NOSESSIONS` when it fails. Added `kDefaultDirectPlayLoopbackPort = 51322`
  (`LoopbackDirectPlayTransport.hpp`), mirroring Decision 5's ENet port, asked of and confirmed by
  the user before implementing. **Found and reverted a regression while implementing**: wiring the
  hosting role to symmetrically call `Listen()` (as the literal task wording could be read) would
  put every hosted session's transport into the "listening" state, where Decision 10 makes
  `Send()`/`Receive()` always return `false` — silently breaking the self-send feature every Phase
  4 test (and `free-eggbert`'s own real self-send pattern) depends on. Scope corrected to
  joining-role-only; the hosting role's `Open()` branch is unchanged, so `Connect()` always fails
  today (nothing ever calls `Listen()` on the shared port) — that's honestly correct, not a bug,
  until a follow-up task reconciles "hosted and discoverable" with "still able to self-send."
  Verified: 28/28 `tests/directplay_tests.cpp` suite passes (new end-to-end test
  `Test_OpenAsJoinWithNoHostPresent_ReturnsNoSessions`, via the real public `Open()`, not whitebox);
  both CMake configs (`ENET=OFF`/`ON`) build clean; every pre-existing hosting/self-send test still
  passes; `include/dplay.h` still has zero ENet/SDL identifiers.
- `dbf39ab` — `plan.md` Phase 7 groundwork: gave `LoopbackDirectPlayTransport` a
  real multi-instance connection lifecycle (`docs/directplay-design.md` Decision 10), so a separate
  "client" instance can find and connect to a separate "host" instance in the same process — needed
  because Phase 7's own acceptance criteria requires a deterministic loopback host+2-clients(+
  rejected-3rd) test. Two design questions asked of and confirmed by the user before implementing:
  (1) `Connect()` finds the host via a process-wide static registry keyed by the `port` argument
  (over a session-GUID-keyed alternative); (2) `Send()`/`Receive()` return `false` for both the
  hosting and joining roles once connected (diverging from `EnetDirectPlayTransport`'s asymmetry —
  real payload delivery is a separate, later decision, not incidental to connection lifecycle).
  Connection lifecycle otherwise mirrors `EnetDirectPlayTransport`'s pending/connected/disconnected
  model exactly (Decisions 7-9), with `LoopbackDirectPlayTransport*` in place of `ENetPeer*`.
  Deleted copy/move (these instances hold raw pointers to each other); `Shutdown()`/destructor
  scrub every known peer's reference symmetrically, required for memory safety, not just parity.
  **Does not** touch `DirectPlay.cpp`'s `Open()` (still doesn't call `Connect()` for `DPOPEN_JOIN`)
  and does **not** implement the join-request/accepted wire handshake (blocked on per-DPID-addressed
  `Send()`, Phase 10). Verified: 27/27 `tests/directplay_tests.cpp` suite passes (11 new whitebox
  tests against `LoopbackDirectPlayTransport` directly); both CMake configs (`ENET=OFF`/`ON`) build
  clean; `include/dplay.h` still has zero ENet/SDL identifiers.
- `db85b59` — Added `Test_OpenWithMalformedDwSize_ReturnsInvalidParams`
  (`tests/directplay_tests.cpp`), closing `plan.md` Phase 6's "Add a test for invalid host
  parameters" task. Found and documented a scope correction while implementing: `dwMaxPlayers == 0`
  (mentioned in the task's own wording) is deliberately **not** an error case (Decision 9's
  intentional "no limit" value) and `Open()` never validates it, so the test only covers the real
  gap — a malformed `DPSESSIONDESC2.dwSize` making `Open()` return `DPERR_INVALIDPARAMS` instead of
  `DP_OK`. `tests/directplay_tests.cpp` is not referenced by `CMakeLists.txt` (Phase 15, not
  CTest-wired), so this change cannot affect either CMake build config either way. Verified: 16/16
  `tests/directplay_tests.cpp` suite passes; `include/dplay.h` still has zero ENet/SDL identifiers.
- `4e26809` — Added `Test_OpenAsHostOverLoopback_ReturnsOk`
  (`tests/directplay_tests.cpp`), closing `plan.md` Phase 6's "Add a test for host session
  creation over loopback" task. Found and documented, rather than worked around, that
  `IDirectPlay2A` has no public way to observe "is this session the host" today (`Open()` returns
  `DP_OK` for `DPOPEN_JOIN`/`DPOPEN_OPENSESSION` too, since Phase 7's real `Connect()` doesn't
  exist yet) — the test asserts only the observable half (`Open(..., DPOPEN_CREATE)` succeeds over
  loopback), and `plan.md`'s checkbox annotation spells out why a host-observing query wasn't
  added (would be new public API surface with no real-game call site, requiring the user's sign-off
  first per `CLAUDE.md`). Verified: 15/15 `tests/directplay_tests.cpp` suite passes; both CMake
  configs (`ENET=OFF`/`ON`) build clean; `include/dplay.h` still has zero ENet/SDL identifiers.
- `52616cf` — Enforce `dwMaxPlayers` by rejecting over-cap pending connections. New
  `IDirectPlayTransport::RejectPendingConnection()` (mirrors `AssignPendingConnection(DPID)`, no
  DPID parameter — nothing to assign) pops the oldest pending ENet peer and gracefully
  `enet_peer_disconnect`s it. `DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) runs it in a loop
  once `dwMaxPlayers != 0` and the session is at/over the cap. Documented as
  `docs/directplay-design.md` Decision 9. Verified with a real ENet smoke test: clients within the
  cap connect normally; an excess client observes a genuine `ENET_EVENT_TYPE_DISCONNECT`. While
  chasing an initial test failure, found and fixed a **test-harness bug** (not production code):
  the smoke test stopped servicing its first client's own `ENetHost` the instant that client
  locally observed `CONNECT`, so its handshake-completing ACK never reached the server — the
  server never actually admitted that client, leaving a slot open that a second, supposedly-
  rejected client then legitimately took.
- `36276a4` — `include/dplay.h`'s `DPID` typedef changed from `DWORD_PTR` (8 bytes on this
  platform) to `DWORD` (4 bytes, matching real Microsoft DirectPlay and `free-eggbert`'s
  `NetPlayer` hardcoded-stride assumption). `DirectPlaySession::nextPlayerId` now starts at `0`.
- `aa487e1` — Disconnect notification: `EnetDirectPlayTransport` gained
  `HasDisconnectedPeer()`/`TakeDisconnectedPeer(DPID*)`; `Receive()` now decrements
  `dwCurrentPlayers`/removes from `remotePlayerIds` when an assigned peer disconnects.
- `7b371f8` — Real multi-peer hosting: `connectedPeers_` (`DPID → ENetPeer*`) and `pendingPeers_`
  replace the old single-peer model for the hosting role; DPIDs assigned up to `dwMaxPlayers`.
- `6d26379` — `Open()` generates a session-instance `guidInstance` when hosting with an all-zero
  one; confirmed the session descriptor was already fully stored (no new code needed).
- `cba0c7c` — Added `IDirectPlayTransport::Service()`; `Receive()` calls it to drain ENet events —
  closed the gap where nothing ever called `enet_host_service()`.
- `8e4219b` and earlier Phase 5/6 commits — real ENet lifecycle, `Listen`/`Connect`/`Send`
  (reliable + unreliable), graceful `Shutdown`, build-time backend selection.

Full narrative detail and rationale for each decision lives in `docs/directplay-design.md`
(Decisions 1-14) and in each commit's own message / `plan.md`'s per-task `**Done:**` annotations.

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists right now** — everything builds and 37/37 tests pass.
Real unicast delivery works end-to-end over loopback for the host role (Decisions 14-15). What
remains is scope, not a blocker: host-side routing/forwarding (a joining peer reaching another
peer through the host) and broadcast (`idTo == 0`) are both unimplemented, and broadcast in
particular has a real, flagged-but-unresolved semantic question waiting (see Section 5/8) - DPID
`0` is both a real player ID (Decision 3) and `free-eggbert`'s own broadcast convention.

A minor, non-blocking open item: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` (the system-package ENet path)
has never been exercised successfully in this environment (no `libenet` system package installed
here) — only the vendored-submodule ENet path is proven. Not currently blocking anything.

## 5. Known bugs and limitations

- **Confirmed, low priority**: `GUID::Data1` is `unsigned long`, 8 bytes on this (Linux/64-bit)
  platform, not the 4 bytes real DirectPlay's `Data1` documents — `sizeof(GUID)` is 24 here, not
  16. Only matters if FreeDirect ever needs wire-compatible `GUID` serialization across platforms
  with different `unsigned long` widths, which is not a current, demonstrated need.
- **Confirmed, not a FreeDirect bug**: `../free-eggbert`'s own DirectPlay lobby/session UI
  (`WM_PHASE_DP_*` handlers in `src/event.cpp`) is unwired — `NetCreate`/`NetEnumSessions`/
  `JoinSession`/`CreateSession`/`NetStartPlay` have zero callers anywhere in that game's current
  source. Only gameplay-time `Send`/`Receive` are reachable. Out of `free-direct`'s scope to fix
  (game source must not be modified).
- **Incomplete**: transport-level `Receive()` (ENet backend only - loopback is real now, Decision
  14) unconditionally returns `false`; received packets are destroyed by `Service()`, not delivered
  anywhere (Phase 10).
- **Incomplete**: a joining-role session cannot address any specific remote player in `Send()` at
  all (always `DPERR_INVALIDPLAYER`, Decision 15) - it has no way to learn any remote DPID
  (including the host's own) before the join-accepted handshake exists. This is also what still
  blocks the join-accepted/join-rejected packet tasks in Phase 6.
- **Incomplete**: no host-side routing/forwarding - a `Send()` addressed to a non-host recipient by
  a joining peer isn't relayed by the host (Phase 10, star-topology routing not started).
- **Incomplete**: broadcast (`idTo == 0`/`DPID_ALLPLAYERS`) isn't implemented (Phase 10). **Real,
  unresolved ambiguity found while wiring unicast delivery (Decision 15)**: Decision 3 assigned DPID
  `0` to the host's own first local player rather than reserving it as `DPID_ALLPLAYERS`
  (specifically to match `free-eggbert`'s own comparison pattern) - but `free-eggbert`'s only real
  `Send()` call site (`Send(m_dpid, 0, ...)`, per `docs/directplay-callsite-audit.md`) always uses
  `0` to mean *broadcast*. Whoever implements broadcast must resolve this - "is `0` broadcast, or
  the specific player who happens to have DPID `0`?" - not treat it as already decided.
- **Incomplete**: `EnumSessions()` always reports zero sessions (Phase 8, not started).
- **Incomplete**: no CTest/CI wiring for `tests/directplay_tests.cpp` (Phase 15).
- **Needs verification**: `FREE_DIRECT_USE_SYSTEM_ENET=ON` — logic written and reviewed, never
  actually exercised against a real `libenet` install (see Section 4).
- **Risky assumption**: in `Send()`'s self-send path, any `DirectPlayMessageQueue::Enqueue()`
  failure (queue full **or** oversize payload) currently returns `DPERR_SENDTOOBIG` — imprecise
  for the "queue full" case specifically. Flagged in `plan.md` for Phase 10 to refine.
- **Risky assumption**: `Open()` validates `dwFlags` against the `DPOPEN_CREATE`/`DPOPEN_JOIN`/
  `DPOPEN_OPENSESSION` mask but does not enforce that `DPOPEN_CREATE` and `DPOPEN_JOIN` are
  mutually exclusive. Not currently exercised by any known call site.
- **Unknown**: whether the demo executable (`FREE_DIRECT`) actually runs and renders correctly on
  a real display — it compiles, but has not been run graphically in recent sessions.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped types/constants/
interfaces only. **Hard rule, enforced by convention + manual `grep` (not yet automated in CI)**:
no SDL3/SDL3_net/ENet symbol may ever appear here. Verify with:
```bash
grep -niE "enet|SDL_|SdlNet" include/dplay.h
```
(Expect zero matches; a pre-existing, unrelated hit in `include/dsound.h`'s doc comments is known
and out of scope.)

**Internal implementation** (`src/`):
- `src/directdraw/`, `src/directsound/` — SDL3-backed; not touched in recent sessions.
- `src/directplay/` — the actively-developed subsystem:
  - `DirectPlay.cpp` — public entry points (`DirectPlayCreate`, `DirectPlayEnumerateA/W`) and two
    anonymous-namespace classes: `DirectPlayImpl` (`IDirectPlay`, thin factory) and
    `DirectPlay2AImpl` (`IDirectPlay2A`, the real implementation, owning one `DirectPlaySession`
    member directly). `Open()` `#ifdef FREE_DIRECT_ENABLE_ENET`-selects the transport backend.
    Cannot be whiteboxed from external test files (no header, anonymous namespace).
  - `DirectPlaySession.hpp` — per-object state: lifecycle enum, `isHost`, local/remote player ID
    lists, `nextPlayerId` (DPID allocator, starts at `0`), session descriptor fields, an owned
    `transport` (`std::unique_ptr<IDirectPlayTransport>`), an owned `messageQueue`. Deliberately
    does not store a raw `DPSESSIONDESC2` (its string pointers are caller-owned).
  - `DirectPlayMessageQueue.hpp` — bounded FIFO + `TryReceive()`, the exact logic
    `DirectPlay2AImpl::Receive()` delegates to.
  - `DirectPlayTransport.hpp` — `IDirectPlayTransport` abstract interface: `Listen`/`Connect`/
    `Send`/`Receive`/`Service`/`HasPendingConnection`/`AssignPendingConnection`/
    `RejectPendingConnection`/`HasDisconnectedPeer`/`TakeDisconnectedPeer`/`Shutdown`.
    Backend-agnostic and byte-buffer-oriented; may know `DPID` as an opaque map key, but must
    never allocate one, validate one against session state, or decide whether to accept a
    connection — that policy stays in `DirectPlay2AImpl`/`DirectPlaySession`.
  - `LoopbackDirectPlayTransport.hpp`/`.cpp` — in-memory implementation, used in the default
    (`FREE_DIRECT_ENABLE_ENET=OFF`) build; all connection-oriented methods are trivial/`false`.
  - `EnetDirectPlayTransport.hpp`/`.cpp` — real ENet backend. Hosting (`Listen()`) and joining
    (`Connect()`) roles are asymmetric and tracked separately: `hostPeer_` (joining role, one
    `ENetPeer*`) vs. `connectedPeers_` (`DPID → ENetPeer*`, hosting role) / `pendingPeers_`
    (hosting role, not yet assigned a DPID) / `disconnectedPeerIds_` (hosting role). Private
    header — never included from `include/`, per `CLAUDE.md`'s Internal Backend Policy.
  - `DirectPlayWireProtocol.hpp`/`.cpp` — packet header type + serialize/deserialize; not yet
    constructed from a real session or consumed by any transport.

**Data flow for the one fully-FreeDirect-driven path (self-send, default build)**:
`Open(DPOPEN_CREATE)` → `LoopbackDirectPlayTransport` → `CreatePlayer` allocates a DPID →
`Send(id, id, ...)` round-trips through `transport->Send()`/`Receive()` → wrapped into a
`DirectPlayMessagePacket` → `messageQueue.Enqueue()` → `Receive()` calls
`messageQueue.TryReceive()` synchronously.

**Invariants that must not be broken**:
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- `plan.md` tasks are atomic (one thing each); phases build on each other in order.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX surface "for completeness" without asking first (`CLAUDE.md`).

## 7. Useful commands

Configure + build (default backend, no ENet):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```

Configure + build with the ENet transport backend enabled (vendored copy at `third_party/enet`):
```bash
cmake -B cmake-build-enet -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build cmake-build-enet -j4
```

Run the demo (not verified graphically in recent sessions):
```bash
./cmake-build-debug/FREE_DIRECT
```

Build + run the DirectPlay tests (standalone, not yet CTest-integrated):
```bash
g++ -std=c++20 -Wall -Wextra \
    -I include -I ../free-api/include -I ../free-api/include_non_windows \
    -I src/directplay \
    src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
    tests/directplay_tests.cpp \
    -o directplay_tests
./directplay_tests
```

No lint/format tooling is configured in this repository. There is no currently-known reproducible
bug (see Section 4).

## 8. Next smallest tasks

Basic real unicast delivery (host → one specific assigned client, and the reverse validation
failures) is done (Decisions 14-15). Several genuinely different directions are open next, all
within `plan.md` Phase 10 or unblocked by it - this is a real choice, not an obvious next step, so
ask the user rather than picking unilaterally:

1. **Host-side routing/forwarding**: a joining peer's `Send()` addressed to another (non-host)
   peer gets relayed by the host (star topology, `plan.md`'s own next Phase 10 checklist item).
   Needs its own design pass - e.g. does the host's `Receive()` drain loop need to distinguish "a
   packet addressed to me" from "a packet addressed to someone else that I should forward," and
   what happens to sender/recipient validation in that case.
2. **Broadcast** (`idTo == 0`/`DPID_ALLPLAYERS`, matching `free-eggbert`'s actual real call
   pattern - see `docs/directplay-callsite-audit.md`). **Must resolve the DPID-0 ambiguity first**
   (Section 5): Decision 3 assigned DPID `0` to the host's own first local player rather than
   reserving it, so `Send(m_dpid, 0, ...)` is ambiguous between "broadcast" and "the player whose
   DPID is `0`" under current semantics - this needs explicit user input, not a unilateral call.
3. **Phase 7's remaining tasks** (join-request/accepted wire handshake, DPID/session-descriptor
   agreement) - unblocked in principle now that per-DPID delivery exists, but still needs its own
   design pass for the handshake protocol itself (what triggers sending a join-accepted packet, how
   the joining side recognizes and processes one, etc.).
4. **Phase 8/9** (session enumeration / player management) remain available too, same as before.

Only one `plan.md` Phase 6 task remains, and it is still blocked (unchanged): "Add a test for
closing a host session" needs `EnumSessions()` to track real sessions first (`plan.md` Phase 8, not
started).

## 9. Do not do yet

- Do not refactor `DirectPlay.cpp`'s overall class structure — it works and is being incrementally
  extended by design.
- Do not touch DirectDraw or DirectSound source (`src/directdraw/`, `src/directsound/`) — separate
  subsystems on separate, not-yet-started `plan.md` phases (13/14).
- Do not modify game source in `../free-eggbert` or `../planetblupi` under any circumstances.
- Do not decide broadcast's DPID-0-vs-`DPID_ALLPLAYERS` ambiguity unilaterally (Section 5/8) - ask
  the user before implementing broadcast at all.
- Do not implement host-side routing/forwarding, broadcast, or the join-accepted/join-rejected wire
  packet tasks without first asking the user which of Section 8's several open directions to
  pursue - more than one real option exists now, it is not an obvious single next step.
- Do not decide the ENet backend's "how does a joining call resolve a host address" question
  unilaterally — ask the user first, same as Decision 5 did for the hosting side's port choice.
- Do not wire `tests/directplay_tests.cpp` into CMake/CTest yet (`plan.md` Phase 15, deliberately
  deferred until more of Phases 5-11 exist to test).
- Do not add a run-time backend-selection mechanism for `Open()`'s loopback-vs-ENet choice —
  `docs/directplay-design.md` Decision 4 explicitly chose build-time-only, and this was a
  deliberate rejection, not an open question.
- Do not add an SDL3_net backend (`plan.md` Phase 12 explicitly defers this until ENet is stable).
- Do not add DirectX API surface, flags, or behavior beyond what `../free-eggbert`/
  `../planetblupi` call sites actually require (`CLAUDE.md` scope policy) — ask before expanding.
- Do not attempt a mass rewrite or broad "cleanup" pass — this codebase is being built up
  incrementally, one atomic `plan.md` task at a time, each verified before the next.

## 10. Resume prompt

```
Read NEXT.md first, especially Section 8 (the next task) and docs/directplay-design.md Decisions
14-15. Real unicast Send()/Receive() delivery works end-to-end over loopback now, host role only -
a host can address one of its own remotePlayerIds directly. Several genuinely different
directions are open next (host-side routing, broadcast - which needs the DPID-0 ambiguity
resolved first, Phase 7's join-accepted handshake, or Phase 8/9) - this is a real choice, not an
obvious next step. Ask the user (AskUserQuestion) which to pursue before writing any code. Do not
refactor unrelated code, do not touch DirectDraw/DirectSound, and do not modify ../free-eggbert or
../planetblupi. Once a direction is chosen, implement one small atomic slice and run the relevant
build/test command from Section 7, confirming it actually passes before considering the task done.
Then update NEXT.md to reflect the new state (Sections 2, 3, 8, and 4/5 if anything changed there).
```
