# Free Direct

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)

**Free Direct** is an experimental C++ project that aims to reimplement a **subset of DirectX 3 (2D)** on top of the **CNA** library.

The goal is not full DirectX compatibility, but a **focused, minimal implementation** sufficient to run specific legacy games (e.g. *Speedy Blupi*) while remaining portable and modern.

---

## Overview

Free Direct acts as a **compatibility layer**:

```
DirectX 3 (subset)
        ↓
   Free Direct
        ↓
       CNA
        ↓
     SDL 3
```

* **Free Direct** → reimplements selected DirectX 3 APIs (2D only)
* **CNA** → XNA-like abstraction layer written in C++
* **SDL 3** → low-level cross-platform backend

---

## Goals

* Recreate a **minimal subset of DirectX 3 (2D)** in modern C++
* Enable running legacy DirectX-based games without original dependencies
* Keep the implementation **simple, readable, and hackable**
* Build everything on top of your existing **CNA architecture**
* Maintain **cross-platform support** (Linux, Windows, macOS, Android, Web)

---

## Non-Goals

* ❌ Full DirectX 3 compatibility
* ❌ Hardware-accurate emulation
* ❌ Direct3D (3D pipeline) support (for now)

---

## Features (Planned / In Progress)

* Surface / bitmap rendering
* Basic blitting operations
* Transparency handling (color key)
* Simple sprite rendering
* Timing and game loop integration via CNA
* Input abstraction via CNA

---

## Technologies

* **C++17**
* **CNA** (custom XNA-like framework in C++)
* **SDL 3** (platform abstraction layer)

---

## Example Use Case

Free Direct is primarily designed to support:

* Porting old DirectX 3 games to modern platforms
* Running reverse-engineered games
* Studying legacy graphics APIs in a simplified environment

---

## Project Structure (Conceptual)

```
freedirect/
 ├── include/
 │    ├── FreeDirect/
 │    │    ├── Surface.hpp
 │    │    ├── Device.hpp
 │    │    └── ...
 ├── src/
 ├── examples/
 └── screenshots/
```

---

## Build Instructions

```bash
git clone https://github.com/openeggbert/free-direct.git
cd free-direct

mkdir build
cd build
cmake ..
make
```

Run example:

```bash
./FreeDirectDemo
```

---

## Screenshots

![Screenshot 1](screenshots/screenshot1.png)
Basic rendering using Free Direct over CNA.

![Screenshot 2](screenshots/screenshot2.png)
Example of sprite drawing and background handling.

---

## Project Status

**Work in progress**

Current focus:

* Designing API compatible subset of DirectX 3 (2D)
* Integrating with CNA rendering pipeline
* Making first legacy game run successfully

---

## Long-Term Vision

* Stable subset sufficient for at least one full game
* Expandable architecture for additional DirectX features
* Optional future support for:

  * Direct3D (limited)
  * More advanced rendering paths (OpenGL / Vulkan via CNA backend)

---

## Repository

[https://github.com/openeggbert/free-direct](https://github.com/openeggbert/free-direct)

## License

This project is licensed under **MIT**. See [LICENSE](LICENSE).

