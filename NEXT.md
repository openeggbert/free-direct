# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is explicitly **not** an attempt at full DirectX compatibility —
scope is bounded by what the two target games' real call sites need (see `CLAUDE.md`, the project
charter).

- **Target games** (sibling repos, present on disk): `../free-eggbert` (*Speedy Blupi* —
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi* — DirectDraw +
  DirectSound only; confirmed zero DirectPlay usage by grep). **Both confirmed building cleanly,
  out-of-tree, this session** (see Section 3).
- **Main DirectPlay goal**: two programs both built against FreeDirect's own DirectPlay
  implementation can host/join/exchange messages with each other. This is explicitly **not**
  wire-compatible with real Microsoft DirectPlay — FreeDirect-to-FreeDirect only.
- **Current development phase**: `plan.md`'s original Phases 0–18 and the 24-Hour Stabilization
  Backlog are both now deep - the backlog carries **147 atomic `TASK-24H-XXXX` tasks**. As of this
  session (real count, `grep`-verified against `plan.md`, not estimated): **125 DONE, 13 TODO, 1
  PARTIAL, 8 BLOCKED** pending a human decision. Session 1 ended at 64 DONE - this session closed
  **61 more**.
- **Important architectural decisions** (full narrative + rationale for the DirectPlay ones lives
  in `docs/directplay-design.md`, Decisions 1–19; deviation summary in
  `docs/directplay-limitations.md`, new this session):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only. No SDL3/ENet/SDL3_net symbol may ever appear in them — automatically enforced by the
    `header_hygiene` CTest test (`tests/check_header_hygiene.sh`), not just manual grep.
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`, 13 methods), with two concrete backends selected at
    **build time** (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option:
    `LoopbackDirectPlayTransport` (in-process, deterministic, used for all committed tests) and
    `EnetDirectPlayTransport` (real ENet UDP sockets - now with committed transport-level tests,
    `tests/enet_directplay_tests.cpp`, new this session).
  - DPID `0` is assigned to the host's own first local player (not reserved as
    `DPID_ALLPLAYERS`), matching `free-eggbert`'s own comparison pattern — a deliberate deviation
    from real DirectPlay that creates a **proven** bug for broadcast: see Section 5. `DPID` is now
    `static_assert`-enforced to be exactly 4 bytes (`plan.md` TASK-24H-0020, this session).
  - Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
    transport entirely.
  - **DirectDraw/DirectDrawSurface/DirectSound/DirectSoundBuffer implementation classes live in
    anonymous namespaces with no separate header** (unlike some DirectPlay internals) — all tests
    for these two subsystems go through the real public `IDirectDraw*`/`IDirectSound*` interfaces
    only, never whitebox.
  - Sanitizer builds (`FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`, new this session) and
    per-flag compile-time debug-log force-enables (`FREE_DIRECT_FORCE_DEBUG_*`, 7 flags, new this
    session) are both opt-in, `PRIVATE` to `free-direct`'s own targets, and OFF by default.

## 2. Current status

**Build status: working**, verified fresh multiple times this session, not assumed:
- Standalone (`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`): configure/build exit
  0, `ctest` 7/7.
- Bare standalone (no flags, no sibling game): **confirmed still fails** with the expected
  actionable 3-option error message (system SDL3 / build via a target game / provide SDL3 targets
  manually) - not a silent regression to a generic CMake error.
- Through `../free-eggbert`: configure/build exit 0, out-of-tree in a `/tmp` scratch dir (touching
  neither sibling repo), producing `SPEEDY_BLUPI_WINDOWS` - including `wave.cpp`/`network.cpp`
  compiling cleanly.
- Through `../planetblupi`: same result, producing `PLANET_BLUPI_WINDOWS`.
- `-DFREE_DIRECT_ENABLE_ENET=ON`: configure/build exit 0; `ctest -L enet` passes 1/1
  (`enet_directplay_tests`); the *unfiltered* `ctest` against this same build fails
  `directplay_tests` (1/8) **by design**, not a regression - see Section 5.
- `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON`: configure/build exit 0, `ctest`
  passes 7/7 with **zero sanitizer diagnostics** - after fixing one real bug the first sanitizer
  run caught (Section 3). Also verified combined with ENet (8/8).

**Test status: real counts, `grep`-verified against the actual test files, not recalled from
memory:**
- `directplay_tests` (label `directplay`) — **61/61** passing (was 49 at session-1 end).
- `header_smoke_ddraw` / `header_smoke_dsound` / `header_smoke_dplay` / `header_hygiene` (label
  `headers`) — all passing.
- `directdraw_tests` (label `directdraw`) — **53/53** passing (was 45).
- `directsound_tests` (label `directsound`) — **30/30** passing (was 24).
- `enet_directplay_tests` (label `enet`, opt-in, only built with `FREE_DIRECT_ENABLE_ENET=ON`) —
  **4/4** passing, new this session.

Total: **148 individual `Test_*` functions** across four hand-written test binaries (61 + 53 + 30 +
4), all passing, plus the 4 header-compile/hygiene checks.

The `ctest`-only-discoverable-from-inside-`FREE_DIRECT`-subdirectory caveat for target-game builds
(unchanged from before this session) does not apply to the standalone build path, which remains
the faster iteration loop.

**Available artifacts** (unchanged in kind, extended in content):
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`); compiles, still not run/observed graphically.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/enet_directplay_tests.cpp` (new), `tests/header_smoke_*.cpp`,
  `tests/check_header_hygiene.sh` — all CMake/CTest-wired.

**What does not work yet** (see Section 5 for full detail, unchanged in substance from before this
session - no BLOCKED question was resolved):
- DirectPlay: ENet joining/discovery, broadcast (proven to collide with self-send), host routing,
  player names, duplicate-player/player-lost semantics — all still BLOCKED on a human decision.
- `DirectPlayEnumerateA`/`W` real provider enumeration (`TASK-24H-0100`) — Decision 1 already
  decided the shape; genuinely just not yet coded. Not attempted this session (out of this
  session's DirectPlay-safe-tasks scope - implementing new stub behavior wasn't in the explicit
  "tests/cleanup only" DirectPlay priority list).
- The `FREE_DIRECT` demo executable's actual on-screen behavior — still unverified.

## 3. Recent changes

**This session** (2026-07-08, continuation implementation session - closed 61 more `TASK-24H-XXXX`
tasks, from 64 to 125 DONE; no new audit, no new plan created, per explicit instruction):

1. **DirectDraw**: added `Flip`/presentation-throttle/clipper-one-time-init tests (45→53), closing
   `TASK-24H-0046`/`0047`/`0050`-`0055`. Introduced `ReadPresentedPixel()` (a genuine black-box
   technique using `SDL_GetRenderer`/`SDL_RenderReadPixels` to verify *actually-rendered* pixels,
   not just the CPU buffer) and `FREE_DIRECT_TARGET_FPS`-based deterministic throttle testing.
2. **DirectSound**: added 6 buffer-lifetime/`Play`/`Stop`/ref-count edge-case tests (24→30),
   closing `TASK-24H-0142`-`0147`.
3. **DirectPlay**: added 12 new tests (49→61) - null-argument validation, `Close()`-twice safety,
   oversized-payload rejection, and a real port-rebind-after-`Close()` proof. **Found and fixed a
   real crash-risk bug** while sweeping for null-pointer issues (`TASK-24H-0106`/`0107`): `Send()`
   never checked `lpData` for null before `bytes + dwDataSize` pointer arithmetic when
   `dwDataSize > 0` - undefined behavior, now guarded with `DPERR_INVALIDPARAMS`.
4. **ENet transport tests** (`tests/enet_directplay_tests.cpp`, new file, 4 tests,
   `TASK-24H-0097`/`0098`/`0108`): whitebox tests directly against `EnetDirectPlayTransport` (not
   through `IDirectPlay2A`), hardcoded distinct ports to sidestep the still-BLOCKED host-discovery
   question. Also found and fixed a **stale doc comment** in `EnetDirectPlayTransport.hpp` claiming
   `Receive()` "always returns false" - contradicted by Decision 19's real buffering, landed in a
   prior session but never reflected in this comment.
5. **Sanitizer builds** (`TASK-24H-0010`): added `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`
   CMake options, `PRIVATE` to `free-direct`'s own targets only. **The first sanitizer run
   immediately caught a real bug**: `DirectPlayMessageQueue.hpp`'s `TryReceive()` passed an empty
   `std::vector::data()` (which may be `nullptr`) as `memcpy`'s nonnull-declared `src` argument for
   zero-length messages - undefined behavior even at `count == 0`. Fixed by skipping the `memcpy`
   call when `payloadSize == 0`. Audited every other `memcpy`/`memcmp`/`memmove` call site in
   `src/`/`include/` for the same pattern - this was the only instance. Re-verified across 4
   scratch-build configurations after the fix; all pass clean.
6. **Debug-flag CMake overrides** (`TASK-24H-0119`/`0120`): added `#ifdef`-based compile-time
   override support to DirectDraw's 5 debug flags (mirroring DirectSound's pre-existing pattern),
   plus 7 `FREE_DIRECT_FORCE_DEBUG_*` CMake options. Verified the mechanism actually changes
   behavior (not just compiles) by building with a force-flag on and confirming the corresponding
   zero-log regression test now fails as expected. README's flag documentation brought to full
   parity with source (`FREE_DIRECT_DEBUG_DSOUND_FORMAT`/`FREE_DIRECT_DIAGNOSTICS` were
   undocumented before).
