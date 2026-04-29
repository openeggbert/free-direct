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
* **DirectSound** → Partially implemented: SDL3-backed audio playback for static PCM buffers.
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
* **DirectSound**: Partially implemented over SDL3 audio (see DirectSound section below).
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

Debug logging and performance options:

* DirectSound debug logs can be enabled with `FREE_DIRECT_DEBUG_DSOUND=1`.
* DirectDraw debug logs can be enabled with `FREE_DIRECT_DEBUG_DDRAW=1`.
* Presentation-path debug logs can be enabled with `FREE_DIRECT_DEBUG_PRESENTATION=1`.
* Color-key diagnostics can be enabled with `FREE_DIRECT_DEBUG_COLORKEY=1`.
* Optional one-time primary clear diagnostic can be enabled with `FREE_DIRECT_DEBUG_PRIMARY_CLEAR=1`.
* Performance counters (presents/s, uploads/s, blts/s) can be enabled with `FREE_DIRECT_DEBUG_PERF=1`.

Color-key behavior (subset):

* `SetColorKey(DDCKEY_SRCBLT, ...)` stores low/high values per source surface.
* `BltFast` uses source color key when `DDBLTFAST_SRCCOLORKEY` is set.
* `Blt` uses source color key when `DDBLT_KEYSRC` is set.
* For 8-bit surfaces, comparisons are made against palette index values.
* For 32-bit surfaces, comparisons use packed source pixel values with RGB-masked compatibility fallback.
* `NOCOLORKEY` blits copy all pixels, including blue.

Presentation model and frame pacing:

* All surfaces (primary and offscreen) store CPU pixel buffers.
* `Blt` / `BltFast` write to CPU buffers and mark the primary surface dirty.
* `Flip` or a Blt-to-primary call triggers `PresentPrimary`:
  1. **Throttle check**: skips upload+present if called within the frame interval (default 60 FPS / ~16.7 ms). Override with `FREE_DIRECT_TARGET_FPS=<n>`.
  2. **Dirty check**: skips upload+present if the primary surface has not changed since the last present.
  3. Uploads the primary surface buffer to a cached streaming `SDL_Texture`.
  4. Clears the renderer immediately before drawing.
  5. Renders the texture to the full window.
  6. Calls `SDL_RenderPresent` exactly once.
* VSync is enabled by default via `SDL_SetRenderVSync`. Disable with `FREE_DIRECT_ENABLE_VSYNC=0`.
* `PeekMessageA` yields CPU with `SDL_Delay(1)` when the message queue is empty, preventing busy-spin in the game's main loop.
* These changes collectively eliminate the busy-loop CPU overhead that caused ~6% CPU usage.

---

## DirectSound (SDL3 backend)

DirectSound support targets the narrow subset declared in `include/dsound.h`.

### SDL3 audio design

- One shared `SDL_AudioDeviceID` is opened on `DirectSoundCreate` and shared by all sound buffers.
- Each `IDirectSoundBuffer` owns one `SDL_AudioStream` created on the first `Play()` call.
- SDL3 converts the source PCM format (from `PCMWAVEFORMAT`) to the device format automatically.
- `Play()` clears the stream, puts the stored PCM bytes, and SDL3 handles drain/mixing.
- `Stop()` calls `SDL_ClearAudioStream`, which silences the buffer immediately.

### Supported PCM formats

| Bits | Channels | Sample rates |
|------|----------|--------------|
| 8-bit unsigned | mono / stereo | 11025, 22050, 44100 Hz |
| 16-bit signed LE | mono / stereo | 11025, 22050, 44100 Hz |

Any format SDL3 can convert from is also accepted; unsupported formats fall back to a safe 16-bit mono 22050 Hz default.

### Lock / Unlock behaviour

- `Lock()` returns a direct pointer into an internal `std::vector<uint8_t>` PCM buffer.
- `DSBLOCK_FROMWRITECURSOR` returns the full buffer from offset 0.
- Wrap-around two-region locks are supported.
- `Unlock()` is a no-op; the data is used as-is on the next `Play()`.

### Play / Stop / GetStatus behaviour

- `Play()`: clears the SDL stream, inserts PCM data from `playCursor_`, resets cursor to 0.
- `Stop()`: calls `SDL_ClearAudioStream` — the buffer is quiet until the next `Play()`.
- `GetStatus()`: sets `DSBSTATUS_PLAYING` if `SDL_GetAudioStreamQueued() > 0`.

### Volume behaviour

- DirectSound centibel range: `DSBVOLUME_MIN` (-10 000) → `DSBVOLUME_MAX` (0).
- Conversion: `gain = 10^(cB / 2000)` (approximate but perceptually reasonable).
- Applied via `SDL_SetAudioStreamGain()`.
- Values outside the valid range are clamped.

### Pan behaviour

- Centibel range: `DSBPAN_LEFT` (-10 000) → `DSBPAN_RIGHT` (+10 000).
- Constant-power pan gains are calculated for mono sources (stored internally).
- Accurate per-channel output via SDL3 stream channel maps is a TODO.
- For stereo sources the value is stored but not applied.
- `SetPan()` never crashes on any input.

### Limitations

- Looping (`DSBPLAY_LOOPING = 0x1`) is accepted but ignored (TODO).
- `SetCurrentPosition()` updates an internal cursor but the SDL stream is not seekable.
- Accurate stereo panning for mono sources requires a per-sample callback (TODO).
- Capture, 3D audio, and DirectPlay are not implemented.
- Multiple simultaneous plays of the same buffer: calling `Play()` again stops and restarts the buffer (one stream per buffer).

---

## Repository

[https://github.com/openeggbert/free-direct](https://github.com/openeggbert/free-direct)

## License

This project is licensed under **MIT**. See [LICENSE](LICENSE).

