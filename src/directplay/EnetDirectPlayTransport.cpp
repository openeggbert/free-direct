/**
 * @file EnetDirectPlayTransport.cpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 * @note Status: STUB
 */
#include "EnetDirectPlayTransport.hpp"

#include <mutex>

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
    return host_ != nullptr;
}

bool EnetDirectPlayTransport::Connect(const char* address, std::uint16_t port) {
    if (!enetReady_ || host_ || peer_) return false;

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
    // non-null peer_ means the attempt was queued, not that it has completed. Waiting
    // for (or timing out on) an actual connection is a later concern (this class has
    // no event-servicing method yet), not this task's.
    peer_ = enet_host_connect(host_, &enetAddress, kChannelLimit, 0);
    if (!peer_) {
        enet_host_destroy(host_);
        host_ = nullptr;
        return false;
    }

    return true;
}

bool EnetDirectPlayTransport::Send(const void* data, std::size_t size, bool reliable) {
    // Only the peer_ this class itself tracks (the one Connect() created) can be sent
    // to - a host with multiple connected peers (Listen()'s eventual real multi-peer
    // role) needs its own per-peer addressing, which is Phase 6/10's job, not this
    // one's. This mirrors Shutdown()'s existing peer_-only scoping.
    if (!peer_) return false;

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
    if (enet_peer_send(peer_, 0, packet) != 0) {
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

void EnetDirectPlayTransport::Shutdown() {
    if (peer_ && host_) {
        // Graceful disconnect: ask the remote peer to acknowledge before we tear the
        // host down, rather than just destroying local state and leaving the remote
        // side to notice via a timeout. Only handles the one peer_ this class itself
        // tracks (the one Connect() created) - a host with multiple connected peers
        // (Listen()'s eventual real multi-peer role) needs its own per-peer tracking,
        // which is Phase 6/10's job, not this one's.
        enet_peer_disconnect(peer_, 0);

        // Bounded wait, not indefinite: Shutdown() must still return promptly even if
        // the remote peer is gone or unresponsive. kDisconnectPollAttempts *
        // kDisconnectPollTimeoutMs is the worst-case time spent here - both are
        // provisional, generous-but-small placeholders, not derived from a specific
        // free-eggbert/planetblupi requirement.
        constexpr int kDisconnectPollAttempts = 10;
        constexpr unsigned kDisconnectPollTimeoutMs = 100;
        for (int i = 0; i < kDisconnectPollAttempts; ++i) {
            ENetEvent event;
            if (enet_host_service(host_, &event, kDisconnectPollTimeoutMs) <= 0) continue;
            if (event.type == ENET_EVENT_TYPE_RECEIVE) enet_packet_destroy(event.packet);
            if (event.type == ENET_EVENT_TYPE_DISCONNECT) break;
        }
    }

    if (host_) {
        // enet_host_destroy() also frees every ENetPeer belonging to this host, so
        // peer_ (if set) would be left dangling if not cleared here too.
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    peer_ = nullptr;
}

} // namespace free_direct_directplay
