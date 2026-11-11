# DirectPlay wire protocol (FreeDirect-internal)

**This is FreeDirect's own internal wire format, used only between two programs both linked
against this FreeDirect DirectPlay implementation. It has no relationship to, and is not
compatible with, any real Microsoft DirectPlay wire/packet format.** See `CLAUDE.md`'s DirectPlay
Policy and `docs/directplay-design.md` for the broader design rationale; this document covers only
the concrete byte layout, defined in `src/directplay/DirectPlayWireProtocol.hpp`.

This header is used by `LoopbackDirectPlayTransport` (Decisions 10-18), `EnetDirectPlayTransport`
(Decision 19) and, wrapped in the Free Direct server's own messages, `RelayDirectPlayTransport`
(Decision 30, `docs/directplay-relay-protocol.md`) - the same struct and (de)serialization
functions serve every backend.

## Header layout (version 2, since 2026-11-11)

Every FreeDirect-to-FreeDirect DirectPlay packet begins with a fixed **56-byte** header
(`DirectPlayWirePacketHeader`, `kDirectPlayWireHeaderSize`), written field by field, little-endian
and fixed-width on every platform (`PutWireU32`, `PutWireGuid`). Payload bytes, if any, follow
immediately after the header with no gap.

| Offset | Size (bytes) | Field | Notes |
|---|---|---|---|
| 0 | 4 | `magic` | Always `0x46524450` (`kDirectPlayWireMagic`, "PDRF" in byte order). **Checked on receive** (Decision 27). |
| 4 | 4 | `version` | `2` (`kDirectPlayWireProtocolVersion`). **Checked on receive**; version 1 packets are refused. |
| 8 | 4 | `type` | See "Packet types" below. |
| 12 | 16 | `applicationGuid` | `Data1` as 32 bits, `Data2`, `Data3` as 16 bits, then the 8 bytes of `Data4`. |
| 28 | 16 | `sessionGuid` | Same layout. |
| 44 | 4 | `idFrom` | Sender's player ID (`DPID`). |
| 48 | 4 | `idTo` | Recipient's player ID, `0` (`DPID_ALLPLAYERS`) for a broadcast, or a role-specific convention (see below). |
| 52 | 4 | `payloadLength` | Byte count of the payload that follows; cross-checked against the received size. |

Version 1 (until 2026-11-11) copied `sizeof(GUID)` and `sizeof(DPID)` bytes as they were in
memory: Free API's `GUID::Data1` is an `unsigned long`, so the header was 72 bytes on 64-bit Linux
and 64 on Windows and WebAssembly, and those builds could not read each other. Version 2 is the
same on all of them (Decision 28), which Internet play between native and browser builds needs.

## Packet types (`DirectPlayWirePacketType`)

| Value | Name | Status today | Used by |
|---|---|---|---|
| 0 | `Join` | **Used** | Joining role's `Open(..., DPOPEN_JOIN)`, sent to the host right after connecting (Decision 16); `Open()` then waits for the answer (Decision 29). The host ignores its contents - DPID assignment is driven by the transport-level pending-connection queue (Decision 7), not by this packet. |
| 1 | `JoinAccept` | **Used** | Sent by the host to a newly-assigned peer immediately after `AssignPendingConnection()` succeeds (Decision 16). `idTo` carries the newly-assigned DPID; no payload. The joining role's `Receive()` adopts this DPID into `localPlayerIds` and overwrites its cached `applicationGuid`/`sessionInstanceGuid`. |
| 2 | `JoinReject` | **Defined, never sent** | Would explain *why* a pending connection was turned away (e.g. over `dwMaxPlayers`), but a rejected `pendingPeers_` entry is never assigned a DPID (Decision 9), and `Send()`'s addressing (Decision 14) can only reach `connectedPeers_` entries - so there is structurally no DPID to address this packet to today. A rejected client only observes a disconnect, with no explanation (Decision 9's Caveat, reaffirmed by Decision 16). |
| 3 | `Data` | **Used** | Ordinary application payload - the only type the self-send path and unicast `Send()`/`Receive()` (Decision 15) exercise today. This is the default value of `DirectPlayWirePacketHeader::type`. |
| 4 | `Discovery` | **Defined, never sent** | Reserved for a future ENet-based session-discovery mechanism (analogous to `EnumSessions()`'s loopback-only registry, Decision 18, which has no ENet equivalent yet). Not wired to anything. |
| 5 | `DiscoveryResponse` | **Defined, never sent** | Reserved companion to `Discovery`, same status. |

`Receive()`'s drain loop (`DirectPlay2AImpl::Receive()`, Decision 15/16) `switch`es on `type`:
`Data` enqueues to the local message queue; `JoinAccept` (joining role only) triggers DPID
adoption; `Join` (host role) and every other type currently fall through a `default:` case and are
silently consumed with no effect.

## Known gaps (mostly honestly documented rather than fixed; one has since been fixed - see below)

- **`magic`/`version` are now validated on receive** (`plan.md` `TASK-24H-0176`, resolved from a
  genuine design question asked of the user via `AskUserQuestion` rather than decided
  unilaterally, tracked as `docs/audit_dplay.md` finding D6). `TryDeserializeDirectPlayWireHeader`
  now rejects a header whose `magic`/`version` don't match `kDirectPlayWireMagic`/
  `kDirectPlayWireProtocolVersion` the same way it already rejected a too-small or
  `payloadLength`-mismatched buffer - by returning `std::nullopt`, which the caller treats as "no
  valid packet here." This closes the gap this section used to describe: a wrong-protocol or
  garbage buffer that happened to be the right size used to be accepted and processed as if valid;
  it is now dropped. Every real FreeDirect sender already writes the correct `magic`/`version` via
  `SerializeDirectPlayWireHeader`, so this never rejects legitimate FreeDirect-to-FreeDirect
  traffic. **Deliberately still not done**: a malformed/wrong-protocol packet and a wrong-*version*
  packet still share one rejection outcome (`std::nullopt`, no distinct `DPERR_*` code) rather than
  being distinguished - judged not worth the added complexity when `TASK-24H-0176` was decided.
- **`payloadLength` cross-checking is the other structural validation performed**, alongside the
  magic/version check above. `TryDeserializeDirectPlayWireHeader` rejects a buffer smaller than the
  header, and rejects a header whose `payloadLength` disagrees with the actual trailing byte count
  - but neither check (and none possible from this layer alone) detects a packet that is
  well-formed but semantically wrong (e.g. a `sessionGuid` that doesn't match the receiver's own
  session).
- **No GUID-mismatch validation on join.** `Open()`'s joining branch never compares the joining
  caller's `guidApplication` against the host's before completing a connection (Decision 16's own
  explicit "deliberately still out of scope" note).
- **The header's wire size used to depend on the platform** (`sizeof(GUID)` 24 bytes on 64-bit
  Linux, 16 on Windows and WebAssembly). Fixed by wire version 2 (Decision 28), see above.

This section was originally written to document these gaps, not fix them (`plan.md`
TASK-24H-0095). One of them - `magic`/`version` validation - has since actually been fixed in code
(`TASK-24H-0176`, 2026-07-09) and is now described above as fixed, not as an open gap; the
platform-dependent header size was fixed on 2026-11-11 (Decision 28). The remaining two
(payload-length-only structural validation's own limits, no GUID-mismatch validation on join) are
still open.
