/**
 * @file DirectPlayPlayer.hpp
 * @brief Internal DirectPlay player record (scaffolding only).
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * `DirectPlayPlayer` does not yet hold any real state - one player's DPID,
 * short/long name, and (if a real call site is ever found to need them)
 * data bytes are added starting in `plan.md` Phase 2/9. It is not yet used
 * by `DirectPlay.cpp`.
 * @note Status: STUB
 */
#pragma once

namespace free_direct_directplay {

class DirectPlayPlayer {
public:
    DirectPlayPlayer() = default;
    ~DirectPlayPlayer() = default;
};

} // namespace free_direct_directplay
