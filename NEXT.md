# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is explicitly **not** an attempt at full DirectX compatibility —
scope is bounded by what the two target games' real call sites need (see `CLAUDE.md`, the project
charter).

- **Target games** (sibling repos, present on disk): `../free-eggbert` (*Speedy Blupi* —
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi* — DirectDraw +
  DirectSound only; confirmed zero DirectPlay usage by grep).
- **Main goal**: two programs both built against FreeDirect's own DirectPlay implementation can
  host/join/exchange messages with each other. This is explicitly **not** wire-compatible with
  real Microsoft DirectPlay — FreeDirect-to-FreeDirect only.
- **Current development phase**: `plan.md` Phases 0–9 are complete or have no more reachable
  tasks over the **loopback** transport backend (in-process, no real sockets — used in the
  default, non-ENet build). Phase 10 (Send/Receive networking) is partially done — host→one
  specific client unicast works over loopback; broadcast and host-side routing do not exist yet.
  ENet (the real-socket backend) has fallen behind loopback across almost every capability and is
  the subject of the session's current "ENet catch-up" direction — its single biggest gap
  (receive-side packet delivery) was just closed. Phases 11–18 (error semantics, SDL3_net,
  DirectSound/DirectDraw hardening, CTest/CI, documentation, final validation) have **not
  started at all**.
- **Important architectural decisions** (full narrative + rationale for all of these lives in
  `docs/directplay-design.md`, Decisions 1–19 — read that file for the "why," not just the "what"):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only. No SDL3/ENet/SDL3_net symbol may ever appear in them (Internal Backend Policy).
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`), with two concrete backends selected at
    **build time** (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option:
    `LoopbackDirectPlayTransport` (in-process, deterministic, used for all committed tests) and
    `EnetDirectPlayTransport` (real ENet UDP sockets).
  - DPID `0` is assigned to the host's own first local player (not reserved as
    `DPID_ALLPLAYERS`), matching `free-eggbert`'s own comparison pattern — a deliberate deviation
    from real DirectPlay that creates a real, unresolved ambiguity for broadcast (`idTo == 0`),
    see Section 5.
  - Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
    transport entirely — it must not depend on, or be affected by, the transport's connection
    state.

## 2. Current status

**Build status: working**, both configurations, verified clean at the current commit
(`ae484a1`, `develop` branch):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON && cmake --build cmake-build-debug -j4
cmake -B cmake-build-enet -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON && cmake --build cmake-build-enet -j4
```
Zero warnings on changed files. `grep -niE "enet|SDL_|SdlNet" include/dplay.h` finds zero matches
(the one hard architectural rule this project checks by hand on every change).

**Test status: 46/46 passing.** `tests/directplay_tests.cpp` is a standalone file with its own
`main()`, **not wired into CMake/CTest yet** (`plan.md` Phase 15, not started). Build/run command
in Section 7. These tests only exercise the default (`FREE_DIRECT_ENABLE_ENET=OFF`, loopback)
build — no committed test exercises the ENet backend at all today.

