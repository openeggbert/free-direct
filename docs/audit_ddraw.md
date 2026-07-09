# DirectDraw subsystem audit

This is a from-scratch, evidence-based audit of FreeDirect's DirectDraw implementation
(`src/directdraw/DirectDraw.cpp`, 1632 lines; declared in `include/ddraw.h`, 431 lines). It covers
performance, memory safety, correctness against DirectDraw semantics, code quality, and several
additional dimensions identified during the audit itself (build configuration hygiene, API-boundary
input validation, exception-safety-boundary compliance, and test coverage gaps).

This document is **read-only analysis**. No code was changed to produce it, and no finding below
has been fixed as part of this audit, per this project's standing policy of treating audits and
implementation as separate tasks (see `CLAUDE.md` Safety Rules). Findings are candidates for new
`plan.md` tasks, not a to-do list to execute directly.

> **Status update (2026-07-09, later the same day): all 12 lettered findings below (F1-F12) plus
> one additional §3.4 code-quality item with no F-number have since been turned into 13 `plan.md`
> tasks (`TASK-24H-0151`-`0163`, one task per finding, F1→0151 through F12→0162, §3.4→0163) and all
> 13 are now `DONE`.** This audit document
> itself is left exactly as originally written below - it is a record of what was found and when,
> not a live status tracker - so several findings below still read as open problems ("has no
> bound", "no fast path exists", etc.) even though the corresponding code has since been fixed. For
> current status, see `plan.md`'s "DirectDraw audit hardening (2026-07-09)" section (each task's own
> `Verified:` note) or `NEXT.md` Section 5 for a condensed per-finding summary of what changed.

## 1. Scope and methodology

- **Primary subject**: `src/directdraw/DirectDraw.cpp` (all four COM-style classes:
  `DirectDrawImpl`, `DirectDrawSurfaceImpl`, `DirectDrawPaletteImpl`, `DirectDrawClipperImpl`) and
  the public contract in `include/ddraw.h`.
- **Builds on, does not duplicate**: `docs/directdraw-limitations.md` already contains a
  carefully-verified set of findings (corrected `Blt`-vs-`BltFast` call-site counts, `SetDisplayMode`'s
  `dwBPP` discard, 8-bit palette conversion reachability, color-key range handling, `GetDC`/`ReleaseDC`
  STUB-label mismatch, `IsLost`/`Restore` inert stub, simplified flip chain, presentation
  throttle/dirty-flag testability limits). Those findings are treated as established and are only
  referenced, not re-derived, below. Everything in Sections 3-6 is a new finding from this audit
  unless explicitly marked "(see `directdraw-limitations.md`)".
- **Evidentiary standard**: every finding is graded on two independent axes, matching the standard
  `directdraw-limitations.md` already applies informally:
  - **Impact** — how bad the consequence is *if* the code path executes.
  - **Reachability** — whether either target game's *actual, currently-verified* call sites can
    trigger the path today. This was checked by grepping `../free-eggbert/src/` and
    `../planetblupi/src/` directly (never assumed from memory), same methodology as the existing
    limitations doc.
  - A finding can be high-impact and simultaneously "not reachable today" — that combination is
    common in this codebase precisely because both games only ever exercise a narrow slice of the
    implemented surface (see Section 8).
- **Empirical vs. code-reasoned**: performance claims that could be measured were measured against
  the real compiled library (Section 7), not estimated from Big-O reasoning alone. Where a claim is
  Big-O/code-reasoning only, it is labeled as such.
- **Not in scope**: DirectSound, DirectPlay, and anything outside `src/directdraw/` /
  `include/ddraw.h`, per `CLAUDE.md`'s atomicity rule (don't mix subsystems in one task).

## 2. Executive summary

| # | Finding | Impact | Reachable today? | Section |
|---|---|---|---|---|
| F1 | Default build (`cmake ..` with no `CMAKE_BUILD_TYPE`) compiles with **no optimization flags at all** | High — ~6.6x slower than `-O3` on the measured hot path, affects every function in the library | **Yes** — this is literally the build produced by following the repo's own build instructions with no extra flags | 3.1 |
| F2 | `BlitFrom` has no 1:1 (no-scale) fast path; every pixel pays a division plus a per-pixel format branch | High — measured 29x slower than `memcpy` of the same bytes even at `-O3`; both games' full-screen present blit and every `BltFast` sprite blit pay this | **Yes** — `BltFast` is 1:1 by construction (see 8.1), and both games call it far more than `Blt` | 3.2 |
| F3 | `ReleaseDC`'s 8-bit path does an O(width×height×256) nearest-palette linear search per call | High if triggered (measured 2.6ms unoptimized / 0.43ms at `-O3` for one 640x480 surface) | **No** — conclusively confirmed unreachable; see 8.2 | 3.3 |
| F4 | `CreateSurface` never validates `dwWidth`/`dwHeight` before an unchecked `size_t`-multiplying `vector::resize` | High if triggered — uncaught `std::length_error`/`std::bad_alloc` crashes the process, violating this project's own "no exceptions cross the interface boundary" rule | **No** — both games always pass small literal sizes | 4.4 |
| F5 | `DirectDrawPaletteImpl::GetEntries`/`SetEntries` bounds check (`dwBase + dwNumEntries > 256`) can integer-overflow and be bypassed | High if triggered — `SetEntries`' bypass is an out-of-bounds **write** | **No** — neither game ever calls these with attacker-scale values; also untested (see 9) | 5.2 |
| F6 | `SetCooperativeLevel` destroys/replaces `renderer_` without invalidating any existing surface's cached `texture_`, which then dangles against a destroyed renderer | High if triggered — undefined behavior in SDL (using a texture after its owning renderer is destroyed) | **No** — both games call it exactly once, before any surface exists | 4.6 |
| F7 | `FillColor`'s 8-bit branch `return`s before the `MarkDirty()` call that the 32-bit branch reaches | Medium — a fill on an 8-bit primary surface would silently never reach the screen | **No** — neither game uses `DDBLT_COLORFILL`, and no 8-bit primary surface is ever created in practice | 5.1 |
| F8 | `GetDC` never returns `DDERR_DCALREADYCREATED` on a redundant call, despite the constant being defined in `include/ddraw.h` | Low-Medium — silent semantic deviation from real DirectDraw | **No** — every confirmed real call site pairs `GetDC`/`ReleaseDC` tightly with no nesting | 4.3 |
| F9 | `GetSurfaceDesc` hardcodes `0x00000020L`/`0x00000040L` instead of the already-defined `DDPF_PALETTEINDEXED8`/`DDPF_RGB` constants | Low — maintainability only, values are correct | N/A (correctness unaffected) | 5.3 |
| F10 | File-scope `#define SDL_Log DirectDrawLog` shadows the real SDL3 function name for the rest of the translation unit | Low — works today, but is a foot-gun for future edits/merges | N/A | 6.1 |
| F11 | `owner_` is a raw, unmanaged back-pointer from surface to `DirectDrawImpl` with no lifetime enforcement | Medium (latent) — no crash observed in any current path, since nothing dereferences `owner_` today, but the invariant "surfaces must not outlive their `DirectDrawImpl`" is unenforced and undocumented | N/A — currently unused | 4.1 |
| F12 | No documented concurrency model; ref-counting is atomic but surface state (`pixels_`, `texture_`, `dirty_`, `attachedDc_`) is not synchronized | Medium (latent) — fine under the single-threaded usage both games exhibit; a real hazard the moment any caller touches DirectDraw from two threads | Assumed no (games are single-threaded DirectDraw callers) | 4.2 |

