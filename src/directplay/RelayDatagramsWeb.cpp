/**
 * @file RelayDatagramsWeb.cpp
 * @brief IRelayDatagrams over a WebRTC data channel to the Free Direct server (browsers, which have
 * no UDP). See RelayDatagrams.hpp.
 *
 * One RTCPeerConnection with one data channel, unordered and without retransmissions, so a lost
 * datagram is lost as over UDP. Its offer, complete with the candidates, goes to the server as
 * RtcOffer (TakeOffer); the server's RtcAnswer comes back through Answer. The server sends BindAck
 * over the channel when it opens; the channel then carries the same datagrams as UDP (with the
 * token). The page's JavaScript objects live on the main browser thread (MAIN_THREAD_EM_ASM), the
 * incoming datagrams wait there until Pump copies them out.
 * @note Status: IMPLEMENTED
 */
#include "RelayDatagrams.hpp"

#include <emscripten.h>

#include <chrono>
#include <cstdio>

// EM_ASM's code is a macro argument: no commas outside parentheses in it.
EM_JS_DEPS(free_direct_relay_rtc, "$stringToUTF8,$lengthBytesUTF8,$UTF8ToString");

namespace free_direct_directplay {

using namespace free_direct_relay;

namespace {

class WebDatagrams final : public IRelayDatagrams {
public:
    ~WebDatagrams() override {
        MAIN_THREAD_EM_ASM({
            var R = Module.freeDirectRtc;
            if (R && R.pc) { try { R.pc.close(); } catch (e) {} }
            Module.freeDirectRtc = null;
        });
    }

    bool Start(const std::string&, std::uint16_t, const DatagramToken& token) override {
        const int ok = MAIN_THREAD_EM_ASM_INT({
            if (typeof RTCPeerConnection === 'undefined') return 0;
            var R = Module.freeDirectRtc = {};
            R.pc = null; R.dc = null; R.offer = null; R.inbox = []; R.failed = false;
            R.sent = 0; R.received = 0;
            try {
                R.pc = new RTCPeerConnection({iceServers: []});
                var options = {};
                options.ordered = false;
                options.maxRetransmits = 0;
                R.dc = R.pc.createDataChannel('datagrams', options);
                R.dc.binaryType = 'arraybuffer';
                R.dc.onmessage = function(e) { if (e.data instanceof ArrayBuffer) { R.inbox.push(e.data); R.received++; } };
                R.pc.onicegatheringstatechange = function() {
                    if (R.pc.iceGatheringState === 'complete' && R.offer === null) R.offer = R.pc.localDescription.sdp;
                };
                R.pc.createOffer().then(function(o) { return R.pc.setLocalDescription(o); })
                    .catch(function() { R.failed = true; });
            } catch (e) { R.failed = true; }
            return 1;
        });
        if (!ok) return false;
        token_ = token;
        started_ = Clock::now();
        state_ = State::Binding;
        return true;
    }

    bool Ready() const override { return state_ == State::Bound; }

    bool Send(const std::vector<std::uint8_t>& datagram) override {
        return MAIN_THREAD_EM_ASM_INT({
            var R = Module.freeDirectRtc;
            if (!R || !R.dc || R.dc.readyState !== 'open') return 0;
            try { R.dc.send(HEAPU8.slice($0, $0 + $1)); R.sent++; return 1; } catch (e) { return 0; }
        }, datagram.data(), static_cast<int>(datagram.size())) != 0;
    }

    void Pump(const DeliverFn& deliver) override {
        for (int i = 0; i < 256; ++i) {
            const int n = MAIN_THREAD_EM_ASM_INT({
                var R = Module.freeDirectRtc;
                return R && R.inbox.length ? R.inbox[0].byteLength : -1;
            });
            if (n < 0) break;
            std::vector<std::uint8_t> buffer(static_cast<std::size_t>(n) + 1);
            MAIN_THREAD_EM_ASM({ HEAPU8.set(new Uint8Array(Module.freeDirectRtc.inbox.shift()), $0); }, buffer.data());
            ServerDatagramView v;
            if (!ParseServerDatagram(buffer.data(), static_cast<std::size_t>(n), v)) continue;
            if (v.type == DatagramType::BindAck && state_ == State::Binding) {
                state_ = State::Bound;
                std::fprintf(stderr, "free-direct relay: datagrams on (WebRTC)\n");
            } else if (v.type == DatagramType::Deliver && deliver) {
                deliver(v.sender, std::vector<std::uint8_t>(v.payload, v.payload + v.payloadSize));
            }
        }
        if (state_ == State::Binding) {
            const bool failed = MAIN_THREAD_EM_ASM_INT({ var R = Module.freeDirectRtc; return R && R.failed ? 1 : 0; }) != 0;
            if (failed || Clock::now() - started_ > std::chrono::milliseconds(kWebRtcTimeoutMs)) {
                state_ = State::Off;
                std::fprintf(stderr, "free-direct relay: datagrams off: no WebRTC channel; everything over the WebSocket\n");
            }
        }
    }

    bool TakeOffer(std::string& sdp) override {
        if (offerTaken_ || state_ != State::Binding) return false;
        const int length = MAIN_THREAD_EM_ASM_INT({
            var R = Module.freeDirectRtc;
            return R && R.offer !== null ? lengthBytesUTF8(R.offer) : -1;
        });
        if (length < 0) return false;
        std::vector<char> text(static_cast<std::size_t>(length) + 1);
        MAIN_THREAD_EM_ASM({ stringToUTF8(Module.freeDirectRtc.offer, $0, $1); }, text.data(), length + 1);
        sdp.assign(text.data(), static_cast<std::size_t>(length));
        offerTaken_ = true;
        return true;
    }

    void Answer(const std::string& sdp) override {
        MAIN_THREAD_EM_ASM({
            var R = Module.freeDirectRtc;
            if (!R || !R.pc) return;
            var answer = {};
            answer.type = 'answer';
            answer.sdp = UTF8ToString($0);
            R.pc.setRemoteDescription(answer).catch(function() { R.failed = true; });
        }, sdp.c_str());
    }

private:
    using Clock = std::chrono::steady_clock;
    enum class State { Binding, Bound, Off };
    DatagramToken token_{};
    State state_ = State::Off;
    bool offerTaken_ = false;
    Clock::time_point started_{};
};

} // namespace

std::unique_ptr<IRelayDatagrams> CreateRelayDatagrams() { return std::make_unique<WebDatagrams>(); }
std::uint8_t RelayDatagramsWelcomeFlag() { return kWelcomeFlagWebRtc; }

} // namespace free_direct_directplay
