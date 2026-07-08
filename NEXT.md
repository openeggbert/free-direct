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
- **Main DirectPlay goal**: two programs both built against FreeDirect's own DirectPlay
  implementation can host/join/exchange messages with each other. This is explicitly **not**
  wire-compatible with real Microsoft DirectPlay — FreeDirect-to-FreeDirect only.
- **Current development phase**: `plan.md`'s original Phases 0–9 remain complete (or have no more
  reachable tasks) over the **loopback** transport backend. Phase 10 (Send/Receive networking) is
  partially done — host→one specific client unicast works over loopback; broadcast and host-side
  routing still do not exist. Phases 11–18 remain largely not started. `plan.md` also carries a
  large appended section, **"24-Hour Autonomous Stabilization Backlog"** (141 atomic
  `TASK-24H-XXXX` tasks). As of this session, **64 of those 141 are DONE** (1 PARTIAL, 8 BLOCKED
  pending a human decision, 68 TODO). DirectDraw and DirectSound — previously the largest
  P0/P1 risk (zero test coverage) — now have 45 and 24 tests respectively; see Section 3.
- **Important architectural decisions** (full narrative + rationale for the DirectPlay ones lives
  in `docs/directplay-design.md`, Decisions 1–19):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only. No SDL3/ENet/SDL3_net symbol may ever appear in them — automatically enforced by the
    `header_hygiene` CTest test (`tests/check_header_hygiene.sh`), not just manual grep.
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`), with two concrete backends selected at
    **build time** (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option:
    `LoopbackDirectPlayTransport` (in-process, deterministic, used for all committed tests) and
    `EnetDirectPlayTransport` (real ENet UDP sockets).
  - DPID `0` is assigned to the host's own first local player (not reserved as
    `DPID_ALLPLAYERS`), matching `free-eggbert`'s own comparison pattern — a deliberate deviation
    from real DirectPlay that creates a **proven** bug for broadcast: see Section 5.
  - Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
    transport entirely.
  - **DirectDraw/DirectDrawSurface/DirectSound/DirectSoundBuffer implementation classes live in
    anonymous namespaces with no separate header** (unlike some DirectPlay internals) — all tests
    for these two subsystems go through the real public `IDirectDraw*`/`IDirectSound*` interfaces
    only, never whitebox.

## 2. Current status

**Build status: working.** FreeDirect is still never meant to be built in true isolation (it
depends on `free-api`, which depends on SDL3/SDL3_image/SDL3_mixer) — but **this environment now
has system SDL3/SDL3_image/SDL3_mixer installed** (confirmed via `pkg-config` and
`/usr/local/lib/cmake/SDL3*`), which a prior session's audit did not find. This means a genuinely
standalone configure now works:
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build>
```
This is a real, verified capability, not just a fallback — every test in this session was first
iterated on this way, then re-verified through both `../free-eggbert` and `../planetblupi`
subdirectory builds (the traditional, still-fully-supported path) before committing. All three
paths build clean with zero warnings on changed files.

**Test status: 7 CTest tests, all passing.** `FREE_DIRECT_BUILD_TESTS=ON` now builds and registers:
- `directplay_tests` (label `directplay`) — **49/49** passing (was 46 before this session).
- `header_smoke_ddraw` / `header_smoke_dsound` / `header_smoke_dplay` / `header_hygiene` (label
  `headers`) — all passing.
- `directdraw_tests` (label `directdraw`) — **45/45** passing. **New this session** — DirectDraw
  had zero tests before.
- `directsound_tests` (label `directsound`) — **24/24** passing. **New this session** — DirectSound
  had zero tests before.

Total: **118 individual `Test_*` functions** across the three hand-written test binaries (49 + 45
+ 24), all passing, plus the 4 header-compile/hygiene checks.

One caveat carried over from before: `ctest` only discovers these tests when run from **inside the
`FREE_DIRECT` build subdirectory** (`cd <build>/FREE_DIRECT && ctest`) when built through a target
game — because neither `free-eggbert` nor `planetblupi`'s own top-level `CMakeLists.txt` calls
`enable_testing()` itself. **This caveat does not apply to the new standalone build path** — there,
free-direct IS the top-level project, so plain `ctest` from the build root works directly. ENet
tests remain not attempted this session (out of scope — this session was DirectDraw/DirectSound
only, per explicit instruction).

