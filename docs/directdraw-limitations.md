# DirectDraw Limitations

This document records deliberate simplifications, known gaps, and audit findings in FreeDirect's
DirectDraw subset (`include/ddraw.h`, `src/directdraw/DirectDraw.cpp`). It exists per
`CLAUDE.md`'s Documentation Policy: honest "STUB"/"PARTIAL"/"IMPLEMENTED" labeling over silence.
See `docs/audit-24h-free-direct.md` §2.1/§4 for the full call-site audit this document is based
on, and `tests/directdraw_tests.cpp` for the automated regression coverage of the behavior
described here.

## Blt is not a minor path — corrected framing (TASK-24H-0052)

Earlier project documentation (including a prior version of `CLAUDE.md`'s DirectDraw Policy
section) characterized `Blt` as a rare/minor path relative to `BltFast`, citing call-site counts of
roughly "18 vs. 1" (free-eggbert) and "17 vs. 0" (planetblupi). A fresh call-site audit
(`docs/audit-24h-free-direct.md` §2.1) found the real counts are **4 vs. 6** (free-eggbert) and
**3 vs. 5** (planetblupi) — and, more importantly, **one `Blt` call site per game is
`CPixmap::Display()`, the once-per-frame back-buffer-to-primary present call** — the single
hottest path in both games. `BltFast` still has more raw call sites (per-sprite/icon compositing,
called many times per frame via `decor.cpp`), but `Blt` is not a minor path either. Both are now
covered by regression tests (`Test_Blt_FullSurfaceCopy_MatchesSource` specifically replicates the
`CPixmap::Display()` call shape; `Test_BltFast_OpaqueCopy_*` covers the sprite-compositing shape).

## SetDisplayMode's dwBPP is accepted but never used (new finding, TASK-24H-0053)

`IDirectDraw::SetDisplayMode(dwWidth, dwHeight, dwBPP)` stores `dwWidth`/`dwHeight` into
`displayModeWidth_`/`displayModeHeight_` (used later by `CreateSurface` to size the primary
surface), but **`dwBPP` is logged and then silently discarded — there is no `displayModeBpp_`
member anywhere in `DirectDrawImpl`.** `CreateSurface`'s primary-surface branch always creates a
32bpp primary surface, regardless of what `dwBPP` a caller requested.

**Why this is not currently an observable bug for either target game**: `free-eggbert` requests
`SetDisplayMode(640, 480, 8)` or `SetDisplayMode(640, 480, 16)` depending on `bTrueColor`
(`pixmap.cpp:203-206`); `planetblupi` requests `SetDisplayMode(640, 480, 8)` (`pixmap.cpp:171`,
hardcoded). Neither game's `ddutil.cpp` sets `DDSD_PIXELFORMAT` when creating its own offscreen
bitmap surfaces either, so those surfaces *also* default to 32bpp through a separate code path
inside `CreateSurface`'s offscreen branch. Both games therefore end up with an internally
consistent, all-32bpp surface set today — the requested-but-ignored primary bpp never causes a
visible mismatch between the primary and the surfaces blitted onto it.

**This is a real, previously-undocumented gap, not a fix made speculatively here.** Implementing
real bpp-aware primary surface creation (e.g. actually creating an 8-bit paletted primary when
`dwBPP == 8`) would be new behavior with no currently-observable call site forcing it, and would
interact with the already-PARTIAL 8-bit palette-conversion present path (see below) in ways that
need real testing against actual 8-bit primary usage before changing — out of scope to fix
speculatively per `CLAUDE.md`'s scope policy. Filed here as a known gap for future reference.

## 8-bit palette-to-RGBA32 conversion: verified correct, but currently unreachable

