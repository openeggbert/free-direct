# DirectPlay Design Decisions

This document records DirectPlay-related architectural decisions as they are made, with the
rationale behind each. It starts small during `plan.md` Phase 1 and is expected to grow through
Phase 16, at which point it should also gain the state-model/transport-abstraction/DPID-allocation
write-ups `plan.md` Phase 16 calls for. Until then, treat it as a running decision log, not a
complete design document — sections are added one decision at a time, only once that decision has
actually been made.

Every decision here follows `CLAUDE.md`'s DirectPlay Policy: no claim of Microsoft DirectPlay
wire/binary compatibility, decisions driven by real call sites in `../free-eggbert` (the only
target game with any DirectPlay usage — see `docs/directplay-callsite-audit.md` §3), and no scope
expansion beyond what that game actually needs without asking first.

---

## Decision 1: `DirectPlayEnumerateA`/`DirectPlayEnumerateW` must eventually report a fake provider

**Status:** Decided. **Not yet implemented** — this document records the decision only, per
`plan.md` Phase 1's task for this; the actual behavior change belongs to a later phase (see
"When this gets implemented" below). `DirectPlayEnumerateA`/`W` still return `DP_OK` with zero
callback invocations today.

### The question

Should `DirectPlayEnumerateA`/`DirectPlayEnumerateW` ever invoke their callback, or is "always
report zero service providers" an acceptable permanent stub?

### The finding that forces the answer

`free-eggbert/src/network.cpp`'s `CNetwork::CreateProvider(int index)` (cited in
`docs/directplay-callsite-audit.md` §2.2) begins with:

```c
if (index >= m_providers.nb) return FALSE;
```

`m_providers.nb` is populated exclusively by `CNetwork::EnumProviders()`, which calls
`DirectPlayEnumerateA`/`W` and counts how many times the callback fires. If enumeration never
invokes the callback, `m_providers.nb` stays `0` forever, and `index >= 0` is unconditionally true
for any `index >= 0` — so **`CreateProvider` can never succeed, for any input**. `CreateProvider`
is `free-eggbert`'s only call path to `DirectPlayCreate` (see `docs/directplay-callsite-audit.md`
§2.2, the `QueryInterface` row). This is not a UI nicety; it is a hard prerequisite for
`free-eggbert`'s `CNetwork` wrapper to ever obtain a working `IDirectPlay2A` object at all.

### Decision

Once implemented (see below), `DirectPlayEnumerateA`/`DirectPlayEnumerateW` will invoke their
callback **exactly once**, describing a single FreeDirect-internal service provider — enough to
satisfy `CNetwork::CreateProvider(0)`, which is the only index `free-eggbert`'s reconstructed
source is ever seen constructing (see `docs/directplay-callsite-audit.md` §2.3: `CEvent::NetCreate`
calls `CreateProvider(session)`, and nothing in the visible source populates `session` with
anything other than a small index derived from the (currently unreachable) provider-list UI).

- The provider's GUID and display name are FreeDirect-internal placeholders, not real Microsoft
  service-provider identifiers — consistent with this project's explicit non-goal of Microsoft
  DirectPlay wire/binary compatibility. Exact placeholder values are chosen when this is
  implemented, not fixed here.
- The display name should be human-readable (e.g. `"FreeDirect"`), since `CNetwork::
  GetProviderName` exposes it directly to the game's UI text rendering.

### When this gets implemented

Not in Phase 1 — Phase 1 is documentation/`QueryInterface`/`DirectPlayCreate` cleanup only. Real
provider enumeration belongs together with `plan.md` Phase 8 (session enumeration), because a
"provider" conceptually corresponds to a usable transport backend, and Phase 8 is where the
transport abstraction (`IDirectPlayTransport`, per `CLAUDE.md`'s Networking Backend Decision) is
mature enough to describe one.

### Open idea — not decided, not scheduled

Once more than one transport backend exists (`LoopbackDirectPlayTransport`,
`EnetDirectPlayTransport`, and possibly a future `SdlNetDirectPlayTransport`), it may be tempting
to expose **one provider per backend**, so the game's existing (currently unreachable, per
`docs/directplay-callsite-audit.md` §2.3) provider-picker UI doubles as a transport-backend
selector via `DirectPlayCreate`'s `lpGUID` parameter. This is recorded here only as a possible
future direction — it is **not decided**, and per `CLAUDE.md`'s scope policy it must not be
implemented without first confirming `free-eggbert` (the only game that uses DirectPlay at all)
actually needs backend selection at the provider level, or asking the user for an explicit
exception.

### Caveat inherited from the Phase 0 audit

Even once this is implemented, `docs/directplay-callsite-audit.md` §2.3 found that
`free-eggbert`'s only caller of `CNetwork::EnumProviders` (`CEvent::NetEnumSessions`) itself has
zero callers anywhere in the currently-visible `event.cpp` source (the `WM_PHASE_DP_*` handlers
that would call it are empty placeholders). So this decision is necessary for any direct or
integration-test exercise of `CNetwork`, but does not by itself guarantee the shipped/reconstructed
game UI will ever reach it — that gap is a `free-eggbert` source-completeness question, out of
FreeDirect's scope to fix.

---

## Decision 2: single ENet channel (channel `0`) for all DirectPlay traffic

**Status:** Decided and already implemented. `plan.md` Phase 5 listed this as "decide the default
ENet channel layout... and document the decision with rationale" as a separate task from the code
that assumes it; the code (`EnetDirectPlayTransport::Listen`/`Connect`/`Send`, all using
`kChannelLimit = 1` and channel `0`) landed first, with each call site's own comment noting the
decision was "provisional" pending this write-up. This document closes that out — it does not
change anything already built.

### The question

ENet lets a single peer connection carry multiple independent, separately-ordered channels (e.g.
one channel for chat, another for game state, so a large chat backlog can't head-of-line-block a
latency-sensitive position update). How many channels should FreeDirect's ENet-backed DirectPlay
transport use, and how should traffic be assigned to them?

### The finding that forces the answer

`IDirectPlay`/`IDirectPlay2A`'s `Send`/`Receive` methods (`include/dplay.h`) have no channel,
priority, or stream-selection parameter at all — every message a caller sends goes through the
same `Send(idFrom, idTo, dwFlags, lpData, dwDataSize)` call, and every received message comes back
through the same `Receive(lpidFrom, lpidTo, dwFlags, lpData, lpdwDataSize)` call. `docs/
directplay-callsite-audit.md` confirms `free-eggbert`'s `CNetwork` wrapper (`src/network.cpp`'s
`Send`/`Receive`, `src/decnet.cpp`'s gameplay dispatch) exposes and uses exactly this single
stream — there is no separate "chat channel" or "control channel" concept anywhere in the audited
call sites. There is nothing in either target game's real usage that a second ENet channel would
serve.

### Decision

Exactly **one** ENet channel (channel index `0`), for both the hosting (`Listen`) and joining
(`Connect`) roles, for all traffic (`Send`). `kChannelLimit = 1` is passed to `enet_host_create` in
both `Listen()` and `Connect()`; `enet_peer_send(peer_, 0, packet)` always targets channel `0`.
Adding a second channel with no real call site driving a need for it would be exactly the kind of
speculative capability `CLAUDE.md`'s scope policy prohibits ("Speculative API coverage, flags, or
backend features added 'for completeness'... without a real call site").

### When this gets revisited

Only if a future audit of `free-eggbert` (or a change to `free-eggbert` itself, which this project
does not perform) introduces a second independent message stream with its own ordering
requirements — nothing currently visible suggests this will happen. Until then, treat the single
channel as a durable decision, not a placeholder to "eventually" expand.

---

## Decision 3: `DPID` must be a 4-byte `DWORD`, and the host's first player must be assigned `DPID` `0`

