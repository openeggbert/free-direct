/**
 * @file DirectPlayWait.hpp
 * @brief Short sleeps for DirectPlay's bounded waits (joining, relay requests).
 *
 * Private to the DirectPlay implementation. In a WebAssembly build the wait must give the
 * browser's event loop a turn, because that is where WebSocket messages arrive; the web
 * builds that use the relay transport link with Asyncify, which `emscripten_sleep` requires.
 * @note Status: IMPLEMENTED
 */
#pragma once

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#include <chrono>
#include <thread>
#endif

namespace free_direct_directplay {

inline void DirectPlaySleepMs(unsigned milliseconds) {
#ifdef __EMSCRIPTEN__
    emscripten_sleep(milliseconds);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

} // namespace free_direct_directplay
