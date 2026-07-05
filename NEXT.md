# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so that specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is not an attempt at full DirectX compatibility.

- **Main goal**: support exactly two named target games — `../free-eggbert` (*Speedy Blupi*, uses
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi*, uses DirectDraw +
  DirectSound, confirmed zero DirectPlay usage). Scope is bounded by what these two games'
  real call sites actually need, not by DirectX API coverage in general (`CLAUDE.md`).
- **Current development phase**: `plan.md` Phase 5 ("ENet integration planning"), in progress.
  Phases 0-4 are complete.
- **Important architectural decisions**:
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only — no SDL3/ENet/SDL3_net type or symbol may ever appear in them.
  - SDL3 is the internal DirectDraw/DirectSound backend; ENet is the preferred DirectPlay network
    transport backend (over SDL3_net), chosen for built-in reliable UDP, ordering, and peer
    management.
  - DirectPlay is explicitly **not** Microsoft-wire-compatible. The only compatibility goal is
    FreeDirect-to-FreeDirect: two programs built against this same DirectPlay implementation must
    be able to host/join/exchange messages with each other.
  - Transport is abstracted behind `IDirectPlayTransport` (`src/directplay/DirectPlayTransport.hpp`),
    with `LoopbackDirectPlayTransport` (implemented, in-process, no sockets) and
    `EnetDirectPlayTransport` (class skeleton only - every method an honest `false`/no-op stub, not
    yet selected by `Open()`) as concrete backends.
  - `plan.md` is the authoritative English task list (every task atomic, one thing each);
    `CLAUDE.md` is the standing project charter/policy; this file (`NEXT.md`) is the living status
    snapshot.

## 2. Current status

**Build status**: the full CMake build works, but requires a non-default flag in this environment
because `free-direct` has no vendored SDL3 of its own:

```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```

This was broken for most of this session (a CMake target-visibility bug) and was fixed; see
Section 3. `free-api`, `free-direct` (static lib), and the `FREE_DIRECT` demo executable all
compile and link successfully with the command above. Adding `-DFREE_DIRECT_ENABLE_ENET=ON` also
now builds successfully, against a real vendored ENet copy, and now also compiles the new
`EnetDirectPlayTransport.cpp` skeleton (see Section 3). Both configurations (`ENET=OFF` default and
`ENET=ON`) were freshly configured and built end-to-end this session to confirm this.

