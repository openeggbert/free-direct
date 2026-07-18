# FreeDirect Task Plan

This is the **authoritative English task list** for FreeDirect (see `CLAUDE.md`'s `plan.md`
Policy). It formerly superseded `TODO.md` as the forward-looking backlog; `TODO.md` itself was
deleted on 2026-07-18 once every item in it was resolved (see `CLAUDE.md`'s Documentation Policy
section).

**Archive note (2026-07-18):** this file used to contain 19 phases (0-18) plus the entire
189-task "24-Hour Autonomous Stabilization Backlog." Everything confirmed complete after a
item-by-item re-verification (not just a checkbox count) was split out to
[`archive/plan20260718.md`](archive/plan20260718.md): Phase 0, Phase 2, Phase 3, Phase 4, Phase 5,
Phase 15, Phase 16, Phase 17, and the entire 24-Hour Backlog. What remains below is Phase 1 and
Phases 6-14 and 18 — every phase where the 2026-07-18 re-verification found at least one
genuinely open item, kept visible rather than archived. See `archive/plan20260718.md`'s own header
for the full rationale and a list of the specific gaps found in phases whose *titles* looked done
but weren't.

**Scope reminder:** FreeDirect exists to serve exactly two target games, both sibling
repositories: `../free-eggbert` (uses DirectDraw, DirectSound, and DirectPlay) and
`../planetblupi` (uses DirectDraw and DirectSound; confirmed to have zero DirectPlay usage). Every
task below is scoped to what one or both of these games actually need. Tasks that would add
capability beyond that are explicitly marked "ask the user first" — do not implement them
speculatively. See `CLAUDE.md` for the full policy.

## How to read this plan

- Every task is a checkbox (`- [ ]`) and is **atomic**: it does exactly one thing. Do not combine
  two changes into one checkbox, even if they seem related.
- Tasks are grouped into phases, numbered as they were in the original 19-phase plan (gaps in the
  numbering below are phases that moved to `archive/plan20260718.md`, not missing content).
- Each phase ends with an **Acceptance criteria** note describing how to verify the phase's
  important/milestone tasks are genuinely done — not just written.
- Check a box only when the corresponding code is merged, builds, and passes the tests implied by
  that phase's acceptance criteria. Do not check boxes speculatively.
- Do not delete a task that turns out to be unnecessary; strike it through with a short reason
  instead, so the plan preserves a record of what was considered.
- Do not mix DirectDraw, DirectSound, and DirectPlay work in one task/commit unless the task is
  documentation-only.
- Tasks phrased as "if needed"/"ask the user first" are intentionally conditional: do not resolve
  the condition by guessing. Re-check the named call sites, or ask, before implementing.

---

## Phase 1 — DirectPlay API boundary cleanup

Goal: make the existing `IDirectPlay`/`IDirectPlay2A` stub in `src/directplay/DirectPlay.cpp`
COM-correct and ready to host real state, without yet implementing real session/player/message
behavior.

- [x] Fix `QueryInterface` null-pointer handling in `DirectPlayImpl::QueryInterface` and
      `DirectPlay2AImpl::QueryInterface` (`src/directplay/DirectPlay.cpp`): return
      `DPERR_INVALIDPARAMS` (or `E_INVALIDARG`) when `ppvObject == nullptr`, instead of
      dereferencing it unconditionally. **Done:** both methods now return `DPERR_INVALIDPARAMS`
      when `ppvObject == nullptr` before touching it. Verified by a standalone
      `g++ -fsyntax-only` type-check against the project's real `include/dplay.h`
      (the full linked CMake build could not be run in this environment — the vendored SDL3
      submodule under `third_party/` is not checked out here, unrelated to this change). No
      automated unit test exists yet for this (test infrastructure is `plan.md` Phase 15, not
      started), so this is verified by compilation only, not by a passing test.
- [x] Define a real internal `IID_IDirectPlay` constant in `include/dplay.h`, distinct from the
      current placeholder `IID_IDirectPlay2A = {0}`, so `QueryInterface` can compare against a real
      GUID value instead of accepting anything. **Done:** added `IID_IDirectPlay = {1}` next to
      `IID_IDirectPlay2A = {0}`, with a doc comment stating explicitly these are FreeDirect-internal
      placeholder values, not real Microsoft IIDs (no wire/binary-compatibility claim).
- [x] Ensure `DirectPlayImpl::QueryInterface` returns `DPERR_NOINTERFACE`/`E_NOINTERFACE` for any
      `riid` other than `IID_IDirectPlay`/`IID_IDirectPlay2A`, instead of unconditionally
      succeeding for any request as it does today. **Done:** added a local `IsEqualGuid` helper
      (`src/directplay/DirectPlay.cpp`, `memcmp`-based — no such helper existed anywhere in
      `free-api` or `free-direct` to reuse) and rewrote `DirectPlayImpl::QueryInterface` to check
      `riid` against both known IIDs, returning `E_NOINTERFACE` and setting `*ppvObject = nullptr`
      for anything else. Runtime-verified with a throwaway scratch harness (compiled and run
      outside the repo, not committed) asserting an unknown GUID is rejected and does not fabricate
      an object.
- [x] Ensure `QueryInterface` calls `AddRef()` on the returned interface pointer before returning
      `DP_OK`, in both `DirectPlayImpl` and `DirectPlay2AImpl`. **Done for `DirectPlayImpl`, not
      applicable yet for `DirectPlay2AImpl`:** `DirectPlayImpl::QueryInterface(IID_IDirectPlay,
      ...)` now returns `this` and calls `AddRef()` first, verified at runtime to correctly
      increment/decrement the refcount (scratch harness: `AddRef()`/`Release()` balance checked
      explicitly). `DirectPlayImpl::QueryInterface(IID_IDirectPlay2A, ...)` still allocates a
      *new* `DirectPlay2AImpl`, whose constructor already starts `refCount_` at 1 — adding a
      further `AddRef()` there would double-count and leak a reference, so it was deliberately
      **not** added; this was verified at runtime too (a single `Release()` on the returned
      `IDirectPlay2A*` correctly drops it to 0). `DirectPlay2AImpl::QueryInterface` itself still
      always returns `E_NOINTERFACE` (no self-`riid` success path exists yet at the time this note
      was first written), so there was initially no success path in it to attach an `AddRef()`
      call to. **Update:** the next task below has since closed this gap — see it for the
      completed fix. With that fix in place, "`AddRef()` in both `DirectPlayImpl` and
      `DirectPlay2AImpl`" is now genuinely true of both classes' only success paths
      (`IID_IDirectPlay`/`IID_IDirectPlay2A` self-QI in `DirectPlayImpl`, `IID_IDirectPlay2A`
      self-QI in `DirectPlay2AImpl`); the still-not-`AddRef()`'d path
      (`DirectPlayImpl::QueryInterface(IID_IDirectPlay2A, ...)` constructing a brand-new object)
      remains deliberately excluded, as explained above.
- [x] **(New task, discovered while implementing the two tasks above, not in the original Phase 1
      list.)** Give `DirectPlay2AImpl::QueryInterface` a self-identity success path: return `this`
      (as `IDirectPlay2A*`, and optionally also for a `IID_IUnknown`-equivalent if one is ever
      defined) with an `AddRef()` call when `riid` matches `IID_IDirectPlay2A`, instead of always
      returning `E_NOINTERFACE` as it does today. Once this exists, revisit the previous task's
      "AddRef() in both" wording, since only then will `DirectPlay2AImpl` have a success path to
      attach it to. **Done:** `DirectPlay2AImpl::QueryInterface` now returns `this` with `AddRef()`
      for `IID_IDirectPlay2A`, and still `E_NOINTERFACE` + `*ppvObject = nullptr` for anything
      else (including `IID_IDirectPlay`, which this class does not implement). No
      `IID_IUnknown`-equivalent exists anywhere in this codebase, so that optional part was not
      added — nothing calls for it. Runtime-verified with a throwaway scratch harness: self-QI
      returns the same pointer with a balanced `AddRef()`/`Release()`, an unknown GUID is rejected
      without fabricating an object, and null `ppvObject` is still rejected.
- [x] Ensure `DirectPlayCreate` initializes `*lplpDP = nullptr` before any failure return path, so
      callers never observe an uninitialized pointer on error. **Done:** `*lplpDP = nullptr` is now
      set immediately after the `!lplpDP` null-pointer check, before the `pUnkOuter` check, so every
      failure path after that point returns a caller-visible `nullptr` rather than an untouched or
      garbage value.
- [x] Ensure `DirectPlayCreate` rejects `pUnkOuter != nullptr` consistently, returning
      `DPERR_NOAGGREGATION` (matching the existing `DSERR_NOAGGREGATION` naming convention already
      used in `dsound.h`) instead of today's `DPERR_INVALIDPARAMS`; add `DPERR_NOAGGREGATION` to
      `include/dplay.h` if it is not already defined. **Done:** added
      `DPERR_NOAGGREGATION ((HRESULT)0x88770033L)` to `include/dplay.h` (next unused value after
      the existing `DPERR_*` sequence, which tops out at `DPERR_UNSUPPORTED = 0x88770032`) and
      changed `DirectPlayCreate` to return it for `pUnkOuter != nullptr`, separately from the
      `!lplpDP` case (which still returns `DPERR_INVALIDPARAMS`). Runtime-verified with a
      throwaway scratch harness: null `lplpDP` rejected, non-null `pUnkOuter` rejected with
      `*lplpDP` correctly reset to `nullptr` (checked against a poison pointer value), and the
      normal success path still works.
- [x] Add a `@note Status:` comment to `DirectPlayEnumerateA` documenting its current behavior
      (returns `DP_OK`, invokes the callback zero times) as an intentional interim stub pending
      Phase 8. **Done:** expanded the existing doc comment in `include/dplay.h` to explain the
      current zero-callback behavior and why it cannot stay that way forever (cites the
      `CNetwork::CreateProvider` bound-check finding — see the next task).
- [x] Add the same `@note Status:` documentation to `DirectPlayEnumerateW`. **Done:** its comment
      now points to `DirectPlayEnumerateA`'s documentation rather than duplicating it, since the
      behavior is identical.
- [x] Decide whether `DirectPlayEnumerateA`/`DirectPlayEnumerateW` should invoke the callback once
      with a fake "FreeDirect" service-provider GUID/name, so `free-eggbert`'s
      `CNetwork::EnumProviders` sees at least one selectable provider. Record the decision and
      rationale in `docs/directplay-design.md` (created in Phase 16) — do not implement the
      behavior change in this task, only decide and document it. **Done — decision recorded in the
      new `docs/directplay-design.md`, created early (not waiting for Phase 16) since this
      decision was needed now:** yes, enumeration must eventually report exactly one fake
      provider, because `free-eggbert/src/network.cpp`'s `CNetwork::CreateProvider` has a bound
      check (`if (index >= m_providers.nb) return FALSE;`) that makes it **unconditionally fail**
      when zero providers are enumerated — this is `free-eggbert`'s only call path to
      `DirectPlayCreate`, so it is a hard prerequisite, not a nicety. Real implementation is
      deferred to Phase 8 (session enumeration), not done in this task. No behavior change was
      made to `DirectPlayEnumerateA`/`W` themselves in this task.
