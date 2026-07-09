# DirectPlay limitations and deviations from real Microsoft DirectPlay

**FreeDirect's DirectPlay reimplementation is not, and does not aim to be, compatible with real
Microsoft DirectPlay at the wire/packet level or with real Microsoft DirectPlay service
providers.** The compatibility goal is the `IDirectPlay`/`IDirectPlay2A` C++ API contract that
`../free-eggbert` already calls, satisfied between two programs both linked against this
FreeDirect implementation - see `CLAUDE.md`'s DirectPlay Policy. This document lists every place
FreeDirect's actual behavior differs from real DirectPlay's documented behavior, citing the
numbered Decision in `docs/directplay-design.md` that made each choice rather than repeating its
full rationale here. `planetblupi` has zero DirectPlay usage (confirmed by grep) and places no
requirements on any of this.

## Deviation table

| Area | FreeDirect behavior | Real DirectPlay behavior | Source |
|---|---|---|---|
| `DPID` width | 4-byte `DWORD` (`static_assert`-enforced, `plan.md` TASK-24H-0020) | Also 4-byte `DWORD` in real DirectPlay - **not** a deviation today, but FreeDirect's header briefly used an 8-byte `DWORD_PTR` before this was corrected | Decision 3 |
| `DPID` `0` allocation | Assigned to the **host's own first local player** - a real, ordinary player identity | Reserved: never assigned to a real player; means `DPID_SYSMSG` (as a `from` value) or `DPID_ALLPLAYERS` (as an `idTo` broadcast target) | Decision 3 |
| `DPID` `0` ambiguity (**resolved**) | `idTo == 0` (`DPID_ALLPLAYERS`) always means broadcast, checked before the self-send branch so it takes priority even for the host's own `Send(0, 0, ...)`. A caller can never legitimately want to unicast *to* DPID `0` specifically - no real call site ever addresses the host by its own DPID | N/A - real DirectPlay has no such ambiguity, since `0` is never a real player's ID | Decision 3 (created the ambiguity), **Decision 20 (resolved it, `plan.md` TASK-24H-0148)** |
| Wire/network compatibility | None. FreeDirect's own internal 72-byte (on this project's Linux/LP64 build) packet header, see `docs/directplay-protocol.md` | Real DirectPlay's actual service-provider/packet format | `CLAUDE.md` DirectPlay Policy |
| `EnumSessions()` (loopback) | A **synchronous, in-process, loopback-only** registry lookup (`LoopbackHostedSessionRegistry`) - reads live session state directly, no packets sent, no timeout needed | A real network enumeration with a wire round-trip and a caller-supplied `dwTimeout` | Decision 18 |
| Session discovery over ENet / LAN (**resolved, implemented**) | Real UDP broadcast `Discovery`/`DiscoveryResponse` round-trip, additive to the loopback lookup above - a hosting session listens on a dedicated port (`51323`) and replies; `EnumSessions()` broadcasts and collects for up to `dwTimeout` (its first real use anywhere in this codebase) | Real DirectPlay enumerates via its service provider, potentially over a real network | Decision 18 (loopback-only, unaffected), **Decision 23 (resolved/implemented LAN discovery, `plan.md` TASK-24H-0150)** |
| ENet host address resolution for a joining `Connect()` (**resolved, implemented**) | A new environment variable, `FREE_DIRECT_ENET_HOST_ADDRESS` (`"<host>"` or `"<host>:<port>"`), read once by `Open()`'s ENet joining branch | A real DirectPlay service provider resolves an address for the caller | Decision 5 (flagged the missing address field), **Decision 22 (resolved/implemented, `plan.md` TASK-24H-0149)** |
| `DirectPlayEnumerateA`/`DirectPlayEnumerateW` (**resolved, implemented**) | Invoke the callback exactly once, describing a single FreeDirect-internal placeholder provider (`plan.md` TASK-24H-0100) | Would enumerate real installed service providers | Decision 1 |
| `Open()` join outcome | **Asynchronous.** Returns `DP_OK` immediately once the raw connection exists (or immediately attempts `Connect()` and returns based on that alone, over loopback); the actual join accept/reject outcome is discovered later by polling `Receive()`, same as ordinary messages | `Open()` blocks internally until a join-accepted/-rejected response arrives, or times out | Decision 16 |
| Join rejection reason | A rejected client only observes an ordinary disconnect, with **no explanation packet** (`JoinReject` is defined but never sent - structurally cannot be, since a rejected connection is never assigned a DPID to address it to) | A real join-rejected response, potentially with a reason | Decision 9's Caveat, reaffirmed by Decision 16 |
| GUID validation on join | **None.** `Open()`'s joining branch never compares the joining caller's `guidApplication` against the host's | Real DirectPlay would reject a session/application GUID mismatch | Decision 16 ("deliberately still out of scope") |
| Broadcast (`Send` with `idTo == 0` / `DPID_ALLPLAYERS`) (**resolved, implemented**) | Delivers to every player in the session except the sender - the host iterates its `remotePlayerIds` directly; a non-host sender's broadcast reaches the host first, which relays to every other connected peer | `idTo == DPID_ALLPLAYERS` (`0`) delivers to every player in the session | Decision 15 (flagged as out of scope), **Decisions 20/21 (resolved/implemented, `plan.md` TASK-24H-0148)** |
| Host routing / relay (**resolved, implemented**) | The host relays a broadcast received from one connected peer to every *other* connected peer (never back to the sender), and enqueues its own copy. Direct non-broadcast unicast between two non-host peers still has no path (no real call site needs it - `free-eggbert`'s only pattern is broadcast) | Real DirectPlay's star topology relays messages between non-host peers through the host transparently | Decision 15 (flagged as out of scope), **Decision 21 (resolved/implemented, `plan.md` TASK-24H-0148)** |
| ENet unicast `Send()`/`Receive()` | `Send()` works for the hosting role addressing one of its own `connectedPeers_` (Decision 14). `Receive()` now genuinely buffers and delivers incoming packets for **both** roles (Decision 19 - this was a real gap as recently as Decision 14, since fixed) | Both directions work symmetrically in real DirectPlay | Decisions 14, 19 |
| ENet join handshake (**resolved, implemented**) | The full `Join`/`JoinAccept` handshake (Decision 16) now works over ENet too, once Decision 22 wired `Connect()` - the handshake logic in `Receive()`'s drain loop is backend-agnostic and required no ENet-specific changes | N/A | Decision 16 (originally loopback-only), **Decision 22 (wired ENet `Connect()`, making the existing shared handshake code reachable, `plan.md` TASK-24H-0149)** |
| Player short/long names (`DPNAME`) (**resolved: decided not needed**) | **Not stored.** `CreatePlayer()` accepts `lpPlayerName` but discards it - a deliberate decision, not an open question | A real player name is stored and queryable | Decision 17 (flagged), **Decision 24 (decided not needed - no `GetPlayerName`-style method added, `plan.md` TASK-24H-0135)** |
| Duplicate-player detection (**resolved: decided not needed**) | **Not implemented**, deliberately - no concrete definition of "duplicate" exists given DPID allocation is collision-free by construction and `free-eggbert` only ever calls `CreatePlayer()` once | Real DirectPlay may reject a duplicate join attempt by some caller-identity notion | Decision 17 (flagged), **Decision 25 (decided not needed, `plan.md` TASK-24H-0136)** |
| Player-lost vs. clean-removal distinction (**resolved: decided not needed**) | **Not implemented**, deliberately - no public way to observe this distinction even internally, since there is no system-message mechanism | Real DirectPlay can report a player as "lost" (e.g. a timeout) distinctly from a clean departure | Decision 17 (flagged), **Decision 26 (decided not needed, `plan.md` TASK-24H-0137)** |
| Player data bytes / event handle / player-created / player-destroyed system messages | Not implemented. `CreatePlayer()`'s `lpData`/`dwDataSize`/`hEvent` parameters are accepted but unused | Real DirectPlay supports all of these | Decision 17 (Phase 0's audit found no `free-eggbert` dependency on any of them; left as-is in `plan.md`, not struck through, pending explicit user confirmation - this specific item was not part of the 7 resolved Track B questions) |
| Host migration (`DPSESSION_MIGRATEHOST`) (**still open - was never one of the 7 Track B questions**) | **Silently ignored.** `free-eggbert` sets this flag (and `DPSESSION_KEEPALIVE`) in `DPSESSIONDESC2::dwFlags` when hosting, but `Open()` never reads `lpSessionDesc->dwFlags` at all - confirmed by grep across `src/directplay/`, `plan.md` TASK-24H-0022. Host **migration** (electing a new host) is distinct from host **message routing** (relaying between peers, now resolved via Decision 21) - resolving Track B did not include a migration decision | Real DirectPlay would attempt to migrate hosting duties to another player if the host leaves | Not yet a numbered Decision - found via `plan.md` TASK-24H-0022, still genuinely open |
| `EnumSessions` end-of-enumeration signal (`DPESC_TIMEDOUT`) | `free-eggbert`'s `EnumSessionsCallback` checks `dwFlags & DPESC_TIMEDOUT` to detect the end of enumeration, but FreeDirect's synchronous `EnumSessions()` (Decision 18) never sets this flag when invoking the callback - the check is real but currently unreachable from FreeDirect's side. Not a bug: `EnumSessions()` still terminates normally via a plain `DP_OK` return once the loop over the registry finishes | Real DirectPlay's asynchronous enumeration invokes the callback one final time with `DPESC_TIMEDOUT` set when the caller-supplied timeout elapses | Not yet a numbered Decision - found this session (`plan.md` TASK-24H-0022) |
| Session name/password/`dwMaxPlayers` sync on join | **Not synced** to the joining side beyond `applicationGuid`/`sessionInstanceGuid` - no current consumer needs them | Real DirectPlay would give a joining caller the full session descriptor | Decision 16 |
| LAN discovery responder validation (**new, 2026-07-09**) | `DirectPlayDiscoveryService`'s raw-socket responder (Decision 23) validates a request via the shared `TryDeserializeDirectPlayWireHeader` (size, `magic`/`version` since `TASK-24H-0176`, and `payloadLength` consistency) plus its own `type == Discovery` check - but still has no authentication of any kind. It unicasts a real `DiscoveryResponse` to whatever source address the OS reports for the incoming packet, which is trivially spoofable on a local network - a third party who already knows this project's (documented, open-source) wire format can still cause this process to send a network packet to an address of its choosing. A structurally-present UDP reflection primitive, with modest amplification (the reply is roughly the size of the request, not the 10-100x seen in classic DNS/NTP reflection). Real-world stakes are low given this feature's explicit LAN-only, casual-discovery design intent (Decision 23), not an internet-facing service - no code change proposed, recorded here per this project's practice of writing down known characteristics rather than leaving them implicit | N/A - real DirectPlay's service-provider enumeration model has no direct raw-socket-broadcast analog to compare against | Decision 23 (introduced the raw-socket design), `docs/audit_dplay.md` §7.3 (D7, `plan.md` TASK-24H-0177) |

