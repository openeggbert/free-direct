/**
 * @file RelaySocket.hpp
 * @brief A client WebSocket for the Free Direct server, behind one small interface.
 *
 * Private to the DirectPlay implementation. Two implementations exist: `RelaySocketIx.cpp`
 * (native builds, IXWebSocket; its callbacks run on IXWebSocket's own thread) and
 * `RelaySocketWeb.cpp` (WebAssembly builds, the browser's WebSocket through
 * `emscripten/websocket.h`; its callbacks run on the main thread whenever the browser event loop
 * gets a turn). Both only queue events; `RelayClient` consumes them on the game's thread with
 * `Poll()`, so nothing in the DirectPlay layer runs on a network thread.
 * @note Status: IMPLEMENTED
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace free_direct_directplay {

class IRelaySocket {
public:
    enum class EventKind { Opened, Message, Closed };

    struct Event {
        EventKind kind = EventKind::Closed;
        std::vector<std::uint8_t> bytes;  ///< Message
        std::string reason;               ///< Closed: why (connection refused, TLS error, close reason)
    };

    virtual ~IRelaySocket() = default;

    /// Starts connecting to `url` (ws:// or wss://). False if that could not even be started; a
    /// later failure arrives as a Closed event.
    virtual bool Open(const std::string& url) = 0;
    /// Queues one binary message. False if the socket is not open.
    virtual bool Send(const std::vector<std::uint8_t>& bytes) = 0;
    /// Closes the connection and stops all callbacks. No events arrive afterwards.
    virtual void Close() = 0;
    /// Pops the oldest queued event; false if there is none.
    virtual bool Poll(Event& out) = 0;
    /// Bytes queued for sending and not yet written to the network.
    virtual std::size_t BufferedAmount() const = 0;
};

std::unique_ptr<IRelaySocket> CreateRelaySocket();

} // namespace free_direct_directplay
