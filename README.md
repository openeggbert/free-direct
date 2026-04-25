# Free Direct

![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)

**Free Direct** is an experimental C++ project that aims to reimplement a **subset of DirectX 3 (2D)** using **SDL3** as a backend.

The goal is not full DirectX compatibility, but a **focused, minimal implementation** sufficient to run specific legacy games (e.g. *Speedy Blupi*) while remaining portable and modern.

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
* **SDL 3** → internal implementation detail used as a backend

---

## Goals

* Recreate a **minimal subset of DirectX 3 (2D)** in modern C++
* Enable running legacy DirectX-based games without original dependencies
* Keep the implementation **simple, readable, and hackable**
* Use **SDL3** internally for cross-platform rendering
* Maintain **cross-platform support** (Linux, Windows, macOS, Android, Web)

---

## Non-Goals

* ❌ Full DirectX 3 compatibility
* ❌ Hardware-accurate emulation
* ❌ Direct3D (3D pipeline) support (for now)

---

## Features

* Surface / bitmap rendering
* Basic blitting operations
* Transparency handling (color key)
* Simple sprite rendering
* Internal SDL3 renderer mapping

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

* Designing API compatible subset of DirectX 3 (2D)
* Implementing DirectDraw surfaces using SDL3 textures
* Supporting basic blitting and presentation

---

## Repository

[https://github.com/openeggbert/free-direct](https://github.com/openeggbert/free-direct)

## License

This project is licensed under **MIT**. See [LICENSE](LICENSE).