**Status:** Decided and implemented (the code landed in `plan.md` Phase 6's "assign the host
player-ID namespace" task, after confirming with the user first since it touches a *public*
header). This document originally recorded the decision only, per `plan.md` Phase 9's acceptance
criteria ("the DPID-vs-index conflict has an explicit written decision in `docs/
directplay-design.md` merged *before* any other Phase 9 code lands") - `include/dplay.h`'s `DPID`
typedef is now `DWORD` (was `DWORD_PTR`), and `DirectPlaySession::nextPlayerId`'s initial value
(and `Close()`'s reset value) is now `0` (was the placeholder `1`).

This decision bundles two questions `docs/directplay-callsite-audit.md` treats separately (§5,
size; §6, starting value) because they both resolve to the same practical requirement once traced
through to `free-eggbert`'s actual source, and `plan.md`/`NEXT.md` already track them as one
combined "resolve the DPID-size decision" task.

### Question 1: how wide is `DPID`?

`include/dplay.h:96` currently defines `typedef DWORD_PTR DPID, *LPDPID;` — 8 bytes on a 64-bit
build — chosen, per its own comment, "to stay ABI-safe on both 32-bit and 64-bit hosts." Real
Microsoft DirectPlay's `DPID` (confirmed in the vendored reference header,
`../free-eggbert/dxsdk3/sdk/inc/dplay.h:51`) is `typedef DWORD DPID` — 4 bytes on every platform.

`free-eggbert/include/network.hpp:10-20`'s `NetPlayer` struct is walked by two functions
(`CEvent::NetSearchPlayer`, `CEvent::NetStartPlay` — `src/event.cpp:2180-2225`, cited in full in
`docs/directplay-callsite-audit.md` §5) using **raw pointer arithmetic with a hardcoded 32-byte
stride**, derived from `NetPlayer`'s size with a 4-byte `DPID`. If FreeDirect's `DPID` stays 8
bytes, `NetPlayer` grows past 32 bytes and both functions silently read the wrong memory offsets
when `free-eggbert` is compiled against this header on a 64-bit target — a correctness bug, not a
crash, which is worse.

**Decision: change `DPID` to `typedef DWORD DPID, *LPDPID;`** (4 bytes, matching real DirectPlay
and `free-eggbert`'s layout assumption). This is resolution path (a) from the audit's §5, not path
(b) (keep 8 bytes, accept 64-bit incompatibility). Justification: FreeDirect's own DPID allocator
(`DirectPlaySession::nextPlayerId`, a plain sequential counter) never needs to encode a pointer
value in a DPID — the entire reason `DWORD_PTR` was chosen no longer applies once DPID allocation
is a small integer counter rather than a disguised pointer. Choosing the width that actually
matches both real DirectPlay's own type and the one real target game's memory-layout assumption
has no offsetting cost.

### Question 2: what DPID value does the host's first (local) player get?

`free-eggbert/src/event.cpp:4692-4699` (inside the `WM_PHASE_MULTI` handler, gated on
`m_pNetwork->IsHost()`) populates `m_players[0]` — explicitly the **local** player's slot — from
the real DPID returned by `CreatePlayer`:

```c
m_pNetwork->m_players[0].bIsPresent = TRUE;
m_pNetwork->m_players[0].dpid = m_pNetwork->m_dpid;
```

Separately, `CNetwork::Receive` (`src/network.cpp:262-289`, cited in `docs/
directplay-callsite-audit.md` §6) looks up which player a received message came from like this:

```c
for (int i = 0; i < MAXNETPLAYER; i++)
{
    if (m_players[i].bIsPresent && from == i)   // compares a DPID directly against a loop index
    { ... }
}
```

This compares the message's sender `DPID` (`from`) directly against the **loop index** `i`, not
against `m_players[i].dpid` (the field that was correctly populated above). For the local player
(slot `0`) to ever be recognized as the sender of a message it receives — which is exactly what
happens on FreeDirect's already-implemented self-send loopback path (`plan.md` Phase 4, `idTo ==
idFrom`) — `from` must equal `0`, i.e. **the local/host player's real `DPID` must literally be
`0`**.

Real Microsoft DirectPlay reserves `DPID` `0` for `DPID_SYSMSG` (the `from` value on system
messages) and `DPID_ALLPLAYERS` (the broadcast `idTo` target) — see
`../free-eggbert/dxsdk3/sdk/inc/dplay.h:56,61`, both literally `0` — and never assigns `0` to a
real player. `free-eggbert`'s own `CNetwork::Send` (`src/network.cpp:254`) independently requires
`idTo == 0` to mean "broadcast to all players" (`m_pDP->Send(m_dpid, 0, !!dwFlags, ...)` — a
hardcoded literal `0`, not a named constant, so FreeDirect cannot make broadcast mean anything
other than literal `0` without editing game source, which is not allowed). These two requirements
— `0` must mean "broadcast target"/"system message source" **and** `0` must be a real player's ID
— are exactly the "mutually exclusive" choice `plan.md` Phase 9 flags.

**Decision: break with real DirectPlay's reservation convention. The host's first (local) player
is assigned `DPID` `0`.** `DPID_ALLPLAYERS` and `DPID_SYSMSG` (Phase 11, not yet defined in
`include/dplay.h`) will also use the literal value `0`, reused for a third role (a real player's
ID) beyond their usual two (broadcast target, system-message source) — real DirectPlay already
overloads `0` across two roles depending on field position (`idTo` vs. `from`); this adds one
more instance of the same literal value, not a new kind of ambiguity. `CLAUDE.md`'s DirectPlay
Policy is explicit that wire/semantic compatibility with real DirectPlay is a non-goal — the only
compatibility bar is `free-eggbert`'s actual C++ call/comparison patterns, and its own source
requires this to work. `docs/directplay-callsite-audit.md`'s Phase 0 audit found no evidence
`free-eggbert` ever depends on receiving a real `DPID_SYSMSG`-sourced system message, so the
`DPID_SYSMSG`-vs-real-player-`0` ambiguity is not expected to cause an observable problem for
either named target game; if a future audit finds otherwise, revisit this decision rather than
silently special-casing around it.

Subsequent players (joining after the host) get sequential DPIDs starting at `1` (`1, 2, 3, ...`).
This part is **not** independently confirmed against `free-eggbert` source the way slot `0` is:
`docs/directplay-callsite-audit.md` §2.3 and §5 both note that the code which would populate
`m_players[1..]` for remote joiners (processing an incoming `MESS_2` join message, sent by
`src/event.cpp:4701-4709`) is not visible/reachable in the current decompiled `event.cpp` snapshot
— only the host's own slot-`0` self-population is. Sequential allocation from `1` is the
best-available, honestly-labeled-provisional choice for remote players until a more complete
`free-eggbert` source (or a real multi-peer integration test) confirms or contradicts it.

### Implemented

Landed in `plan.md` Phase 6's "assign the host player-ID namespace" task (also closing out Phase
9's identically-worded "reserve an invalid DPID value" task, in the opposite direction from that
task's own title - see its `plan.md` annotation): `include/dplay.h`'s `DPID` typedef changed to
`DWORD`, and `DirectPlaySession::nextPlayerId`'s initial value (and `Close()`'s reset value)
changed from the placeholder `1` to `0`. Both changed together, since a `DPID` `0` returned by a
still-8-byte-wide `DPID` would have carried no benefit - only the size-plus-value combination
together satisfies `free-eggbert`'s observed assumptions.
`src/directplay/DirectPlayWireProtocol.hpp`'s wire header (`plan.md` Phase 5) serializes
`idFrom`/`idTo` using the local `sizeof(DPID)` as-is (never a hardcoded byte count), so its wire
format changed with this decision automatically - no separate wire-protocol code change was
needed. `tests/directplay_tests.cpp`'s DPID-uniqueness test was updated (`0` is now the correct
first value to assert, not an error). **Verified for real**: confirmed `sizeof(DPID) == 4`
directly (was `8`); both CMake build configurations and the 14/14 test suite re-verified.

### Caveat

This decision is scoped to what is needed for `free-eggbert`'s host-local-player, self-received-
message case — the only case with a direct source citation. `planetblupi` has zero DirectPlay
usage (confirmed by grep, `docs/directplay-callsite-audit.md` §3) and places no requirements here.
If a future, more complete audit of `free-eggbert` finds the remote-player index-assignment
strategy above is wrong, that is a new finding to act on, not a sign this decision itself was
made incorrectly given what was knowable at the time.

---

## Decision 4: `Open()` selects its transport backend at build time, via `FREE_DIRECT_ENABLE_ENET`

**Status:** Decided and implemented. Recorded ahead of `plan.md` Phase 6 ("Session hosting")
writing any code that depends on it, per the same "decide before implementing" pattern as
Decisions 2 and 3 - `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`) now `#ifdef
FREE_DIRECT_ENABLE_ENET`-selects `EnetDirectPlayTransport` vs. `LoopbackDirectPlayTransport`, and
`CMakeLists.txt`'s `if(FREE_DIRECT_ENABLE_ENET)` block defines that macro via
`target_compile_definitions`. Asked of, and confirmed by, the user directly (this is a FreeDirect-internal
mechanism with no real DirectPlay equivalent to derive it from — see "The question" below).

### The question

`DirectPlay2AImpl::Open()` (`DirectPlay.cpp`) currently assigns `LoopbackDirectPlayTransport`
unconditionally — it is the only backend that exists in a way `Open()` can reach. `plan.md` Phase
6 wants `Open(..., DPOPEN_CREATE)` to "start the ENet host listener... when using
`EnetDirectPlayTransport`", which presupposes some way for `Open()` to know *whether* it is using
`EnetDirectPlayTransport`. Real Microsoft DirectPlay has no concept of a FreeDirect-internal
transport backend at all, so this cannot be answered by "what does `free-eggbert` already call" —
it is a FreeDirect-specific mechanism question with no real-DirectPlay precedent to defer to.

### Decision

Backend selection is a **build-time**, not run-time, choice, driven directly by the existing
`FREE_DIRECT_ENABLE_ENET` CMake option (`CLAUDE.md`'s Networking Backend Decision; `plan.md` Phase
5's CMake infrastructure):

- When `FREE_DIRECT_ENABLE_ENET=ON`, `Open(..., DPOPEN_CREATE)` always constructs an
  `EnetDirectPlayTransport`.
- When `FREE_DIRECT_ENABLE_ENET=OFF` (the default), `Open()` always constructs a
  `LoopbackDirectPlayTransport`, exactly as it does today — the default build's behavior is
  unchanged by this decision.
- No new `IDirectPlay`/`IDirectPlay2A`/`DPSESSIONDESC2` parameter, environment variable, or other
  run-time switch is added. `DirectPlayCreate`'s signature and `Open`'s signature stay exactly as
  real DirectPlay defines them.

### Rationale

- Simplicity: no new API surface, matching `CLAUDE.md`'s scope policy against adding
  DirectPlay-shaped surface without a concrete call-site need — there is no `free-eggbert`/
  `planetblupi` call site that could ever supply a "pick ENet vs. loopback" argument, since real
  DirectPlay has no such concept.
  `tests/directplay_tests.cpp`'s existing loopback-only tests keep working unmodified: they are
  built and run without `FREE_DIRECT_ENABLE_ENET`, so they get `LoopbackDirectPlayTransport`
  exactly as before. Whichever test program eventually exercises real ENet networking
  end-to-end (`plan.md` Phase 15) is built *with* `FREE_DIRECT_ENABLE_ENET=ON` instead, and gets
  `EnetDirectPlayTransport` automatically, no extra wiring needed on either side.
- Matches the already-established pattern: every `EnetDirectPlayTransport`-touching line in
  `CMakeLists.txt` is already gated behind this exact option; extending that gating into `Open()`'s
  runtime behavior is a natural continuation, not a new mechanism, from the transport's own
  build/link perspective.

### Consequence for testing

Because the choice is compile-time, `DirectPlay.cpp`'s `Open()` implementation needs a
preprocessor conditional (`#ifdef`/`#if`) fed by a compile definition CMake sets when
`FREE_DIRECT_ENABLE_ENET=ON` — `CMakeLists.txt` does not currently define one (it only adds
`EnetDirectPlayTransport.cpp` to `target_sources()` and links `FreeDirect::ENet`), so adding that
compile definition is part of implementing this decision, not a separate task.

### Implemented

`CMakeLists.txt`'s existing `if(FREE_DIRECT_ENABLE_ENET)` block (the one already linking
`FreeDirect::ENet` and adding `EnetDirectPlayTransport.cpp`) now also has
`target_compile_definitions(free-direct PRIVATE FREE_DIRECT_ENABLE_ENET=1)`.
`DirectPlay2AImpl::Open()` `#ifdef FREE_DIRECT_ENABLE_ENET`-guards both the
`#include "EnetDirectPlayTransport.hpp"` and the transport construction itself. Verified with a
standalone smoke test compiling the identical `DirectPlay.cpp` with and without the macro and
observing genuinely different runtime behavior (see Decision 5's own verification, which reuses
the same technique).

### Open idea — not decided, not scheduled

A run-time backend choice (e.g. an environment variable read once at process start) would let a
single build exercise both backends without reconfiguring CMake, which could simplify future
integration testing (`plan.md` Phase 15). This was considered and explicitly rejected for now in
favor of the simpler build-time mechanism, per the user's direct choice — revisit only if Phase 15
finds the build-time mechanism genuinely blocks a specific testing need, not preemptively.

---

## Decision 5: fixed default ENet listen port `51321`

**Status:** Decided and implemented. Asked of, and confirmed by, the user directly - like
Decision 4, this is a FreeDirect-internal mechanism question with no real DirectPlay precedent to
derive it from.

### The question

Once `Open(..., DPOPEN_CREATE)` selects `EnetDirectPlayTransport` (Decision 4), it needs to call
`Listen(port)` to actually start hosting. What `port`?

### The finding that forces the answer

`DPSESSIONDESC2` (`include/dplay.h`) has no port, address, or any other network-addressing field
at all - real Microsoft DirectPlay abstracts network addressing behind service providers, a
concept FreeDirect's DirectPlay reimplementation does not have (per `CLAUDE.md`'s DirectPlay
Policy, FreeDirect's compatibility goal is the `IDirectPlay`/`IDirectPlay2A` C++ API contract, not
wire/protocol-level fidelity to real DirectPlay's service-provider architecture). There is
therefore no DirectPlay API value a port could be derived from, unlike (for example) the DPID
decisions in Decision 3, which were forced by concrete `free-eggbert` source citations. This is a
pure FreeDirect-internal implementation choice.

### Decision

A single fixed constant, `kDefaultDirectPlayEnetPort = 51321`
(`src/directplay/EnetDirectPlayTransport.hpp`), used by every hosting `Open(...,
DPOPEN_CREATE)` call when `FREE_DIRECT_ENABLE_ENET` is on. Chosen from IANA's dynamic/private port
range (49152-65535) specifically to minimize collision risk with registered services, since
FreeDirect has no way to know what else might be running on a host machine. No configurability
(environment variable, session-descriptor-adjacent field, etc.) is added - matching Decision 4's
"no new mechanism beyond what's strictly needed" reasoning, and because nothing currently needs
more than one FreeDirect session listening on one machine at a time.

### Implemented

`DirectPlay2AImpl::Open()` (`DirectPlay.cpp`), inside the `FREE_DIRECT_ENABLE_ENET` branch, calls
`session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayEnetPort)` only when
`session_.isHost` (i.e. `DPOPEN_CREATE`) - a joining role calling `Connect()` instead is `plan.md`
Phase 7's job, not wired up here. If `Listen()` fails, `Open()` resets the transport and returns
`DPERR_CANTCREATESESSION` (the closest-matching real `DPERR_*` code: it means "the host attempt to
create/set up the session itself failed," which is exactly what a failed `Listen()` represents at
this level - not e.g. `DPERR_INVALIDPARAMS`, since nothing about the caller's parameters was
wrong).

