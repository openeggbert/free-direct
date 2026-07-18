# Free Direct – TODO & Review Notes (Historical)

This document originally listed current issues, limitations, and next steps for both the
**Free API (WinAPI subset)** and **Free Direct (DirectDraw subset)** implementations, as they
stood at an earlier review.

On 2026-07-18, every item was re-checked against current source and this file was split:

* Everything confirmed already fixed in code (or already documented in `docs/*.md` exactly as the
  original item asked) was **removed** entirely.
* Every item about `free-api` — fixed or still open — was **moved out** to
  [`../freeapiissues.md`](../freeapiissues.md), since `free-api` is a separate project from
  `free-direct`; none of it belongs in this repo's `TODO.md`, so no `free-api` item, resolved or
  not, is repeated below.
* What remains below is **`free-direct`-only**, and none of it is open: every DirectDraw item was
  either fixed in code or was already correctly documented as an intentional, in-scope
  simplification per `CLAUDE.md`'s DirectDraw Policy.

Per `CLAUDE.md`'s `plan.md` Policy, `plan.md` is the authoritative forward-looking task list and
`NEXT.md` is the current status snapshot; this file remains only as historical review notes and is
not deleted.

---

## Resolution summary (as of 2026-07-18) — free-direct items only

| Original item | Resolution |
|---|---|
| `Flip` is not a real DirectDraw flip chain | Resolved as originally requested ("document as simplified present mechanism") — see `docs/directdraw-limitations.md`'s "Simplified flip chain and DDBLTFX.dwFillColor interpretation" section. |
| Texture recreated every frame | Fixed — `PresentPrimary` now caches the streaming texture on the primary surface and only creates it once. |
| `DDBLTFX.dwFillColor` uses a simplified `0x00RRGGBB` interpretation | Resolved as originally requested (documented) — see `docs/directdraw-limitations.md`. |
| Header naming risk: `include/ddraw.h` vs. a real DirectX SDK header | Not a bug — `ddraw.h` deliberately mirrors the legacy DirectX SDK's `#include <ddraw.h>` layout by design (`CMakeLists.txt` comment: "Intentionally mirrors the legacy DirectX SDK include layout"), which is required for target-game source compatibility; renaming it would defeat the point of the header. `CLAUDE.md` already states FreeDirect must not require the original DirectX SDK to build or run, which covers the practical risk. |
| "Not Yet Implemented": COM correctness (`QueryInterface` etc.) | Implemented — see `QueryInterface` overrides in `src/directdraw/DirectDraw.cpp`. |
| "Not Yet Implemented": real DirectSound/DirectPlay | Actively tracked with accurate current status in `NEXT.md` and `plan.md`, not a stale TODO item (DirectSound: functional over SDL3 audio, remaining `PARTIAL` features tracked in `docs/directsound-limitations.md`/`plan.md` Phase 13; DirectPlay: real loopback/ENet transport implemented but not yet exercised by a live game session, tracked in `NEXT.md`/`plan.md`). |

---

## See also

* [`plan.md`](plan.md) — current task backlog.
* [`NEXT.md`](NEXT.md) — current implementation status.
* [`docs/directdraw-limitations.md`](docs/directdraw-limitations.md) — documented DirectDraw
  simplifications (`Flip`, `DDBLTFX.dwFillColor`, `SetDisplayMode`'s `dwBPP`, etc.).
* [`docs/directsound-limitations.md`](docs/directsound-limitations.md) — documented DirectSound
  simplifications.
* [`docs/directplay-limitations.md`](docs/directplay-limitations.md) — documented DirectPlay
  deviations from real Microsoft DirectPlay.
* [`../freeapiissues.md`](../freeapiissues.md) — open issues in the sibling `free-api` project
  (WinAPI subset), including the Web/Android port readiness analysis.
