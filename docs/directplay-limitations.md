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
| `DPID` `0` ambiguity (**open, not resolved**) | `free-eggbert`'s own broadcast call (`Send(m_dpid, 0, ...)`) is genuinely ambiguous under FreeDirect's current semantics: is `0` "broadcast to everyone" or "the specific player whose DPID happens to be `0`" (typically the host)? Not resolved by any Decision - broadcast itself is not implemented at all (see below) | N/A - real DirectPlay has no such ambiguity, since `0` is never a real player's ID | Decision 3 (creates the ambiguity), Decision 15 (flags it explicitly, still open) - **one of the project's standing BLOCKED design questions** |
| Wire/network compatibility | None. FreeDirect's own internal 72-byte (on this project's Linux/LP64 build) packet header, see `docs/directplay-protocol.md` | Real DirectPlay's actual service-provider/packet format | `CLAUDE.md` DirectPlay Policy |
| `EnumSessions()` | A **synchronous, in-process, loopback-only** registry lookup (`LoopbackHostedSessionRegistry`) - reads live session state directly, no packets sent, no timeout needed. An ENet-hosted session is **not discoverable** by this mechanism at all | A real network enumeration with a wire round-trip and a caller-supplied `dwTimeout` | Decision 18 |
| Session discovery over ENet / LAN (**open, not resolved**) | Not implemented. `DirectPlayWirePacketType::Discovery`/`DiscoveryResponse` are defined but never sent by anything | Real DirectPlay enumerates via its service provider, potentially over a real network | Decision 18 (loopback-only, explicitly does not cover this) - **standing BLOCKED design question ("LAN discovery")** |
| ENet host address resolution for a joining `Connect()` (**open, not resolved**) | Not implemented. The ENet backend's joining role's `Open()` branch is **completely unwired** - it never calls `Connect()` at all, for any reason, including that there is no mechanism to learn a host's address in the first place (`DPSESSIONDESC2` has no address-like field, and no discovery mechanism exists per the row above) | A real DirectPlay service provider resolves an address for the caller | Decision 5 (flags the missing address field), Decision 19 ("Explicitly out of scope... how a joining ENet call resolves a host address... flagged as a separate, still-open question since Decision 5") - **standing BLOCKED design question ("ENet host discovery")** |
| `DirectPlayEnumerateA`/`DirectPlayEnumerateW` | Still genuine stubs: return `DP_OK`, invoke the callback **zero times**, unconditionally | Would enumerate real installed service providers | Decision 1 (decided - eventually invoke the callback once for a FreeDirect-internal placeholder provider - but **not yet implemented**) |
| `Open()` join outcome | **Asynchronous.** Returns `DP_OK` immediately once the raw connection exists (or immediately attempts `Connect()` and returns based on that alone, over loopback); the actual join accept/reject outcome is discovered later by polling `Receive()`, same as ordinary messages | `Open()` blocks internally until a join-accepted/-rejected response arrives, or times out | Decision 16 |
| Join rejection reason | A rejected client only observes an ordinary disconnect, with **no explanation packet** (`JoinReject` is defined but never sent - structurally cannot be, since a rejected connection is never assigned a DPID to address it to) | A real join-rejected response, potentially with a reason | Decision 9's Caveat, reaffirmed by Decision 16 |
| GUID validation on join | **None.** `Open()`'s joining branch never compares the joining caller's `guidApplication` against the host's | Real DirectPlay would reject a session/application GUID mismatch | Decision 16 ("deliberately still out of scope") |
| Broadcast (`Send` with `idTo == 0` / `DPID_ALLPLAYERS`) (**open, not resolved**) | **Not implemented at all.** Only self-send (`idTo == idFrom`) and host-to-one-specific-assigned-remote-player unicast work | `idTo == DPID_ALLPLAYERS` (`0`) delivers to every player in the session | Decision 15 ("broadcast delivery for `idTo == 0`/`DPID_ALLPLAYERS`... explicitly out of scope, left for later") - **standing BLOCKED design question ("DPID-0 broadcast semantics")**, entangled with the DPID-0 ambiguity row above |
| Host routing / relay (**open, not resolved**) | **Not implemented.** A non-host peer can only ever reach the host (its one `hostPeer_`); the host can only reach its own directly-`connectedPeers_`. A message from one non-host peer intended for another non-host peer has no path today | Real DirectPlay's star topology relays messages between non-host peers through the host transparently | Decision 15 ("host-side routing/forwarding of a Send a non-host peer addresses to another non-host peer - star-topology relay - today only 'host directly addresses one of its own remotePlayerIds' works") - **standing BLOCKED design question ("host routing")** |
| ENet unicast `Send()`/`Receive()` | `Send()` works for the hosting role addressing one of its own `connectedPeers_` (Decision 14). `Receive()` now genuinely buffers and delivers incoming packets for **both** roles (Decision 19 - this was a real gap as recently as Decision 14, since fixed) | Both directions work symmetrically in real DirectPlay | Decisions 14, 19 |
| ENet join handshake | **Not implemented.** Decision 16's `Join`/`JoinAccept` handshake is loopback-only | N/A | Decision 16, reaffirmed by Decision 19's "explicitly out of scope" list |
| Player short/long names (`DPNAME`) (**open, not resolved**) | **Not stored at all.** `CreatePlayer()` accepts `lpPlayerName` but discards it | A real player name is stored and queryable | Decision 17 - flagged pending a user decision on how (or whether) to make stored names observable, since `IDirectPlay2A` has no `GetPlayerName`-style method at all - **standing BLOCKED design question ("player names")** |
| Duplicate-player detection (**open, not resolved**) | **Not implemented** - no concrete definition of "duplicate" exists given DPID allocation is collision-free by construction and `free-eggbert` only ever calls `CreatePlayer()` once | Real DirectPlay may reject a duplicate join attempt by some caller-identity notion | Decision 17 - **standing BLOCKED design question ("duplicate-player semantics")** |
| Player-lost vs. clean-removal distinction (**open, not resolved**) | **Not implemented** - no public way to observe this distinction even internally, since there is no system-message mechanism | Real DirectPlay can report a player as "lost" (e.g. a timeout) distinctly from a clean departure | Decision 17 - **standing BLOCKED design question ("player-lost state")** |
| Player data bytes / event handle / player-created / player-destroyed system messages | Not implemented. `CreatePlayer()`'s `lpData`/`dwDataSize`/`hEvent` parameters are accepted but unused | Real DirectPlay supports all of these | Decision 17 (Phase 0's audit found no `free-eggbert` dependency on any of them; left as-is in `plan.md`, not struck through, pending explicit user confirmation) |
| Host migration (`DPSESSION_MIGRATEHOST`) | **Silently ignored.** `free-eggbert` sets this flag (and `DPSESSION_KEEPALIVE`) in `DPSESSIONDESC2::dwFlags` when hosting, but `Open()` never reads `lpSessionDesc->dwFlags` at all - confirmed by grep across `src/directplay/`, `plan.md` TASK-24H-0022 | Real DirectPlay would attempt to migrate hosting duties to another player if the host leaves | Not yet a numbered Decision - found this session (`plan.md` TASK-24H-0022); consistent with host routing/player-lost-state being open BLOCKED questions above, not a newly-discovered contradiction |
| `EnumSessions` end-of-enumeration signal (`DPESC_TIMEDOUT`) | `free-eggbert`'s `EnumSessionsCallback` checks `dwFlags & DPESC_TIMEDOUT` to detect the end of enumeration, but FreeDirect's synchronous `EnumSessions()` (Decision 18) never sets this flag when invoking the callback - the check is real but currently unreachable from FreeDirect's side. Not a bug: `EnumSessions()` still terminates normally via a plain `DP_OK` return once the loop over the registry finishes | Real DirectPlay's asynchronous enumeration invokes the callback one final time with `DPESC_TIMEDOUT` set when the caller-supplied timeout elapses | Not yet a numbered Decision - found this session (`plan.md` TASK-24H-0022) |
| Session name/password/`dwMaxPlayers` sync on join | **Not synced** to the joining side beyond `applicationGuid`/`sessionInstanceGuid` - no current consumer needs them | Real DirectPlay would give a joining caller the full session descriptor | Decision 16 |

## Standing BLOCKED design questions

Seven questions are intentionally left undecided, per this project's standing policy of never
resolving a DirectPlay network-semantics design question without asking the user first (see
`CLAUDE.md`'s Safety Rules). Each is cited in the deviation table above; listed together here for
visibility:

1. **DPID-0 broadcast semantics** - is DPID `0` in `idTo` "broadcast" or "the real player whose ID
   is `0`"? (Decisions 3, 15)
2. **ENet host discovery** - how does a joining ENet `Connect()` learn a host's address? (Decisions
   5, 19)
3. **LAN discovery** - is any real-network session discovery ever added, and how? (Decision 18)
4. **Host routing** - does the host ever relay a message between two non-host peers? (Decision 15)
5. **Player names** - should `DPNAME` ever be stored, and if so, how would it become observable
   given `IDirectPlay2A` has no getter for it? (Decision 17)
6. **Duplicate-player semantics** - what would "duplicate" even mean, given collision-free DPID
   allocation? (Decision 17)
7. **Player-lost state** - should a distinct "lost" (vs. cleanly removed) player state exist, and
   how would it be observed? (Decision 17)

None of these are resolved by this document. Resolving any of them requires a direct decision from
the user, per `CLAUDE.md`.
