# Free Direct server protocol

The protocol between a game built with Free Direct's "FreeDirect server" provider and
`free-direct-server` (`docs/directplay-design.md` Decision 30). Source:
[`src/relay/RelayProtocol.hpp`](../src/relay/RelayProtocol.hpp) (shared by client and server),
[`server/RelayHub.cpp`](../server/RelayHub.cpp) (server logic),
[`src/directplay/RelayClient.cpp`](../src/directplay/RelayClient.cpp) (client).

It is Free Direct's own protocol, not Microsoft's DirectPlay protocol.

## Transport

- A WebSocket connection (`ws://`, or `wss://` with TLS), default port **51325**.
- Binary messages only; one WebSocket message is one protocol message. A text message closes
  the connection.
- A message is at most 8192 bytes. Larger messages close the connection.
- No per-message compression.
- The server pings every 20 seconds (`--ping-interval`); browsers and IXWebSocket answer on their
  own, and a client that stops answering is disconnected.

## Encoding

Every message starts with a one-byte type. Integers are little-endian (`u8`, `u16`, `u32`).
A string (`str`) is a `u16` byte count, then that many UTF-8 bytes, at most 512. A `guid` is 16
bytes in DirectPlay wire form (`Data1` 32 bits, `Data2`, `Data3` 16 bits, `Data4` 8 bytes, all
little-endian, `docs/directplay-protocol.md`). A `SessionInfo` is
`instance guid, application guid, maxPlayers u32, currentPlayers u32, name str`.

## Messages

Client to server:

| Type | Name | Fields | Answer |
|---|---|---|---|
| `0x01` | Hello | magic `u32` = `0x4C524446` ("FDRL"), version `u16` = 1, compat `str`, password `str`, client `str` | Welcome, or Error and close |
| `0x02` | CreateRoom | - | RoomCreated |
| `0x03` | JoinRoom | room ID `str` | RoomJoined or Error |
| `0x04` | ListSessions | request ID `u32` | SessionList |
| `0x05` | HostSession | `SessionInfo` (`currentPlayers` ignored) | SessionHosted or Error |
| `0x06` | CloseSession | - | (members get SessionEnded) |
| `0x07` | JoinSession | instance `guid` | SessionJoined or Error; the host gets PeerJoined |
| `0x08` | LeaveSession | - | (the host gets PeerLeft) |
| `0x09` | Admit | member connection `u32` | - |
| `0x0A` | Refuse | member connection `u32` | (the member gets SessionEnded Refused) |
| `0x0B` | Data | route `u8`, target connection `u32`, payload (rest) | (Deliver to the recipients) |

Server to client:

| Type | Name | Fields |
|---|---|---|
| `0x81` | Welcome | version `u16`, the client's connection ID `u32`, flags `u8` (bit 0: the server has a password; bit 1: datagrams, then UDP port `u16` and token, see "Datagrams") |
| `0x82` | Error | request type `u8`, code `u16`, text `str` (English) |
| `0x83` | RoomCreated | room ID `str` (formatted) |
| `0x84` | RoomJoined | room ID `str` (formatted) |
| `0x85` | SessionList | request ID `u32`, count `u16`, `SessionInfo` × count |
| `0x86` | SessionHosted | - |
| `0x87` | SessionJoined | the host's connection `u32` |
| `0x88` | PeerJoined | member connection `u32` (to the host: waiting for admission) |
| `0x89` | PeerLeft | member connection `u32` (to the host: a pending or admitted member is gone) |
| `0x8A` | SessionEnded | reason `u8`: 0 host closed or left, 1 refused, 2 server shutting down |
| `0x8B` | Deliver | sender connection `u32`, payload (rest) |

Error codes: 1 malformed, 2 no hello yet, 3 protocol version, 4 wrong password, 5 rate limited,
6 server full, 7 origin not allowed, 8 too many rooms, 9 no such room, 10 room full, 11 room of
another game or edition, 12 not in a room, 13 too many games in the room, 14 game exists, 15 no
such game, 16 game full, 17 already in a game, 18 not in a game, 19 not the host, 20 unknown
member.

## Lifecycle

1. **Hello.** The first message. A wrong magic, version or password closes the connection.
   `compat` names the game, edition and string table (`FREE_DIRECT_RELAY_COMPAT`, e.g.
   `blupi_recon/sb1-blupi/blupi/1`); a room only admits connections with its creator's identity.
2. **Room.** CreateRoom makes a room with a new secret ID and enters it; JoinRoom enters an
   existing one. A connection is in at most one room; entering another leaves the first.
   Rooms are never listed. A room without members is deleted after `--room-idle-timeout`
   (default 600 s), so its creator can reconnect.