**Verified for real**, not just "compiles", with two standalone smoke tests (not committed):
(1) the identical `DirectPlay.cpp` compiled twice - once without and once with
`-DFREE_DIRECT_ENABLE_ENET=1` - producing genuinely different `Open`/`CreatePlayer`/`Send`
behavior each time (`DP_OK` for loopback self-send; `DPERR_GENERIC` for the ENet path, since
`Send()` still has no peer - `Listen()` alone does not create one). (2) A port-conflict test under
the ENet build: a first `Open(DPOPEN_CREATE)` succeeds; a *second*, independent `DirectPlayCreate`
object's `Open(DPOPEN_CREATE)` (same default port) fails with `DPERR_CANTCREATESESSION`, because
the OS-level UDP socket is already bound - real evidence `Listen()` performed a genuine `bind()`,
not a no-op; releasing the first host and opening a third then succeeds again, proving the port
was genuinely released. **An earlier attempt at a third kind of test - a real raw ENet client
actually connecting to the hosted port - failed, and stayed failed after investigation, which is
itself an important, now-documented finding** (see Caveat below), not a bug in `Listen()` or this
decision. Also re-verified both CMake build configurations (`ENET=OFF`/`ON`) end-to-end and the
12/12 `tests/directplay_tests.cpp` suite (built without the macro, unaffected).

### Caveat

`Listen()` succeeding means a real UDP socket is bound on port `51321` and ENet considers the host
capable of accepting connections - but **no connection can actually complete today**, for a more
fundamental reason than "nothing calls `Connect()`/`EnumSessions` yet" (Phases 7/8, not started):
ENet is a poll-driven library with no internal thread - nothing happens on an `ENetHost` (accepting
a pending connection, delivering a packet, anything) until something calls `enet_host_service()` on
it. `Open()` calls `Listen()` and returns; nothing in `DirectPlay.cpp` ever services the
constructed transport's host afterward, and `EnetDirectPlayTransport` has no general "pump" method
for anything to call even if `DirectPlay.cpp` wanted to (only `Shutdown()`'s own bounded
disconnect-wait loop services the host internally, and only during teardown). A real external ENet
client attempting to connect today (verified directly - the connection genuinely never completes)
would see its connection attempt time out, not get rejected - the socket is open but nothing is
answering it. Some later task (implied by Phase 7/8/10's real message/connection handling, not
explicitly named as its own checkbox anywhere yet) needs to give `Open()`'s hosted session an
actual event loop or periodic service call before a real network connection can complete
end-to-end. **Resolved by Decision 6, below.**

---

## Decision 6: event servicing via a new `IDirectPlayTransport::Service()`, called from `Receive()`

**Status:** Decided and implemented. Resolves Decision 5's Caveat. Asked of, and
confirmed by, the user directly - like Decisions 4 and 5, this is a FreeDirect-internal mechanism
question with no real DirectPlay equivalent (real DirectPlay's service providers hide however
they pump their own network I/O; FreeDirect's ENet backend needs to expose this somehow, since
ENet itself is poll-driven with no internal thread).

### The question

Something must call `enet_host_service()` on a hosted (or connecting) `EnetDirectPlayTransport`'s
`ENetHost` periodically, or no ENet event (a peer connecting, a packet arriving, a peer
disconnecting) is ever processed. What calls it, and when?

### The finding that forces the answer

`docs/directplay-callsite-audit.md` establishes that `free-eggbert`'s own `CNetwork::Receive()`
(`src/network.cpp:262-289`) is called repeatedly from the game's own polling loop
(`src/decnet.cpp`'s gameplay dispatch) - `free-eggbert` already has a "pump" of its own, driven by
however often the game calls `Receive`. There is no equivalent existing call site that would drive
a periodic "just service the network, regardless of whether the caller wants a message right now"
operation - `free-eggbert` never calls anything like that.

### Decision

Add `virtual void Service() = 0;` to `IDirectPlayTransport` (`src/directplay/
DirectPlayTransport.hpp`). `DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) calls
`session_.transport->Service()` once, unconditionally, before consulting
`session_.messageQueue.TryReceive()` - piggybacking on `free-eggbert`'s own existing
call-`Receive()`-repeatedly pattern rather than introducing a new API the game would never call,
or a background thread that would add real thread-safety requirements to classes that have none
today (`EnetDirectPlayTransport`'s `host_`/`peer_` are accessed only from whatever thread calls
`Send`/`Receive`/`Shutdown`/`Service` - a background thread servicing the same `ENetHost`
concurrently would require locking that does not exist and should not be added speculatively).

`LoopbackDirectPlayTransport::Service()` is a no-op - loopback has no real network events to
process; everything already happens synchronously inside `Send()`/`Receive()`.

`EnetDirectPlayTransport::Service()` drains all currently-pending events with a non-blocking
`enet_host_service(host_, &event, 0)` loop (timeout `0`: process what's ready, never block
`Receive()` waiting for network I/O - `free-eggbert`'s own `Receive()` call sites already expect
`DPERR_NOMESSAGES` back immediately when nothing is available, not to block):

- `ENET_EVENT_TYPE_CONNECT`: if `peer_` is currently null, adopt the newly-connected
  `event.peer` as `peer_`. This naturally extends the existing single-peer model (documented
  throughout `Listen`/`Connect`/`Send`/`Shutdown`'s own comments) to the hosting role for its
  first (and, today, only) connecting client - multi-peer hosting remains `plan.md` Phase 6/10's
  separate, later job, not expanded here. If `peer_` is already set, the new connection is
  accepted at the ENet protocol level but not adopted (nothing tracks it) - an honest limitation
  of the current single-peer scope, not a crash or silent corruption.
- `ENET_EVENT_TYPE_DISCONNECT`: if the disconnecting `event.peer` is the tracked `peer_`, clear
  it.
- `ENET_EVENT_TYPE_RECEIVE`: the packet is destroyed (`enet_packet_destroy`) without being
  delivered anywhere. Real transport-level `Receive()` (making `EnetDirectPlayTransport::Receive()`
  do something other than unconditionally return `false`) is a separate, still-open task - no
  `plan.md` Phase 5/6 task covers it, and inventing a half-finished buffering mechanism here to
  make `Service()` "more complete" would be exactly the kind of speculative, undirected code
  `CLAUDE.md` warns against. Dropping the packet is the honest choice until that task exists.

### Consequence

A connection can now genuinely complete end-to-end through `Open()`'s hosted listener, but only
once something calls `IDirectPlay2A::Receive()` on it at least once after a peer starts
connecting - matching exactly how `free-eggbert` already drives its own network polling. A host
that is `Open()`ed but whose owner never calls `Receive()` still never processes any ENet event,
which is consistent with `free-eggbert`'s own usage pattern (it always polls via `Receive()`), not
a new limitation introduced here.

### Implemented

`IDirectPlayTransport::Service()` (`src/directplay/DirectPlayTransport.hpp`),
`LoopbackDirectPlayTransport::Service()` (no-op), `EnetDirectPlayTransport::Service()` (drains
pending events as described above), and `DirectPlay2AImpl::Receive()`'s call to it - all in
`plan.md` Phase 6's "give `EnetDirectPlayTransport` a way to actually service its events" task.
**Verified for real, closing Decision 5's Caveat**: a standalone smoke test (not committed) drove
a real, external (non-FreeDirect) ENet client to a genuinely *completed* connection with `Open()`'s
hosted session, using nothing but repeated calls to the public `IDirectPlay2A::Receive()` - the
exact same call pattern `free-eggbert` already uses, no whitebox access needed. A second smoke
test confirmed the connect/disconnect bookkeeping directly: a first client is adopted as `peer_`
(`HasPeer()` becomes `true`); a second, concurrently-connecting client is not adopted (no
corruption of the existing single-peer tracking); the first client's graceful `Shutdown()` is
observed and clears `HasPeer()` back to `false`.

---

## Decision 7: multi-peer hosting via a DPID-keyed peer map, with DPID assignment left outside the transport

**Status:** Decided and implemented. Asked of, and confirmed by, the user directly: a
`DPID → ENetPeer*` map (over a plain list, or documenting-only). This elaborates that choice with
the concrete mechanics it requires - the question as asked did not by itself resolve several real
sub-questions the design forces, so those are resolved here too, following the same pattern as
every other Decision in this document.

### The question

`EnetDirectPlayTransport` today tracks exactly one `peer_` total, shared ambiguously between the
hosting role (`Listen()`) and the joining role (`Connect()`). `plan.md` Phase 6 wants the host to
accept multiple incoming client connections, up to `dwMaxPlayers`. What replaces `peer_`, and how
does DPID assignment - which is `DirectPlaySession`'s job (`nextPlayerId`, shared with local
`CreatePlayer()` calls so DPIDs never collide) - reach a class that is intentionally
backend-agnostic and knows nothing about DPID/session semantics?

### Sub-question 1: hosting and joining are not symmetric

A single `EnetDirectPlayTransport` instance is used *either* as a host (`Listen()`) *or* as a
joining client (`Connect()`) - never both (each method already refuses to run if `host_` is
already set). The two roles have fundamentally different peer cardinality: a joining client has
exactly one relationship (to the host it connected to); a host can have zero-to-many. Trying to
force both into one `DPID → ENetPeer*` map would require the client role to also carry a DPID for
"the host" before it has one (DPID assignment for a joining client happens later, via a
join-accepted packet - `plan.md`'s next two Phase 6 tasks, not implemented yet).

**Decision:** keep them separate. `hostPeer_` (renamed from `peer_`) remains a single-`ENetPeer*`
field, used *only* by the client role (`Connect()`/`Send()`/`Shutdown()` for that role, `HasPeer()`
still reports on it - unchanged behavior for every currently-passing test). A new
`std::unordered_map<DPID, ENetPeer*> connectedPeers_` is used *only* by the host role, populated
only after a caller explicitly assigns a DPID (sub-question 2).

### Sub-question 2: where does an incoming peer's DPID come from?

`Service()`'s `ENET_EVENT_TYPE_CONNECT` handling cannot allocate a DPID itself: `DirectPlaySession
::nextPlayerId` is the single shared counter that must also serve local `CreatePlayer()` calls
(so a local player and a remote peer never end up with the same DPID), and that counter lives in
`DirectPlaySession` (`DirectPlay.cpp`), not in the transport - moving it into the transport would
violate the standing invariant that `IDirectPlayTransport` implementations stay backend-agnostic
and know nothing about DPID/session semantics (`NEXT.md`'s architecture notes; this decision
narrows, but does not remove, that boundary - see "What stays out of the transport" below).

**Decision:** a newly-connected, not-yet-assigned peer is queued in a new
`std::deque<ENetPeer*> pendingPeers_` (host role only). Two new `IDirectPlayTransport` methods let
a caller resolve a pending connection *without the transport ever exposing an `ENetPeer*`* (which
would leak an ENet type across the abstraction boundary the whole `IDirectPlayTransport`
abstraction exists to prevent):

- `virtual bool HasPendingConnection() const = 0;` - true if at least one connected-but-unassigned
  peer is waiting.
- `virtual bool AssignPendingConnection(DPID id) = 0;` - pops the oldest pending peer and registers
  it under `id`; returns `false` if there was no pending connection to assign.

`DirectPlay2AImpl::Receive()` (already the one call site `Service()` piggybacks on, per Decision
6) is extended: after `Service()`, while `session_.isHost` and the transport reports a pending
connection and `session_.currentPlayers < session_.maxPlayers`, allocate the next DPID
(`session_.nextPlayerId++`), call `AssignPendingConnection(id)`, add `id` to
`session_.remotePlayerIds`, and increment `session_.currentPlayers`. If the cap is already
reached, the pending connection is simply left pending (connected at the ENet level, but never
assigned a DPID) - explicitly rejecting/disconnecting it once full is `plan.md`'s separate
"enforce `dwMaxPlayers` by rejecting new joins" task, not this one's. Decrementing
`currentPlayers`/removing from `remotePlayerIds` on disconnect is likewise `plan.md`'s separate
"update `dwCurrentPlayers` as players join and leave" task - this decision's `Service()` only
removes the ENet-level peer bookkeeping (`connectedPeers_`/`pendingPeers_`) on disconnect, not the
DirectPlay-level player-count bookkeeping.

`LoopbackDirectPlayTransport` implements both new methods trivially: `HasPendingConnection()`
always `false`, `AssignPendingConnection()` always `false` - loopback has no concept of an
incoming connection to accept.

### Sub-question 3: what does `Send()`/`Receive()` mean for a host with multiple peers now?

Before this decision, `Service()` adopted the *first* connecting peer into the single `peer_`
field regardless of role, which made `Send()` incidentally work for a host's first (and only)
connected client - an accident of the single-peer model, not a designed capability (already
flagged in earlier comments as "a host with multiple connected peers... needs its own per-peer
addressing, which is Phase 6/10's job"). This decision makes that accident go away: a host-role
instance's `hostPeer_` is now always null (host role never touches it), so `Send()`/`Receive()`
return `false` for a `Listen()`-ed instance unconditionally, regardless of how many peers are in
`connectedPeers_`. **This is an intentional, documented behavior change, not a silent regression**
- no committed test exercised host-role `Send()` before this decision (only connection/disconnect
completion were tested that way), and per-DPID-addressed `Send()`/`Receive()` for a multi-peer
host is explicitly `plan.md` Phase 10's job.

### What stays out of the transport

`EnetDirectPlayTransport` now knows about `DPID` as an opaque map key (it already did, in a
narrower sense, for `DirectPlayWireProtocol.hpp`'s wire header fields) - but it never allocates
one, never validates one against session state (`dwMaxPlayers`, duplicate detection, etc.), and
never decides *whether* to accept a pending connection. All of that policy stays in
`DirectPlay.cpp`/`DirectPlaySession`, exactly matching the existing boundary. The transport's job
stays "move bytes and report connection-shaped facts (pending, assigned, gone)"; DirectPlay-level
meaning is layered on top, same as before.

### Implemented

`src/directplay/DirectPlayTransport.hpp` (`HasPendingConnection()`/`AssignPendingConnection(DPID)`
added to `IDirectPlayTransport`; `LoopbackDirectPlayTransport` implements both trivially, always
`false`), `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` (`hostPeer_`/`connectedPeers_`/
`pendingPeers_` split, `Service()`'s `CONNECT`/`DISCONNECT` handling updated for both roles,
`Send()` now always `false` for a hosting-role instance, `Shutdown()` generalized to disconnect
every known peer), and `src/directplay/DirectPlay.cpp` (`DirectPlay2AImpl::Receive()` extends its
`Service()` call with the DPID-assignment loop, gated on `dwMaxPlayers` with the `0`-means-
unlimited real-DirectPlay convention). **Verified for real** with two standalone smoke tests (not
committed): (1) directly against `EnetDirectPlayTransport`, three real ENet clients all connect
and queue as pending; two are assigned DPIDs (simulating a cap), the third stays pending;
disconnecting one of the two assigned peers correctly shrinks `connectedPeers_` without disturbing
the still-pending third, which is then assigned successfully. (2) End-to-end through the real
`Open()`/`CreatePlayer()`/`Receive()` path: two independent real ENet clients both complete a
genuine connection to the same hosted session simultaneously - impossible under the previous
single-`peer_` model. `dwMaxPlayers` enforcement itself is verified at the transport level only,
since `IDirectPlay2A` has no player-count-observing method to check it through end-to-end. Both
CMake build configurations and the 14/14 `tests/directplay_tests.cpp` suite (unaffected)
re-verified throughout.

---

## Decision 8: disconnect notification via a `DPID` queue, mirroring the pending-connection design

**Status:** Decided and implemented. Resolves the gap Decision 7 left open: `Service()`
already removes a disconnected peer from `connectedPeers_`, but nothing tells the caller *which*
`DPID` that was, so `plan.md`'s "update `dwCurrentPlayers` as players join and leave" task cannot
decrement `currentPlayers`/remove from `remotePlayerIds` on the "leave" side.

### The question

How does `EnetDirectPlayTransport` tell `DirectPlay2AImpl::Receive()` "the peer assigned DPID `X`
just disconnected", without exposing an `ENetPeer*` outside the transport (the same rule Decision
7 established for pending connections)?

### Decision

Mirror Decision 7's pending-connection shape exactly, rather than inventing a new pattern: a new
`std::deque<DPID> disconnectedPeerIds_` (hosting role only), populated by `Service()`'s
`ENET_EVENT_TYPE_DISCONNECT` handling - when the disconnecting peer is found in `connectedPeers_`
(i.e. it had already been assigned a DPID via `AssignPendingConnection`), that `DPID` is pushed
onto `disconnectedPeerIds_` before the map entry is erased. A peer that disconnects while still
only in `pendingPeers_` (never assigned) is *not* reported - nothing in `DirectPlaySession` knows
about it yet, so there is nothing to reconcile.

Two new `IDirectPlayTransport` methods:

- `virtual bool HasDisconnectedPeer() const = 0;` - true if at least one disconnect is queued.
- `virtual bool TakeDisconnectedPeer(DPID* outId) = 0;` - pops the oldest queued disconnect into
  `*outId` (if non-null) and returns `true`; returns `false` (no-op) if the queue was empty.

`TakeDisconnectedPeer` uses an output parameter and a `bool` return, **not** a `DPID` return value
with `0` meaning "none" - unlike before Decision 3, `DPID` `0` is now a valid, real player ID (the
host's own local player), so it can never double as an "empty" sentinel. This matches the existing
style used elsewhere in this codebase for "optional value via output pointer" (e.g. `CreatePlayer`'s
`lpidPlayer`, `Receive`'s `lpidFrom`/`lpidTo`).

`DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) is extended, in the same `session_.isHost` block
as Decision 7's assignment loop, with a companion loop *before* it (process departures before
admitting new arrivals in the same `Receive()` call): while hosting and
`HasDisconnectedPeer()`, take the next disconnected `DPID`, remove it from
`session_.remotePlayerIds`, and decrement `session_.currentPlayers` (guarded against underflow,
though it should never actually happen given the invariant that every `remotePlayerIds` entry
came from a prior successful `AssignPendingConnection`/`currentPlayers++` pair).