Severity is not the same as priority: F1 and F2 are the two findings a reader should act on first,
because they are the only High-impact findings that are also confirmed reachable by both target
games' real, everyday code paths — every present, every sprite draw.

## 3. Performance analysis

### 3.1 Build configuration: no default `CMAKE_BUILD_TYPE` (new finding)

`build/CMakeCache.txt` (the repo's own existing build directory, produced by following the
top-level build instructions with no extra flags) shows:

```
CMAKE_BUILD_TYPE:STRING=
CMAKE_CXX_FLAGS:STRING=
```

The root `CMakeLists.txt` never sets a default `CMAKE_BUILD_TYPE` (confirmed by grep — no
`CMAKE_BUILD_TYPE` assignment anywhere in it). GCC/Clang's behavior for an empty build type is to
apply **no optimization flags at all** (effectively `-O0`), since only `CMAKE_CXX_FLAGS_RELEASE` /
`_DEBUG` / `_RELWITHDEBINFO` / `_MINSIZEREL` carry `-O` flags, and none of those variables are
consulted when `CMAKE_BUILD_TYPE` is empty.

**Measured impact** (Section 7, Benchmark 2): the same `BltFast` hot path is **6.6x faster**
(4.70ms → 0.71ms per 640x480 call) when built with `-DCMAKE_BUILD_TYPE=Release` versus the
project's own default configuration. This affects every function in the library, not just
DirectDraw — DirectPlay's packet handling and DirectSound's mixing would see the same class of
regression.

This is arguably the single highest-leverage finding in this audit: it is a one-line `CMakeLists.txt`
fix (`if(NOT CMAKE_BUILD_TYPE) set(CMAKE_BUILD_TYPE Release) endif()`, guarded so an explicit
`-DCMAKE_BUILD_TYPE=Debug` from a caller is still respected) with no behavioral risk, and it changes
the performance of literally every measurement in this document by ~6.6x.

### 3.2 `BlitFrom`: no 1:1 (no-scale) fast path (new finding, empirically measured)

`BlitFrom` (`DirectDraw.cpp:558-652`) is the shared CPU-blit engine for both `Blt()` and `BltFast()`.
Every destination pixel goes through:

```cpp
// DirectDraw.cpp:580-587
for (int y = 0; y < dstHeight; ++y) {
    const int srcY = sourceClamped.top + (y * srcHeight) / dstHeight;
    const int dstY = destClamped.top + y;
    for (int x = 0; x < dstWidth; ++x) {
        const int srcX = sourceClamped.left + (x * srcWidth) / dstWidth;
        const int dstX = destClamped.left + x;

        if (bpp_ == 8 && source.GetBPP() == 8) {
```

Two costs apply on **every pixel of every blit, including a 1:1 copy where `srcWidth == dstWidth`**:

1. An integer division (`(x * srcWidth) / dstWidth`) to compute `srcX`, even when the division is
   mathematically the identity (`x`). Integer division is one of the few arithmetic operations most
   compilers cannot eliminate here, because `srcWidth`/`dstWidth` are runtime values, not
   compile-time constants — the compiler cannot prove `srcWidth == dstWidth` ahead of time.
2. A per-pixel `bpp_ == 8` / `bpp_ == 32` branch that is loop-invariant (bpp cannot change mid-blit)
   but is re-evaluated every iteration, which also defeats auto-vectorization of what should be a
   straight-line copy loop.

`BltFast` (`IDirectDrawSurface::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE, LPRECT lpSrcRect,
DWORD dwTrans)`) has **no destination-size parameter at all** in the real DirectDraw 3 API — it is
1:1 by construction, never capable of scaling. Since both games call `BltFast` far more than `Blt`
(`directdraw-limitations.md`: 6 vs. 4 in `free-eggbert`, 5 vs. 3 in `planetblupi`), and every one of
those `BltFast` calls is provably non-scaling, the missing fast path is pure, permanent waste on the
majority of real blit traffic in both games — not a theoretical edge case.

**Measured impact** (Section 7, Benchmark 2, `-O3`): a 640x480 1:1 32-bit `BltFast` costs
**0.715ms/call — 29x slower than a raw `memcpy` of the identical byte count** (0.0244ms). At the
unoptimized default build (Section 3.1), the same call costs 4.70ms — **206x slower than `memcpy`**.
For scale: `CPixmap::Display()` (both games) issues one full-screen `Blt` every frame; at 60fps the
frame budget is 16.67ms, so even the `-O3` figure alone consumes ~4.3% of one frame for a single
full-screen blit, before counting the per-sprite `BltFast` calls (17-18 call sites in `free-eggbert`,
similarly in `planetblupi`) that pay the same per-pixel tax at smaller sizes.

A fast path would check `srcWidth == dstWidth && srcHeight == dstHeight` once, before the loop, and
route to a per-row `memcpy` (or `std::copy`) when true and no color key is requested. This is a
correctness-preserving optimization, not a behavior change.

### 3.3 `ReleaseDC`: O(width×height×256) nearest-palette search (already known algorithmically, newly measured)

