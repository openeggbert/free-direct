/**
 * @file RelayServer.cpp
 * @brief IXWebSocket front end of `free-direct-server`. See RelayServer.hpp.
 * @note Status: IMPLEMENTED
 */
#include "RelayServer.hpp"

#include <ixwebsocket/IXConnectionState.h>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(__linux__)
#include <sys/random.h>
#endif

namespace free_direct_relay {

void FillRandom(std::uint8_t* out, std::size_t count) {
#if defined(__linux__)
    std::size_t done = 0;
    while (done < count) {
        const ssize_t n = getrandom(out + done, count - done, 0);
        if (n <= 0) break;
        done += static_cast<std::size_t>(n);
    }
    if (done == count) return;
#endif
    static std::random_device device;
    for (std::size_t i = 0; i < count; ++i) out[i] = static_cast<std::uint8_t>(device());
}

namespace {

/// A client whose unsent data exceeds this is too slow to keep up and is disconnected.
constexpr std::size_t kMaxBufferedBytes = 1 << 20;

/// Our numeric connection id, attached to IXWebSocket's per-connection state.
class RelayConnectionState final : public ix::ConnectionState {
public:
    ConnId relayId = 0;
};

} // namespace

struct RelayServer::Impl {
    ServerOptions options;
    RelayHub::Logger log;
    std::mutex hubMutex;
    RelayHub hub;
    // Live sockets by connection id. Sends happen under socketsMutex, and a socket is removed
    // under it when IXWebSocket reports its close, before the socket object is destroyed.
    std::mutex socketsMutex;
    std::unordered_map<ConnId, ix::WebSocket*> sockets;
    std::atomic<ConnId> nextConnId{1};
    std::unique_ptr<ix::WebSocketServer> server;
    bool started = false;

    Impl(ServerOptions o, RelayHub::Logger l)
        : options(std::move(o)), log(l), hub(options.hub, FillRandom, l) {}

    void Execute(const std::vector<HubAction>& actions) {
        std::lock_guard<std::mutex> lock(socketsMutex);
        for (const HubAction& a : actions) {
            auto it = sockets.find(a.conn);
            if (it == sockets.end()) continue;
            ix::WebSocket* ws = it->second;
            if (a.kind == HubAction::Kind::Send) {
                if (ws->bufferedAmount() > kMaxBufferedBytes) {
                    ws->close(1008, "client too slow");
                    continue;
                }
                ws->sendBinary(std::string(a.bytes.begin(), a.bytes.end()));
            } else {
                ws->close(a.closeCode, a.reason);
            }
        }
    }

    void OnMessage(const std::shared_ptr<ix::ConnectionState>& state, ix::WebSocket& ws,
                   const ix::WebSocketMessagePtr& msg) {
        auto* relayState = static_cast<RelayConnectionState*>(state.get());
        const auto now = RelayHub::Clock::now();
        std::vector<HubAction> actions;
        switch (msg->type) {
            case ix::WebSocketMessageType::Open: {
                relayState->relayId = nextConnId++;
                {
                    std::lock_guard<std::mutex> lock(socketsMutex);
                    sockets[relayState->relayId] = &ws;
                }
                std::string origin;
                auto originIt = msg->openInfo.headers.find("Origin");
                if (originIt != msg->openInfo.headers.end()) origin = originIt->second;
                std::lock_guard<std::mutex> lock(hubMutex);
                actions = hub.OnOpen(relayState->relayId, state->getRemoteIp(), origin, now);
                break;
            }
            case ix::WebSocketMessageType::Message: {
                if (!msg->binary) {
                    HubAction close;
                    close.kind = HubAction::Kind::Close;
                    close.conn = relayState->relayId;
                    close.closeCode = 1003;
                    close.reason = "binary messages only";
                    actions.push_back(std::move(close));
                    break;
                }
                std::lock_guard<std::mutex> lock(hubMutex);
                actions = hub.OnMessage(relayState->relayId,
                                        reinterpret_cast<const std::uint8_t*>(msg->str.data()),
                                        msg->str.size(), now);
                break;
            }
            case ix::WebSocketMessageType::Close: {
                {
                    std::lock_guard<std::mutex> lock(socketsMutex);
                    sockets.erase(relayState->relayId);
                }
                std::lock_guard<std::mutex> lock(hubMutex);
                actions = hub.OnClose(relayState->relayId, now);
                break;
            }
            case ix::WebSocketMessageType::Error:
                if (log) log("connection " + std::to_string(relayState->relayId) + " error: " + msg->errorInfo.reason);
                break;
            default:
                break;
        }
        Execute(actions);
    }
};

RelayServer::RelayServer(ServerOptions options, RelayHub::Logger log)
    : impl_(std::make_unique<Impl>(std::move(options), std::move(log))) {}

RelayServer::~RelayServer() { Stop(); }

bool RelayServer::Start(std::string& error) {
    static const bool netInitialized = ix::initNetSystem();
    (void)netInitialized;
    ServerOptions& o = impl_->options;
    if (o.port == 0) o.port = ix::getFreePort();
    impl_->server = std::make_unique<ix::WebSocketServer>(
        o.port, o.host, ix::SocketServer::kDefaultTcpBacklog, o.hub.maxConnections + 16,
        /*handshakeTimeoutSecs=*/10, ix::SocketServer::kDefaultAddressFamily,
        o.pingSeconds > 0 ? o.pingSeconds : -1);
    ix::WebSocketServer& server = *impl_->server;
    server.disablePerMessageDeflate();
    server.setConnectionStateFactory([] { return std::make_shared<RelayConnectionState>(); });
    if (!o.tlsCert.empty()) {
        ix::SocketTLSOptions tls;
        tls.tls = true;
        tls.certFile = o.tlsCert;
        tls.keyFile = o.tlsKey;
        tls.caFile = "NONE";
        server.setTLSOptions(tls);
    }
    Impl* impl = impl_.get();
    server.setOnClientMessageCallback(
        [impl](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket& ws, const ix::WebSocketMessagePtr& msg) {
            impl->OnMessage(state, ws, msg);
        });
    const auto result = server.listen();
    if (!result.first) {
        error = "cannot listen on " + o.host + ":" + std::to_string(o.port) + ": " + result.second;
        impl_->server.reset();
        return false;
    }
    server.start();
    impl_->started = true;
    return true;
}

int RelayServer::Port() const { return impl_->options.port; }

void RelayServer::Reap() {
    std::lock_guard<std::mutex> lock(impl_->hubMutex);
    impl_->hub.Reap(RelayHub::Clock::now());
}

void RelayServer::Stop() {
    if (!impl_ || !impl_->started) return;
    impl_->started = false;
    std::vector<HubAction> actions;
    {
        std::lock_guard<std::mutex> lock(impl_->hubMutex);
        actions = impl_->hub.Shutdown();
    }
    impl_->Execute(actions);
    // Give the close frames a moment to leave before the sockets go.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    impl_->server->stop();
    impl_->server.reset();
}

} // namespace free_direct_relay
