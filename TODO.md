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
* `CreateSurface`
* `Blt`:

    * color fill
    * surface-to-surface copy
* simplified present via primary surface

---

## 🚧 Not Yet Implemented

* COM correctness
* Pixel formats
* Lock / Unlock
* Clipper support
* Palette support
* Real flipping chain
* Multi-window message routing
* Accurate message filtering
* GDI / HDC support

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