`ReleaseDC`'s 8-bit path (`DirectDraw.cpp:956-984`) converts a temporary 32-bit RGBA buffer back to
palette indices with, for every pixel, a **linear scan of all 256 palette entries** computing squared
RGB distance:

```cpp
// DirectDraw.cpp:964-983
for (size_t i = 0; i < pixelCount; ++i) {
    ...
    int bestIdx = 0;
    int bestDist = INT_MAX;
    for (int j = 0; j < 256; ++j) {
        const int dr = static_cast<int>(r) - static_cast<int>(entries[j].peRed);
        ...
        if (dist < bestDist) { bestDist = dist; bestIdx = j; if (dist == 0) break; }
    }
    pixels_[i] = static_cast<uint8_t>(bestIdx);
}
```

**Measured** (Section 7, Benchmark 1) for one 640x480 8-bit surface: **2.60ms** at the project's
default (unoptimized) build, **0.427ms** at `-O3`. Section 8.2 establishes this branch is currently
unreachable by either target game, so this cost is not paid today — but it is a real algorithmic
defect (exact nearest-neighbor search is O(n·256) where a k-d tree or a precomputed inverse lookup
table would be O(n) or O(n log 256)) that would matter the moment any 8-bit surface path becomes
reachable (e.g. if F1/the `dwBPP` discard documented in `directdraw-limitations.md` is ever fixed).

### 3.4 Minor performance notes

- **`dynamic_cast` in `Blt`/`BltFast`** (`DirectDraw.cpp:662` and the equivalent in `BltFast`):
  every blit call pays an RTTI-based downcast from `LPDIRECTDRAWSURFACE` to
  `DirectDrawSurfaceImpl*`. Since `DirectDrawSurfaceImpl` is `final` and this is the only concrete
  implementation of `IDirectDrawSurface` in the codebase, a `static_cast` would be correctness-safe
  and avoid the RTTI lookup — a small, low-risk win given `dynamic_cast` runs on every single blit.
- **Logging-gate calls are not the smoking gun for F2/3.2**: every `SDL_Log`/`PresentLog`/`ColorKeyLog`
  call re-checks its debug flag via `SDL_getenv` (`DirectDraw.cpp:22-33`), but these calls are all
  **outside** the per-pixel loops (once per `Blt`/`BltFast`/`GetDC`/`ReleaseDC` call, not once per
  pixel) — confirmed by reading every `SDL_Log` call site in the file. At 1-2 `getenv` calls per
  blit this is negligible (nanoseconds) next to the millisecond-scale costs in 3.2/3.3, but it is
  still a repeated syscall-adjacent check on a path that will get hotter as the fast path in 3.2 is
  fixed — worth hoisting into a cached value if F2 is ever addressed.
- **Per-frame 8-bit→RGBA32 primary conversion** (`PresentPrimary`, `DirectDraw.cpp:1485-1519`):
  already uses a persistent `paletteConvertBuffer_` to avoid per-frame heap allocation (a real,
  already-applied optimization, visible in the comment at `DirectDraw.cpp:1487`). Confirmed
  unreachable today for the same reason as 3.3 (no 8-bit primary surface exists in practice), but
  unlike 3.3 this path is O(n), not O(n·256), so it is not a comparable concern if it ever becomes
  reachable.

## 4. Memory safety and lifecycle analysis

### 4.1 `owner_`: unmanaged back-pointer (new finding)

```cpp
// DirectDraw.cpp:315
DirectDrawImpl* owner_;
```

Every `DirectDrawSurfaceImpl` stores a raw, non-ref-counted pointer back to the `DirectDrawImpl`
that created it, set once in the constructor (`DirectDraw.cpp:424-433`) and never reassigned or
cleared. Nothing in `DirectDrawSurfaceImpl` currently dereferences `owner_` (confirmed by grep — it
is written once and never read anywhere in the file), so there is no live bug today. But the
invariant this implies — "a surface must not outlive the `DirectDrawImpl` that created it" — is
neither enforced (a caller can `Release()` the `IDirectDraw` while holding a surface reference; COM
reference-counting rules don't require surfaces to hold a reference on their parent, but that also
means nothing stops this) nor documented anywhere (no header comment, no `docs/` note). If a future
task ever makes use of `owner_` (e.g. to look up the shared renderer from a surface, which would be
a natural way to fix F6/4.6), that use would inherit an unenforced lifetime contract silently.
Recommend either documenting the contract explicitly (header comment on `owner_` plus a `NEXT.md`/
`plan.md` note) or removing the field until it's actually needed, per this project's own
no-speculative-surface policy.

### 4.2 Concurrency model is undocumented

Reference counting uses `std::atomic<ULONG>` in all four classes (`DirectDrawPaletteImpl`,
`DirectDrawClipperImpl`, `DirectDrawSurfaceImpl`, `DirectDrawImpl`), which suggests some intent
toward thread-safety. But none of the actual mutable state touched by every method —
`pixels_`, `texture_`, `dirty_`, `attachedDc_`, `dcTempBuffer_`, `paletteConvertBuffer_`,
`renderer_`, `logicalPresentationSet_`, `lastPresentNs_` — is protected by any lock or atomic. Two
threads calling `Blt`/`BltFast`/`GetDC`/`PresentPrimary` concurrently on the same surface would race
on plain `std::vector` reads/writes with no synchronization, which is undefined behavior in C++.

This is not a live bug: both `free-eggbert` and `planetblupi` are single-threaded Win32 message-loop
programs, and nothing in FreeDirect currently spawns a second thread that touches DirectDraw
objects. But the atomic ref-counts create an appearance of thread-safety that the rest of the class
doesn't back up, which is a documentation gap worth closing explicitly (a one-line header comment —
"DirectDraw objects are not thread-safe beyond ref-counting; all calls on a given object must come
from a single thread" — would prevent a future contributor from reasonably-but-wrongly assuming more).

### 4.3 `GetDC`/`ReleaseDC` lifecycle: correct cleanup, one missing error path

**What's correct**: `attachedDc_` and `dcTempBuffer_` are cleaned up in every exit path that matters —
`ReleaseDC` destroys the DC and clears `dcTempBuffer_` (`DirectDraw.cpp:984-989`), and the destructor
also destroys `attachedDc_` if a caller never called `ReleaseDC` at all (`DirectDraw.cpp:462-465`).
No leak was found here.

