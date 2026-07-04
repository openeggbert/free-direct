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
