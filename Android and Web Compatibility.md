# Android and Web Compatibility

`free-direct` is well-positioned to run on the **Web (via Emscripten)** and **Android** because it is built directly on **SDL3**, which is designed for cross-platform portability. However, several specific architectural adjustments are required to move from the current desktop-centric Win32 simulation to a fully cross-platform one.

#### 1. Main Loop Refactoring (Critical)
The current demo and `free-api` use a traditional Win32-style blocking loop:
```cpp
while (running) {
    while (PeekMessage(&msg, ...)) { ... }
    // game logic and rendering
    Sleep(16);
}
```
*   **Web/Android Problem**: Browsers and mobile operating systems do not allow blocking the main thread. Blocking on the Web will freeze the browser tab, and on Android, it will trigger an "Application Not Responding" (ANR) error.
*   **Solution**: Refactor the entry point to use SDL3's new main callback system (`SDL_AppInit`, `SDL_AppIterate`, `SDL_AppQuit`). `free-api` would then wrap these callbacks and simulate the message loop state without blocking.

#### 2. Entry Point (SDL_main)
*   **Context**: On Android and Web, SDL3 expects to control the entry point.
*   **Solution**: `free-api` must be updated to use `SDL_main` or the `SDL_App` system, which "hijacks" the standard `main` to handle platform-specific initialization.

#### 3. Assets and File System
*   **Context**: Current code loads `player.png` from relative paths on disk.
*   **Solution**:
    *   **Android**: Files must be loaded from the `assets/` folder using `SDL_OpenIO` or by placing them in the correct APK structure.
    *   **Web**: Files must be pre-packaged into the Emscripten virtual file system (using `--preload-file`).

#### 4. Display and Windowing
*   **Context**: `CreateWindowExA` currently uses fixed sizes (e.g., 800x600).
*   **Solution**: Mobile and Web windows are typically fullscreen and may resize or rotate. `free-api` should be updated to ignore fixed coordinates and create a fullscreen-friendly window on these platforms.

#### 5. Input Mapping
*   **Context**: DirectX 3 games expect mouse and keyboard.
*   **Solution**: `free-api` needs a translation layer that maps touch events (finger down/move/up) to Windows messages like `WM_LBUTTONDOWN` and `WM_MOUSEMOVE`.

#### Summary of Necessary Changes
| Component | Status | Required Change |
| :--- | :--- | :--- |
| **Rendering Backend** | ✅ Supported | `SDL_Renderer` is already compatible with GLES and WebGL. |
| **Main Loop** | ❌ Blocking | Must be refactored to use `SDL_AppIterate` (non-blocking). |
| **Entry Point** | ⚠️ Partial | Need to integrate `SDL_main` for mobile/web. |
| **Assets** | ⚠️ Partial | Need a cross-platform path resolver for resources. |
| **Input** | ❌ Missing | Need to map touch/accelerometer to WinAPI events. |

In conclusion, while the core rendering logic is already portable, the **"Win32 simulation layer"** in `free-api` must be made asynchronous to satisfy the requirements of Web and Mobile environments.