**What's missing**: real DirectDraw returns `DDERR_DCALREADYCREATED` if `GetDC` is called a second
time before the first DC is released. FreeDirect's `GetDC` (`DirectDraw.cpp:875-934`) instead does:

```cpp
if (!attachedDc_) {
    ... create ...
}
*lphDC = attachedDc_;   // reached unconditionally, even if attachedDc_ already existed
return DD_OK;
```

`DDERR_DCALREADYCREATED` **is already defined** in `include/ddraw.h:128` — it's simply never
returned anywhere in the `.cpp`. This is a real, confirmed behavioral deviation from documented
DirectDraw semantics that is not yet listed in `docs/directdraw-limitations.md`. Confirmed
unreachable by both games' real call sites (Section 8.3 below) — every real `GetDC` call is tightly
paired with `ReleaseDC` in the same short function, no nesting.

### 4.4 `CreateSurface`: no bound on `dwWidth`/`dwHeight` before an unchecked allocation (new finding)

```cpp
// DirectDraw.cpp:1352-1353 (offscreen branch)
width = static_cast<int>(lpDDSurfaceDesc->dwWidth);
height = static_cast<int>(lpDDSurfaceDesc->dwHeight);
```

`dwWidth`/`dwHeight` are caller-supplied `DWORD`s with no upper-bound check anywhere before they
reach:

```cpp
// DirectDraw.cpp:438 (constructor)
pixels_.resize(static_cast<size_t>(width_) * static_cast<size_t>(height_) * (bpp_ / 8u), 0);
```

Two distinct failure modes:

1. A `DWORD` near `0xFFFFFFFF` converted to `int` (`static_cast<int>`) can produce a negative
   `width_`/`height_`. Casting a *negative* `int` to `size_t` in the multiplication above produces a
   huge positive value (e.g. `width_ == -1` → `static_cast<size_t>(-1)` is `SIZE_MAX`).
2. Even a large *positive* `dwWidth`/`dwHeight` (still well within `DWORD` range) multiplies out to
   a multi-gigabyte-or-larger `resize()` request.

Either way, `std::vector::resize` throws `std::length_error` or `std::bad_alloc` on an unsatisfiable
request. `CLAUDE.md`'s own Coding Style section states "no exceptions crossing the COM-style
interface boundary" — but there is no `try`/`catch` anywhere around this constructor call
(`DirectDrawImpl::CreateSurface`, `DirectDraw.cpp:1361-1370`, only checks `new (std::nothrow)`
returning `nullptr`, which guards allocation of the `DirectDrawSurfaceImpl` object itself, not the
`std::vector` growth that happens *inside* its constructor body). An uncaught exception here
propagates out of a `WINAPI`-declared virtual method and crashes the process — a robustness gap at
a genuine public API boundary, not merely a style nit.

**Reachability**: confirmed **not reachable** by either target game — every real `CreateSurface`
call site in both games (traced through `ddutil.cpp`'s `DDLoadBitmap`/surface-creation helpers and
`pixmap.cpp`'s primary/back-buffer setup) passes small, fixed literal dimensions (640x480 or
smaller icon sizes). This is a hardening gap for API-boundary robustness, not a bug either game can
currently trigger.

### 4.5 Diagnostic counter pairing — verified balanced

Spot-checked every `FREE_DIRECT_DIAG_INC`/`FREE_DIRECT_DIAG_DEC` pair in the file (gated behind
`FREE_DIRECT_DIAGNOSTICS`, off in the default build, so this only matters when that flag is turned
on):

| Counter | INC site | DEC site | Balanced? |
|---|---|---|---|
| `ddPalettes` | ctor, `DirectDraw.cpp:168` | dtor, `DirectDraw.cpp:183` | Yes |
| `ddClippers` | ctor, `DirectDraw.cpp:227` | dtor, `DirectDraw.cpp:232` | Yes |
| `ddSurfaces` | ctor, `DirectDraw.cpp:434` | dtor, `DirectDraw.cpp:480` | Yes |
| `ddInstances` | ctor, `DirectDraw.cpp:1151` | dtor, `DirectDraw.cpp:1175` | Yes |
| `sdlTextures` | `PresentPrimary`, `DirectDraw.cpp:1475` (only on first successful create, gated by `if (!primary.texture_)`) | surface dtor, `DirectDraw.cpp:469` (only if `texture_` non-null) | Yes — texture is created at most once per surface and only ever freed in the destructor; no path overwrites `texture_` without going through destruction first |

No unpaired increment or decrement was found. This also confirms, as a side effect, that
`PresentPrimary` never *replaces* an existing `primary.texture_` — it only creates one when null
(Section 4.6 explains why that matters).

### 4.6 `SetCooperativeLevel`: renderer replaced without invalidating existing surfaces' textures

```cpp
// DirectDraw.cpp:1217-1230
SDL_Renderer* windowRenderer = SDL_GetRenderer(sdlWindow_);
if (windowRenderer && windowRenderer != renderer_) {
    SDL_DestroyRenderer(windowRenderer);
    ...
}
if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
}
renderer_ = SDL_CreateRenderer(sdlWindow_, NULL);
```

If `SetCooperativeLevel` is ever called a second time on the same `DirectDrawImpl` *after* a primary
surface already has a cached `texture_` (Section 3.4/`PresentPrimary`'s `if (!primary.texture_)`
caching), the old renderer is destroyed and a new one created, but the primary surface's `texture_`
pointer is untouched — it still points at a texture that belonged to the now-destroyed renderer.
`PresentPrimary`'s cache check (`DirectDraw.cpp:1468`, `if (!primary.texture_)`) sees a non-null
pointer and skips recreation, then passes that stale texture into `SDL_RenderTexture(renderer_,
primary.texture_, ...)` against the *new* renderer — undefined behavior in SDL, not merely a
resource leak.

**Reachability**: confirmed **not reachable**. Both target games call `DirectDrawCreate` once and
`SetCooperativeLevel` exactly once (an if/else between `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN` and
`DDSCL_NORMAL`, never both), strictly before `CreateSurface` is ever called — verified by reading
`free-eggbert/src/pixmap.cpp`'s startup sequence directly. There is no re-entry, loop, or
mode-switch path in either game that calls `SetCooperativeLevel` twice. This finding stands as a
real robustness gap (worth a guard clause or a documented precondition) but is inert under both
games' actual usage.

