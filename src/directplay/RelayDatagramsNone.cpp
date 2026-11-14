/**
 * @file RelayDatagramsNone.cpp
 * @brief No datagram channel (a browser has no UDP): everything stays on the WebSocket. See
 * RelayDatagrams.hpp.
 * @note Status: IMPLEMENTED
 */
#include "RelayDatagrams.hpp"

namespace free_direct_directplay {

std::unique_ptr<IRelayDatagrams> CreateRelayDatagrams() { return nullptr; }

} // namespace free_direct_directplay