**Available artifacts**:
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`); compiles, still not run/observed graphically.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/header_smoke_*.cpp`, `tests/check_header_hygiene.sh` — all CMake/CTest-wired.

**What does not work yet** (see Section 5 for full detail):
- DirectPlay: ENet joining/discovery, broadcast (proven to collide with self-send), host routing,
  player names, duplicate-player/player-lost semantics — all unchanged from before, all either
  BLOCKED on a human decision or genuinely not started.
- DirectDraw: `Flip` and presentation dirty-flag/throttle tests (`TASK-24H-0046`/`0047`) and the
  clipper one-time-init test (`TASK-24H-0050`) remain untested — not tackled this session since
  they weren't in this session's explicit 9-group scope, though `plan.md` still lists them.
- The `FREE_DIRECT` demo executable's actual on-screen behavior — still unverified.

## 3. Recent changes

**This session** (2026-07-08, DirectDraw/DirectSound test-coverage focus, continuing directly from
the prior session's audit — no new audit or plan was created, per explicit instruction):

1. **`tests/directdraw_tests.cpp`** (new, 45 tests) — closes `TASK-24H-0026` through `0045`,
   `0048`, `0049`. Covers creation/lifecycle (`DirectDrawCreate`, `AddRef`/`Release`,
   `SetCooperativeLevel` for both `DDSCL_NORMAL` and `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN` via a real
   `HWND` from free-api's own `RegisterClassA`/`CreateWindowExA` under `SDL_VIDEODRIVER=dummy`,
   `SetDisplayMode`, `CreateSurface` primary/offscreen/invalid descriptors, `GetSurfaceDesc`),
   surface memory (8-bit/32-bit format readback, `Lock`/`Unlock` pitch correctness, pixel
   read/write round-trips), blits (`BltFast` 1:1 copy/clipping/color-key for 8- and 32-bit, `Blt`
   color-fill/the exact present-path pattern `CPixmap::Display()` uses in both games/rect
   clipping/already-implemented scaling), color key (range semantics, unsupported-flag
   rejection), palette (`CreatePalette`/`SetEntries`/`GetEntries` round-trip and bounds checks,
   `SetPalette`, and a `GetDC`-based cross-check that palette application changes interpreted
   color), the DC bridge (proves free-api GDI access and DirectDraw `Lock` share the same backing
   pixels for 32-bit surfaces, bidirectionally), lost/restore characterization tests, and an
   `SDL_LogOutputFunction`-based regression guard proving no log fires from `Blt`/`BltFast` when
   `FREE_DIRECT_DEBUG_*` is unset. Found and fixed two real test-authoring bugs during
   verification (not DirectDraw bugs): `BlitFrom` always forces the destination alpha byte to 255
   regardless of source alpha (documented, deliberate), which two tests' initial full-32-bit-value
   comparisons didn't account for.
2. **`tests/directsound_tests.cpp`** (new, 24 tests) — closes `TASK-24H-0056` through `0075`
   (`0057` left `PARTIAL`, see below). Covers `DirectSoundCreate` (success + invalid-param paths),
   `SetCooperativeLevel`, `CreateSoundBuffer` (16-bit mono / 8-bit stereo PCM, using a real
   `PCMWAVEFORMAT` per `DirectSound.cpp`'s own padding-safety warning), `Lock` (single-region and
   wraparound two-region), `Unlock` (verified via `Play`+`GetStatus` that written data reaches the
   SDL stream), `Play`/`Stop`/`GetStatus`, `SetCurrentPosition` (the real `0`-only pattern both
   games use, plus out-of-range clamping), `SetVolume`/`SetPan` clamping (with an honest
   observability caveat — no public getter exists for the stored values), null/zero-sized/missing-
   format buffer descriptors (each documenting real, specified behavior), and a logging-gate
   regression guard.
3. **`docs/directsound-limitations.md`** (new) — closes `TASK-24H-0074`/`0075`. Documents the
   `PCMWAVEFORMAT` padding fix, mono-only `SetPan`, non-seekable `SetCurrentPosition`, no-looping
   policy, one-stream-per-buffer restart-on-replay, a **new finding**
   (`CreateSoundBuffer` performs no `dwSize` validation at all, unlike DirectDraw's
   `CreateSurface`), null/zero-sized-descriptor and missing-format-fallback behaviors, and the
   dead-code call sites (`soundbass.cpp`, `wave.cpp`, `PlaySoundDS`) found in the prior session's
   audit.
4. **`tests/CMakeLists.txt`** extended with `directdraw_tests` (label `directdraw`) and
   `directsound_tests` (label `directsound`) targets, each with
   `SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` set via the CTest `ENVIRONMENT` property.
5. Every change above was verified by an actual build+test run — a standalone free-direct build
   (system SDL3 now available) plus full builds through both `../free-eggbert` and
   `../planetblupi` — not assumed. `plan.md` updated with a `Verified:` note under each closed
   task's acceptance criteria, citing exactly what was tested and how.

Left `PARTIAL`, not `DONE`, with a documented reason (not silently skipped):
- `TASK-24H-0057` (DirectSoundCreate's `DSERR_NODRIVER` graceful-failure path) — `SharedAudioDevice`
  is a process-wide singleton whose chosen SDL audio driver is sticky for the process's lifetime
  once first selected (nothing ever calls `SDL_QuitSubSystem`), so forcing a no-driver failure
  in-process after a real driver is already selected isn't reliably possible without a subprocess
  harness this test file doesn't have. The code path itself was verified by reading
  `DirectSound.cpp` directly instead.

**Prior session** (audit + plan extension + 20 initial `TASK-24H-*` implementations — condensed;
see `docs/audit-24h-free-direct.md` for the full audit and `git log` for exact commits):
produced `docs/audit-24h-free-direct.md` (re-audit of DirectDraw/DirectSound/DirectPlay call sites,
corrected the prior "Blt is a rare path" assumption — it's the once-per-frame present call in both
games — and found the DPID-0/self-send/broadcast collision described in Section 5), appended the
141-task `plan.md` backlog, then implemented and verified: CMake/CTest wiring for
`directplay_tests` (fixing a real `CMAKE_SOURCE_DIR`-vs-`CMAKE_CURRENT_SOURCE_DIR` bug in the
process), the three header compile smoke tests plus the automated `header_hygiene` check, three
new DirectPlay regression tests (46→49, including the DPID-0 characterization test), fixed every
stale `STUB` Doxygen tag in `include/dplay.h`, and a diagnostics/logging audit across
DirectDraw/DirectSound/DirectPlay/ENet that found no ungated hot-path logs anywhere.

## 4. Current blocker / main problem

**There is still no build- or test-breaking blocker.** Everything builds, all 118 committed
`Test_*` checks pass (49 directplay + 45 directdraw + 24 directsound), plus 4 header-level checks.

The DirectPlay design fork described in prior sessions is unchanged — every remaining DirectPlay
direction still needs a human decision (see Section 8's Track B). This session deliberately did
not touch any of them, per explicit instruction to focus on DirectDraw/DirectSound test coverage
instead.

**What's different now**: the single biggest non-DirectPlay risk (zero DirectDraw/DirectSound test
coverage) is closed. The `plan.md` 24-Hour backlog's remaining 68 non-blocked TODO items are
mostly smaller, lower-priority items: a handful of DirectDraw tests not in this session's explicit
scope (`Flip`, presentation dirty-flag/throttle, clipper one-time-init), more DirectPlay regression
tests for previously-uncommitted-scratch-harness-only behavior, `docs/directplay-limitations.md`/
`docs/directplay-protocol.md`/`docs/networking-backends.md` (still not created), and
`plan.md` checkbox-drift reconciliation for Phases 6/7/9/16 (`TASK-24H-0086..0090`, still TODO).

**Minor, unchanged**: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` still unexercised in this environment (no
system `libenet` package). The `ctest`-top-level-discovery caveat for target-game builds (Section
2) is unchanged, though it doesn't apply to the new standalone build path.

## 5. Known bugs and limitations

**DirectPlay** (unchanged this session — see prior session's findings, still accurate):
- **Confirmed by test**: a hosting process's real broadcast call (`Send(m_dpid, 0, ...)`,
  `free-eggbert`'s only reachable `Send()` pattern) collides with self-send because the host's own
  DPID is also 0 (Decision 3) — proven by `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`.
  Blocked on a human decision (`TASK-24H-0131`) — do not resolve unilaterally.
- ENet joining/discovery, host routing, player names, duplicate-player/player-lost semantics — all
  unchanged, all either BLOCKED or not started. See Section 8, Track B.

**DirectDraw** (new findings/status this session):
- `GetDC`/`ReleaseDC` are documented `STUB` in the header but are functionally real (wrap the
  surface's own 32-bit buffer directly, or a palette-expanded temp buffer for 8-bit) — now
  covered by `Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock`. `planetblupi`'s
  `IsIconPixel` (a live gameplay hit-test path) depends on this and is now indirectly protected by
  regression coverage, though `IsIconPixel` itself is game code, not directly tested here.
- `Flip`, presentation dirty-flag/throttle behavior, and the one-time clipper setup sequence
  (`CreateClipper`/`SetClipper`/`SetHWnd`) remain untested (`TASK-24H-0046`/`0047`/`0050`) — not in
  this session's scope, still queued.
- `IsLost`/`Restore` remain honestly-documented inert stubs (`DD_OK` unconditionally) — now
  locked in by characterization tests, not changed.
- `DDBLT_COLORFILL`/`DDBLT_KEYSRC` remain implemented-but-dead for both target games (no real call
  site) — now has direct test coverage (`Test_Blt_ColorFill_FillsDestRectWithColor`) despite being
  unused by either game, since it's still public API.
- Minor, non-blocking, recorded not fixed: `IsDirectDrawDebugEnabled()`/
  `IsPresentationDebugEnabled()`/`IsColorKeyDebugEnabled()` call `SDL_getenv` on every invocation
  (uncached), unlike `IsPerfDebugEnabled()` in the same file, which caches. Real but minor hot-path
  overhead, not an output/correctness bug (the logging-gate test confirms zero log *output*, which
  is the actual invariant that matters).

**DirectSound** (new findings/status this session):
- `CreateSoundBuffer` performs **no `dwSize` validation at all** on the incoming `DSBUFFERDESC`,
  unlike DirectDraw's `CreateSurface` — confirmed by reading `DirectSound.cpp` directly. Not a bug
  (no call site needs it), but a real asymmetry between the two subsystems, now documented in
  `docs/directsound-limitations.md`.
- `DirectSoundCreate`'s `DSERR_NODRIVER` graceful-failure path remains **untested** (not just
  "not this session" — genuinely hard to test in-process, see Section 3's `TASK-24H-0057` note).
- Mono-only `SetPan`, non-seekable `SetCurrentPosition`, no-looping — all pre-existing, all now
  documented in `docs/directsound-limitations.md` with test coverage locking in the exact current
  behavior.
- **New environment fact, not a limitation**: this session's environment has system SDL3 fully
  available (see Section 2) — a prior session's audit found otherwise. Worth re-checking in future
  sessions rather than assuming either state.

**Unchanged**: `GUID::Data1` is 8 bytes on this platform vs. real DirectPlay's 4 (low priority,
wire-compat only). `../free-eggbert`'s own DirectPlay lobby UI is unwired in the game's current
source (not a FreeDirect bug, game source must never be modified). The `FREE_DIRECT` demo
executable's on-screen behavior is still unverified.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped only, now
CTest-enforced via `header_hygiene`. `dplay.h`'s Doxygen `Status:` tags are accurate as of the
prior session's fix.

**DirectDraw/DirectSound internals** (`src/directdraw/DirectDraw.cpp`,
`src/directsound/DirectSound.cpp`) — **unchanged this session**, both remain anonymous-namespace
implementations with no separate header, tested exclusively through the public `IDirectDraw*`/
`IDirectSound*` interfaces. Key facts that shaped this session's tests, worth keeping in mind for
future ones:
- `CreateSurface`/`Lock`/`Unlock`/`Blt`/`BltFast`/`SetColorKey`/`SetPalette`/`GetDC`/`ReleaseDC`
  never touch the SDL renderer — only `SetCooperativeLevel` and the primary-surface auto-present
  path inside `Blt`/`BltFast`/`Flip` need a real SDL window/renderer. Most DirectDraw tests
  therefore never create a window.
- `Lock()`/`GetSurfaceDesc()` only populate `lpSurface`/`lPitch` for **offscreen** surfaces, not
  the primary surface — matching real target-game usage (neither game locks the primary surface).
  Reading back a primary surface's actual pixels requires `GetDC`/`GetPixel` instead.
  `BlitFrom` always forces the destination alpha byte to 255 on copy, regardless of source alpha.
- `FreeApiCreateSurfaceDC` only supports 32bpp buffers; `GetDC` on an 8-bit surface expands through
  a temporary palette-converted buffer, so the "GDI sees the same backing pixels as Lock" property
  is only literally true for 32-bit surfaces (documented and tested separately from the 8-bit
  palette-expansion path).
- `DirectSoundBufferImpl`'s constructor reads `DSBUFFERDESC::lpwfxFormat` as `const PCMWAVEFORMAT*`
  — never `WAVEFORMATEX*` (a real struct-padding hazard on LP64, see
  `docs/directsound-limitations.md`).
- `SharedAudioDevice` (DirectSound) is a process-wide, ref-counted singleton around one SDL audio
  device; its chosen driver is sticky for the process's lifetime once first selected.

**Build/test infrastructure** — `tests/CMakeLists.txt` now builds and registers **7** CTest tests
when `FREE_DIRECT_BUILD_TESTS=ON` (default `OFF`): `directplay_tests` (`directplay`),
`header_smoke_ddraw`/`_dsound`/`_dplay`/`header_hygiene` (`headers`), `directdraw_tests`
(`directdraw`), `directsound_tests` (`directsound`). The latter two set
`SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` via the CTest `ENVIRONMENT` property. **New this
session**: a genuinely standalone free-direct build now works in this environment via
`-DFREE_API_USE_SYSTEM_SDL3=ON` (system SDL3/SDL3_image/SDL3_mixer are installed) — this was the
primary iteration loop for developing both new test files, re-verified via both target games
before every commit.

**Invariants that must not be broken** (unchanged):
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever — CTest-enforced.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — ask a human
  before adding DirectX surface "for completeness."
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never a runtime switch.

## 7. Useful commands

Standalone build with FreeDirect's own tests (now works — system SDL3 is available):
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build> -j8
cd <build> && ctest --output-on-failure   # top-level ctest works directly here
```

Or through a target game (also fully supported, no system-SDL3 dependency):
```bash
cd ../free-eggbert   # or ../planetblupi
cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build> -j8
cd <build>/FREE_DIRECT && ctest --output-on-failure   # must cd into FREE_DIRECT here - see Section 2
```

Run one subsystem's tests only: `ctest -L directdraw` / `-L directsound` / `-L directplay` /
`-L headers`.

Build + run just the DirectPlay tests standalone (fastest loop, no CMake/SDL needed):
```bash
g++ -std=c++20 -Wall -Wextra \
    -I include -I ../free-api/include -I ../free-api/include_non_windows \
    -I src/directplay \
    src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
    tests/directplay_tests.cpp -o directplay_tests
./directplay_tests   # expect "OK: all DirectPlay tests passed." (49/49)
```
DirectDraw/DirectSound tests need the full SDL3 stack and are CMake-only (no equivalent standalone
`g++` command).

Check the public-header/backend-leak invariant manually (also a CTest test):
```bash
bash tests/check_header_hygiene.sh include
```

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

**Track A — safe, no design decision needed:**
1. `TASK-24H-0046`/`0047`/`0050`: the three DirectDraw tests not covered this session (`Flip`,
   presentation dirty-flag/throttle, clipper one-time-init).
2. `TASK-24H-0077..0079, 0082, 0093, 0100..0110`: more DirectPlay regression tests for previously
   uncommitted-scratch-harness-only behavior, plus implementing real `DirectPlayEnumerateA`/`W`
   (`TASK-24H-0100` — Decision 1 already decided the shape, just needs coding).
3. `TASK-24H-0095, 0096`: `docs/directplay-protocol.md`, `docs/networking-backends.md` (still
   missing; `docs/directdraw-limitations.md` and `docs/directsound-limitations.md` now exist,
   `docs/directplay-limitations.md` does not yet — `TASK-24H-0094`).
4. `TASK-24H-0086..0090, 0130`: `plan.md` Phase 6/7/9/16 checkbox-drift reconciliation.
5. `TASK-24H-0097, 0098, 0108`: committed ENet transport-level tests using hardcoded loopback
   ports (does not require the ENet host-discovery design decision).
6. `TASK-24H-0010`: `FREE_DIRECT_ENABLE_ASAN`/`UBSAN` CMake options — would add real confidence to
   the `BltFast` clipping test in particular (currently verified by logical assertion, not a
   sanitizer run).

**Track B — needs a human decision first** (`plan.md`'s 8 `BLOCKED` tasks,
`TASK-24H-0091, 0131..0137`): DPID-0 broadcast/self-send semantics (now proven, not just
suspected — see Section 5), ENet host-address resolution, LAN discovery, host routing, player
names, duplicate-player definition, player-lost state. Do not start any of these without asking.

## 9. Do not do yet

- **No broad refactor** of `DirectDraw.cpp`/`DirectSound.cpp`/`DirectPlay.cpp` — each works and is
  extended incrementally by design.
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances.
- **Do not decide any of the 7 BLOCKED DirectPlay design questions unilaterally** (Section 8, Track
  B) — ask first. The DPID-0 characterization test now proves the exact current behavior; do not
  change what it asserts without a real decision first.
- **Do not add `DSBVOLUME_MIN`/`MAX` named constants to `include/dsound.h`** — no call site needs
  them by name (confirmed this session).
- **Do not add stricter `DSBUFFERDESC::dwSize` validation to `CreateSoundBuffer`** without asking —
  it's a documented asymmetry with DirectDraw, not a confirmed bug (no call site needs it either).
- **Do not implement real looping, capture, 3D audio, or accurate per-channel stereo panning for
  DirectSound** — no call site in either target game needs any of them (re-confirmed this
  session).
- **Do not add a run-time backend-selection mechanism** for loopback-vs-ENet — build-time-only via
  `FREE_DIRECT_ENABLE_ENET` is a deliberate, already-made decision.
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule. Ask first.
- **Do not edit a target game's `CMakeLists.txt`** to work around the `ctest`-top-level-discovery
  caveat without asking first.
- **No mass rewrites or speculative architecture changes.** Verify every change with an actual
  build+test run through at least one real consumer (standalone build or a target game) before
  considering it done.

## 10. Resume prompt

```
Read NEXT.md first (this file), especially Sections 4 and 8, then docs/audit-24h-free-direct.md
and plan.md's "24-Hour Autonomous Stabilization Backlog" section for full task detail. 64 of 141
TASK-24H-XXXX tasks are DONE, 1 PARTIAL, 8 BLOCKED (do not start those without asking), 68 TODO. DirectDraw (45 tests) and DirectSound (24 tests) test coverage - the biggest non-DirectPlay
risk - is now closed; the next real gaps are smaller (a few remaining DirectDraw tests, more
DirectPlay regression tests, missing docs, plan.md checkbox-drift reconciliation - see Section 8
Track A) or blocked on a human decision (Track B). This environment now has system SDL3 installed
- a standalone `cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON` build
works directly, which is faster to iterate on than going through a target game every time, though
both paths must still be re-verified before committing. Pick a Track A task, implement it, verify
with a real build+test run, mark it DONE in plan.md with a Verified note citing what you actually
ran, commit+push, then update this file's Sections 2/3/5 to match. Do not touch ../free-eggbert or
../planetblupi source. Do not resolve any Track B question unilaterally.
```