**Test status**: `tests/directplay_tests.cpp` is a standalone, dependency-light file with its own
`main()` — **not yet wired into CMake/CTest** (that is `plan.md` Phase 15, not started). Last
confirmed run (manual, per the file's own documented build command): **11/11 tests passing**
(added this session: a wire-header serialize/deserialize round-trip test, plus three tests for the
validating `TryDeserializeDirectPlayWireHeader` - truncated buffer, mismatched payload length,
and the accept case). No other automated tests exist in the repository.

**CLI/tools/apps/libraries currently available**:
- `libfree-direct.a` (static library) — the compatibility layer itself.
- `FREE_DIRECT` — a demo executable (`src/Main.cpp`) exercising DirectDraw surfaces/blits/palette;
  it compiles, but was not run/observed graphically in this session (no display verification was
  performed, only compilation).
- `tests/directplay_tests.cpp` — build/run manually per its own header comment; not an installed
  tool.

**Recently implemented features** (this session, in order): a full `plan.md`/`CLAUDE.md` planning
pass; a real DirectPlay call-site audit (`docs/directplay-callsite-audit.md`); `plan.md` Phases
1-4 fully implemented (COM `QueryInterface`/`DirectPlayCreate` correctness fixes, a real
`DirectPlaySession` state model, a real FIFO `DirectPlayMessageQueue`, and a working
`LoopbackDirectPlayTransport` supporting self-send/receive); the SDL3 build blocker fixed; `plan.md`
Phase 5's CMake option/detection infrastructure for ENet, plus a real ENet copy now vendored and
verified to build and function correctly; the internal DirectPlay wire packet header
(`DirectPlayWireProtocol.hpp`) added as a pure data structure with a passing round-trip test, plus
defensive receive-side size/length validation (`TryDeserializeDirectPlayWireHeader`); the
`EnetDirectPlayTransport` class skeleton (stub methods only, not yet selected by `Open()`).

**What does not work yet / is not implemented**:
- `EnetDirectPlayTransport` — **class skeleton only**: `Listen`/`Connect`/`Send`/`Receive` all
  return `false`, `Shutdown` no-ops, and no ENet host/peer is ever created (`enet_initialize` is
  not even called yet - that's the next task). `DirectPlay2AImpl::Open()` still unconditionally
  uses `LoopbackDirectPlayTransport`; nothing constructs an `EnetDirectPlayTransport` anywhere.
  There is currently no networked (cross-process) DirectPlay of any kind — only same-object
  loopback self-send works.
- The wire packet header (`DirectPlayWirePacketHeader`) exists, round-trips correctly, and
  validates buffer size/payload-length consistency on receive, but nothing constructs one from a
  real `DPSESSIONDESC2`/session yet, and `magic`/`version` mismatches are not rejected yet either
  (deliberately deferred — see `DirectPlayWireProtocol.hpp`'s file comment).
- General `Send()` routing (to a player other than the sender) is a silent no-op — real routing,
  broadcast, and player-ID/payload validation are `plan.md` Phase 10, not started.
- `EnumSessions()` never finds anything (returns zero results) — real session discovery is
  `plan.md` Phase 8, not started. This is currently the *honestly correct* answer (nothing exists
  to discover yet), not a bug.
- DirectSound/DirectDraw hardening (`plan.md` Phases 13-14) not started.
- CTest/CI integration (`plan.md` Phase 15) not started.

## 3. Recent changes

- **Added** `CLAUDE.md` (project charter) and `plan.md` (phased task list, ~19 phases).
- **Added** `docs/directplay-callsite-audit.md` — real grep/read-based audit of DirectPlay usage
  in `../free-eggbert` and `../planetblupi`. Key findings: `planetblupi` has zero DirectPlay
  usage; `free-eggbert`'s DirectPlay lobby/session UI (`WM_PHASE_DP_*` handlers in `event.cpp`) is
  unwired (empty placeholders) — only gameplay-time `Send`/`Receive` are reachable; a DPID-size
  mismatch hazard exists (see Section 4).
- **Added** `docs/directplay-design.md` — records the "fake provider enumeration" decision
  (not yet implemented).
- **Modified** `src/directplay/DirectPlay.cpp` extensively: `QueryInterface` null-checks and
  `riid` dispatch fixed in both `DirectPlayImpl`/`DirectPlay2AImpl`; `DirectPlayCreate` output
  initialization and `DPERR_NOAGGREGATION` added; `Open`/`CreatePlayer`/`Close`/`EnumSessions`/
  `Send`/`Receive`/`Release` all rewritten from unconditional-`DP_OK` stubs to real
  state-driven behavior.
- **Added** `src/directplay/DirectPlaySession.hpp`/`.cpp` — real per-object state (lifecycle enum,
  player lists, session descriptor fields, DPID allocator, transport slot, message queue).
- **Added** `src/directplay/DirectPlayMessageQueue.hpp`/`.cpp` — `DirectPlayMessagePacket` +
  FIFO queue + `TryReceive()` (the exact logic `DirectPlay2AImpl::Receive()` delegates to) +
  size/oversize bounds (`kMaxQueuedMessages=256`, `kMaxPayloadBytes=4096`).
- **Added** `src/directplay/DirectPlayTransport.hpp` (`IDirectPlayTransport` interface) and
  `src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp` (in-memory, real implementation, wired
  into `Open`/`Close`/`Send`'s self-send path).
- **Added** `tests/directplay_tests.cpp` — 7 tests covering the message queue and loopback
  behavior end-to-end through the real `IDirectPlay2A` interface.
- **Fixed** a CMake bug in `CMakeLists.txt`: `SDL3::SDL3`/`SDL3_image::SDL3_image`/
  `SDL3_mixer::SDL3_mixer` imported targets created via `find_package()` inside `free-api`'s
  nested `add_subdirectory()` were invisible in `free-direct`'s own top-level scope. Fixed by
  re-running `find_package(... CONFIG QUIET)` directly in `free-direct`'s scope when the targets
  are still missing after the subdirectory returns.
- **Added** `FREE_DIRECT_ENABLE_ENET`/`FREE_DIRECT_USE_SYSTEM_ENET` CMake options, vendored/system
  ENet detection, and a `FreeDirect::ENet` ALIAS target linked `PRIVATE`.
- **Vendored** `third_party/enet` as a real git submodule (`https://github.com/lsalzman/enet`,
  pinned `v1.3.18-17-g5a9c537`); fixed a real upstream-CMake include-path propagation bug found
  during verification (`target_include_directories(enet PUBLIC ...)` added).
- **Updated** `README.md` with the `-DFREE_USE_SYSTEM_SDL=ON` build instructions.
- **Added** `src/directplay/DirectPlayWireProtocol.hpp`/`.cpp` — `DirectPlayWirePacketType`
  (`Join`/`JoinAccept`/`JoinReject`/`Data`/`Discovery`/`DiscoveryResponse`) and
  `DirectPlayWirePacketHeader` (magic, version, type, `applicationGuid`, `sessionGuid`, `idFrom`,
  `idTo`, `payloadLength`), plus `SerializeDirectPlayWireHeader`/`DeserializeDirectPlayWireHeader`
  (flat, padding-free, per-field `memcpy`). Zero ENet dependency — pure data structure, matching
  `plan.md` Phase 5's explicit intent to stand this up before any real ENet code exists. Wired into
  `CMakeLists.txt`'s `target_sources(free-direct ...)`. Added
  `Test_WireHeaderRoundTrip_PreservesAllFields` as an 8th test in `tests/directplay_tests.cpp`.
  Does **not** yet: populate a header from a real `DPSESSIONDESC2`/session (nothing constructs one
  outside the test).
- **Added** `TryDeserializeDirectPlayWireHeader(data, dataSize)` to the same file — validates an
  untrusted buffer before parsing it: rejects (`std::nullopt`) a buffer smaller than
  `kDirectPlayWireHeaderSize`, or one whose parsed `payloadLength` disagrees with the actual
  trailing byte count. Does not check `magic`/`version` (deliberately deferred, see the header's
  file comment). Added three tests to `tests/directplay_tests.cpp` (now 11 total): truncated
  buffer, mismatched payload length, and the accept case.
- **Added** `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` — the `IDirectPlayTransport`
  skeleton for the real ENet backend: an `ENetHost*`/`ENetPeer*` member pair (both
  null-initialized) and every interface method stubbed (`Listen`/`Connect`/`Send`/`Receive` return
  `false`; `Shutdown` no-ops; constructor/destructor are trivial). Not selected by `Open()` yet.
  `EnetDirectPlayTransport.cpp` is added to `CMakeLists.txt`'s `target_sources(free-direct ...)`
  only inside the existing `if(FREE_DIRECT_ENABLE_ENET)` block, so the default build is unaffected.
  Verified with two fresh, separate configure+build runs: `-DFREE_DIRECT_ENABLE_ENET=OFF` (default)
  and `-DFREE_DIRECT_ENABLE_ENET=ON`, both succeeding end-to-end; also re-ran the standalone
  DirectPlay test suite (11/11, unaffected) and re-confirmed `include/dplay.h` has zero ENet/SDL
  identifiers.

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker right now.** The most recently-active blocker (the
SDL3 CMake target-visibility bug) was found and fixed this session (Section 3), and is verified
working. The closest thing to an open problem is an **unresolved design decision**, not a failure:

- **Symptom**: none observed yet (no test reproduces it) — this is a hazard identified by static
  audit, not a runtime failure.
- **Issue**: `include/dplay.h` defines `DPID` as `DWORD_PTR` (8 bytes on 64-bit), but real
  DirectPlay's `DPID` is `DWORD` (4 bytes). `../free-eggbert/src/event.cpp`'s `NetSearchPlayer`
  and `NetStartPlay` walk arrays of its own `NetPlayer` struct using **hardcoded 32-byte
  pointer-arithmetic strides** that only match the original 4-byte `DPID` layout. If
  `free-eggbert` is ever compiled against `free-direct`'s current `dplay.h` on a 64-bit target,
  these two functions would silently read the wrong memory offsets.
- **Failing command/test**: none — not yet reproduced by any test; found via static analysis in
  `docs/directplay-callsite-audit.md` §5.
- **Affected files**: `include/dplay.h` (the `DPID` typedef), and (read-only, not to be modified)
  `../free-eggbert/src/event.cpp`'s `NetSearchPlayer`/`NetStartPlay`.
- **Suspected cause**: `DPID` was originally typedef'd as `DWORD_PTR` "to stay ABI-safe on both
  32-bit and 64-bit hosts" (per its own header comment), without accounting for `free-eggbert`'s
  raw-pointer-arithmetic assumption about struct layout.
  A second, related but separate question (DPID *starting value*, not size) is also open: whether
  DPID `0` must stay reserved (matching real DirectPlay's `DPID_SYSMSG`/`DPID_ALLPLAYERS`
  convention, and `DirectPlaySession::nextPlayerId`'s current placeholder default of `1`) or
  whether it must be assignable to the first real player to match `free-eggbert`'s own
  `CNetwork::Receive`'s `from == i` index-based comparison.
- **What has already been tried**: nothing yet — this has only been documented (twice, in the
  audit and again in `plan.md` Phase 9's task list as an explicit "resolve this before
  implementing" requirement), not acted on. It does not block current work because no code yet
  depends on the answer (`plan.md` Phase 9, real DPID allocation, has not started —
  `DirectPlaySession::nextPlayerId` is still an explicitly-labeled placeholder counter).

A secondary, much smaller open item: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` (the system-package ENet
path) has never been exercised successfully in this environment, since no `libenet` system
package is installed here — only the vendored-submodule ENet path has been verified. This is not
currently blocking anything, since the vendored path is the proven, working default.

## 5. Known bugs and limitations

- **Confirmed hazard, not yet fixed**: `DPID` size mismatch (`DWORD_PTR` vs. real DirectPlay's
  `DWORD`) — see Section 4. `docs/directplay-callsite-audit.md` §5.
- **Confirmed, not a FreeDirect bug**: `../free-eggbert`'s own DirectPlay lobby/session UI
  (`WM_PHASE_DP_*` handlers in `src/event.cpp`) is unwired — ten empty placeholder bodies, and a
  whole-repository grep confirms `NetCreate`/`NetEnumSessions`/`JoinSession`/`CreateSession`/
  `NetStartPlay` all have zero callers anywhere in that game's current source. Only gameplay-time
  `Send`/`Receive` are reachable. This is a `free-eggbert` source-completeness gap, out of
  `free-direct`'s scope to fix (game source must not be modified).
- **Incomplete**: `EnetDirectPlayTransport` is a stub-only skeleton (no ENet host/peer ever
  created, not selected by `Open()`) — no networked DirectPlay yet.
- **Incomplete**: `Send()` only handles the exact self-send case (`idTo == idFrom`); any other
  recipient is a silent no-op pending Phase 10.
- **Incomplete**: `EnumSessions()` always reports zero sessions (correct today, since nothing
  hosts yet) pending Phase 8.
- **Incomplete**: no CTest/CI wiring for `tests/directplay_tests.cpp` (Phase 15).
- **Needs verification**: the system-installed-ENet CMake path (`FREE_DIRECT_USE_SYSTEM_ENET=ON`)
  — logic is written and reviewed, but never actually exercised against a real `libenet` install.
- **Risky assumption, flagged for later refinement**: in `Send()`'s self-send path, any
  `DirectPlayMessageQueue::Enqueue()` failure (queue full **or** oversize payload) currently
  returns `DPERR_SENDTOOBIG` — imprecise for the "queue full" case specifically. Flagged in
  `plan.md` for Phase 10 to refine once full routing/error-code mapping is implemented.
- **Risky assumption**: `Open()` validates `dwFlags` against the `DPOPEN_CREATE`/`DPOPEN_JOIN`/
  `DPOPEN_OPENSESSION` mask but does not enforce that `DPOPEN_CREATE` and `DPOPEN_JOIN` are
  mutually exclusive (both-set would currently pass validation). Not currently exercised by any
  known call site.
- **Unknown**: whether the demo executable (`FREE_DIRECT`) actually runs and renders correctly on
  a real display in this environment — it compiles, but was not run graphically this session.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped types/constants/
interfaces only. **Hard rule**: no SDL3, SDL3_net, or ENet type/symbol may ever appear here
(enforced by convention + manual `grep` checks in this session, not yet automated in CI).

**Internal implementation** (`src/`):
- `src/directdraw/DirectDraw.cpp`, `src/directsound/DirectSound.cpp` — SDL3-backed, largely
  pre-existing (not touched this session beyond earlier, unrelated work).
- `src/directplay/` — the actively-developed subsystem this session:
  - `DirectPlay.cpp` — the public COM-shaped entry points (`DirectPlayCreate`,
    `DirectPlayEnumerateA/W`) and two anonymous-namespace classes: `DirectPlayImpl`
    (`IDirectPlay`, a thin factory) and `DirectPlay2AImpl` (`IDirectPlay2A`, the real
    implementation, owning one `DirectPlaySession session_` member directly, not by pointer).
  - `DirectPlaySession.hpp` — per-object state: `DirectPlayObjectState` enum
    (`Created`/`Open`/`Closed`), `isHost`, `localPlayerIds`/`remotePlayerIds`, `nextPlayerId`
    (placeholder DPID counter), session descriptor fields (`sessionName`, `password`,
    `applicationGuid`, `maxPlayers`, `currentPlayers`), an owned `transport`
    (`std::unique_ptr<IDirectPlayTransport>`), and an owned `messageQueue`
    (`DirectPlayMessageQueue`). Deliberately does **not** store a raw `DPSESSIONDESC2` (its
    string pointers are caller-owned and must not be retained).
  - `DirectPlayMessageQueue.hpp` — `DirectPlayMessagePacket` (idFrom/idTo/flags/payload) in a
    bounded FIFO `std::deque`, plus `TryReceive()` — the exact buffer-size-query/
    `DPERR_NOMESSAGES`/too-small/successful-copy logic that `DirectPlay2AImpl::Receive()`
    delegates to (extracted specifically so tests exercise the real logic, not a duplicate).
  - `DirectPlayTransport.hpp` — `IDirectPlayTransport` abstract interface
    (`Listen`/`Connect`/`Send`/`Receive`/`Shutdown`), backend-agnostic, byte-buffer-oriented.
  - `LoopbackDirectPlayTransport.hpp`/`.cpp` — real, in-memory implementation; assigned
    unconditionally by `Open()` today (the only backend that exists).
  - `DirectPlayWireProtocol.hpp`/`.cpp` — `DirectPlayWirePacketType` enum and
    `DirectPlayWirePacketHeader` struct (magic/version/type/applicationGuid/sessionGuid/idFrom/
    idTo/payloadLength) plus flat serialize/deserialize free functions, plus a validating
    `TryDeserializeDirectPlayWireHeader` for untrusted receive buffers (size/length checks only,
    not magic/version). Pure data structure, zero ENet dependency, not yet used by any transport
    (`EnetDirectPlayTransport` is what will construct/consume these once it exists).
  - `EnetDirectPlayTransport.hpp`/`.cpp` — **class skeleton only**. Holds an `ENetHost*`/`ENetPeer*`
    pair (both null); `Listen`/`Connect`/`Send`/`Receive` all return `false`, `Shutdown` no-ops, no
    `enet_initialize`/host/peer call exists yet. Compiled only under
    `-DFREE_DIRECT_ENABLE_ENET=ON`; not selected by `Open()` (Phase 6, not started).

**Data flow for the one working networked-ish path (self-send)**: `Open(DPOPEN_CREATE)` →
`session_.transport = make_unique<LoopbackDirectPlayTransport>()` → `CreatePlayer` allocates a
DPID → `Send(id, id, ...)` round-trips the payload through `transport->Send()`/`Receive()` (to
genuinely exercise the transport, not bypass it) → wraps the result into a
`DirectPlayMessagePacket` → `session_.messageQueue.Enqueue()` → `Receive()` calls
`session_.messageQueue.TryReceive()` synchronously, no waiting.

**Invariants / boundaries that must not be broken**:
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever.
- `IDirectPlayTransport` implementations must stay backend-agnostic byte-buffer interfaces; DPID/
  session metadata is layered on top by `DirectPlay2AImpl`, not inside the transport.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- `plan.md` tasks are atomic (one thing each); phases build on each other in order (e.g. Phase 9's
  real DPID allocation depends on the Section 4 decision being made first).
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX surface "for completeness."

## 7. Useful commands

Configure + build (default backend, no ENet):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```

Configure + build with the ENet transport backend enabled (vendored copy already present at
`third_party/enet`):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build cmake-build-debug -j4
```

Run the demo (untested graphically this session):
```bash
./cmake-build-debug/FREE_DIRECT
```

Run the DirectPlay tests (standalone, not yet CTest-integrated):
```bash
g++ -std=c++20 -Wall -Wextra \
    -I include -I ../free-api/include -I ../free-api/include_non_windows \
    -I src/directplay \
    src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
    tests/directplay_tests.cpp \
    -o directplay_tests
./directplay_tests
```

No lint/format tooling is configured in this repository.

There is no known reproducible bug to "reproduce" right now (see Section 4) — the closest thing
is confirming the `DPID`-size hazard, which would require compiling `../free-eggbert` itself
against this repo's `include/dplay.h` on a 64-bit target and inspecting `NetPlayer` array
behavior; this has not been attempted.

## 8. Next smallest tasks

1. **Add ENet init/shutdown lifecycle** (`enet_initialize`/`enet_deinitialize`, once per process
   regardless of instance count) to `EnetDirectPlayTransport`'s constructor/destructor, as its
   first real (non-stub) behavior.
   - Files: `src/directplay/EnetDirectPlayTransport.cpp`.
   - Verify: `cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON &&
     cmake --build cmake-build-debug -j4` succeeds, plus a small standalone smoke test
     constructing and destroying one or more `EnetDirectPlayTransport` instances without crashing
     (watch for the known ENet gotcha: `enet_initialize`/`enet_deinitialize` must be called once
     per **process**, not once per instance — get the reference counting right if more than one
     instance can exist concurrently).

2. **Resolve the DPID-size decision** (Section 4) in writing before Phase 9 needs it.
   - Files: `docs/directplay-design.md` (add a new decision entry, matching the existing
     "Decision 1" format already in that file).
   - Verify: none — this is a documentation/decision task, not code.

## 9. Do not do yet

- Do not refactor `DirectPlay.cpp`'s overall class structure — it works and is incrementally
  extended by design.
- Do not touch DirectDraw or DirectSound source (`src/directdraw/`, `src/directsound/`) — separate
  subsystems on separate, not-yet-started `plan.md` phases (13/14).
- Do not modify game source in `../free-eggbert` or `../planetblupi` under any circumstances.
- Do not implement general `Send()` routing, host forwarding, or broadcast (`plan.md` Phase 10) —
  it depends on `EnetDirectPlayTransport` having real send/receive behavior, which does not exist
  yet (still a stub).
- Do not wire `tests/directplay_tests.cpp` into CMake/CTest yet (`plan.md` Phase 15) — deliberately
  deferred until more of Phases 5-11 exist to test.
- Do not silently pick an answer to the DPID-size question — it must be written down explicitly
  first (task 2 above), not decided implicitly inside implementation code.
- Do not add an SDL3_net backend (`plan.md` Phase 12 explicitly defers this until ENet is stable).
- Do not add DirectX API surface, flags, or behavior beyond what `../free-eggbert`/
  `../planetblupi` call sites actually require (`CLAUDE.md` scope policy) — ask before expanding.
- Do not attempt a mass rewrite or "cleanup" pass — this codebase is being built up incrementally,
  one atomic `plan.md` task at a time, each verified before the next.

## 10. Resume prompt

```
Read NEXT.md first. Inspect only the files needed for the first task in "Next smallest tasks"
(currently: ENet init/shutdown lifecycle in EnetDirectPlayTransport's constructor/destructor). Do
not refactor unrelated code, do not touch DirectDraw/DirectSound, and do not modify
../free-eggbert or ../planetblupi. Make one small,
verified improvement - implement just that one task. Run the relevant build/test command from
"Useful commands" (or the task's own "Verify" step) and confirm it actually passes before
considering the task done. Then update NEXT.md to reflect the new state.
```
