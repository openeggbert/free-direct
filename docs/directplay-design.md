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

**Status:** Decided, **not yet implemented**. This document records the decision only, per
`plan.md` Phase 9's acceptance criteria ("the DPID-vs-index conflict has an explicit written
decision in `docs/directplay-design.md` merged *before* any other Phase 9 code lands"). Two
separate pieces of code still need to change before this decision is actually in effect:
`include/dplay.h`'s `DPID` typedef (currently `DWORD_PTR`), and
`DirectPlaySession::nextPlayerId`'s placeholder initial value (currently `1`) — both are real
Phase 9 (or a dedicated preparatory) tasks, not done here.

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

### When this gets implemented

`plan.md` Phase 9 ("Implement stable DPID allocation... Reserve an invalid DPID value... and
explicitly resolve the conflict"): change `include/dplay.h`'s `DPID` typedef to `DWORD`, and
change `DirectPlaySession::nextPlayerId`'s initial value from the current placeholder `1` to `0`.
Both changes should land together, since a `DPID` `0` returned by a still-8-byte-wide `DPID`
carries no benefit and only the size-plus-value combination together satisfies `free-eggbert`'s
observed assumptions. `src/directplay/DirectPlayWireProtocol.hpp`'s wire header (`plan.md` Phase 5)
serializes `idFrom`/`idTo` using the local `sizeof(DPID)` as-is, so its wire format changes with
this decision automatically — no separate wire-protocol task is needed for that.

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
end-to-end.
