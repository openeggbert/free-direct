/**
 * @file EnetDirectPlayTransport.cpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 * @note Status: STUB
 */
#include "EnetDirectPlayTransport.hpp"

namespace free_direct_directplay {

// ENet init/shutdown lifecycle (enet_initialize/enet_deinitialize, once per process)
// is the next plan.md Phase 5 task, not this one - the constructor/destructor stay
// trivial until then.
EnetDirectPlayTransport::EnetDirectPlayTransport() = default;

EnetDirectPlayTransport::~EnetDirectPlayTransport() = default;

bool EnetDirectPlayTransport::Listen() { return false; }

bool EnetDirectPlayTransport::Connect() { return false; }

bool EnetDirectPlayTransport::Send(const void* /*data*/, std::size_t /*size*/) { return false; }

bool EnetDirectPlayTransport::Receive(void* /*buffer*/, std::size_t /*bufferSize*/,
                                       std::size_t* /*outSize*/) {
    return false;
}

void EnetDirectPlayTransport::Shutdown() {}

} // namespace free_direct_directplay