**Available artifacts**:
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`) exercising DirectDraw surfaces/blits/palette;
  compiles, but **has not been run/observed graphically** in this or recent sessions (no display
  verification performed — see Section 5).
- `tests/directplay_tests.cpp` — standalone DirectPlay unit/integration test binary, build/run
  manually per Section 7 (not CTest-wired).

**Recently implemented and verified** (this session; see Section 3 for the full list):
- Real DirectPlay session hosting, joining, a join-request/join-accepted handshake, unicast
  `Send`/`Receive` to one specific real remote player, `dwMaxPlayers` player-count validation, and
  real `EnumSessions()` discovery — all working end-to-end over the **loopback** backend, verified
  by the 46 committed tests (real public `IDirectPlay2A` API calls, not whitebox, for the
  end-to-end ones).
- Real ENet receive-side packet delivery (`EnetDirectPlayTransport::Receive()`/`Service()`) —
  verified with an uncommitted, real two-socket smoke test over `127.0.0.1`, not just a compile
  check.

**What does not work yet** (see Section 5 for full detail):
- Over ENet: `Open()` never calls `Connect()` for the joining role (only `Listen()` for hosting);
  no join handshake; not discoverable via `EnumSessions()`. The transport can now deliver bytes
  (just added), but nothing in `DirectPlay.cpp` uses that for ENet yet.
- Over loopback (and therefore everywhere): host-side routing between two non-host peers;
  broadcast (`idTo == 0`); a real `JoinReject` explanation packet (structurally blocked, not just
  unimplemented — see Section 5); player name storage; a distinct "player-lost" vs. clean-removal
  state; duplicate-player validation.
- DirectSound/DirectDraw hardening (`plan.md` Phases 13–14) — not started, separate subsystems,
  untouched this session.
- CTest/CI integration (`plan.md` Phase 15) — not started.
- The `FREE_DIRECT` demo executable's actual on-screen behavior — unverified.

## 3. Recent changes

This session implemented DirectPlay session hosting/joining/messaging/discovery over loopback
from near-scratch, then began ENet catch-up. In commit order (oldest → newest), each closing a
`plan.md` task and documented as a numbered Decision in `docs/directplay-design.md`:

- **Decision 10** — gave `LoopbackDirectPlayTransport` a real multi-instance connection lifecycle
  (a process-wide static registry keyed by port), so a separate client instance can find and
  connect to a separate host instance in the same test process.
- **Decision 11** — wired `Open(..., DPOPEN_JOIN)` to call `transport->Connect()` over loopback.
- **Decision 12** — made self-send bypass the transport entirely (fixing a real regression found
  while wiring `Open(..., DPOPEN_CREATE)` to call `Listen()`, which would otherwise have silently
  broken the pre-existing self-send feature).
- **Decision 13** — added `IDirectPlayTransport::IsConnectedToHost()` so a rejected/disconnected
  joining client can observe it (`DPERR_NOCONNECTION` on its next `Receive()`).
- **Decision 14** — `IDirectPlayTransport::Send()` gained a `DPID targetId` parameter, so a
  hosting instance can address one specific connected peer among potentially many.
- **Decision 15** — wired `DirectPlay2AImpl::Send()`/`Receive()` to actually use Decision 14's
  capability: real unicast delivery to one specific already-assigned remote player, with
  `idFrom`/`idTo` validation and an oversized-payload check.
- **Decision 16** — implemented the join-request/join-accepted handshake: the joining client
  sends a `Join` packet, the host responds with `JoinAccept` (containing the assigned DPID), and
  the client adopts it as its own local player identity. Asynchronous by design, not blocking.
- **Decision 17** — `CreatePlayer()` now validates `dwMaxPlayers` before allocating a DPID
  (`DPERR_CANTCREATEPLAYER` when full). Also investigated and explicitly deferred (not
  implemented) player-name storage, a distinct player-lost state, and duplicate-player
  validation — all three lack either a concrete definition or any way to observe the result via
  the public API.
- **Decision 18** — real `EnumSessions()` over loopback via a synchronous, DirectPlay-level static
  registry (separate from Decision 10's transport-level one), with real `guidApplication`/
  `DPENUMSESSIONS_AVAILABLE` filtering. This also finally closed `plan.md` Phase 6's last
  remaining task (a closed session no longer appears in `EnumSessions()`).
- **Decision 19** (latest, `ae484a1`) — real ENet receive-side buffering:
  `EnetDirectPlayTransport::Service()` now copies received packet bytes into an inbox instead of
  discarding them, and `Receive()` pops from it for real. This was the single biggest gap blocking
  any ENet-backed capability (nothing above the transport layer could ever receive anything over
  a real socket before this). Verified with an uncommitted two-instance smoke test over real
  `127.0.0.1` sockets — the first attempt at that smoke test failed for a real, instructive
  reason (only serviced one side of the handshake), not a bug in the implementation.

Every commit above kept the 46-test suite green, kept both CMake configs building clean, and kept
`include/dplay.h` free of ENet/SDL identifiers. Several of these decisions were made only after
asking the user to choose between concrete alternatives (recorded in each Decision's own "Status"
line in `docs/directplay-design.md`).

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker.** Everything builds, all 46 committed tests pass.

The "problem," such as it is, is a **scope/design fork with no single obvious next step** — every
remaining direction needs a human decision before more code should be written:
- Which ENet catch-up task next (wiring `Connect()` for joining, the join handshake, discovery,
  or a committed reliable-delivery smoke test) — each needs its own small ENet-specific design
  answer (e.g. how a joining ENet call resolves a host address at all, since `DPSESSIONDESC2` has
  no address field — open since Decision 5).
- Whether to tackle broadcast (`idTo == 0`) next, which requires resolving a real semantic
  ambiguity first: DPID `0` is both a real, valid player ID (Decision 3) and `free-eggbert`'s own
  broadcast convention (`Send(m_dpid, 0, ...)`) — these two facts are in tension and nobody has
  decided which wins yet.
- Whether to pursue Phase 9's leftover questions (player-name observability, a player-lost state,
  what "duplicate player" means), which are still open per Decision 17.

No suspected cause/failing command applies here — this is a "what to build next," not a "why is
this broken" situation.

**Minor, non-blocking**: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` (the system-package ENet path, as
opposed to the vendored `third_party/enet` submodule) has never been exercised successfully in
this environment (no `libenet` system package installed here). Not currently blocking anything.