## 5. Correctness against DirectDraw semantics and code quality

### 5.1 `FillColor`: 8-bit branch skips `MarkDirty()` for primary surfaces (new finding)

```cpp
// DirectDraw.cpp:513-547
HRESULT DirectDrawSurfaceImpl::FillColor(const RECT* destRect, const DWORD fillColor)
{
    const RECT fillRect = ClampRect(...);

    if (bpp_ == 8) {
        ... fill loop ...
        return DD_OK;                              // <-- returns here
    }

    ... 32-bit fill loop ...

    if (type_ == SurfaceType::Primary) {
        MarkDirty();                                // <-- only reached by the 32-bit path
    }
    return DD_OK;
}
```

The 8-bit branch's early `return DD_OK;` skips the `MarkDirty()` call that the 32-bit branch reaches
at the bottom of the function. If `FillColor` were ever invoked on an 8-bit **primary** surface (the
`DDBLT_COLORFILL` path in `Blt`, or the CMake/env-gated debug-primary-clear self-test), the fill
would silently never be flagged for presentation — the dirty-flag-based present logic
(`directdraw-limitations.md` already documents this mechanism) would never pick it up, and the
screen would not update.

**Reachability**: confirmed **not reachable**. `directdraw-limitations.md` already establishes
neither game ever uses `DDBLT_COLORFILL`, and `SetDisplayMode`'s `dwBPP` discard means no primary
surface is ever created at 8bpp in practice either — this bug needs *both* conditions
simultaneously, and neither one occurs today. Still a real branch-asymmetry defect worth fixing
alongside F2/3.2 if `BlitFrom`/`FillColor` are ever touched for the performance work, since the fix
is a one-line move of the `MarkDirty()` check to the top of the function (or removing the early
`return` in favor of a shared tail).

### 5.2 `DirectDrawPaletteImpl::GetEntries`/`SetEntries`: integer-overflow bypass of the bounds check (new finding)

```cpp
// DirectDraw.cpp:199-206 (GetEntries) and 208-215 (SetEntries, identical shape)
HRESULT WINAPI GetEntries(DWORD dwFlags, DWORD dwBase, DWORD dwNumEntries, LPPALETTEENTRY lpEntries) override {
    (void)dwFlags;
    if (!lpEntries || dwBase + dwNumEntries > 256) return DDERR_INVALIDPARAMS;
    for (DWORD i = 0; i < dwNumEntries; ++i) {
        lpEntries[i] = entries_[dwBase + i];
    }
    return DD_OK;
}
```

`dwBase` and `dwNumEntries` are both `DWORD` (32-bit unsigned). `dwBase + dwNumEntries` is computed
in `DWORD` arithmetic, which wraps on overflow. A caller passing e.g. `dwBase = 0xFFFFFFFF,
dwNumEntries = 2` gets `dwBase + dwNumEntries == 1` (wrapped), which passes the `> 256` check, and
the loop then indexes `entries_[0xFFFFFFFF]` and `entries_[0]` — the first is a massive
out-of-bounds access. `entries_` is a fixed `PALETTEENTRY entries_[256]` member
(`DirectDraw.cpp:220`); in `GetEntries` this is an out-of-bounds **read** into caller memory, and in
`SetEntries` it is an out-of-bounds **write** into `DirectDrawPaletteImpl`'s own memory — the more
serious of the two, since it's a memory-corruption primitive if ever reachable with attacker- or
bug-controlled input.

**Reachability**: confirmed **not reachable** by any real call site in either game — both games only
ever call these with fixed, sane arguments (`0, 256` or similar). This is also **not covered by the
existing test suite**: `Test_Palette_GetEntries_OutOfRangeReturnsInvalidParams` and
`Test_Palette_SetEntries_OutOfRangeReturnsInvalidParams`
(`tests/directdraw_tests.cpp:1053-1075`) both use `dwBase=250, dwNumEntries=10` — a plain
over-range case (`260 > 256`) that never exercises the wraparound. A boundary-condition input
validation gap at a public API surface, independent of current game usage, and a concrete
addressable test gap (Section 9).

### 5.3 `GetSurfaceDesc`: magic numbers duplicate already-named constants

```cpp
// DirectDraw.cpp:1009
lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = (bpp_ == 8) ? 0x00000020L : 0x00000040L; // DDPF_PALETTEINDEXED8 : DDPF_RGB
```

`include/ddraw.h:182-183` already defines `#define DDPF_PALETTEINDEXED8 0x00000020L` and
`#define DDPF_RGB 0x00000040L`, and `ddraw.h` is already `#include`d at `DirectDraw.cpp:6` — there
is no technical reason (e.g. avoiding a header dependency) for the raw hex literals. The values are
correct (verified against `free-eggbert/dxsdk3/sdk/inc/ddraw.h`, the vendored real DirectX 3 SDK
header, earlier in this project's history), so this is pure maintainability risk: a future edit to
either constant's value in the header would silently desync from this hardcoded copy. The comment
even names the correct constants, confirming the author knew them and chose not to use them (likely
an oversight, not a deliberate choice worth preserving).

### 5.4 bpp handling: refining the existing finding's framing

`docs/directdraw-limitations.md` already documents that `SetDisplayMode`'s `dwBPP` parameter is
accepted but never stored, and that neither game's own surface-creation helpers set
`DDSD_PIXELFORMAT`, so both games end up all-32bpp in practice. This audit confirms that finding
still holds, and adds one precision: **the bpp-honoring mechanism inside `CreateSurface` itself is
not broken.** For offscreen surfaces:

```cpp
// DirectDraw.cpp:1354-1356
if (lpDDSurfaceDesc->dwFlags & DDSD_PIXELFORMAT) {
    bpp = static_cast<int>(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount);
}
```

If a caller *does* set `DDSD_PIXELFORMAT` with `dwRGBBitCount = 8`, `CreateSurface` honors it
correctly and produces a real 8-bit surface. The reason both games end up 32bpp everywhere is purely
that neither one's own bitmap-loading code (`DDLoadBitmap` in each game's `ddutil.cpp`) ever sets
that flag — confirmed by reading `planetblupi/src/ddutil.cpp`'s `DDLoadBitmap`
(`ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH;`, no `DDSD_PIXELFORMAT`) and cross-checking
that this is the exact function behind `m_lpDDSurface[channel] = DDLoadBitmap(...)`
(`planetblupi/src/pixmap.cpp:593`), the surface array `IsIconPixel` operates on. This distinction —
"the mechanism works; it's simply never exercised by either game's own asset-loading code, not a
FreeDirect defect" — is worth keeping precise, since it changes where a future fix (if one were ever
wanted) would need to land: in the two games' `ddutil.cpp`, not in FreeDirect. Per `CLAUDE.md`, that
is explicitly out of bounds for this project regardless (never modify target game source).

