/**
 * @file RelaySocketWeb.cpp
 * @brief WebAssembly `IRelaySocket` over the browser's WebSocket (`emscripten/websocket.h`).
 *
 * Compiled only in Emscripten relay builds; the consumer links with `-lwebsocket.js`. The socket
 * is created by the main thread and its callbacks run there, between frames or while a
 * DirectPlay wait yields with `emscripten_sleep` (Asyncify). A page served over https:// can
 * only open wss:// connections: browsers block ws:// from a secure page except to localhost.
 * @note Status: IMPLEMENTED
 */
#include "RelaySocket.hpp"

#include <emscripten/websocket.h>

#include <deque>
#include <mutex>

namespace free_direct_directplay {
namespace {

class WebRelaySocket final : public IRelaySocket {
public:
    ~WebRelaySocket() override { Close(); }

    bool Open(const std::string& url) override {
        if (!emscripten_websocket_is_supported()) return false;
        EmscriptenWebSocketCreateAttributes attr;
        emscripten_websocket_init_create_attributes(&attr);
        attr.url = url.c_str();
        attr.protocols = nullptr;
        attr.createOnMainThread = EM_TRUE;
        const EMSCRIPTEN_WEBSOCKET_T ws = emscripten_websocket_new(&attr);
        if (ws <= 0) return false;
        ws_ = ws;
        emscripten_websocket_set_onopen_callback(ws_, this, &WebRelaySocket::OnOpen);
        emscripten_websocket_set_onmessage_callback(ws_, this, &WebRelaySocket::OnMessage);
        emscripten_websocket_set_onclose_callback(ws_, this, &WebRelaySocket::OnClose);
        emscripten_websocket_set_onerror_callback(ws_, this, &WebRelaySocket::OnError);
        return true;
    }

    bool Send(const std::vector<std::uint8_t>& bytes) override {
        if (ws_ <= 0 || !open_) return false;
        return emscripten_websocket_send_binary(ws_, const_cast<std::uint8_t*>(bytes.data()),
                                                static_cast<std::uint32_t>(bytes.size())) ==
               EMSCRIPTEN_RESULT_SUCCESS;
    }

    void Close() override {
        if (ws_ <= 0) return;
        emscripten_websocket_close(ws_, 1000, "closing");
        // Deleting the handle also unregisters its callbacks.
        emscripten_websocket_delete(ws_);
        ws_ = 0;
        open_ = false;
        std::lock_guard<std::mutex> lock(mutex_);
        inbox_.clear();
    }

    bool Poll(Event& out) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (inbox_.empty()) return false;
        out = std::move(inbox_.front());
        inbox_.pop_front();
        return true;
    }

    std::size_t BufferedAmount() const override {
        if (ws_ <= 0) return 0;
        size_t amount = 0;
        emscripten_websocket_get_buffered_amount(ws_, &amount);
        return amount;
    }

private:
    void Push(Event e) {
        std::lock_guard<std::mutex> lock(mutex_);
        inbox_.push_back(std::move(e));
    }

    static EM_BOOL OnOpen(int, const EmscriptenWebSocketOpenEvent*, void* user) {
        auto* self = static_cast<WebRelaySocket*>(user);
        self->open_ = true;
        Event e;
        e.kind = EventKind::Opened;
        self->Push(std::move(e));
        return EM_TRUE;
    }

    static EM_BOOL OnMessage(int, const EmscriptenWebSocketMessageEvent* event, void* user) {
        if (event->isText) return EM_TRUE;
        Event e;
        e.kind = EventKind::Message;
        e.bytes.assign(event->data, event->data + event->numBytes);
        static_cast<WebRelaySocket*>(user)->Push(std::move(e));
        return EM_TRUE;
    }

    static EM_BOOL OnClose(int, const EmscriptenWebSocketCloseEvent* event, void* user) {
        auto* self = static_cast<WebRelaySocket*>(user);
        self->open_ = false;
        Event e;
        e.kind = EventKind::Closed;
        e.reason = event->reason[0] ? event->reason : "connection closed";
        self->Push(std::move(e));
        return EM_TRUE;
    }

    static EM_BOOL OnError(int, const EmscriptenWebSocketErrorEvent*, void* user) {
        // The browser gives no detail (on purpose); a close event follows.
        (void)user;
        return EM_TRUE;
    }

    EMSCRIPTEN_WEBSOCKET_T ws_ = 0;
    bool open_ = false;
    std::mutex mutex_;
    std::deque<Event> inbox_;
};

} // namespace

std::unique_ptr<IRelaySocket> CreateRelaySocket() { return std::make_unique<WebRelaySocket>(); }

} // namespace free_direct_directplay
