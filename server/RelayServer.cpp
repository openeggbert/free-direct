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

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#if defined(FREE_DIRECT_SERVER_WEBRTC)
#include <rtc/rtc.hpp>
#endif

#include <atomic>
#include <chrono>
#include <cstring>
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

/// At most this many datagrams per second from one connection; more are dropped.
constexpr unsigned kMaxDatagramsPerSecond = 400;

#if defined(_WIN32)
using UdpSocket = SOCKET;
const UdpSocket kNoUdpSocket = INVALID_SOCKET;
void CloseUdp(UdpSocket s) { closesocket(s); }
using AddressLength = int;
#else
using UdpSocket = int;
constexpr UdpSocket kNoUdpSocket = -1;
void CloseUdp(UdpSocket s) { ::close(s); }
using AddressLength = socklen_t;
#endif

/// A connection's bound UDP address (Bind) or its open WebRTC data channel, and its datagram count
/// in the current second.
struct Endpoint {
    sockaddr_storage address{};
    AddressLength length = 0;
#if defined(FREE_DIRECT_SERVER_WEBRTC)
    std::shared_ptr<rtc::DataChannel> channel;  ///< set: datagrams go over the channel
#endif
    unsigned count = 0;
    std::chrono::steady_clock::time_point second{};
};

bool SameAddress(const sockaddr_storage& a, const sockaddr_storage& b) {
    if (a.ss_family != b.ss_family) return false;
    if (a.ss_family == AF_INET) {
        const auto& x = reinterpret_cast<const sockaddr_in&>(a);
        const auto& y = reinterpret_cast<const sockaddr_in&>(b);
        return x.sin_port == y.sin_port && x.sin_addr.s_addr == y.sin_addr.s_addr;
    }
    if (a.ss_family == AF_INET6) {
        const auto& x = reinterpret_cast<const sockaddr_in6&>(a);
        const auto& y = reinterpret_cast<const sockaddr_in6&>(b);
        return x.sin6_port == y.sin6_port && std::memcmp(&x.sin6_addr, &y.sin6_addr, sizeof(x.sin6_addr)) == 0;
    }
    return false;
}

/// The datagram socket: for any address ("0.0.0.0", "::" or empty) an IPv6 socket that also takes
/// IPv4, or IPv4 alone where that fails; otherwise the given address.
UdpSocket OpenUdp(const std::string& host, int port, std::string& error) {
    const bool any = host.empty() || host == "0.0.0.0" || host == "::";
    if (any) {
        UdpSocket s = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
        if (s != kNoUdpSocket) {
            int off = 0;
            setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&off), sizeof(off));
            sockaddr_in6 a{};
            a.sin6_family = AF_INET6;
            a.sin6_addr = in6addr_any;
            a.sin6_port = htons(static_cast<std::uint16_t>(port));
            if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0) return s;
            CloseUdp(s);
        }
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
    addrinfo* found = nullptr;
    const std::string service = std::to_string(port);
    if (getaddrinfo(any ? "0.0.0.0" : host.c_str(), service.c_str(), &hints, &found) != 0 || !found) {
        error = "cannot resolve " + host;
        return kNoUdpSocket;
    }
    UdpSocket s = socket(found->ai_family, found->ai_socktype, found->ai_protocol);
    if (s != kNoUdpSocket && bind(s, found->ai_addr, static_cast<AddressLength>(found->ai_addrlen)) != 0) {
        CloseUdp(s);
        s = kNoUdpSocket;
    }
    freeaddrinfo(found);
    if (s == kNoUdpSocket) error = "cannot bind UDP port " + service;
    return s;
}

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
    // Datagrams: the socket, its reader thread and each connection's bound address.
    UdpSocket udp = kNoUdpSocket;
    std::thread udpThread;
    std::atomic<bool> udpRunning{false};
    std::mutex endpointsMutex;  // never held while taking socketsMutex or hubMutex
    std::unordered_map<ConnId, Endpoint> endpoints;
