/**
 * @file DirectPlaySession.hpp
 * @brief Internal DirectPlay session state.
 *
 * This header is intentionally private to the DirectPlay implementation: it
 * must never be included from `include/dplay.h` and must never be installed.
 * `DirectPlay2AImpl` (`DirectPlay.cpp`) owns one instance and wires `Open`/
 * `CreatePlayer`/`Close` to it; `EnumSessions`/`Send`/`Receive` do not use it
 * yet (later `plan.md` Phase 2 tasks).
 * @note Status: PARTIAL
 */
#pragma once

#include "dplay.h"

#include <string>
#include <vector>

namespace free_direct_directplay {

/**
 * @brief Lifecycle state of a `DirectPlaySession`.
 * @note Status: PARTIAL
 */
enum class DirectPlayObjectState {
    Created,  ///< Constructed, `Open()` not yet called.
    Open,     ///< `Open()` succeeded; session is hosting or joined.
    Closed,   ///< `Close()` was called; all session/player/message state has been cleared.
};

/**
 * @brief Per-object DirectPlay session state.
 *
 * Deliberately does not retain a raw `DPSESSIONDESC2` copy, since that
 * struct's `lpszSessionName`/`lpszPassword` members are caller-owned
 * pointers that must not be kept past the `Open()` call that provided them.
 * The scalar fields worth tracking are broken out below as owned members
 * instead; `sessionName`/`password` are deep-copied into owned strings.
 * @note Status: PARTIAL
 */
class DirectPlaySession {
public:
    DirectPlaySession() = default;
    ~DirectPlaySession() = default;

    bool IsOpen() const { return state == DirectPlayObjectState::Open; }
    bool IsClosed() const { return state == DirectPlayObjectState::Closed; }

    DirectPlayObjectState state = DirectPlayObjectState::Created;

    /// True if this peer created the session (`DPOPEN_CREATE`); false if it joined one.
    bool isHost = false;

    std::vector<DPID> localPlayerIds;
    std::vector<DPID> remotePlayerIds;

    /// Placeholder DPID allocator (starts at 1, skipping the real-DirectPlay-reserved 0);
    /// `plan.md` Phase 9 revisits this against the Phase 0 DPID-vs-array-index finding.
    DPID nextPlayerId = 1;

    std::string sessionName;
    std::string password;
    GUID applicationGuid{};
    DWORD maxPlayers = 0;
    DWORD currentPlayers = 0;
};

} // namespace free_direct_directplay
