/**
 * @file EnetDirectPlayTransport.hpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 *
 * Compiled only when `FREE_DIRECT_ENABLE_ENET` is `ON` - `CMakeLists.txt` only adds
 * `EnetDirectPlayTransport.cpp` to `target_sources()` under that option. Nothing else
 * includes this header yet: `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`) still
 * unconditionally assigns `LoopbackDirectPlayTransport` - actually selecting this
 * backend is a later `plan.md` Phase 6 task, not this one.
 *
 * This header is intentionally private to the DirectPlay implementation: it must
 * never be included from `include/dplay.h` and must never be installed. Per
 * `CLAUDE.md`'s Internal Backend Policy, a private header under `src/**` that is only
 * ever included by `.cpp` files (as this one is, today) is allowed to name ENet types
 * directly - the policy bans ENet from reaching `include/`, not from internal headers.
 *
 * Every method below is an honest stub for this task: all backend-selecting/routing
 * behavior (Open()) still uses `LoopbackDirectPlayTransport` exclusively, so returning
 * `false` here (rather than a fake success) does not regress any currently-reachable
 * code path. Real ENet host/peer lifecycle, connect, send, receive, and disconnect
 * handling are separate, later tasks in this same `plan.md` Phase 5.
 * @note Status: STUB
 */
#pragma once

#include "DirectPlayTransport.hpp"

#include <enet/enet.h>

namespace free_direct_directplay {

/**
 * @brief `IDirectPlayTransport` implemented over real ENet reliable UDP.
 * @note Status: STUB
 */
class EnetDirectPlayTransport final : public IDirectPlayTransport {
public:
    EnetDirectPlayTransport();
    ~EnetDirectPlayTransport() override;

    bool Listen() override;
    bool Connect() override;
    bool Send(const void* data, std::size_t size) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Shutdown() override;

private:
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
};

} // namespace free_direct_directplay
