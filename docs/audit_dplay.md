# DirectPlay subsystem audit

This is a from-scratch, evidence-based audit of FreeDirect's DirectPlay implementation
(`src/directplay/*.{cpp,hpp}`, ~2450 lines across 9 source files; declared in `include/dplay.h`,
308 lines). It covers performance, memory safety, correctness, code quality, edge cases under
extreme situations, and a risk-analysis synthesis, ending with a proposed-tasks list, matching
`docs/audit_ddraw.md`/`docs/audit_dsound.md`'s methodology: every finding cites exact file:line
evidence, is graded on Impact × Reachability, and empirical claims are measured against the real
compiled library, not just reasoned about.

This document is **read-only analysis**. No code was changed to produce it (`CLAUDE.md` Safety
Rules).

## 1. Scope and methodology — DirectPlay is different from the other two audits

DirectPlay is, by a wide margin, FreeDirect's most heavily audited and most heavily decided
subsystem already: 26 numbered design Decisions (`docs/directplay-design.md`, 2068 lines), a
22-row deviation table (`docs/directplay-limitations.md`), a wire-format spec
(`docs/directplay-protocol.md`), a backend-tradeoffs doc (`docs/networking-backends.md`), and a
372-line call-site audit (`docs/directplay-callsite-audit.md`). All 26 Decisions are resolved — none
are open, and this audit did not re-litigate any of them. A background research pass digested all
five documents before this audit began, specifically so nothing below duplicates settled ground;
every finding in Sections 4-8 is either genuinely new or explicitly marked as building on a named,
existing Decision/finding.

Given that depth of prior audit, this document's new findings are necessarily narrower and more
specific than `docs/audit_ddraw.md`'s (which had no prior audit at all) — but Section 4 below
surfaces one framing fact that changes how every other finding in this document should be read.

- **Primary subject**: `DirectPlay.cpp` (832 lines, the core `IDirectPlay2A` implementation),
  `DirectPlaySession.hpp`, `DirectPlayMessageQueue.hpp`, `DirectPlayWireProtocol.hpp`,
  `DirectPlayTransport.hpp` (the abstract interface), `LoopbackDirectPlayTransport.{hpp,cpp}`,
  `EnetDirectPlayTransport.{hpp,cpp}`, `DirectPlayDiscovery.{hpp,cpp}`, `DirectPlayPlayer.{hpp,cpp}`,
  and `include/dplay.h`.
- **Not in scope**: DirectDraw and DirectSound (see `docs/audit_ddraw.md`/`docs/audit_dsound.md`),
  per `CLAUDE.md`'s atomicity rule.
- **`planetblupi` places zero requirements on this subsystem** — confirmed zero DirectPlay usage by
  grep, restated here only for completeness; all call-site evidence below is `../free-eggbert`-only.

## 2. Executive summary

| # | Finding | Impact | Reachable today? | Section |
|---|---|---|---|---|
| D1 | **Framing fact, not a defect**: `CDecor::TreatNetData()` — the per-frame packet pump that would drive `Send()`/`Receive()` during an active session — has its one call site commented out (`event.cpp:2045`) | N/A | This is *why* everything below is unreachable *today* — but free-eggbert's decompilation is ongoing, not finished, and the user has confirmed DirectPlay will actually be used once it completes. Not a finding to fix, but not a reason to deprioritize the rest of this table either | 4 |
| D2 | `Send()`'s self-send path reads `dwDataSize` bytes from `lpData` *before* checking it against `kMaxPayloadBytes`, unlike the broadcast/unicast paths, which check first | High if triggered — an out-of-bounds read if `dwDataSize` lies about the buffer's real size | **No** — free-eggbert's one real `Send()` call site always passes `idTo=0` (broadcast), never reaching self-send; the existing oversized-self-send test uses an honestly-sized buffer, so it doesn't exercise this either | 5.1 |
| D3 | `include/dplay.h`'s top-of-file comment ("Broadcast delivery... does not work correctly yet") contradicts the `Send()` method's own doc comment 270 lines below it (broadcast is real) | Low — stale documentation, not a behavior bug | N/A | 6.1 |
| D4 | `docs/networking-backends.md` claims ENet joining/discovery "does not work today," contradicted by Decisions 22/23 (already implemented) | Low — stale documentation | N/A | 6.2 |
| D5 | `DirectPlayPlayer` (`.hpp`+`.cpp`) is confirmed dead scaffolding — zero members, zero usage, superseded early on by `DirectPlaySession`'s simpler design | Low — dead code, no functional risk | N/A | 6.3 |
| D6 | Wire header `magic`/`version` fields are deliberately unvalidated on receive, deferred "until a real transport exists" — that transport (ENet) now exists and receives real UDP traffic | Low-Medium — weak protocol-identification once a real socket is involved | Structurally yes for the ENet backend (opt-in, off by default) — but see D1 | 6.4, 7.2 |
| D7 | `DirectPlayDiscoveryService`'s raw-socket responder validates only packet size/type (no magic/version, no auth) and unicasts a real reply to whatever source address a request claims | Low — a structurally-present UDP reflection primitive, modest amplification, LAN-only intended scope | Structurally yes for ENet+LAN-discovery builds — but see D1 | 7.3 |
| D8 | `EnetDirectPlayTransport::Service()` and `DirectPlayDiscoveryService::RespondToPendingRequests()` both have unbounded drain loops with no per-call iteration cap | Low-Medium — could stall the calling thread under a high incoming-packet rate | Structurally yes for ENet builds — but see D1 | 5.4, 7.4 |
| D9 | `EnetDirectPlayTransport::Shutdown()` can block the calling thread up to ~1 second (10 × 100ms) waiting for peer disconnect acknowledgement | Medium — a real, deliberate-but-generous synchronous wait on every `Close()` with connected ENet peers | Structurally yes for ENet builds — but see D1 | 5.3 |
| D10 | `Receive()` allocates a fresh ~4.1KB `wireBuf` vector every call, even when nothing is pending | Negligible — measured at 228ns/call | Yes in principle, but see D1; negligible even if it were | 5.2 |