`LoopbackDirectPlayTransport` implements both new methods trivially: `HasDisconnectedPeer()`
always `false`, `TakeDisconnectedPeer()` always `false` - loopback has no incoming-connection
concept, so it has no disconnect-of-an-incoming-connection concept either.

### Implemented

`src/directplay/DirectPlayTransport.hpp` (`HasDisconnectedPeer()`/`TakeDisconnectedPeer(DPID*)`
added to `IDirectPlayTransport`; `LoopbackDirectPlayTransport` implements both trivially, always
`false`), `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` (`disconnectedPeerIds_` queue,
populated by `Service()`'s `ENET_EVENT_TYPE_DISCONNECT` handling only for already-assigned peers),
and `src/directplay/DirectPlay.cpp` (`DirectPlay2AImpl::Receive()`'s new departures-before-arrivals
loop). **Verified for real** with two standalone smoke tests (not committed): (1) directly against
`EnetDirectPlayTransport` - a real ENet client connects, is assigned a DPID, disconnects, and
`TakeDisconnectedPeer()` reports exactly that DPID exactly once; a second client that disconnects
*before* being assigned is confirmed not reported at all. (2) End-to-end through the real
`Open()`/`CreatePlayer()`/`Receive()` path: a real ENet client connects then disconnects,
`Receive()` runs repeatedly throughout with no crash/hang, and the session stays healthy afterward
(a second local `CreatePlayer()` still succeeds). `remotePlayerIds`/`currentPlayers` correctness
itself could only be verified at the transport level, since `IDirectPlay2A` has no
player-count-observing method. Both CMake build configurations and the 14/14
`tests/directplay_tests.cpp` suite (unaffected) re-verified.

---

## Decision 9: rejecting an over-`dwMaxPlayers` pending connection - graceful disconnect, no explanation packet yet

**Status:** Decided and implemented. Closes `plan.md`'s "enforce `dwMaxPlayers` by rejecting new
joins" task, left open by Decision 7 (which only assigned DPIDs up to the cap and left any excess
pending forever, undisturbed).

### The question

Once `session_.currentPlayers` reaches `session_.maxPlayers`, what should happen to a connection
that is still sitting in `pendingPeers_` (connected at the ENet level, never assigned a DPID)?
Two sub-questions: how does the host actually reject it, and should the disconnect be graceful or
immediate?

### Decision

A new `IDirectPlayTransport::RejectPendingConnection()` method, mirroring
`AssignPendingConnection(DPID id)`'s shape exactly but with no `DPID` parameter (there is nothing
to assign - the connection is being turned away, not admitted): pops the oldest pending peer and
calls `enet_peer_disconnect(peer, 0)` on it - the same **graceful** disconnect
`Shutdown()`/`AssignPendingConnection`'s counterpart use elsewhere in this class, not
`enet_peer_disconnect_now`. Graceful was chosen over immediate because this is an ordinary,
expected outcome (the session is full), not a fault or a misbehaving peer - the same reasoning
`Shutdown()` already applies to every peer it tears down. The rejected peer is popped out of
`pendingPeers_` immediately (so it can never be reconsidered for assignment while its disconnect
is in flight); `Service()`'s existing `ENET_EVENT_TYPE_DISCONNECT` handling harmlessly finds no
match for it in `pendingPeers_`/`connectedPeers_` once the disconnect completes, which is correct -
a rejected connection was never assigned a `DPID`, so (per Decision 8) it must not be reported via
`TakeDisconnectedPeer()` either; nothing outside the transport ever knew about it.

`DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) gains a second loop after the existing
DPID-assignment loop, only entered when there is a *real* cap (`session_.maxPlayers != 0` - `0`
means "no limit", so nothing is ever rejected in that case) and the session is at or over it:
repeatedly reject pending connections until none remain.

`LoopbackDirectPlayTransport::RejectPendingConnection()` is trivially `false` - loopback never has
a pending connection to reject.

**No join-rejected explanation packet is sent** - the rejected peer only observes a disconnect,
with no way (yet) to know *why*. Sending an actual `DirectPlayWirePacketType::JoinReject` packet
(`plan.md`'s separate "send a join-rejected packet..." task) needs per-DPID-addressed `Send()`,
which does not exist yet (`plan.md` Phase 10, per Decision 7's sub-question 3) - a pending
connection has no DPID to address a packet to in the first place, so that task cannot be
implemented before DPID-addressed send exists regardless. This decision only covers the
ENet-level rejection; the explanation packet remains a distinct, still-blocked task.

### Implemented

`src/directplay/DirectPlayTransport.hpp` (`RejectPendingConnection()` added to
`IDirectPlayTransport`; `LoopbackDirectPlayTransport` implements it trivially, always `false`),
`src/directplay/EnetDirectPlayTransport.hpp`/`.cpp`, and `src/directplay/DirectPlay.cpp`
(`DirectPlay2AImpl::Receive()`'s new post-assignment rejection loop). **Verified for real** with a
standalone smoke test (not committed): with `dwMaxPlayers` set to a small cap, more real ENet
clients connect than fit; the clients within the cap complete normally, and the excess client
genuinely observes a real `ENET_EVENT_TYPE_DISCONNECT` (not a timeout, not silence) - proof the
rejection is a real ENet-protocol-level disconnect, not just an internal bookkeeping no-op. Both
CMake build configurations and the 14/14 `tests/directplay_tests.cpp` suite (unaffected)
re-verified.

**Amendment (Decision 10):** `LoopbackDirectPlayTransport::RejectPendingConnection()` (and
`HasPendingConnection()`/`AssignPendingConnection()`/`HasDisconnectedPeer()`/
`TakeDisconnectedPeer()`) are no longer trivially `false` - Decision 10, below, gives
`LoopbackDirectPlayTransport` a real multi-instance connection lifecycle, so these now behave the
same way `EnetDirectPlayTransport`'s do. This paragraph is left as-written above (rather than
edited) since it was an accurate description of the code at the time Decision 9 was implemented.

---

## Decision 10: `LoopbackDirectPlayTransport` multi-instance connections via a port-keyed static registry

**Status:** Decided and implemented. Two sub-questions asked of, and confirmed by, the user
directly; a `Plan` subagent then validated the concrete mechanics against
`EnetDirectPlayTransport`'s existing shape.

### The question

`plan.md` Phase 7 ("Session joining") needs a deterministic loopback test where a host and two
clients (a `dwMaxPlayers`-capped session) all connect in the same test process, and a third client
is rejected - mirroring Phase 6/Decisions 7-9's ENet-level coverage, but committed and CI-friendly
(this project's Testing Policy already prefers `LoopbackDirectPlayTransport` for determinism over
uncommitted ENet smoke tests). But `LoopbackDirectPlayTransport` only ever talked to itself:
`Listen()`/`Connect()` were both trivial `return true;` no-ops, and `Send()`/`Receive()` operated
on a single instance's own FIFO. There was no way for a separate "client" instance to find and
reach a separate "host" instance in the same process at all.

### Decision

**Sub-question 1: how does `Connect()` find the host?** A process-wide static registry
(function-local static `std::unordered_map<std::uint16_t, LoopbackDirectPlayTransport*>`, keyed by
the `port` argument `Listen()`/`Connect()` are called with), over a session-GUID-keyed alternative
(rejected: it would require plumbing DPID/GUID knowledge down into the transport layer, a bigger
interface change with no other motivating need yet). `Listen(port)` on an already-registered port
fails, mirroring `EnetDirectPlayTransport::Listen()`'s real `bind()`-conflict behavior - a "coexist"
alternative was rejected because it would make which host a `Connect()` reaches
order-dependent/nondeterministic.

`Connect()` deliberately **deviates from ENet**: real ENet's `Connect()` succeeds even with nobody
listening yet, discovered later via a `Service()` timeout. Loopback has synchronous, perfect
knowledge of the registry's contents, so `Connect()` fails immediately when no host is registered -
this gives the future `Open(..., DPOPEN_JOIN)` wiring task a clean synchronous signal to map
straight to `DPERR_NOSESSIONS` for the loopback backend, with no timeout machinery needed there.

Connection lifecycle otherwise mirrors `EnetDirectPlayTransport`'s pending/connected/disconnected
model exactly (Decisions 7/8/9), with `LoopbackDirectPlayTransport*` in place of `ENetPeer*`: a
hosting instance tracks `pendingPeers_`/`connectedPeers_`/`disconnectedPeerIds_`; a joining instance
tracks a single `hostPeer_`. Because these are direct C++ object pointers (not opaque handles),
methods on one instance freely reach into another connected instance's private state (e.g.
`RejectPendingConnection()` clears the rejected peer's own `hostPeer_` directly) - legal C++, since
access control is per-class, not per-instance, and simpler than adding friend declarations or new
public API.

**Sub-question 2: what do `Send()`/`Receive()` do once really connected?** `false` for **both**
roles (hosting and joining), diverging from `EnetDirectPlayTransport`'s asymmetry (whose joining
role has a real, working `Send()`, since its single `hostPeer_` is unambiguous). Real per-recipient
payload delivery - needed for the actual join-request/join-accepted wire handshake - is left as a
separate, later design decision (tracked as still-blocked in `NEXT.md`, pending per-DPID-addressed
`Send()`, `plan.md` Phase 10), not decided as an incidental side effect of connection-lifecycle
work. The pre-existing self-send path (`Send()`/`Receive()` operating on `buffered_` when neither
`Listen()` nor `Connect()` was ever called) is completely unaffected - the default, non-ENet build's
`Open()` never calls either method on its `LoopbackDirectPlayTransport`, so no currently-passing
test ever puts an instance into the connected state.

**Memory safety:** `Shutdown()`/the destructor scrub every known peer's reference back to null
symmetrically (a hosting instance nulls every `connectedPeers_`/`pendingPeers_` entry's `hostPeer_`;
a joining instance removes itself from its `hostPeer_`'s maps/queues and, if it had been assigned a
DPID, queues a disconnect notification exactly as Decision 8 specifies) - required so that either
destruction order (host-first or client-first) never leaves a dangling pointer, not merely to mirror
ENet's own graceful-teardown ceremony. Copy and move construction/assignment are deleted: unlike
`ENetPeer*`/`ENetHost*`, these instances hold raw pointers *to each other*, so a moved/copied
instance would leave stale pointers in whatever peer still references it.

No mutex guards the registry - unlike ENet's lifecycle-counter mutex (which guards a genuine
process-global side effect, `enet_initialize`/`enet_deinitialize`), the loopback registry is pure
in-test bookkeeping; every current DirectPlay call site and every existing test is single-threaded,
and this backend's whole purpose is deterministic testing, not modeling concurrency.

Three test-only public accessors (`HasHostConnection()`, `ConnectedPeerCount()`,
`PendingConnectionCount()`) were added, mirroring `EnetDirectPlayTransport`'s own test-only
accessors - not part of `IDirectPlayTransport`.

**Explicitly out of scope for this decision:** wiring `DirectPlay2AImpl::Open(...,
DPOPEN_JOIN/DPOPEN_OPENSESSION)` to actually call `transport->Connect()` (a separate follow-up task,
which also needs its own answer for how the ENet backend resolves a host address, since
`DPSESSIONDESC2` has no address-like field any more than it had a port-like one - see Decision 5);
and the join-request/join-accepted wire-protocol handshake itself (blocked on per-DPID-addressed
`Send()`, `plan.md` Phase 10, as noted above).

### Implemented

`src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp` - the static registry, real
`Listen()`/`Connect()`, real `HasPendingConnection()`/`AssignPendingConnection()`/
`RejectPendingConnection()`/`HasDisconnectedPeer()`/`TakeDisconnectedPeer()`, `Send()`/`Receive()`
returning `false` once connected, a memory-safe `Shutdown()`/destructor, deleted copy/move, and the
three test-only accessors above. **Verified** with eleven new committed whitebox tests in
`tests/directplay_tests.cpp` (27/27 total passing): connect-finds-host, connect-with-no-host-fails,
listen-on-taken-port-fails, assign-moves-pending-to-connected, reject-pops-and-then-fails-when-empty,
assigned-peer-disconnect-reported-exactly-once, pending-peer-disconnect-not-reported, a
third-client-over-a-two-player-cap-is-rejected test (the transport-level analog of Phase 7's own
acceptance criterion), host-shutdown-clears-peers'-connections, shutdown-unregisters-port-for-reuse,
and send/receive-false-for-both-roles. Also re-verified both CMake build configurations
(`ENET=OFF`/`ON`) end-to-end and that `include/dplay.h` has zero ENet/SDL identifiers.

---

## Decision 11: wiring `Open(..., DPOPEN_JOIN)` to `Connect()` over loopback - joining role only, hosting role deliberately not wired yet

**Status:** Decided and implemented. The fixed-port choice was asked of, and confirmed by, the
user directly (mirroring Decision 5's ENet port problem); the scope-narrowing (joining role only)
was discovered and self-corrected while implementing, not asked separately, because it fixes a
regression rather than opening a new design fork.

### The question

With Decision 10 giving `LoopbackDirectPlayTransport` real `Listen()`/`Connect()`, `plan.md` Phase
7's first task ("wire `Open(..., DPOPEN_JOIN)` to actually connect") needs `DirectPlay.cpp`'s
`Open()` to call `transport->Connect()`. But `Connect(address, port)` needs a `port` value, and
`DPSESSIONDESC2` has no port-like field to derive one from - the identical problem Decision 5 faced
for the ENet backend's `Listen()` call.

### Decision (port choice)

A single fixed constant, `kDefaultDirectPlayLoopbackPort = 51322`
(`src/directplay/LoopbackDirectPlayTransport.hpp`), mirroring Decision 5's
`kDefaultDirectPlayEnetPort` exactly - over a per-session-unique-port alternative, which was
rejected because nothing can yet tell a joining caller which unique port to use (no address field
on `DPSESSIONDESC2`, and `EnumSessions` doesn't exist yet, `plan.md` Phase 8) without inventing new
public API with no motivating call site - exactly the kind of speculative addition `CLAUDE.md`
rules out.

### The regression found while implementing, and the resulting scope correction

The literal task ("wire `Open()` to call `Connect()`") could be read as symmetrically wiring the
hosting role to call `Listen()` too (mirroring how Decision 5/Phase 6 wired the ENet hosting role).
**Implementing that symmetric change was tried and reverted before committing**, because it breaks
an already-working feature: once a hosted session's transport enters the "listening" state,
Decision 10 makes `Send()`/`Receive()` unconditionally return `false` on it - silently breaking the
self-send path every Phase 4 test (and `free-eggbert`'s own real self-send call pattern,
`idFrom == idTo`) depends on. Every existing hosting test opens a session and then self-sends
through the same transport instance; making `Open()` call `Listen()` there would have made all of
them fail non-obviously (`Send()` returning `DPERR_GENERIC` instead of `DP_OK`), a regression that
would not have been caught by this task's own new tests, only by the pre-existing suite.

**Decision: only the joining role's `Open()` branch calls `Connect()` in this task.** The hosting
role's `Open()` branch is deliberately left unchanged - it does not call `Listen()`. Reconciling
"a hosted session's transport is discoverable for real joins" with "that same session can still
self-send" is left as a separate, later design question (needed before Phase 7's "successful join"
test can be attempted), not decided here as an incidental side effect.

### Consequence

`Open(..., DPOPEN_JOIN)`/`DPOPEN_OPENSESSION` over loopback now calls
`transport->Connect(nullptr, kDefaultDirectPlayLoopbackPort)`. Since nothing calls `Listen()` on
that port yet (see above), this **always fails today** - which is the honestly-correct behavior
right now, not a bug: no loopback host is ever actually listening there yet. `Connect()` failing
maps directly to `DPERR_NOSESSIONS`, with no timeout needed (Decision 10: loopback `Connect()`
fails synchronously). The ENet backend's joining role remains completely unwired (untouched by this
task) - how it would resolve a host address is a separate, still-open question.

### Implemented

`src/directplay/DirectPlay.cpp`'s `Open()` (the `#else`/non-ENet branch): a joining call
(`!session_.isHost`) now calls `transport->Connect()`, resetting the transport and returning
`DPERR_NOSESSIONS` on failure. `kDefaultDirectPlayLoopbackPort` added to
`src/directplay/LoopbackDirectPlayTransport.hpp`. **Verified** with a new committed end-to-end test
in `tests/directplay_tests.cpp` (28/28 total passing), `Test_OpenAsJoinWithNoHostPresent_
ReturnsNoSessions`, going through the real public `IDirectPlay2A::Open()` (not whitebox) - asserts
`Open(&desc, DPOPEN_JOIN)` with no host ever started returns `DPERR_NOSESSIONS`. Also re-verified
both CMake build configurations (`ENET=OFF`/`ON`) end-to-end, that every pre-existing hosting/
self-send test still passes unaffected, and that `include/dplay.h` has zero ENet/SDL identifiers.

---

## Decision 12: self-send bypasses the transport entirely, resolving Decision 11's open question

**Status:** Decided and implemented. Asked of, and confirmed by, the user directly - two
candidate directions were identified (self-send moves off the transport onto
`session_.messageQueue` directly; or the transport gains a narrower carve-out permitting
self-send even while "listening"), and the user confirmed the first.

### The question

Decision 11 left open: how can a hosted loopback session's transport be both `Listen()`ing (so a
joining `Connect()` can find it) and still support the self-send path (`DirectPlay2AImpl::Send()`
with `idFrom == idTo`), given Decision 10 made `LoopbackDirectPlayTransport::Send()`/`Receive()`
return `false` unconditionally once "connected" (`listening_ || hostPeer_`)?

### The finding that resolves it

`IDirectPlayTransport::Send(const void* data, std::size_t size, bool reliable)` has **no**
recipient parameter at all - the transport is never told whether a given call is a self-send or
traffic meant for some other peer. That distinction exists only one layer up, in
`DirectPlay2AImpl::Send()`'s own `idTo == idFrom` check. This makes the "narrower transport-level
carve-out" direction structurally unworkable without adding a special-purpose method or a mode
flag to `IDirectPlayTransport` (new API surface with no other motivating need) - the fix belongs in
`DirectPlay2AImpl::Send()`, not in the transport.

### Decision

