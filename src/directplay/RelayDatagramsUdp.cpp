/**
 * @file RelayDatagramsUdp.cpp
 * @brief IRelayDatagrams over a UDP socket (native programs). See RelayDatagrams.hpp.
 *
 * The socket is connected to the server's address, so the system delivers only the server's
 * datagrams to it. It is read in every state: the server sends by UDP as soon as it has a Bind,
 * even if its BindAck is lost.
 * @note Status: IMPLEMENTED
 */
#include "RelayDatagrams.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>

namespace free_direct_directplay {

using namespace free_direct_relay;

namespace {

#if defined(_WIN32)
using UdpSocket = SOCKET;
const UdpSocket kNoSocket = INVALID_SOCKET;
void CloseSocket(UdpSocket s) { closesocket(s); }
bool SetNonBlocking(UdpSocket s) {
    u_long on = 1;
    return ioctlsocket(s, FIONBIO, &on) == 0;
}
#else
using UdpSocket = int;
constexpr UdpSocket kNoSocket = -1;
void CloseSocket(UdpSocket s) { ::close(s); }
bool SetNonBlocking(UdpSocket s) {
    const int flags = fcntl(s, F_GETFL, 0);
    return flags >= 0 && fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
#endif

class UdpDatagrams final : public IRelayDatagrams {
public:
    ~UdpDatagrams() override {
        if (socket_ != kNoSocket) CloseSocket(socket_);
    }

    bool Start(const std::string& host, std::uint16_t port, const DatagramToken& token) override {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_flags = AI_NUMERICSERV;
        addrinfo* found = nullptr;
        if (host.empty() || getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0) return false;
        for (addrinfo* a = found; a && socket_ == kNoSocket; a = a->ai_next) {
            UdpSocket s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == kNoSocket) continue;
            if (connect(s, a->ai_addr, static_cast<int>(a->ai_addrlen)) == 0 && SetNonBlocking(s)) socket_ = s;
            else CloseSocket(s);
        }
        freeaddrinfo(found);
        if (socket_ == kNoSocket) return false;
        token_ = token;
        started_ = Clock::now();
        state_ = State::Binding;
        Pump(nullptr);  // the first Bind
        return true;
    }

    bool Ready() const override { return state_ == State::Bound; }

    bool Send(const std::vector<std::uint8_t>& datagram) override {
        if (socket_ == kNoSocket) return false;
        return send(socket_, reinterpret_cast<const char*>(datagram.data()), static_cast<int>(datagram.size()), 0) ==
               static_cast<long>(datagram.size());
    }

    void Pump(const DeliverFn& deliver) override {
        if (socket_ == kNoSocket) return;
        std::uint8_t buffer[kMaxDatagramBytes + 1];
        for (int i = 0; i < 256; ++i) {
            const auto n = recv(socket_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0);
            if (n <= 0) break;
            ServerDatagramView v;
            if (!ParseServerDatagram(buffer, static_cast<std::size_t>(n), v)) continue;
            if (v.type == DatagramType::BindAck && state_ == State::Binding) {
                state_ = State::Bound;
                std::fprintf(stderr, "free-direct relay: datagrams on (UDP)\n");
            } else if (v.type == DatagramType::Deliver && deliver) {
                deliver(v.sender, std::vector<std::uint8_t>(v.payload, v.payload + v.payloadSize));
            }
        }
        const auto now = Clock::now();
        if (state_ == State::Binding && now - started_ > std::chrono::milliseconds(kDatagramBindTimeoutMs)) {
            state_ = State::Off;
            std::fprintf(stderr, "free-direct relay: datagrams off: no answer over UDP; everything over the WebSocket\n");
        }
        if (state_ == State::Off) return;
        const auto every = std::chrono::milliseconds(state_ == State::Binding ? kDatagramBindRetryMs : kDatagramKeepAliveMs);
        if (now - lastBind_ >= every) {
            lastBind_ = now;  // repeated, it also keeps a NAT's mapping open
            Send(ClientDatagram(token_, DatagramType::Bind));
        }
    }

private:
    using Clock = std::chrono::steady_clock;
    enum class State { Binding, Bound, Off };
    UdpSocket socket_ = kNoSocket;
    DatagramToken token_{};
    State state_ = State::Off;
    Clock::time_point started_{}, lastBind_{};
};

} // namespace

std::unique_ptr<IRelayDatagrams> CreateRelayDatagrams() { return std::make_unique<UdpDatagrams>(); }
std::uint8_t RelayDatagramsWelcomeFlag() { return kWelcomeFlagDatagrams; }

} // namespace free_direct_directplay