**The single most important fact in this document is D1.** Every other finding above that says
"reachable today" or "structurally yes" is reachable *only* in the sense that the code path exists
and would execute if a session were open and its packet pump ran — but Section 4 establishes that
free-eggbert's own multiplayer packet pump (`CDecor::TreatNetData()`) is never actually invoked
anywhere in the game's current reconstructed source. This sharpens (does not contradict) the
already-established fact from earlier work this project did this session: not only can a player
never *open* a network session through free-eggbert's UI, the gameplay-loop mechanics that would
*drive* an already-open session don't run either. Every runtime-behavior finding below (D2, D6-D10)
is therefore confirmed unreachable by any current free-eggbert code path today, full stop — not
merely "unreachable via specific menu screens."

**This unreachability is temporary, not permanent, and must not be read as license to defer these
findings indefinitely.** `../free-eggbert`'s source is an active, ongoing decompilation/
reconstruction effort, not a finished, frozen codebase — confirmed directly by the user: DirectPlay
*will* be used by free-eggbert once that decompilation effort is complete. The commented-out
`TreatNetData()` call and the empty `WM_PHASE_DP_*` handlers are gaps expected to close as that work
progresses, unlike, say, `DirectPlayPlayer`'s dead scaffolding (D5), which is unreachable because it
was architecturally superseded inside FreeDirect itself and has no relationship to free-eggbert's
decompilation status at all. Every "reachable today: No (but see D1)" verdict in this document
should be read as "not yet, pending decompilation completion," not as "structurally irrelevant."

## 3. What's already solid (checked, not just assumed)

- **Ref-counting is correct across every class** (`DirectPlay2AImpl`, `DirectPlayImpl`) — spot
  checked the same way as the other two audits, no unpaired increment/decrement found.
- **`LoopbackDirectPlayTransport`'s cross-object raw-pointer teardown is careful and correct.**
  Hosting and joining instances hold raw pointers into each other's private state by design
  (`hostPeer_`, `connectedPeers_`, `pendingPeers_`) — a real dangling-pointer hazard if handled
  carelessly. `Shutdown()` (`LoopbackDirectPlayTransport.cpp:128-174`) scrubs every known peer's
  back-reference in *both* directions before clearing its own state: a shutting-down host nulls out
  every connected/pending peer's `hostPeer_`; a shutting-down client removes itself from its host's
  `connectedPeers_`/`pendingPeers_` before clearing its own `hostPeer_`. `Shutdown()` is also
  idempotent (safe to call twice, including via the destructor always calling it once more). This
  is a non-trivial thing to get right and this audit found no gap in it.
