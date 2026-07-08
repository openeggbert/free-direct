# DirectPlay wire protocol (FreeDirect-internal)

**This is FreeDirect's own internal wire format, used only between two programs both linked
against this FreeDirect DirectPlay implementation. It has no relationship to, and is not
compatible with, any real Microsoft DirectPlay wire/packet format.** See `CLAUDE.md`'s DirectPlay
Policy and `docs/directplay-design.md` for the broader design rationale; this document covers only
the concrete byte layout, defined in `src/directplay/DirectPlayWireProtocol.hpp`.

This header is used today by both `LoopbackDirectPlayTransport` (Decisions 10-18) and
`EnetDirectPlayTransport` (Decision 19) - the same struct and (de)serialization functions serve
both backends, so this document applies to either.

## Header layout

Every FreeDirect-to-FreeDirect DirectPlay packet begins with a fixed 56-byte header
(`DirectPlayWirePacketHeader`), serialized field-by-field with `std::memcpy` in declaration order
(**not** `sizeof(DirectPlayWirePacketHeader)`'s in-memory, padding-affected layout - the wire form
is flat and padding-free by construction). Payload bytes, if any, follow immediately after the
header with no gap.

| Offset | Size (bytes) | Field | Type | Notes |
|---|---|---|---|---|
| 0 | 4 | `magic` | `uint32_t` | Always `0x46524450` (`kDirectPlayWireMagic`, ASCII "FRDP" reversed). Rejects non-FreeDirect traffic early. **Not currently checked on receive** - see "Known gaps" below. |
| 4 | 4 | `version` | `uint32_t` | Always `1` today (`kDirectPlayWireProtocolVersion`). Bumped whenever this layout changes incompatibly. **Not currently checked on receive** - see "Known gaps" below. |
| 8 | 4 | `type` | `uint32_t` | Serialized as the underlying `uint32_t`, not the native `enum class` size. See "Packet types" below. |
| 12 | 24 | `applicationGuid` | `GUID` (`Data1: unsigned long`, `Data2/Data3: unsigned short`, `Data4: unsigned char[8]`) | Copied via `std::memcpy(..., sizeof(GUID))`. **`sizeof(GUID)` is 24 bytes on this project's Linux/LP64 build** (`unsigned long` is 8 bytes under LP64, plus alignment padding to `alignof(GUID) == 8`) - **not** the 16 bytes a real Win32 `GUID` occupies (where `unsigned long` is 4 bytes under LLP64). Verified by direct compilation, not assumed - see "Known gaps" below for what this means for portability. |
| 36 | 24 | `sessionGuid` | `GUID` | Same layout and same platform-dependent size as `applicationGuid`. |
| 60 | 4 | `idFrom` | `DPID` (`DWORD`, `static_assert`-enforced 4 bytes - `plan.md` TASK-24H-0020) | Sender's player ID. |
| 64 | 4 | `idTo` | `DPID` | Recipient's player ID (or the target of a role-specific convention - see "Packet types" below). |
| 68 | 4 | `payloadLength` | `uint32_t` | Byte count of the payload that follows the header, if any. Cross-checked against the actual received size by `TryDeserializeDirectPlayWireHeader` (see "Known gaps"). |

Total header size: **72 bytes** on this project's verified Linux/LP64 build (`kDirectPlayWireHeaderSize`,
computed as the sum of the column above, not `sizeof(DirectPlayWirePacketHeader)`) - confirmed by
compiling a standalone program against the real header and printing every field's actual offset
and `kDirectPlayWireHeaderSize` together (all offsets matched the running total exactly). Any
payload bytes are appended by the caller separately - `DirectPlayWirePacketHeader`/
`SerializeDirectPlayWireHeader` do not own or store payload bytes, only `payloadLength`.

## Packet types (`DirectPlayWirePacketType`)

