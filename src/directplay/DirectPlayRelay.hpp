/**
 * @file DirectPlayRelay.hpp
 * @brief The "FreeDirect server" provider's glue: server address, connection dialog, room.
 *
 * Private to the DirectPlay implementation, compiled only with FREE_DIRECT_ENABLE_RELAY.
 *
 * When the game enumerates sessions with the server provider for the first time on a DirectPlay
 * object, the player confirms the connection in a dialog (Free API's
 * `FreeApiPromptServerConnection`): the server's address or URL, its port, its password if it
 * has one, and whether to create a room or to join one with its secret ID. The dialog is
 * prefilled from the previous connection, or from the environment:
 *
 * | Variable | Meaning |
 * |---|---|
 * | `FREE_DIRECT_SERVER` | host name, IP address, `host:port` or `ws://` / `wss://` URL |
 * | `FREE_DIRECT_SERVER_PORT` | port, when `FREE_DIRECT_SERVER` has none (default 51325) |
 * | `FREE_DIRECT_SERVER_PASSWORD` | server password |
 * | `FREE_DIRECT_ROOM` | a room's secret ID, or `new` to create a room |
 * | `FREE_DIRECT_ROOM_FILE` | after creating a room, write its ID to this file |
 *
 * With both `FREE_DIRECT_SERVER` and `FREE_DIRECT_ROOM` set, no dialog is shown (automation).
 * @note Status: IMPLEMENTED
 */
#pragma once

#include "dplay.h"
#include "RelayProtocol.hpp"

#include <string>

namespace free_direct_directplay {

/// Puts the process in a room of a Free Direct server. With `confirmWithPlayer`, the connection
/// dialog is shown (prefilled) even when a room is already entered; otherwise an entered room
/// is kept, and a lost connection is re-established silently, falling back to the dialog.
/// Returns DP_OK, DPERR_USERCANCEL (the player cancelled) or DPERR_NOCONNECTION.
HRESULT EnsureRelayRoom(bool confirmWithPlayer);

/// "ws://host:port/" from what the player typed: a host name, an IP address (IPv6 in
/// brackets), host:port, or a ws://, wss://, http:// or https:// URL. `port` applies when the
/// address has none (empty: 51325). Returns an empty string and sets `error` if invalid.
std::string BuildRelayUrl(const std::string& server, const std::string& port, std::string& error);

/// The identity of this game and edition: rooms only admit connections with the same one.
std::string RelayCompatIdentity();

free_direct_relay::RelayGuid ToRelayGuid(const GUID& guid);
GUID FromRelayGuid(const free_direct_relay::RelayGuid& guid);

} // namespace free_direct_directplay