#if defined(FREE_DIRECT_SERVER_WEBRTC)
    // WebRTC: one peer connection per browser that offered, and its data channel until it opens.
    bool webrtc = false;
    rtc::Configuration rtcConfig;
    std::mutex peersMutex;  // held only around the maps
    std::unordered_map<ConnId, std::shared_ptr<rtc::PeerConnection>> peers;
    std::unordered_map<ConnId, std::shared_ptr<rtc::DataChannel>> channels;
#endif

    Impl(ServerOptions o, RelayHub::Logger l)
        : options(std::move(o)), log(l), hub(options.hub, FillRandom, l) {}

    void Execute(const std::vector<HubAction>& actions) {
        std::vector<const HubAction*> offers;
        {
            std::lock_guard<std::mutex> lock(socketsMutex);
            for (const HubAction& a : actions) {
                if (a.kind == HubAction::Kind::RtcOffer) offers.push_back(&a);
                else Perform(a);
            }
        }
        // Outside socketsMutex: the WebRTC library may call back (and so Execute) at once.
        for (const HubAction* a : offers) AnswerOffer(a->conn, std::string(a->bytes.begin(), a->bytes.end()));
    }

    /// One Send or Close; socketsMutex is held.
    void Perform(const HubAction& a) {
        {
            auto it = sockets.find(a.conn);
            if (it == sockets.end()) return;
            ix::WebSocket* ws = it->second;
            if (a.kind == HubAction::Kind::Send && a.datagram && SendDatagram(a)) return;
            if (a.kind == HubAction::Kind::Send) {
                if (ws->bufferedAmount() > kMaxBufferedBytes) {
                    ws->close(1008, "client too slow");
                    return;
                }
                ws->sendBinary(std::string(a.bytes.begin(), a.bytes.end()));
            } else {
                ws->close(a.closeCode, a.reason);
            }
        }
    }

    /// A browser's WebRTC offer: a peer connection whose answer (complete, with its candidates)
    /// goes back as RtcAnswer; the browser's data channel, once open, is the connection's datagram
    /// endpoint, as a bound UDP address is.
    void AnswerOffer(ConnId conn, const std::string& sdp) {
#if defined(FREE_DIRECT_SERVER_WEBRTC)
        if (!webrtc) return;
        try {
            auto pc = std::make_shared<rtc::PeerConnection>(rtcConfig);
            std::weak_ptr<rtc::PeerConnection> weakPc = pc;
            pc->onGatheringStateChange([this, conn, weakPc](rtc::PeerConnection::GatheringState state) {
                if (state != rtc::PeerConnection::GatheringState::Complete) return;
                auto p = weakPc.lock();
                if (!p) return;
                const auto description = p->localDescription();
                if (!description) return;
                const std::string answer = std::string(*description);
                HubAction a;
                a.conn = conn;
                a.bytes = Writer(MsgType::RtcAnswer).Bytes(answer.data(), answer.size()).Take();
                Execute({a});
            });
            pc->onDataChannel([this, conn](std::shared_ptr<rtc::DataChannel> channel) {
                std::weak_ptr<rtc::DataChannel> weakChannel = channel;
                channel->onOpen([this, conn, weakChannel] {
                    auto c = weakChannel.lock();
                    if (!c) return;
                    {
                        std::lock_guard<std::mutex> lock(endpointsMutex);
                        Endpoint& e = endpoints[conn];
                        e = Endpoint{};
                        e.channel = c;
                    }
                    const std::vector<std::uint8_t> ack = ServerDatagram(DatagramType::BindAck);
                    c->send(reinterpret_cast<const std::byte*>(ack.data()), ack.size());
                });
                channel->onMessage([this, conn](rtc::binary data) {
                    OnChannelDatagram(conn, reinterpret_cast<const std::uint8_t*>(data.data()), data.size());
                }, [](rtc::string) {});
                std::lock_guard<std::mutex> lock(peersMutex);
                channels[conn] = std::move(channel);
            });
            {
                std::lock_guard<std::mutex> lock(peersMutex);
                peers[conn] = pc;
            }
            pc->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
        } catch (const std::exception& e) {
            if (log) log("connection " + std::to_string(conn) + ": WebRTC offer refused: " + e.what());
        }
#else
        (void)conn;
        (void)sdp;
#endif
    }

    /// A datagram over a connection's data channel: as over UDP, but the channel is the endpoint.
    void OnChannelDatagram(ConnId conn, const std::uint8_t* data, std::size_t size) {
        ClientDatagramView v;
        if (!ParseClientDatagram(data, size, v) || v.type != DatagramType::Data) return;
        {
            std::lock_guard<std::mutex> lock(hubMutex);
            if (hub.ConnForToken(v.token) != conn) return;
        }
        const auto now = RelayHub::Clock::now();
        {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            auto it = endpoints.find(conn);
            if (it == endpoints.end()) return;
            Endpoint& e = it->second;
            if (now - e.second >= std::chrono::seconds(1)) {
                e.second = now;
                e.count = 0;
            }
            if (++e.count > kMaxDatagramsPerSecond) return;
        }
        std::vector<HubAction> actions;
        {
            std::lock_guard<std::mutex> lock(hubMutex);
            actions = hub.OnDatagramData(conn, v.body, v.bodySize, now);
        }
        Execute(actions);
    }

    /// A datagram's Deliver to a connection with a bound address, as a datagram; false to send it
    /// over the WebSocket instead.
    bool SendDatagram(const HubAction& a) {
        if (a.bytes.empty()) return false;
        Endpoint to;
        {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            auto it = endpoints.find(a.conn);
            if (it == endpoints.end()) return false;
            to = it->second;
        }
        // The WebSocket's Deliver is type, sender, payload; the datagram is magic, type, the rest.
        std::vector<std::uint8_t> d = ServerDatagram(DatagramType::Deliver);
        d.insert(d.end(), a.bytes.begin() + 1, a.bytes.end());
        if (d.size() > kMaxDatagramBytes) return false;
#if defined(FREE_DIRECT_SERVER_WEBRTC)
        if (to.channel) {
            if (!to.channel->isOpen()) return false;
            to.channel->send(reinterpret_cast<const std::byte*>(d.data()), d.size());
            return true;
        }
#endif
        if (udp == kNoUdpSocket) return false;
        sendto(udp, reinterpret_cast<const char*>(d.data()), static_cast<int>(d.size()), 0,
               reinterpret_cast<const sockaddr*>(&to.address), to.length);
        return true;
    }

    void ReadDatagrams() {
        std::uint8_t buffer[kMaxDatagramBytes + 1];
        while (udpRunning) {
            sockaddr_storage from{};
            AddressLength fromLength = sizeof(from);
            const auto n = recvfrom(udp, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                    reinterpret_cast<sockaddr*>(&from), &fromLength);
            if (n <= 0) continue;  // the receive timeout, or an error: look at udpRunning again
            ClientDatagramView v;
            if (!ParseClientDatagram(buffer, static_cast<std::size_t>(n), v)) continue;
            ConnId conn = 0;
            {
                std::lock_guard<std::mutex> lock(hubMutex);
                conn = hub.ConnForToken(v.token);
            }
            if (conn == 0) continue;  // unknown tokens get no answer
            const auto now = RelayHub::Clock::now();
            if (v.type == DatagramType::Bind) {
                {
                    std::lock_guard<std::mutex> lock(endpointsMutex);
                    Endpoint& e = endpoints[conn];
                    e.address = from;
                    e.length = fromLength;
                }
                const std::vector<std::uint8_t> ack = ServerDatagram(DatagramType::BindAck);
                sendto(udp, reinterpret_cast<const char*>(ack.data()), static_cast<int>(ack.size()), 0,
                       reinterpret_cast<const sockaddr*>(&from), fromLength);
                continue;
            }
            {
                // Data only from the bound address, and not faster than kMaxDatagramsPerSecond.
                std::lock_guard<std::mutex> lock(endpointsMutex);
                auto it = endpoints.find(conn);
                if (it == endpoints.end() || !SameAddress(it->second.address, from)) continue;
                Endpoint& e = it->second;
                if (now - e.second >= std::chrono::seconds(1)) {
                    e.second = now;
                    e.count = 0;
                }
                if (++e.count > kMaxDatagramsPerSecond) continue;
            }
            std::vector<HubAction> actions;
            {
                std::lock_guard<std::mutex> lock(hubMutex);
                actions = hub.OnDatagramData(conn, v.body, v.bodySize, now);
            }
            Execute(actions);
        }
    }

    void Forget(ConnId conn) {
        {
            std::lock_guard<std::mutex> lock(endpointsMutex);
            endpoints.erase(conn);
        }
#if defined(FREE_DIRECT_SERVER_WEBRTC)
        std::shared_ptr<rtc::PeerConnection> pc;
        std::shared_ptr<rtc::DataChannel> channel;
        {
            std::lock_guard<std::mutex> lock(peersMutex);
            auto p = peers.find(conn);
            if (p != peers.end()) {
                pc = std::move(p->second);
                peers.erase(p);
            }
            auto c = channels.find(conn);
            if (c != channels.end()) {
                channel = std::move(c->second);
                channels.erase(c);
            }
        }
        if (channel) channel->close();
        if (pc) pc->close();
#endif
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
                Forget(relayState->relayId);
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
    if (o.datagrams) {
        const int udpPort = o.udpPort > 0 ? o.udpPort : o.port;
        std::string udpError;
        impl_->udp = OpenUdp(o.host, udpPort, udpError);
        if (impl_->udp == kNoUdpSocket) {
            if (impl_->log) impl_->log("datagrams off: " + udpError + "; everything goes over the WebSocket");
        } else {
#if defined(_WIN32)
            DWORD timeout = 200;
#else
            timeval timeout{0, 200000};
#endif
            setsockopt(impl_->udp, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
            {
                std::lock_guard<std::mutex> lock(impl_->hubMutex);
                impl_->hub.SetDatagramPort(static_cast<std::uint16_t>(udpPort));
            }
            impl_->udpRunning = true;
            impl_->udpThread = std::thread([impl] { impl->ReadDatagrams(); });
            if (impl_->log) impl_->log("datagrams on UDP port " + std::to_string(udpPort));
        }
    }
#if defined(FREE_DIRECT_SERVER_WEBRTC)
    if (o.webrtc) {
        // Every peer connection's ICE on one UDP port (multiplexed), so a firewall needs one port.
        const int rtcPort = o.rtcPort > 0 ? o.rtcPort : (o.udpPort > 0 ? o.udpPort : o.port) + 1;
        impl_->rtcConfig.enableIceUdpMux = true;
        impl_->rtcConfig.portRangeBegin = impl_->rtcConfig.portRangeEnd = static_cast<std::uint16_t>(rtcPort);
        if (!(o.host.empty() || o.host == "0.0.0.0" || o.host == "::")) impl_->rtcConfig.bindAddress = o.host;
        RelayHub::Logger rtcLog = impl_->log;
        rtc::InitLogger(rtc::LogLevel::Error, [rtcLog](rtc::LogLevel, std::string message) {
            if (rtcLog) rtcLog("webrtc: " + message);
        });
        impl_->webrtc = true;
        {
            std::lock_guard<std::mutex> lock(impl_->hubMutex);
            impl_->hub.SetWebRtc(true);
        }
        if (impl_->log) impl_->log("WebRTC data channels on UDP port " + std::to_string(rtcPort));
    }
#endif
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
    if (impl_->udpThread.joinable()) {
        impl_->udpRunning = false;
        impl_->udpThread.join();
    }
#if defined(FREE_DIRECT_SERVER_WEBRTC)
    std::vector<ConnId> rtcConns;
    {
        std::lock_guard<std::mutex> lock(impl_->peersMutex);
        for (const auto& [conn, pc] : impl_->peers) rtcConns.push_back(conn);
    }
    for (ConnId conn : rtcConns) impl_->Forget(conn);
#endif
    if (impl_->udp != kNoUdpSocket) {
        CloseUdp(impl_->udp);
        impl_->udp = kNoUdpSocket;
    }
    // Give the close frames a moment to leave before the sockets go.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    impl_->server->stop();
    impl_->server.reset();
}

} // namespace free_direct_relay
