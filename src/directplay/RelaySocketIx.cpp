/**
 * @file RelaySocketIx.cpp
 * @brief Native `IRelaySocket` over IXWebSocket. Compiled only in native relay builds.
 *
 * TLS (wss://) verifies the server certificate against the system's CA store. For a test server
 * with a self-signed certificate, FREE_DIRECT_RELAY_TLS_INSECURE=1 turns verification off; never
 * use that against a server on the Internet.
 * @note Status: IMPLEMENTED
 */
#include "RelaySocket.hpp"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocket.h>

#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

namespace free_direct_directplay {
namespace {

void EnsureNetSystem() {
    static const bool initialized = ix::initNetSystem();
    (void)initialized;
}

class IxRelaySocket final : public IRelaySocket {
public:
    ~IxRelaySocket() override { Close(); }

    bool Open(const std::string& url) override {
        EnsureNetSystem();
        ws_.setUrl(url);
        ws_.disableAutomaticReconnection();
        ws_.disablePerMessageDeflate();
        ws_.setHandshakeTimeout(10);
        ws_.setPingInterval(20);
        const char* insecure = std::getenv("FREE_DIRECT_RELAY_TLS_INSECURE");
        if (insecure && std::strcmp(insecure, "1") == 0) {
            ix::SocketTLSOptions tls;
            tls.caFile = "NONE";
            ws_.setTLSOptions(tls);
        }
        ws_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& msg) {
            Event e;
            switch (msg->type) {
                case ix::WebSocketMessageType::Open:
                    e.kind = EventKind::Opened;
                    break;
                case ix::WebSocketMessageType::Message:
                    if (!msg->binary) return;
                    e.kind = EventKind::Message;
                    e.bytes.assign(msg->str.begin(), msg->str.end());
                    break;
                case ix::WebSocketMessageType::Close:
                    e.kind = EventKind::Closed;
                    e.reason = msg->closeInfo.reason.empty() ? "connection closed" : msg->closeInfo.reason;
                    break;
                case ix::WebSocketMessageType::Error:
                    e.kind = EventKind::Closed;
                    e.reason = msg->errorInfo.reason.empty() ? "connection failed" : msg->errorInfo.reason;
                    break;
                default:
                    return;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            inbox_.push_back(std::move(e));
        });
        ws_.start();
        started_ = true;
        return true;
    }

    bool Send(const std::vector<std::uint8_t>& bytes) override {
        if (ws_.getReadyState() != ix::ReadyState::Open) return false;
        return ws_.sendBinary(std::string(bytes.begin(), bytes.end())).success;
    }

    void Close() override {
        if (!started_) return;
        started_ = false;
        // stop() joins IXWebSocket's thread, so no callback runs after it returns.
        ws_.stop();
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

    std::size_t BufferedAmount() const override { return ws_.bufferedAmount(); }

private:
    ix::WebSocket ws_;
    bool started_ = false;
    std::mutex mutex_;
    std::deque<Event> inbox_;
};

} // namespace

std::unique_ptr<IRelaySocket> CreateRelaySocket() { return std::make_unique<IxRelaySocket>(); }

} // namespace free_direct_directplay