## 5. Known bugs and limitations

- **Incomplete**: `DirectPlay.cpp`'s ENet branch of `Open()` never calls `Connect()` for the
  joining role (only `Listen()` for hosting). Transport-level receive now genuinely works
  (Decision 19), but nothing above it uses ENet for joining, the join handshake, unicast
  `Send()`/`Receive()`, or `EnumSessions()` yet — all of that is loopback-only today.
- **Incomplete**: a joining-role session still cannot address any specific *other* remote player
  in `Send()` (always `DPERR_INVALIDPLAYER`) — even after adopting its own host-assigned DPID, it
  has no way to learn any other peer's DPID (the host's, or another client's). Would need a
  player-list sync mechanism that doesn't exist yet (Phase 9).
- **Structurally blocked, not just unimplemented**: a real `JoinReject` explanation packet for an
  over-`dwMaxPlayers` rejection. A rejected pending peer is never assigned a DPID, and addressed
  `Send()` only ever reaches *assigned* peers — there is nothing to address such a packet to,
  regardless of future work, unless pending-peer addressing is added as its own separate feature.
- **Incomplete**: no host-side routing/forwarding — a `Send()` addressed to a non-host recipient
  by a joining peer isn't relayed by the host at all (Phase 10, not started).
- **Incomplete, real unresolved ambiguity**: broadcast (`idTo == 0`/`DPID_ALLPLAYERS`) isn't
  implemented. Decision 3 assigned DPID `0` to the host's own first local player rather than
  reserving it — but `free-eggbert`'s only real `Send()` call site (`Send(m_dpid, 0, ...)`, per
  `docs/directplay-callsite-audit.md`) always uses `0` to mean *broadcast*. Whoever implements
  broadcast must resolve "is `0` broadcast, or the specific player who happens to have DPID `0`?"
  — this is not decided, and should not be assumed either way.
- **Deliberately deferred, not a bug**: player short/long name storage. `../free-eggbert`'s
  `CreatePlayer()` call sites do supply a real short name, so this isn't out of scope by the
  two-game rule — but `IDirectPlay2A` has no `GetPlayerName`-style method and `free-eggbert` never
  calls one either, so a stored name would be permanently unobservable by anything (no public
  getter, no whitebox test path for `DirectPlaySession` unlike `LoopbackDirectPlayTransport`).
  Needs a conversation about whether/how to make it observable before any storage code lands.
- **Needs a concrete definition from a human, not implemented**: "validate against duplicate
  players" has no clear target — `CreatePlayer()`'s sequential DPID allocator makes every DPID
  unique by construction, and no other field defines caller identity to dedupe against.
  `free-eggbert` itself only calls `CreatePlayer()` once per connection anyway.
- **Deliberately deferred, same observability gap as player names**: a distinct "player-lost"
  state (transport disconnect without an explicit `Close`) vs. a clean removal — nothing in the
  current `IDirectPlay2A` subset could ever query this distinction even if it were tracked.
- **Incomplete**: `EnumSessions()` only discovers a *loopback*-hosted session. A session hosted
  over ENet is not discoverable at all yet — would need a real Discovery/DiscoveryResponse wire
  exchange (packet types already defined in `DirectPlayWireProtocol.hpp`, unused).
- **Not provably tested, honestly flagged, not a defect**: `EnumSessions()`'s
  callback-returns-`FALSE`-stops-enumeration behavior is implemented (a `break` statement) but
  cannot be exercised by a real test today — only one loopback-hosted session can exist per
  process at a time (a deliberate, documented constraint), so there is no way to construct a
  genuine two-simultaneous-sessions scenario to prove the `break` actually matters.
- **Incomplete**: no CTest/CI wiring for `tests/directplay_tests.cpp` (`plan.md` Phase 15).
- **Needs verification**: `FREE_DIRECT_USE_SYSTEM_ENET=ON` — logic written and reviewed, never
  actually exercised against a real `libenet` install in this environment.
