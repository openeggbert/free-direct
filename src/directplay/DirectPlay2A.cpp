/**
 * @file DirectPlay2A.cpp
 * @brief Translation unit for `DirectPlay2AImpl` (the real `IDirectPlay2A` implementation).
 *
 * `DirectPlay2AImpl` is defined entirely in `DirectPlayInternal.hpp` (header-only, all methods
 * inline in the class body) - this file exists to give it its own discoverable CMake source
 * entry, matching `src/directplay/`'s existing convention for its other header-only classes
 * (e.g. `DirectPlaySession.cpp`, `DirectPlayMessageQueue.cpp`).
 * @note Status: PARTIAL
 */
#include "DirectPlayInternal.hpp"
