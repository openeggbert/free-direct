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
  ("Session joining") has started: `LoopbackDirectPlayTransport` has a real multi-instance
  connection lifecycle (Decision 10), and `Open(..., DPOPEN_JOIN)` now calls `Connect()` over
  loopback (Decision 11) — but only the *failure* path works today (`DPERR_NOSESSIONS` when no
  host is listening, which is always, since nothing wires the hosting role to call `Listen()` yet —
  see Section 4). The ENet side of "resolve a host address" is also still undecided. Phases 8
  onward (enumeration, player management, real Send/Receive routing, error semantics, hardening,
  CI, docs) have not started.
- **Key architectural decisions** (`docs/directplay-design.md` has the full record, Decisions
  1-11):
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

**Test status: 28/28 passing.** `tests/directplay_tests.cpp` is a standalone file with its own
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
- (uncommitted, this session) — Wired `DirectPlay2AImpl::Open(..., DPOPEN_JOIN/DPOPEN_OPENSESSION)`
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
(Decisions 1-11) and in each commit's own message / `plan.md`'s per-task `**Done:**` annotations.

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists right now** — everything builds and 28/28 tests pass.
There is, however, a real **open design question** blocking Phase 7's "successful join" test:
a hosted loopback session's transport cannot both (a) call `Listen()` so a joining `Connect()` can
find it, and (b) keep self-sending working, since Decision 10 made `Send()`/`Receive()` return
`false` unconditionally once a transport is "connected" (listening or joined) — see Decision 11's
regression note. This needs its own design pass (does self-send move off the transport entirely and
talk to `session_.messageQueue` directly? Does "listening" need a sub-state that still permits the
old self-send path? Something else?) before a host+client loopback join can be tested end-to-end.
Flag this to the user before picking an approach — it's the kind of judgment call this project
asks about rather than deciding solo.

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
- **Incomplete**: transport-level `Receive()` (ENet backend) unconditionally returns `false`;
  received packets are destroyed by `Service()`, not delivered anywhere (Phase 10).
- **Incomplete**: `Send()`/`Receive()` cannot address one specific connected peer among many on a
  hosting-role instance (Phase 10) — this is also what blocks the join-accepted/join-rejected
  packet tasks in Phase 6.
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

`Open(..., DPOPEN_JOIN)` now calls `Connect()` over loopback (Decision 11), but only the failure
path is real — a successful join is blocked on a genuine open design question (Section 4). The
next smallest task is to resolve that, since nothing else in Phase 7 can proceed without it:

1. **Design (ask the user first) how a hosted loopback session's transport can be both
   `Listen()`ing (discoverable by a joining `Connect()`) and still able to self-send.**
   - Files to understand first: `src/directplay/DirectPlay.cpp` (`Send()`'s self-send path,
     `idTo == idFrom`), `src/directplay/LoopbackDirectPlayTransport.cpp` (`Send()`/`Receive()`'s
     `if (listening_ || hostPeer_) return false;` guard, Decision 10).
   - Candidate directions to present, not decide solo: (a) self-send stops going through
     `transport->Send()`/`Receive()` entirely once real connections exist, and talks to
     `session_.messageQueue` directly instead (transport only used for real inter-process-style
     traffic); (b) `Send()`/`Receive()` gain a narrower guard that still permits the exact
     `idFrom == idTo` self-send shape even while `listening_`; (c) something else. Each has
     different consequences for later Phase 10 addressed-send work — flag that tradeoff too.
   - Only after that's decided: wire `Open(..., DPOPEN_CREATE)` to call
     `transport->Listen(kDefaultDirectPlayLoopbackPort)` for loopback (symmetric to the joining
     wiring already done), then attempt Phase 7's "Add a test for a successful join using a local
     host/client pair over loopback" task.
   - Do not implement the join-request/join-accepted wire handshake itself yet (blocked on
     per-DPID-addressed `Send()`, Phase 10 — see Section 9) — a successful "join" for now would only
     mean the client's `Connect()` succeeds and the host sees it as a pending connection, not a
     full DPID-assignment handshake.

Only one `plan.md` Phase 6 task remains, and it is still blocked (unchanged from before): "Add a
test for closing a host session" needs `EnumSessions()` to track real sessions first (`plan.md`
Phase 8, not started).

## 9. Do not do yet

- Do not refactor `DirectPlay.cpp`'s overall class structure — it works and is being incrementally
  extended by design.
- Do not touch DirectDraw or DirectSound source (`src/directdraw/`, `src/directsound/`) — separate
  subsystems on separate, not-yet-started `plan.md` phases (13/14).
- Do not modify game source in `../free-eggbert` or `../planetblupi` under any circumstances.
- Do not implement general `Send()`/`Receive()` routing to a specific connected peer, host
  forwarding, or broadcast (`plan.md` Phase 10) until per-DPID addressing is designed.
- Do not implement the join-accepted/join-rejected wire packet tasks before per-DPID-addressed
  `Send()` exists — they need it and will need their own design pass.
- Do not make `LoopbackDirectPlayTransport::Send()`/`Receive()` return real data once connected —
  Decision 10 already decided `false` for both roles; real payload delivery is a separate, later
  design decision, not something to revisit incidentally while wiring `Open(DPOPEN_JOIN)`.
- Do not decide the ENet backend's "how does a joining call resolve a host address" question
  unilaterally — ask the user first, same as Decision 5 did for the hosting side's port choice.
- Do not wire `Open(..., DPOPEN_CREATE)` to call `transport->Listen()` over loopback without first
  resolving Section 4's open design question (self-send vs. "listening" conflict) — it was tried
  and reverted once already (Decision 11) because it silently broke every self-send test.
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
Read NEXT.md first, especially Section 4 (the current blocker) and Section 8 (the next task).
Open(..., DPOPEN_JOIN) already calls transport->Connect() over loopback and correctly returns
DPERR_NOSESSIONS when nothing is listening - but nothing wires the hosting role to call Listen()
yet, because doing so naively breaks the self-send feature every Phase 4 test depends on
(LoopbackDirectPlayTransport::Send()/Receive() return false once "listening", per Decision 10).
Before writing any code, design (with the user - use AskUserQuestion, present at least two
candidate directions) how a hosted loopback session can be both discoverable via Listen() and
still able to self-send. Only after that's decided, wire Open(..., DPOPEN_CREATE) to call
Listen() over loopback and add Phase 7's "successful join" test. Do not refactor unrelated code,
do not touch DirectDraw/DirectSound, and do not modify ../free-eggbert or ../planetblupi. Do not
implement the join-request/join-accepted wire handshake yet (Section 9 - blocked on per-DPID-
addressed Send(), Phase 10). Make one small, verified improvement - implement just that one task.
Run the relevant build/test command from Section 7 and confirm it actually passes before
considering the task done. Then update NEXT.md to reflect the new state (Sections 2, 3, 8, and
4/5 if anything changed there).
```
