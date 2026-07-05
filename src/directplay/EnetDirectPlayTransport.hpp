/**
 * @file EnetDirectPlayTransport.hpp
 * @brief ENet-backed `IDirectPlayTransport` implementation (`plan.md` Phase 5).
 *
 * Compiled only when `FREE_DIRECT_ENABLE_ENET` is `ON` - `CMakeLists.txt` only adds
 * `EnetDirectPlayTransport.cpp` to `target_sources()` under that option, and only defines
 * the `FREE_DIRECT_ENABLE_ENET` macro `DirectPlay.cpp` `#ifdef`s on under the same option
 * (`docs/directplay-design.md` Decision 4). `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`)
 * now includes this header and constructs this class under that macro; it still always
 * uses `LoopbackDirectPlayTransport` in the default (`FREE_DIRECT_ENABLE_ENET=OFF`) build.
 *
 * This header is intentionally private to the DirectPlay implementation: it must
 * never be included from `include/dplay.h` and must never be installed. Per
 * `CLAUDE.md`'s Internal Backend Policy, a private header under `src/**` that is only
 * ever included by `.cpp` files (as this one is, today) is allowed to name ENet types
 * directly - the policy bans ENet from reaching `include/`, not from internal headers.
 *
 * Real behavior implemented so far (`plan.md` Phase 5/6, in order): the
 * constructor/destructor join/leave a process-wide `enet_initialize()`/
 * `enet_deinitialize()` reference count (see the `.cpp` file); `Listen(port)` creates a
 * real listening `ENetHost`; `Connect(address, port)` creates a client-role `ENetHost`
 * (no listen address) and calls `enet_host_connect` (implemented together, since a
 * created-but-never-connected client host is not independently testable); `Shutdown()`
 * (and the destructor) perform a real graceful disconnect (`enet_peer_disconnect` +
 * a bounded wait for `ENET_EVENT_TYPE_DISCONNECT`) before destroying whatever
 * host/peer `Listen()`/`Connect()` created; `Send()` sends a real ENet packet
 * (`ENET_PACKET_FLAG_RELIABLE` or `ENET_PACKET_FLAG_UNSEQUENCED`, selected by its
 * `reliable` parameter) to the single `peer_` this class tracks; `Service()`
 * (`docs/directplay-design.md` Decision 6) drains pending ENet events - adopting a
 * newly-connected peer as `peer_` (host role) if none is tracked yet, clearing `peer_`
 * on disconnect, and discarding (not delivering) received packets, since transport-level
 * `Receive()` is still an honest `false` stub - no `plan.md` task covers it yet.
 * All of this is scoped to the single `peer_` this class tracks; multiple connected
 * peers (a real multi-player host) is Phase 6/10's job.
 * `Open(..., DPOPEN_CREATE)` constructs this class under `FREE_DIRECT_ENABLE_ENET`, calls
 * `Listen(kDefaultDirectPlayEnetPort)` when hosting, and calls `Service()` once per
 * `IDirectPlay2A::Receive()` call - see `kDefaultDirectPlayEnetPort` below.
 * @note Status: PARTIAL
 */
#pragma once

#include "DirectPlayTransport.hpp"

#include <cstdint>
#include <enet/enet.h>

namespace free_direct_directplay {

/// Default ENet listen port `Open(..., DPOPEN_CREATE)` uses when hosting under
/// `FREE_DIRECT_ENABLE_ENET` (`docs/directplay-design.md` Decision 5). A fixed,
/// FreeDirect-internal constant, not derived from any real DirectPlay API value -
/// `DPSESSIONDESC2` has no port-like field (real DirectPlay abstracts network
/// addressing behind service providers FreeDirect does not implement).
inline constexpr std::uint16_t kDefaultDirectPlayEnetPort = 51321;

/**
 * @brief `IDirectPlayTransport` implemented over real ENet reliable/unreliable UDP.
 * @note Status: PARTIAL
 */
class EnetDirectPlayTransport final : public IDirectPlayTransport {
public:
    EnetDirectPlayTransport();
    ~EnetDirectPlayTransport() override;

    bool Listen(std::uint16_t port) override;
    bool Connect(const char* address, std::uint16_t port) override;
    bool Send(const void* data, std::size_t size, bool reliable) override;
    bool Receive(void* buffer, std::size_t bufferSize, std::size_t* outSize) override;
    void Service() override;
    void Shutdown() override;

    /// True once this instance's constructor successfully joined the process-wide
    /// enet_initialize() reference count; false if enet_initialize() itself failed.
    /// Exists so tests can observe real init behavior without needing a way to drive
    /// Listen()/Connect() yet.
    bool IsEnetReady() const { return enetReady_; }

    /// True once Listen() or Connect() has created a real ENetHost that has not yet
    /// been torn down by Shutdown(). Exists purely so a test can observe real
    /// host-creation behavior directly - there is no other public way to tell a
    /// genuinely-created ENetHost apart from Listen()/Connect() having merely returned
    /// true.
    bool HasHost() const { return host_ != nullptr; }

    /// True once a peer_ is tracked: either Connect() queued a connection attempt (does
    /// *not* by itself mean that attempt completed - checking ENetPeer::state directly
    /// is out of scope), or Service() adopted a newly-connected peer on the hosting
    /// side (this *does* mean a real connection completed, since Service() only adopts
    /// on a genuine ENET_EVENT_TYPE_CONNECT event).
    bool HasPeer() const { return peer_ != nullptr; }

private:
    bool enetReady_ = false;
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
};

} // namespace free_direct_directplay