- **`ParseEnetHostAddressEnvVar`'s input validation is careful** (`DirectPlay.cpp:61-85`) — rejects
  non-numeric ports outright rather than letting `strtol` parse a garbage prefix, checks the parsed
  port range, handles the no-colon/empty-host/empty-port cases explicitly. No gap found.
  `TryDeserializeDiscoveryResponsePayload`/`TryDeserializeDirectPlayWireHeader` are similarly
  careful about not reading past a caller-supplied `dataSize`, with no underflow risk in either's
  size-difference arithmetic (both guard the subtraction with a prior `<` check).
  `TryDeserializeDiscoveryResponsePayload`'s `nameLength` is transitively bounded by the 512-byte
  stack receive buffers both discovery call sites use, so no overflow risk there either.
  `Send()`'s null-`lpData`-with-nonzero-size UB (a real bug, already found and fixed in an earlier
  session, `TASK-24H-0106`/`0107`) and `DirectPlayMessageQueue`'s zero-length-message `memcpy` UB
  (already found and fixed via an ASan/UBSan sweep) both remain fixed — re-confirmed by this audit's
  fresh reading, not re-derived as new findings.
- **`Receive()`'s per-call cost is negligible** (Section 5.2) — measured directly rather than
  assumed, matching this session's established practice of not asserting a performance finding
  without a number behind it.
- **`EnumSessions()`'s loopback registry lookup and ENet discovery collection are both real,
  bounded, and correctly filtered** by `guidApplication`/`DPENUMSESSIONS_AVAILABLE`, with
  session-instance deduplication on the ENet path — no new gap found beyond what
  `docs/directplay-design.md` Decisions 18/23 already established.

## 4. The framing fact: `CDecor::TreatNetData()` is dead code

`docs/directplay-callsite-audit.md` already established that free-eggbert's DirectPlay-triggering
UI functions (`JoinSession`, `CreateSession`, `NetCreate`, `NetEnumSessions`, `NetStartPlay`) have
zero callers, and that the `WM_PHASE_DP_*` menu-phase state-machine handlers are empty fallthroughs.
This audit traced one level deeper, into the gameplay-loop side of the same question: **what
actually calls `Send()`/`Receive()` once (hypothetically) a session is open?**

```cpp
// free-eggbert/src/decnet.cpp:87
void CDecor::TreatNetData()
{
    ...
    m_pNetwork->Send(&pack, sizeof(NetMessage) * pack.nbMessages + 20, flag);
    ...
    for (i = 0; i < 10; i++)
    {
        if (!m_pNetwork->Receive(&pack, sizeof(pack), (LPDWORD)&player))
        {
            break;
        }
        ...
```

`CDecor::TreatNetData()` is the per-frame packet pump — it sends the current frame's outgoing
network packet and drains up to 10 incoming ones. Searching the entire `../free-eggbert/src/` tree
for its one call site finds exactly one match, commented out:

```cpp
// free-eggbert/src/event.cpp:2045
    //m_pDecor->TreatNetData();
```

`network.cpp:254` (`m_pDP->Send(m_dpid, 0, !!dwFlags, lpData, dwDataSize)`) and `network.cpp:269`
(`m_pDP->Receive(&from, &to, DPRECEIVE_ALL, dataBuffer, &dataSize)`) are the *only* two places in
free-eggbert's entire source that call the real `IDirectPlay2A::Send`/`Receive` interface methods
directly — both are inside `CNetwork`'s own wrapper implementations, and both are reached
exclusively through `CDecor::TreatNetData()`. With that one call site commented out, neither is
ever invoked by any currently-running code path in the game.

