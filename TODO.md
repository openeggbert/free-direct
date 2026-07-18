# Free Direct – TODO & Review Notes (Historical)

This document originally listed current issues, limitations, and next steps for both the
**Free API (WinAPI subset)** and **Free Direct (DirectDraw subset)** implementations, as they
stood at an earlier review.

On 2026-07-18, every item was re-checked against current source and this file was split:

* Every item about `free-api` — fixed or still open — was **moved out** to
  [`../freeapiissues.md`](../freeapiissues.md), since `free-api` is a separate project from
  `free-direct`; none of it belongs in this repo's `TODO.md`.
* Every remaining `free-direct` item was walked through individually with the user, verified
  against current source, and resolved one of three ways: fixed in code, confirmed as an
  intentional in-scope simplification per `CLAUDE.md`'s DirectDraw Policy (and documented in
  `docs/directdraw-limitations.md`), or confirmed stale (the underlying feature is now real,
  not a stub).

**Nothing remains open in this file.** Per `CLAUDE.md`'s `plan.md` Policy, `plan.md` is the
authoritative forward-looking task list and `NEXT.md` is the current status snapshot; this file is
kept only as historical review notes and is not deleted.

---

## Resolution summary (as of 2026-07-18) — free-direct items only

| Original item | Resolution |
|---|---|
| `Flip` is not a real DirectDraw flip chain | Won't fix — neither target game calls `Flip()` at all (both auto-present via `Blt`/`BltFast`); implementing a real flip chain would be a speculative addition with no call site. Documented in `docs/directdraw-limitations.md`. |
| Texture recreated every frame | **Fixed** — `PresentPrimary` caches the streaming texture on the primary surface (`if (!primary.texture_)` guard) and only creates it once; every later frame updates the same texture via `SDL_UpdateTexture`. |
| `DDBLTFX.dwFillColor` uses a simplified `0x00RRGGBB` interpretation | Won't fix — neither target game ever calls `DDBLT_COLORFILL`. Documented in `docs/directdraw-limitations.md`. |
| Header naming risk: `include/ddraw.h` vs. a real DirectX SDK header | Won't fix, by design — `ddraw.h` deliberately mirrors the legacy DirectX SDK's `#include <ddraw.h>` layout so target-game source keeps compiling unmodified; renaming it would defeat the point of the header. `CLAUDE.md` already guarantees FreeDirect never requires the real DirectX SDK to build or run, which removes the practical conflict risk. |
| "Not Yet Implemented": COM correctness (`QueryInterface`) | Confirmed still accurate for DirectDraw specifically — all four DirectDraw classes' `QueryInterface` unconditionally return `DDERR_UNSUPPORTED`, already honestly labeled `STUB` in `include/ddraw.h`. Verified by a full-source grep of both target games that neither ever calls `QueryInterface` on a DirectDraw object (the one real call site, `free-eggbert/src/network.cpp:91`, is on DirectPlay, which *is* correctly implemented). Left as a documented stub rather than fixed speculatively — see `docs/directdraw-limitations.md`. |
| "Not Yet Implemented": real DirectSound/DirectPlay | Stale — both are real implementations now, not stubs (DirectSound: `SDL_AudioStream`-backed playback; DirectPlay: `LoopbackDirectPlayTransport`/opt-in `EnetDirectPlayTransport`). Current status (including DirectPlay's remaining gap — never exercised by a live game session) is tracked in `NEXT.md`/`plan.md`, not here. |

---

## See also

* [`plan.md`](plan.md) — current task backlog.
* [`NEXT.md`](NEXT.md) — current implementation status.
* [`docs/directdraw-limitations.md`](docs/directdraw-limitations.md) — documented DirectDraw
  simplifications (`Flip`, `DDBLTFX.dwFillColor`, `QueryInterface`, `SetDisplayMode`'s `dwBPP`,
  etc.).
* [`docs/directsound-limitations.md`](docs/directsound-limitations.md) — documented DirectSound
  simplifications.
* [`docs/directplay-limitations.md`](docs/directplay-limitations.md) — documented DirectPlay
  deviations from real Microsoft DirectPlay.
* [`../freeapiissues.md`](../freeapiissues.md) — open issues in the sibling `free-api` project
  (WinAPI subset), including the Web/Android port readiness analysis.
