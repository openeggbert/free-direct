# Free Direct / Free API – TODO & Review Notes

This document lists current issues, limitations, and next steps for the **Free API (WinAPI subset)** and **Free Direct (DirectDraw subset)** implementation.

---

## 🔴 Critical Fixes (Do First)

### 1. Calling Convention Macros (`__stdcall`, `__cdecl`)
**Problem:**
`windef.h` overrides `__stdcall` and `__cdecl` globally using `#undef` and `#define`.

**Why it's bad:**
- Breaks ABI compatibility on Windows
- Can conflict with system headers
- Unsafe for public headers

**Fix:**
Define them only if missing:

```cpp
#ifndef __stdcall
#define __stdcall
#endif

#ifndef __cdecl
#define __cdecl
#endif
````

---

### 2. Incorrect Window Close Flow (`WM_CLOSE`, `WM_DESTROY`)

**Problem:**
`DemoWindowProc` calls `PostQuitMessage` directly on `WM_CLOSE`.

**Correct behavior:**

```
WM_CLOSE → DestroyWindow → WM_DESTROY → PostQuitMessage
```

**Fix:**

```cpp
case WM_CLOSE:
    DestroyWindow(hWnd);
    return 0;

case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
```

---

### 3. `DestroyWindow` Does Not Emit `WM_DESTROY`

**Problem:**
Window is destroyed without notifying the application.

**Fix:**
Call the window procedure with `WM_DESTROY` before destroying SDL window.

---

### 4. `CloseHandle` Always Returns TRUE

**Problem:**
Stub returns success even when nothing is closed.

**Fix options:**

* Return `FALSE` for now
* Or implement real handle tracking

---

## 🟠 Important Improvements

### 5. `PeekMessage` Ignores Filters

**Current limitations:**

* Ignores `HWND`
* Ignores message range filters
* Uses global queue only

**Action:**
Document as **intentional subset behavior**.

---

### 6. `ShowWindow` and `UpdateWindow` Are No-Ops

**Problem:**

* `SW_SHOW` / `SW_HIDE` ignored
* No redraw triggered

**Action:**
Keep as stub, but document limitation.

---

### 7. `CreateWindowEx` Ignores Most Parameters

Ignored:

* styles (`dwStyle`, `dwExStyle`)
* position (`X`, `Y`)
* parent/menu
* `lpParam`

**Action:**
Acceptable for now, but document clearly.

---

## 🟡 DirectDraw Limitations

### 8. `Flip` Is Not Real DirectDraw Flip

**Current behavior:**
Acts as a manual present from another surface.

**Issue:**
Not compatible with real flipping chains.

**Action:**
Document as:

> "Simplified present mechanism, not real DirectDraw flipping"

---

### 9. Texture Created Every Frame

**Problem:**

```cpp
CreateTexture(...) // called every frame
```

**Impact:**

* Performance issue
* Unnecessary allocations

**Future fix:**

* Persistent texture
* Update pixels instead of recreating

---

### 10. `DDBLTFX.dwFillColor` Is Simplified

**Current interpretation:**

```
0x00RRGGBB
```

**Issue:**
Real DirectDraw depends on pixel format.

**Action:**
Document as internal simplified format.

---

## 🟢 Minor Issues / Notes

### 11. `OutputDebugStringW` Not Unicode-Safe

**Problem:**
Casts wide chars to `char`.

**Action:**
Acceptable for now (ASCII fallback).

---

### 12. `NULL` Defined as `0` in C++

**Note:**
Outdated style, but not critical.

---

### 13. Header Naming Conflicts

Files:

* `windows.h`
* `ddraw.h`

**Risk:**
Conflicts with real Windows SDK headers.

**Action:**
Ensure controlled include paths or rename internally.

---

## 📌 Current Scope (Explicitly Supported)

### WinAPI Subset

* Window creation
* Message loop (basic)
* Quit handling
* Minimal debugging output

### DirectDraw Subset

* `DirectDrawCreate`
* `SetCooperativeLevel`
* `CreateSurface` (Primary, Offscreen)
* `CreatePalette`
* `CreateClipper`
* `SetDisplayMode` (Stub)
* `Blt` / `BltFast`:
    * color fill
    * surface-to-surface copy
    * source color key transparency
* `Lock` / `Unlock`: direct pixel access
* `GetSurfaceDesc`: returns width, height, pitch, pixel format
* `SetPalette` / `SetClipper`: basic support
* 8-bit paletted surfaces: automatic conversion to RGBA32 on present
* simplified present via primary surface

### DirectSound Subset (Stubs)

* `DirectSoundCreate`
* `SetCooperativeLevel`
* `CreateSoundBuffer`: allocates dummy memory
* `Lock` / `Unlock`: provides access to dummy memory
* `Play` / `Stop` / `GetStatus`: consistent dummy behavior

### DirectPlay Subset (Stubs)

* `DirectPlayCreate`
* `DirectPlayEnumerateA` / `DirectPlayEnumerateW`
* `EnumSessions` / `Open` / `CreatePlayer`
* `Send` / `Receive`: dummy behavior

---

## 🚧 Not Yet Implemented

* COM correctness (`QueryInterface` etc.)
* Accurate Pixel formats (mostly fixed to RGBA32 internally)
* Real flipping chain
* Multi-window message routing
* Accurate message filtering
* GDI / HDC support
* Real audio output (DirectSound)
* Real networking (DirectPlay)

---

## 🎯 Short-Term Goal

Make a **minimal but stable subset** that:

* runs a simple 2D demo
* behaves predictably
* avoids undefined or misleading behavior

---

## 🎯 Long-Term Goal

Evolve toward a **faithful DirectDraw 2D subset**:

* compatible semantics
* correct message flow
* reusable for legacy game ports

---

## ✔️ Summary

The project is on a **good path**, but currently:

* partially compatible
* partially stub-based
* not yet safe to call "compatible subset"

Focus first on:

1. ABI safety
2. Correct window/message lifecycle
3. Removing misleading stubs

# Web and Android 

a port to **Web + Android is feasible**, but it’s not just a matter of “switching CMake.” The effort is **medium to fairly high**.

* The good news: the render backend already goes through `SDL3` (`free-direct/src/directdraw/DirectDraw.cpp`), so a path to WebGL/OpenGL ES exists.
* The main issue is not rendering, but the **Win32 simulation layer and runtime model** in `free-api` (`winapi.cpp`, `winmain_bridge.cpp`).

---

### What is already in good shape

* `free-direct` is internally SDL-based (`SDL_CreateRenderer`, textures, present) → that’s the right direction for Web/Android.
* The project is already separated via `free-api` + `free-direct` backend in `CMakeLists.txt`, so changes can be made in the compatibility layer without refactoring game logic.

---

### Biggest blockers for Web/Android (without changing the game)

* **Entrypoint and main loop:**
  `free-api/src/winmain_bridge.cpp` + `GetMessageA` / `WaitMessage` in `winapi.cpp` use a desktop/blocking model (`SDL_Delay(1)` loop).
  Web/Android require a non-blocking, callback-driven model (`SDL_main` / SDL app lifecycle).

* **Input model:**
  Win32 message emulation is primarily mouse/keyboard; Android needs touch → `WM_*` translation (`WM_LBUTTONDOWN`, `WM_MOUSEMOVE`, etc.).

* **Assets / filesystem:**
  Loading is based on local files/relative paths (`LoadImageA`, file fallback).
  Web needs preloading into a virtual FS; Android needs asset packaging.

* **Window semantics:**
  Win32 styles and desktop coordinates (`CreateWindowExA`, `ShowWindow`, focus/raise) only partially map to fullscreen/rotation/resizing on mobile.

* **Timers:**
  Parts of the timer API are still simplified/stub-like (`SetTimer`, `KillTimer`, WinMM timers) — this can affect input and animation timing on mobile/web.

---

### Effort estimate (realistic)

* **Web (Emscripten):** ~2–4 weeks to reach stable “menu + basic gameplay loop.”
* **Android:** ~3–6 weeks (extra lifecycle handling, pause/resume, touch ergonomics, packaging).
* For both platforms done properly and shared: about **4–8 weeks** iteratively (depending on runtime edge cases).

---

### Recommended order (best ROI)

1. Convert runtime to a non-blocking SDL lifecycle in `free-api` (no game logic changes).
2. Implement a platform abstraction for assets/path resolving (desktop/android/web).
3. Add input translation (touch + keyboard/mouse fallback).
4. Refine timers/message queue compatibility and lifecycle (pause/resume, visibility).

---

### Conclusion

The hardest part is **emulating the WinAPI application runtime** (loop/input/assets), not the DirectDraw rendering itself.

So: **yes, it’s doable**, but it’s more of a *runtime compatibility layer port* than a simple build task.
