# Free Direct – TODO & Review Notes (Historical)

This document originally listed current issues, limitations, and next steps for both the
**Free API (WinAPI subset)** and **Free Direct (DirectDraw subset)** implementations, as they
stood at an earlier review.

On 2026-07-18, every item was re-checked against current source:

* Everything confirmed already fixed in code (or already documented in `docs/*.md` exactly as the
  original item asked) was **removed** from this file.
* Everything still open in `free-api`, or explicitly left as an accepted low-priority limitation
  rather than fixed, was **moved to** [`../freeapiissues.md`](../freeapiissues.md) — `free-api` is
  a separate project from `free-direct`, so its open issues no longer belong in this repo's
  `TODO.md`.
* No item specific to `free-direct` itself remained open: every DirectDraw item below was either
  fixed in code or was already correctly documented as an intentional, in-scope simplification per
  `CLAUDE.md`'s DirectDraw Policy.

Per `CLAUDE.md`'s `plan.md` Policy, `plan.md` is the authoritative forward-looking task list and
`NEXT.md` is the current status snapshot; this file remains only as historical review notes and is
not deleted.

---

## Resolution summary (as of 2026-07-18)

| # | Original item | Repo | Resolution |
|---|---|---|---|
| 2 | `WM_CLOSE` skipped straight to `PostQuitMessage` instead of going through `DestroyWindow`/`WM_DESTROY` | free-api | Fixed — moved to `../freeapiissues.md` history; current `winuser_message.cpp`/`winuser_window.cpp` implement the correct `WM_CLOSE → DestroyWindow → WM_DESTROY → PostQuitMessage` chain. |
| 3 | `DestroyWindow` did not emit `WM_DESTROY` | free-api | Fixed — `DestroyWindow` now synchronously dispatches `WM_DESTROY` to the window proc before teardown. |
| 4 | `CloseHandle` always returns `TRUE` | free-api | Still open — see `../freeapiissues.md`. |
| 5 | `PeekMessage` ignores `HWND`/message-range filters | free-api | Resolved as originally requested ("document as intentional subset behavior") — now has a detailed in-code justification citing full-source usage sweeps. |
| 6 | `ShowWindow`/`UpdateWindow` were no-ops | free-api | Fixed — both now perform real SDL show/hide/minimize/maximize/restore/raise operations. |
| 7 | `CreateWindowEx` ignored most parameters | free-api | Fixed — style flags, position, and size now drive real SDL window behavior; remaining unused parameters (`hWndParent`, `hMenu`, `hInstance`, `lpParam`) are stored and passed through rather than silently dropped. |
| 8 | `Flip` is not a real DirectDraw flip chain | **free-direct** | Resolved as originally requested ("document as simplified present mechanism") — see `docs/directdraw-limitations.md`'s "Simplified flip chain and DDBLTFX.dwFillColor interpretation" section. |
| 9 | Texture recreated every frame | **free-direct** | Fixed — `PresentPrimary` now caches the streaming texture on the primary surface and only creates it once. |
| 10 | `DDBLTFX.dwFillColor` uses a simplified `0x00RRGGBB` interpretation | **free-direct** | Resolved as originally requested (documented) — see `docs/directdraw-limitations.md`. |
| 11 | `OutputDebugStringW` not Unicode-safe | free-api | Still open, but originally noted as acceptable — see `../freeapiissues.md`. |
| 12 | `NULL` defined as `0` in C++ | free-api | Still open, but originally noted as not critical — see `../freeapiissues.md`. |
| 13 | Header naming risk (`windows.h`, `ddraw.h`) vs. a real Windows SDK | both | `free-api`'s half is fixed (controlled, `WIN32`-conditional include paths in `CMakeLists.txt`, avoiding duplicate `kernel32.dll` symbol definitions). `free-direct`'s `include/ddraw.h` deliberately mirrors the legacy DirectX SDK's `#include <ddraw.h>` layout by design (`CMakeLists.txt` comment: "Intentionally mirrors the legacy DirectX SDK include layout") — this is required for target-game source compatibility, not a bug; renaming it would defeat the point of the header. `CLAUDE.md` already states FreeDirect must not require the original DirectX SDK to build or run, which covers the practical risk. |
| — | "Not Yet Implemented" list (COM correctness, GDI/HDC support, multi-window routing, accurate message filtering) | mixed | `free-direct`'s share (COM/`QueryInterface`) is implemented. `free-api`'s share (GDI/HDC, multi-window routing, message filtering) is implemented/documented in `free-api`; not re-tracked here. |
| — | "Not Yet Implemented": real DirectSound/DirectPlay | **free-direct** | Not a stale TODO item — actively tracked with accurate current status in `NEXT.md` and `plan.md` (DirectSound: functional over SDL3 audio, some `PARTIAL` features tracked in `docs/directsound-limitations.md`/`plan.md` Phase 13; DirectPlay: real loopback/ENet transport implemented but not yet exercised by a live game session, tracked in `NEXT.md`/`plan.md`). |
| — | "Web and Android" feasibility analysis | free-api | Moved to `../freeapiissues.md` — concerns `free-api`'s runtime/entrypoint model (`winapi.cpp`, `winmain_bridge.cpp`), not `free-direct`'s rendering. |

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