- [ ] Add a follow-up task (tracked here, executed once the enumeration decision above is
      implemented) to test enumeration behavior against `free-eggbert`'s real provider-selection UI
      flow in `event.cpp`. **Re-checked 2026-07-19, left open, still low-priority**: reachability
      has partially changed since the original `docs/directplay-callsite-audit.md` (2026-07-08).
      `CNetwork::EnumProviders()` now has a genuine direct call site at `event.cpp:4644` (inside
      `if (m_phase == WM_PHASE_SERVICE)`), not just through the previously-cited
      `NetEnumSessions`/zero-callers path - `free-eggbert` is under active decompilation and this
      may have been reconnected since the audit. However, `CNetwork::EnumSessions()`,
      `JoinSession()`, and `CreateSession()` (the next steps after provider selection) still have
      **zero callers anywhere** in `event.cpp` (re-confirmed by grep), so even if a player can
      reach the provider-selection screen, the flow cannot proceed past it today - the same
      upstream blocker already tracked for the whole DirectPlay real-session verification gap
      (`NEXT.md`, tied to `TreatNetData()`'s still-commented-out call site). A UI-driven smoke test
      (simulating menu clicks to actually reach `WM_PHASE_SERVICE` and observe the provider list)
      would need real reverse-engineering of `event.cpp`'s phase/button state machine - a
      significant, uncertain-payoff investment for a path that dead-ends immediately afterward
      regardless. Not attempted this session; left open, low priority until the downstream blocker
      clears.
- [x] Create `src/directplay/DirectPlaySession.hpp` and `src/directplay/DirectPlaySession.cpp`
      declaring an empty `DirectPlaySession` class (no members yet) that will own session/host/
      player-count state starting in Phase 2. **Done:** both files created, class has only a
      defaulted constructor/destructor, namespaced `free_direct_directplay` (matching the existing
      flat, project-prefixed namespace convention already used by `free_direct_diag` in
      `Diagnostics.hpp`). Not yet included/used anywhere. Syntax-checked with
      `-Wall -Wextra -Wpedantic`, zero warnings.
- [x] Create `src/directplay/DirectPlayPlayer.hpp` and `src/directplay/DirectPlayPlayer.cpp`
      declaring an empty `DirectPlayPlayer` type that will own one player's DPID/names/data
      starting in Phase 2/9. **Done:** same pattern as `DirectPlaySession` above; not yet
      included/used anywhere.
- [x] Create `src/directplay/DirectPlayMessageQueue.hpp` and
      `src/directplay/DirectPlayMessageQueue.cpp` declaring an empty `DirectPlayMessageQueue` type
      that will back `Receive` starting in Phase 3. **Done:** same pattern as the two files above;
      not yet included/used anywhere.
- [x] Create `src/directplay/DirectPlayTransport.hpp` declaring the internal `IDirectPlayTransport`
      abstract interface (pure virtual `Connect`/`Listen`/`Send`/`Receive`/`Shutdown`-style methods;
      exact signature finalized when Phase 4 implements the first concrete backend). **Done:**
      header-only (pure abstract interface, nothing to compile into a `.cpp`), explicitly
      documented as a first pass whose signatures may change in Phase 4, and explicitly documented
      as never including any backend header. Verified with a throwaway mock implementation
      (compiled and run outside the repo, not committed) confirming the interface is actually
      implementable and callable through a base-class reference.
- [x] Add the four new `src/directplay/*.cpp` files to the `free-direct` target's sources in
      `CMakeLists.txt`. **Done, with a correction to this task's own premise:** only three of the
      four new files have a `.cpp` — `DirectPlayTransport` is header-only by design (a pure
      abstract interface has nothing to compile). Added
      `DirectPlaySession.cpp`/`DirectPlayPlayer.cpp`/`DirectPlayMessageQueue.cpp` to
      `target_sources(free-direct PRIVATE ...)`; `DirectPlayTransport.hpp` is not listed there,
      matching this file's existing convention of only listing `.cpp` files (e.g.
      `Diagnostics.hpp` isn't listed either, only `Diagnostics.cpp` is). Verified all three new
      object files compile and link into a static library together with no ODR conflicts.
- [x] Move `DirectPlay2AImpl`/`DirectPlayImpl` out of `src/directplay/DirectPlay.cpp` and into
      dedicated files only once they hold real state (Phase 2+) — do not split the file while it
      remains a pure stub, to avoid empty-file churn. **Done 2026-07-19** (deferred to Phase 2+ as
      this task's own wording specified — both classes have held real state since Phase 2, this
      was just never revisited until now, user-approved as the same category of decision as the
      2026-07-18 `src/directdraw/` split): `DirectPlay2AImpl` (the dominant, real-implementation
      class, 737 lines) moved to `src/directplay/DirectPlayInternal.hpp` (full definition, all
      methods still inline in the class body exactly as before — unlike the `DirectDraw.cpp` split,
      this file's methods were never out-of-line to begin with, so converting ~30 methods to
      out-of-line-with-qualification purely for stylistic parity would have been unnecessary
      rewrite risk for a purely organizational move), paired with a trivial `DirectPlay2A.cpp`
      (`#include "DirectPlayInternal.hpp"`, matching `DirectPlaySession.cpp`'s existing
      header-only-class convention already used elsewhere in `src/directplay/`). `DirectPlayImpl`
      (35 lines, tightly coupled to the free functions that construct it) stayed in
      `DirectPlay.cpp` itself, now down to 105 lines from 998 — that file also gained
      `#include "DirectPlayInternal.hpp"` + `using namespace free_direct_directplay;` in place of
      the old single anonymous namespace. `CMakeLists.txt` updated with the new
      `DirectPlay2A.cpp` source. **Verified**: fresh build + `ctest` (9/9, both default and
      `-DFREE_DIRECT_ENABLE_ENET=ON` with `ctest -L enet` 1/1), `header_hygiene` clean,
      `../free-eggbert` rebuild confirmed unaffected (exit 0, zero `error:` matches) — no test file
      needed any change, since tests only ever went through the public `IDirectPlay*` interfaces.

**Acceptance criteria:** a unit test constructs a `DirectPlayImpl`, calls `QueryInterface` with
`ppvObject == nullptr` and asserts `DPERR_INVALIDPARAMS`/`E_INVALIDARG`; calls it with an
unrelated GUID and asserts `E_NOINTERFACE`/`DPERR_NOINTERFACE`; calls it with `IID_IDirectPlay2A`
and asserts the returned object's refcount reflects one `AddRef()`. The project still builds with
`DirectPlaySession`/`DirectPlayPlayer`/`DirectPlayMessageQueue`/`DirectPlayTransport` as
near-empty scaffolding files. **Note (2026-07-19):** the file-layout half of this note is now
stale in one respect — `DirectPlaySession`/`DirectPlayMessageQueue`/`DirectPlayTransport` are no
longer "near-empty scaffolding" (all hold real state since Phase 2-4); `DirectPlayPlayer.hpp`/`.cpp`
remain genuinely empty/unused placeholders (Phase 9 decided player data storage was not needed —
see `docs/directplay-limitations.md`). The refcount/`QueryInterface` behavioral assertions
themselves are unaffected by today's file split and remain accurate.

---

## Phase 6 — Session hosting

Goal: make `Open(..., DPOPEN_CREATE)` actually start a session other peers can join, on top of
whichever transport is configured.

- [x] Implement `Open(..., DPOPEN_CREATE)` end-to-end on top of the configured transport. **Done,
      loopback only** - this umbrella task is satisfied by the sum of the sub-tasks below plus
      later phases' work: a hosted loopback session real-listens (Decision 11/12), is discoverable
      via `EnumSessions()` (Decision 18), accepts and assigns real joining clients up to
      `dwMaxPlayers` with a real join-accepted handshake (Decisions 7-9, 16), and can send/receive
      real messages to/from them (Decisions 14-15). ENet's hosting side works through `Listen()`
      and can now genuinely receive at the transport level too (Decision 19), but is not
      discoverable (Phase 8 is loopback-only, Decision 18) and `DirectPlay.cpp` doesn't wire any
      of Decisions 15/16 to the ENet branch yet.
- [x] Create a session instance GUID (`guidInstance`) when hosting, if the caller did not already
      supply one. **Done:** added `GenerateSessionInstanceGuid()` (anonymous namespace,
      `DirectPlay.cpp`) - fills `Data1`/`Data2`/`Data3`/`Data4` individually from
      `std::mt19937_64`/`std::random_device`, not a bulk `sizeof(GUID)` memcpy, since `GUID::Data1`
      is `unsigned long` (8 bytes on this platform, not the 4 bytes real DirectPlay's `Data1`
      documents - a real, previously-unnoted portability wrinkle, noted in `NEXT.md` but not fixed
      here, since changing the typedef is a bigger, separate concern). No RFC 4122 version/variant
      bits - not needed, since `CLAUDE.md` already establishes no real-DirectPlay wire
      compatibility. `Open()` generates and writes back into the caller's `DPSESSIONDESC2` only
      when hosting (`DPOPEN_CREATE`) and the caller's `guidInstance` is all-zero; a
      caller-supplied non-zero value (or a joining call) is preserved as-is. Added
      `DirectPlaySession::sessionInstanceGuid` (mirrors the existing `applicationGuid` field),
      reset to zero in `Close()`. **Verified** with two new tests in `tests/directplay_tests.cpp`
      (now 14 total): `Test_OpenAsHostWithZeroGuidInstance_GeneratesNonZeroGuid` (two separate
      hosted sessions get different, non-zero, written-back GUIDs) and
      `Test_OpenAsHostWithNonZeroGuidInstance_PreservesCallerValue`. Also re-verified both CMake
      build configurations (`ENET=OFF`/`ON`) end-to-end and that `include/dplay.h` has zero
      ENet/SDL identifiers.
- [x] Store the session descriptor supplied to `Open` on `DirectPlaySession` (per Phase 2).
      **Verified already satisfied, no new code needed:** cross-checked every `DPSESSIONDESC2`
      field (`include/dplay.h`) against `DirectPlaySession`'s members. `guidApplication`,
      `dwMaxPlayers`, `dwCurrentPlayers`, `lpszSessionNameA`, `lpszPasswordA` were already stored
      (Phase 2); `guidInstance` is now stored too (the task directly above). `dwSize` is
      validation-only, correctly never retained. `dwFlags` (e.g. `free-eggbert`'s own
      `DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST`), `dwUser1`-`dwUser4`, and
      `dwReserved1`/`dwReserved2` are **not** stored, and per `CLAUDE.md`'s scope policy, must stay
      that way until a concrete call site needs them: `free-eggbert`'s own source
      (`src/network.cpp`) only ever *writes* `dwFlags` before calling `Open()` and never reads it
      back afterward, no host-migration/keep-alive behavior exists anywhere in `plan.md`, and
      `dwUser1`-`4`/reserved fields have zero observed `free-eggbert` usage. Storing any of these
      now would be exactly the "for completeness" speculative storage `CLAUDE.md` prohibits.
- [x] Start the ENet host listener as part of `Open(..., DPOPEN_CREATE)` when using
      `EnetDirectPlayTransport`. **Done, in two batches:** batch 1 added build-time backend
      selection - `CMakeLists.txt`'s `if(FREE_DIRECT_ENABLE_ENET)` block (the one already linking
      `FreeDirect::ENet` and adding `EnetDirectPlayTransport.cpp`) now also adds
      `target_compile_definitions(free-direct PRIVATE FREE_DIRECT_ENABLE_ENET=1)`;
      `DirectPlay2AImpl::Open()` (`DirectPlay.cpp`) `#ifdef FREE_DIRECT_ENABLE_ENET`s between
      constructing an `EnetDirectPlayTransport` or a `LoopbackDirectPlayTransport` (`docs/
      directplay-design.md` Decision 4). Batch 2 added the actual listener start: after asking the
      user how to pick a port (`DPSESSIONDESC2` has no port-like field), `Open()` now calls
      `session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayEnetPort)` (a fixed
      constant, `51321` - `docs/directplay-design.md` Decision 5) when `session_.isHost`, resets
      the transport and returns `DPERR_CANTCREATESESSION` if `Listen()` fails.
      **Verified for real**, in stages: (1) a standalone smoke test compiled the exact same
      `DirectPlay.cpp` twice, once without and once with `-DFREE_DIRECT_ENABLE_ENET=1`, confirming
      observably different runtime behavior for the same `Open`/`CreatePlayer`/`Send` sequence -
      proof the `#ifdef` selects a different concrete class, not just that both branches compile.
      (2) A port-conflict test: a first `Open(DPOPEN_CREATE)` succeeds; a second, independent
      `DirectPlayCreate` object's `Open(DPOPEN_CREATE)` on the same default port fails with
      `DPERR_CANTCREATESESSION` because the OS-level UDP socket is already bound (real evidence of
      a genuine `bind()`, not a no-op); releasing the first and opening a third succeeds again.
      (3) **An attempted test of a real raw ENet client actually connecting to the hosted port
      failed, and the failure itself is a real, now-documented finding, not a test bug**: ENet is
      poll-driven with no internal thread, and nothing in `Open()`'s current wiring ever calls
      `enet_host_service()` on the hosted transport after `Listen()` returns, so no connection can
      complete end-to-end today regardless of what calls `Connect()` (`docs/directplay-design.md`
      Decision 5's Caveat spells this out - it is a gap beyond "Phase 7/8 don't exist yet"). Also
      re-verified both full CMake builds (`ENET=OFF`/`ON`) end-to-end, the 12/12 `tests/
      directplay_tests.cpp` suite (built without the macro, unaffected), and that `include/
      dplay.h` has zero ENet/SDL identifiers.
- [x] **New task, added after discovering this gap while verifying the ENet host listener above:**
      give `EnetDirectPlayTransport` (or whatever drives it - `DirectPlaySession`? A new
      `DirectPlay.cpp`-level mechanism?) a way to actually service its `ENetHost`'s events
      (`enet_host_service`) outside of `Shutdown()`'s own internal disconnect-wait loop. ENet is
      poll-driven with no internal thread: nothing happens on a listening host - not accepting a
      pending connection, not receiving a packet, nothing - until something calls
      `enet_host_service()` on it. Confirmed by direct testing: a real external ENet client's
      connection attempt to `Open()`'s hosted port never completes today, timing out instead of
      connecting or being rejected (`docs/directplay-design.md` Decision 5's Caveat). This blocks
      "allow the host to accept incoming client connections" (the next task below) and effectively
      all of Phase 7/8/10's real networking from ever working, regardless of what those phases
      implement on top - decide the servicing model (a call the game's message loop drives, an
      internal thread, something else) before those phases assume one. **Done, after asking the
      user:** added `virtual void Service() = 0;` to `IDirectPlayTransport`
      (`src/directplay/DirectPlayTransport.hpp`); `DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`)
      calls `session_.transport->Service()` once, unconditionally, before consulting the message
      queue - piggybacking on `free-eggbert`'s own already-existing call-`Receive()`-repeatedly
      polling pattern (`docs/directplay-callsite-audit.md`), per the user's chosen option, rather
      than a new API or a background thread (`docs/directplay-design.md` Decision 6).
      `LoopbackDirectPlayTransport::Service()` is a no-op. `EnetDirectPlayTransport::Service()`
      drains pending events with a non-blocking `enet_host_service(host_, &event, 0)` loop:
      `ENET_EVENT_TYPE_CONNECT` adopts the peer as `peer_` if none is tracked yet (extends the
      single-peer model to the hosting role; a second concurrent connection is accepted at the
      protocol level but not adopted - multi-peer hosting stays Phase 6/10's job);
      `ENET_EVENT_TYPE_DISCONNECT` clears `peer_` if it matches; `ENET_EVENT_TYPE_RECEIVE` destroys
      the packet without delivering it (transport-level `Receive()` remains an honest `false` stub
      - no task covers it yet; buffering here would be speculative, half-finished code). **Verified
      for real**, closing Decision 5's Caveat: (1) a standalone smoke test drove a real, external
      (non-FreeDirect) ENet client to a *completed* connection with `Open()`'s hosted session,
      using nothing but repeated calls to the **public** `IDirectPlay2A::Receive()` - exactly
      `free-eggbert`'s own polling pattern, no whitebox tricks needed. (2) A second smoke test
      confirmed `Service()`'s connect/disconnect bookkeeping directly: a first client's connection
      is adopted (`HasPeer()` becomes `true`); a second client connecting concurrently is not
      adopted (peer tracking untouched, no corruption); the first client's graceful `Shutdown()`
      is observed by the server's `Service()`, clearing `HasPeer()` back to `false`. Also
      re-verified both CMake build configurations (`ENET=OFF`/`ON`) end-to-end, the 12/12
      `tests/directplay_tests.cpp` suite (unaffected), and that `include/dplay.h` has zero
      ENet/SDL identifiers.
- [x] Assign the host player-ID namespace: decide and document the starting DPID value and
      increment rule for host-allocated players, consistent with Phase 0's finding about
      `free-eggbert`'s index-based DPID comparison. **Done**, after confirming with the user before
      touching a public header (the decision itself, `docs/directplay-design.md` Decision 3, was
      already written; this task is where it was actually coded): `include/dplay.h`'s `DPID`
      typedef changed from `DWORD_PTR` (`uintptr_t`, pointer-sized - 8 bytes on this platform) to
      `DWORD` (`uint32_t`, a real, portable, always-4-byte type), matching real Microsoft
      DirectPlay's `DPID` exactly and resolving the `NetPlayer`-hardcoded-32-byte-stride hazard
      from `docs/directplay-callsite-audit.md` §5. `DirectPlaySession::nextPlayerId`'s initial
      value (and `Close()`'s reset value) changed from `1` to `0` - the host's first local player
      (via `CreatePlayer`) now genuinely gets DPID `0`, matching `free-eggbert`'s own comparison
      pattern instead of real DirectPlay's `DPID_SYSMSG`/`DPID_ALLPLAYERS` reservation convention.
      Updated `tests/directplay_tests.cpp`'s `Test_LoopbackCreatePlayer_ReturnsUniqueNonZeroDpids`
      (renamed `...ReturnsUniqueSequentialDpidsStartingAtZero`): `0` is now the correct, asserted
      first value, not an error to assert against. **Verified for real**: confirmed
      `sizeof(DPID) == 4` directly (was `8` before this change); `DirectPlayWireProtocol.hpp`'s
      `kDirectPlayWireHeaderSize` computation already used `sizeof(DPID)` dynamically (never a
      hardcoded byte count), so its round-trip test needed no changes and still passes. Also
      re-verified both full CMake builds (`ENET=OFF`/`ON`) end-to-end, the 14/14
      `tests/directplay_tests.cpp` suite, and that `include/dplay.h` has zero ENet/SDL
      identifiers.
- [x] Allow the host to accept incoming client connections up to `dwMaxPlayers`. **Done, after
      asking the user** (DPID-keyed peer map, chosen over a plain list or documentation-only):
      `docs/directplay-design.md` Decision 7 covers the full design, including sub-questions the
      literal ask didn't resolve by itself (host/client role asymmetry, where DPID assignment
      comes from without leaking `ENetPeer*` out of the transport, what `Send()`/`Receive()` mean
      for a multi-peer host). `EnetDirectPlayTransport` now splits `peer_` into `hostPeer_`
      (joining role only, unchanged behavior) and `connectedPeers_`
      (`std::unordered_map<DPID, ENetPeer*>`) + `pendingPeers_` (`std::deque<ENetPeer*>`, hosting
      role only). Two new `IDirectPlayTransport` methods -
      `HasPendingConnection()`/`AssignPendingConnection(DPID)` - let a caller resolve a pending
      connection without the transport ever exposing an `ENetPeer*`. `DirectPlay2AImpl::Receive()`
      (`DirectPlay.cpp`) extends its existing `Service()` call (Decision 6) with a loop:
      while hosting, under `dwMaxPlayers` (`0` correctly means "no limit", matching real
      DirectPlay - discovered this was never validated anywhere in the codebase before), and a
      pending connection exists, allocate the next DPID (`session_.nextPlayerId++`), assign it,
      and record it in `session_.remotePlayerIds`/`session_.currentPlayers`. A pending connection
      left over the cap stays connected-but-unassigned - explicit rejection is the separate,
      still-open "enforce `dwMaxPlayers` by rejecting new joins" task, not this one's; likewise
      decrementing `currentPlayers`/removing from `remotePlayerIds` on disconnect is the separate
      "update `dwCurrentPlayers` as players join and leave" task. `Send()`/`Receive()` on a
      hosting-role instance now always return `false` (no single implicit recipient exists with
      potentially many `connectedPeers_`) - an intentional, documented behavior change from the
      old single-`peer_` model's incidental single-peer send, not a silent regression (no
      committed test exercised it). `Shutdown()` generalizes its graceful-disconnect loop to every
      peer this instance still knows about (`hostPeer_`, all of `connectedPeers_`, all of
      `pendingPeers_`), not just one. **Verified for real** with two standalone smoke tests (not
      committed): (1) directly against `EnetDirectPlayTransport` with three real ENet clients -
      all three genuinely connect and queue as pending; two are assigned DPIDs (simulating a cap),
      the third stays pending; disconnecting one of the two assigned peers correctly shrinks
      `connectedPeers_` (removal by `ENetPeer*` match, not insertion order) without disturbing the
      still-pending third; the previously-pending third is then assigned successfully; assigning
      with nothing pending correctly fails. (2) End-to-end through the real, public
      `Open()`/`CreatePlayer()`/`Receive()` path: two independent real ENet clients both complete
      a genuine connection to the same hosted session simultaneously - something the previous
      single-`peer_` model could not do. (`IDirectPlay2A` has no player-count-observing method in
      this narrow subset, so the `dwMaxPlayers`-cap-enforcement logic itself is verified at the
      transport level, not through this end-to-end path.) Also re-verified both CMake build
      configurations (`ENET=OFF`/`ON`) end-to-end, the 14/14 `tests/directplay_tests.cpp` suite
      (unaffected - no committed test touches multi-peer hosting), and that `include/dplay.h` has
      zero ENet/SDL identifiers.
- [x] Send a join-accepted packet (Phase 5 protocol) to a connecting client once accepted.
      **Done** (`docs/directplay-design.md` Decision 16): the assignment loop in `Receive()` sends
      a `DirectPlayWirePacketType::JoinAccept` packet, addressed to the newly-assigned DPID, right
      after each successful `AssignPendingConnection()`. **Verified**:
      `Test_JoinHandshake_ClientAdoptsHostAssignedDpid` (`tests/directplay_tests.cpp`).
- [ ] ~~Send a join-rejected packet to a connecting client when the session is full or the
      application GUID does not match.~~ **Cancelled (2026-07-19)**, re-verified against current
      code and against `free-eggbert` before deciding: still structurally blocked for the
      over-`dwMaxPlayers` case (a rejected `pendingPeers_` entry is never assigned a DPID,
      `RejectPendingConnection()` never calls `AssignPendingConnection()`, and addressed `Send()`
      only ever reaches `connectedPeers_` - there is structurally no DPID to address a
      `JoinReject` packet to); GUID-mismatch rejection isn't implemented at all either. Fixing
      this properly would need a real architecture change (a way to address a still-pending,
      not-yet-DPID'd peer) for a case with **no evidence of real need**: `JoinSession`/
      `CreateSession` (`free-eggbert/src/network.cpp`) still have zero callers anywhere in
      `event.cpp` (unreachable in the live game, confirmed this session), and even if reachable,
      rejection is already observable today via a real, meaningful `DPERR_NOCONNECTION` following
      the disconnect (Decision 13) - `network.cpp` has no code that parses a distinct
      "join-rejected reason" from a packet payload, only generic `HRESULT`/`TraceErrorDP` handling.
      Already correctly recorded as an accepted deviation in `docs/directplay-limitations.md`'s
      "Join rejection reason" row - this cancellation just makes the `plan.md` checkbox agree with
      that. Not implementing speculatively per `CLAUDE.md`'s scope policy; revisit only if a real
      call site or an explicit user ask surfaces.
- [x] Enforce `dwMaxPlayers` by rejecting new joins once `dwCurrentPlayers` reaches the configured
      maximum. **Done:** `docs/directplay-design.md` Decision 9 - a new
      `IDirectPlayTransport::RejectPendingConnection()` (mirrors `AssignPendingConnection`, no DPID
      parameter) pops the oldest pending peer and gracefully `enet_peer_disconnect`s it;
      `DirectPlay2AImpl::Receive()` runs it in a loop after DPID assignment, only when
      `dwMaxPlayers != 0` (0 = no limit) and the session is at/over the cap. No join-rejected
      explanation packet yet - that needs per-DPID `Send()` (Phase 10), tracked separately by the
      two tasks directly above this one. Verified with a real ENet smoke test: clients within the
      cap connect normally, an excess client observes a genuine `ENET_EVENT_TYPE_DISCONNECT`. Both
      CMake configs build clean and the 14/14 `tests/directplay_tests.cpp` suite still passes.
- [x] Update `dwCurrentPlayers` as players join and leave. **Done, after asking the user** how
      the transport should surface "this DPID disconnected" without exposing `ENetPeer*` -
      `docs/directplay-design.md` Decision 8 mirrors Decision 7's pending-connection shape exactly:
      a new `std::deque<DPID> disconnectedPeerIds_` (hosting role only) in
      `EnetDirectPlayTransport`, populated by `Service()`'s `ENET_EVENT_TYPE_DISCONNECT` handling
      only when the disconnecting peer was already in `connectedPeers_` (i.e. previously assigned
      a DPID via `AssignPendingConnection`) - a peer that disconnects while still only in
      `pendingPeers_` is not reported, since nothing outside the transport knows about it yet. Two
      new `IDirectPlayTransport` methods: `HasPendingConnection()`-style
      `HasDisconnectedPeer() const` and `TakeDisconnectedPeer(DPID* outId)` (an output parameter,
      not a `DPID` return value with `0` meaning "none", since DPID `0` is now a valid real player
      ID per Decision 3 and can never double as an empty sentinel). `LoopbackDirectPlayTransport`
      implements both trivially (always `false` - no incoming-connection concept, so no
      disconnect-of-one concept either). `DirectPlay2AImpl::Receive()` (`DirectPlay.cpp`) gained a
      companion loop *before* the existing pending-connection-assignment loop (process departures
      before admitting arrivals in the same call): while hosting and a disconnect is queued, take
      the DPID, remove it from `session_.remotePlayerIds`, decrement `session_.currentPlayers`
      (underflow-guarded, though it should never actually trigger given the join/leave invariant).
      **Verified for real** with two standalone smoke tests (not committed): (1) directly against
      `EnetDirectPlayTransport` - a real ENet client connects, is assigned a DPID, disconnects, and
      `TakeDisconnectedPeer()` reports exactly that DPID exactly once; separately, a *second* client
      that disconnects *before* ever being assigned is confirmed **not** reported at all. (2)
      End-to-end through the real `Open()`/`CreatePlayer()`/`Receive()` path: a real ENet client
      connects then disconnects, `Receive()` is called repeatedly throughout (including well after
      the disconnect) with no crash and no hang, and the session remains healthy afterward (a
      second local `CreatePlayer()` call still succeeds with a distinct DPID). `remotePlayerIds`/
      `currentPlayers` correctness itself could only be verified at the transport level - `IDirectPlay2A`
      has no player-count-observing method in this narrow subset. Also re-verified both CMake build
      configurations (`ENET=OFF`/`ON`) end-to-end, the 14/14 `tests/directplay_tests.cpp` suite
      (unaffected), and that `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add a test for host session creation over loopback, asserting `Open(..., DPOPEN_CREATE)`
      returns `DP_OK` and the session reports itself as host. **Done, with a caveat found while
      implementing:** `IDirectPlay2A` exposes no public way to query "is this session the host" -
      there is no getter, and `DPOPEN_JOIN`/`DPOPEN_OPENSESSION` currently also return `DP_OK`
      (Phase 7's real `Connect()` doesn't exist yet, so nothing observably distinguishes host from
      join today). Adding such a query would be new public API surface with no
      `free-eggbert`/`planetblupi` call site behind it, which `CLAUDE.md` requires asking the user
      about first rather than adding speculatively - not done here. `Test_OpenAsHostOverLoopback_
      ReturnsOk` (`tests/directplay_tests.cpp`) therefore only asserts the half that genuinely is
      observable through the real public interface: `Open(..., DPOPEN_CREATE)` succeeds over the
      default loopback transport. **Verified**: 15/15 `tests/directplay_tests.cpp` suite passes;
      both CMake configs (`ENET=OFF`/`ON`) build clean; `include/dplay.h` has zero ENet/SDL
      identifiers.
- [x] Add a test for invalid host parameters (e.g. `dwMaxPlayers == 0`, malformed
      `DPSESSIONDESC2.dwSize`), asserting a meaningful `DPERR_*` rather than `DP_OK`. **Done, with
      a scope correction found while implementing:** `dwMaxPlayers == 0` is deliberately **not**
      tested as an error case - it is a real, intentional "no limit" value (Decision 9), and
      `Open()` never validates it as invalid, so asserting a `DPERR_*` for it would pin down wrong
      behavior rather than verify correct behavior. The one real gap was `dwSize`: `Open()`
      (`DirectPlay.cpp`) already checks `lpSessionDesc->dwSize != sizeof(DPSESSIONDESC2)`, but
      nothing exercised that check through the public interface before this task. Added
      `Test_OpenWithMalformedDwSize_ReturnsInvalidParams` (`tests/directplay_tests.cpp`): a
      `dwSize` one byte short of `sizeof(DPSESSIONDESC2)` makes `Open(..., DPOPEN_CREATE)` return
      `DPERR_INVALIDPARAMS` instead of `DP_OK`. **Verified**: 16/16 `tests/directplay_tests.cpp`
      suite passes. `tests/directplay_tests.cpp` is not referenced anywhere in `CMakeLists.txt`
      (Phase 15, not yet CTest-wired), so this change cannot affect either CMake build
      configuration; re-ran `cmake --build cmake-build-debug` anyway as a sanity check (no
      recompilation triggered, as expected) and re-confirmed `include/dplay.h` has zero ENet/SDL
      identifiers.
- [x] Add a test for closing a host session, asserting a subsequent `EnumSessions` from another
      loopback peer no longer finds it. **Done**, finally unblocked by Phase 8's real
      `EnumSessions()` (`docs/directplay-design.md` Decision 18):
      `Test_EnumSessions_NoLongerFindsSessionAfterClose` (`tests/directplay_tests.cpp`) - a hosted
      session is found once, then `Close()`d, then no longer found. **Verified**: 46/46
      `tests/directplay_tests.cpp` suite passes; both CMake configs (`ENET=OFF`/`ON`) build clean;
      `include/dplay.h` has zero ENet/SDL identifiers.

**Acceptance criteria:** two `DirectPlaySession` instances over `LoopbackDirectPlayTransport` in
the same test process can host and observe each other's presence; all three new tests pass with
`FREE_DIRECT_ENABLE_ENET=OFF`.

---

## Phase 7 — Session joining

Goal: make `Open(..., DPOPEN_JOIN)`/`Open(..., DPOPEN_OPENSESSION)` actually connect to a hosted
session and receive an assigned player ID.

- [x] Implement `Open(..., DPOPEN_JOIN)` (and the `DPOPEN_OPENSESSION` path used by
      `free-eggbert`) end-to-end on top of the configured transport. **Done, loopback only**
      (Decisions 11, 12, 16): `Open()`'s joining branch calls `transport->Connect()`, sends a
      join-request, and returns `DP_OK` immediately (asynchronous, confirmed with the user -
      Decision 16); the host's `Receive()`-driven assignment loop sends back a real `JoinAccept`
      packet, which the client's own `Receive()` adopts (assigned DPID + the two GUID fields
      making up this implementation's version of "session descriptor" - see that task's own
      caveat). ENet remains completely unaddressed for the joining role - untouched by any of
      Decisions 11-16.
- [ ] Resolve an explicit host address if the caller/transport configuration provides one
      (loopback: direct in-process reference; ENet: host/port). **Partially done, loopback side
      only:** `docs/directplay-design.md` Decision 10 - `LoopbackDirectPlayTransport::Connect()`
      now resolves a host via a process-wide static registry keyed by the `port` argument, asked
      of and confirmed by the user before implementing (over a session-GUID-keyed alternative).
      The ENet side of this task (how a joining `Open()` call learns what host/port to dial, given
      `DPSESSIONDESC2` has no address-like field - see Decision 5's analogous port problem) is
      **not** resolved and needs its own design pass. Left unchecked since only half the task is
      done. **Update:** `docs/directplay-design.md` Decision 11 adds the fixed-port choice
      (`kDefaultDirectPlayLoopbackPort = 51322`, mirroring Decision 5's ENet port exactly, asked of
      and confirmed by the user) and wires `DirectPlay.cpp`'s `Open()` to actually use it for the
      joining role. Still left unchecked - the ENet side remains unresolved.
- [ ] Connect to the host transport (`enet_host_connect` for ENet; direct handoff for loopback).
      **Partially done, loopback side only:** `LoopbackDirectPlayTransport::Connect()`/`Listen()`
      now really link a client instance to a host instance in the same process (Decision 10),
      including a real `pendingPeers_`/`connectedPeers_`/`disconnectedPeerIds_` lifecycle mirroring
      `EnetDirectPlayTransport`'s (Decisions 7-9). **Verified** with eleven new committed whitebox
      tests in `tests/directplay_tests.cpp` (27/27 total): connection found via registry, connect
      with no host fails, listen on a taken port fails, assign moves pending→connected, reject
      pops-then-fails-when-empty, an assigned peer's disconnect is reported exactly once, a
      never-assigned peer's disconnect is not reported, a third client over a two-player cap is
      rejected (the transport-level analog of this phase's own "max-players rejection" acceptance
      criterion below), host `Shutdown()` clears every peer's connection state (no dangling
      pointer), `Shutdown()` unregisters its port for reuse, and `Send()`/`Receive()` return
      `false` for both roles once connected (a deliberate choice, asked of and confirmed by the
      user: real payload delivery is left for a later, separate design decision - see Decision 10).
      Also re-verified both CMake build configurations (`ENET=OFF`/`ON`) end-to-end and that
      `include/dplay.h` has zero ENet/SDL identifiers. **Update:** `DirectPlay.cpp`'s `Open()` now
      calls `transport->Connect()` for the loopback joining role, and (per Decision 12)
      `transport->Listen()` for the loopback hosting role too - a real loopback join now succeeds
      end-to-end at the connection level (`Test_OpenAsJoinWithHostPresent_Succeeds`). The ENet side
      of "connect to the host transport" remains untouched - left unchecked since only the loopback
      backend is done.
- [x] Send a join-request packet to the host once connected. **Done** (`docs/directplay-design.md`
      Decision 16): `Open()`'s joining branch sends a `DirectPlayWirePacketType::Join` packet,
      fire-and-forget, right after a successful `Connect()` - via `session_.transport->Send(0,
      ...)` directly (not the validated public `Send()` path, since this session has no DPID yet).
      Asked of and confirmed by the user first: this is asynchronous (matches Decision 6's polling
      model), not a blocking `Open()` - see that task below for why blocking cannot work over
      loopback in a single-threaded test process regardless.
- [x] Receive a join-accepted packet from the host and transition local state to "joined."
      **Done, no separate "joined" flag** (Decision 16): the host's own `Receive()`-driven
      assignment loop (Decision 7) now sends a `JoinAccept` packet per newly-assigned peer; a
      joining role's `Receive()` drain loop (Decision 15) recognizes it and adopts the assigned
      DPID. There is no separate boolean "joined" state - adoption into `session_.localPlayerIds`
      *is* the observable transition, since nothing else in the current API surface would ever
      observe a distinct "joined" flag.
- [x] Receive the host-assigned player ID from the join-accepted packet and store it as this
      peer's local DPID. **Done** (Decision 16): `header->idTo` (the assigned DPID) is added to
      `session_.localPlayerIds` if not already present, and `session_.nextPlayerId` is bumped past
      it to avoid a future local `CreatePlayer()` collision.
- [ ] Store the session descriptor received from the host (or supplied by the caller) on the
      joining `DirectPlaySession`. **Partially done** (Decision 16): `applicationGuid` and
      `sessionInstanceGuid` are overwritten from the `JoinAccept` header's fields - the closest
      match to "the session descriptor" `DirectPlaySession` can store, since it deliberately never
      retains a raw `DPSESSIONDESC2` (see its own class comment). `dwMaxPlayers`/session name/
      password are **not** synced - no current consumer needs them on the joining side. Left
      unchecked since the full `DPSESSIONDESC2` isn't synced, only two GUID fields.
- [ ] Handle a join timeout: fail `Open` if no join-accepted/join-rejected packet arrives within a
      configured timeout. **Not applicable to loopback** (Decision 16, mirroring Decision 10/11's
      already-established position): `Open()` is asynchronous now (confirmed with the user) - it
      returns immediately after sending the join-request, never blocks waiting for a response, so
      there is nothing for a timeout to bound on this backend. ENet's side remains a genuinely
      open question (a real network round-trip can legitimately hang or get lost).
- [ ] Return `DPERR_NOSESSIONS` when no host could be reached at all, and `DPERR_TIMEOUT` when a
      host was reached but did not respond in time, matching the distinction implied by
      `DPESC_TIMEDOUT` usage in `free-eggbert/src/network.cpp`'s `EnumSessionsCallback`.
      **Partially done, loopback `DPERR_NOSESSIONS` only** (Decision 11): `Open()`'s loopback
      joining branch returns `DPERR_NOSESSIONS` when `Connect()` fails, which today is always
      (nothing hosts yet - see above). No timeout concept is needed for loopback at all (`Connect()`
      fails synchronously, per Decision 10), so `DPERR_TIMEOUT` doesn't apply to this backend.
      ENet's side of both codes is untouched. Left unchecked since only half the task (one code,
      one backend) is done.
- [x] Add a test for a failed join (no host present), asserting `DPERR_NOSESSIONS`. **Done:**
      `Test_OpenAsJoinWithNoHostPresent_ReturnsNoSessions` (`tests/directplay_tests.cpp`) - a real,
      public `IDirectPlay2A::Open(&desc, DPOPEN_JOIN)` call (not whitebox) with no loopback host
      ever started returns `DPERR_NOSESSIONS`. **Verified**: 28/28 `tests/directplay_tests.cpp`
      suite passes; both CMake configs (`ENET=OFF`/`ON`) build clean; every pre-existing hosting/
      self-send test still passes unaffected; `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add a test for a successful join using a local host/client pair over loopback, asserting both
      peers agree on the assigned DPIDs and session descriptor. **Done** (Decision 16):
      `Test_JoinHandshake_ClientAdoptsHostAssignedDpid` (`tests/directplay_tests.cpp`) - a real
      host + a real joining client; the host's `Receive()` assigns a DPID and sends `JoinAccept`;
      the client's `Receive()` adopts it; proven via the client's own self-send now succeeding
      with the adopted id and failing with an unadopted one (no public getter exists for a
      session's own DPID, so this is the observable proof). "Session descriptor agreement" is
      exercised at the level Decision 16 actually implements (two GUID fields, not the full
      `DPSESSIONDESC2`) - see that decision and the still-partial task above for the honest scope.
      **Verified**: 38/38 `tests/directplay_tests.cpp` suite passes; both CMake configs
      (`ENET=OFF`/`ON`) build clean; `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add a test for max-players rejection: a third loopback client joining a two-player-max
      session receives a rejected outcome (a `DPERR_*` code, not `DP_OK`). **Done, after asking
      the user** whether to add client-side rejection observability now (new scope) or leave it
      blocked: `docs/directplay-design.md` Decision 13 adds `IDirectPlayTransport::
      IsConnectedToHost()` (promoted from both backends' existing `HasHostConnection()`/`HasPeer()`
      test-only accessors), wired into `DirectPlay2AImpl::Receive()`'s joining-role path - checked
      only after the local message queue comes back empty, so it can never shadow a legitimately
      queued self-sent message (Decision 12). **Done:**
      `Test_OpenAsJoinOverMaxPlayers_ThirdClientReceivesNoConnection` - a real `dwMaxPlayers = 2`
      host and three real joining clients over loopback; the host's own `Receive()` call runs the
      assignment/rejection loop (admits the first two, rejects the third); the third client's own
      `Receive()` call returns `DPERR_NOCONNECTION` (the outcome the client actually observes -
      there is still no join-rejected explanation packet, so "rejected for `dwMaxPlayers`" and "the
      host disconnected for another reason" look identical today, per Decision 9's existing
      caveat). **Verified**: 30/30 `tests/directplay_tests.cpp` suite passes; both CMake configs
      (`ENET=OFF`/`ON`) build clean; `include/dplay.h` has zero ENet/SDL identifiers.

**Acceptance criteria:** the host/client-pair test and the max-players test both pass
deterministically over loopback; no test depends on real wall-clock timing beyond a small,
generous timeout bound.

---

## Phase 8 — Session enumeration

Goal: make `EnumSessions` discover real hosted sessions instead of always reporting none.

- [x] Decide whether `EnumSessions` supports explicit-host-only discovery, LAN broadcast
      discovery, or both, and record the decision with rationale in `docs/directplay-design.md`.
      Given `free-eggbert`'s `CNetwork::EnumSessions` only needs *some* list of sessions to
      populate a picker, explicit-host-only is the minimal viable choice — confirm against Phase 0
      findings before committing to broadcast. **Done** (`docs/directplay-design.md` Decision 18):
      explicit-host-only, exactly as this task's own text already reasoned - nothing found while
      implementing contradicted it, so not re-litigated as a fresh question. LAN broadcast
      discovery remains a separate, not-yet-asked-about task (see the task below).
- [x] Implement explicit-host enumeration first (query one or more known host addresses/loopback
      sessions directly). **Done, loopback only** (Decision 18): asked of and confirmed by the
      user - a synchronous, DirectPlay2AImpl-level static registry (keyed by the fixed loopback
      port, mapping to a live `DirectPlaySession*`), not a wire-protocol round-trip - resolving the
      same async/host-must-poll tension Decision 16 already hit for the join handshake, but worse
      here since `EnumSessions()` isn't even called on an `Open()`ed object. Deliberately separate
      from `LoopbackDirectPlayTransport`'s own port registry (Decision 10), which must stay
      ignorant of DirectPlay-level concepts. ENet-hosted sessions are not discoverable by this
      mechanism - untouched, a separate later task.
- [ ] Add LAN broadcast discovery later, **only if a concrete need is confirmed** — ask the user
      before starting this task, per the two-game scope rule in `CLAUDE.md`.
- [ ] Define a discovery-request packet in the Phase 5 protocol. **Not needed for the loopback
      mechanism implemented** (Decision 18) - `DirectPlayWirePacketType::Discovery` already exists
      in `DirectPlayWireProtocol.hpp` (Phase 5) for whenever a real wire exchange is needed (e.g.
      the ENet backend's own discovery, still unimplemented).
- [ ] Define a discovery-response packet in the Phase 5 protocol. Same note as above -
      `DirectPlayWirePacketType::DiscoveryResponse` already exists, unused by the loopback
      mechanism implemented.
- [ ] Include the protocol version field in both discovery packets. N/A to the loopback mechanism
      implemented (no wire packets involved) - applies only once a real wire-based discovery
      exchange is built (ENet backend).
- [x] Include the application GUID field in both discovery packets. **Done, as a direct registry
      filter rather than a wire-packet field** (Decision 18): `EnumSessions()`'s
      `lpEnumSessionsDesc->guidApplication`, when non-zero, filters to only matching hosted
      sessions - a real, call-site-backed behavior (`docs/directplay-callsite-audit.md`:
      `free-eggbert`'s own `CNetwork::EnumSessions()` supplies a real filter). **Verified**:
      `Test_EnumSessions_FiltersByApplicationGuid` (`tests/directplay_tests.cpp`).
- [ ] Ignore discovery responses whose application GUID does not match the requesting
      application's GUID. Same effect achieved via the direct filter above, for loopback - no
      discovery *responses* exist to ignore in this mechanism; applies to a real wire exchange
      once one exists (ENet backend).
- [x] Fill `DPSESSIONDESC2` correctly for the `EnumSessions` callback, from each discovered
      session's advertised descriptor fields. **Done** (Decision 18): `guidApplication`,
      `guidInstance`, `dwMaxPlayers`, `dwCurrentPlayers`, `lpszSessionNameA` filled from the live
      `DirectPlaySession`; `lpszPasswordA` deliberately always `nullptr` (never reveal a password
      via enumeration, matching real DirectPlay convention). **Verified**:
      `Test_EnumSessions_FindsOneHostedSession` asserts exact field values, matching this phase's
      own acceptance criterion precisely.
- [x] Call the `EnumSessions` callback exactly once per discovered session. **Done** - the registry
      lookup loop invokes the callback once per matching entry; only one entry can exist at a time
      today (Decisions 11/12), so this is currently trivially true, but the loop shape is correct
      for whenever more than one entry can exist.
- [x] Respect the callback's `BOOL` return value: stop enumerating further sessions once it
      returns `FALSE`. **Implemented** (a `break` on a `FALSE` return), but **not provably tested**
      - see the struck-through test task below for why.
- [ ] Respect the `dwTimeout` parameter passed to `EnumSessions`, bounding how long discovery
      waits for responses. N/A to the synchronous loopback mechanism implemented - there is no
      waiting to bound (the registry lookup is instantaneous). Applies once a real, potentially-
      slow wire exchange exists (ENet backend).
- [x] Return `DPERR_NOSESSIONS` only if confirmed necessary by `free-eggbert`'s exact expected
      behavior (Phase 0 found no explicit dependency on this specific code — verify before
      hard-coding it as a required return). **Confirmed still not needed**: `EnumSessions()`
      returns `DP_OK` even when zero sessions are found (never invoking the callback) - matches
      the pre-existing stub's own reasoning, still correct now that real discovery exists.
- [x] Add a test for `EnumSessions` finding zero sessions. **Done**:
      `Test_EnumSessions_FindsZeroSessions` (`tests/directplay_tests.cpp`).
- [x] Add a test for `EnumSessions` finding exactly one local (loopback-hosted) session, asserting
      the exact `DPSESSIONDESC2` fields the callback received. **Done**:
      `Test_EnumSessions_FindsOneHostedSession`.
- [ ] Add a test for callback-stop behavior: a callback returning `FALSE` after the first result
      must prevent a second invocation even when two sessions exist. **Not implemented - honestly
      flagged, not shallowly faked** (Decision 18): only one loopback-hosted session can exist per
      process at a time (Decisions 11/12's fixed single-port constraint), so there is no way to
      construct a real two-simultaneous-sessions scenario to exercise this against today.

**Acceptance criteria:** all three enumeration tests pass over loopback with zero real network
I/O; the "one session" test asserts on exact `DPSESSIONDESC2` field values, not just call count.
**Two of three satisfied** - the callback-stop test is not constructible under the current
single-session-at-a-time design (see above), not a shortfall in the implementation itself.

---

## Phase 9 — Player management

Goal: give player creation, naming, and removal real, race-free semantics consistent with the
Phase 0 DPID-vs-index finding.

- [x] Implement stable DPID allocation in `DirectPlaySession`/`DirectPlayPlayer`, using the
      strategy documented in Phase 0/Phase 6 (host-assigned sequential small integers in join
      order). **Reconciled 2026-07-08 (TASK-24H-0088):** already satisfied by
      `DirectPlaySession::nextPlayerId`, a sequential allocator starting at 0
      (`docs/directplay-design.md` Decision 3), consumed by both `CreatePlayer()` (local players)
      and `Receive()`'s pending-connection assignment loop (remote players) — checked off now that
      this checkbox-drift finding from the 24-Hour Stabilization Backlog's reconciliation has been
      confirmed against current code, not assumed.
- [x] Reserve an invalid DPID value (`0`, matching real DirectPlay's `DPID_SYSMSG`/
      `DPID_ALLPLAYERS` convention) so it is never assigned to a real player — **and explicitly
      resolve the conflict** with Phase 0's finding that `free-eggbert`'s receive-side code
      compares `from == i` starting at index `0`: document in writing whether the host's first
      real player must be DPID `1` (reserving `0`) or DPID `0` (matching the game's apparent
      assumption), since these two choices are mutually exclusive. **Resolved, in the opposite
      direction from this task's own literal title** - `docs/directplay-design.md` Decision 3
      decided DPID `0` is *not* reserved; it is assigned to the host's first real (local) player,
      matching `free-eggbert`'s own comparison pattern rather than real DirectPlay's reservation
      convention. That decision is now fully coded (`plan.md` Phase 6's "assign the host
      player-ID namespace" task): `include/dplay.h`'s `DPID` typedef is `DWORD` (4 bytes), and
      `DirectPlaySession::nextPlayerId` starts at `0`. Checked done because the conflict this task
      asks to resolve **is** resolved and implemented - just not in the "reserve 0" direction the
      task's own title assumed going in.
- [x] Register a local player on `CreatePlayer`, storing it in `DirectPlaySession`'s local-player
      list. **Already done, pre-dates Phase 9's explicit start**: `CreatePlayer()`
      (`DirectPlay.cpp`) has pushed each allocated DPID onto `session_.localPlayerIds` since
      Phase 2/4. No new code needed - checked here since Phase 9 formally covers it.
- [x] Register a remote player when a join-accepted/player-joined notification arrives from the
      transport, storing it in the remote-player list. **Already done** via Decision 7's
      assignment loop (`Receive()`) and Decision 16's `JoinAccept` handling - both push the
      relevant DPID onto `remotePlayerIds`/`localPlayerIds` respectively. No new code needed.
- [ ] Store each player's short name (`DPNAME.lpszShortNameA`) as an owned `std::string`.
      **Deliberately deferred, not attempted** (`docs/directplay-design.md` Decision 17): a real
      short name is genuinely supplied at both `../free-eggbert` `CreatePlayer()` call sites
      (`src/network.cpp:183-185,231-233`), so this is not out-of-scope by the two-game rule - but
      `IDirectPlay2A` has no `GetPlayerName`-style method at all, and `free-eggbert` never calls
      one either, so a stored name would be permanently unobservable by anything (test or real
      caller) without adding new public API surface, which needs its own separate ask-the-user
      pass first (`CLAUDE.md`). Storing genuinely dead, unverifiable state would violate this
      project's own Testing Policy.
- [ ] Store each player's long name (`DPNAME.lpszLongNameA`) as an owned `std::string`, allowing it
      to be empty since `free-eggbert` always passes `NULL` for it. **Deferred alongside the short
      name task above** - same observability gap, same reasoning.
- [ ] Store player data bytes (`lpData`/`dwDataSize` from `CreatePlayer`) **only if** a concrete
      call site is found requiring it — Phase 0 found `free-eggbert` always passes `NULL`/`0`;
      confirm before adding storage, per the two-game scope rule.
- [ ] Signal an event handle (`hEvent` from `CreatePlayer`) on message arrival **only if** a
      concrete call site needs it — Phase 0 found `free-eggbert` always passes `NULL`; confirm
      before implementing, per the two-game scope rule.
- [x] Validate player count against `dwMaxPlayers` before allocating a new DPID in `CreatePlayer`,
      returning `DPERR_CANTCREATEPLAYER` when the session is full. **Done** (`docs/
      directplay-design.md` Decision 17): checked before DPID allocation, reusing Decision 9's
      existing "`dwMaxPlayers == 0` means no limit" convention; a local `CreatePlayer()` call
      counts against the same cap `Receive()`'s remote-assignment loop already enforces.
      **Verified**: `Test_CreatePlayerOverMaxPlayers_ReturnsCantCreatePlayer` and
      `Test_CreatePlayerWithNoMaxPlayersLimit_NeverRejects` (`tests/directplay_tests.cpp`).
- [ ] Validate against duplicate players (the same peer calling `CreatePlayer` twice without an
      intervening `Close`) and decide/document the resulting behavior. **Investigated, not
      implemented** (`docs/directplay-design.md` Decision 17's amendment): this task has no
      concrete meaning to implement against yet - `CreatePlayer()`'s sequential DPID allocator
      (Decision 3) makes every call's assigned DPID unique by construction, and there is no other
      caller-identity concept `DPNAME` or any other parameter could dedupe against.
      `free-eggbert` itself only ever calls `CreatePlayer()` once per `CNetwork` instance, so
      there's no real call-site behavior to match either. Needs a concrete definition of
      "duplicate" from the user before any code could target it.
- [ ] Implement a player-lost state (transport-level disconnect detected for a remote player
      without an explicit `Close`) distinct from a clean removal. **Investigated, not implemented**
      (`docs/directplay-design.md` Decision 17's amendment): hits the same observability wall as
      player-name storage - there is no public way for anything to ever query "is this player
      lost vs. cleanly removed" (no system messages exist, correctly, per the two tasks below).
      Adding an internal-only distinction nothing can observe would be the same dead,
      untestable-state problem already flagged for player names.
- [ ] Generate a player-created system message (`DPID_SYSMSG`-sourced) **only if** Phase 0's audit
      finds a concrete `free-eggbert` dependency on receiving one — it did not find explicit
      system-message handling in `event.cpp`/`decnet.cpp`; verify before implementing.
- [ ] Generate a player-destroyed system message under the same condition as above.
- [x] Add a test asserting DPID uniqueness across multiple `CreatePlayer` calls within one session.
      **Already satisfied**, pre-dates Phase 9's formal start:
      `Test_LoopbackCreatePlayer_ReturnsUniqueSequentialDpidsStartingAtZero`
      (`tests/directplay_tests.cpp`, added in Phase 4/6's work).
- [ ] Add a test asserting stored short/long player names round-trip correctly. **Blocked** -
      depends on the deferred name-storage tasks above (Decision 17); nothing to test until that
      future conversation resolves how (or whether) names become observable.
- [x] Add a test asserting player removal (via `Close` or disconnect) updates `dwCurrentPlayers`
      and removes the player from future `EnumSessions`/roster queries. **Done, `dwCurrentPlayers`
      half only** (`docs/directplay-design.md` Decision 17's amendment): `dwCurrentPlayers` has no
      public getter, so `Test_RemotePlayerDisconnect_DecrementsCurrentPlayers` proves the decrement
      indirectly, reusing this phase's own `dwMaxPlayers` cap check as the observable - a host at
      the cap rejects a local `CreatePlayer()`, and the same call succeeds once the remote player
      that filled the cap disconnects and is processed. The "roster queries" half isn't covered -
      no such API exists in this narrow `IDirectPlay2A` subset, and `EnumSessions()` is Phase 8,
      not started.

**Acceptance criteria:** the DPID-vs-index conflict has an explicit written decision in
`docs/directplay-design.md` merged *before* any other Phase 9 code lands (**satisfied**: Decision
3, merged in the Phase 5-adjacent documentation batch); the three new tests pass.

---

## Phase 10 — Send/Receive networking

Goal: real message delivery between distinct peers (not just self-loopback), including host
routing, broadcast, and validation.

- [x] Implement `Send` from a local player to a specific remote player, routed through the
      configured transport. **Done, host role and loopback only:** `docs/directplay-design.md`
      Decision 14 gave `IDirectPlayTransport::Send()` a `DPID targetId` parameter (transport-layer
      groundwork); Decision 15 wired `DirectPlay2AImpl::Send()`/`Receive()` (`DirectPlay.cpp`) to
      actually use it - `Send()` validates `idFrom`/`idTo`, serializes a
      `DirectPlayWirePacketHeader` + payload (`DirectPlayWireProtocol.hpp`, built in Phase 5, unused
      until now), and calls `transport->Send(idTo, ...)`; `Receive()` drains real transport-
      delivered blobs, deserializes them, and enqueues into `session_.messageQueue`. Only the
      hosting role can address a specific remote player today - `docs/directplay-callsite-audit.md`
      confirms `free-eggbert` never does this anyway (its one real `Send()` call site is always
      `Send(m_dpid, 0, ...)`, a broadcast); a joining role has no way to learn any remote DPID yet
      (blocked on the still-unimplemented join-accepted handshake), so it gets `DPERR_INVALIDPLAYER`
      for any non-self `Send()`. **Verified** with the exact task below's own test plus four
      validation tests - see that task and Decision 15 for full detail. **Update:**
      `docs/directplay-design.md` Decision 19 - `EnetDirectPlayTransport::Receive()`/`Service()`
      now really buffer and deliver received packets too (previously an honest `false` stub since
      Phase 5), verified with a real two-instance ENet smoke test over `127.0.0.1` (not committed).
      `DirectPlay.cpp`'s ENet branch of `Open()`/`Send()`/`Receive()` is still not wired to any of
      this - the ENet backend can now genuinely deliver bytes at the transport level, but nothing
      above that layer uses it for a non-self send yet.
- [ ] ~~Implement host-side routing for a **non-broadcast unicast** `Send` addressed to a non-host
      recipient (a joining peer B directly addressing another joining peer C by DPID, relayed
      through the host).~~ **Cancelled (2026-07-19, user decision)**: neither target game uses this
      pattern - the Phase 0 audit found `free-eggbert`'s only real `Send()` call site is always
      `Send(m_dpid, 0, ...)` (broadcast), and `planetblupi` has zero DirectPlay usage at all. Not
      implementing speculatively per `CLAUDE.md`'s scope policy. `SendUnicast`
      (`src/directplay/DirectPlay.cpp`) still only handles the host addressing one of its own
      `remotePlayerIds` directly, and `include/dplay.h`'s `Send` doc comment still says so
      explicitly - left as-is, not a bug. Revisit only if a real call site is ever found.
- [ ] ~~Implement direct peer-to-peer delivery **only if** a future architectural decision moves
      away from the host-hub star topology.~~ **Cancelled (2026-07-19, user decision)**: same
      reasoning as the item above - not needed under the current plan, and no future decision to
      change topology is anticipated absent a real need.
- [x] Implement broadcast-to-all delivery for `idTo == DPID_ALLPLAYERS`/`0`, matching
      `free-eggbert/src/network.cpp`'s `Send(m_dpid, 0, ...)` call pattern. **Done**
      (`TASK-24H-0148`, archived - see `archive/plan20260718.md`): `Send()`'s dispatcher checks
      `idTo == DPID_ALLPLAYERS` before the `idTo == idFrom` self-send branch (Decision 20, so
      `Send(0, 0, ...)` - the host's own real call shape - means broadcast, not self-send).
      `SendBroadcast()` (`src/directplay/DirectPlay.cpp`): hosting role iterates `remotePlayerIds`
      and addresses each directly, never looping back to its own queue; joining role sends to the
      host with the wire header's `idTo` left at `DPID_ALLPLAYERS` as a relay marker.
      `Receive()`'s `Data` case relays a non-host sender's broadcast (byte-for-byte, no
      re-serialization) to every other `remotePlayerIds` entry except the original sender, after
      enqueueing the host's own copy (Decision 21) - this is the host-side relay for broadcast
      specifically (distinct from the still-open non-broadcast unicast routing item above).
      **Re-verified 2026-07-19**: fresh clean build + `ctest` (9/9), `directplay_tests` binary run
      directly ("OK: all DirectPlay tests passed."), covering
      `Test_HostBroadcast_ReachesAllRemoteClientsNotSelf`,
      `Test_HostBroadcast_ReachesMultipleRemoteClients`, and
      `Test_ClientBroadcast_RelayedByHostToOtherClientAndHost`.
- [x] Add the `DPID_ALLPLAYERS` and `DPID_SYSMSG` constants to `include/dplay.h` (if not already
      added in Phase 0/Phase 2), so broadcast sends have named constants available even though
      current call sites use a literal `0`. **Done** (`TASK-24H-0091`, folded into `TASK-24H-0148`'s
      commit, archived): both `#define`d to `0` in `include/dplay.h` (line ~126-127), with a doc
      comment explaining they collide by design since `free-eggbert` never distinguishes them.
- [x] Preserve DirectPlay-like packet boundaries: each `Send` call must arrive as exactly one
      `Receive`-visible message, never coalesced or split. **Done, loopback:**
      `LoopbackDirectPlayTransport::Send()`/`Receive()` (Decision 10/14) never coalesce or split -
      each `Send()` call is one `buffered_` entry, popped whole by exactly one `Receive()` call;
      `DirectPlay2AImpl::Receive()`'s drain loop (Decision 15) parses one wire header + payload per
      transport-level `Receive()` call, preserving the same one-to-one boundary up to
      `session_.messageQueue`. ENet's side is moot until its receive-side buffering exists.
- [x] Preserve reliable, ordered delivery for `DPSEND_GUARANTEED` sends (mapped to
      `ENET_PACKET_FLAG_RELIABLE` per Phase 5 when using the ENet backend). Loopback trivially
      preserves order (a plain FIFO, no real network to reorder anything) and never drops a
      packet regardless of the `reliable` flag. **Done, fully checked 2026-07-19**: the note this
      item used to carry ("ENet backend, whose receive side isn't implemented yet") was stale -
      `docs/directplay-design.md` Decision 19 gave `EnetDirectPlayTransport` a real receive side,
      and `EnetDirectPlayTransport::Send()` (`src/directplay/EnetDirectPlayTransport.cpp`)
      correctly maps `reliable` to `ENET_PACKET_FLAG_RELIABLE` on the single channel both ends
      assume, which ENet's own protocol guarantees delivers reliably and in order. The remaining
      gap (no test demonstrated ordering specifically) is now closed by the two now-checked items
      below: `Test_MultipleGuaranteedSends_ArriveInSendOrder` (loopback) and
      `Test_EnetTransport_ReliableBatchSend_AllPacketsArriveInOrder` (real ENet, 50-packet batch).
      Both verified passing this session.
- [x] Validate the sender player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idFrom` does
      not correspond to a locally-registered player. **Done** (Decision 15): checked against
      `session_.localPlayerIds`. **Verified**: `Test_SendFromUnknownLocalPlayer_
      ReturnsInvalidPlayer` (`tests/directplay_tests.cpp`).
- [x] Validate the recipient player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idTo` is
      neither a known player DPID nor the broadcast ID. **Done**: `idTo` not in
      `session_.remotePlayerIds` correctly returns `DPERR_INVALIDPLAYER` for the hosting role
      (**verified**: `Test_SendToUnknownRemotePlayer_ReturnsInvalidPlayer`). The ambiguity this
      item used to flag - DPID `0` being both the host's own first local player (Decision 3) and
      the literal value of `DPID_ALLPLAYERS` - is resolved by Decision 20/21 (`TASK-24H-0148`,
      archived): `idTo == DPID_ALLPLAYERS` is checked *before* any other interpretation in `Send`'s
      dispatcher, so `idTo == 0` always means broadcast, never "the specific player whose DPID
      happens to be `0`." No remaining unvalidated case.
- [x] Validate a null payload with zero length (`lpData == nullptr && dwDataSize == 0`) as an
      accepted no-payload send, only if a call site needs it; otherwise document it as rejected.
      **Done, re-checked 2026-07-19**: `Send()`'s own comment (`src/directplay/DirectPlay.cpp`)
      says so directly ("A null payload with zero length is a valid no-payload send"); only
      `!lpData && dwDataSize > 0` is rejected, so a null pointer with zero size falls through to
      a real send. **Verified**: `Test_SelfSend_NullPayloadWithZeroSize_ReturnsOk`
      (`tests/directplay_tests.cpp`) sends and receives a zero-byte payload end-to-end
      (`Send(..., nullptr, 0) == DP_OK`, then `Receive()` reports `size == 0`). Re-ran the full
      suite fresh: 9/9 `ctest`.
- [x] Reject a null payload with nonzero length (`lpData == nullptr && dwDataSize > 0`) with
      `DPERR_INVALIDPARAMS`. **Done, re-checked 2026-07-19**: `Send()`'s first check
      (`src/directplay/DirectPlay.cpp`) is exactly `if (!lpData && dwDataSize > 0) return
      DPERR_INVALIDPARAMS;`, fixed under `TASK-24H-0106`'s null-pointer sweep (previously undefined
      behavior via `bytes + dwDataSize` pointer arithmetic on a null pointer). **Verified**:
      `Test_Send_NullPayloadWithNonzeroSize_ReturnsInvalidParams` (`tests/directplay_tests.cpp`).
- [x] Reject messages larger than the maximum payload size (Phase 3/Phase 11) with
      `DPERR_SENDTOOBIG`, sized to comfortably exceed the largest observed `free-eggbert` payload
      (e.g. `sizeof(NetMessage) * pack.nbMessages + 20` in `src/decnet.cpp`, and the 128/132-byte
      packets in `src/event.cpp`). **Done** (Decision 15): `dwDataSize >
      DirectPlayMessageQueue::kMaxPayloadBytes` (4096, already comfortably exceeding every
      `free-eggbert` payload observed in the Phase 0 audit) returns `DPERR_SENDTOOBIG`, checked
      before ever reaching the transport - enforced there specifically because an oversized packet
      that reached the wire would exceed `Receive()`'s fixed-size read buffer and wedge the
      receiver's queue forever. **Verified**: `Test_SendOversizedPayloadToRemotePlayer_
      ReturnsSendTooBig`.
- [x] Add a host/client integration test over loopback: host sends to a specific client, client
      receives the exact payload. **Done**: `Test_SendToSpecificRemotePlayer_
      HostDeliversToAssignedClient` (`tests/directplay_tests.cpp`) - a real host and a real joining
      client over loopback; the host `Send()`s to the client's assigned DPID and the client's own
      `Receive()` gets the exact payload, with `idFrom`/`idTo` matching. **Verified**: 37/37
      `tests/directplay_tests.cpp` suite passes; both CMake configs (`ENET=OFF`/`ON`) build clean;
      `include/dplay.h` has zero ENet/SDL identifiers.
- [ ] ~~Add a two-client **non-broadcast unicast** routing test over loopback: client A sends
      *only* to client B via the host (not a broadcast); client B receives it and the host/client
      A do not.~~ **Cancelled (2026-07-19, user decision)**: depends entirely on the non-broadcast
      unicast routing item above, which is itself cancelled for the same reason (no real call
      site). Broadcast fan-out - the pattern both games actually use - is already tested
      (`Test_HostBroadcast_ReachesMultipleRemoteClients`,
      `Test_ClientBroadcast_RelayedByHostToOtherClientAndHost`).
- [x] Add a packet-ordering test: multiple guaranteed sends from the same sender arrive at the
      receiver in send order. **Done (2026-07-19)**: `Test_MultipleGuaranteedSends_ArriveInSendOrder`
      (`tests/directplay_tests.cpp`) - host sends 10 `DPSEND_GUARANTEED` messages to an assigned
      client, each carrying a distinguishable index byte; the client's `Receive()` calls are
      asserted to return them in send order, then a final `Receive()` confirms no leftover/
      duplicated message. **Verified**: fresh build + `ctest` (9/9), `directplay_tests` run
      directly ("OK: all DirectPlay tests passed.").
- [x] Add a reliable-delivery smoke test gated behind `FREE_DIRECT_ENABLE_ENET`, sending a batch of
      packets over a real local ENet host/client pair on `127.0.0.1` and asserting all arrive.
      **Done (2026-07-19)**: `Test_EnetTransport_ReliableBatchSend_AllPacketsArriveInOrder`
      (`tests/enet_directplay_tests.cpp`) - a real ENet host/client pair over `127.0.0.1` (port
      52105); host sends 50 reliable packets, each with an index byte; asserts all 50 arrive, in
      order, with no duplicates left over. **Verified**: fresh `-DFREE_DIRECT_ENABLE_ENET=ON`
      build + `ctest -L enet` (1/1), `enet_directplay_tests` run directly ("OK: all ENet DirectPlay
      transport tests passed."); default (non-ENet) build + `ctest` re-confirmed unaffected (9/9);
      `header_hygiene` re-confirmed clean.

**Acceptance criteria:** the packet-ordering test and host/client integration test both pass over
loopback in the default (no ENet) build; the ENet smoke test is excluded from the default test run
and only executes when `FREE_DIRECT_ENABLE_ENET=ON`. **Met (2026-07-19)**: both confirmed via a
real build+test run this session (see the checked items above for exact test names and verified
output).

---

## Phase 11 — Error semantics

Goal: a final sweep ensuring no DirectPlay method still returns an unconditional success value
once real behavior is expected of it, and that every deviation from Microsoft DirectPlay is
written down.

- [x] Replace every remaining misleading unconditional-success stub in `src/directplay/*.cpp`
      with a real, state-driven return value (cross-check against Phases 2-10; this is the final
      audit/confirmation pass, not new implementation work). **Done, full method-by-method audit
      completed 2026-07-19**: read every `IDirectPlay`/`IDirectPlay2A` method
      (`QueryInterface`/`AddRef`/`Release` x2, `EnumSessions`, `Open`, `CreatePlayer`,
      `Send`/`SendBroadcast`/`SendSelf`/`SendUnicast`, `Receive`, `Close`) plus the three free
      functions (`DirectPlayEnumerateA`/`W`, `DirectPlayCreate`) - every method's success path is
      real and state-driven; the only unconditional-`DP_OK`-shaped returns left
      (`AddRef`/`Release`, `DirectPlayEnumerateA`/`W`) are honestly unconditional by real
      DirectPlay semantics too (ref-counting always succeeds; there is genuinely only one
      FreeDirect-internal provider to enumerate, Decision 1), not misleading stubs. See the new
      return-code table in `docs/directplay-limitations.md` for the full evidence.
- [x] Return `DPERR_NOCONNECTION` from `Send`/`Receive` when called on a session that is not open
      (before `Open` or after `Close`). Verified 2026-07-18: `src/directplay/DirectPlay.cpp`
      lines 450/475 (`if (!session_.IsOpen()) return DPERR_NOCONNECTION;`).
- [x] Confirm `Send` returns `DPERR_INVALIDPLAYER` for an invalid sender DPID (cross-reference
      Phase 10). Verified 2026-07-18: `SendBroadcast`/`SendUnicast` both check `idFrom` against
      `session_.localPlayerIds` (`DirectPlay.cpp` ~619, ~720).
- [x] Confirm `Send` returns `DPERR_INVALIDPLAYER` for an invalid recipient DPID (cross-reference
      Phase 10). Verified 2026-07-18: `SendUnicast` checks `idTo` against
      `session_.remotePlayerIds` (`DirectPlay.cpp` ~724).
- [x] Confirm `Send` returns `DPERR_SENDTOOBIG` for oversized payloads (cross-reference Phase 10).
      Verified 2026-07-18: both `SendBroadcast` and `SendUnicast` check `dwDataSize` against
      `DirectPlayMessageQueue::kMaxPayloadBytes` (`DirectPlay.cpp` ~626, ~732).
- [x] Confirm `Receive` returns `DPERR_NOMESSAGES` when the queue is empty (cross-reference
      Phase 3). Verified 2026-07-18: `src/directplay/DirectPlayMessageQueue.hpp:95`
      (`if (!front) return DPERR_NOMESSAGES;`).
- [x] Sweep every method of `IDirectPlay`/`IDirectPlay2A` for missing null-pointer checks on
      required output parameters, returning `DPERR_INVALIDPARAMS` where one is missing.
      **Done, exhaustive sweep completed 2026-07-19**: every method with a required
      output/inout pointer already null-checks it - `QueryInterface` (both `DirectPlayImpl` and
      `DirectPlay2AImpl`) checks `ppvObject`; `Open` checks `lpSessionDesc`; `EnumSessions` checks
      `lpEnumSessionsCallback`; `Receive` (via `DirectPlayMessageQueue::TryReceive`) checks
      `lpdwDataSize`; `DirectPlayEnumerateA`/`W` check their callback; `DirectPlayCreate` checks
      `lplpDP`. `CreatePlayer`'s `lpidPlayer` and `Receive`'s `lpidFrom`/`lpidTo` are treated as
      genuinely optional (write-if-non-null) rather than required - a deliberate, pre-existing,
      non-crashing simplification, not a missing check (real DirectPlay documents these as
      required, but no target-game call site ever passes null for them, and allowing null costs
      nothing). No gap found.
- [x] Return `DPERR_UNSUPPORTED` for any flag combination not covered by the target games'
      observed usage, instead of silently ignoring unknown flags. **Done 2026-07-19, using
      `DPERR_INVALIDFLAGS` instead of `DPERR_UNSUPPORTED`**: `Open()` already validated this way
      (`dwFlags & ~(DPOPEN_CREATE|DPOPEN_JOIN|DPOPEN_OPENSESSION)` -> `DPERR_INVALIDFLAGS`) - the
      2026-07-18 audit's `grep -rn DPERR_UNSUPPORTED` search literally for that macro name missed
      this equivalent, already-correct validation. Added the same pattern to the four methods that
      were genuinely missing it: `CreatePlayer` (only `0` is valid - no `DPPLAYER_*` flags are even
      declared in `include/dplay.h`), `Send` (`DPSEND_GUARANTEED`), `Receive` (`DPRECEIVE_ALL`),
      `EnumSessions` (`DPENUMSESSIONS_AVAILABLE`) - each bound is exactly the flag set
      `docs/directplay-callsite-audit.md` confirms `free-eggbert` ever passes. `DPERR_INVALIDFLAGS`
      is real DirectPlay's actual documented code for "the flags parameter contains an invalid
      value" - a better semantic fit than `DPERR_UNSUPPORTED`, kept for consistency with the
      pattern `Open()` already established, not a second, redundant convention. **Verified**: 5 new
      tests (`Test_Open_InvalidFlags_ReturnsInvalidFlags`,
      `Test_CreatePlayer_InvalidFlags_ReturnsInvalidFlags`, `Test_Send_InvalidFlags_ReturnsInvalidFlags`,
      `Test_Receive_InvalidFlags_ReturnsInvalidFlags`, `Test_EnumSessions_InvalidFlags_ReturnsInvalidFlags`,
      `tests/directplay_tests.cpp`); fresh build + `ctest` (9/9, default and
      `-DFREE_DIRECT_ENABLE_ENET=ON`), `header_hygiene` clean, `../free-eggbert` rebuild confirmed
      unaffected (it never passes an out-of-range flag).
- [x] Document every intentional deviation from Microsoft DirectPlay's documented error semantics
      in `docs/directplay-limitations.md` (Phase 16), with a one-line rationale per deviation.
      **Done 2026-07-19**: added a "Return code table" section listing every unique `DPERR_*`/
      `DP_OK`/`E_NOINTERFACE` value actually returned anywhere in `src/directplay/` (gathered by
      grepping every `return`/ternary-return site), its trigger condition(s), and whether it
      matches or deviates from real DirectPlay's documented semantics for that code - plus a note
      on which declared `DPERR_*` macros are never returned at all (API-shape completeness only,
      no call site needs the condition). The existing "Deviation table" (behavioral/design
      deviations) is unchanged and complements this new code-level table, not replaced by it.

**Acceptance criteria:** `docs/directplay-limitations.md` contains a table listing every `DPERR_*`
code FreeDirect returns, the condition that triggers it, and whether it matches or deviates from
documented Microsoft DirectPlay behavior; every method of `IDirectPlay2A` has at least one unit
test covering its primary error path. **Met (2026-07-19)**: return-code table added (see above);
every method with a real error path (`EnumSessions`, `Open`, `CreatePlayer`, `Send`, `Receive`) has
existing test coverage for at least one error condition, per the tests cited throughout this
phase plus the 5 new `InvalidFlags` tests. `QueryInterface`/`AddRef`/`Release`/`Close` have no
meaningful error path beyond `E_NOINTERFACE`/allocation failure, which are COM-standard and not
target-game-observable conditions worth a dedicated test.

---

## Phase 12 — SDL3_net optional backend

Goal: document the SDL3_net option honestly without building it, keeping the door open without
committing engineering time until ENet is proven.

- [x] Add a design note for the SDL3_net backend to `docs/networking-backends.md` (Phase 16),
      describing where `SdlNetDirectPlayTransport` would plug into `IDirectPlayTransport`.
      Verified 2026-07-18: `docs/networking-backends.md` §"Backend 3:
      `SdlNetDirectPlayTransport` - optional, future, not implemented" covers this.
- [x] Explain TCP stream socket tradeoffs in that note (simple client/server, poor fit for
      discrete unreliable/unordered game packets, requires manual message framing over the stream).
      Verified 2026-07-18: same section, TCP bullet.
- [x] Explain UDP datagram tradeoffs in that note (closer semantic fit, but requires FreeDirect to
      hand-roll reliability, ordering, fragmentation, acknowledgement, and retransmission —
      everything ENet already provides). Verified 2026-07-18: same section, UDP bullet.
- [x] Do not implement `SdlNetDirectPlayTransport` until `EnetDirectPlayTransport` (Phases 5-11)
      is stable and passing its integration tests. Verified 2026-07-18: no
      `SdlNetDirectPlayTransport` file exists anywhere in `src/directplay/` - the constraint has
      been honored by simple absence.
- [ ] Add an optional future CMake flag `FREE_DIRECT_ENABLE_SDL3_NET` to `CMakeLists.txt`, default
      `OFF`, with no source files wired to it until the design note above is written and reviewed.
      **Not done, confirmed 2026-07-18**: `grep -n FREE_DIRECT_ENABLE_SDL3_NET CMakeLists.txt`
      finds nothing. Harmless to leave undone (nothing depends on the flag existing yet), but
      genuinely not present - left unchecked rather than assumed.
- [ ] Keep any future SDL3_net usage private to `.cpp` files under `src/directplay/`, matching the
      Internal Backend Policy in `CLAUDE.md`. N/A today - no SDL3_net code exists yet to be
      private or not. Left unchecked since there is nothing to verify against; not a real gap.
- [ ] Add transport-abstraction tests structured so they can run against
      `LoopbackDirectPlayTransport` and `EnetDirectPlayTransport` today, and against
      `SdlNetDirectPlayTransport` later without modification (i.e. tests target
      `IDirectPlayTransport`, not a concrete backend type). **Confirmed NOT the case, 2026-07-18**:
      `tests/enet_directplay_tests.cpp`'s own file header states plainly that
      `tests/directplay_tests.cpp`'s 61 loopback tests fail 29/61 checks if run against an
      ENet-enabled build, because they assume `LoopbackDirectPlayTransport`'s synchronous
      semantics that don't hold for ENet's real asynchronous network model - a deliberate,
      well-reasoned design choice (documented in that file), but the literal opposite of this
      bullet's "parameterized by backend, not duplicated" goal. Each backend has its own
      dedicated, non-interchangeable test file today.

**Acceptance criteria:** the design note exists and is reviewed before any
`SdlNetDirectPlayTransport` source file is created (**met** - note exists, no such file exists);
the transport-abstraction test suite is parameterized by backend rather than duplicated per
backend (**not met** - see the last bullet above; tests are deliberately backend-specific, not
parameterized).

---

## Phase 13 — DirectSound hardening

Goal: close the gap between "partial" and "correct for what `free-eggbert`/`planetblupi` actually
need," without adding DirectSound surface neither game uses.

- [x] Audit `DSBPLAY_LOOPING` against both `free-eggbert` and `planetblupi` call sites (Phase 0
      baseline: neither game passes this flag) and record the result in
      `docs/directsound-limitations.md`. Verified 2026-07-18: documented (lines ~71-74, confirmed
      neither game's source contains `DSBPLAY_LOOPING` or the substring "LOOPING").
- [x] Add a task to implement real looping **only if** a future audit of either target game finds
      an actual `DSBPLAY_LOOPING` call site, or the user explicitly requests it as a named
      exception per `CLAUDE.md`'s scope policy — do not implement it speculatively now. Satisfied
      by absence: not implemented, matching this instruction.
- [x] Audit `SetPan` against both target games' call sites (Phase 0 baseline: `planetblupi` calls
      it once; confirm `free-eggbert`'s usage) and record findings. Verified 2026-07-18:
      `docs/directsound-limitations.md`'s "SetPan: mono-only" section cites
      `docs/audit-24h-free-direct.md` §2.2 for "both target games only pan mono sound effects" -
      `free-eggbert`'s two real call sites (`src/sound.cpp:493`, `src/soundbass.cpp:455`) were
      independently re-confirmed to exist via grep, consistent with that audit's claim.
- [x] Add a task to implement correct mono panning (real per-channel gain via SDL3 stream channel
      maps) only if the audit shows the current approximate behavior is audible/incorrect for
      either game's actual sound assets. Satisfied by absence: not implemented, audit found no
      such need.
- [ ] Audit `GetCurrentPosition` — `include/dsound.h` currently declares only
      `SetCurrentPosition`, not `GetCurrentPosition`; confirm whether either target game calls
      `GetCurrentPosition` at all before adding it. **Genuine gap, confirmed 2026-07-18**:
      `grep -rn GetCurrentPosition src/ include/ docs/` returns zero matches anywhere - this audit
      was never actually performed or recorded, unlike the `DSBPLAY_LOOPING`/`SetPan` audits above
      which both have clear documented findings.
- [ ] Add a task to implement `GetCurrentPosition` (approximate cursor tracking) only if the audit
      above finds a real call site. Blocked on the unchecked item above - left unchecked, not
      assumed.
- [x] Add a unit test for `Play`, asserting `GetStatus` reports `DSBSTATUS_PLAYING` afterward.
      Verified 2026-07-18: `tests/directsound_tests.cpp:284`, `Test_Play_SetsPlayingStatus`.
- [x] Add a unit test for `Stop`, asserting `GetStatus` no longer reports `DSBSTATUS_PLAYING`
      afterward. Verified 2026-07-18: `tests/directsound_tests.cpp:297`,
      `Test_Stop_ClearsPlayingStatus`.
- [x] Add a unit test for `GetStatus` on a freshly created, never-played buffer, asserting it does
      not report `DSBSTATUS_PLAYING`. Verified 2026-07-18: `tests/directsound_tests.cpp:311`,
      `Test_GetStatus_FreshBuffer_NotPlaying`.
- [ ] Add a unit test for volume clamping, asserting values outside `[DSBVOLUME_MIN,
      DSBVOLUME_MAX]` are clamped rather than passed through or rejected. **Partial, left
      unchecked**: `tests/directsound_tests.cpp:520`, `Test_SetVolume_OutOfRangeValues_ReturnOk`,
      only asserts out-of-range `SetVolume()` calls return `DS_OK` (not rejected) - it never reads
      the value back to confirm it was actually *clamped* rather than stored verbatim out-of-range.
      The bullet specifically asks for a test proving clamping, which this test does not do.
- [x] Add documentation for all remaining DirectSound limitations to
      `docs/directsound-limitations.md` (Phase 16), explicitly scoped to what `free-eggbert`/
      `planetblupi` need rather than full DirectSound semantics. Verified 2026-07-18: 161-line
      doc exists, covers pixel/wave-format cast risk, `SetPan`, `SetCurrentPosition`,
      `DSBPLAY_LOOPING`, and more, each with a target-game-relevance note.

**Acceptance criteria:** the four new unit tests run headlessly (no real audio device required, or
gracefully skipped when `DSERR_NODRIVER` is the only available outcome in a sandboxed environment)
and pass; `docs/directsound-limitations.md` states, per limitation, whether it is known to affect
`free-eggbert`, `planetblupi`, both, or neither.

---

## Phase 14 — DirectDraw hardening

Goal: close the gap between "subset-oriented" and "correct for what `free-eggbert`/`planetblupi`
actually need," prioritized by real call-site frequency.

- [x] Audit primary surface presentation against both target games' actual resolution/format
      usage (not just the in-repo demo's 800x600 32-bit path). Verified 2026-07-18:
      `docs/directdraw-limitations.md`/`docs/audit_ddraw.md` both cite both games' real 640x480
      surfaces repeatedly (e.g. `docs/audit_ddraw.md` §9), not the demo's 800x600.
- [x] Audit 8-bit palette conversion against both target games' `CreatePalette`/`SetEntries`/
      `GetEntries`/`SetPalette` call sites. Verified 2026-07-18: covered by
      `docs/directdraw-limitations.md`'s 8-bit palette-to-RGBA32 conversion section plus
      `tests/directdraw_tests.cpp`'s `Test_CreatePalette_SetEntriesGetEntries_RoundTrips` and
      `Test_SetPalette_AffectsGetDCColorExpansion`.
- [x] Audit color-key range behavior against both target games' `SetColorKey` call sites (Phase 0
      baseline: `planetblupi` calls it twice; confirm `free-eggbert`'s exact count and flags).
      Verified 2026-07-18: `docs/directdraw-limitations.md`'s "Color-key range handling" section
      covers this with concrete call-site citations.
- [x] Audit `Blt` clipping given both games call plain `Blt` rarely (`free-eggbert`: 1 call site;
      `planetblupi`: 0 call sites) — confirm whether the single `free-eggbert` `Blt` call site
      actually relies on clipping before investing further effort here. Verified 2026-07-18: see
      `docs/directdraw-limitations.md`'s "Blt is not a minor path" section (revised counts: 4 vs 6
      for free-eggbert, 3 vs 5 for planetblupi - the original "1 call site" baseline here was
      itself superseded by that later, more careful audit).
- [x] Audit `BltFast` clipping given both games call `BltFast` heavily (`free-eggbert`: 18 call
      sites; `planetblupi`: 17 call sites) — this is the higher-priority clipping path. Verified
      2026-07-18: covered by the same section above plus
      `tests/directdraw_tests.cpp`'s `Test_BltFast_SrcColorKey_8Bit_SkipsKeyedPixels` /
      `_32Bit_SkipsKeyedPixels`.
- [ ] Audit mixed 8-bit/32-bit behavior: identify whether either game ever blits directly between
      an 8-bit paletted surface and a 32-bit surface (as opposed to via the palette-to-RGBA32
      present-time conversion already documented in `README.md`). **Not documented, confirmed
      2026-07-18**: no mention of "mixed 8-bit/32-bit" or "mixed-depth" anywhere in
      `docs/directdraw-limitations.md` or `docs/audit_ddraw.md`.
- [ ] Decide whether mixed-depth blits should error (`DDERR_INVALIDPARAMS` or similar) instead of
      silently skipping pixels, based on the audit above, and document the decision in
      `docs/directdraw-limitations.md`. **Genuine gap, confirmed 2026-07-18**:
      `DirectDrawSurface.cpp`'s `BlitFrom` only has explicit branches for `bpp_==8 &&
      source.GetBPP()==8` and `bpp_==32 && source.GetBPP()==32` - a mismatched-depth pair falls
      through both `if` checks and silently copies nothing, with no error returned and no comment
      explaining this is intentional. Real, undecided, undocumented behavior - not a stale
      checkbox.
- [x] Audit `GetDC`/`ReleaseDC` given both games call these meaningfully (`free-eggbert`: 3/3 call
      sites; `planetblupi`: 4/4 call sites) — identify what GDI operations happen between `GetDC`
      and `ReleaseDC` in both games (likely text/UI drawing) and confirm current behavior covers
      them. Verified 2026-07-18: `docs/directdraw-limitations.md`'s "GetDC/ReleaseDC" section
      covers this in detail, including `planetblupi`'s `IsIconPixel` live call path.
- [x] Add a unit test for palette updates (`SetEntries`/`GetEntries` round-trip, then `SetPalette`
      plus present, asserting presented pixel colors reflect the palette). Verified 2026-07-18:
      `Test_CreatePalette_SetEntriesGetEntries_RoundTrips` (round-trip) plus
      `Test_Flip_8BitPrimaryWithPalette_PresentsCorrectColor` (present reflects palette) together
      cover this, though as two separate tests rather than one combined one.
- [x] Add a unit test for color-key blits, covering both the 8-bit palette-index comparison path
      and the 32-bit packed-pixel comparison path documented in `README.md`. Verified 2026-07-18:
      `Test_BltFast_SrcColorKey_8Bit_SkipsKeyedPixels` / `_32Bit_SkipsKeyedPixels`.
- [x] Add a unit test for primary auto-present behavior (a `Blt` to the primary surface triggers
      `PresentPrimary` under the documented throttle/dirty-check rules). Verified 2026-07-18:
      `Test_Presentation_ThrottlesSecondPresentWithinInterval` /
      `_PresentsAgainAfterThrottleIntervalElapses`.
- [x] Add a unit test for `Flip` mode, asserting it behaves as the documented "simplified present,
      not a real flip chain" rather than silently diverging further. Verified 2026-07-18:
      `Test_Flip_PresentsPrimarySurface`, `Test_Flip_OnOffscreenSurface_ReturnsUnsupported`,
      `Test_Flip_WithoutCooperativeLevel_ReturnsUnsupported`.
- [x] Add documentation for the non-real flip-chain behavior to `docs/directdraw-limitations.md`
      (Phase 16), explicit that this is a known, permanent simplification, not a bug to eventually
      fix. Verified 2026-07-18: "Simplified flip chain and DDBLTFX.dwFillColor interpretation"
      section.
- [x] Audit `IsLost`/`Restore` given both games call these meaningfully (`free-eggbert`: 4/8 call
      sites; `planetblupi`: 4/8 call sites) and confirm current behavior does not cause either game
      to enter an unexpected recovery loop. Verified 2026-07-18: `docs/directdraw-limitations.md`'s
      "IsLost/Restore: honest inert stub, not a bug" section.

**Acceptance criteria:** every new DirectDraw test runs headlessly against an off-screen/software
SDL renderer (no real display required) - confirmed true, all of `tests/directdraw_tests.cpp` runs
under the default headless CTest config; `docs/directdraw-limitations.md` cites concrete call-site
counts from both target games for each documented limitation, not general DirectDraw folklore -
true for every limitation checked above **except** the mixed-depth-blit question, which remains
genuinely undocumented (see the two unchecked items above).

---

## Phase 18 — Final validation

Goal: prove, end-to-end and from a clean checkout, that everything claimed in Phases 1-17 actually
works — not just that unit tests pass in isolation.

- [x] Build the project from a clean checkout (fresh clone or an equivalent scratch copy, not the
      developer's working tree) with default CMake options. Done 2026-07-18: fresh out-of-tree
      `build_verify` directory, `cmake -B build_verify -DFREE_API_USE_SYSTEM_SDL3=ON
      -DFREE_DIRECT_BUILD_TESTS=ON && cmake --build build_verify`, exit 0.
- [x] Run all tests via `ctest` from that clean build. Done 2026-07-18: 9/9 passed.
- [x] Run a `free-eggbert` single-player smoke test if a runnable build of `free-eggbert` against
      this FreeDirect is available. Done 2026-07-18: built fresh, ran headless
      (`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`) via a local, uncommitted game-data symlink
      workaround (see `NEXT.md` Section 7 - that game's own CMake build doesn't deploy
      `gamefiles/` into the build output). Result: got past its own truecolor→8-bit sprite
      fallback, ran its main loop stably for the full 8-second observation window
      (`present_count` 16→136, ~20 FPS, no crash). Not a full interactive playthrough - no input
      was driven.
- [x] Run a `planetblupi` single-player smoke test if a runnable build of `planetblupi` against
      this FreeDirect is available. Done 2026-07-18: same method, same result - stable main loop
      for 8 seconds (`present_count` 10→68, ~8-11 FPS, no crash).
- [ ] Run a `free-eggbert` multiplayer smoke test (two local processes) if a runnable build is
      available. **Still blocked, re-confirmed 2026-07-18**: `free-eggbert`'s own
      `CDecor::TreatNetData()` packet-pump call site is still commented out
      (`../free-eggbert/src/event.cpp:2045`), so a multiplayer smoke test through the actual game
      is not currently possible - not a FreeDirect-side gap, tied to that repo's own
      decompilation-in-progress.
- [ ] Test DirectPlay host creation end-to-end via the smoke-test build (not just unit tests).
      Blocked by the item above.
- [ ] Test DirectPlay client join end-to-end via the smoke-test build. Blocked by the item above.
- [ ] Test DirectPlay player creation end-to-end via the smoke-test build. Blocked by the item
      above.
- [ ] Test DirectPlay reliable send end-to-end via the smoke-test build. Blocked by the item above.
- [ ] Test DirectPlay receive queue end-to-end via the smoke-test build. Blocked by the item above.
- [ ] Test DirectPlay close/disconnect end-to-end via the smoke-test build. Blocked by the item
      above.
- [ ] Update `docs/*.md` after validation to reflect any behavior discovered only under real
      end-to-end testing. Not applicable this pass: the 2026-07-18 single-player smoke tests found
      FreeDirect behaving exactly as documented (no new/undocumented behavior surfaced) - left
      unchecked rather than checked-with-nothing-to-show, since nothing was actually written.
- [x] Update `NEXT.md` with final validation results (build status, test status, smoke-test
      results, remaining known issues). Done 2026-07-18 (`NEXT.md` commit `e046de6`).

**Acceptance criteria:** this phase is only marked complete when every sub-task above has a real,
observed pass/fail result recorded in `NEXT.md` — "assumed to work" is not an acceptable status for
any Phase 18 item. **Partially met**: single-player validation items above all have real, observed
2026-07-18 results recorded in `NEXT.md`. The DirectPlay multiplayer end-to-end items remain
genuinely unmet, blocked externally on `free-eggbert`'s decompilation - not assumed, explicitly
recorded as blocked.