**This does not change any of `docs/directplay-limitations.md`'s or `docs/directplay-design.md`'s
conclusions** — it adds one more, more specific data point to a fact this project's own earlier work
this session already established (per the conversation record: "FreeDirect's DirectPlay layer is
technically ready for what `CNetwork` calls, but free-eggbert's own UI has no reachable code path to
actually invoke multiplayer hosting/joining"). What's new here: it's not just the UI's session-open
path that's unreachable — the gameplay-loop packet pump that would exercise an already-open
session's `Send()`/`Receive()` is structurally disabled in the source too, not merely gated behind
unreachable menu state. Per `CLAUDE.md`, this is a free-eggbert source-completeness fact, not
something this project modifies game source to work around — and, per the user's own confirmation,
a fact expected to change as free-eggbert's decompilation continues, not a permanent one. FreeDirect
should keep treating this DirectPlay surface as a real, near-term-live target, not as a corner it
can afford to leave soft indefinitely.

One concrete consequence worth naming: real `dwDataSize` values observed at free-eggbert's other
`CNetwork::Send`-wrapper call sites (`decnet.cpp:83`, `event.cpp:2159,2176,2212,2247,2281,4708`) are
all small — `4`, `sizeof(NetMessage)*pack.nbMessages+20`, `3`, `132`, `2`, `4`, `108`, `28` bytes —
comfortably inside `kMaxPayloadBytes` (4096). These values are real evidence of what the game
*would* send if `TreatNetData()` ever ran, useful context for Section 7's discussion, even though
none of them currently execute.

## 5. Performance analysis

### 5.1 `Send()`'s self-send path: correctness issue with a performance-adjacent shape (see 6.5 for full writeup)

Covered fully in Section 6.5 as a correctness finding (it's fundamentally a missing bounds check,
not a performance defect) — cross-referenced here because its shape (`.assign()` before validation)
is the same "do the expensive/dangerous thing before checking if you should" pattern this section's
other findings share.

### 5.2 `Receive()`'s per-call allocation — measured negligible

```cpp
// DirectPlay.cpp:613-616
constexpr std::size_t kMaxWireBufferSize =
    kDirectPlayWireHeaderSize + DirectPlayMessageQueue::kMaxPayloadBytes;
std::vector<std::uint8_t> wireBuf(kMaxWireBufferSize);
```

Every `Receive()` call value-initializes a fresh ~4.1KB buffer (72-byte header + 4096-byte max
payload), even when the transport has nothing pending — a heap allocation plus a zero-fill on every
call, unlike DirectDraw's `PresentPrimary` (`docs/audit_ddraw.md`), which already learned to reuse
a persistent conversion buffer instead of allocating fresh each frame.

**Measured** (Section 8.1): **228.7ns/call** (100,000 calls, `Receive()` with nothing pending,
default/unoptimized build). At any realistic call frequency this is negligible — even the now-dead
`TreatNetData()`'s own `for (i = 0; i < 10; i++)` loop, at 60fps, would cost `10 × 228.7ns ≈ 2.3µs`
per frame, about 0.0000137% of a 16.67ms frame budget. Recorded as a genuine, honest negative
result, and as a minor code-consistency opportunity (a persistent member buffer, matching the
pattern already used elsewhere in this project) rather than a performance finding worth prioritizing.

### 5.3 `EnetDirectPlayTransport::Shutdown()`'s bounded disconnect wait

```cpp
// EnetDirectPlayTransport.cpp:268-276
constexpr int kDisconnectPollAttempts = 10;
constexpr unsigned kDisconnectPollTimeoutMs = 100;
std::size_t stillPending = toDisconnect.size();
for (int i = 0; i < kDisconnectPollAttempts && stillPending > 0; ++i) {
    ENetEvent event;
    if (enet_host_service(host_, &event, kDisconnectPollTimeoutMs) <= 0) continue;
    ...
}
```

`Shutdown()` (called from `Close()` and the destructor) gracefully disconnects every known peer,
then waits — synchronously, on the calling thread — for disconnect acknowledgement, up to 10 × 100ms
= **1 second worst case**, before tearing the host down. This is a real, deliberate, already-
documented-as-"generous-but-small" design choice (`EnetDirectPlayTransport.cpp:263-267`'s own
comment), not a bug — but it means every `Close()` call with connected ENet peers can synchronously
stall the calling thread for up to a second. Not empirically measured in this audit (would need a
real two-peer ENet setup with a deliberately unresponsive peer to hit the worst case meaningfully);
recorded as a code-reasoned finding, clearly labeled as such. Given D1, this cannot currently
trigger via free-eggbert's own code regardless.

### 5.4 Unbounded drain loops (see 7.4 for the full writeup)

`EnetDirectPlayTransport::Service()` and `DirectPlayDiscoveryService::RespondToPendingRequests()`
both drain "everything currently pending" with no iteration cap. Covered fully as a correctness/
robustness finding in Section 7.4, since the interesting risk is unbounded work under adversarial
input volume, not routine-case throughput.

## 6. Correctness, code quality, and documentation accuracy

### 6.1 `include/dplay.h`: a stale top-of-file comment contradicts the `Send()` method doc directly below it

```cpp
// include/dplay.h:9-11 (file-level comment)
 * `DirectPlayEnumerateA`/`DirectPlayEnumerateW` remain genuine stubs
 * (Decision 1: decided, not yet implemented). Broadcast delivery (`Send` with `idTo == 0`) does
 * not work correctly yet - see `Send`'s own doc comment below and
 * `docs/audit-24h-free-direct.md`.
```

```cpp
// include/dplay.h:281-282 (Send() method's own doc comment, same file)
    /** @brief Sends a packet. Real for self-send, host-to-one-assigned-remote-player unicast, and
     broadcast (idTo == DPID_ALLPLAYERS, delivered to every other player, with host-side relay ...
```

These two comments, in the same file, 270 lines apart, say opposite things about broadcast. The
`Send()` method's own comment is correct and current (broadcast was implemented via Decisions
20/21, `TASK-24H-0148`, confirmed by this audit's own fresh reading of `DirectPlay.cpp:389-437`)
— the file-level paragraph simply was never updated afterward. A reader who only skims the
top-of-file summary (a reasonable thing to do) gets actively wrong information about the very
method whose own doc comment, in the same file, says otherwise.

### 6.2 `docs/networking-backends.md` is stale about ENet joining/discovery (found via the pre-audit digest, independently confirmed)

This doc's Backend-2 (ENet) section states the joining role's `Open()` branch never calls
`Connect()` and that ENet join/discovery "does not work today." Confirmed by direct reading of
`DirectPlay.cpp:284-308` that this is no longer true — `Connect()` is called, wired to
`FREE_DIRECT_ENET_HOST_ADDRESS` (Decision 22), and LAN discovery is real (Decision 23,
`DirectPlayDiscoveryService`). This doc was not updated after those two Decisions landed.

### 6.3 `DirectPlayPlayer` is confirmed dead scaffolding

```cpp
// DirectPlayPlayer.hpp:15-23 (the entire class)
class DirectPlayPlayer {
public:
    DirectPlayPlayer() = default;
    ~DirectPlayPlayer() = default;
};
```

Zero members, zero methods beyond default construction, and the file's own header comment already
says "It is not yet used by `DirectPlay.cpp`." Player state lives on `DirectPlaySession` instead, as
plain `std::vector<DPID> localPlayerIds`/`remotePlayerIds` — a simpler design that was evidently
adopted early and this class was never removed. Confirmed still true, still unused, by this audit's
fresh reading (`DirectPlayPlayer` does not appear anywhere in `DirectPlay.cpp`,
`DirectPlaySession.hpp`, or any transport file).

### 6.4 Wire header `magic`/`version` validation: a deferred decision whose own precondition has now been met

```cpp
// DirectPlayWireProtocol.hpp:10-16
 * This header defines the packet *header* fields, their flat (de)serialization, and
 * defensive size validation for an untrusted receive buffer
 * (`TryDeserializeDirectPlayWireHeader`). That validation deliberately does not check
 * `magic`/`version` - a wrong-protocol/wrong-version packet is a different rejection
 * reason than a malformed/truncated buffer, with its own `DPERR_*` mapping to be decided
 * once a real transport (`EnetDirectPlayTransport`, still to be added in this same
 * phase) actually receives packets.
```

This is an honest, already-documented, deliberate deferral — not a silent gap, and not a new
finding in the sense of "an oversight nobody noticed." What *is* new here: this comment was written
when `EnetDirectPlayTransport` was still hypothetical ("still to be added in this same phase"). It
now exists (Phase 5, and Decisions 19/22/23 since), receives real UDP packets, and
`TryDeserializeDirectPlayWireHeader` is exactly what both `DirectPlay2AImpl::Receive()`
(`DirectPlay.cpp:619`) and `DirectPlayDiscoveryService::RespondToPendingRequests()`/
`BroadcastAndCollect()` (`DirectPlayDiscovery.cpp:116,204`) call on every real received packet.
The comment's own precondition for revisiting this ("once a real transport actually receives
packets") has been satisfied for a while. Worth an explicit decision either way — keep deferring
with a fresh, current rationale, or add the check — rather than leaving a Phase-5-era comment as
the last word on a question whose facts have since changed.

### 6.5 `Send()`'s self-send path reads before validating size (new finding, most significant in this audit)

```cpp
// DirectPlay.cpp:439-468 (self-send branch, idTo == idFrom)
if (idTo == idFrom) {
    if (std::find(...) == session_.localPlayerIds.end()) return DPERR_INVALIDPLAYER;
    const auto* bytes = static_cast<const std::uint8_t*>(lpData);
    free_direct_directplay::DirectPlayMessagePacket packet;
    packet.idFrom = idFrom;
    packet.idTo = idTo;
    packet.flags = dwFlags;
    packet.payload.assign(bytes, bytes + dwDataSize);          // <-- reads dwDataSize bytes here
    if (!session_.messageQueue.Enqueue(std::move(packet))) return DPERR_SENDTOOBIG;  // <-- checked here
    return DP_OK;
}
```

Compare the broadcast path, five lines above it in the same function:

```cpp
// DirectPlay.cpp:400-402 (broadcast branch)
if (dwDataSize > free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes) {
    return DPERR_SENDTOOBIG;                                    // <-- checked BEFORE touching lpData
}
```

and the unicast path (`DirectPlay.cpp:492-494`), which has the identical pre-check. The self-send
path is the only one of the three that reads `lpData` before validating `dwDataSize` against
anything. `packet.payload.assign(bytes, bytes + dwDataSize)` reads exactly `dwDataSize` bytes
starting at `lpData` — if a caller's `dwDataSize` overstates the real size of the buffer at
`lpData` (a caller bug, not necessarily malice — this project's threat model is "internal, not
adversarial" callers, but caller bugs are exactly the class of thing a size-bound check like the
other two paths' exists to catch), this is an out-of-bounds read that happens *before* `Enqueue()`
gets a chance to reject the packet for being oversized. `Enqueue()`'s own check
(`DirectPlayMessageQueue.hpp:62`, `if (packet.payload.size() > kMaxPayloadBytes) return false;`)
only runs after the read has already occurred — by then, any damage (a crash reading unmapped
memory, or transiently copying adjacent heap contents into a `packet.payload` that gets discarded a
moment later) is already done.

**Reachability, checked precisely, not assumed:**
- Not reachable via free-eggbert's real traffic: its one real `Send()` call site
  (`network.cpp:254`) always passes `idTo = 0` (`DPID_ALLPLAYERS`), and `Send()` checks
  `idTo == DPID_ALLPLAYERS` *before* the self-send check (`idTo == idFrom`) — every real call takes
  the broadcast branch, which has the protective pre-check. The self-send branch is structurally
  unreachable by free-eggbert's actual call shape regardless of `idFrom`. (D1 also makes this whole
  function unreachable today regardless.)
- Not caught by the existing test suite either: `Test_SelfSend_OversizedPayload_ReturnsSendTooBig`
  (`tests/directplay_tests.cpp:1731-1755`) does exercise the self-send-oversized-payload outcome,
  but with `std::vector<char> oversized(kMaxPayloadBytes + 1, 'x')` — a buffer whose real size
  exactly matches the `dwDataSize` passed. That's an honest, correctly-sized "just too big" case; it
  proves the *eventual* rejection works, but never exercises a `dwDataSize` that lies about the
  buffer's real size, so it can't catch this specific ordering issue.

This is a real, self-contained, precisely-located inconsistency between one of `Send()`'s three
delivery paths and the other two — not a hypothetical. The fix is small: move the existing
`dwDataSize > kMaxPayloadBytes` check (already written twice elsewhere in the same function) to run
before the self-send path's `.assign()` call too.

## 7. Edge cases under extreme situations and risk analysis

Per this audit's requested structure, this section collects extreme-input and extreme-load
scenarios specifically, including some already covered above (cross-referenced, not repeated) and
some new to this section.

### 7.1 Oversized/malformed payloads — see 6.5

The one genuinely exploitable-shaped edge case (a `dwDataSize` that lies about its buffer) is
covered in full in Section 6.5. Every *other* size-boundary case this audit checked (broadcast,
unicast, the wire header's `payloadLength`-vs-actual-received-size cross-check) validates correctly
before touching untrusted length-bearing data.

### 7.2 Malformed/foreign wire traffic on the ENet port — see 6.4

Covered in Section 6.4. Risk-analysis framing: since the main ENet data channel goes through
`ENetHost`/`ENetPeer` (which requires a real ENet protocol handshake before any application data is
delivered), genuinely random UDP noise arriving at the hosting port is filtered by ENet's own
protocol layer before it ever reaches `TryDeserializeDirectPlayWireHeader`. The weaker exposure is
the discovery service (7.3), which deliberately bypasses `ENetHost`/`ENetPeer` for a raw socket.

### 7.3 The LAN discovery responder as a UDP reflection primitive (new finding)

```cpp
// DirectPlayDiscovery.cpp:108-139 (RespondToPendingRequests, abbreviated)
for (;;) {
    ...
    const int received = enet_socket_receive(socket_, &fromAddress, &recvBuffer, 1);
    if (received <= 0) break;
    const auto header = TryDeserializeDirectPlayWireHeader(buf, received);
    if (!header || header->type != DirectPlayWirePacketType::Discovery) continue;
    if (!IsWildcardOrMatchingGuid(header->applicationGuid, info.applicationGuid)) continue;
    ...
    enet_socket_send(socket_, &fromAddress, &sendBuffer, 1);   // unicast reply to the claimed sender
}
```

`DirectPlayDiscoveryService` (Decision 23, an explicit, user-approved scope exception for real LAN
discovery) deliberately uses a raw `ENetSocket` datagram socket, not `ENetHost`/`ENetPeer` — by
design (`DirectPlayDiscovery.hpp:10-16`'s own rationale: connection-oriented ENet is "a poor
semantic match for a stateless 'is anyone here' announce/reply exchange"). That design choice also
means this specific code path has none of `ENetHost`'s protocol-level gating: any UDP datagram
arriving at the fixed, well-known port 51323 that happens to be at least `kDirectPlayWireHeaderSize`
bytes, has an internally-consistent `payloadLength`, and has `type == Discovery` gets a real,
unicast `DiscoveryResponse` sent back to whatever source address the OS reports for the incoming
packet (UDP source addresses are trivially spoofable on a local network, where the ingress
filtering that often blocks this on the wider internet typically does not apply). That is a
structurally-present **UDP reflection primitive**: a third party can cause this process to send a
network packet to an address of the sender's choosing, without any reply routing back to the actual
sender. Amplification is modest (the `DiscoveryResponse` is roughly the same size as the
`Discovery` request, not the 10-100x seen in classic DNS/NTP reflection attacks), and the feature's
explicit design intent is LAN-only casual game discovery, not an internet-facing service — so
real-world stakes here are low. Still, this characteristic is not written down anywhere today, and
this project's own documentation practice (`CLAUDE.md`'s Documentation Policy) is to record known
deviations/characteristics explicitly rather than leave them implicit, which is exactly the
practice this finding follows.

### 7.4 Unbounded drain loops under high incoming-packet volume (new finding)

```cpp
// EnetDirectPlayTransport.cpp:165 (Service())
while (enet_host_service(host_, &event, 0) > 0) { ... }
```
```cpp
// DirectPlayDiscovery.cpp:108 (RespondToPendingRequests())
for (;;) { ... if (received <= 0) break; ... }
```

Both loops drain "everything currently available" with no cap on iteration count. Under normal
gameplay traffic (a handful of small packets per frame, per Section 4's real observed payload
sizes) this is exactly the right behavior — bounded by how much data is actually waiting, which is
normally small. Under a sufficiently high incoming-packet rate (a flood, whether malicious or from
a misbehaving/looping peer), both loops would keep processing for as long as data keeps arriving
faster than the loop drains it, with no per-call bound on how long `Service()`/
`RespondToPendingRequests()` — and therefore `Receive()`, since it calls `Service()` first — can
take. This is a real, structurally-present robustness gap on the one part of this codebase that is
genuinely exposed to arbitrary network input volume (as opposed to caller-supplied API misuse,
which is this audit's other correctness findings' threat model). Not empirically measured (would
require a real packet-flood test harness); recorded as a code-reasoned structural finding, clearly
labeled as such, not an observed incident.

### 7.5 Summary table

| Scenario | Result | Evidence |
|---|---|---|
| `dwDataSize` overstating the real buffer size, self-send path | **Real gap** — OOB read before the size check | D2 / §6.5 |
| Malformed/foreign UDP traffic on the main ENet data port | **Filtered by ENet's own protocol layer** before reaching this project's code | §7.2 |
| Malformed/spoofed UDP traffic on the discovery port | **Weak filter** (size+type only) — reflection primitive, low real-world stakes given LAN-only scope | D7 / §7.3 |
| High incoming-packet-rate flood (ENet data or discovery) | **Unbounded drain loop**, no cap | D8 / §7.4 |
| `Close()`/destructor with connected ENet peers | **Real, bounded (~1s worst case) blocking wait** — deliberate, documented as generous-but-small | D9 / §5.3 |
| `Receive()` called at high frequency with nothing pending | **Checked, negligible** (228ns/call measured) | D10 / §5.2 |
| Everything above, against free-eggbert's actual running code | **Confirmed unreachable** — the packet pump that would drive any of it is commented out | D1 / §4 |

## 8. Empirical benchmarks

Compiled and linked against this project's real, compiled `libfree-direct.a` (loopback backend, the
default build — no ENet needed for these two measurements), following the same methodology as
`docs/audit_ddraw.md`/`docs/audit_dsound.md`: real public API calls only, `std::chrono::steady_clock`,
single-threaded. Full benchmark source written to the session scratchpad (`bench_dplay.cpp`), not
committed to the repository, matching prior precedent.

### 8.1 `Receive()` with nothing pending

100,000 calls, no message ever queued:

| Call | Result |
|---|---|
| `Receive()` (nothing pending) | **0.000229 ms/call** (228.7 ns/call; 22.874ms total) |

### 8.2 One self-sent small message per `Send()`+`Receive()` cycle

20,000 cycles, each sending a 16-byte payload to a real, non-zero local player DPID and immediately
receiving it back:

| Call | Result |
|---|---|
| `Send()` + `Receive()` round trip | **0.00050 ms/cycle** (9.965ms total) |

Both numbers are against this project's current default (unoptimized) build, per
`docs/audit_ddraw.md` §3.1's already-tracked `TASK-24H-0151` finding — not re-measured against a
`-DCMAKE_BUILD_TYPE=Release` build here, since both figures are already negligible at any realistic
call frequency and a 6.6x-style multiplier (the DirectDraw finding's own measured ratio) would not
change that conclusion.

## 9. Proposed tasks

Priority reflects Impact × Reachability per Section 2 — **with Reachability read as "not yet, per
Section 4's decompilation-in-progress caveat," not "structurally irrelevant."** None of these are
live risks against free-eggbert's actual current code *today*, but that code is a work in progress,
not a finished target, so these are real, near-term-relevant fixes, not indefinitely-deferrable
cleanup.

1. **Fix `Send()`'s self-send path to validate `dwDataSize` before reading `lpData`** (D2, §6.5).
   Highest priority in this audit — move the existing `kMaxPayloadBytes` check up, matching the
   other two paths. Small, self-contained, and closes this audit's only genuinely novel correctness
   gap. Add a test with a `dwDataSize` that overstates a real, smaller buffer's actual size
   (distinct from the existing honestly-sized oversized-payload test).
2. **Decide (and record) whether wire-header `magic`/`version` validation should be added now that
   ENet is real** (D6, §6.4) — real protocol-robustness gap that will matter for actual peer traffic
   once free-eggbert's decompilation reconnects `TreatNetData()`; a small decision-plus-
   implementation task if the answer is "add it," or a one-line comment update citing this audit if
   the answer is "still defer, here's why."
3. **Add an iteration cap to `Service()`'s and `RespondToPendingRequests()`'s drain loops** (D8,
   §7.4) — the other genuine robustness gap that only matters once real network traffic actually
   flows; bound the per-call work, returning "more still pending" state to be drained on a
   subsequent call rather than looping unboundedly in one call.
4. **Fix `include/dplay.h`'s stale top-of-file broadcast comment** (D3, §6.1) — documentation-only,
   trivial, but actively misleading to a reader who trusts the file-level summary over the specific
   method doc below it, and increasingly likely to be read by someone actually wiring up
   `TreatNetData()` again.
5. **Update `docs/networking-backends.md`'s ENet section** to reflect Decisions 22/23 (D4, §6.2) —
   documentation-only.
6. **Document the discovery responder's reflection-primitive characteristic** in
   `docs/directplay-limitations.md` (D7, §7.3) — documentation-only; add a bound on response rate
   only if this is ever exposed beyond a trusted LAN, which is not this project's current scope.
7. **Remove `DirectPlayPlayer.hpp`/`.cpp`** (D5, §6.3), or, if there's a near-term reason to keep
   the scaffolding, add a comment explaining why it's being kept despite being unused — matching
   this project's stated preference for not accumulating unexplained unused surface. Unlike items
   1-3, this one's priority is genuinely unaffected by decompilation progress (§4's caveat doesn't
   apply — it's dead because of an internal FreeDirect design change, not free-eggbert's state).
8. **Consider a persistent `wireBuf` member for `Receive()`** instead of a fresh per-call allocation
   (D10, §5.2) — lowest priority in this list; empirically negligible even under realistic call
   frequency, proposed purely for consistency with this project's established buffer-reuse pattern
   elsewhere, not for a measured performance need.

Not proposed as a task: `EnetDirectPlayTransport::Shutdown()`'s bounded wait (D9, §5.3) is already
a deliberate, documented, reasonable design choice (generous-but-small, per its own comment) — this
audit surfaces it for awareness, not as something to change without a concrete reason to.