## Formerly-BLOCKED design questions - all 7 now resolved

All 7 questions below were, for most of this project's history, intentionally left undecided per
this project's standing policy of never resolving a DirectPlay network-semantics design question
without asking the user first (see `CLAUDE.md`'s Safety Rules). **All 7 were asked of, and
answered by, the user directly in one session** (never decided unilaterally) and are now recorded
as `docs/directplay-design.md` Decisions 20-26. Listed together here for visibility, exactly as
they were listed while still open, with their resolution:

1. **DPID-0 broadcast semantics** - is DPID `0` in `idTo` "broadcast" or "the real player whose ID
   is `0`"? **Resolved (Decision 20): always broadcast.** (Decisions 3, 15, 20)
2. **ENet host discovery** - how does a joining ENet `Connect()` learn a host's address?
   **Resolved (Decision 22): `FREE_DIRECT_ENET_HOST_ADDRESS` environment variable.**
   (Decisions 5, 19, 22)
3. **LAN discovery** - is any real-network session discovery ever added, and how? **Resolved
   (Decision 23): yes, real UDP broadcast `Discovery`/`DiscoveryResponse`.** (Decisions 18, 23)
4. **Host routing** - does the host ever relay a message between two non-host peers? **Resolved
   (Decision 21): yes, for broadcast relay.** (Decisions 15, 21)
5. **Player names** - should `DPNAME` ever be stored, and if so, how would it become observable
   given `IDirectPlay2A` has no getter for it? **Resolved (Decision 24): not stored.**
   (Decisions 17, 24)
6. **Duplicate-player semantics** - what would "duplicate" even mean, given collision-free DPID
   allocation? **Resolved (Decision 25): no detection added.** (Decisions 17, 25)
7. **Player-lost state** - should a distinct "lost" (vs. cleanly removed) player state exist, and
   how would it be observed? **Resolved (Decision 26): no distinct state added.**
   (Decisions 17, 26)

**Not part of this list, still genuinely open** (was never one of the 7 questions asked): host
**migration** (`DPSESSION_MIGRATEHOST`, electing a new host if the original leaves) - distinct from
host **message routing** (item 4 above, now resolved). See the deviation table's own row for this.
