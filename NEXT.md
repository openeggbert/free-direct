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
  well underway — most sub-tasks done and verified (see Section 3); a few remain (Section 8).
  Phases 7 onward (session joining, enumeration, player management, real Send/Receive routing,
  error semantics, hardening, CI, docs) have not started.
- **Key architectural decisions** (`docs/directplay-design.md` has the full record, Decisions 1-9):
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

**Test status: 15/15 passing.** `tests/directplay_tests.cpp` is a standalone file with its own
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
- (uncommitted, this session) — Added `Test_OpenAsHostOverLoopback_ReturnsOk`
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
(Decisions 1-9) and in each commit's own message / `plan.md`'s per-task `**Done:**` annotations.

## 4. Current blocker / main problem

**No build- or test-breaking blocker exists right now.** The last debugging investigation (the
Decision 9 rejection logic appearing not to work) turned out to be a test-harness bug, already
fixed; the production code is verified correct. There is currently no open design decision either.

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

Remaining `plan.md` Phase 6 tasks, in order:

1. **Add a test for invalid host parameters** (e.g. malformed `DPSESSIONDESC2.dwSize`), asserting
   a meaningful `DPERR_*` rather than `DP_OK`.
   - Files: `tests/directplay_tests.cpp`, possibly `DirectPlay.cpp`'s `Open()` validation if a gap
     is found.
   - Note: `dwMaxPlayers == 0` is a real, intentional "no limit" case (Decision 9), not an error —
     re-read `Open()`'s current validation logic first so the new test doesn't assert against
     already-shipped, correct behavior.
   - Verify: same as above.

2. **Add a test for closing a host session**, asserting a subsequent `EnumSessions` from another
   loopback peer no longer finds it.
   - Blocked on real content: `EnumSessions()` doesn't track sessions yet (`plan.md` Phase 8, not
     started). Do this task after Phase 8's basic discovery exists, or it will only be testing "an
     empty list stays empty," which isn't meaningful.

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
Read NEXT.md first. Inspect only the files needed for the first task in Section 8 (currently:
adding a loopback host-session-creation test in tests/directplay_tests.cpp). Do not refactor
unrelated code, do not touch DirectDraw/DirectSound, and do not modify ../free-eggbert or
../planetblupi. Make one small, verified improvement - implement just that one task. Run the
relevant build/test command from Section 7 and confirm it actually passes before considering the
task done. Then update NEXT.md to reflect the new state (Sections 2, 3, 8, and 4/5 if anything
changed there).
```