7. **Header hygiene/documentation** (`TASK-24H-0020`-`0023`, `0025`): added
   `static_assert(sizeof(DPID) == 4)`; documented `DDBLT_COLORFILL`/`KEYSRC`/`ROTATIONANGLE` and
   `DSBCAPS_STATIC`'s real call-site status inline. **`TASK-24H-0022`'s own evidence turned out to
   be stale** - it claimed `DPESC_TIMEDOUT`/`DPSESSION_KEEPALIVE`/`DPSESSION_MIGRATEHOST` were
   unused, but direct verification found real call sites for all three in `free-eggbert`'s
   `network.cpp`. Corrected with accurate comments instead (see Section 5). Also found (not fixed,
   inert today): `DDBLT_ROTATIONANGLE`'s value doesn't match `free-eggbert`'s own vendored SDK
   header. `TASK-24H-0025` confirmed the *only* new public declaration added all session is the
   `static_assert` itself - zero unjustified surface crept in.
8. **Build/test documentation** (`TASK-24H-0011`/`0012`): rewrote README's "Build Instructions" -
   it was itself stale (first example was exactly the config that fails; documented system-SDL flag
   name didn't exist). New content: 5 configurations, each actually run this session, including the
   verified free-eggbert/planetblupi out-of-tree builds. Added `docs/ci-matrix.md` (non-blocking
   note, no `.github/workflows` added).
9. **DirectPlay documentation** (`TASK-24H-0094`/`0095`/`0096`): created
   `docs/directplay-limitations.md` (19-Decision deviation table + all 7 standing BLOCKED
   questions listed together), `docs/directplay-protocol.md` (wire header layout), and
   `docs/networking-backends.md` (`IDirectPlayTransport` abstraction + backend tradeoffs). **Caught
   a real error before publishing** `directplay-protocol.md`: hand-derived the header size as 56
   bytes assuming a 16-byte `GUID` (the real Win32/LLP64 value); compiling a verification program
   directly against `DirectPlayWireProtocol.hpp` found `sizeof(GUID) == 24` on this Linux/LP64
   build (`unsigned long` is 8 bytes here) and the true header size is **72 bytes** - documented as
   a new, previously-unwritten "Known gap" (wire size is platform/ABI-dependent).
10. **README compatibility pass** (`TASK-24H-0121`/`0122`/`0125`-`0130`): rewrote both DirectPlay
    bullets (was "dummy stubs", now accurately describes real loopback+ENet-hosting behavior and
    explicitly lists what's NOT implemented); added a "Compatibility Status" section with a
    per-method STUB/PARTIAL/IMPLEMENTED table sourced directly from header `@note Status:` tags;
    cross-linked `docs/audit-24h-free-direct.md`; swept all docs for accidental "full DirectX 3
    compatibility" and "Microsoft DirectPlay wire-compatible" claims (zero found - every match was
    a correct disclaimer); checked all 11 `plan.md` Phase 16 documentation checkboxes, each with a
    citation.
11. Every change above was verified by an actual build+test run in a fresh `/tmp` scratch
    directory before being marked DONE - never assumed. `plan.md` has a `Verified:` note under
    every closed task's acceptance criteria citing exactly what was tested.

**Prior session (2)** (DirectDraw/DirectSound test-coverage focus - condensed; full detail was
here before this rewrite, still in `git log`): added `tests/directdraw_tests.cpp` (45 tests) and
`tests/directsound_tests.cpp` (24 tests), both new from zero; `docs/directsound-limitations.md`;
discovered this environment has usable system SDL3 (`-DFREE_API_USE_SYSTEM_SDL3=ON`), contradicting
an earlier audit finding. Left `TASK-24H-0057` (`DSERR_NODRIVER` path) `PARTIAL` - `SharedAudioDevice`'s
SDL audio driver selection is process-lifetime-sticky, not reliably forceable in-process without a
subprocess harness.

**Prior session (1)** (audit + plan extension + 20 initial `TASK-24H-*` implementations - see
`docs/audit-24h-free-direct.md` for the full audit and `git log` for exact commits): produced the
initial audit, appended the original 141-task `plan.md` backlog (since grown to 147), CMake/CTest
wiring for `directplay_tests`, header hygiene automation, and the DPID-0 self-send/broadcast
collision characterization test.

## 4. Current blocker / main problem

**There is still no build- or test-breaking blocker.** Everything builds, all 148 committed
`Test_*` checks pass (61 directplay + 53 directdraw + 30 directsound + 4 ENet, opt-in), plus 4
header-level checks, across every verified configuration (default, ENet, ASan+UBSan, and both
target games).

The DirectPlay design fork described in prior sessions is **unchanged** — all 7 standing BLOCKED
design questions (DPID-0 broadcast semantics, ENet host discovery, LAN discovery, host routing,
player names, duplicate-player semantics, player-lost state) still need a human decision. This
session deliberately did not touch any of them, per explicit instruction - see
`docs/directplay-limitations.md`'s dedicated section for all 7 listed together with citations.

**What's different now**: 61 more tasks closed, two real memory-safety/UB bugs found and fixed via
systematic sweeps (one manual, one sanitizer-driven), one stale task premise caught and corrected
rather than blindly executed, one real documentation math error (GUID wire size) caught before
publishing, and both target games freshly re-verified building clean. The remaining 13 TODO tasks
(real count, listed in Section 8) are smaller: a few recurring-verification tasks whose discipline
was already followed throughout this session but not formally re-closed as discrete checkboxes, the
real-`DirectPlayEnumerateA`/`W` implementation (decided, not yet coded), and this file's own
closing entries.

**Minor, unchanged**: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` still unexercised in this environment (no
system `libenet` package) - the vendored `third_party/enet` path is the one actually verified.

## 5. Known bugs and limitations

**DirectPlay** (updated this session):
- **Confirmed by test**: a hosting process's real broadcast call (`Send(m_dpid, 0, ...)`,
  `free-eggbert`'s only reachable `Send()` pattern) collides with self-send because the host's own
  DPID is also 0 (Decision 3) — proven by `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`.
  Blocked on a human decision (`TASK-24H-0131`) — do not resolve unilaterally.
- ENet joining/discovery, host routing, player names, duplicate-player/player-lost semantics — all
  unchanged, all either BLOCKED or not started. See Section 8, Track B, and
  `docs/directplay-limitations.md`'s dedicated section for all 7 together.
- **Two new findings this session, both in `docs/directplay-limitations.md` now**:
  - `DPESC_TIMEDOUT`: `free-eggbert`'s `EnumSessionsCallback` really does check this flag (contrary
    to `TASK-24H-0022`'s own original, now-corrected, evidence), but FreeDirect's `EnumSessions()`
    is a synchronous registry lookup that never sets it - the check is real but unreachable from
    FreeDirect's side today. Not a bug: enumeration still terminates normally via `DP_OK`.
  - `DPSESSION_KEEPALIVE`/`DPSESSION_MIGRATEHOST`: `free-eggbert` really does set both when hosting,
    but `Open()` never reads `DPSESSIONDESC2::dwFlags` at all - silently ignored. Consistent with
    host migration being entangled with the still-open "host routing"/"player-lost state" BLOCKED
    questions, not a new contradiction.
- **Fixed this session, not just documented**: `Send()`'s null-`lpData`-with-nonzero-size UB
  (pointer arithmetic on a null pointer), and `DirectPlayMessageQueue::TryReceive()`'s null-`src`
  `memcpy` UB for zero-length messages (found via ASan/UBSan). Both are real hardening of already-
  validated behavior, not new API surface.

**DirectDraw** (unchanged this session in substance; test coverage now complete for previously-gap
areas):
- `GetDC`/`ReleaseDC` are documented `STUB` in the header but are functionally real - unchanged.
- `Flip`, presentation throttle, and clipper one-time-init are **now tested** (`TASK-24H-0046`/
  `0047`/`0050`-`0055`, closed this session) - previously the largest DirectDraw test gap.
- `IsLost`/`Restore` remain honestly-documented inert stubs — unchanged, still test-locked.
- **New finding, documented not fixed** (`docs/directdraw-limitations.md`, prior session):
  `SetDisplayMode`'s `dwBPP` parameter is accepted/logged but never stored/used - the primary
  surface is always 32bpp. Currently latent for both target games; not fixed speculatively per
  scope policy.
- `DDBLT_ROTATIONANGLE`'s literal value doesn't match `free-eggbert`'s own vendored SDK header
  (found this session, `TASK-24H-0021`) - harmless today, nothing reads this constant.

**DirectSound** (unchanged this session):
- `CreateSoundBuffer` performs no `dwSize` validation - documented, not a confirmed bug.
- `DirectSoundCreate`'s `DSERR_NODRIVER` path remains untested (`TASK-24H-0057`, `PARTIAL`) -
  genuinely hard to force in-process given `SharedAudioDevice`'s process-lifetime-sticky driver
  selection.
- Mono-only `SetPan`, non-seekable `SetCurrentPosition`, no-looping - all pre-existing, documented.

**Unchanged**: `../free-eggbert`'s own DirectPlay lobby UI is unwired in the game's current source
(not a FreeDirect bug, game source must never be modified). The `FREE_DIRECT` demo executable's
on-screen behavior is still unverified. `docs/directplay-protocol.md`'s wire header size (72 bytes)
is platform/ABI-dependent (found this session) - would be 64 bytes on a real Win32/LLP64 build,
only matters if two differently-built FreeDirect peers ever tried to talk to each other, which
nothing does today.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped only, CTest-
enforced via `header_hygiene`. `dplay.h` now also carries a `static_assert(sizeof(DPID) == 4)`
(this session) and corrected inline comments for `DDBLT_*`/`DPESC_TIMEDOUT`/`DPSESSION_*` constants
whose real call-site status was previously undocumented or mis-documented.

**DirectDraw/DirectSound internals** (`src/directdraw/DirectDraw.cpp`,
`src/directsound/DirectSound.cpp`) — both remain anonymous-namespace implementations with no
separate header, tested exclusively through the public `IDirectDraw*`/`IDirectSound*` interfaces.
Key facts, largely unchanged from before this session (see prior `NEXT.md` history in `git log` for
the full list) - **new this session**: both files' debug-flag-check functions now support an
`#ifdef FREE_DIRECT_DEBUG_*` compile-time override in addition to the existing runtime env-var
check (DirectSound already had this; DirectDraw's 5 flags gained it this session).

**DirectPlay internals** (`src/directplay/`) — `IDirectPlayTransport` (13 methods:
`Listen`/`Connect`/`Send`/`Receive`/`Service`/`HasPendingConnection`/`AssignPendingConnection`/
`RejectPendingConnection`/`HasDisconnectedPeer`/`TakeDisconnectedPeer`/`IsConnectedToHost`/
`Shutdown`, plus the destructor) is implemented by `LoopbackDirectPlayTransport` (always available)
and `EnetDirectPlayTransport` (opt-in). `DirectPlayMessageQueue::TryReceive()`'s zero-length-message
`memcpy` bug (Section 5) is fixed. `DirectPlayWireProtocol.hpp`'s header is 72 bytes on this
project's Linux/LP64 build (see `docs/directplay-protocol.md`, new this session, for the exact
field-by-field layout).

**Build/test infrastructure** — `tests/CMakeLists.txt` now builds and registers up to **8** CTest
tests when `FREE_DIRECT_BUILD_TESTS=ON` (default `OFF`): the 7 from before plus
`enet_directplay_tests` (`enet` label, only when `FREE_DIRECT_ENABLE_ENET=ON` too). Root
`CMakeLists.txt` gained `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN` (both OFF by default,
`PRIVATE` to `free-direct`'s own targets) and 7 `FREE_DIRECT_FORCE_DEBUG_*` options (also OFF by
default, additive to the existing env-var mechanism) - all new this session.

**Invariants that must not be broken** (unchanged):
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever — CTest-enforced.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — ask a human
  before adding DirectX surface "for completeness."
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never a runtime switch.
- Sanitizer/force-debug-flag CMake options are `PRIVATE` and OFF by default — never forced onto a
  consuming target game, never on by default.

## 7. Useful commands

Standalone build with FreeDirect's own tests:
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build> -j8
cd <build> && ctest --output-on-failure
```

Through a target game (no system-SDL3 dependency, vendored SDL):
```bash
cmake -B <build> -S ../free-eggbert    # or ../planetblupi
cmake --build <build> -j8
```

ENet-enabled (opt-in, scoped test run - see Section 5 for why `-L enet` matters):
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build <build> -j8
ctest --test-dir <build> -L enet
```

Sanitizer build (opt-in):
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON
cmake --build <build> -j8
ctest --test-dir <build> --output-on-failure
```

Run one subsystem's tests only: `ctest -L directdraw` / `-L directsound` / `-L directplay` /
`-L headers` / `-L enet`.

Check the public-header/backend-leak invariant manually (also a CTest test):
```bash
bash tests/check_header_hygiene.sh include
```

Force a debug-log flag on at compile time instead of via environment variable (new this session):
```bash
cmake -B <build> ... -DFREE_DIRECT_FORCE_DEBUG_DDRAW=ON   # or _DSOUND/_PRESENTATION/_COLORKEY/_PERF/_PRIMARY_CLEAR/_DSOUND_FORMAT
```

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

**Track A — safe, no design decision needed** (real remaining `plan.md` TODO count: 15):
1. `TASK-24H-0100`: Implement real `DirectPlayEnumerateA`/`W` provider enumeration - Decision 1
   already decided the exact shape (invoke the callback once, describing a FreeDirect-internal
   placeholder provider); genuinely just not yet coded. Not attempted this session since it's new
   behavior, not a test/cleanup task, and wasn't in this session's explicit DirectPlay-safe-tasks
   list.
2. `TASK-24H-0005`/`0006`/`0007`/`0008`/`0013`-`0015`/`0076`: mostly recurring "re-verify X after
   every change" process tasks from early planning, whose discipline was followed throughout this
   session (every batch got a real build+test verification) but were never formally closed as
   discrete checkboxes - worth a dedicated pass to either close them with citations or confirm
   they're genuinely still meaningful as ongoing habits rather than one-time deliverables.
3. `TASK-24H-0138`-`0141`: the "end of session" final-integration-reverification tasks - these are
   what this session's own closing full-CTest-run/free-eggbert-build/planetblupi-build/header-
   hygiene-recheck pass (about to happen) will satisfy directly.

**Track B — needs a human decision first** (`plan.md`'s 8 `BLOCKED` tasks,
`TASK-24H-0091, 0131..0137`): DPID-0 broadcast/self-send semantics (proven, not just suspected -
see Section 5), ENet host-address resolution, LAN discovery, host routing, player names,
duplicate-player definition, player-lost state. All 7 questions are now also listed together, with
citations, in `docs/directplay-limitations.md`'s dedicated section. Do not start any of these
without asking.

## 9. Do not do yet

- **No broad refactor** of `DirectDraw.cpp`/`DirectSound.cpp`/`DirectPlay.cpp` — each works and is
  extended incrementally by design.
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances.
- **Do not decide any of the 7 BLOCKED DirectPlay design questions unilaterally** (Section 8, Track
  B) — ask first. The DPID-0 characterization test now proves the exact current behavior; do not
  change what it asserts without a real decision first.
- **Do not add `DSBVOLUME_MIN`/`MAX` named constants to `include/dsound.h`** — no call site needs
  them by name.
- **Do not add stricter `DSBUFFERDESC::dwSize` validation to `CreateSoundBuffer`** without asking —
  it's a documented asymmetry with DirectDraw, not a confirmed bug.
- **Do not implement real looping, capture, 3D audio, or accurate per-channel stereo panning for
  DirectSound** — no call site in either target game needs any of them.
- **Do not add a run-time backend-selection mechanism** for loopback-vs-ENet — build-time-only via
  `FREE_DIRECT_ENABLE_ENET` is a deliberate, already-made decision.
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule. Ask first.
- **Do not implement host migration** (reading `DPSESSION_KEEPALIVE`/`MIGRATEHOST`) without asking
  — entangled with the still-BLOCKED host-routing/player-lost-state questions (new finding this
  session, Section 5).
- **Do not "fix" `DDBLT_ROTATIONANGLE`'s value mismatch** against the vendored SDK header without
  asking — currently inert (nothing reads it), found this session, documented not fixed.
- **Do not correct the wire protocol's platform-dependent header size** (72 vs. 64 bytes, Section
  5) to be portable without asking — nothing today needs cross-ABI FreeDirect-to-FreeDirect
  interoperability, and doing so would be new protocol-versioning work, not a bug fix.
- **No mass rewrites or speculative architecture changes.** Verify every change with an actual
  build+test run through at least one real consumer (standalone build or a target game) before
  considering it done.

## 10. Resume prompt

```
Read NEXT.md first (this file), especially Sections 4 and 8, then docs/directplay-limitations.md
(the 7 BLOCKED design questions, listed together) and plan.md's "24-Hour Autonomous Stabilization
Backlog" section for full task detail. 125 of 147 TASK-24H-XXXX tasks are DONE, 1 PARTIAL, 8
BLOCKED (do not start those without asking), 13 TODO. DirectDraw (53 tests), DirectSound (30
tests), DirectPlay (61 tests + 4 opt-in ENet transport tests) all have solid coverage now; two real
memory-safety/UB bugs were found and fixed this session (one via manual sweep, one via a new
ASan/UBSan sanitizer build - TASK-24H-0010). The remaining Track A work (Section 8) is mostly
recurring-verification bookkeeping and one real implementation task (DirectPlayEnumerateA/W, shape
already decided). Standalone build: `cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON
-DFREE_DIRECT_BUILD_TESTS=ON`. Pick a Track A task, implement it, verify with a real build+test run
in a fresh /tmp scratch dir, mark it DONE in plan.md with a Verified note citing what you actually
ran, commit+push, then update this file's Sections 2/3/5 to match. Do not touch ../free-eggbert or
../planetblupi source. Do not resolve any Track B question unilaterally.
```