`PresentPrimary`'s 8-bit upload path (`paletteConvertBuffer_[i] = peRed | (peGreen << 8) |
(peBlue << 16) | 0xFF000000`) was independently audited against `FillColor`/`BlitFrom`'s byte-level
`[R, G, B, A]` memory layout (the same `SDL_PIXELFORMAT_RGBA32` layout the streaming texture
uses) and found correct — no channel swap, no off-by-one, no wrong shift width, on every
little-endian target this project supports (x86/x64/ARM/Wasm). A minor code smell was found (not a
value bug): the `hasPalette` local in this path is unconditionally set `true` in both the
"has a real palette" and "no palette, use default" branches, making the intended "no palette →
grayscale" fallback comment dead code — harmless since `GetDefault332Palette` is still applied
correctly in that branch, just not distinguished from the "has palette" case for logging purposes.

**Reachability caveat**: because of the `SetDisplayMode`/`dwBPP` gap above, both target games'
real surfaces are 32bpp, not 8bpp, so this 8-bit conversion path is not actually exercised by
either game today — its correctness here was verified by code inspection and by
`tests/directdraw_tests.cpp`'s explicit 8-bit surface tests, not by an observed live 8-bit render
from either game.

## Color-key range handling: verified correct for planetblupi's actual usage

`planetblupi`'s two `Cache()`-driven `SetColorKey`-equivalent call sites
(`src/pixmap.cpp:603,648`, via `DDSetColorKey` in `src/ddutil.cpp:405-412`) always construct a
**degenerate range** (`dwColorSpaceLowValue == dwColorSpaceHighValue`, both equal to a single
color matched via a `GetDC`/`SetPixel`/`Lock` round-trip against white, `RGB(255,255,255)`) — not
a hardcoded index, and not a genuine multi-value range (that's a different function,
`DDSetColorKey2`, used only by `SetTransparent2`, outside `Cache()`). free-direct's `SetColorKey`
stores the range verbatim, and `BlitFrom`'s comparison (`index/pixel >= low && <= high`) correctly
degenerates to an exact-match test when `low == high`, for both the 8-bit and 32-bit comparison
branches. Per the `SetDisplayMode` finding above, the branch actually exercised by planetblupi at
runtime is the 32-bit one (since its surfaces are 32bpp), not the 8-bit one — both were verified
correct regardless.

Whether `DDColorMatch`'s underlying `GetDC`/`SetPixel`/`Lock` round-trip produces the *correct*
matched color in the first place is a separate question from the range-comparison arithmetic
audited here, and depends on `GetDC`/`ReleaseDC` (see below) — the project's own highest-documented
DirectDraw risk area, tracked separately.

## GetDC/ReleaseDC: functionally real, documented STUB, highest-risk area

`GetDC`/`ReleaseDC` are tagged `STUB` in `include/ddraw.h`'s Doxygen comments, but are
functionally real: for a 32-bit surface, `GetDC` wraps the surface's own pixel buffer directly (no
copy) via `FreeApiCreateSurfaceDC`; for an 8-bit surface, it expands through a temporary
palette-converted 32-bit buffer, converting back to nearest-palette-index on `ReleaseDC`.
`planetblupi`'s `IsIconPixel` (a live per-click gameplay hit-test path, `pixmap.cpp:729-731`,
called from `decblupi.cpp:3399`) depends on this. `tests/directdraw_tests.cpp`'s
`Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock` and
`Test_SetPalette_AffectsGetDCColorExpansion` now cover the mechanism directly (though not
`IsIconPixel` itself, which is game code). The `STUB` tag remains accurate in the sense of "not
full GDI emulation," not "broken" — see `include/ddraw.h`'s own per-method comment.

## IsLost/Restore: honest inert stub, not a bug

`IsLost()` always returns `DD_OK` (never "lost"); `Restore()` always returns `DD_OK`
unconditionally. Both games' `RestoreAll()` helper (called from ~8-9 sites per game, including
every frame's `Display()`) is reachable but its `Restore()`-on-loss branch never fires, since
`IsLost()` never reports loss. This is honestly documented as `STUB` in the header and locked in by
`tests/directdraw_tests.cpp`'s `Test_IsLost_AlwaysReturnsNotLost`/`Test_Restore_ReturnsOkUnconditionally`
as current, intentional behavior — no call site in either game has been shown to need real
lost-surface recovery (this backend never loses surfaces the way a real GPU-backed DirectDraw
device could).

## Simplified flip chain and DDBLTFX.dwFillColor interpretation

`Flip()` is a "Simplified Present" — a single present call, not a real multi-buffer flip chain
with page-flipping semantics. Neither target game calls `Flip()` at all (both rely on `Blt`/
`BltFast` auto-present against the primary surface), so this deviation from real DirectDraw has no
observable effect on either game; it is tested (`tests/directdraw_tests.cpp`'s `Test_Flip_*`
tests) as implemented behavior, not as a faithful flip-chain emulation.

`DDBLTFX.dwFillColor` is interpreted as a simplified `0x00RRGGBB` value, not the real
DirectDraw-accurate value (which depends on the destination surface's actual pixel format —
different bit layouts for different bpp). Neither target game uses `DDBLT_COLORFILL` at any real
call site (confirmed by the original call-site audit); it is implemented and tested
(`Test_Blt_ColorFill_FillsDestRectWithColor`) as documented simplified behavior only, kept for
API-shape completeness.

## Presentation throttle and dirty-flag

The presentation path throttles redundant re-presents (skips upload+present if called within the
configured frame interval, default 1/60s, overridable via `FREE_DIRECT_TARGET_FPS`) and skips
re-presenting when the primary surface hasn't changed since the last present. The throttle
behavior is directly tested (`tests/directdraw_tests.cpp`'s `Test_Presentation_Throttles*`/
`_PresentsAgain*` tests, using `FREE_DIRECT_TARGET_FPS` to make the interval deterministic). The
dirty-flag check specifically **cannot be independently proven through the public API alone**:
every pixel-writing path (`Blt`/`BltFast`/`FillColor`/`BlitFrom`) unconditionally marks the primary
dirty as a side effect of writing, and `Lock()` exposes no writable pointer for the primary surface
at all — so there is no way to change the primary's content without also marking it dirty, meaning
"dirty=false skipped a present that would have shown different content" can never be constructed
as an observable black-box scenario. `Test_Presentation_RepeatedFlipWithNoChange_IsSafeAndStable`
covers the weaker, genuinely provable safety property (repeated presents with no content change is
safe and stable) instead.