| Value | Name | Status today | Used by |
|---|---|---|---|
| 0 | `Join` | **Used** | Joining role's `Open(..., DPOPEN_JOIN)`, sent fire-and-forget to the host immediately after a successful `Connect()` (Decision 16). The host currently ignores its contents entirely - DPID assignment is still driven by the transport-level pending-connection queue (Decision 7), not by receiving this packet. |
| 1 | `JoinAccept` | **Used** | Sent by the host to a newly-assigned peer immediately after `AssignPendingConnection()` succeeds (Decision 16). `idTo` carries the newly-assigned DPID; no payload. The joining role's `Receive()` adopts this DPID into `localPlayerIds` and overwrites its cached `applicationGuid`/`sessionInstanceGuid`. |
| 2 | `JoinReject` | **Defined, never sent** | Would explain *why* a pending connection was turned away (e.g. over `dwMaxPlayers`), but a rejected `pendingPeers_` entry is never assigned a DPID (Decision 9), and `Send()`'s addressing (Decision 14) can only reach `connectedPeers_` entries - so there is structurally no DPID to address this packet to today. A rejected client only observes a disconnect, with no explanation (Decision 9's Caveat, reaffirmed by Decision 16). |
| 3 | `Data` | **Used** | Ordinary application payload - the only type the self-send path and unicast `Send()`/`Receive()` (Decision 15) exercise today. This is the default value of `DirectPlayWirePacketHeader::type`. |
| 4 | `Discovery` | **Defined, never sent** | Reserved for a future ENet-based session-discovery mechanism (analogous to `EnumSessions()`'s loopback-only registry, Decision 18, which has no ENet equivalent yet). Not wired to anything. |
| 5 | `DiscoveryResponse` | **Defined, never sent** | Reserved companion to `Discovery`, same status. |

`Receive()`'s drain loop (`DirectPlay2AImpl::Receive()`, Decision 15/16) `switch`es on `type`:
`Data` enqueues to the local message queue; `JoinAccept` (joining role only) triggers DPID
adoption; `Join` (host role) and every other type currently fall through a `default:` case and are
silently consumed with no effect.

## Known gaps (honestly documented, not fixed here)

- **`magic`/`version` are parsed but never validated on receive.** `TryDeserializeDirectPlayWireHeader`
  deliberately does not check them (see its own doc comment in `DirectPlayWireProtocol.hpp`) - a
  wrong-protocol or wrong-version packet is a different rejection reason than a malformed/truncated
  buffer, with its own `DPERR_*` mapping "to be decided once a real transport actually receives
  packets" (a decision that has not yet been made, even though `EnetDirectPlayTransport` now does
  receive real packets as of Decision 19). Today, a buffer that happens to be the right size but
  has garbage `magic`/`version` bytes is still accepted and processed as if it were valid.
- **`payloadLength` cross-checking is the only structural validation performed.**
  `TryDeserializeDirectPlayWireHeader` rejects a buffer smaller than the header, and rejects a
  header whose `payloadLength` disagrees with the actual trailing byte count - but does not (and
  cannot, from this layer alone) detect a packet that is well-formed but semantically wrong (e.g. a
  `sessionGuid` that doesn't match the receiver's own session).
- **No GUID-mismatch validation on join.** `Open()`'s joining branch never compares the joining
  caller's `guidApplication` against the host's before completing a connection (Decision 16's own
  explicit "deliberately still out of scope" note).
- **The header's wire size is not a portable constant across ABIs.** `applicationGuid`/
  `sessionGuid` are copied via `std::memcpy(..., sizeof(GUID))`, and `sizeof(GUID)` depends on the
  compiling platform's data model: verified as **24 bytes** on this project's Linux/LP64 build
  (`unsigned long` is 8 bytes), but a real Win32 build (`LLP64`, `unsigned long` is 4 bytes) would
  produce a **16-byte** `GUID` and therefore a **64-byte**, not 72-byte, total header. This mirrors
  the exact same "serialized using the local `sizeof(...)` as-is" pattern
  `DirectPlayWireProtocol.hpp`'s own file comment already documents for `DPID` - it just wasn't
  previously written down for `GUID` specifically. Not a bug: `CLAUDE.md`'s DirectPlay Policy
  already establishes that only FreeDirect-to-FreeDirect compatibility matters, and in practice
  both peers in any real session build from the same source against the same target platform's
  ABI. It would only become a real interoperability problem if two FreeDirect builds on different
  data models (e.g. one Linux/LP64 peer and one Windows/LLP64 peer) ever tried to talk to each
  other - not a configuration this project builds or tests today.

None of these are fixed in this document - documenting them is this task's (`plan.md`
TASK-24H-0095) entire scope; fixing any of them is separate, later work.
