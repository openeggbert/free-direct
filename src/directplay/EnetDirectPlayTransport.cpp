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

bool EnetDirectPlayTransport::Connect() { return false; }

bool EnetDirectPlayTransport::Send(const void* /*data*/, std::size_t /*size*/) { return false; }

bool EnetDirectPlayTransport::Receive(void* /*buffer*/, std::size_t /*bufferSize*/,
                                       std::size_t* /*outSize*/) {
    return false;
}

void EnetDirectPlayTransport::Shutdown() {
    if (host_) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
}

} // namespace free_direct_directplay
