/**
 * @file EnetDirectPlayTransport.cpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 * @note Status: STUB
 */
#include "EnetDirectPlayTransport.hpp"

#include <mutex>
#include <vector>

namespace free_direct_directplay {

namespace {
// enet_initialize()/enet_deinitialize() must be called exactly once per process, not
// once per EnetDirectPlayTransport instance - this reference count is shared by every
// live instance so the pair still happens exactly once even if several instances
// exist at once (e.g. a host and a client transport in the same process).
std::mutex g_enetLifecycleMutex;
int g_enetLiveInstances = 0;
bool g_enetInitialized = false;
} // namespace

EnetDirectPlayTransport::EnetDirectPlayTransport() {
    std::lock_guard<std::mutex> lock(g_enetLifecycleMutex);
    if (g_enetLiveInstances == 0) {
        g_enetInitialized = (enet_initialize() == 0);
    }
    if (g_enetInitialized) {
        ++g_enetLiveInstances;
        enetReady_ = true;
    }
    // If enet_initialize() failed, g_enetLiveInstances stays 0 and enetReady_ stays
    // false for this instance - the next constructed instance retries enet_initialize()
    // itself, since nothing else owns a successful init to fall back on.
}

EnetDirectPlayTransport::~EnetDirectPlayTransport() {
    Shutdown();

    if (!enetReady_) return;
    std::lock_guard<std::mutex> lock(g_enetLifecycleMutex);
    if (--g_enetLiveInstances == 0) {
        enet_deinitialize();
        g_enetInitialized = false;
    }
}

bool EnetDirectPlayTransport::Listen(std::uint16_t port) {
    if (!enetReady_ || host_) return false;

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    // Peer count and channel limit are provisional placeholders, not derived from a
    // specific free-eggbert/planetblupi requirement: plan.md's "decide the default
    // ENet channel layout" task (still unchecked) and Phase 6's real
    // DPSESSIONDESC2::dwMaxPlayers wiring are what should eventually replace these
    // constants.
    constexpr std::size_t kMaxPeers = 32;
    constexpr std::size_t kChannelLimit = 1;

    host_ = enet_host_create(&address, kMaxPeers, kChannelLimit, 0, 0);
    if (!host_) return false;
    listening_ = true;
    return true;
}

bool EnetDirectPlayTransport::Connect(const char* address, std::uint16_t port) {
    if (!enetReady_ || host_ || hostPeer_) return false;

    // Channel limit matches Listen()'s - see that method's comment on why it's a
    // provisional placeholder. peerCount=1: a joining client only ever talks to the
    // one host it connects to.
    constexpr std::size_t kChannelLimit = 1;

    // A joining client's ENetHost has no listen address (nullptr) - it only ever
    // initiates outgoing connections, never accepts incoming ones.
    host_ = enet_host_create(nullptr, 1, kChannelLimit, 0, 0);
    if (!host_) return false;

    ENetAddress enetAddress;
    if (enet_address_set_host(&enetAddress, address) != 0) {
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }
    enetAddress.port = port;

    // enet_host_connect() only queues a CONNECT attempt and returns immediately - a
    // non-null hostPeer_ means the attempt was queued, not that it has completed.
    // Waiting for (or timing out on) an actual connection happens via Service()
    // (Decision 6), driven by whatever calls Receive() - not blocked on here.
    hostPeer_ = enet_host_connect(host_, &enetAddress, kChannelLimit, 0);
    if (!hostPeer_) {
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }

    return true;
}

bool EnetDirectPlayTransport::Send(const void* data, std::size_t size, bool reliable) {
    // Client role only - hostPeer_ is the one host this instance connected to. A
    // hosting instance's hostPeer_ stays null forever (docs/directplay-design.md
    // Decision 7): with potentially many connectedPeers_, there is no single implicit
    // recipient to send to - real per-DPID-addressed send is Phase 10's job.
    if (!hostPeer_) return false;

    // ENET_PACKET_FLAG_UNSEQUENCED (not just "no RELIABLE bit") for the unreliable
    // path: it also skips ENet's ordering guarantee, matching "best-effort" as
    // distinctly as ENet's flag set allows. No free-eggbert/planetblupi call site
    // observed in the Phase 0 audit actually needs this path (every real Send() call
    // site collapses to DPSEND_GUARANTEED) - this exists for IDirectPlayTransport
    // interface completeness, per explicit user direction, not a concrete requirement.
    const enet_uint32 flags = reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
    ENetPacket* packet = enet_packet_create(data, size, flags);
    if (!packet) return false;

    // Channel 0 - the single channel Listen()/Connect() already assume via
    // kChannelLimit = 1. plan.md's "decide the default ENet channel layout" task is
    // still open; this reuses that same provisional single-channel assumption rather
    // than introducing a second, undocumented one.
    if (enet_peer_send(hostPeer_, 0, packet) != 0) {
        // enet_peer_send() takes ownership of the packet only on success; on failure
        // it does not, so it must be destroyed here to avoid leaking it.
        enet_packet_destroy(packet);
        return false;
    }

    // Push the packet out now rather than waiting for the next service call, so a
    // caller that never separately services the host still actually transmits.
    enet_host_flush(host_);
    return true;
}

bool EnetDirectPlayTransport::Receive(void* /*buffer*/, std::size_t /*bufferSize*/,
                                       std::size_t* /*outSize*/) {
    return false;
}

void EnetDirectPlayTransport::Service() {
    if (!host_) return;

    // Drain everything currently pending; timeout 0 means "don't block" - Receive()
    // (the only caller today, per docs/directplay-design.md Decision 6) must return
    // promptly regardless of whether any network I/O is ready.
    ENetEvent event;
    while (enet_host_service(host_, &event, 0) > 0) {
        switch (event.type) {
            case ENET_EVENT_TYPE_CONNECT:
                if (listening_) {
                    // Hosting role: queue as pending until a caller allocates a real
                    // DPID and calls AssignPendingConnection() - this class never
                    // allocates one itself (docs/directplay-design.md Decision 7).
                    pendingPeers_.push_back(event.peer);
                } else if (!hostPeer_) {
                    // Joining role: adopt the one connection this instance initiated.
                    hostPeer_ = event.peer;
                }
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                if (listening_) {
                    for (auto it = connectedPeers_.begin(); it != connectedPeers_.end(); ++it) {
                        if (it->second == event.peer) {
                            // Only an already-assigned peer's DPID is reported
                            // (docs/directplay-design.md Decision 8) - a peer still
                            // only in pendingPeers_ (below) has no DPID for
                            // DirectPlaySession to reconcile against.
                            disconnectedPeerIds_.push_back(it->first);
                            connectedPeers_.erase(it);
                            break;
                        }
                    }
                    for (auto it = pendingPeers_.begin(); it != pendingPeers_.end(); ++it) {
                        if (*it == event.peer) {
                            pendingPeers_.erase(it);
                            break;
                        }
                    }
                } else if (event.peer == hostPeer_) {
                    hostPeer_ = nullptr;
                }
                break;
            case ENET_EVENT_TYPE_RECEIVE:
                // Dropped, not delivered: transport-level Receive() is still an honest
                // false stub - no plan.md task covers it yet. Buffering this packet for
                // a Receive() that can't return it would be speculative, half-finished
                // code, not a real capability.
                enet_packet_destroy(event.packet);
                break;
            default:
                break;
        }
    }
}

bool EnetDirectPlayTransport::HasPendingConnection() const { return !pendingPeers_.empty(); }

bool EnetDirectPlayTransport::AssignPendingConnection(DPID id) {
    if (pendingPeers_.empty()) return false;
    connectedPeers_[id] = pendingPeers_.front();
    pendingPeers_.pop_front();
    return true;
}

bool EnetDirectPlayTransport::RejectPendingConnection() {
    if (pendingPeers_.empty()) return false;

    // Popped out immediately, same as a successful AssignPendingConnection() - this
    // peer must never be reconsidered for assignment while its disconnect is in
    // flight. Graceful (enet_peer_disconnect), not enet_peer_disconnect_now: being
    // turned away because the session is full is an ordinary, expected outcome, not a
    // fault (docs/directplay-design.md Decision 9) - the same reasoning Shutdown()
    // already applies to every peer it tears down.
    ENetPeer* peer = pendingPeers_.front();
    pendingPeers_.pop_front();
    enet_peer_disconnect(peer, 0);
    return true;
}

bool EnetDirectPlayTransport::HasDisconnectedPeer() const { return !disconnectedPeerIds_.empty(); }

bool EnetDirectPlayTransport::TakeDisconnectedPeer(DPID* outId) {
    if (disconnectedPeerIds_.empty()) return false;
    if (outId) *outId = disconnectedPeerIds_.front();
    disconnectedPeerIds_.pop_front();
    return true;
}

void EnetDirectPlayTransport::Shutdown() {
    if (host_) {
        // Gracefully disconnect every peer this instance still knows about - the
        // single hostPeer_ (joining role) or every connectedPeers_/pendingPeers_ entry
        // (hosting role) - before tearing the host down, so each remote side can
        // observe a real disconnect rather than just noticing a timeout.
        std::vector<ENetPeer*> toDisconnect;
        if (hostPeer_) toDisconnect.push_back(hostPeer_);
        for (const auto& [id, peer] : connectedPeers_) toDisconnect.push_back(peer);
        for (ENetPeer* peer : pendingPeers_) toDisconnect.push_back(peer);
        for (ENetPeer* peer : toDisconnect) enet_peer_disconnect(peer, 0);

        // Bounded wait, not indefinite: Shutdown() must still return promptly even if
        // a remote peer is gone or unresponsive. kDisconnectPollAttempts *
        // kDisconnectPollTimeoutMs is the worst-case time spent here - both are
        // provisional, generous-but-small placeholders, not derived from a specific
        // free-eggbert/planetblupi requirement.
        constexpr int kDisconnectPollAttempts = 10;
        constexpr unsigned kDisconnectPollTimeoutMs = 100;
        std::size_t stillPending = toDisconnect.size();
        for (int i = 0; i < kDisconnectPollAttempts && stillPending > 0; ++i) {
            ENetEvent event;
            if (enet_host_service(host_, &event, kDisconnectPollTimeoutMs) <= 0) continue;
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) --stillPending;
        }
    }

    if (host_) {
        // enet_host_destroy() also frees every ENetPeer belonging to this host, so
        // hostPeer_/connectedPeers_/pendingPeers_ (if set) would be left dangling if
        // not cleared here too.
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    listening_ = false;
    hostPeer_ = nullptr;
    connectedPeers_.clear();
    pendingPeers_.clear();
    disconnectedPeerIds_.clear();
}

} // namespace free_direct_directplay