`DirectPlay2AImpl::Send()`'s self-send branch (`src/directplay/DirectPlay.cpp`) no longer calls
`session_.transport->Send()`/`Receive()` at all. It constructs a `DirectPlayMessagePacket` directly
from the caller's `lpData`/`dwDataSize` and enqueues it straight into `session_.messageQueue` -
the same queue `Receive()` already drains via `TryReceive()`. This is not merely a workaround: a
message sent to yourself is conceptually always a local operation, independent of whatever
network role (idle, hosting, joined) this session's transport is playing, so it should never have
depended on transport connection state in the first place. `DPSEND_GUARANTEED`/`dwFlags`'s
reliability distinction is dropped for this path (there is nothing to be reliable/unreliable *about*
for a same-process, in-memory queue write) - it is still stored on the packet
(`packet.flags = dwFlags`) unchanged from before.

This supersedes Phase 4's original rationale for routing self-send through the transport ("so
`LoopbackDirectPlayTransport`'s own `Send()`/`Receive()` are genuinely exercised, matching the
eventual shape of a real backend") - that rationale predates Decision 10's real connection
lifecycle work, and continuing to route self-send through the transport after Decision 10 would be
a correctness bug (self-send breaking the moment hosting is wired), not just a stylistic
mismatch.

### Consequence

It is now safe for `Open(..., DPOPEN_CREATE)` to call `transport->Listen(kDefaultDirectPlayLoopbackPort)`
over loopback (this was the whole point): hosting no longer puts the *self-send* path at risk,
since self-send no longer touches the transport. Wired as part of this same task (see Implemented,
below) - completing Decision 11's deferred half.

One test needed adjusting as a direct consequence of the fixed single port
(`kDefaultDirectPlayLoopbackPort`) now genuinely being enforced by a real `Listen()`:
`Test_OpenAsHostWithZeroGuidInstance_GeneratesNonZeroGuid` previously held two loopback-hosted
sessions open simultaneously to compare their generated `guidInstance` values - now only one
loopback-hosted session can exist per process at a time (the same constraint Decision 5 already
accepted for the real ENet port), so the test was changed to open/capture/close the first session
before opening the second. This doesn't weaken what the test actually verifies (GUID uniqueness
across generations), since the two sessions were never required to coexist for that assertion.

### Implemented

`src/directplay/DirectPlay.cpp`: `Send()`'s self-send branch now enqueues directly into
`session_.messageQueue`; `Open()`'s hosting branch (the `#else`/non-ENet path) now calls
`transport->Listen(kDefaultDirectPlayLoopbackPort)`, resetting the transport and returning
`DPERR_CANTCREATESESSION` on failure - mirroring the ENet branch's existing shape exactly.
`tests/directplay_tests.cpp`: adjusted `Test_OpenAsHostWithZeroGuidInstance_
GeneratesNonZeroGuid` as described above; added `Test_OpenAsJoinWithHostPresent_Succeeds`, a new
end-to-end test opening a real host session then a real joining session over loopback, asserting
`Open(..., DPOPEN_JOIN)` returns `DP_OK` once a host is genuinely listening - this covers Phase 7's
"successful join" task's connection-establishment half only; the full acceptance criterion ("both
peers agree on the assigned DPIDs and session descriptor") still needs the join-request/accepted
wire handshake, which remains blocked on per-DPID-addressed `Send()` (`plan.md` Phase 10). **Verified**:
29/29 `tests/directplay_tests.cpp` suite passes (every pre-existing hosting/self-send test
unaffected); both CMake build configurations (`ENET=OFF`/`ON`) build clean; `include/dplay.h` has
zero ENet/SDL identifiers.

---

## Decision 13: client-side rejection/disconnection observability via `IsConnectedToHost()`

**Status:** Decided and implemented. Asked of, and confirmed by, the user directly: add this
capability now, rather than leaving it blocked alongside the join-request/accepted wire-handshake
work.

### The question

