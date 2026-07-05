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
 * The constructor/destructor do real work: they join/leave a process-wide
 * `enet_initialize()`/`enet_deinitialize()` reference count (see the `.cpp` file),
 * since ENet requires that pair to be called exactly once per process, not once per
 * instance - multiple `EnetDirectPlayTransport` instances (e.g. a host and a client
 * transport in the same process) must share one real init/deinit pair. `Listen()` now
 * does real work too: it creates a real `ENetHost` via `enet_host_create` in listen
 * mode. `Connect()`/`Send()`/`Receive()` are still honest stubs - all
 * backend-selecting/routing behavior (`Open()`) still uses
 * `LoopbackDirectPlayTransport` exclusively, so returning `false` from them (rather
 * than a fake success) does not regress any currently-reachable code path. `Shutdown()`
 * (and the destructor) now destroy the `ENetHost` created by `Listen()`, if any -
 * cleanup for a resource this class creates isn't a separate task, it's the other half
 * of creating it. Real client host/peer creation, connect, send, receive, and
 * disconnect handling are separate, later tasks in this same `plan.md` Phase 5.
 * @note Status: STUB
 */
#pragma once

#include "DirectPlayTransport.hpp"

#include <cstdint>
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

    bool Listen(std::uint16_t port) override;
    bool Connect() override;
    bool Send(const void* data, std::size_t size) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Shutdown() override;

    /// True once this instance's constructor successfully joined the process-wide
    /// enet_initialize() reference count; false if enet_initialize() itself failed.
    /// Exists so tests can observe real init behavior without needing a way to drive
    /// Listen()/Connect() yet (both are still stubs - see the class comment above).
    bool IsEnetReady() const { return enetReady_; }

    /// True once Listen() has created a real ENetHost that has not yet been torn down
    /// by Shutdown(). Exists purely so a test can observe real host-creation behavior
    /// directly - there is no other public way to tell a genuinely-created ENetHost
    /// apart from Listen() having merely returned true.
    bool HasHost() const { return host_ != nullptr; }

private:
    bool enetReady_ = false;
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
};

} // namespace free_direct_directplay
