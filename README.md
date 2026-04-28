# Free Direct

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)

**Free Direct** is a C++ project that reimplements a **narrow, game-driven subset of DirectX 3 (2D)** using **SDL3** as an internal backend.

The goal is not full DirectX compatibility, but a **focused, minimal implementation** sufficient to run specific legacy games (e.g. *Speedy Blupi*) while remaining portable.

---

## Overview

Free Direct acts as a **compatibility layer**:

```
DirectX 3 (subset)
        ↓
   Free Direct
        ↓
     SDL 3
```

* **Free Direct** → reimplements selected DirectX 3 APIs (2D only)
* **DirectDraw** → Current real implementation focus, constrained to methods and flags used by the game and demo.
* **DirectSound** → Currently stubbed (dummy implementations).
* **DirectPlay** → Currently stubbed (dummy implementations).
* **Direct3D** → Not implemented (not used by target code).
* **SDL 3** → Internal implementation detail used only inside `.cpp` files.

---

## Goals

* Recreate a **minimal subset of DirectX 3 (2D)** in modern C++
* Enable running legacy DirectX-based games without original dependencies
* Keep the implementation **simple, readable, and hackable**
* Use **SDL3** internally for cross-platform rendering
* Keep compatibility behavior driven by real call sites, not by full API coverage

---

## Non-Goals

* ❌ Full DirectX 3 compatibility
* ❌ Hardware-accurate emulation
* ❌ Direct3D (3D pipeline) support
* ❌ Expanding APIs that are not used by the target game/demo

---

## Features

* **DirectDraw**: Narrow subset implemented using SDL3.
* **DirectSound**: Declarations and dummy stubs provided.
* **DirectPlay**: Declarations and dummy stubs provided.
* Surface creation for primary and system-memory/offscreen surfaces.
* `Blt` / `BltFast` with clipping and source color key handling.
* `Lock` / `Unlock` for direct pixel access.
* Palette support (`CreatePalette`, `SetEntries`, `GetEntries`, `SetPalette`).
* Primary surface presentation through SDL renderer.
* Minimal clipper support (`CreateClipper`, `SetHWnd`, `SetClipper`).

---

## Technologies

* **C++20**
* **SDL 3** (internal backend)

---

## Build Instructions

```bash
git clone https://github.com/openeggbert/free-direct.git
cd free-direct

cmake -B build
cmake --build build
```

Run example:

```bash
./build/FREE_DIRECT
```

---

## Project Status

**Work in progress**

Current focus:

* Keep the implementation limited to the used DirectDraw subset
* Stabilize surface/lock/blit behavior required by game code
* Document known limits honestly (partial/stub behavior)

Current known limitations:

* The implementation is subset-oriented, not full DirectDraw.
* Some compatibility paths are intentionally minimal (`SetDisplayMode`, `IsLost`, `Restore`).
* `GetDC` support is minimal and intended for compatibility helpers.
* 8-bit rendering is supported through palette conversion on present; full hardware-era semantics are not replicated.
* Source color-key range handling is implemented for the used subset; broader legacy edge cases are still partial.

Debug logging:

* DirectDraw debug logs can be enabled with `FREE_DIRECT_DEBUG_DDRAW=1`.
* Presentation-path debug logs can be enabled with `FREE_DIRECT_DEBUG_PRESENTATION=1`.
* Color-key diagnostics can be enabled with `FREE_DIRECT_DEBUG_COLORKEY=1`.
* Optional one-time primary clear diagnostic can be enabled with `FREE_DIRECT_DEBUG_PRIMARY_CLEAR=1`.

Color-key behavior (subset):

* `SetColorKey(DDCKEY_SRCBLT, ...)` stores low/high values per source surface.
* `BltFast` uses source color key when `DDBLTFAST_SRCCOLORKEY` is set.
* `Blt` uses source color key when `DDBLT_KEYSRC` is set.
* For 8-bit surfaces, comparisons are made against palette index values.
* For 32-bit surfaces, comparisons use packed source pixel values with RGB-masked compatibility fallback.
* `NOCOLORKEY` blits copy all pixels, including blue.

Presentation model:

* All surfaces (primary and offscreen) store CPU pixel buffers.
* `Blt` / `BltFast` write to CPU buffers only — no direct SDL rendering.
* `Flip` is the single presentation entry point:
  1. Uploads the primary surface buffer to a cached streaming `SDL_Texture`.
  2. Clears the renderer immediately before drawing.
  3. Renders the texture to the full window.
  4. Calls `SDL_RenderPresent` exactly once.
* This avoids flickering caused by clearing after drawing or presenting before upload.

---

## Repository

[https://github.com/openeggbert/free-direct](https://github.com/openeggbert/free-direct)

## License

This project is licensed under **MIT**. See [LICENSE](LICENSE).