Investigating `plan.md` Phase 7's "Add a test for max-players rejection" task found a real gap:
`DirectPlay2AImpl::Receive()`'s DPID-assignment/rejection loop (Decisions 7/9) is backend-agnostic
and already worked against `LoopbackDirectPlayTransport` once Decision 10 gave it a real
`HasPendingConnection()`/`AssignPendingConnection()`/`RejectPendingConnection()` implementation -
but nothing let the *rejected client itself* observe that it had been turned away.
`IDirectPlayTransport::HasDisconnectedPeer()`/`TakeDisconnectedPeer()` (Decision 8) are purely a
hosting-role concept (they report the host's own peers disconnecting); there was no equivalent for
a joining-role instance to ask "am I still connected to the host I `Connect()`ed to?" - even though
both backends already tracked exactly that internally (`LoopbackDirectPlayTransport`'s
`hostPeer_`, `EnetDirectPlayTransport`'s `hostPeer_`, both already exposed only as test-only
accessors, `HasHostConnection()`/`HasPeer()`).

### Decision

Promoted the existing test-only accessors into a real `IDirectPlayTransport` method:
`virtual bool IsConnectedToHost() const = 0;`. `LoopbackDirectPlayTransport::IsConnectedToHost()`
and `EnetDirectPlayTransport::IsConnectedToHost()` both simply return `hostPeer_ != nullptr` -
exactly what their now-removed `HasHostConnection()`/`HasPeer()` test-only accessors already did;
this is a rename/promotion, not new logic. A hosting-role instance's `hostPeer_` stays null
forever (Decision 7), so `IsConnectedToHost()` correctly always returns `false` for it - "am I
connected to a host" doesn't apply to a hosting instance itself.

`DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) now checks this for the joining role
(`!session_.isHost`), but **only after** `TryReceive()` on the local message queue has already
been attempted, and **only** if that attempt came back `DPERR_NOMESSAGES`:

```cpp
const HRESULT hr = session_.messageQueue.TryReceive(lpidFrom, lpidTo, lpData, lpdwDataSize);
if (session_.transport && !session_.isHost && hr == DPERR_NOMESSAGES &&
    !session_.transport->IsConnectedToHost()) {
    return DPERR_NOCONNECTION;
}
return hr;
```

This ordering is deliberate and was not part of the literal question as first asked - it is a
direct consequence of Decision 12: self-send now enqueues straight into `session_.messageQueue`,
completely independent of transport connection state, so a joining session's own earlier self-sent
message must still be deliverable via `Receive()` even after its host connection is gone. Checking
`IsConnectedToHost()` unconditionally (before consulting the queue) would have silently broken
that guarantee - exactly the kind of self-send-vs-connection-state coupling Decision 12 was
written to eliminate. Only once the local queue is genuinely empty does the lost host connection
become observable, mapped to `DPERR_NOCONNECTION` (the same code `Close()` already uses for "no
connection" - no new error code was needed; there is still no join-rejected explanation packet, so
FreeDirect cannot distinguish "rejected for being over `dwMaxPlayers`" from "the host disconnected
for some other reason" - both look identical to the client today, matching Decision 9's existing
caveat).

### Implemented

`src/directplay/DirectPlayTransport.hpp` (`IsConnectedToHost()` added to `IDirectPlayTransport`),
`src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp` and
`src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` (both implement it, replacing their old
test-only `HasHostConnection()`/`HasPeer()` accessors - all call sites in
`tests/directplay_tests.cpp` renamed accordingly), and `src/directplay/DirectPlay.cpp`'s
`Receive()` as shown above. **Verified** with a new committed end-to-end test in
`tests/directplay_tests.cpp` (30/30 total passing),
`Test_OpenAsJoinOverMaxPlayers_ThirdClientReceivesNoConnection`: a real host (`dwMaxPlayers = 2`)
and three real joining clients connect over loopback; the host's own `Receive()` call (which is
what actually runs the assignment/rejection loop, per Decision 6's polling model) admits the first
two and rejects the third; the third client's own subsequent `Receive()` call returns
`DPERR_NOCONNECTION` - this finally closes Phase 7's "max-players rejection" task. Also
re-verified both CMake build configurations (`ENET=OFF`/`ON`) end-to-end and that `include/dplay.h`
has zero ENet/SDL identifiers.

---

## Decision 14: `IDirectPlayTransport::Send()` gains a `targetId` parameter for real peer-addressed delivery

**Status:** Decided and implemented. Asked of, and confirmed by, the user directly: extend
`Send()`'s existing signature with a `DPID targetId` parameter (over adding a separate new
`SendTo()` method alongside the unchanged old `Send()`).

### The question

`plan.md` Phase 10's first task, "Implement `Send` from a local player to a specific remote
player, routed through the configured transport," needs a way for a hosting-role transport
instance (with potentially many `connectedPeers_`) to address one specific recipient - something
`Send()`'s original signature (`Send(const void* data, std::size_t size, bool reliable)`) has never
supported, going all the way back to Decision 7's explicit deferral ("real per-DPID-addressed send
is Phase 10's job, not this one's"). Before touching this, confirmed that `session_.transport
->Send()`/`->Receive()` have **zero callers left in `DirectPlay.cpp`** - Decision 12 removed the
only one (the self-send round-trip) - so this interface change breaks no production call site,
only whitebox test call sites.

### Decision

`Send(DPID targetId, const void* data, std::size_t size, bool reliable)`. Over a separate
`SendTo()` method: `Send()` was already completely unused by production code, so leaving its old
signature around unused while adding a parallel addressed method would be dead API surface for no
benefit - a straight signature change is simpler and leaves nothing stale behind.

`targetId` is DPID-shaped but the transport never validates or allocates it (same rule as every
other DPID-shaped parameter on this interface, e.g. `AssignPendingConnection(DPID id)`) - it is
purely an opaque lookup key into `connectedPeers_`. Semantics by role:
- **Hosting role** (`connectedPeers_` populated): `targetId` is looked up in `connectedPeers_`;
  `false` is returned if no peer is currently assigned that id.
- **Joining role** (`hostPeer_` set): there is exactly one possible destination - the host itself
  - so `targetId` is accepted (for interface uniformity) but ignored.
- **Self-send-only mode** (neither role - `Listen()`/`Connect()` never called): unaffected,
  `targetId` is accepted but ignored, same as the joining role.

`LoopbackDirectPlayTransport` implements real delivery for all three modes now, by giving every
instance one inbox (`buffered_`, the same FIFO the self-send-only mode already had) and having
`Send()` reach into the *target instance's* `buffered_` directly - the hosting role via
`connectedPeers_[targetId]->buffered_`, the joining role via `hostPeer_->buffered_`. This is the
same "reach into another instance's private state directly" mechanism Decision 10 already
established for `Connect()`/`RejectPendingConnection()`/`Shutdown()`, applied one more place.
`Receive()` no longer has any role-based guard - it unconditionally pops from `this->buffered_`,
since by construction only legitimate senders (addressed via `targetId`, or the sole `hostPeer_`)
ever push into it.

`EnetDirectPlayTransport::Send()` gained the equivalent `connectedPeers_.find(targetId)` lookup for
the hosting role (falling back to `hostPeer_` first, since a hosting instance's `hostPeer_` is
always null - Decision 7 - so this order is unambiguous). **Receive-side delivery over ENet is
deliberately left unimplemented in this same task** (loopback-first, matching this session's
established pattern - Decisions 10/11/12/13 all did the loopback side of a capability before, or
instead of, the ENet side): `Service()` still discards `ENET_EVENT_TYPE_RECEIVE` packets rather
than buffering them, so `EnetDirectPlayTransport::Receive()` still unconditionally returns `false`
for every role. A real ENet host can now genuinely *send* a packet to one specific connected
client, but nothing can *receive* it yet on either backend's ENet side - a real, separate, still-
open task, not a silent gap.

### Implemented

`src/directplay/DirectPlayTransport.hpp` (`Send()`'s new `targetId` parameter, documented),
`src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp` (real addressed delivery for all three
modes, `Receive()`'s guard removed), `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` (`Send()`'s
`targetId` lookup added; `Receive()` unchanged). `tests/directplay_tests.cpp`: replaced the now-
obsolete `Test_LoopbackSend_ReturnsFalseForHostingAndJoiningRoles` (its entire premise - `Send()`
always fails once connected - no longer holds) with three new tests -
`Test_LoopbackSend_HostToAssignedClient_DeliversPayload`, `Test_LoopbackSend_ClientToHost_
DeliversPayload`, `Test_LoopbackSend_ToUnknownTargetId_Fails`. **Verified**: 32/32 `tests/
directplay_tests.cpp` suite passes; both CMake build configurations (`ENET=OFF`/`ON`) build clean;
`include/dplay.h` has zero ENet/SDL identifiers.

Not yet done, and explicitly out of scope for this task: wiring `DirectPlay2AImpl::Send()`/
`Receive()` (`DirectPlay.cpp`) to actually use this new transport capability for non-self sends -
constructing/parsing `DirectPlayWirePacketHeader`s, looking up a recipient's DPID against
`remotePlayerIds`, host-side routing/broadcast, and the remaining `plan.md` Phase 10 checklist
items. This decision covers transport-layer groundwork only, mirroring how Decision 10 gave
connection lifecycle at the transport layer before `DirectPlay.cpp` was wired to use it
(Decision 11).

---

## Decision 15: wiring `DirectPlay2AImpl::Send()`/`Receive()` to real per-DPID delivery, host role only

**Status:** Decided and implemented, without a fresh user question - this is the direct,
previously-scoped continuation of Decision 14 ("this decision covers transport-layer groundwork
only... [Send()/Receive() wiring] is the next task, not yet started"), not a new architectural
fork.

### The question

Decision 14 gave `IDirectPlayTransport` real addressed delivery, but `DirectPlay2AImpl::Send()`/
`Receive()` (`DirectPlay.cpp`) still only handled the `idTo == idFrom` self-send case (Decision
12). `plan.md` Phase 10's first task asks for real delivery "from a local player to a specific
remote player."

### The finding that scoped it down to host-only

`docs/directplay-callsite-audit.md` confirms `free-eggbert`'s only real `Send()` call site is
`Send(m_dpid, 0, ...)` - **always a broadcast to `idTo == 0`, never a specific named remote
DPID**. So this task's own scenario (a local player addressing one specific remote player) doesn't
literally match any known call site - it is Phase 10's own necessary building block (broadcast, a
later checklist item, is naturally "send to each known remote player," so the unicast primitive has
to exist first regardless of whether free-eggbert calls it directly). Implementing it now is
`plan.md`-scheduled groundwork, not speculative scope.

More importantly: only the **hosting** role can meaningfully validate/address "a specific remote
player" at all today. A joining-role session has no way to learn *any* remote DPID yet - not the
host's own local player ID, not any other client's - because that information only ever arrives via
the join-accepted handshake, which remains blocked on this same Send()/Receive() capability
(circular - Phase 7's remaining tasks and Phase 9's player-list sync both depend on delivery
existing first, in exactly the order these decisions are landing). So for now, a joining role's
attempt to `Send()` to anything other than itself is honestly `DPERR_INVALIDPLAYER` - "not
supported yet," not a silently-wrong success.

**Also found, and worth flagging for whoever implements broadcast next:** Decision 3 chose to
assign DPID `0` to the host's own first local player (not reserve it as `DPID_ALLPLAYERS`, unlike
real DirectPlay) specifically to match `free-eggbert`'s comparison pattern. This means
`free-eggbert`'s own broadcast call, `Send(m_dpid, 0, ...)`, is genuinely ambiguous under
FreeDirect's current DPID semantics: is `0` "broadcast to everyone," or "the specific player whose
DPID happens to be `0`" (typically the host)? This decision does not resolve that ambiguity - it
is explicitly out of scope here (broadcast is untouched) - but it is a real, concrete question the
broadcast task will have to answer, not an invented one.

### Decision

`Send(idFrom, idTo, dwFlags, lpData, dwDataSize)` for `idTo != idFrom`:
1. `idFrom` must be in `session_.localPlayerIds`, or `DPERR_INVALIDPLAYER` (applies to both
   roles - `plan.md`'s own "validate the sender player ID" task).
2. If not hosting, or no transport: `DPERR_INVALIDPLAYER` (the joining-role limitation above).
3. `idTo` must be in `session_.remotePlayerIds` (an already-assigned remote player - Decision 7/9's
   bookkeeping), or `DPERR_INVALIDPLAYER` (`plan.md`'s "validate the recipient player ID" task).
4. `dwDataSize` must not exceed `DirectPlayMessageQueue::kMaxPayloadBytes`, or
   `DPERR_SENDTOOBIG` - enforced here, before the transport, not just as a `Receive()`-side
   nicety: an oversized packet that made it onto the wire would arrive larger than `Receive()`'s
   fixed-size read buffer (see below) and get stuck at the front of the receiver's queue forever,
   wedging every message behind it. This folds in `plan.md`'s own "reject messages larger than the
   maximum payload size" task, since Decision 14's new delivery path would otherwise ship with that
   exact landmine already armed.
5. A `DirectPlayWirePacketHeader` (`idFrom`/`idTo`/`payloadLength`/`applicationGuid`/
   `sessionGuid`, `type` left at its `Data` default) is serialized (`DirectPlayWireProtocol.hpp`,
   built in Phase 5, unused by any real caller until now) and the payload appended; the whole
   buffer is handed to `session_.transport->Send(idTo, ...)`, `reliable` derived from
   `DPSEND_GUARANTEED` exactly as the self-send path already does.

`Receive()` gains a drain loop, for **both** roles, right before the existing
`messageQueue.TryReceive()` call: repeatedly calls `session_.transport->Receive()` into a
fixed-size buffer (`kDirectPlayWireHeaderSize + DirectPlayMessageQueue::kMaxPayloadBytes`,
generous enough for anything `Send()`'s own size check above allows through), deserializes each
blob via `TryDeserializeDirectPlayWireHeader`, and enqueues a `DirectPlayMessagePacket` into
`session_.messageQueue`. A blob that fails to deserialize (too small, or a `payloadLength` that
disagrees with what actually arrived) is dropped silently, matching the defensive posture that
function was already built for in Phase 5. A full `messageQueue` also drops the packet silently
(`Enqueue()`'s existing bounded-growth behavior, unchanged) - there is no send-side
acknowledgement/backpressure channel to report the drop through yet.

### Implemented

`src/directplay/DirectPlay.cpp`: `Send()`'s new validation + serialization + `transport->Send()`
call for `idTo != idFrom`; `Receive()`'s new drain loop. **Verified** with five new committed
end-to-end tests (via the real public `IDirectPlay2A` API, not whitebox) in
`tests/directplay_tests.cpp` (37/37 total passing):
`Test_SendToSpecificRemotePlayer_HostDeliversToAssignedClient` (a real host + a real joining
client; the host creates its own local player - deterministically DPID `0`, Decision 3 - `Receive()`s
once to assign the connecting client DPID `1`, `Send()`s to it, and the client's own `Receive()`
gets the exact payload back, with `idFrom`/`idTo` matching), `Test_SendToUnknownRemotePlayer_
ReturnsInvalidPlayer`, `Test_SendFromUnknownLocalPlayer_ReturnsInvalidPlayer`,
`Test_JoiningRoleSendToNonSelf_ReturnsInvalidPlayer`, and
`Test_SendOversizedPayloadToRemotePlayer_ReturnsSendTooBig`. Also re-verified both CMake build
configurations (`ENET=OFF`/`ON`) end-to-end and that `include/dplay.h` has zero ENet/SDL
identifiers.

**Explicitly out of scope, left for later `plan.md` Phase 10 tasks:** host-side routing/forwarding
of a `Send` a non-host peer addresses to another non-host peer (star-topology relay - today only
"host directly addresses one of its own `remotePlayerIds`" works, a joining peer still cannot reach
any other peer at all); broadcast delivery for `idTo == 0`/`DPID_ALLPLAYERS` (including resolving
the DPID-0-ambiguity finding above); packet-ordering and reliable-delivery-focused tests; the ENet
backend's receive-side buffering (still blocked since Decision 14, `Service()` still discards
`ENET_EVENT_TYPE_RECEIVE`).

---

## Decision 16: the join-request/join-accepted handshake, over loopback, asynchronous (not Open()-blocking)

**Status:** Decided and implemented. The synchronous-vs-asynchronous question was asked of, and
confirmed by, the user directly; the remaining mechanics (packet shape, where DPID adoption
happens, what "session descriptor" means here) followed from that answer and from Decisions
10-15's already-established patterns, not from fresh questions.

### The question

`plan.md` Phase 7's remaining tasks - send a join-request, receive a join-accepted packet and the
host-assigned DPID, store the session descriptor, handle a join timeout - all became reachable
once Decision 15 gave `Send()`/`Receive()` real delivery. The central question: does
`Open(..., DPOPEN_JOIN)` block internally until a join-accepted/rejected packet arrives (matching
real DirectPlay's synchronous `Open()` contract, and `plan.md`'s own "handle a join timeout: fail
`Open`..." wording), or does it return immediately once the raw connection exists, with the actual
join outcome discovered later via `Receive()` polling (matching this project's already-established
Decision 6 model)?

### The finding that resolved it

`docs/directplay-callsite-audit.md` (via `NEXT.md`'s own already-recorded finding) confirms
`../free-eggbert`'s own `Open(DPOPEN_JOIN)`/`CreateSession`/`JoinSession` call path is entirely
**dead code** - `NetCreate`/`NetEnumSessions`/`JoinSession`/`CreateSession`/`NetStartPlay` have zero
callers anywhere in the game's current source. So there is no real, observed call-site behavior to
match for `Open()`'s blocking contract specifically - this is FreeDirect-internal mechanism
territory, like Decisions 4/5/6.

Separately, and decisively for the loopback backend regardless of the above: a blocking `Open()`
that waits for the *host* to independently call its own `Receive()` (per Decision 6's polling
model, nothing happens on a hosted session until something calls `Receive()` on it) cannot work in
a single-threaded test process. There is no second thread to service the host while the client
"blocks" - the host object and the client object are both driven by the same call stack in every
committed test. A blocking `Open()` would either hang forever in that setup or require some
mechanism for `Open()` to reach across and drive the *other* object's `Receive()` itself, which
would break the transport abstraction (a joining transport instance has no business calling
methods on the specific `DirectPlay2AImpl` that happens to own the host side).

### Decision

Asynchronous, matching Decision 6's polling model exactly. `Open(..., DPOPEN_JOIN)`:
1. Calls `Connect()` (Decision 11) - unchanged.
2. Sends a `DirectPlayWirePacketType::Join` packet to the host, fire-and-forget, via
   `session_.transport->Send(0, ...)` directly (not through the public, validated `Send()` API -
   this session has no DPID yet, which is exactly what the response will provide, so there is no
   `idFrom` to validate against).
3. Returns `DP_OK` immediately - the same return value as before this decision. No `DPERR_TIMEOUT`
   is produced by this backend, mirroring Decision 10/11's already-established position that
   loopback's synchronous `Connect()` needs no timeout concept at all.

The host's existing DPID-assignment loop (Decision 7, inside `Receive()`) is **unchanged in its
trigger condition** - it still assigns DPIDs to any pending connection, regardless of whether a
`Join` packet was ever received from it. (The `Join` packet's practical function today is
therefore mostly protocol-shape completeness/future-proofing for a real network backend, where
connection-level accept and DirectPlay-level join are more likely to genuinely need to be separate
events - loopback's connection lifecycle is already fully known out-of-band via Decision 10's
direct instance-pointer tracking, with or without any wire message.) Immediately after each
successful `AssignPendingConnection()`, the host now additionally sends a
`DirectPlayWirePacketType::JoinAccept` packet - `applicationGuid`/`sessionGuid` from the header,
`idTo` set to the newly-assigned DPID, no payload - addressed via `transport->Send(newId, ...)`,
which now works because `connectedPeers_[newId]` exists (Decision 14).

`Receive()`'s existing drain loop (Decision 15) gains a `switch` on `header->type`: `Data` behaves
exactly as before (enqueued to `session_.messageQueue`); `JoinAccept`, for a joining role only,
adopts the assigned DPID into `session_.localPlayerIds` (if not already present) and bumps
`session_.nextPlayerId` past it (avoiding a future local `CreatePlayer()` collision with the
host-given identity), and overwrites `session_.applicationGuid`/`sessionInstanceGuid` with the
header's values - the closest match, given `DirectPlaySession` deliberately never retains a raw
`DPSESSIONDESC2` (see its own class comment), to "store the session descriptor received from the
host." `dwMaxPlayers`/session name/password are **not** synced - no current consumer needs them on
the joining side, and inventing storage for them now would be exactly the speculative completeness
`CLAUDE.md` warns against. `Join` (host-side, ignored - see above), `JoinReject`, `Discovery`,
`DiscoveryResponse` fall through a `default:` case and are silently consumed without effect - none
are implemented yet.

**A latent validation gap found and closed in passing:** the self-send path (`idTo == idFrom`,
Decision 12) never validated `idFrom` against `session_.localPlayerIds` at all, unlike the unicast
path Decision 15 just added - an inconsistency, not a deliberate choice. Fixed here (`Send()`
returns `DPERR_INVALIDPLAYER` for an unregistered `idFrom` on the self-send path too), which
incidentally also makes DPID adoption observable in a committed test: since `IDirectPlay2A` has no
public getter for a session's own DPID, the test proves adoption happened by showing self-send with
the host-assigned id now succeeds specifically because it is genuinely in `localPlayerIds` - not a
free pass that would have succeeded regardless.

**Deliberately still out of scope**, consistent with Decision 9's existing note: sending a real
`JoinReject` explanation packet when a pending connection is turned away for being over
`dwMaxPlayers`. A rejected `pendingPeers_` entry is, by definition, never assigned a DPID
(`RejectPendingConnection()` pops it without ever calling `AssignPendingConnection()`), and
`Send()`'s addressing (Decision 14) only ever reaches `connectedPeers_` entries - so there is
structurally no DPID to address a `JoinReject` packet to, regardless of anything decided here. The
same is true for GUID-mismatch rejection, which additionally isn't validated anywhere yet at all
(`Open()` never compares the joining caller's `guidApplication` against the host's).

### Implemented

`src/directplay/DirectPlay.cpp`: `Open()`'s joining branch sends a `Join` packet after a successful
`Connect()`; the assignment loop in `Receive()` sends a `JoinAccept` packet per newly-assigned
peer; `Receive()`'s drain loop now switches on packet type; `Send()`'s self-send branch validates
`idFrom`. **Verified** with a new committed end-to-end test (via the real public `IDirectPlay2A`
API, not whitebox) in `tests/directplay_tests.cpp` (38/38 total passing),
`Test_JoinHandshake_ClientAdoptsHostAssignedDpid`: a real host + a real joining client; the host's
own `Receive()` call processes the join-request (dropped) and sends `JoinAccept`; the client's own
`Receive()` call adopts the assigned DPID; the client's self-send with that id succeeds while an
unadopted id fails with `DPERR_INVALIDPLAYER` (proving adoption); the host can still reach the
client by the same id it assigned, exactly as Decision 15 already demonstrated. Also re-verified
both CMake build configurations (`ENET=OFF`/`ON`) end-to-end, that every pre-existing test still
passes unaffected, and that `include/dplay.h` has zero ENet/SDL identifiers.

**Explicitly out of scope, left for later work:** `JoinReject` delivery (structurally blocked, see
above, until pending-peer addressing exists - a bigger change than this decision's scope);
GUID-mismatch validation; host-side routing/forwarding and broadcast (`plan.md` Phase 10's own
remaining tasks, unaffected by this decision); the ENet backend's side of any of this (its
receive-side buffering remains unimplemented, Decision 14).

---

## Decision 17: `CreatePlayer` validates against `dwMaxPlayers`; player-name storage deferred as unobservable

**Status:** Decided and implemented, without a fresh user question for the cap-validation part
(a small, self-contained, already-scoped `plan.md` task). The player-name-storage finding below
is flagged, not decided - it needs its own future conversation before any code lands.

### The question

`plan.md` Phase 9's first reachable tasks: validate `CreatePlayer()`'s player count against
`dwMaxPlayers`, and store each player's short/long name (`DPNAME.lpszShortNameA`/`lpszLongNameA`).
`../free-eggbert/src/network.cpp:183-185,231-233` (both `JoinSession` and `CreateSession`)
confirm a real, concrete pattern: `name.lpszShortNameA = pPlayerName; name.lpszLongNameA = NULL;
m_pDP->CreatePlayer(&m_dpid, &name, NULL, NULL, 0, 0);` - a real short name is always supplied, the
long name/data/event-handle parameters are always empty/null, matching what `plan.md`'s own
"only if a concrete call site needs it" caveats on those fields already anticipated.

### The finding that split this into "do now" vs "flag and defer"

`docs/directplay-callsite-audit.md` already establishes that `free-eggbert`'s `CreatePlayer()` call
sites are only reachable through `JoinSession`/`CreateSession`, which have **zero callers**
anywhere in the game's current source (the same dead-UI-flow caveat `CLAUDE.md`'s DirectPlay Policy
and `NEXT.md`'s "Known bugs" section already record for `Open()` and `EnumSessions()`). This was
already a known, accepted condition of working on this subsystem at all - not a new problem - but
re-checking `include/dplay.h` while scoping this task surfaced a *new* consequence specific to name
storage: **`IDirectPlay2A` has no `GetPlayerName`-style method at all** (confirmed absent from the
header, and confirmed `free-eggbert` never calls one either -
`docs/directplay-callsite-audit.md` §1: "No group methods, no lobby methods, no
`SetSessionDesc`/`GetPlayerName`/etc."). If a player's name were stored today, there would be
**no way for anything - test or real caller - to ever observe it**, since `DirectPlaySession` is
only reachable through the anonymous-namespace `DirectPlay2AImpl` (no whitebox path either, unlike
`LoopbackDirectPlayTransport`, which has its own installable header tests can include directly).
Storing a value that can never be read back is not "partially done, observable behavior TBD" like
earlier partial decisions in this document - it would be genuinely untestable, dead state,
violating this project's Testing Policy (`CLAUDE.md`: "New behavior... must ship with tests
covering at least the success path"). Adding a `GetPlayerName`-shaped method to make it observable
would itself be new public API surface with no `free-eggbert` call site behind it - exactly the
kind of speculative addition `CLAUDE.md` requires asking the user about first, not something to
decide as an implementation detail of a "store the name" task.

### Decision

**Implemented now**: `CreatePlayer()` validates `session_.currentPlayers` against
`session_.maxPlayers` before allocating a new DPID, returning `DPERR_CANTCREATEPLAYER` when full -
`dwMaxPlayers == 0` still means "no limit" (Decision 9's existing convention, reused verbatim). A
local `CreatePlayer()` call counts against the same cap `Receive()`'s remote-assignment loop
already enforces, since `dwMaxPlayers` bounds the session's total player count, not just remote
ones. This is fully observable through the existing public `IDirectPlay2A::CreatePlayer()` return
code - no new API surface, no design fork, safe to implement directly.

**Flagged, not implemented**: short/long name storage (`plan.md`'s next two Phase 9 checkboxes).
Left undone pending a future conversation with the user about how (or whether) to make stored names
observable - candidate directions to present then, not decided now: (a) add a narrow
`GetPlayerName`-equivalent as a deliberate, asked-for exception to the two-game scope rule
(`CLAUDE.md` already requires this ask); (b) store names anyway, accepting they can only ever be
verified by a future whitebox mechanism if `DirectPlaySession` is ever restructured to be
test-reachable (a bigger, separate architectural change, not something to fold into "store a
name"); (c) decide name storage genuinely isn't worth doing until a real consumer exists, striking
the task with a documented reason (`plan.md`'s own policy: "do not delete tasks that turn out to be
unnecessary; strike them through or annotate them instead").

### Implemented

`src/directplay/DirectPlay.cpp`'s `CreatePlayer()`: the `dwMaxPlayers` cap check, placed after the
existing `dwSize` validation and before DPID allocation. **Verified** with two new committed tests
in `tests/directplay_tests.cpp` (40/40 total passing): `Test_CreatePlayerOverMaxPlayers_
ReturnsCantCreatePlayer` (`dwMaxPlayers = 1`; a second `CreatePlayer()` call returns
`DPERR_CANTCREATEPLAYER`) and `Test_CreatePlayerWithNoMaxPlayersLimit_NeverRejects` (`dwMaxPlayers`
left at its default `0`; three `CreatePlayer()` calls all succeed - confirmed through
`CreatePlayer()` specifically, not just `Receive()`'s existing remote-assignment-loop coverage of
the same "no limit" convention). Also re-verified both CMake build configurations (`ENET=OFF`/`ON`)
end-to-end, that every pre-existing test still passes unaffected, and that `include/dplay.h` has
zero ENet/SDL identifiers.

### Amendment: closing out the rest of `plan.md` Phase 9's reachable work

Continuing this decision's own reasoning (real call site vs. unobservable state) across the rest
of Phase 9's task list, worked autonomously per the user's direction to proceed through the
remaining ordered tasks:

- **"Add a test asserting player removal... updates `dwCurrentPlayers`"**: `dwCurrentPlayers`
  itself has no public getter (the identical observability gap this decision already found for
  player names). Proven *indirectly* instead, reusing this decision's own cap check as the
  observable: `Test_RemotePlayerDisconnect_DecrementsCurrentPlayers`
  (`tests/directplay_tests.cpp`) - a host at `dwMaxPlayers = 1` (filled by one assigned remote
  player) rejects a local `CreatePlayer()` with `DPERR_CANTCREATEPLAYER`; once that remote player
  disconnects and the host's `Receive()` processes it (Decision 8), the *same* `CreatePlayer()`
  call succeeds - which could only happen if `dwCurrentPlayers` genuinely went back down. The
  "...removes the player from future `EnumSessions`/roster queries" half of this task is not
  covered - no roster-query API exists in this narrow `IDirectPlay2A` subset, and `EnumSessions()`
  itself is Phase 8, not started. **Verified**: 41/41 `tests/directplay_tests.cpp` suite passes;
  both CMake configs (`ENET=OFF`/`ON`) build clean; `include/dplay.h` has zero ENet/SDL
  identifiers.
- **"Validate against duplicate players"**: found, while scoping this, that the task doesn't have
  a concrete meaning to implement against. `CreatePlayer()`'s sequential DPID allocator
  (Decision 3) makes every call's assigned DPID unique by construction - there is no way for two
  `CreatePlayer()` calls to collide on identity at all, and no other field (no caller identity
  concept beyond the DPID a call *produces*, since `DPNAME` is caller-supplied and never validated
  against existing players) that could define "the same peer calling twice." `free-eggbert` itself
  only ever calls `CreatePlayer()` once per `CNetwork` instance (confirmed in
  `docs/directplay-callsite-audit.md`), so there's no real call-site behavior to match either.
  **Not implemented** - flagged as needing a concrete definition of "duplicate" from the user
  before any code could meaningfully target it, not silently skipped.
- **"Implement a player-lost state... distinct from a clean removal"**: hits the exact same
  observability wall as player names and `dwCurrentPlayers` - there is no public way for anything
  to ever query "is this player lost vs. cleanly removed" (no system messages, Phase 9's own
  "only if a concrete call site needs it" caveat on those - correctly not implemented, per Phase 0's
  audit finding no dependency). Adding an internal-only distinction with no way to observe it would
  be the same "dead, untestable state" problem the name-storage finding already identified. **Not
  implemented** - needs the same kind of observability conversation as player names before
  proceeding.
- **Player data bytes / event handle / player-created / player-destroyed system messages** (four
  separate Phase 9 checkboxes, all already worded "only if a concrete call site needs it"):
  Phase 0's audit already found no `free-eggbert` dependency on any of these. Left as-is in
  `plan.md` (not struck through) - annotating them as "confirmed not needed" is a `plan.md` edit
  the user should see and confirm, not something to make silently, even though the underlying
  finding is already on record.

With this, `plan.md` Phase 9 has no further reachable tasks: every remaining checkbox either needs
a scope/observability question resolved with the user first, or is already correctly marked
conditional on a call site that Phase 0 found does not exist.
