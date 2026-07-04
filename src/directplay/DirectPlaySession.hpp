/**
 * @file DirectPlaySession.hpp
 * @brief Internal DirectPlay session state (scaffolding only).
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * `DirectPlaySession` does not yet hold any real state - session descriptor,
 * host/client role, and player-count tracking are added starting in
 * `plan.md` Phase 2. It is not yet used by `DirectPlay.cpp`.
 * @note Status: STUB
 */
#pragma once

namespace free_direct_directplay {

class DirectPlaySession {
public:
    DirectPlaySession() = default;
    ~DirectPlaySession() = default;
};

} // namespace free_direct_directplay