- **Risky assumption**: in `Send()`'s self-send path, any `DirectPlayMessageQueue::Enqueue()`
  failure (queue full **or** oversize payload) currently returns `DPERR_SENDTOOBIG` — imprecise
  for the "queue full" case specifically.
- **Risky assumption**: `Open()` validates `dwFlags` against the `DPOPEN_CREATE`/`DPOPEN_JOIN`/
  `DPOPEN_OPENSESSION` mask but does not enforce that `DPOPEN_CREATE` and `DPOPEN_JOIN` are
  mutually exclusive. Not currently exercised by any known call site.
- **Confirmed, low priority**: `GUID::Data1` is `unsigned long` — 8 bytes on this (Linux/64-bit)
  platform, not the 4 bytes real DirectPlay's `Data1` documents, so `sizeof(GUID)` is 24 here, not
  16. Only matters for cross-platform wire-compatible `GUID` serialization, which is not a current,
  demonstrated need.
- **Confirmed, not a FreeDirect bug**: `../free-eggbert`'s own DirectPlay lobby/session UI
  (`WM_PHASE_DP_*` handlers in `src/event.cpp`) is unwired in the game's current source —
  `NetCreate`/`NetEnumSessions`/`JoinSession`/`CreateSession`/`NetStartPlay` have zero callers
  anywhere in that game. Only gameplay-time `Send`/`Receive` are reachable in the live game. Out
  of `free-direct`'s scope to fix (game source must never be modified).
- **Unknown**: whether the `FREE_DIRECT` demo executable actually runs and renders correctly on a
  real display — it compiles, but has not been run graphically in this or recent sessions.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped types/constants/