### 5.5 Header `@note Status:` tag accuracy (cross-reference, not new)

`docs/directdraw-limitations.md` already flags that `GetDC`/`ReleaseDC` are tagged `@note Status:
STUB` in `include/ddraw.h:413-416` despite being functionally substantial (confirmed again by this
audit's fresh reading of the full `GetDC`/`ReleaseDC` implementation, Sections 3.3/4.3). Restating
here only to note this audit's independent reading reached the same conclusion — no new information,
included for completeness of this document's coverage of the header/implementation boundary.

## 6. Code quality observations

### 6.1 `#define SDL_Log DirectDrawLog` (already known from earlier reading, restated for completeness)

```cpp
// DirectDraw.cpp:114
#define SDL_Log DirectDrawLog
```

This macro redefines the real SDL3 `SDL_Log` function name for the rest of the translation unit,
routing every subsequent `SDL_Log(...)` call through the debug-gated `DirectDrawLog` wrapper
(`DirectDraw.cpp:66-76`). It works correctly today — everything after line 114 in the same file
consistently means "gated log," and nothing after that point needs the real `SDL_Log`. But
preprocessor macros are not scoped to a namespace or a function; they are file-global from the point
of definition onward. A future edit that adds a call needing the *real*, unconditional `SDL_Log`
anywhere below line 114 in this file would silently get the gated version instead, with no compiler
diagnostic. A free function with a distinct name (the file already has this pattern for
`PresentLog`/`ColorKeyLog`/`PerfLog` — only the primary one reuses `SDL_Log`'s name) would be
equally convenient and remove the shadowing risk entirely.

### 6.2 Other style notes

- `GetPitch()` (`DirectDraw.cpp:294`) computes `width_ * (bpp_ / 8)` in plain `int` arithmetic. For
  the surface sizes both games actually use (max ~640x480) this cannot overflow, but it's the same
  class of unchecked-arithmetic pattern as F4/4.4 — worth keeping in mind if this method is ever
  reused for a larger surface in the future.
- The five debug-flag-check functions (`IsDirectDrawDebugEnabled`, `IsPresentationDebugEnabled`,
  `IsColorKeyDebugEnabled`, `IsPerfDebugEnabled`, `IsDebugPrimaryClearEnabled`,
  `DirectDraw.cpp:39-158`) are structurally identical except for the env var name and the
  compile-time `#ifdef` guard, and one of the five (`IsPerfDebugEnabled`) additionally caches its
  result in a function-local `static int` while the other four re-check `SDL_getenv` on every call.
  Given Section 3.4 already shows this doesn't matter for current perf (calls are outside per-pixel
  loops), this is a pure duplication/consistency nit, not a performance issue — five near-identical
  ~15-line functions could be one parameterized helper.
- `IsDebugPrimaryClearEnabled` (`DirectDraw.cpp:143-158`) duplicates the exact string-comparison
  logic from `IsEnvFlagEnabled` (`DirectDraw.cpp:22-33`) inline instead of calling it, for no
  apparent reason — the other four debug-flag functions all call the shared helper.

## 7. Empirical benchmark appendix

To avoid relying on Big-O reasoning alone, a standalone benchmark was compiled and linked directly
against this project's real, compiled `libfree-direct.a` (not a reimplementation of the algorithms
under test), run headlessly (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) against both the
repo's existing default build (`build/`, no `CMAKE_BUILD_TYPE` — Section 3.1) and a freshly
configured `-DCMAKE_BUILD_TYPE=Release` build (`build-release/`), using identical CMake options
otherwise (`-DFREE_API_USE_SYSTEM_SDL3=ON`, matching the existing build's cached configuration).

**Benchmark 1 — `GetDC`/`ReleaseDC` round-trip, 8-bit 640x480 offscreen surface** (isolates the
O(n·256) cost from Section 3.3):

| Build | `ReleaseDC` (avg of 10 calls) |
|---|---|
| Default (`build/`, no `-O` flags) | 2.60 ms |
| `-DCMAKE_BUILD_TYPE=Release` (`-O3`) | 0.427 ms |

**Benchmark 2 — `BltFast`, 1000x, 32-bit 640x480, no scaling, no color key** (isolates the missing
fast-path cost from Section 3.2), compared against a raw `memcpy` of the same byte count
(640×480×4 bytes) for the same iteration count:

| Build | `BltFast` (ms/call) | `memcpy` (ms/call) | Ratio |
|---|---|---|---|
| Default (`build/`, no `-O` flags) | 4.704 | 0.0229 | 205.8x |
| `-DCMAKE_BUILD_TYPE=Release` (`-O3`) | 0.715 | 0.0244 | 29.4x |

Methodology notes:

- Both benchmarks call only public `IDirectDraw`/`IDirectDrawSurface` API entry points
  (`DirectDrawCreate`, `CreateSurface`, `GetDC`, `ReleaseDC`, `BltFast`) — no internal/private access
  was used, so these numbers reflect exactly what a real caller experiences.
- `BltFast` was called with `DDBLTFAST_NOCOLORKEY` to isolate the scaling-math/branch overhead from
  color-key comparison cost.
- The `ReleaseDC` benchmark used a fresh `GetDC`/`ReleaseDC` pair per iteration (not reusing an
  already-attached DC), matching real call-site usage (Section 4.3).
- Timing used `std::chrono::steady_clock`, single-threaded, one warm-up call before the first
  `ReleaseDC` measurement in Benchmark 1 to exclude one-time allocation cost from the averaged loop.
- Full benchmark source: written to the session scratchpad during this audit
  (`bench_ddraw.cpp`), not committed to the repository — it is a diagnostic tool, not project
  source, consistent with this project's policy of not accumulating speculative test/demo code
  outside `tests/`. Reproducible by any future contributor from the exact API calls shown above.

## 8. Real call-site cross-check reference

This section documents the call-site tracing done specifically for this audit, beyond what
`docs/directdraw-limitations.md` already established.

### 8.1 `BltFast` is 1:1 by construction in both games

`IDirectDrawSurface::BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE lpDDSrcSurface, LPRECT
lpSrcRect, DWORD dwTrans)` has no destination-width/height parameter in the real DirectDraw 3 API —
every `BltFast` call blits `lpSrcRect` at 1:1 scale to `(dwX, dwY)`. This is a property of the API
itself, not something that needed per-call-site verification — but it does mean every confirmed
`BltFast` call site in both games (`free-eggbert/src/pixmap.cpp:406,574,580,612,1762,1818`;
`planetblupi/src/pixmap.cpp:395,401,433,1235,1291`) is provably non-scaling, strengthening Section
3.2's finding from "usually 1:1 in practice" to "structurally always 1:1."

### 8.2 `ReleaseDC`'s 8-bit branch: conclusively unreachable

Traced end-to-end for the one confirmed real `GetDC` call site already cited in
`directdraw-limitations.md` — `planetblupi`'s `IsIconPixel` (`pixmap.cpp:729-731`, called from
`decblupi.cpp:3399`, a live per-click gameplay hit-test): the surfaces it calls `GetDC` on
(`m_lpDDSurface[channel]`) are populated via `DDLoadBitmap(m_lpDD, pFilename, 0, 0)`
(`planetblupi/src/pixmap.cpp:593`), whose surface-creation call
(`planetblupi/src/ddutil.cpp:111,118`) sets `ddsd.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH` —
no `DDSD_PIXELFORMAT`. Per Section 5.4, that means this surface defaults to 32bpp, not 8bpp. This is
the same `DDLoadBitmap`/`ddutil.cpp` pattern already established generically in
`directdraw-limitations.md`; this audit specifically confirms it applies to `IsIconPixel`'s exact
surface array, closing the one open question left in that doc.