3. **Sessions** inside a room correspond to DirectPlay sessions. HostSession makes the sender the
   session's host. JoinSession makes the sender a *pending member* and tells the host
   (PeerJoined); the host's DirectPlay layer assigns a DPID and sends Admit, then its JoinAccept
   as Data; or it sends Refuse. A connection is in at most one session.
4. **Data routes**:
   - `0` to the host: from a pending or admitted member (a pending member's Join packet).
   - `1` to one member: from the host, to a pending or admitted member of its session.
   - `2` to all others: from the host or an admitted member, to the host and every admitted
     member except the sender, each exactly once.

   Anything else is dropped silently (for example a message that crossed the session's end).
   Deliver carries the sender's connection ID; the payload is a whole DirectPlay wire packet
   (header and game data) that the server does not interpret.
5. **Leaving.** CloseSession, LeaveSession, or closing the connection. When the host goes, every
   member gets SessionEnded and the session is gone; when a member goes, the host gets PeerLeft.

## Datagrams

Over the WebSocket everything arrives, in order: one lost TCP segment holds back every message
behind it until TCP resends it, and they then arrive together, which a game that predicts the other
players for a few frames shows as a stop and a jump (`docs/relay-latency.md`). So the server also
takes **non-guaranteed Data as UDP datagrams**, where a loss only loses that message, as DirectPlay's
own non-guaranteed messages did. Source: [`src/relay/RelayDatagram.hpp`](../src/relay/RelayDatagram.hpp).

- **Offer.** Welcome's flags bit 1 (`0x02`) says the server takes datagrams; the flags are then
  followed by the server's UDP port `u16` (by default the WebSocket's port number) and this
  connection's **token** (16 random bytes). A server without datagrams (`--no-udp`, or before them)
  sends neither, and clients that do not know them ignore the rest of Welcome.
- **Client to server** (UDP to the WebSocket's host and Welcome's port): magic `u32` = `0x55524446`
  ("FDRU"), token (16 bytes), type `u8`, then:
  - `0x01` **Bind**: nothing. Binds the datagram's source address to the token's connection (a
    later Bind rebinds it). The client repeats it every 250 ms until BindAck, gives up after 3 s
    (everything then goes over the WebSocket), and repeats it every 15 s afterwards, which keeps a
    NAT's mapping open.
  - `0x02` **Data**: route `u8`, target `u32`, payload: as the WebSocket's Data, for messages that
    need not arrive. Accepted only from the bound address, at most 400 per second per connection.
- **Server to client** (to the bound address): magic `u32`, type `u8`, then:
  - `0x81` **BindAck**: nothing.
  - `0x82` **Deliver**: sender `u32`, payload: as the WebSocket's Deliver.
- A Deliver goes as a datagram only if its Data came as one and the recipient has a bound address;
  otherwise over the WebSocket. Guaranteed messages and every control message always use the
  WebSocket. A datagram is at most 1200 bytes; a longer non-guaranteed message goes over the
  WebSocket.
- Unknown tokens get no answer. The token travels inside Welcome, so over `wss://` it is as private as
  the connection; the datagrams themselves are not encrypted (the WebSocket's `ws://` is not either).
- Clients: Free Direct's native DirectPlay (`src/directplay/RelayDatagramsUdp.cpp`) and the SDL3
  port's relay client (blupi_recon `port/common/net/relay.cpp`). A browser has no UDP: WebRTC data
  channels are the planned equivalent (`docs/relay-latency.md`).

## Room IDs

20 characters of Crockford base32 (`0-9`, `A-Z` without `I`, `L`, `O`, `U`), from 100 random bits of
the operating system's random source, shown as `ABCDE-FGHJK-MNPQR-STVWX`. Entering one ignores
case, spaces and dashes, and reads `O` as `0` and `I`/`L` as `1`. Logs show only the first four
characters.

## Abuse limits

- Per connection, 5 wrong room IDs close it.
- Per IP address, 20 wrong passwords or room IDs within 10 minutes refuse further hellos and
  room joins from it until the window passes.
- Limits on connections (512), rooms (256), members per room (16) and games per room (8), all
  configurable (`docs/free-direct-server.md`).
- A client whose unsent data exceeds 1 MiB is disconnected as too slow.

## Origin

A browser sends its page's origin (`https://…`, `http://…` or `null`) with the WebSocket
handshake. With `--allow-origin`, the server only accepts pages from those origins. Clients
without an `Origin` header, or with a `ws://`/`wss://` one (IXWebSocket sends the server's own
URL), are native and always accepted. The check keeps other web pages from using the server; it
is not authentication. Use the password for that.