interfaces only. **Hard invariant, enforced by convention + manual grep** (not yet automated in
CI): no SDL3/SDL3_net/ENet symbol may ever appear here.
```bash
grep -niE "enet|SDL_|SdlNet" include/dplay.h   # must report zero matches
```
(A pre-existing, unrelated hit in `include/dsound.h`'s doc comments is known and out of scope.)

**Internal implementation** (`src/directplay/`, the actively-developed subsystem):
- `DirectPlay.cpp` — public entry points (`DirectPlayCreate`, `DirectPlayEnumerateA/W`) and two
  anonymous-namespace classes: `DirectPlayImpl` (`IDirectPlay`, thin factory) and
  `DirectPlay2AImpl` (`IDirectPlay2A`, the real implementation, owning one `DirectPlaySession`
  member directly). `Open()` `#ifdef FREE_DIRECT_ENABLE_ENET`-selects the transport backend at
  **build time**, never at runtime. Not whiteboxable from external test files (no header,
  anonymous namespace) — all tests exercise it through the real public API.
- `DirectPlaySession.hpp` — per-object state: lifecycle enum, `isHost`, local/remote player ID
  lists, `nextPlayerId` (sequential DPID allocator starting at `0`), session descriptor scalar
  fields, an owned `transport` (`std::unique_ptr<IDirectPlayTransport>`), an owned
  `messageQueue`. Deliberately does **not** store a raw `DPSESSIONDESC2` (its string pointers are
  caller-owned and may not outlive the `Open()` call).
- `DirectPlayMessageQueue.hpp` — bounded FIFO + `TryReceive()`, the exact logic
  `DirectPlay2AImpl::Receive()` delegates to for the final buffer-size/copy/dequeue step.
- `DirectPlayTransport.hpp` — `IDirectPlayTransport` abstract interface: `Listen`/`Connect`/
  `Send(targetId, ...)`/`Receive`/`Service`/`HasPendingConnection`/`AssignPendingConnection`/
  `RejectPendingConnection`/`HasDisconnectedPeer`/`TakeDisconnectedPeer`/`IsConnectedToHost`/
  `Shutdown`. Backend-agnostic and byte-buffer-oriented; may know `DPID` only as an opaque map
  key — must never allocate one, validate one against session state, or decide whether to accept
  a connection. That policy lives in `DirectPlay2AImpl`/`DirectPlaySession`, never in a transport.
- `LoopbackDirectPlayTransport.hpp`/`.cpp` — in-process backend (default, `FREE_DIRECT_ENABLE_ENET`
  off). A process-wide static registry (keyed by a fixed port constant) lets a separate "client"
  instance find and connect to a separate "host" instance in the same process. Real connection
  lifecycle (pending/connected/disconnected peers), real addressed delivery. Only one
  loopback-hosted session can exist per process at a time — a known, deliberate constraint.
- `EnetDirectPlayTransport.hpp`/`.cpp` — real ENet backend. Hosting (`Listen()`) and joining
  (`Connect()`) roles are asymmetric: `hostPeer_` (joining role) vs. `connectedPeers_`/
  `pendingPeers_`/`disconnectedPeerIds_` (hosting role). Now has real receive-side buffering too
  (`buffered_`, mirroring the loopback transport's shape). Private header — never included from
  `include/`, per the Internal Backend Policy.
- `DirectPlayWireProtocol.hpp`/`.cpp` — packet header type (`Join`/`JoinAccept`/`JoinReject`/
  `Data`/`Discovery`/`DiscoveryResponse`) + serialize/deserialize. Consumed by the join handshake
  and unicast `Send`/`Receive` over loopback; still unused by the ENet backend's own higher-level
  wiring (only its raw transport layer is real now).
- `DirectPlay2AImpl` also owns two separate process-wide static registries in `DirectPlay.cpp`'s
  anonymous namespace: one mapping a live `DirectPlaySession*` for `EnumSessions()` to read
  directly and synchronously (Decision 18) — deliberately **separate** from
  `LoopbackDirectPlayTransport`'s own port registry, so DirectPlay-level concepts never leak into
  the transport layer.

**Data flow, self-send** (works regardless of transport/backend): `Send(id, id, ...)` constructs
a `DirectPlayMessagePacket` directly and calls `messageQueue.Enqueue()` — never touches the
transport at all. `Receive()` calls `messageQueue.TryReceive()`.

**Data flow, real unicast delivery over loopback** (host → one specific assigned remote player):
`Send(idFrom, idTo, ...)` validates both IDs, serializes a `DirectPlayWirePacketHeader` + payload,
calls `transport->Send(idTo, wireBytes, ...)` which reaches into the *target instance's own*
inbox directly. The recipient's `Receive()` drains its transport inbox, deserializes each header,
and enqueues real `Data`-type packets into its own `messageQueue`; `JoinAccept`-type packets are
intercepted before that point to update local session state (adopted DPID, GUIDs) instead.

**Invariants that must not be broken**:
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- `plan.md` tasks are atomic (one thing each); phases build on each other in order.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX surface "for completeness" without asking a human first (`CLAUDE.md`'s core policy).
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never add a runtime switch.

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
(`cmake-build-enet` is a scratch build dir, not committed — recreate/delete freely.)

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

Check the public-header/backend-leak invariant:
```bash
grep -niE "enet|SDL_|SdlNet" include/dplay.h   # must report zero matches
```

No lint/format tooling is configured in this repository. There is no currently-known reproducible
bug (see Section 4 — the current situation is "what to build next," not "something is broken").

## 8. Next smallest tasks

Every item below is a genuine choice, not an obvious default — confirm direction with the user
before starting any of them (see Section 4).

1. **Wire `Open()`'s ENet branch to call `Connect()` for the joining role.**
   - Goal: a joining `Open(..., DPOPEN_JOIN)` call over ENet actually attempts to connect, instead
     of doing nothing (today only the hosting role calls `Listen()`).
   - Files: `src/directplay/DirectPlay.cpp` (`Open()`'s `#ifdef FREE_DIRECT_ENABLE_ENET` branch).
   - Needs its own small design decision first: how does a joining ENet call learn a host address
     to dial, given `DPSESSIONDESC2` has no address-like field (the same problem Decision 5 solved
     for `Listen()`'s port)? Mirrors Decision 11's loopback shape once that's answered.
   - Verification: a real, uncommitted two-process or two-thread ENet smoke test over
     `127.0.0.1` (not just a compile check) — this project's own established practice for ENet
     changes, since "compiles" has repeatedly turned out not to mean "works" for socket code.

2. **Run the join-request/join-accepted handshake over ENet** (depends on task 1).
   - Goal: mirror Decision 16's loopback handshake — the joining side sends `Join`, the host
     responds `JoinAccept` with the assigned DPID, the client adopts it.
   - Files: `src/directplay/DirectPlay.cpp` (the same `Open()`/`Receive()` code paths Decision 16
     already touched for loopback, extended to also run when `FREE_DIRECT_ENABLE_ENET` is on).
   - Verification: real ENet smoke test, not just loopback tests.

3. **Make an ENet-hosted session discoverable via `EnumSessions()`** (independent of tasks 1–2).
   - Goal: mirror Decision 18's loopback discovery, but for a real network — this genuinely needs
     a real Discovery/DiscoveryResponse wire round-trip this time (loopback could take a
     synchronous-registry shortcut; a real network fundamentally cannot).
   - Files: `src/directplay/DirectPlay.cpp` (`EnumSessions()`), `src/directplay/
     DirectPlayWireProtocol.hpp` (packet types already defined, unused).
   - Verification: real ENet smoke test with two real processes/instances.

4. **Commit a real reliable-delivery smoke test gated behind `FREE_DIRECT_ENABLE_ENET`.**
   - Goal: `plan.md` Phase 10 already lists this as an explicit task; formalize this session's
     uncommitted Decision 19 smoke test into a real, committed, CTest-or-manually-run test so ENet
     receive-side delivery has permanent regression coverage (today only loopback has committed
     tests).
   - Files: likely a new `tests/enet_directplay_tests.cpp` or similar, gated so it never runs in
     the default (no-ENet) build/test path.
   - Verification: build with `-DFREE_DIRECT_ENABLE_ENET=ON`, run the new test binary.

Other open directions, each needing its own human decision before starting (not ordered — pick
based on priority): host-side routing/forwarding between two non-host peers; broadcast
(`idTo == 0`, blocked on the DPID-0 ambiguity in Section 5); Phase 9's three deferred questions
(player-name observability, a player-lost state, duplicate-player validation); LAN broadcast
discovery (Phase 8, explicitly requires asking first per `plan.md`).

## 9. Do not do yet

- **No broad refactor.** `DirectPlay.cpp`'s overall class structure works and is being extended
  incrementally by design — do not restructure it "while you're in there."
- **No DirectDraw/DirectSound work.** Separate, working subsystems on separate, not-yet-started
  `plan.md` phases (13/14) — out of scope for the current DirectPlay-focused work.
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances,
  for any reason.
- **Do not decide the broadcast DPID-0-vs-`DPID_ALLPLAYERS` ambiguity unilaterally** — ask the
  user before implementing broadcast at all (Section 4/5).
- **Do not implement a `JoinReject` explanation packet for the over-`dwMaxPlayers` case** — it is
  structurally blocked (Section 5), not just unimplemented; don't attempt a workaround without a
  design conversation about pending-peer addressing first.
- **Do not implement player name storage** without a conversation about observability first
  (Section 5) — storing it today would be untestable dead code.
- **Do not strike Phase 9's "only if a call site needs it" tasks from `plan.md`** without
  confirming with the user first, even though Phase 0's audit already found no call site needs
  them — editing `plan.md` to mark tasks as intentionally-skipped is still a real change worth a
  heads-up.
- **Do not start LAN broadcast discovery** without asking first — `plan.md` says so explicitly.
- **Do not decide the ENet "how does a joining call resolve a host address" question
  unilaterally** — ask first, same as every other transport-shape decision this session.
- **Do not wire `tests/directplay_tests.cpp` into CMake/CTest yet** — `plan.md` Phase 15 is
  deliberately deferred until more of Phases 5–11 exist to test.
- **Do not add a run-time backend-selection mechanism** for loopback-vs-ENet — build-time-only via
  `FREE_DIRECT_ENABLE_ENET` was a deliberate, already-made decision, not an open question.
- **Do not add an SDL3_net backend** — explicitly deferred until ENet is stable (`plan.md` Phase
  12).
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule (`CLAUDE.md`). Ask first.
- **No mass rewrites or speculative architecture changes.** This codebase is built up
  incrementally, one atomic `plan.md` task at a time, each verified before the next — that
  discipline is why the test suite and both build configs have stayed green through 19 rounds of
  architectural change this session alone.

## 10. Resume prompt

```
Read NEXT.md first, especially Sections 4 and 8. Every remaining direction (ENet catch-up tasks,
broadcast, host-side routing, Phase 9's deferred questions) needs an explicit human decision before
any code is written - there is no single obvious next step. Ask which direction to pursue before
touching anything. Once a direction is chosen, inspect only the files needed for that one task
(Section 8 names them). Do not refactor unrelated code, do not touch DirectDraw/DirectSound source,
and do not modify ../free-eggbert or ../planetblupi under any circumstances. Make one small,
verified improvement - implement just that one task, atomically. If it touches the ENet backend,
verify with a real (uncommitted, unless the task is specifically to commit one) smoke test over
real sockets, not just a compile check - "compiles" has repeatedly turned out not to mean "works"
for ENet code in this project. Run the relevant build/test command from Section 7 and confirm it
actually passes before considering the task done. Then update NEXT.md to reflect the new state
(at minimum Sections 2, 3, and 8; also 4/5 if the blocker or known-limitations list changed).
```