Separately traced `free-eggbert`'s own `GetDC`/`ReleaseDC` call sites (not previously enumerated):
all three are inside `free-eggbert/src/ddutil.cpp` itself — `DDLoadBitmap`
(`ddutil.cpp:160,163`, one round-trip using GDI `StretchBlt` to blit a loaded `HBITMAP` resource
onto the freshly created surface) and `DDColorMatch` (`ddutil.cpp:287,291,312,315`, two round-trips
using GDI `SetPixel`/`GetPixel` to let GDI perform RGB→physical-color matching for color-key setup).
Both are called exclusively from `CPixmap`'s image-loading helpers
(`free-eggbert/src/pixmap.cpp:791,851` for `DDLoadBitmap`; `DDColorMatch` is called internally by
`DDSetColorKey`, itself called right after `DDLoadBitmap` at the same call sites) — confirmed
**load-time-only, never per-frame or per-click**. This matters for Section 4.3: every real `GetDC`
call site in both games, without exception, is a short, tightly-scoped, load-time-only
`GetDC`-then-`ReleaseDC` pair, which is exactly why the missing `DDERR_DCALREADYCREATED` check
(4.3) and the O(n·256) cost (3.3, on the rare path where it would apply) are both confirmed inert.

### 8.3 The one real `Blt()` (non-`BltFast`) call site with genuine scaling potential

`free-eggbert` has exactly one non-`BltFast` `Blt()` call site outside `ddutil.cpp`:
`CPixmap::Display()` (`pixmap.cpp:1509-1516`), the once-per-frame full-screen present blit already
covered by `directdraw-limitations.md`. Unlike every `BltFast` call (8.1), this one **can** legitimately
scale: its destination rect (`DestRect`) comes from the actual window's client area
(`GetClientRect`/`ClientToScreen`, `pixmap.cpp:1499-1501`) while its source rect (`MapRect`) is the
fixed internal game resolution (`m_dim.x`/`m_dim.y`, `pixmap.cpp:1503-1506`) — these differ whenever
the window size doesn't exactly match the game's logical resolution. This is the one place in either
game's confirmed call sites where `BlitFrom`'s scaling math (Section 3.2) is doing genuinely
necessary work, not pure waste — worth keeping in mind if a fast path is ever added: the 1:1 check
must stay a runtime check (comparing actual rect sizes), not a static assumption that presentation
is always 1:1.

Separately, `free-eggbert` has exactly one *other* `Blt()` call site,
`CPixmap::DrawMap(int channel, RECT src, RECT dest)` (`pixmap.cpp:640`) — but grepping for its
caller (`DrawMap(` with the matching 3-argument signature) across the entire `free-eggbert` source
tree found **zero call sites**. The only `DrawMap(` matches anywhere in the codebase are calls to an
unrelated, differently-signatured `CDecor::DrawMap(BOOL, int)` method. `CPixmap::DrawMap` and its
one `Blt()` call therefore appear to be **dead code** in `free-eggbert`'s current reconstructed
source — consistent with this project's general finding (recorded in this session's prior DirectPlay
work) that `free-eggbert`'s reconstructed source contains UI-reachable gaps. Noted here for
completeness; not a FreeDirect defect, and per `CLAUDE.md` policy not something this project would
fix by editing game source regardless.

## 9. Test coverage assessment

`tests/directdraw_tests.cpp` has 53 tests across 9 groups (creation/lifecycle, surface memory,
blits, Flip/presentation/clipper, color key, palette, DC bridge, lost/restore, logging-gate
regression). This is solid coverage of the happy paths and the previously-known edge cases. Gaps
this audit specifically surfaced, none currently covered:

| Gap | Relates to | Suggested test shape |
|---|---|---|
| Palette `dwBase`/`dwNumEntries` integer-overflow bypass | F5 / 5.2 | `GetEntries(0, 0xFFFFFFFFu, 2, entries)` and the `SetEntries` equivalent must both return `DDERR_INVALIDPARAMS`, not read/write out of bounds |
| `FillColor` on an 8-bit primary surface doesn't mark dirty | F7 / 5.1 | Create an 8-bit primary surface (requires `DDSD_PIXELFORMAT` at creation, per 5.4), issue `Blt` with `DDBLT_COLORFILL`, assert the dirty flag / a subsequent `PresentPrimary` actually uploads |
| `GetDC` called twice without an intervening `ReleaseDC` | F8 / 4.3 | Second `GetDC` call should return `DDERR_DCALREADYCREATED`, not silently succeed with the same handle |
| `CreateSurface` with a very large or negative-once-cast `dwWidth`/`dwHeight` | F4 / 4.4 | Should return `DDERR_INVALIDPARAMS` (once a bound is added) rather than propagate an allocation exception — this test can only pass *after* a fix; today it would crash the test binary, which is itself worth confirming once, deliberately, in an isolated process |
| `SetCooperativeLevel` called twice with a surface already presented | F6 / 4.6 | Assert no crash / no stale-texture use after a second `SetCooperativeLevel` call — needs a way to trigger `SDL_GetRenderer(window) != renderer_` deliberately |

None of these are needed for either target game today (Section 8 establishes none of the underlying
paths are reachable), so they are not blocking — but they're concrete, cheap additions if any of
Sections 4-5's fixes are ever undertaken, giving each fix a regression guard from day one.

## 10. Additional audit dimensions (self-identified)

Beyond performance/memory/correctness, the following dimensions turned up material findings and are
worth tracking as ongoing audit categories for this subsystem, not just one-off items:

- **Build configuration hygiene** (Section 3.1) — not a DirectDraw-specific concern at all; this
  finding applies to the whole project and arguably belongs in a project-wide (not DirectDraw-only)
  follow-up, flagged here because it was discovered while benchmarking DirectDraw.
- **API-boundary input validation** (F4, F5) — `CreateSurface` and `GetEntries`/`SetEntries` both
  trust caller-supplied sizes without bounds-checking before they reach raw arithmetic or container
  operations. Worth a dedicated pass across *all* public `include/ddraw.h` entry points (this audit
  checked the ones already touched for other reasons, not exhaustively every method) to see if the
  same pattern recurs in `Lock`, `BltFast`'s `lpSrcRect`, or `SetColorKey`.
- **Exception-safety-boundary compliance** (F4) — `CLAUDE.md` states no exceptions may cross the
  COM-style interface boundary, but this audit found at least one path (unchecked `vector::resize`)
  where that boundary can be crossed by an uncaught standard-library exception. Worth a systematic
  grep for every `std::vector`/`std::string` operation that can throw (`resize`, `at`, `reserve`,
  `substr`, ...) reachable from a public virtual method, across all of `DirectDraw.cpp`, not just
  the one site found here.
- **Header/implementation status-tag consistency** (5.5, cross-referencing existing work) — worth
  turning into a lightweight recurring check (e.g. a script that flags any `@note Status: STUB`
  header comment whose corresponding `.cpp` method body exceeds some trivial line count) rather than
  a one-time audit finding, since this project explicitly relies on that tag being trustworthy.
- **Test-gap tracking as a first-class audit output** (Section 9) — recording *which specific new
  edge cases a code-reading pass turned up but the test suite doesn't cover* is more actionable than
  a generic "add more tests" recommendation; worth doing this explicitly any time a correctness
  finding is written up, not just in a dedicated audit.
- **Empirical verification of performance claims** (Section 7) — this audit measured against the
  real compiled library rather than reasoning from Big-O alone, and that surfaced the build-type
  finding (3.1) purely as a side effect of needing a fair baseline. Worth keeping as standard
  practice for any future DirectSound/DirectPlay performance audit too.

## 11. Summary findings table

| Finding | Severity (impact) | Reachable today | New in this audit? |
|---|---|---|---|
| No default `CMAKE_BUILD_TYPE` (3.1) | High | Yes | Yes |
| `BlitFrom` missing 1:1 fast path (3.2) | High | Yes | Partially — mechanism known, empirical measurement and "always 1:1 via `BltFast`" proof are new |
| `ReleaseDC` O(n·256) palette search (3.3) | High (if triggered) | No (confirmed) | Algorithm known from code reading before this audit; empirical measurement and definitive unreachability proof are new |
| `CreateSurface` unchecked size → uncaught exception (4.4) | High (if triggered) | No | Yes |
| Palette `GetEntries`/`SetEntries` integer-overflow bypass (5.2) | High (if triggered) | No; also untested | Yes |
| `SetCooperativeLevel` stale-texture-after-renderer-replace (4.6) | High (if triggered) | No (confirmed) | Refines a previously-known concern with definitive call-order proof |
| `FillColor` 8-bit branch skips `MarkDirty` (5.1) | Medium | No | Yes |
| `GetDC` missing `DDERR_DCALREADYCREATED` (4.3) | Low-Medium | No | Yes |
| `owner_` unmanaged back-pointer (4.1) | Medium (latent) | N/A (unused) | Yes |
| Undocumented single-threaded assumption (4.2) | Medium (latent) | Assumed no | Yes |
| `GetSurfaceDesc` magic numbers (5.3) | Low | N/A | Yes |
| `#define SDL_Log` shadowing (6.1) | Low | N/A | Restated from prior reading in this session |
| `dynamic_cast` per blit call (3.4) | Low | Yes (minor cost) | Yes |
| Debug-flag-function duplication (6.2) | Low | N/A | Yes |

## 12. Suggested next steps (not executed by this audit)

In rough priority order, if this audit's findings are turned into `plan.md` tasks:

1. Default `CMAKE_BUILD_TYPE` to `Release` when unset (3.1) — highest leverage, lowest risk, and
   changes the baseline for every other performance number in this document.
2. Add a 1:1 fast path to `BlitFrom` (3.2), guarded by a runtime size check per 8.3's finding that
   `CPixmap::Display()`'s present blit can legitimately scale — the fast path must not assume 1:1
   unconditionally.
3. Add bounds validation to `CreateSurface`'s `dwWidth`/`dwHeight` (4.4) and to
   `GetEntries`/`SetEntries`' overflow-prone check (5.2), both currently-unreachable-but-real
   API-boundary hardening gaps.
4. Fix `FillColor`'s `MarkDirty` asymmetry (5.1) — a one-line change once someone is already in that
   function for another reason.
5. Everything else in Section 11 at Low/Medium severity, opportunistically.

Each of these would need its own atomic `plan.md` task per `CLAUDE.md`'s task-authoring rules
(one task = one change); this audit intentionally stops short of drafting those tasks itself, since
that step is implementation planning, not analysis.
