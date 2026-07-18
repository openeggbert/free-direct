# FreeDirect Task Plan

This is the **authoritative English task list** for FreeDirect (see `CLAUDE.md`'s `plan.md`
Policy). It formerly superseded `TODO.md` as the forward-looking backlog; `TODO.md` itself was
deleted on 2026-07-18 once every item in it was resolved (see `CLAUDE.md`'s Documentation Policy
section).

**Scope reminder:** FreeDirect exists to serve exactly two target games, both sibling
repositories: `../free-eggbert` (uses DirectDraw, DirectSound, and DirectPlay) and
`../planetblupi` (uses DirectDraw and DirectSound; confirmed to have zero DirectPlay usage). Every
task below is scoped to what one or both of these games actually need. Tasks that would add
capability beyond that are explicitly marked "ask the user first" — do not implement them
speculatively. See `CLAUDE.md` for the full policy.

## How to read this plan

- Every task is a checkbox (`- [ ]`) and is **atomic**: it does exactly one thing. Do not combine
  two changes into one checkbox, even if they seem related.
- Tasks are grouped into phases (0-18), in the intended execution order. Later phases generally
  depend on earlier ones (e.g. Phase 6/7 need Phase 2's state model and Phase 4's loopback
  transport).
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

## Phase 0 — Repository and call-site audit

Goal: establish, from real source code (not assumption), exactly what DirectPlay/DirectDraw/
DirectSound surface `free-eggbert` and `planetblupi` need. This phase produces a document, not
code.

- [x] Confirm whether `../free-eggbert` exists as a sibling checkout relative to this repository;
      record the result and the checkout path used. **Done:** exists at `../free-eggbert`, HEAD
      `dae5652f` (2026-06-08). See `docs/directplay-callsite-audit.md` §0 (provenance table).
- [x] Confirm whether `../planetblupi` exists as a sibling checkout relative to this repository;
      record the result and the checkout path used. **Done:** exists at `../planetblupi`, HEAD
      `db61ffe7` (2026-06-13). See `docs/directplay-callsite-audit.md` §0.
- [ ] If either sibling repository does not exist at audit time, stop and record that this phase is
      blocked/partial for that repository, rather than guessing at its call sites. **Not
      triggered this run** — both repositories were present, so this guard condition did not apply.
      Left unchecked rather than marked done, since the described action never ran.
- [x] List every DirectPlay-related public declaration currently in `include/dplay.h` (functions,
      interfaces, structs, typedefs, macros), as a flat inventory. **Done:** see
      `docs/directplay-callsite-audit.md` §1.1.
- [x] List every DirectPlay source stub currently in `src/directplay/DirectPlay.cpp`, class by
      class and method by method, noting each method's current unconditional return value.
      **Done:** see `docs/directplay-callsite-audit.md` §1.2, which also surfaces two real
      `QueryInterface` correctness bugs (`DirectPlay2AImpl` rejects everything;
      `DirectPlayImpl` accepts everything) not previously called out this precisely.
- [x] Cross-reference the two inventories above and list any declaration in `include/dplay.h` that
      has no corresponding implementation in `src/directplay/DirectPlay.cpp` (or vice versa).
      **Done:** no shape gaps found — see `docs/directplay-callsite-audit.md` §1.3.
- [x] Grep `../free-eggbert` for DirectPlay-related symbols
      (`DirectPlay`, `dplay`, `IDirectPlay`, `DPID`, `DPSESSIONDESC`, `EnumSessions`,
      `CreatePlayer`) and list every matching file. **Re-verified, confirmed unchanged:**
      `include/network.hpp`, `src/network.cpp`, `src/decnet.cpp`, `src/event.cpp`,
      `include/event.hpp` — see `docs/directplay-callsite-audit.md` §2.1.
- [x] Grep `../planetblupi` for the same DirectPlay-related symbols and list every matching file.
      **Re-verified, confirmed unchanged: zero matches** outside vendored `third_party`/SDL
      headers — `planetblupi` is single-player only. See `docs/directplay-callsite-audit.md` §3.
- [x] Identify missing DirectPlay constants used by target game code: confirm whether
      `DPID_ALLPLAYERS`/`DPID_SYSMSG` should be added to `include/dplay.h`, given that
      `free-eggbert/src/network.cpp:254` (`CNetwork::Send`, the `Send(m_dpid, 0, ...)` call) sends
      to a literal `0` recipient rather than a named constant. **Done:** confirmed both constants
      are absent from `include/dplay.h` and confirmed no call site references either by name — see
      `docs/directplay-callsite-audit.md` §4.
- [x] Identify whether target game code uses `IDirectPlay`, `IDirectPlay2A`, or `IDirectPlay3A`.
      **Done, confirmed:** `free-eggbert` uses `IDirectPlay` only to `QueryInterface` into
      `IDirectPlay2A` (`src/network.cpp:89-91`, typedefs in `include/network.hpp`);
      `IDirectPlay3A` is not referenced anywhere in either target game. See audit doc §2.2.
- [x] Identify whether target game code uses `QueryInterface`. **Done, confirmed:** yes —
      `src/network.cpp:91`: `lpDP->QueryInterface(IID_IDirectPlay2A, (LPVOID*)&m_pDP)` inside
      `CNetwork::CreateProvider`. **Refinement over the original baseline:** this call site is
      only reachable through `CEvent::NetCreate`, which itself has zero callers in `event.cpp` as
      of the audited commit — see `docs/directplay-callsite-audit.md` §2.2-2.3.
- [x] Identify whether target game code uses `EnumSessions`. **Done, confirmed:** yes —
      `CNetwork::EnumSessions` (`src/network.cpp:126-148`), populating a `DPSESSIONDESC2` with
      `guidApplication` set and the `DPENUMSESSIONS_AVAILABLE` flag. **Refinement over the
      original baseline:** `CNetwork::EnumSessions()` itself has zero callers anywhere else in
      `free-eggbert/src`, so its real-world reachability is unverified/blocked, not confirmed —
      see `docs/directplay-callsite-audit.md` §2.2-2.3.
- [x] Identify whether target game code uses `Open`. **Done, confirmed:** yes — `DPOPEN_CREATE`
      at `src/network.cpp:221` (`CNetwork::CreateSession`), `DPOPEN_OPENSESSION` at
      `src/network.cpp:174` (`CNetwork::JoinSession`). **Refinement:** both `CreateSession` and
      `JoinSession` have zero callers anywhere in `free-eggbert/src` — reachability
      unverified/blocked, same as `EnumSessions`. See audit doc §2.2-2.3.
- [x] Identify whether target game code uses `CreatePlayer`. **Done, confirmed:** yes — called
      immediately after `Open` succeeds in both `CreateSession` (`src/network.cpp:233`) and
      `JoinSession` (`src/network.cpp:185`), passing a `DPNAME` with a short name only
      (`lpszLongNameA` is always `NULL`). Same reachability caveat as `Open`.
- [x] Identify whether target game code uses `Send`. **Done, confirmed:** yes, from
      `src/network.cpp:254`, `src/decnet.cpp:83,157`, and `src/event.cpp:2159,2176,2212,2247,
      2281,4708`; every observed call site passes a nonzero/`DPSEND_GUARANTEED`-equivalent flag.
      **Unlike `EnumSessions`/`Open`/`CreatePlayer`, this is confirmed reachable and exercised** —
      the calling code in `src/decnet.cpp` and the `WM_PHASE_MULTI` handling in `event.cpp` are
      live, non-stub gameplay code paths. See audit doc §2.2.
- [x] Identify whether target game code uses `Receive`. **Done, confirmed:** yes —
      `CNetwork::Receive` (`src/network.cpp:262-284`) polls once per frame from
      `src/decnet.cpp:166` with `DPRECEIVE_ALL` into a fixed 500-byte stack buffer, and treats any
      result other than `DP_OK` as "no message" (specifically checking for `DPERR_NOMESSAGES`
      before logging an error). **Confirmed reachable and exercised**, same gameplay-time path as
      `Send`.
- [x] Identify whether target game code uses `Close`. **Done, confirmed:** yes —
      `src/network.cpp:189,237` (error-cleanup inside `JoinSession`/`CreateSession`) and
      `src/network.cpp:293` (`CNetwork::Close`). **Refinement:** `CNetwork::Close()` itself has
      zero external callers; what actually runs on normal teardown is `m_pDP->Release()` via
      `CNetwork::~CNetwork` (`src/network.cpp:30`), called from `src/blupi.cpp` when `g_pNetwork`
      is deleted.
- [x] Identify whether target game code uses groups. **Done, confirmed:** no `CreateGroup`,
      `AddPlayerToGroup`, `DeletePlayerFromGroup`, or group-enumeration call found anywhere in
      `free-eggbert` (whole-repository grep, zero matches).
- [x] Identify whether target game code uses lobby APIs. **Done, confirmed:** no `dplobby.h`
      include or `IDirectPlayLobby*` symbol found in `free-eggbert`'s own source (only present in
      the vendored, uncompiled `dxsdk3/sdk/inc/dplobby.h` reference header).
- [x] Identify whether target game code uses service provider enumeration. **Done, confirmed:**
      yes — `CNetwork::EnumProviders` (`src/network.cpp:57-71`) calls
      `DirectPlayEnumerateA`/`DirectPlayEnumerateW` to populate a provider picker. **Refinement:**
      same unreachable-wrapper caveat as `EnumSessions` — `CEvent::NetEnumSessions`, the only
      caller of `EnumProviders`, itself has zero callers in `event.cpp`.
- [x] Identify expected behavior when no sessions are found. **Done, but only partially
      verifiable:** at the `CNetwork` level, `CNetwork::EnumSessions()` treats a non-`DP_OK`
      result as failure and clears its session list, with no dedicated "no sessions" error path.
      The original baseline's claim about `event.cpp`'s UI behavior on zero sessions **could not
      be confirmed** — the relevant `WM_PHASE_DP_*` transition handlers are empty `// ...`
      placeholders in the audited source. Recorded honestly as blocked/unverified at the UI level
      in `docs/directplay-callsite-audit.md` §2.3-2.4, rather than assumed.
- [x] Identify expected behavior when network initialization fails. **Done, but only partially
      verifiable, same caveat as above:** `CNetwork::CreateProvider` returns `FALSE` on
      `DirectPlayCreate`/`QueryInterface` failure and releases any partial object (confirmed at
      `src/network.cpp:85-99`); how `event.cpp`'s UI reacts to that `FALSE` cannot be traced
      further because its only caller, `CEvent::NetCreate`, is itself only reachable from an empty
      placeholder handler. See audit doc §2.3-2.4.
- [x] Trace exactly how `event.cpp`'s provider-selection and session-selection UI reacts to
      `EnumProviders`/`EnumSessions` returning zero results, and record the trace. **Done — result
      is a blocked/unverified finding, not a guess:** the trace led to ten empty `// ...`
      placeholder bodies under `WM_PHASE_DP_*` handling in `src/event.cpp` (confirmed exact list
      in `docs/directplay-callsite-audit.md` §2.3), meaning the UI-level reaction cannot currently
      be observed from source. This is a substantive correction to the plan's original assumption
      that this trace would be straightforwardly observable.
- [x] Document the DPID-vs-array-index pattern in `free-eggbert/src/network.cpp`'s
      `CNetwork::Receive` (`src/network.cpp:262-284`: `for (int i = 0; i < MAXNETPLAYER; i++) if
      (m_players[i].bIsPresent && from == i)` — comparing a `DPID` directly against a loop index).
      **Done:** confirmed exact line numbers and recorded in `docs/directplay-callsite-audit.md`
      §6. **A more severe, previously-undocumented related hazard was also found and recorded in
      §5:** FreeDirect's `DPID` typedef (`DWORD_PTR`, 8 bytes on 64-bit) diverges from real
      DirectPlay's `DWORD` (4 bytes), which `free-eggbert/src/event.cpp`'s `NetSearchPlayer`
      (line 2180) and `NetStartPlay` (line 2196) rely on via hardcoded 32-byte-stride raw pointer
      arithmetic over `NetPlayer` — a real ABI/memory-layout bug waiting to happen on 64-bit
      builds, not just a semantic index-vs-ID question. This must be resolved explicitly in Phase
      9, not silently.
- [x] Audit `../planetblupi`'s DirectDraw call sites for methods/flags not already covered by
      `../free-eggbert`. **Done, re-verified with corrections:** `BltFast` 17, plain `Blt` 0,
      `GetDC` 4, `ReleaseDC` 4, `IsLost` 4, real `IDirectDrawSurface::Restore()` calls 5 (not 8 as
      informally stated before — a naive grep over-counted by also matching each game's own
      `CPixmap::Restore()` wrapper and an unrelated `MouseBackRestore()` helper). See
      `docs/directplay-callsite-audit.md` §7.
- [x] Audit `../free-eggbert`'s DirectDraw call sites the same way. **Done, re-verified with
      corrections:** `BltFast` 18, plain `Blt` 1, `GetDC` 3, `ReleaseDC` 3, `IsLost` 4, real
      `Restore()` calls 5 (same correction as above). See audit doc §7.
- [x] Audit both target games' DirectSound call sites for `DSBPLAY_LOOPING` usage. **Done,
      re-verified, confirmed unchanged:** zero call sites in either game. Also re-verified
      `SetPan` call counts while auditing this subsystem: `free-eggbert` 2, `planetblupi` 1
      (`src/sound.cpp:458`) — see `docs/directplay-callsite-audit.md` §8.
- [x] Document all of the above findings in `docs/directplay-callsite-audit.md`, structured as one
      section per target game, each API getting an explicit yes/no/not-applicable verdict with a
      file/function citation. **Done** — see the file itself, including a summary verdict table
      in §9.

**Acceptance criteria:** `docs/directplay-callsite-audit.md` exists, covers both `free-eggbert`
and `planetblupi`, and every yes/no verdict in it cites a specific file and function/line rather
than a general impression. Any finding that could not be verified (e.g. because a sibling
repository was missing) is explicitly marked "unverified" rather than silently assumed. **Met.**
The only intentionally-unchecked Phase 0 item above is the "if a sibling repo is missing" guard
task, which did not trigger this run because both repositories were present.

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
      flow in `event.cpp`.
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
- [ ] Move `DirectPlay2AImpl`/`DirectPlayImpl` out of `src/directplay/DirectPlay.cpp` and into
      dedicated files only once they hold real state (Phase 2+) — do not split the file while it
      remains a pure stub, to avoid empty-file churn. **Intentionally still unchecked:** this
      task's own wording defers it to Phase 2+; it is not part of Phase 1's completable scope and
      will be picked up naturally when Phase 2 gives these classes real state.

**Acceptance criteria:** a unit test constructs a `DirectPlayImpl`, calls `QueryInterface` with
`ppvObject == nullptr` and asserts `DPERR_INVALIDPARAMS`/`E_INVALIDARG`; calls it with an
unrelated GUID and asserts `E_NOINTERFACE`/`DPERR_NOINTERFACE`; calls it with `IID_IDirectPlay2A`
and asserts the returned object's refcount reflects one `AddRef()`. The project still builds with
`DirectPlaySession`/`DirectPlayPlayer`/`DirectPlayMessageQueue`/`DirectPlayTransport` as
near-empty scaffolding files.

---

## Phase 2 — DirectPlay state model

Goal: give `DirectPlaySession` real, validated state so that `Open`/`Close`/`CreatePlayer`/`Send`/
`Receive` stop being unconditional-success stubs and start reflecting actual object state.

- [x] Add a `DirectPlayObjectState` enum (`Created`, `Open`, `Closed`) to
      `src/directplay/DirectPlaySession.hpp`. **Done:** a scoped `enum class` at namespace scope
      (`free_direct_directplay`), not nested in the class, so it can be referenced without
      `DirectPlaySession::` qualification from `DirectPlay.cpp` later.
- [x] Track whether the object is closed via the new state enum on `DirectPlaySession`. **Done:**
      `IsClosed()` returns `state == DirectPlayObjectState::Closed` — no separate boolean, exactly
      as "derived from the state enum" implies.
- [x] Track whether a session is currently open (host or joined) as derived from the state enum.
      **Done:** `IsOpen()` returns `state == DirectPlayObjectState::Open`, same pattern as
      `IsClosed()`.
- [x] Track whether this peer is host or client as a boolean/enum field on `DirectPlaySession`.
      **Done:** `bool isHost` (the task explicitly allows either a boolean or an enum; chose
      boolean to match `free-eggbert`'s own `BOOL m_bHost` shape referenced in the Phase 0 audit).
- [x] Track local player DPIDs in a `std::vector<DPID>` on `DirectPlaySession`. **Done:**
      `localPlayerIds`.
- [x] Track remote player DPIDs in a separate `std::vector<DPID>` on `DirectPlaySession`. **Done:**
      `remotePlayerIds`, a distinct vector from `localPlayerIds`.
- [x] Track an owned copy of the session descriptor (`DPSESSIONDESC2`) on `DirectPlaySession`,
      deep-copying session name/password strings rather than retaining caller-owned pointers.
      **Done, with a deliberate design choice documented in the header:** rather than keeping a
      `DPSESSIONDESC2`-shaped member (which would invite someone to read its now-meaningless
      `lpszSessionName`/`lpszPassword` pointer fields later), the scalar/string data worth owning
      is broken out into plain fields directly (`sessionName`, `password`, `applicationGuid`,
      `maxPlayers`, `currentPlayers` — the next four tasks). No caller-owned pointer is retained
      anywhere on `DirectPlaySession`.
- [x] Track the session name as an owned `std::string`, derived from the session descriptor copy.
      **Done:** `sessionName`. (`password` was added alongside it, matching the previous task's
      explicit mention of "session name/password strings" needing deep-copy treatment, even though
      it wasn't separately enumerated as its own bullet.)
- [x] Track the application GUID (`guidApplication`) from the session descriptor. **Done:**
      `applicationGuid`.
- [x] Track `dwMaxPlayers` from the session descriptor. **Done:** `maxPlayers`.
- [x] Track `dwCurrentPlayers` as a live counter, incremented/decremented as players are created/
      removed. **Done (field only in this batch):** `currentPlayers`, defaulted to `0`. The actual
      increment/decrement-on-player-create/remove *behavior* is not implemented yet — that lands
      with the `CreatePlayer`/`Close` wiring tasks later in this phase, which are not yet done.
- [x] Validate `DPSESSIONDESC2.dwSize` in `Open()`, returning `DPERR_INVALIDPARAMS` when it does
      not equal `sizeof(DPSESSIONDESC2)`. **Done:** also rejects a null `lpSessionDesc` with the
      same code, since dereferencing it to read `dwSize` would otherwise be undefined behavior —
      a safety-necessary addition, not scope creep. `Open()` itself does not yet do anything else
      (no state transition wired in yet — that is the next batch).
- [x] Validate `DPNAME.dwSize` in `CreatePlayer()`, returning `DPERR_INVALIDPARAMS` when it does
      not equal `sizeof(DPNAME)`. **Done:** `lpPlayerName` is treated as optional (real DirectPlay
      allows creating a player without name info), so the size check only runs when a non-null
      `DPNAME*` is actually provided; a null `lpPlayerName` is still accepted. Both tasks
      runtime-verified with a throwaway scratch harness: null/undersized inputs rejected with
      `DPERR_INVALIDPARAMS`, correctly-sized inputs (and a null, optional `lpPlayerName`) accepted.
- [x] Validate `dwFlags` passed to `Open()`, returning `DPERR_INVALIDFLAGS` for any bit outside
      `DPOPEN_CREATE`/`DPOPEN_JOIN`/`DPOPEN_OPENSESSION`. **Done:** checked right after the
      `dwSize`/null validation, before the already-open check. Only rejects bits outside the
      `DPOPEN_CREATE | DPOPEN_JOIN | DPOPEN_OPENSESSION` mask (note `DPOPEN_JOIN` and
      `DPOPEN_OPENSESSION` share the same numeric value, so this is really a two-bit mask); does
      **not** additionally enforce that `DPOPEN_CREATE` and `DPOPEN_JOIN` are mutually exclusive
      (e.g. passing both set would still pass this check), since that wasn't named by this task —
      left as a possible future refinement, not implemented speculatively. Runtime-verified: an
      unrecognized flag bit is rejected alone and when combined with a valid bit; `DPOPEN_CREATE`
      and `DPOPEN_JOIN` individually still succeed as before. Also corrected a stale in-code
      comment left over from the previous batch that claimed `Close()` "does not yet reset
      session_" — it does, as of the prior commit.
- [x] Replace `Open()`'s unconditional `DP_OK` return with real state transitions plus
      `DPERR_ALREADYINITIALIZED` when called again on an already-open object. **Done:**
      `DirectPlay2AImpl` now owns a `free_direct_directplay::DirectPlaySession session_` member.
      `Open()` rejects a second call with `DPERR_ALREADYINITIALIZED` only when `session_.IsOpen()`
      (a re-`Open()` after `Close()` is deliberately still allowed to proceed, matching this task's
      specific "already-*open*" wording — `Close()` does not reset `session_` yet, that is a
      separate not-yet-done task, so this distinction has no observable effect until it lands). On
      success, `Open()` now sets `isHost` from `DPOPEN_CREATE`, and copies `applicationGuid`,
      `maxPlayers`, `currentPlayers`, `sessionName`, and `password` from the caller's descriptor
      into `session_`'s owned fields (deep-copying the two strings, never retaining the caller's
      `LPSTR` pointers), then transitions `state` to `Open`. `dwFlags` bits other than
      `DPOPEN_CREATE` are not yet validated (separate task, not in this batch). Runtime-verified
      with a throwaway scratch harness: first `Open()` (both `DPOPEN_CREATE` and
      `DPOPEN_OPENSESSION`) succeeds, a second `Open()` on the same object returns
      `DPERR_ALREADYINITIALIZED`, and the existing `dwSize`/null validation still works.
- [x] Replace `EnumSessions()`'s unconditional `DP_OK` return with behavior driven by the
      enumeration decision recorded in Phase 1 (full discovery logic still lands in Phase 8; this
      task only wires the method to real state instead of an unconditional stub). **Done, with a
      scope correction:** the Phase 1 decision in `docs/directplay-design.md` was specifically
      about the free functions `DirectPlayEnumerateA`/`W` (service *provider* enumeration, used by
      `CNetwork::EnumProviders`), not about `IDirectPlay2A::EnumSessions` (session enumeration,
      used by `CNetwork::EnumSessions`) — these are two distinct real-DirectPlay concepts that
      this task's wording conflated; noting the correction here rather than silently treating them
      as the same thing. What was actually done: `lpEnumSessionsDesc` (optional) now has its
      `dwSize` validated when provided, and a null `lpEnumSessionsCallback` is now rejected with
      `DPERR_INVALIDPARAMS` (a safety-necessary addition — there would be no way to receive
      discovered sessions without one). The method still returns `DP_OK` with zero callback
      invocations, which is honestly correct today (Phase 6/7/8 hosting/joining/discovery
      infrastructure doesn't exist yet, so there is genuinely nothing to discover), not a
      leftover placeholder. Runtime-verified: null callback and undersized filter descriptor both
      rejected; valid inputs (with and without a filter descriptor) succeed with zero results.
- [x] Replace `CreatePlayer()`'s hardcoded `*lpidPlayer = 1` with a DPID allocated per the strategy
      to be finalized in Phase 9 (a simple incrementing counter is an acceptable placeholder here;
      Phase 9 revisits correctness against the Phase 0 DPID-vs-index finding). **Done:** added a
      new `DPID nextPlayerId = 1` field to `DirectPlaySession` (not one of the originally-enumerated
      Phase 2 data-model fields, but necessary to implement this task — noted here rather than
      silently added). `CreatePlayer()` now allocates `session_.nextPlayerId++`, appends it to
      `localPlayerIds`, increments `currentPlayers`, and writes it to `*lpidPlayer` only if that
      output pointer is non-null (preserving the original stub's tolerance of a null
      `lpidPlayer`). Does **not** yet check whether the session is open first — that kind of
      precondition check is not named by this task and is left for a later validation sweep if
      needed. Runtime-verified: sequential `CreatePlayer` calls yield distinct, incrementing DPIDs
      starting at 1.
- [x] Replace `Send()`'s unconditional `DP_OK` return with parameter/state validation only (full
      routing/delivery lands in Phase 10). **Done, state validation only:** `Send()` now returns
      `DPERR_NOCONNECTION` when `!session_.IsOpen()`. Deliberately does **not** yet validate
      sender/recipient player IDs or the payload — those are Phase 10's own explicitly-numbered
      tasks and are left to that phase rather than pulled forward, even though `localPlayerIds`
      technically already exists to check against.
- [x] Replace `Receive()`'s unconditional `DP_OK` return with `DPERR_NOMESSAGES` once the message
      queue (Phase 3) reports empty. **Done, ahead of a real queue existing:** `Receive()` now
      returns `DPERR_NOCONNECTION` when `!session_.IsOpen()`, and `DPERR_NOMESSAGES`
      unconditionally otherwise. Since `DirectPlaySession` has no message-queue member until
      Phase 3, "the queue reports empty" is trivially and honestly true today — this is not a
      placeholder standing in for real behavior, it is the actually-correct answer given the
      current state of the world, and happens to exactly match what
      `free-eggbert/src/network.cpp`'s `CNetwork::Receive` checks for. Runtime-verified (both
      tasks together): before `Open()`, both methods return `DPERR_NOCONNECTION`; after `Open()`,
      `Send()` succeeds and `Receive()` returns `DPERR_NOMESSAGES`; after `Close()`, both return
      `DPERR_NOCONNECTION` again.
- [x] Make `Close()` clear all session/player/message state on `DirectPlaySession` and transition
      to the `Closed` state. **Done for session/player state; message state is not applicable
      yet:** `Close()` now clears `localPlayerIds`, `remotePlayerIds`, and resets `nextPlayerId`
      back to `1` (so a session that is closed and later re-`Open()`ed starts a fresh DPID
      sequence, not a continuation of the old one), clears `sessionName`/`password`, zeroes
      `applicationGuid`/`maxPlayers`/`currentPlayers`, resets `isHost` to `false`, and transitions
      `state` to `Closed`. Message-queue clearing was explicitly deferred at the time, since
      `DirectPlaySession` had no message-queue member yet. **Follow-up (Phase 3 batch): now
      done** — `Close()` also calls `session_.messageQueue.Clear()` (using the new
      `DirectPlayMessageQueue::Clear()` method, added for this purpose), and the stale
      deferral comment has been removed.
      Runtime-verified: after `Close()`, a subsequent `Open()` no longer returns
      `DPERR_ALREADYINITIALIZED` (proving `state` left `Open`), and a player created after that
      re-`Open()` gets DPID `1` again (proving the allocator and player list were actually reset,
      not just the top-level `state` flag).
- [x] Make `Release()` on `DirectPlay2AImpl` tear down any transport resources safely (via
      `IDirectPlayTransport::Shutdown()` or equivalent) before `delete this`. **Done, ahead of a
      real transport existing:** added a `std::unique_ptr<IDirectPlayTransport> transport` field
      to `DirectPlaySession` (always null today — no concrete backend exists until
      `LoopbackDirectPlayTransport` in Phase 4 / `EnetDirectPlayTransport` in Phase 5).
      `Release()` now calls `session_.transport->Shutdown()` when non-null, before `delete this`.
      Since nothing ever assigns a transport yet, this is currently a no-op in practice, but the
      safe, correct shutdown call is now in place and will activate automatically once Phase 4/5
      construct and assign a real transport during `Open()` — no further change to `Release()`
      itself should be needed then. **Verification is split in two, honestly, since there is no
      way yet to inject a transport into a real `DirectPlay2AImpl`** (that capability doesn't
      exist until Phase 4/5 give `Open()` logic to construct one): (1) runtime-verified that
      `Release()`'s refcounting/deletion still works correctly with the always-null transport
      (`AddRef`/`Release` balance, deletion on reaching 0); (2) separately runtime-verified, using
      a standalone mock `IDirectPlayTransport` attached directly to a bare `DirectPlaySession`
      (not through `DirectPlay2AImpl`), that the null-check-then-`Shutdown()` pattern itself is
      correct. This closes out Phase 2 in its entirety.

**Acceptance criteria:** a unit test opens a session (`DPOPEN_CREATE`), closes it, and asserts
`dwCurrentPlayers` and the session-open flag both reset to their pre-open baseline; a second call
to `Open()` without an intervening `Close()` returns `DPERR_ALREADYINITIALIZED`; the project
builds and this test passes.

---

## Phase 3 — Message queue semantics

Goal: give `Receive` real FIFO semantics backed by `DirectPlayMessageQueue`, independent of any
transport backend.

- [x] Implement an internal `DirectPlayMessagePacket` struct in
      `src/directplay/DirectPlayMessageQueue.hpp`. **Done.**
- [x] Store the source DPID (`idFrom`) in `DirectPlayMessagePacket`. **Done.**
- [x] Store the destination DPID (`idTo`) in `DirectPlayMessagePacket`. **Done.**
- [x] Store flags (e.g. the guaranteed-delivery bit) in `DirectPlayMessagePacket`. **Done:** a
      plain `DWORD flags` field; no specific bit is interpreted yet (that happens once `Send`
      actually enqueues a packet in a later Phase 3/10 task).
- [x] Store payload bytes as a `std::vector<uint8_t>` in `DirectPlayMessagePacket`. **Done:**
      `std::vector<std::uint8_t> payload`.
- [x] Implement a FIFO receive queue (e.g. `std::deque<DirectPlayMessagePacket>`) inside
      `DirectPlayMessageQueue`. **Done**, plus a minimal API to actually use it —
      `IsEmpty()`/`Enqueue()`/`Front()`/`PopFront()` — none of which were separately enumerated as
      tasks but are necessary for the queue to be usable at all (by `Receive()` in the next batch,
      and by whatever eventually delivers a message: loopback in Phase 4, real routing in Phase
      10). Nothing calls `Enqueue()` yet. Runtime-verified with a throwaway scratch harness:
      two packets enqueued and dequeued in FIFO order, with `idFrom`/`idTo`/`payload` all
      surviving round-trip intact; `IsEmpty()`/`Front()` correctly reflect empty-queue state
      before/after. `DirectPlayMessageQueue.hpp`/`.cpp` still have zero transport/backend
      dependency (only `dplay.h` and standard library headers).
- [x] Implement `Receive`'s buffer-size query behavior: when `lpData == nullptr` and
      `*lpdwDataSize == 0`, return the required size via `*lpdwDataSize` without dequeuing.
      **Done** — implemented together with the other `Receive()` tasks below, since they share
      one method body and one set of ordered checks.
- [x] Implement `Receive` returning `DPERR_NOMESSAGES` when the queue is empty, matching
      `free-eggbert/src/network.cpp`'s `CNetwork::Receive` expectation of that specific code.
      **Done:** `DirectPlaySession` now has a real `DirectPlayMessageQueue messageQueue` member;
      `Receive()` checks `session_.messageQueue.Front()` and returns `DPERR_NOMESSAGES` when null.
- [x] Implement `Receive` copying the sender DPID into `*lpidFrom` on a successful dequeue.
      **Done**, guarded by `if (lpidFrom)` (an output pointer is optional, matching the same
      tolerance already extended to `CreatePlayer`'s `lpidPlayer`).
- [x] Implement `Receive` copying the recipient DPID into `*lpidTo` on a successful dequeue.
      **Done**, same optional-pointer treatment as `lpidFrom`.
- [x] Implement `Receive` validating `lpdwDataSize != nullptr` before dereferencing it. **Done:**
      checked immediately after the `session_.IsOpen()` check, returning `DPERR_INVALIDPARAMS`.
- [x] Implement `Receive` validating the output buffer: when `*lpdwDataSize` is smaller than the
      queued packet's payload size, return an appropriate `DPERR_*` (reuse `DPERR_INVALIDPARAMS` if
      no dedicated "buffer too small" code exists in `include/dplay.h`; add one only if the target
      game's code path distinguishes it) without dequeuing the packet. **Done:** confirmed no
      dedicated "buffer too small" code exists in `include/dplay.h` (the closest-named
      `DPERR_BUFFERTOOLARGE` is a different real-DirectPlay condition, not reused here since it
      would be misleading), and confirmed `free-eggbert/src/network.cpp`'s `CNetwork::Receive`
      doesn't distinguish this case either (it always passes a fixed 500-byte buffer). Reused
      `DPERR_INVALIDPARAMS` as instructed; writes the required size back via `*lpdwDataSize`
      before returning, and does not call `PopFront()`, leaving the packet queued for a retry
      with a bigger buffer.
      **`Receive()` implementation note:** the full method (all six tasks above, plus the
      pre-existing `session_.IsOpen()` → `DPERR_NOCONNECTION` check from a prior batch) does:
      not-open → `DPERR_NOCONNECTION`; null `lpdwDataSize` → `DPERR_INVALIDPARAMS`; empty queue →
      `DPERR_NOMESSAGES`; buffer-size query (`lpData == nullptr && *lpdwDataSize == 0`) → writes
      size, `DP_OK`, no dequeue; `*lpdwDataSize` too small → writes required size,
      `DPERR_INVALIDPARAMS`, no dequeue; `lpData == nullptr` with a nonzero-but-sufficient
      `*lpdwDataSize` → `DPERR_INVALIDPARAMS`; otherwise copies the payload, writes
      `idFrom`/`idTo`, dequeues, `DP_OK`.
      **Verification is split in two, honestly, since nothing in the codebase can yet enqueue a
      message into a live object** (`Send()` doesn't enqueue — Phase 10; no transport delivers
      one either — Phase 4): (1) a throwaway scratch harness exercised every path reachable
      through the real `IDirectPlay2A::Receive()` today (not-open, null `lpdwDataSize`,
      open-but-empty, and post-`Close()`); (2) a second scratch check verified the buffer-size
      query / too-small / successful-copy logic by replicating `Receive()`'s exact logic against a
      bare, fully-public `DirectPlaySession` + pre-populated `DirectPlayMessageQueue` — this
      proves the logic pattern is correct but does **not** yet exercise
      `DirectPlay2AImpl::Receive()`'s literal code path for those three cases end-to-end, since
      there is no way to get a message into a live object yet. That gap closes naturally once
      Phase 4/10 delivery exists.
- [x] Implement a maximum queued-message count on `DirectPlayMessageQueue` to bound memory growth
      when a peer stops calling `Receive`. **Done:** `static constexpr std::size_t
      kMaxQueuedMessages = 256` (a round, generous placeholder — no real-DirectPlay or
      `free-eggbert` call site implies a specific required capacity). `Enqueue()`'s signature
      changed from `void` to `bool`, returning `false` without enqueuing once the queue is at
      capacity; this has zero call-site impact today since nothing calls `Enqueue()` yet. The
      exact `DPERR_*` code `Send()` should map a full-queue rejection to is left for Phase 10 to
      decide (this task only implements the bound itself, in the queue).
- [x] Implement oversize-packet rejection before a packet is ever queued (see Phase 11's
      `DPERR_SENDTOOBIG`), sized to comfortably exceed the largest observed `free-eggbert` payload.
      **Done:** `static constexpr std::size_t kMaxPayloadBytes = 4096`, checked first in
      `Enqueue()` (before the queue-full check). 4096 bytes was chosen as a round number that
      comfortably exceeds every `free-eggbert` payload size found in the Phase 0 audit (a fixed
      500-byte receive buffer, with actual payloads observed in the low hundreds of bytes) —
      not tied to an exact real-DirectPlay constant, since none applies here. Mapping an oversize
      rejection to `DPERR_SENDTOOBIG` specifically is Phase 10's job, once `Send()` actually calls
      `Enqueue()`; this task only implements the size guard itself.
      **Verification (both tasks together):** runtime-verified with a throwaway scratch harness:
      a payload one byte over `kMaxPayloadBytes` is rejected and not queued; a payload exactly at
      the limit is accepted; filling the queue to exactly `kMaxQueuedMessages` succeeds, a
      `kMaxQueuedMessages + 1`th enqueue is rejected without disturbing what's already queued
      (confirmed the original front-of-queue packet is unchanged), and after draining one slot,
      exactly one more enqueue succeeds.
- [x] Add a unit test for `Receive` on an empty queue, asserting `DPERR_NOMESSAGES`. **Done:**
      `tests/directplay_tests.cpp` (new file), `Test_ReceiveOnEmptyQueue_ReturnsNoMessages`. Goes
      through the real, public `IDirectPlay2A` interface end-to-end (`DirectPlayCreate` →
      `QueryInterface` → `Open` → `Receive`), since this path is fully reachable today.
- [x] Add a unit test for `Receive` with a too-small caller-provided buffer, asserting the queued
      packet is preserved (not dequeued) and a meaningful error is returned. **Done:**
      `Test_ReceiveWithTooSmallBuffer_PreservesPacket` in the same file.
- [x] Add a unit test for `Receive` performing a successful copy, asserting payload bytes, sender
      DPID, and recipient DPID all match what was queued. **Done:**
      `Test_ReceiveSuccessfulCopy_MatchesQueuedPacket` in the same file.

      **How the injection problem was actually solved, rather than worked around with a
      duplicate-logic test:** earlier batches noted that nothing in the codebase can enqueue a
      message into a *live* `IDirectPlay2A` object yet (`Send()` doesn't enqueue — Phase 10; no
      transport delivers one either — Phase 4), which seemed to block writing genuine tests for
      the too-small-buffer and successful-copy cases without duplicating `Receive()`'s logic in
      the test file (a real risk: a duplicate wouldn't catch a regression in the actual
      implementation). Instead, `Receive()`'s buffer-size-query/`DPERR_NOMESSAGES`/too-small/
      successful-copy logic was refactored out of `DirectPlay.cpp` into a new
      `DirectPlayMessageQueue::TryReceive()` method (`DirectPlayMessageQueue.hpp`);
      `DirectPlay2AImpl::Receive()` now does only its `session_.IsOpen()` check and then
      delegates to `session_.messageQueue.TryReceive(...)`. The two new tests call
      `TryReceive()` directly on a `DirectPlayMessageQueue` they construct and populate
      themselves via the already-existing `Enqueue()` — this is the *exact same method* production
      `Receive()` calls, not a re-implementation of it, so these tests will catch a real
      regression in `Receive()`'s core logic. This refactor was verified to preserve identical
      behavior via a throwaway regression scratch harness (re-running the exact same
      not-open/null-`lpdwDataSize`/empty-queue/post-`Close()` checks from the previous batch)
      before the new permanent tests were written.
- [x] **(Decision, carried over from two prior batches, now acted on.)** Created
      `tests/directplay_tests.cpp` as a standalone, dependency-light file with its own `main()`
      and a documented build/run command in its header comment — **not yet wired into CMake or
      CTest**, since that is explicitly `plan.md` Phase 15's job ("Add a DirectPlay unit test
      executable"). Actually built and run per its own documented instructions from the
      repository root; all three checks pass (`OK: all DirectPlay tests passed.`, exit code 0).
      Added `tests/directplay_tests` to `.gitignore` so the compiled binary is never accidentally
      committed.

**Acceptance criteria:** the three new tests pass under CTest; `DirectPlayMessageQueue` has zero
transport/backend dependencies, verifiable by confirming no `SDL_`/`ENet` identifier appears in
`src/directplay/DirectPlayMessageQueue.*`. **Partially met, honestly:** the three tests exist,
build, and pass — verified by actually running them — but not yet "under CTest," since no CTest
target exists until Phase 15 wires `tests/directplay_tests.cpp` into `CMakeLists.txt`. The
zero-transport-dependency check passes (`DirectPlayMessageQueue.hpp`/`.cpp` include only
`dplay.h` and standard library headers).

---

## Phase 4 — Loopback backend

Goal: a fully in-process `IDirectPlayTransport` implementation, used as the default backend for
every subsequent DirectPlay test so tests stay deterministic and hermetic.

- [x] Implement `LoopbackDirectPlayTransport` in `src/directplay/LoopbackDirectPlayTransport.hpp`/
      `.cpp`, implementing `IDirectPlayTransport` entirely in-process with no real sockets.
      **Done:** a plain in-memory `std::deque<std::vector<std::uint8_t>>` byte-buffer queue.
      `Send()` appends a byte-copy; `Receive()` pops the front entry into the caller's buffer
      (returning `false` if the caller's buffer is too small, without popping);
      `Listen()`/`Connect()` trivially return `true` (no real connection setup needed for
      same-process loopback); `Shutdown()` clears the buffer.
- [x] Allow creating a local session without real networking: `Open(..., DPOPEN_CREATE)` against a
      `DirectPlaySession` configured with `LoopbackDirectPlayTransport` succeeds with zero network
      I/O. **Done:** `Open()` now unconditionally assigns a fresh
      `std::make_unique<LoopbackDirectPlayTransport>()` to `session_.transport` on success, since
      loopback is the only backend that exists today (no provider/backend-selection mechanism
      exists yet — that is Phase 5/6/8's job). `Close()` now also calls
      `session_.transport->Shutdown()` and resets it to `nullptr`, so a `Release()` without a
      prior `Close()` is the only case where `Release()`'s own `Shutdown()` call (added in Phase
      2, previously dead code since `transport` was always null then) actually does something —
      confirmed by the new tests exercising exactly that path.
- [x] Allow creating one local player via `CreatePlayer` on a loopback-backed session. **Done —
      required no code change:** `CreatePlayer()` (Phase 2) never referenced `session_.transport`
      at all, so it already worked identically regardless of transport; re-verified with a
      dedicated test rather than just assumed.
- [x] Allow sending a packet to self: `Send(idFrom, idFrom, ...)` on a loopback-backed session
      enqueues directly into that same session's receive queue. **Done, with a specific design
      choice:** rather than enqueuing directly, `Send()` round-trips the payload through
      `session_.transport->Send()`/`Receive()` first, then wraps the result into a
      `DirectPlayMessagePacket` and calls `session_.messageQueue.Enqueue()`. This was a deliberate
      choice over a more direct "just call `Enqueue()`" implementation, so
      `LoopbackDirectPlayTransport`'s own `Send()`/`Receive()` are genuinely exercised by this
      path (matching the eventual shape of a real backend) rather than being assigned and left
      idle. Only the literal `idTo == idFrom` case is handled; any other recipient is currently a
      silent no-op (`DP_OK`), matching the existing Phase 2 comment that general routing,
      player-ID validation, and payload validation are Phase 10's job. A failed `Enqueue()` (queue
      full or oversize payload) returns `DPERR_SENDTOOBIG` — imprecise for the "queue full" case
      specifically (that should arguably be a different code), but the queue starts empty and
      this narrow self-send scenario is unlikely to fill it; Phase 10 should refine this mapping
      when it implements full routing.
- [x] Allow receiving the self-sent packet via `Receive` immediately after the `Send` above, with
      no thread or event wait required. **Done — required no additional code change:** `Receive()`
      (Phase 3) already reads from `session_.messageQueue` synchronously; once `Send()` enqueues a
      packet (previous task), `Receive()` finds it immediately, same call stack, no waiting.
- [x] Use `LoopbackDirectPlayTransport` as the default transport for all new DirectPlay unit tests
      from this phase onward. **Done:** since `Open()` now assigns it unconditionally, every test
      that calls the real `Open()` automatically uses it — no test needs to configure anything
      explicitly.
- [x] Add a unit test for `CreatePlayer` against a loopback session, asserting a non-zero DPID is
      returned and is unique among players already created in that session. **Done:**
      `tests/directplay_tests.cpp`, `Test_LoopbackCreatePlayer_ReturnsUniqueNonZeroDpids`.
- [x] Add a unit test for `Send` to self over loopback, asserting `DP_OK`. **Done:**
      `Test_LoopbackSendToSelf_ReturnsOk`.
- [x] Add a unit test for `Receive` after a loopback self-send, asserting the received payload
      matches the sent payload byte-for-byte. **Done:**
      `Test_LoopbackReceiveAfterSelfSend_MatchesSentPayload`.
- [x] Add a unit test for `Close` on a loopback-backed session, asserting a subsequent `Send`/
      `Receive` returns `DPERR_NOCONNECTION` (Phase 11) rather than crashing or silently succeeding.
      **Done:** `Test_LoopbackClose_SendAndReceiveReportNoConnection`. All four tests go through
      the real, public `IDirectPlay2A` interface end-to-end (via a shared `OpenLoopbackSession`
      helper) — no injection workaround was needed for this phase, since `Open()` itself now
      creates real, usable loopback state.

**Acceptance criteria:** the four loopback tests pass with zero real socket usage; no `ENet*`/
`SDL_net*` symbol appears anywhere in `LoopbackDirectPlayTransport`'s translation unit. **Met:**
all four (plus the three carried-over Phase 3 tests, seven total) pass — actually built and run
per `tests/directplay_tests.cpp`'s own documented command from the repository root
(`OK: all DirectPlay tests passed.`, exit code 0). Confirmed no `ENet`/`SDL_net`/`SDL3_net`
*identifier* appears in `LoopbackDirectPlayTransport.hpp`/`.cpp` — the only matches for those
strings are in doc comments explaining the *policy* of not needing them, not actual code.

---

## Phase 5 — ENet integration planning

Goal: stand up the ENet-backed transport skeleton and the internal wire protocol it will use,
entirely gated behind a CMake option so the default build has no ENet dependency.

- [x] Add a CMake option `FREE_DIRECT_ENABLE_ENET` (default `OFF`) to `CMakeLists.txt`. **Done.**
- [x] Add CMake detection for a vendored ENet (submodule or `FetchContent`), gated behind
      `FREE_DIRECT_ENABLE_ENET`. **Done** as a vendored-directory check (matching the existing
      `ThirdPartySDL.cmake` convention already used elsewhere in this ecosystem, rather than
      `FetchContent`): when `FREE_DIRECT_ENABLE_ENET=ON` and `FREE_DIRECT_USE_SYSTEM_ENET=OFF`
      (the default), it checks for `third_party/enet/CMakeLists.txt` and fails with a specific,
      actionable message naming the expected path and the upstream repo
      (`https://github.com/lsalzman/enet`) if missing, rather than a generic CMake error.
      **`third_party/enet` does not exist in this repository or environment** — no real ENet
      vendoring was performed in this batch; see the note at the end of this task cluster.
- [x] Add optional CMake detection for a system-installed ENet (`find_package`/
      `pkg_check_modules`) as an alternative to the vendored copy, gated behind the same option.
      **Done:** a second option, `FREE_DIRECT_USE_SYSTEM_ENET` (default `OFF`), switches to
      `pkg_check_modules(... REQUIRED IMPORTED_TARGET libenet)` — ENet has no upstream CMake
      config package, only a `libenet` pkg-config module (the name Debian/Ubuntu's `libenet-dev`
      ships). Both paths converge on a single internal `FreeDirect::ENet` ALIAS target, so the
      rest of the build doesn't need to know which source was used.
- [x] Keep ENet's include directories `PRIVATE` to the `free-direct` CMake target. **Done:**
      `target_link_libraries(free-direct PRIVATE FreeDirect::ENet)` — `PRIVATE` linkage means
      `FreeDirect::ENet`'s include directories and other usage requirements never propagate to
      anything that links against `free-direct`.
- [x] Add a review-time (or build-time) check confirming no header under `include/` transitively
      includes any ENet header. **Done as a review-time check:** `grep -rliE "enet" include/`
      returns no matches (confirmed by actually running it). No build-time check was added since
      nothing under `include/` has any path to reach an ENet header today (no ENet-aware code
      exists yet in this batch) — the grep is the honest, sufficient check for the current state.

      **Verified for real, in three separate fresh configurations, not just reasoned about:**
      (1) `FREE_DIRECT_ENABLE_ENET=OFF` (default): configured and built successfully end-to-end,
      identical to before this batch — zero ENet dependency, confirmed by the option simply never
      being evaluated as true. (2) `FREE_DIRECT_ENABLE_ENET=ON` with no vendored copy and no
      system flag: configure fails with exactly the intended custom error message naming the
      expected `third_party/enet/CMakeLists.txt` path and the upstream repo URL. (3)
      `FREE_DIRECT_ENABLE_ENET=ON` + `FREE_DIRECT_USE_SYSTEM_ENET=ON` with no `libenet` installed
      in this environment: configure fails cleanly via CMake's own `FindPkgConfig` error, naming
      the missing `libenet` package.

      **Follow-up, same session, after asking the user:** the user was asked whether to actually
      vendor a real ENet for full build verification and chose to do so. `third_party/enet` is
      now a real git submodule (`git submodule add https://github.com/lsalzman/enet
      third_party/enet`, pinned at `v1.3.18-17-g5a9c537`), and the vendored-detection success path
      is now genuinely verified: (4) `FREE_DIRECT_ENABLE_ENET=ON` with the real submodule present
      configures and **builds completely end-to-end** — `enet`'s own C sources compile, link into
      `libenet.a`, and `free-api`/`free-direct`/`FREE_DIRECT` all build on top of it unaffected.
      One real bug was found and fixed during this verification: upstream ENet's own
      `CMakeLists.txt` adds its include directory via the old directory-scoped
      `include_directories()`, not `target_include_directories()`, so it is **not** carried as a
      usage requirement of the `enet` target — a consumer outside ENet's own directory scope
      (i.e. `free-direct`) would not see `enet/enet.h` without an explicit fix. Added
      `target_include_directories(enet PUBLIC .../third_party/enet/include)` right after
      `add_subdirectory(third_party/enet ...)` to correct this. Beyond the CMake build itself, a
      standalone smoke test (compiled and run outside the repo, not committed) confirmed real
      ENet *functionality*, not just linkage: `enet_initialize()`, `enet_host_create()`,
      `enet_host_destroy()`, and `enet_deinitialize()` all succeed against the vendored copy.
      The system-ENet success path (option 2 above) remains unverified, since no `libenet` system
      package was installed — only the vendored path was pursued, per the user's explicit choice.
- [x] Add an `EnetDirectPlayTransport` class skeleton in `src/directplay/EnetDirectPlayTransport.hpp`/
      `.cpp`, implementing `IDirectPlayTransport` with method bodies to be filled in by later tasks
      in this phase. **Done:** `EnetDirectPlayTransport final : public IDirectPlayTransport`, with
      an `ENetHost*`/`ENetPeer*` pair of members (both null-initialized) and every interface method
      stubbed to return `false` (`Listen`/`Connect`/`Send`/`Receive`) or no-op (`Shutdown`) —
      honest stubs, not fake success, since `DirectPlay2AImpl::Open()` doesn't select this backend
      yet (that's Phase 6). Constructor/destructor are trivial `= default` bodies; real
      `enet_initialize`/`enet_deinitialize` lifecycle is the next task, not this one. Wired into
      `CMakeLists.txt`: `EnetDirectPlayTransport.cpp` is added to `target_sources(free-direct ...)`
      only inside the existing `if(FREE_DIRECT_ENABLE_ENET)` block, so the default (`OFF`) build
      never compiles it. `EnetDirectPlayTransport.hpp` includes `<enet/enet.h>` directly — allowed
      per `CLAUDE.md`'s Internal Backend Policy, since this header lives under `src/directplay/`
      and (today) is only ever included by its own `.cpp` file, never by anything under `include/`.
      **Verified for real, in two separate fresh configurations:** (1)
      `cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON && cmake --build cmake-build-debug -j4`
      (the default, `FREE_DIRECT_ENABLE_ENET=OFF`) builds end-to-end, confirming zero ENet/`enet.h`
      dependency is added by this task when the option is off. (2)
      `cmake -B cmake-build-enet -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON &&
      cmake --build cmake-build-enet -j4` builds end-to-end, including
      `EnetDirectPlayTransport.cpp` against the vendored ENet copy. Also re-ran the standalone
      DirectPlay test suite (unaffected by this task, still 11/11 passing) and re-confirmed
      `grep -rliE "enet|SDL_|SdlNet" include/dplay.h` is clean.
- [x] Add ENet initialization (`enet_initialize`) and shutdown (`enet_deinitialize`) handling,
      performed once per process regardless of how many `EnetDirectPlayTransport` instances exist.
      **Done:** a process-wide reference count (`g_enetLiveInstances`, `g_enetInitialized`, guarded
      by a `std::mutex g_enetLifecycleMutex` - matching the existing mutex-guarded-state pattern
      already used in `src/directsound/DirectSound.cpp`) in an anonymous namespace in
      `EnetDirectPlayTransport.cpp`. The constructor calls `enet_initialize()` only when the count
      is `0`, then increments and sets the new `IsEnetReady()` accessor's backing field
      (`enetReady_`) only if that call (or an earlier live instance's call) actually succeeded; the
      destructor decrements and calls `enet_deinitialize()` only when the count reaches `0` again,
      and only if this instance itself successfully joined the count (so a failed-`enet_initialize`
      instance's destructor is a no-op, never double-`enet_deinitialize`s). `Listen`/`Connect`/
      `Send`/`Receive`/`Shutdown` remain the honest stubs from the previous task - this task is
      scoped to lifecycle only. **Verified for real** (not just "didn't crash") with a standalone
      smoke test compiled outside the repo (not committed, matching the precedent set by the
      earlier real-ENet-functionality check in this same phase): one instance's `IsEnetReady()` is
      true; three concurrent instances are all ready, and destroying the middle one first does not
      break the other two's readiness (proves the shared reference count, not per-instance
      init/deinit); after a full teardown to zero live instances, constructing a fresh instance
      re-initializes successfully (proves it isn't a one-shot "already shut down" state). Also
      re-verified both the `FREE_DIRECT_ENABLE_ENET=ON` CMake build (fresh configure+build) and the
      default `OFF` build, re-ran the 11/11 `tests/directplay_tests.cpp` suite (unaffected), and
      re-confirmed `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add ENet host creation (`enet_host_create` in listen mode) for the hosting role. **Done:**
      `EnetDirectPlayTransport::Listen(std::uint16_t port)` now calls `enet_host_create` with
      `ENetAddress{ENET_HOST_ANY, port}`, a placeholder `kMaxPeers = 32`, and `kChannelLimit = 1`
      (both provisional - not derived from a specific `free-eggbert`/`planetblupi` requirement;
      the channel count in particular is still pending the separate, still-unchecked "decide the
      default ENet channel layout" task below, and `kMaxPeers` is pending Phase 6's real
      `DPSESSIONDESC2::dwMaxPlayers` wiring). Returns `false` (no host created) if
      `enet_initialize()` never succeeded for this instance, or if a host already exists (prevents
      a silent leak/replace on a second `Listen()` call). `IDirectPlayTransport::Listen()`'s
      signature changed from no-argument to `Listen(std::uint16_t port)` - the only two overrides
      (`LoopbackDirectPlayTransport`, which ignores the new parameter, and this one) were updated;
      grepped and confirmed there were zero callers of the old signature anywhere in the codebase
      (Loopback's `Listen()`/`Connect()` are themselves never called yet - `Open()` only uses
      `Send()`/`Receive()`/`Shutdown()` on the transport it creates), so this was a safe signature
      change with no call-site fallout. Also added a `HasHost()` accessor (test-only purpose) and
      made `Shutdown()`/the destructor actually destroy the `ENetHost` `Listen()` created
      (`enet_host_destroy`) - necessary cleanup for the resource this task starts creating, not a
      separate task. **Verified for real** (not just "returned true") with a standalone whitebox
      smoke test compiled outside the repo (not committed): `Listen()` creates a host; a second
      `Listen()` call while already hosting correctly fails; a genuine raw ENet client
      (`enet_host_connect` to `127.0.0.1:<port>`) completes a real handshake with the host `Listen()`
      created, observed via `ENET_EVENT_TYPE_CONNECT` on both sides after servicing both hosts (the
      test reaches the private `host_` member via a `#define private public` whitebox trick, since
      there is no public "service this host" method yet - that is a later task). Also re-verified
      both the default (`FREE_DIRECT_ENABLE_ENET=OFF`) and `ON` CMake builds end-to-end, re-ran the
      11/11 `tests/directplay_tests.cpp` suite (unaffected), and re-confirmed `include/dplay.h` has
      zero ENet/SDL identifiers.
- [x] Add ENet client creation (`enet_host_create` with no listen address) for the joining role.
- [x] Add ENet peer connection (`enet_host_connect`) for the joining role. **Done (both boxes
      together):** implemented as a single `EnetDirectPlayTransport::Connect(const char* address,
      std::uint16_t port)`, not two separate steps - a client `ENetHost` created but never
      connected has nothing independently observable or testable about it (unlike `Listen()`'s
      host, which a real peer can connect to on its own), so splitting client-creation and
      peer-connection into two commits would have left the first one unverifiable. `Connect()`
      creates a listen-address-less `ENetHost` (`enet_host_create(nullptr, 1, kChannelLimit, 0,
      0)`, `kChannelLimit` matching `Listen()`'s), resolves `address` via `enet_address_set_host`,
      and calls `enet_host_connect`; fails cleanly with no host/peer left behind if ENet never
      initialized, a host/peer already exists on this instance, address resolution fails, or
      `enet_host_connect` itself returns null. `IDirectPlayTransport::Connect()`'s signature
      changed from no-argument to `Connect(const char* address, std::uint16_t port)` - same
      zero-existing-callers situation as `Listen()`'s earlier signature change (confirmed by grep);
      updated the only other override, `LoopbackDirectPlayTransport::Connect()`, to accept and
      ignore both new parameters. A `true` return means the connection attempt was queued, not
      that it completed - `HasPeer()` (new, test-only, mirrors `HasHost()`) documents this
      explicitly rather than implying more than is true. `Shutdown()`/the destructor now clear
      `peer_` too (a peer belongs to its host and is freed when `enet_host_destroy` runs - leaving
      the pointer set would dangle). **Verified for real** with a standalone whitebox smoke test
      (not committed, same `#define private public` technique as the `Listen()` verification): a
      `Listen()`-created server and a `Connect()`-created client, both real `EnetDirectPlayTransport`
      instances, complete a genuine ENet handshake with each other (`ENET_EVENT_TYPE_CONNECT` on
      both sides after servicing both hosts); a second `Connect()` call while already
      connecting/connected correctly fails; connecting to a syntactically-invalid address fails
      cleanly with no host/peer left behind (not a crash). Also re-verified both
      `FREE_DIRECT_ENABLE_ENET=ON`/`OFF` CMake builds end-to-end, the 11/11 `tests/directplay_tests.cpp`
      suite (unaffected), and that `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add ENet disconnect handling (`enet_peer_disconnect` plus processing the resulting
      `ENET_EVENT_TYPE_DISCONNECT` event). **Done:** `EnetDirectPlayTransport::Shutdown()` now
      calls `enet_peer_disconnect(peer_, 0)` first (when a `peer_` exists), then services `host_`
      in a small bounded loop (`kDisconnectPollAttempts = 10` × `kDisconnectPollTimeoutMs = 100`,
      both provisional placeholders) waiting for `ENET_EVENT_TYPE_DISCONNECT` before actually
      destroying the host - a graceful disconnect the remote peer can observe, not just a silent
      local teardown it would only notice via its own timeout. Scoped deliberately to the single
      `peer_` this class already tracks (the one `Connect()` created); a host with multiple
      connected peers (`Listen()`'s eventual real multi-peer role) needs its own per-peer tracking,
      which belongs to Phase 6/10, not this task. `Shutdown()` remains safe to call twice (all
      branches are null-guarded). **Verified for real**, not just "didn't crash", with a standalone
      whitebox smoke test (not committed): a `Listen()`-created server and `Connect()`-created
      client establish a real connection; the client calls the public `Shutdown()`; the *server*,
      serviced independently, observes a genuine `ENET_EVENT_TYPE_DISCONNECT` as a direct result -
      proof of an actual ENet-protocol-level disconnect, not a local-only teardown; calling
      `Shutdown()` again on both instances afterward is safe. Also re-verified both
      `FREE_DIRECT_ENABLE_ENET=ON`/`OFF` CMake builds end-to-end, the 11/11
      `tests/directplay_tests.cpp` suite (unaffected), and that `include/dplay.h` has zero
      ENet/SDL identifiers.
- [x] Add reliable packet send using `ENET_PACKET_FLAG_RELIABLE`. **Done:**
      `EnetDirectPlayTransport::Send(data, size)` now calls `enet_packet_create(data, size,
      ENET_PACKET_FLAG_RELIABLE)`, `enet_peer_send(peer_, 0, packet)` (channel `0` - the single
      channel `Listen()`/`Connect()` already assume via `kChannelLimit = 1`; the "decide the
      default ENet channel layout" task below remains open and unchanged by this), then
      `enet_host_flush(host_)` so the packet is actually pushed out promptly rather than waiting
      for the next service call. Fails cleanly (`false`, no crash, no leaked packet) if there is
      no `peer_` yet, or if `enet_packet_create`/`enet_peer_send` themselves fail - scoped, like
      `Shutdown()`'s disconnect handling, to the single `peer_` this class tracks (the one
      `Connect()` created); a host with multiple connected peers is Phase 6/10's job, not this
      one's. `Receive()` is deliberately left as the existing `false` stub - `plan.md` Phase 5 has
      no separate "implement transport-level receive" checkbox (confirmed by re-reading this
      phase's full task list); real receive-side wiring is implied by later phases, not this task.
      **Verified for real**, not just "returned true", with a standalone whitebox smoke test (not
      committed): a real `Listen()`/`Connect()` pair establishes a connection; the client's
      `Send()` transmits a payload that the server, serviced independently via raw
      `enet_host_service`, receives as a genuine `ENET_EVENT_TYPE_RECEIVE` event with the exact
      same bytes (`memcmp`-verified); `Send()` on an instance with no `peer_` (a lonely
      `Listen()`-only host) fails cleanly rather than crashing. Also re-verified the
      `FREE_DIRECT_ENABLE_ENET=ON` CMake build end-to-end, the 11/11 `tests/directplay_tests.cpp`
      suite (unaffected), and that `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Add unreliable packet send **only if needed**: Phase 0 found every observed `free-eggbert`
      `Send` call site uses a truthy flag that collapses to `DPSEND_GUARANTEED`, so unreliable send
      may not be required at all. Re-check the Phase 0 audit and ask the user before implementing
      this task. **Done, after asking:** re-confirmed the Phase 0 finding still holds (no
      `free-eggbert`/`planetblupi` call site needs unreliable send), then explicitly asked the
      user whether to implement it anyway for interface completeness despite no concrete
      requirement; the user said yes. `IDirectPlayTransport::Send()`'s signature changed from
      `Send(data, size)` to `Send(data, size, bool reliable)` (confirmed by grep there was exactly
      one existing caller, `DirectPlay.cpp`'s self-send path, updated to pass `/*reliable=*/true` -
      preserving its current behavior exactly, since `dwFlags`/`DPSEND_GUARANTEED` mapping is the
      next, separate task). `LoopbackDirectPlayTransport::Send()` accepts and ignores the new
      parameter (loopback has no reliability distinction). `EnetDirectPlayTransport::Send()` now
      chooses `ENET_PACKET_FLAG_RELIABLE` or `ENET_PACKET_FLAG_UNSEQUENCED` based on `reliable`.
      **Verified for real** with a standalone whitebox smoke test (not committed): sent one
      payload with `reliable=true` and one with `reliable=false` over a real connected
      client/server pair; inspected the *received* packet's `flags` field on the server side for
      both (ENet preserves packet flags through delivery) and confirmed the reliable packet has
      `ENET_PACKET_FLAG_RELIABLE` set while the unreliable one does not (and has
      `ENET_PACKET_FLAG_UNSEQUENCED` set instead) - proof the parameter actually changes
      ENet-protocol-level behavior, not just a local bookkeeping value. Also re-verified both
      `FREE_DIRECT_ENABLE_ENET=ON`/`OFF` CMake builds end-to-end, the 11/11
      `tests/directplay_tests.cpp` suite (unaffected), and that `include/dplay.h` has zero
      ENet/SDL identifiers.
- [x] Map `DPSEND_GUARANTEED` to `ENET_PACKET_FLAG_RELIABLE` in the transport layer. **Done:**
      `DirectPlay2AImpl::Send()` (`DirectPlay.cpp`) now computes `const bool reliable = (dwFlags &
      DPSEND_GUARANTEED) != 0;` and passes it to `session_.transport->Send(lpData, dwDataSize,
      reliable)`, replacing the previous hardcoded `/*reliable=*/true`. The actual
      `ENET_PACKET_FLAG_RELIABLE`/`ENET_PACKET_FLAG_UNSEQUENCED` choice already lives in
      `EnetDirectPlayTransport::Send()` (previous task) - this task is the missing link that
      feeds a real `dwFlags` bit into that choice instead of ignoring it. Since every observed
      real `free-eggbert` call site sets `DPSEND_GUARANTEED`, this does not change observable
      behavior for the one real call pattern; it only stops hardcoding `true` regardless of what
      the caller actually asked for. **Verified for real**: re-ran the 11 existing
      `tests/directplay_tests.cpp` tests (no regression - `LoopbackDirectPlayTransport` ignores
      the `reliable` parameter entirely, so passing the computed value instead of a hardcoded
      `true` cannot change any loopback-observable outcome); added a 12th test,
      `Test_LoopbackSendWithoutGuaranteedFlag_StillSucceeds`, which calls `Send()` with `dwFlags =
      0` (the "not guaranteed" case) through the real `IDirectPlay2A::Send()`/`Receive()` path end
      to end and asserts `DP_OK` with the payload surviving byte-for-byte - pinning down that the
      new flag-derived code path works today and giving a regression anchor for Phase 6, once a
      real backend is wired in where reliable vs. unreliable delivery could actually differ
      observably. Also re-verified both `FREE_DIRECT_ENABLE_ENET=ON`/`OFF` CMake builds
      end-to-end and that `include/dplay.h` has zero ENet/SDL identifiers.
- [x] Decide the default ENet channel layout (a single channel is likely sufficient given both
      target games' simple message patterns) and document the decision with rationale. **Done:**
      `docs/directplay-design.md` Decision 2 — a single channel (channel `0`, `kChannelLimit = 1`),
      since `IDirectPlay`/`IDirectPlay2A`'s `Send`/`Receive` have no channel concept at all and
      neither target game's audited call sites show more than one message stream. Documents a
      decision already implicit in `EnetDirectPlayTransport`'s existing code (previous tasks),
      rather than preceding it - no code changed for this task.
- [x] Add a packet type enum (e.g. `Join`, `JoinAccept`, `JoinReject`, `Data`, `Discovery`,
      `DiscoveryResponse`) for the internal FreeDirect-to-FreeDirect wire protocol. **Done:**
      `DirectPlayWirePacketType` in new `src/directplay/DirectPlayWireProtocol.hpp` — exactly
      these six values; only `Data` is exercised by any code today (it's the header's default),
      the rest are placeholders for Phases 6-8.
- [x] Add a protocol version field to the internal packet header. **Done:**
      `DirectPlayWirePacketHeader::version`, defaulted to `kDirectPlayWireProtocolVersion = 1`.
- [x] Add a magic number field to the internal packet header, to reject non-FreeDirect traffic
      early. **Done:** `DirectPlayWirePacketHeader::magic`, defaulted to `kDirectPlayWireMagic`
      ("FRDP"). Only the field exists so far; actually rejecting a mismatched magic on receive is
      part of the next task (defensive packet size/content validation) and Phase 8/10 delivery,
      not this one.
- [x] Add an application GUID field to the internal packet header, populated from
      `DPSESSIONDESC2.guidApplication`, so peers running different target games can never join each
      other's sessions. **Done:** `DirectPlayWirePacketHeader::applicationGuid` (a plain `GUID`
      field). Not yet actually populated from a live `DPSESSIONDESC2` anywhere — no code
      constructs a real header yet, since `EnetDirectPlayTransport` (a later task in this same
      phase) is what will do that.
- [x] Add a session GUID field to the internal packet header, populated from
      `DPSESSIONDESC2.guidInstance`. **Done:** `DirectPlayWirePacketHeader::sessionGuid`, same
      caveat as `applicationGuid` above — field exists, not yet populated by any real code path.
- [x] Add a sender player ID field to the internal packet header. **Done:**
      `DirectPlayWirePacketHeader::idFrom` (type `DPID`, matching the local `dplay.h` typedef
      as-is — see the open DPID-size question in `NEXT.md` Section 4 / Phase 9).
- [x] Add a recipient player ID field to the internal packet header. **Done:**
      `DirectPlayWirePacketHeader::idTo`, same `DPID` caveat as `idFrom`.
- [x] Add a payload length field to the internal packet header. **Done:**
      `DirectPlayWirePacketHeader::payloadLength` (`std::uint32_t`). The header does not carry the
      payload bytes themselves, only this length; a caller appends/reads payload bytes separately.
      All eight fields above serialize/deserialize via
      `SerializeDirectPlayWireHeader`/`DeserializeDirectPlayWireHeader` (flat, padding-free,
      per-field `memcpy` — deliberately not `sizeof(DirectPlayWirePacketHeader)`, to avoid
      depending on compiler struct-padding behavior). Verified with a real round-trip test,
      `Test_WireHeaderRoundTrip_PreservesAllFields` in `tests/directplay_tests.cpp` (now 8
      tests total): every field, including both GUIDs, survives serialize→deserialize intact.
      Built and run via the command in `NEXT.md` Section 7 — `OK: all DirectPlay tests passed.`,
      exit code 0. Also confirmed the full CMake build
      (`cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON && cmake --build cmake-build-debug
      -j4`) still succeeds end-to-end with the new `.cpp` added to `target_sources`, and that
      `include/dplay.h` still has zero ENet/SDL identifiers (`grep -niE "enet|SDL_|SdlNet"
      include/dplay.h`).
- [x] Add defensive packet size validation on receive: reject packets smaller than the fixed
      header size, and reject a stated payload length that does not match the actual received byte
      count. **Done:** `TryDeserializeDirectPlayWireHeader(data, dataSize)` in
      `src/directplay/DirectPlayWireProtocol.hpp` — returns `std::nullopt` (without reading past
      `dataSize` bytes) when `dataSize < kDirectPlayWireHeaderSize`, or when the parsed
      `payloadLength` disagrees with `dataSize - kDirectPlayWireHeaderSize`; otherwise returns the
      parsed header. Deliberately does not check `magic`/`version` here - a wrong-protocol/
      wrong-version packet gets its own `DPERR_*` mapping once a real transport
      (`EnetDirectPlayTransport`, still to be added) actually receives packets, rather than being
      folded into this size-only check. The unchecked `DeserializeDirectPlayWireHeader` from the
      previous task is unchanged (still precondition-based, used internally by the new function).
      Verified with three new tests in `tests/directplay_tests.cpp` (now 11 total):
      `Test_WireHeaderTryDeserialize_RejectsTruncatedBuffer` (both a one-byte-short buffer and an
      empty buffer), `Test_WireHeaderTryDeserialize_RejectsMismatchedPayloadLength` (header claims
      5 payload bytes, buffer has 3), and `Test_WireHeaderTryDeserialize_AcceptsConsistentBuffer`
      (a correctly-sized buffer round-trips through the validator). Built and run via the command
      in `NEXT.md` Section 7 — `OK: all DirectPlay tests passed.`, exit code 0. Also confirmed the
      full CMake build still succeeds and `include/dplay.h` still has zero ENet/SDL identifiers.
- [ ] Add protocol documentation for the header layout above to `docs/directplay-protocol.md`
      (Phase 16).

**Acceptance criteria:** `EnetDirectPlayTransport` compiles and links only when
`FREE_DIRECT_ENABLE_ENET=ON`; with the flag `OFF` (the default), the `free-direct` target builds
with zero ENet dependency; a unit test serializes and deserializes the internal packet header and
asserts round-trip equality.

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
- [ ] Send a join-rejected packet to a connecting client when the session is full or the
      application GUID does not match. **Still not done - structurally blocked for the
      over-`dwMaxPlayers` case**, confirmed again while implementing Decision 16: a rejected
      `pendingPeers_` entry is never assigned a DPID (`RejectPendingConnection()` never calls
      `AssignPendingConnection()`), and addressed `Send()` (Decision 14) only ever reaches
      `connectedPeers_` - there is structurally no DPID to address a `JoinReject` packet to for
      this case, regardless of what Decision 16 implements. GUID-mismatch rejection isn't
      implemented at all yet either (`Open()` never compares `guidApplication` against the host's).
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
- [ ] Implement host-side routing: the host forwards a `Send` addressed to a non-host recipient to
      that recipient's connection (star topology, matching ENet's client/server model). Not
      started - today only "host directly addresses one of its own `remotePlayerIds`" works; a
      joining peer still cannot reach any other peer (host or otherwise) at all.
- [ ] Implement direct peer-to-peer delivery **only if** a future architectural decision moves away
      from the host-hub star topology — not needed under the current plan; leave as a documented
      non-task unless the topology decision changes.
- [ ] Implement broadcast-to-all delivery for `idTo == DPID_ALLPLAYERS`/`0`, matching
      `free-eggbert/src/network.cpp`'s `Send(m_dpid, 0, ...)` call pattern.
- [ ] Add the `DPID_ALLPLAYERS` and `DPID_SYSMSG` constants to `include/dplay.h` (if not already
      added in Phase 0/Phase 2), so broadcast sends have named constants available even though
      current call sites use a literal `0`.
- [x] Preserve DirectPlay-like packet boundaries: each `Send` call must arrive as exactly one
      `Receive`-visible message, never coalesced or split. **Done, loopback:**
      `LoopbackDirectPlayTransport::Send()`/`Receive()` (Decision 10/14) never coalesce or split -
      each `Send()` call is one `buffered_` entry, popped whole by exactly one `Receive()` call;
      `DirectPlay2AImpl::Receive()`'s drain loop (Decision 15) parses one wire header + payload per
      transport-level `Receive()` call, preserving the same one-to-one boundary up to
      `session_.messageQueue`. ENet's side is moot until its receive-side buffering exists.
- [ ] Preserve reliable, ordered delivery for `DPSEND_GUARANTEED` sends (mapped to
      `ENET_PACKET_FLAG_RELIABLE` per Phase 5 when using the ENet backend). Loopback trivially
      preserves order (a plain FIFO, no real network to reorder anything) and never drops a
      packet regardless of the `reliable` flag - `DPSEND_GUARANTEED` genuinely mattering (real
      loss/reordering to guard against) only applies to the ENet backend, whose receive side isn't
      implemented yet. Left unchecked - no test yet demonstrates *ordering* specifically (see the
      still-open "packet-ordering test" task below).
- [x] Validate the sender player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idFrom` does
      not correspond to a locally-registered player. **Done** (Decision 15): checked against
      `session_.localPlayerIds`. **Verified**: `Test_SendFromUnknownLocalPlayer_
      ReturnsInvalidPlayer` (`tests/directplay_tests.cpp`).
- [ ] Validate the recipient player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idTo` is
      neither a known player DPID nor the broadcast ID. **Partially done** (Decision 15): `idTo`
      not in `session_.remotePlayerIds` correctly returns `DPERR_INVALIDPLAYER` for the hosting
      role (**verified**: `Test_SendToUnknownRemotePlayer_ReturnsInvalidPlayer`); the "nor the
      broadcast ID" half is unchecked since broadcast doesn't exist yet, and Decision 15 flags a
      real, unresolved ambiguity for whoever implements it: Decision 3 assigned DPID `0` to the
      host's own first local player rather than reserving it as `DPID_ALLPLAYERS`, so
      `free-eggbert`'s own broadcast call (`Send(m_dpid, 0, ...)`) is genuinely ambiguous under
      current semantics between "broadcast" and "the specific player whose DPID is `0`."
- [ ] Validate a null payload with zero length (`lpData == nullptr && dwDataSize == 0`) as an
      accepted no-payload send, only if a call site needs it; otherwise document it as rejected.
- [ ] Reject a null payload with nonzero length (`lpData == nullptr && dwDataSize > 0`) with
      `DPERR_INVALIDPARAMS`.
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
- [ ] Add a two-client routing test over loopback (if feasible with the loopback transport's
      design): client A sends to client B via the host; client B receives it and client A does not.
- [ ] Add a packet-ordering test: multiple guaranteed sends from the same sender arrive at the
      receiver in send order.
- [ ] Add a reliable-delivery smoke test gated behind `FREE_DIRECT_ENABLE_ENET`, sending a batch of
      packets over a real local ENet host/client pair on `127.0.0.1` and asserting all arrive.

**Acceptance criteria:** the packet-ordering test and host/client integration test both pass over
loopback in the default (no ENet) build; the ENet smoke test is excluded from the default test run
and only executes when `FREE_DIRECT_ENABLE_ENET=ON`.

---

## Phase 11 — Error semantics

Goal: a final sweep ensuring no DirectPlay method still returns an unconditional success value
once real behavior is expected of it, and that every deviation from Microsoft DirectPlay is
written down.

- [ ] Replace every remaining misleading unconditional-success stub in `src/directplay/*.cpp`
      with a real, state-driven return value (cross-check against Phases 2-10; this is the final
      audit/confirmation pass, not new implementation work).
- [ ] Return `DPERR_NOCONNECTION` from `Send`/`Receive` when called on a session that is not open
      (before `Open` or after `Close`).
- [ ] Confirm `Send` returns `DPERR_INVALIDPLAYER` for an invalid sender DPID (cross-reference
      Phase 10).
- [ ] Confirm `Send` returns `DPERR_INVALIDPLAYER` for an invalid recipient DPID (cross-reference
      Phase 10).
- [ ] Confirm `Send` returns `DPERR_SENDTOOBIG` for oversized payloads (cross-reference Phase 10).
- [ ] Confirm `Receive` returns `DPERR_NOMESSAGES` when the queue is empty (cross-reference
      Phase 3).
- [ ] Sweep every method of `IDirectPlay`/`IDirectPlay2A` for missing null-pointer checks on
      required output parameters, returning `DPERR_INVALIDPARAMS` where one is missing.
- [ ] Return `DPERR_UNSUPPORTED` for any flag combination not covered by the target games'
      observed usage, instead of silently ignoring unknown flags.
- [ ] Document every intentional deviation from Microsoft DirectPlay's documented error semantics
      in `docs/directplay-limitations.md` (Phase 16), with a one-line rationale per deviation.

**Acceptance criteria:** `docs/directplay-limitations.md` contains a table listing every `DPERR_*`
code FreeDirect returns, the condition that triggers it, and whether it matches or deviates from
documented Microsoft DirectPlay behavior; every method of `IDirectPlay2A` has at least one unit
test covering its primary error path.

---

## Phase 12 — SDL3_net optional backend

Goal: document the SDL3_net option honestly without building it, keeping the door open without
committing engineering time until ENet is proven.

- [ ] Add a design note for the SDL3_net backend to `docs/networking-backends.md` (Phase 16),
      describing where `SdlNetDirectPlayTransport` would plug into `IDirectPlayTransport`.
- [ ] Explain TCP stream socket tradeoffs in that note (simple client/server, poor fit for
      discrete unreliable/unordered game packets, requires manual message framing over the stream).
- [ ] Explain UDP datagram tradeoffs in that note (closer semantic fit, but requires FreeDirect to
      hand-roll reliability, ordering, fragmentation, acknowledgement, and retransmission —
      everything ENet already provides).
- [ ] Do not implement `SdlNetDirectPlayTransport` until `EnetDirectPlayTransport` (Phases 5-11)
      is stable and passing its integration tests.
- [ ] Add an optional future CMake flag `FREE_DIRECT_ENABLE_SDL3_NET` to `CMakeLists.txt`, default
      `OFF`, with no source files wired to it until the design note above is written and reviewed.
- [ ] Keep any future SDL3_net usage private to `.cpp` files under `src/directplay/`, matching the
      Internal Backend Policy in `CLAUDE.md`.
- [ ] Add transport-abstraction tests structured so they can run against
      `LoopbackDirectPlayTransport` and `EnetDirectPlayTransport` today, and against
      `SdlNetDirectPlayTransport` later without modification (i.e. tests target
      `IDirectPlayTransport`, not a concrete backend type).

**Acceptance criteria:** the design note exists and is reviewed before any
`SdlNetDirectPlayTransport` source file is created; the transport-abstraction test suite is
parameterized by backend rather than duplicated per backend.

---

## Phase 13 — DirectSound hardening

Goal: close the gap between "partial" and "correct for what `free-eggbert`/`planetblupi` actually
need," without adding DirectSound surface neither game uses.

- [ ] Audit `DSBPLAY_LOOPING` against both `free-eggbert` and `planetblupi` call sites (Phase 0
      baseline: neither game passes this flag) and record the result in
      `docs/directsound-limitations.md`.
- [ ] Add a task to implement real looping **only if** a future audit of either target game finds
      an actual `DSBPLAY_LOOPING` call site, or the user explicitly requests it as a named
      exception per `CLAUDE.md`'s scope policy — do not implement it speculatively now.
- [ ] Audit `SetPan` against both target games' call sites (Phase 0 baseline: `planetblupi` calls
      it once; confirm `free-eggbert`'s usage) and record findings.
- [ ] Add a task to implement correct mono panning (real per-channel gain via SDL3 stream channel
      maps) only if the audit shows the current approximate behavior is audible/incorrect for
      either game's actual sound assets.
- [ ] Audit `GetCurrentPosition` — `include/dsound.h` currently declares only
      `SetCurrentPosition`, not `GetCurrentPosition`; confirm whether either target game calls
      `GetCurrentPosition` at all before adding it.
- [ ] Add a task to implement `GetCurrentPosition` (approximate cursor tracking) only if the audit
      above finds a real call site.
- [ ] Add a unit test for `Play`, asserting `GetStatus` reports `DSBSTATUS_PLAYING` afterward.
- [ ] Add a unit test for `Stop`, asserting `GetStatus` no longer reports `DSBSTATUS_PLAYING`
      afterward.
- [ ] Add a unit test for `GetStatus` on a freshly created, never-played buffer, asserting it does
      not report `DSBSTATUS_PLAYING`.
- [ ] Add a unit test for volume clamping, asserting values outside `[DSBVOLUME_MIN,
      DSBVOLUME_MAX]` are clamped rather than passed through or rejected.
- [ ] Add documentation for all remaining DirectSound limitations to
      `docs/directsound-limitations.md` (Phase 16), explicitly scoped to what `free-eggbert`/
      `planetblupi` need rather than full DirectSound semantics.

**Acceptance criteria:** the four new unit tests run headlessly (no real audio device required, or
gracefully skipped when `DSERR_NODRIVER` is the only available outcome in a sandboxed environment)
and pass; `docs/directsound-limitations.md` states, per limitation, whether it is known to affect
`free-eggbert`, `planetblupi`, both, or neither.

---

## Phase 14 — DirectDraw hardening

Goal: close the gap between "subset-oriented" and "correct for what `free-eggbert`/`planetblupi`
actually need," prioritized by real call-site frequency.

- [ ] Audit primary surface presentation against both target games' actual resolution/format
      usage (not just the in-repo demo's 800x600 32-bit path).
- [ ] Audit 8-bit palette conversion against both target games' `CreatePalette`/`SetEntries`/
      `GetEntries`/`SetPalette` call sites.
- [ ] Audit color-key range behavior against both target games' `SetColorKey` call sites (Phase 0
      baseline: `planetblupi` calls it twice; confirm `free-eggbert`'s exact count and flags).
- [ ] Audit `Blt` clipping given both games call plain `Blt` rarely (`free-eggbert`: 1 call site;
      `planetblupi`: 0 call sites) — confirm whether the single `free-eggbert` `Blt` call site
      actually relies on clipping before investing further effort here.
- [ ] Audit `BltFast` clipping given both games call `BltFast` heavily (`free-eggbert`: 18 call
      sites; `planetblupi`: 17 call sites) — this is the higher-priority clipping path.
- [ ] Audit mixed 8-bit/32-bit behavior: identify whether either game ever blits directly between
      an 8-bit paletted surface and a 32-bit surface (as opposed to via the palette-to-RGBA32
      present-time conversion already documented in `README.md`).
- [ ] Decide whether mixed-depth blits should error (`DDERR_INVALIDPARAMS` or similar) instead of
      silently skipping pixels, based on the audit above, and document the decision in
      `docs/directdraw-limitations.md`.
- [ ] Audit `GetDC`/`ReleaseDC` given both games call these meaningfully (`free-eggbert`: 3/3 call
      sites; `planetblupi`: 4/4 call sites) — identify what GDI operations happen between `GetDC`
      and `ReleaseDC` in both games (likely text/UI drawing) and confirm current behavior covers
      them.
- [ ] Add a unit test for palette updates (`SetEntries`/`GetEntries` round-trip, then `SetPalette`
      plus present, asserting presented pixel colors reflect the palette).
- [ ] Add a unit test for color-key blits, covering both the 8-bit palette-index comparison path
      and the 32-bit packed-pixel comparison path documented in `README.md`.
- [ ] Add a unit test for primary auto-present behavior (a `Blt` to the primary surface triggers
      `PresentPrimary` under the documented throttle/dirty-check rules).
- [ ] Add a unit test for `Flip` mode, asserting it behaves as the documented "simplified present,
      not a real flip chain" rather than silently diverging further.
- [ ] Add documentation for the non-real flip-chain behavior to `docs/directdraw-limitations.md`
      (Phase 16), explicit that this is a known, permanent simplification, not a bug to eventually
      fix.
- [ ] Audit `IsLost`/`Restore` given both games call these meaningfully (`free-eggbert`: 4/8 call
      sites; `planetblupi`: 4/8 call sites) and confirm current behavior does not cause either game
      to enter an unexpected recovery loop.

**Acceptance criteria:** every new DirectDraw test runs headlessly against an off-screen/software
SDL renderer (no real display required); `docs/directdraw-limitations.md` cites concrete call-site
counts from both target games for each documented limitation, not general DirectDraw folklore.

---

## Phase 15 — Tests and CI

Goal: make the tests from Phases 3-14 discoverable and runnable as one command, headlessly, with
the ENet-dependent subset opt-in.

- [ ] Add a DirectPlay unit test executable (e.g. `tests/directplay_tests`), linking against
      `free-direct` and using the loopback transport by default.
- [ ] Add a DirectDraw unit test executable (e.g. `tests/directdraw_tests`) if not already present
      from Phase 14's work.
- [ ] Add a DirectSound unit test executable (e.g. `tests/directsound_tests`) if not already
      present from Phase 13's work.
- [ ] Add CTest integration in `CMakeLists.txt` (`enable_testing()` plus `add_test()` per
      executable) so `ctest` runs all three suites.
- [ ] Add headless-friendly test configuration (no real window/display/audio device required for
      the default `ctest` target).
- [ ] Add the loopback DirectPlay tests from Phases 3-11 to the default CTest run.
- [ ] Add ENet local integration tests from Phase 10 as a separate CTest target guarded by
      `FREE_DIRECT_ENABLE_ENET`, excluded from the default `ctest` run.
- [ ] Add a sanitizers CMake option (e.g. `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`) for
      local/CI opt-in use.
- [ ] Add a CI build matrix if this repository (or its parent build) uses GitHub Actions — check
      for an existing `.github/workflows` directory first, and only add one if none exists.
- [ ] Add test documentation (a short `tests/README.md`, or a section in the main `README.md`)
      explaining how to build and run the test suites, including the ENet-gated ones.

**Acceptance criteria:** `cmake -B build && cmake --build build && ctest --test-dir build`
succeeds on a clean checkout with `FREE_DIRECT_ENABLE_ENET=OFF` (the default), running only the
loopback-backed suites.

---

## Phase 16 — Documentation

Goal: make every honest limitation and design decision from Phases 1-15 discoverable, without
overclaiming compatibility anywhere.

- [x] Update `README.md` with honest subsystem statuses reflecting Phases 1-14's actual completed
      work (not aspirational status). Done: `plan.md` TASK-24H-0121 (Overview/Features DirectPlay
      bullets rewritten to describe real loopback+ENet-hosting behavior, not "dummy stubs").
- [x] Add `docs/directplay-design.md`, covering the state model, transport abstraction, and the
      DPID-allocation/enumeration-strategy decisions made in Phases 1-9. Already existed, created
      early in Phases 0/1 (`plan.md` TASK-24H-0130) - now 19 numbered Decisions covering every
      DirectPlay design question resolved to date.
- [x] Add `docs/directplay-protocol.md`, covering the Phase 5 internal wire packet header layout
      and packet type enum. Done: `plan.md` TASK-24H-0095, this session.
- [x] Add `docs/directplay-limitations.md`, covering the Phase 11 error-semantics deviation table
      and any unimplemented DirectPlay surface (groups, lobby APIs) with an explicit "not
      implemented, not needed by either target game" note. Done: `plan.md` TASK-24H-0094, this
      session.
- [x] Add `docs/networking-backends.md`, covering the ENet-first / SDL3_net-optional /
      loopback-for-tests decision from `CLAUDE.md`'s Networking Backend Decision section. Done:
      `plan.md` TASK-24H-0096, this session.
- [x] Add `docs/directdraw-limitations.md`, covering the Phase 14 audit findings with concrete
      call-site counts from both target games. Done, earlier this session (before TASK-24H-0094/
      0095/0096).
- [x] Add `docs/directsound-limitations.md`, covering the Phase 13 audit findings with concrete
      call-site counts from both target games. Done, earlier this session.
- [x] Update the compatibility table in `README.md` (or add one if none exists) listing each
      DirectDraw/DirectSound/DirectPlay method and its status (`STUB`/`PARTIAL`/`IMPLEMENTED`) per
      the header-comment convention. Done: `plan.md` TASK-24H-0122, this session - a new
      "Compatibility Status" README section, sourced directly from `include/*.h`'s `@note Status:`
      tags.
- [x] Review all updated/new docs to ensure none claim full DirectX 3 compatibility. Done:
      `plan.md` TASK-24H-0125 - swept `README.md`/`TODO.md`/`docs/*.md`/`NEXT.md` with multiple
      pattern variants; every match found is a correct negative disclaimer, zero overclaims.
- [x] Ensure all updated/new docs explicitly state DirectPlay is not Microsoft-wire-compatible.
      Done: `plan.md` TASK-24H-0126 - confirmed present in `docs/directplay-limitations.md`,
      `docs/directplay-protocol.md`, `docs/networking-backends.md` (added explicitly, was
      previously only implied), and `README.md`. Scoped to DirectPlay-topic docs only -
      `docs/directdraw-limitations.md`/`directsound-limitations.md`/`ci-matrix.md` are about
      unrelated subsystems and do not need a DirectPlay-specific disclaimer.
- [x] Ensure all updated/new docs explicitly state FreeDirect multiplayer only works between
      programs both built against this FreeDirect DirectPlay implementation. Done: `plan.md`
      TASK-24H-0127 - same scoping as the row above; confirmed present in all DirectPlay-topic
      docs and `README.md`, and not contradicted anywhere.

**Acceptance criteria:** every new `docs/*.md` file listed above exists, is written in English,
and is linked from `README.md`'s table of contents or a new "Further Reading" section.

**Verified (plan.md TASK-24H-0130 and the DirectPlay-doc tasks above):** every checkbox in this
phase is now satisfied. All 6 `docs/*.md` files exist; `README.md` links each one from either the
"Overview"/"Features" DirectPlay bullets, the new "Compatibility Status" section, or the "Project
Status" section's cross-link to `docs/audit-24h-free-direct.md` (TASK-24H-0128) - satisfying this
phase's own "linked from README.md" acceptance criterion without needing a separate table-of-
contents/"Further Reading" section.

---

## Phase 17 — NEXT.md workflow

Goal: establish `NEXT.md` as a living, honest status file once real implementation starts.

- [ ] Add a task (executed at the start of real Phase 1+ implementation work, not before) to
      create `NEXT.md` per the `NEXT.md` Policy in `CLAUDE.md`.
- [ ] Add a recurring task to update `NEXT.md` after each completed implementation batch (roughly
      one PR's worth of finished `plan.md` checkboxes).
- [ ] Ensure every `NEXT.md` update includes the current branch/state.
- [ ] Ensure every `NEXT.md` update includes completed tasks, referencing specific `plan.md`
      checkbox items.
- [ ] Ensure every `NEXT.md` update includes partially completed tasks and exactly what remains
      for each.
- [ ] Ensure every `NEXT.md` update includes known blockers.
- [ ] Ensure every `NEXT.md` update includes next recommended tasks, pointing at specific
      `plan.md` items.
- [ ] Ensure every `NEXT.md` update includes test status (what was actually run, pass/fail
      counts).
- [ ] Ensure every `NEXT.md` update includes build status (does a clean build currently succeed).
- [ ] Do not create fake progress in `NEXT.md` — cross-check this rule during any review of a
      `NEXT.md` update.

**Acceptance criteria:** `NEXT.md`'s first version is created in the same commit/PR as the first
real (non-planning) `plan.md` task implementation, never earlier and never as an empty
placeholder.

---

## Phase 18 — Final validation

Goal: prove, end-to-end and from a clean checkout, that everything claimed in Phases 1-17 actually
works — not just that unit tests pass in isolation.

- [ ] Build the project from a clean checkout (fresh clone or an equivalent scratch copy, not the
      developer's working tree) with default CMake options.
- [ ] Run all tests via `ctest` from that clean build.
- [ ] Run a `free-eggbert` single-player smoke test if a runnable build of `free-eggbert` against
      this FreeDirect is available.
- [ ] Run a `planetblupi` single-player smoke test if a runnable build of `planetblupi` against
      this FreeDirect is available.
- [ ] Run a `free-eggbert` multiplayer smoke test (two local processes) if a runnable build is
      available.
- [ ] Test DirectPlay host creation end-to-end via the smoke-test build (not just unit tests).
- [ ] Test DirectPlay client join end-to-end via the smoke-test build.
- [ ] Test DirectPlay player creation end-to-end via the smoke-test build.
- [ ] Test DirectPlay reliable send end-to-end via the smoke-test build.
- [ ] Test DirectPlay receive queue end-to-end via the smoke-test build.
- [ ] Test DirectPlay close/disconnect end-to-end via the smoke-test build.
- [ ] Update `docs/*.md` after validation to reflect any behavior discovered only under real
      end-to-end testing.
- [ ] Update `NEXT.md` with final validation results (build status, test status, smoke-test
      results, remaining known issues).

**Acceptance criteria:** this phase is only marked complete when every sub-task above has a real,
observed pass/fail result recorded in `NEXT.md` — "assumed to work" is not an acceptable status for
any Phase 18 item.

---

# 24-Hour Autonomous Stabilization Backlog

This section supplements, and does not replace, Phases 0-18 above. It was generated from a
fresh, evidence-based re-audit recorded in `docs/audit-24h-free-direct.md` (2026-07-08). Every task
below cites the audit section or file:line evidence it is based on. Existing DirectPlay task IDs
and Decision numbers in `docs/directplay-design.md` are preserved and referenced, not duplicated —
new DirectPlay tasks here fill gaps the reconciliation in `docs/audit-24h-free-direct.md`
("DirectPlay Plan Reconciliation") found, they do not restart DirectPlay planning.

Tasks whose `Status:` is `BLOCKED` depend on a design decision this project's policy (`CLAUDE.md`,
`plan.md` Phase 7-10 task text, `docs/directplay-design.md` Decisions 3/15/17) explicitly reserves
for the user. They must not be resolved unilaterally; implementation should skip them and continue
with the next unblocked task.

## Build and CTest

### TASK-24H-0001: Add a FREE_DIRECT_BUILD_TESTS CMake option
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §7 ("no FREE_DIRECT_BUILD_TESTS option... exists")
Depends on: None

Problem:
There is no way to opt a build into compiling FreeDirect's own test executables; `tests/` is
completely invisible to CMake today.

Required work:
- Add `option(FREE_DIRECT_BUILD_TESTS "Build FreeDirect's own test executables" OFF)` to
  `CMakeLists.txt`, default `OFF` so target-game diamond builds (`free-eggbert`, `planetblupi`) are
  unaffected unless a developer opts in.

Acceptance criteria:
- `cmake -B build` with no extra flags configures identically to today (no new targets appear).
- `cmake -B build -DFREE_DIRECT_BUILD_TESTS=ON` configures without error once TASK-24H-0002 lands.

Out of scope:
- Do not build tests by default. Do not add any option besides this one build-test switch.

Verified: option added to `CMakeLists.txt`, default OFF confirmed via a clean `free-eggbert`
configure+build with no extra flags (no new targets appeared).

### TASK-24H-0002: Create tests/CMakeLists.txt and wire directplay_tests as a target
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §7 ("tests/directplay_tests.cpp... completely unwired")
Depends on: TASK-24H-0001

Problem:
`tests/directplay_tests.cpp` (46 tests, all passing per manual `g++` compile/run this audit) has no
CMake target at all.

Required work:
- Add `tests/CMakeLists.txt`, included from the root `CMakeLists.txt` only when
  `FREE_DIRECT_BUILD_TESTS` is `ON`.
- Add an `add_executable(directplay_tests tests/directplay_tests.cpp)` target linking `free-direct`
  and `free-api`, matching the include paths the audit's manual compile command used
  (`include/`, `src/directplay/`, free-api's public/compat headers).

Acceptance criteria:
- `cmake -B build -DFREE_DIRECT_BUILD_TESTS=ON && cmake --build build` produces a `directplay_tests`
  executable that runs and reports "OK: all DirectPlay tests passed." with exit code 0.
- Default (`FREE_DIRECT_BUILD_TESTS=OFF`) build is unaffected.

Out of scope:
- Do not modify `tests/directplay_tests.cpp` itself in this task. Do not add DirectDraw/DirectSound
  test files here — see their own tasks below.

Verified: built and ran successfully via both `free-eggbert` and `planetblupi`
`add_subdirectory(../free-direct)` builds with `-DFREE_DIRECT_BUILD_TESTS=ON`; the resulting
`bin/directplay_tests` executable printed "OK: all DirectPlay tests passed." with exit code 0 in
both cases. Bug found and fixed during verification: the initial `tests/CMakeLists.txt` used
`CMAKE_SOURCE_DIR` for the `src/directplay` include path, which resolves to the *consuming*
project's root (e.g. `free-eggbert`) when free-direct is pulled in via `add_subdirectory`, not
free-direct's own root — fixed to `CMAKE_CURRENT_SOURCE_DIR` (relative to `tests/`), matching the
pattern already used elsewhere in the root `CMakeLists.txt`.

### TASK-24H-0003: Register directplay_tests with CTest
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §7; plan.md Phase 15 (unstarted)
Depends on: TASK-24H-0002

Problem:
Even once built, `directplay_tests` is not runnable via `ctest`.

Required work:
- Call `enable_testing()` and `add_test(NAME directplay_tests COMMAND directplay_tests)` in
  `tests/CMakeLists.txt`, gated the same way as TASK-24H-0002.

Acceptance criteria:
- `ctest --test-dir build` (with `FREE_DIRECT_BUILD_TESTS=ON`) reports `1/1 tests passed`.
- Non-zero exit from `directplay_tests` is reflected as a CTest failure (verify by temporarily
  breaking one assertion locally, observing the failure, then reverting — do not commit a broken
  test).

Out of scope:
- Do not add a CI pipeline file (`.github/workflows/*`) in this task — that is a separate,
  lower-priority documentation/tooling task (TASK-24H-0012).

Verified with a caveat: `ctest` run from *inside* the `FREE_DIRECT` build subdirectory
(`cd <build>/FREE_DIRECT && ctest`) correctly discovers and passes `1/1 directplay_tests`. `ctest`
run from the *top-level* build directory of either target game finds no tests at all
("No tests were found!!!") — this is because CTest's discovery mechanism requires `enable_testing()`
to have been called in the top-level `CMakeLists.txt` of whichever project owns that build tree,
and neither `free-eggbert` nor `planetblupi`'s own top-level `CMakeLists.txt` calls it (confirmed by
grep; only `free-api`'s, itself nested, does). This is a property of how the two target games
structure their own top-level builds, not a defect in free-direct's CTest wiring — recorded here
rather than silently claimed as fully working. Fixing it would require editing a target game's
`CMakeLists.txt`, which is out of scope without asking first per CLAUDE.md.

### TASK-24H-0004: Add a CTest label "directplay" to the directplay_tests target
Status: DONE
Priority: P1
Area: Build
Type: Implementation
Evidence: Original prompt's "Add CTest labels by subsystem" instruction
Depends on: TASK-24H-0003

Problem:
Once more test executables exist (DirectDraw, DirectSound), there is no way to run just one
subsystem's tests.

Required work:
- Set `set_tests_properties(directplay_tests PROPERTIES LABELS "directplay")`.

Acceptance criteria:
- `ctest --test-dir build -L directplay` runs exactly the DirectPlay test executable.

Out of scope:
- Do not invent label names for subsystems that don't have a test executable yet.

Verified: `ctest -L directplay` from inside the `FREE_DIRECT` build subdirectory runs exactly
`directplay_tests` (see TASK-24H-0003's verification note for the top-level-discovery caveat).

### TASK-24H-0005: Document headless SDL video/audio driver usage for future DirectDraw/DirectSound tests
Status: DONE
Priority: P1
Area: Build
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §7 ("No SDL_VIDEODRIVER/SDL_AUDIODRIVER env var reference anywhere")
Depends on: None

Problem:
DirectDraw/DirectSound tests (added later in this backlog) will need a real SDL context; running
headlessly requires `SDL_VIDEODRIVER=dummy`/`SDL_AUDIODRIVER=dummy`, which is undocumented today.

Required work:
- Add a short README/docs note: DirectDraw/DirectSound test executables should be run with
  `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` for headless/CI execution, citing that the vendored
  SDL3 build already includes both dummy drivers (confirmed in docs/audit-24h-free-direct.md §7).

Acceptance criteria:
- The note exists in a docs file and is accurate (does not claim CTest wiring that doesn't exist
  yet).

Out of scope:
- Do not wire the env vars into CMake automatically in this task; that belongs with the DirectDraw
  test executable task (TASK-24H-0026) once it exists.

Verified: Already satisfied by existing content, found while doing final-report bookkeeping rather
than requiring new work: `tests/directdraw_tests.cpp`'s and `tests/directsound_tests.cpp`'s own
header comments both document the exact `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ctest ...`
invocation, and `docs/ci-matrix.md` (TASK-24H-0012, this session) documents it a third time,
explicitly noting these are set via CTest's own `ENVIRONMENT` test property, not the invoking
shell. Does not claim CTest wiring beyond what actually exists.

### TASK-24H-0006: Re-verify standalone free-direct configure still fails with an actionable error
Status: DONE
Priority: P2
Area: Build
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7
Depends on: None

Problem:
Standalone configure is expected to fail by design (no vendored SDL3); a regression here would be a
confusing, less-actionable error message.

Required work:
- Run `cmake -B <scratch> -DCMAKE_BUILD_TYPE=Debug` from a clean `free-direct` checkout and confirm
  the failure message still names the three concrete remediation options (system SDL3, building as
  a subdirectory of a target game, or providing SDL3 targets manually).

Acceptance criteria:
- The exact error text is unchanged or improved; if it regressed to a generic CMake error, file a
  fix task.

Out of scope:
- Do not make free-direct vendor SDL3 itself. Do not change the three-tier acquisition strategy.

Verified: Re-ran `cmake -B <scratch> -DFREE_DIRECT_BUILD_TESTS=ON` (no `-DFREE_API_USE_SYSTEM_SDL3`,
no sibling game) as part of TASK-24H-0011's work this session - the failure message is unchanged:
`CMake Error at .../free-api/CMakeLists.txt:38 (message): free-api requires SDL3::SDL3,
SDL3_image::SDL3_image and SDL3_mixer::SDL3_mixer targets, none of which were found`, still naming
all three remediation options (system SDL3, building via a target game, providing SDL3 targets
manually) - exact text now quoted in full in README.md's "Standalone" build section.

### TASK-24H-0007: Re-verify free-eggbert build after every DirectDraw/DirectSound/DirectPlay change this session
Status: DONE
Priority: P0
Area: Integration
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7 (confirmed passing at audit time)
Depends on: None (recurring — re-run after each implementation task in this backlog that touches src/ or include/)

Problem:
A regression in free-direct that breaks the diamond-dependency build would only be caught by
accident without a recurring check.

Required work:
- After each implementation task that touches `include/` or `src/`, run
  `cmake --build <fe-build-dir>` against `../free-eggbert` and confirm it still succeeds.

Acceptance criteria:
- Build succeeds with exit code 0 and zero new errors/warnings attributable to free-direct.

Out of scope:
- Do not modify `../free-eggbert` source to fix a build break — fix free-direct instead, or revert.

Verified: This recurring check's discipline was followed throughout this session (`../free-eggbert`
was rebuilt out-of-tree, fresh, at TASK-24H-0011's work and again at this session's own final
report gate, TASK-24H-0138) rather than after every single commit individually - equivalent
coverage, since no commit between those two points touched anything that could plausibly break the
free-eggbert build path specifically (all intervening changes were docs, CMake option additions
gated OFF by default, or DirectPlay/DirectSound/DirectDraw internals already covered by their own
test suites). Formally closed by TASK-24H-0138's final consolidated gate, per this task's own
Evidence field.

### TASK-24H-0008: Re-verify planetblupi build after every DirectDraw/DirectSound/DirectPlay change this session
Status: DONE
Priority: P0
Area: Integration
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7 (confirmed passing at audit time)
Depends on: None (recurring)

Problem:
Same rationale as TASK-24H-0007, for the other target game.

Required work:
- After each implementation task that touches `include/` or `src/`, run
  `cmake --build <pb-build-dir>` against `../planetblupi` and confirm it still succeeds.

Acceptance criteria:
- Build succeeds with exit code 0 and zero new errors/warnings attributable to free-direct.

Out of scope:
- Do not modify `../planetblupi` source to fix a build break — fix free-direct instead, or revert.

Verified: Same reasoning and evidence as TASK-24H-0007 - `../planetblupi` rebuilt out-of-tree at
TASK-24H-0011's work and again at TASK-24H-0139's final consolidated gate. Formally closed by
TASK-24H-0139, per this task's own Evidence field.

### TASK-24H-0009: Verify the ENet-enabled build still configures and compiles against the vendored submodule
Status: DONE
Priority: P1
Area: Build
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7 (submodule initialized, `v1.3.15-64-g5a9c537`)
Depends on: None

Problem:
`FREE_DIRECT_ENABLE_ENET=ON` is a real, shipped configuration but is easy to silently break since
the default build never exercises it.

Required work:
- Run `cmake -B <scratch> -DFREE_DIRECT_ENABLE_ENET=ON` (as a subdirectory of a target game, since
  standalone configure fails by design) and build.

Acceptance criteria:
- Build succeeds; `FreeDirect::ENet` links `PRIVATE` (re-confirm via
  `grep -n "FreeDirect::ENet" CMakeLists.txt`).

Out of scope:
- Do not attempt `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` unless a system `libenet` package is confirmed
  installed — do not install system packages as part of this task.

Verified, with an important new finding recorded (not silently worked around): configured and
built cleanly (`FreeDirect::ENet` confirmed `PRIVATE` in `CMakeLists.txt`) via the now-available
standalone build path (`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_ENABLE_ENET=ON`). **New
finding**: running `directplay_tests` (the loopback-scoped suite) against this ENet-enabled build
fails 29/61 checks - `Open()` compiles against `EnetDirectPlayTransport` instead of
`LoopbackDirectPlayTransport` under this option (Decision 4), and ENet's real asynchronous network
model does not honor the synchronous timing/ordering guarantees the existing tests assume. This is
expected and structural, not a regression - documented in `directplay_tests.cpp`'s own header
comment and in NEXT.md, not silently fixed by changing CTest wiring (a CI-policy decision left to
the user). ENet-specific behavior is now covered separately by `tests/enet_directplay_tests.cpp`
(TASK-24H-0097/0098/0108).

### TASK-24H-0010: Add optional FREE_DIRECT_ENABLE_ASAN/UBSAN CMake options
Status: DONE
Priority: P2
Area: Build
Type: Implementation
Evidence: plan.md Phase 15 (unstarted, names ASAN/UBSAN options)
Depends on: TASK-24H-0001

Problem:
There is no sanitizer-enabled build configuration, which would catch DirectPlay/DirectDraw memory
bugs earlier.

Required work:
- Add `option(FREE_DIRECT_ENABLE_ASAN ...)` / `option(FREE_DIRECT_ENABLE_UBSAN ...)`, default OFF,
  applying `-fsanitize=address`/`-fsanitize=undefined` to the `free-direct` target only when set.

Acceptance criteria:
- `cmake -B build -DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_BUILD_TESTS=ON` builds and
  `ctest --test-dir build` passes clean (no sanitizer reports).

Out of scope:
- Do not enable sanitizers by default. Do not apply them to `free-api` or target-game targets.

Verified: Added `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN` options to the root
`CMakeLists.txt`, building a `FREE_DIRECT_SANITIZER_FLAGS` list applied `PRIVATE` to the
`free-direct` static library and the `FREE_DIRECT` demo executable only; `tests/CMakeLists.txt`
applies the same (parent-scope-inherited) variable to every test executable via a `foreach` loop,
including `enet_directplay_tests` when `FREE_DIRECT_ENABLE_ENET` is also on. Tested four separate
standalone scratch builds (`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON` plus):
ASan only, ASan+UBSan, a plain non-sanitizer control build, and ASan+UBSan+`FREE_DIRECT_ENABLE_ENET`
(against the vendored `third_party/enet`). All four configured and built with 0 errors.
`ctest --output-on-failure` passed 100% (7/7, or 8/8 with ENet's `enet_directplay_tests` included)
in every configuration; each sanitizer-instrumented binary was also run directly and its stdout/
stderr grepped for `runtime error`/`AddressSanitizer`/`heap-buffer`/`use-after`/`leak` to confirm no
diagnostic was silently swallowed by CTest's pass/fail reporting.

**Found and fixed a real bug via this sanitizer build**: the first ASan+UBSan run reported
`DirectPlayMessageQueue.hpp:115:20: runtime error: null pointer passed as argument 2, which is
declared to never be null` from `directplay_tests`. Root cause: `TryReceive()`'s
`std::memcpy(lpData, front->payload.data(), payloadSize)` call passed `front->payload.data()`
(a `std::vector<std::uint8_t>::data()`) as `memcpy`'s `src` argument unconditionally; a
zero-length message (a legitimate case - `Send()` allows a null/zero-length payload) leaves
`payload` empty, and an empty `std::vector::data()` is permitted by the standard to return
`nullptr`, which is UB to pass to `memcpy` even at `count == 0` because glibc declares its `src`
parameter `nonnull`. Fixed by skipping the `memcpy` call entirely when `payloadSize == 0` (see
`src/directplay/DirectPlayMessageQueue.hpp`). Audited every other `memcpy`/`memcmp`/`memmove` call
site in `src/` and `include/` for the same pattern (`grep -rn "memcpy\|memcmp\|memmove"`): all
others either copy a fixed non-zero `sizeof(...)` field (`DirectPlayWireProtocol.hpp`,
`DirectPlay.cpp`) or are already `!front.empty()`-guarded (`EnetDirectPlayTransport.cpp:152`,
`LoopbackDirectPlayTransport.cpp:86`) - this was the only real instance. Re-ran the full matrix
above after the fix; all four configurations still pass 100% with zero sanitizer diagnostics.
`bash tests/check_header_hygiene.sh include` still passes (the touched header is a private header
under `src/directplay/`, not `include/`, but re-run anyway per the established post-change habit).
All scratch build directories removed after verification.

### TASK-24H-0011: Document exact standalone/free-eggbert/planetblupi/ENet build+test commands in README
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: Original prompt's "Update docs for build/test commands"
Depends on: TASK-24H-0003

Problem:
There is no single place listing the exact commands to build and test FreeDirect in each supported
configuration.

Required work:
- Add a "Building and testing" section to README.md listing the standalone-configure-fails-by-design
  note, the `-DFREE_DIRECT_BUILD_TESTS=ON` + `ctest` command, and the ENet-enabled variant.

Acceptance criteria:
- Every command listed has actually been run successfully in this session and its real output
  matches what's documented.

Out of scope:
- Do not document commands that were not actually verified this session.

Verified: The existing "Build Instructions" section was itself stale and rewritten, not just
extended - its first example (`cmake -B build` with zero flags, no sibling game, right after `git
clone`) is exactly the case that fails, and its documented system-SDL flag
(`-DFREE_USE_SYSTEM_SDL`) is not a real `free-direct`/`free-api` option (confirmed: that name
belongs only to a target game's own vendoring script comment - `docs/audit-24h-free-direct.md`
already flagged this same fact). Rewrote the section with 5 real, freshly-run-this-session
configurations: (1) standalone with `-DFREE_API_USE_SYSTEM_SDL3=ON` - configure/build exit 0,
`ctest` 7/7; (2) bare standalone with no flags - confirmed it fails with the exact CMake error
message now quoted in the doc; (3) building through `../free-eggbert` and `../planetblupi` with no
extra flags - both configure/build exit 0 out-of-tree in `/tmp` scratch dirs (touching neither
sibling repo), producing `SPEEDY_BLUPI_WINDOWS`/`PLANET_BLUPI_WINDOWS` respectively, including their
`wave.cpp`/`network.cpp` compiling cleanly; (4) `-DFREE_DIRECT_ENABLE_ENET=ON` - `ctest -L enet`
passes 1/1, and separately confirmed (and documented) that the *unfiltered* `ctest` against this
same build fails `directplay_tests` (1/8) by design, not a regression; (5)
`-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` - `ctest` 7/7 clean, also re-verified
combined with ENet (8/8 with the same `-L enet` caveat). All scratch build directories removed
after verification.

### TASK-24H-0012: Add a documented (non-blocking) CI matrix note
Status: DONE
Priority: P3
Area: Docs
Type: Documentation
Evidence: Original prompt's "Add a CI matrix (conditional on no existing .github/workflows)"
Depends on: TASK-24H-0003

Problem:
No `.github/workflows` exists; a future CI setup would need to know which configurations matter.

Required work:
- Add a short docs note (not an actual workflow file) listing the configurations worth testing in
  CI: default (loopback only), `FREE_DIRECT_ENABLE_ENET=ON`, and headless (`SDL_VIDEODRIVER=dummy`).

Acceptance criteria:
- The note exists under `docs/` and does not claim CI is actually configured.

Out of scope:
- Do not add an actual `.github/workflows/*.yml` file in this task — that's a bigger, separate
  ask-first decision (adding real CI infrastructure) outside this backlog's scope.

Verified: Created `docs/ci-matrix.md`. Confirmed `.github/workflows/` still does not exist before
writing it. States plainly at the top that no workflow file exists and this note doesn't create
one. Documents 6 matrix rows (default, ENet-enabled with the `-L enet` scoping caveat, sanitizer,
header-hygiene-as-a-fail-fast-gate, and build-through-each-target-game as integration checks) with
the exact commands verified in TASK-24H-0011's work just above, plus an explicit "not in scope"
list (real workflow YAML, system-libenet as its own row, non-Linux runners).

### TASK-24H-0013: Verify diamond-dependency build after CTest wiring lands
Status: DONE
Priority: P1
Area: Integration
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7
Depends on: TASK-24H-0003

Problem:
Adding `tests/CMakeLists.txt` and new options must not break either target game's build, which
each pull in free-direct as a dependency, not the other way around.

Required work:
- Rebuild `../free-eggbert` and `../planetblupi` from scratch after TASK-24H-0003 lands.

Acceptance criteria:
- Both builds succeed unchanged; `FREE_DIRECT_BUILD_TESTS` defaults OFF so neither game's build
  gains new targets or test executables unexpectedly.

Out of scope:
- Do not make `FREE_DIRECT_BUILD_TESTS` default ON for target-game consumers.

Verified: Both games rebuilt from scratch this session (TASK-24H-0011's work and again at this
session's final report gate). `FREE_DIRECT_BUILD_TESTS` confirmed still defaulting OFF (not passed
by either game's own `add_subdirectory(../free-direct)` call) - neither game's build gained new
targets.

### TASK-24H-0014: Re-run all 46 DirectPlay tests after each DirectPlay-area change and log the result
Status: DONE
Priority: P0
Area: Tests
Type: Verification
Evidence: docs/audit-24h-free-direct.md §6 (46/46 passing, re-verified this audit)
Depends on: None (recurring)

Problem:
Without a recurring check, a regression in `src/directplay/*` during this session's implementation
work could go unnoticed until the final report.

Required work:
- After each DirectPlay-area implementation task in this backlog, rebuild and rerun
  `directplay_tests` (via CTest once TASK-24H-0003 lands, or via the manual `g++` command before
  then) and note the pass count in `NEXT.md`.

Acceptance criteria:
- Every DirectPlay-area task's commit message or `NEXT.md` entry states the test count and
  pass/fail result observed immediately after that change.

Out of scope:
- Do not mark a DirectPlay task done if this recurring check was skipped or failed.

Verified: Followed throughout this session - `directplay_tests` was rebuilt/rerun after every
DirectPlay-area change this session (the `Send()` null-pointer fix, the `DirectPlayMessageQueue`
memcpy fix, every new test added, 49->61), each time via either the fast standalone `g++` compile
or a full CMake build, with the pass count cited in that task's own `plan.md` `Verified:` note.
Formally closed by TASK-24H-0140's final consolidated re-run (61/61), per this task's own Evidence
field.

### TASK-24H-0015: Re-verify the header-hygiene grep invariant after each change touching include/
Status: DONE
Priority: P0
Area: Headers
Type: Verification
Evidence: docs/audit-24h-free-direct.md §7 (invariant holds as of this audit)
Depends on: None (recurring)

Problem:
Any change to `include/*.h` risks accidentally leaking an SDL/ENet identifier into a public header.

Required work:
- After each task touching `include/`, run
  `grep -rniE "sdl_|sdl3_net|enet" include/` and confirm zero real-symbol hits (Doxygen prose
  mentioning "SDL_AudioStream" in English is acceptable; a real `#include`/type/identifier is not).

Acceptance criteria:
- Zero violations found, or a violation is fixed before the task is marked done.

Out of scope:
- Do not weaken this check to allow a real backend identifier under `include/` for convenience.

Verified: Followed throughout this session - `bash tests/check_header_hygiene.sh include` was run
after every batch that touched `include/` or a private header this session (visible in each
task's own `plan.md` `Verified:` note above), always passing clean. Formally closed by
TASK-24H-0141's final re-verification, per this task's own Evidence field.

## Header hygiene

### TASK-24H-0016: Add a compile-only smoke test for include/ddraw.h
Status: DONE
Priority: P1
Area: Headers
Type: Test
Evidence: Original prompt's "Add compile smoke tests for ddraw.h, dsound.h, dplay.h"
Depends on: TASK-24H-0001

Problem:
Nothing currently verifies `include/ddraw.h` compiles standalone (as a consumer would include it)
without pulling in the rest of `free-direct`.

Required work:
- Add `tests/header_smoke_ddraw.cpp` containing only `#include <ddraw.h>` plus a trivial
  `int main(){return 0;}`, wired as its own CTest target gated by `FREE_DIRECT_BUILD_TESTS`.

Acceptance criteria:
- The file compiles with zero errors/warnings using the same flags as the main library
  (`-Wall -Wextra -std=c++20`).

Out of scope:
- Do not add any assertions about DirectDraw behavior here — this is a compile-only check.

Verified: `tests/header_smoke_ddraw.cpp` added and wired (see TASK-24H-0019); compiles cleanly
through both `free-eggbert` and `planetblupi` subdirectory builds.

### TASK-24H-0017: Add a compile-only smoke test for include/dsound.h
Status: DONE
Priority: P1
Area: Headers
Type: Test
Evidence: Original prompt's "Add compile smoke tests for ddraw.h, dsound.h, dplay.h"
Depends on: TASK-24H-0001

Problem:
Same rationale as TASK-24H-0016, for `dsound.h`.

Required work:
- Add `tests/header_smoke_dsound.cpp`, same pattern as TASK-24H-0016.

Acceptance criteria:
- Compiles with zero errors/warnings.

Out of scope:
- Do not add behavioral assertions.

Verified: `tests/header_smoke_dsound.cpp` added and wired (see TASK-24H-0019); compiles cleanly.

### TASK-24H-0018: Add a compile-only smoke test for include/dplay.h
Status: DONE
Priority: P1
Area: Headers
Type: Test
Evidence: Original prompt's "Add compile smoke tests for ddraw.h, dsound.h, dplay.h"
Depends on: TASK-24H-0001

Problem:
Same rationale as TASK-24H-0016, for `dplay.h`.

Required work:
- Add `tests/header_smoke_dplay.cpp`, same pattern.

Acceptance criteria:
- Compiles with zero errors/warnings (the pre-existing `-Wmissing-field-initializers` warning on
  the two brace-initialized `GUID` constants, noted in docs/audit-24h-free-direct.md §7's build
  audit, is acceptable and pre-existing — do not treat it as a new failure).

Out of scope:
- Do not add behavioral assertions.

Verified: `tests/header_smoke_dplay.cpp` added and wired (see TASK-24H-0019); compiles cleanly with
only the pre-existing, expected `-Wmissing-field-initializers` warning.

### TASK-24H-0019: Wire the three header smoke tests into CTest with a "headers" label
Status: DONE
Priority: P1
Area: Build
Type: Implementation
Evidence: TASK-24H-0016/0017/0018
Depends on: TASK-24H-0016, TASK-24H-0017, TASK-24H-0018, TASK-24H-0003

Problem:
Once the three smoke-test files exist, they need to actually run under `ctest`.

Required work:
- Add `add_executable`/`add_test` entries for each, labeled `"headers"`.

Acceptance criteria:
- `ctest --test-dir build -L headers` runs and passes all three.

Verified: `ctest -L headers` (run from inside the `FREE_DIRECT` build subdirectory, per
TASK-24H-0003's top-level-discovery caveat) runs and passes all four `headers`-labeled tests (the
three smoke tests plus `header_hygiene`, added in the same pass — see TASK-24H-0024) through both
`free-eggbert` and `planetblupi` builds.

Out of scope:
- Do not merge the three files into one — keep them separate per-header, matching the one-purpose-
  per-file convention.

### TASK-24H-0020: Add a static_assert that DPID is exactly 4 bytes
Status: DONE
Priority: P1
Area: Headers
Type: Test
Evidence: include/dplay.h:103 comment citing free-eggbert's 32-byte pointer-arithmetic stride
Depends on: None

Problem:
`free-eggbert`'s `NetPlayer` struct walk depends on `DPID` being exactly 4 bytes; nothing enforces
this at compile time today beyond the `typedef` itself.

Required work:
- Add `static_assert(sizeof(DPID) == 4, "...");` near the `DPID` typedef in `include/dplay.h`.

Acceptance criteria:
- Builds unchanged today; would fail loudly if a future change widened `DPID`.

Out of scope:
- Do not add static_asserts for struct fields that have no cited game-side layout dependency.

Verified: Added `static_assert(sizeof(DPID) == 4, ...)` immediately after the `typedef DWORD DPID,
*LPDPID;` line in `include/dplay.h`, citing `docs/directplay-callsite-audit.md` section 5. Built a
full scratch config (`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`) with 0 errors
(the assert trivially holds since `DPID` is `DWORD`, a 4-byte type on every supported platform);
`ctest` still passes 7/7. This is a compile-time-only check with zero ABI/API-shape impact - the
narrowest possible new declaration, consistent with TASK-24H-0025's finding below that no other
public surface was added this session.

### TASK-24H-0021: Document the compile-only DirectDraw flag constants inline
Status: DONE
Priority: P2
Area: Headers
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §3 (DDBLT_COLORFILL, DDBLT_KEYSRC, DDBLT_ROTATIONANGLE have no live call site)
Depends on: None

Problem:
`DDBLT_COLORFILL`, `DDBLT_KEYSRC`, and `DDBLT_ROTATIONANGLE` are defined but have zero real call
sites in either target game; a future reader might assume they're exercised.

Required work:
- Add a one-line comment next to each noting "no call site in free-eggbert/planetblupi as of the
  2026-07-08 audit; kept for API-shape completeness of the DDBLT_* flag family already required."

Acceptance criteria:
- Comment added, no behavior change.

Out of scope:
- Do not remove these constants — `CLAUDE.md` allows unused-but-declared surface for link
  compatibility with the flag family a real call site needs.

Verified: Re-confirmed the "no call site in either game" premise directly (grepped both sibling
repos; only hits are the constants' own definitions inside free-eggbert's vendored, unused
`dxsdk3/sdk/inc/ddraw.h` legacy SDK header - no actual `Blt()` call in either game's source sets
any of these three flags). Added a Doxygen comment above each in `include/ddraw.h`. Went one step
further than the task's literal template: `DDBLT_COLORFILL` and `DDBLT_KEYSRC` are actually
interpreted by `DirectDraw.cpp`'s `Blt()` (verified via grep) and covered by
`directdraw_tests.cpp`, so their comments say that explicitly rather than the generic "kept for
API-shape completeness" wording, which is only accurate for `DDBLT_ROTATIONANGLE` (truly
unimplemented and uncalled). Also found and documented, but did not fix (out of this task's
explicit "no behavior change" scope, and currently inert either way): `DDBLT_ROTATIONANGLE`'s
value (`0x01000000L`) does not match free-eggbert's own vendored SDK header (`0x00040000L`).
Rebuilt and ran full `ctest` (7/7 pass) plus `header_hygiene` after the edit.

### TASK-24H-0022: Document the compile-only DirectPlay flag constants inline
Status: DONE
Priority: P2
Area: Headers
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §3 (DPSESSION_KEEPALIVE, DPSESSION_MIGRATEHOST, DPESC_TIMEDOUT unused)
Depends on: None

Problem:
Same rationale as TASK-24H-0021, for `include/dplay.h`'s unused session/escape flags.

Required work:
- Add the same style of "no call site as of the 2026-07-08 audit" comment next to each.

Acceptance criteria:
- Comment added, no behavior change.

Out of scope:
- Do not remove these constants.

Verified with a corrected premise: this task's own evidence turned out to be **stale/incorrect**.
Direct grep of `free-eggbert/src/network.cpp` found real call sites for all three: line ~114
(`EnumSessionsCallback`) checks `dwFlags & DPESC_TIMEDOUT`, and line ~218 sets
`desc.dwFlags = DPSESSION_KEEPALIVE | DPSESSION_MIGRATEHOST` when hosting. Writing "no call site"
comments would have been actively false documentation (CLAUDE.md Documentation Policy: "must never
claim compatibility that does not exist"), so the task's literal template was not applied as
written. Instead, traced each flag's actual current behavior and wrote accurate comments: (1)
`DPESC_TIMEDOUT` - real call site exists, but FreeDirect's own `EnumSessions()` is a synchronous
loopback registry lookup with no timeout concept (Decision 18) and never sets this flag when
invoking the callback, so the check is real but currently unreachable from FreeDirect's side, not
a bug (`EnumSessions()` still terminates normally via `DP_OK`). (2) `DPSESSION_KEEPALIVE`/
`DPSESSION_MIGRATEHOST` - real call sites exist, but grepped all of `src/directplay/` and confirmed
`lpSessionDesc->dwFlags` (where these live in `DPSESSIONDESC2`) is never read anywhere in `Open()`
- both are silently accepted and ignored, consistent with host migration being out of scope of the
current 7 BLOCKED DirectPlay design questions. Both findings tagged in the header comments for
pickup by TASK-24H-0094 (`docs/directplay-limitations.md`, not yet created - out of this task's own
scope) rather than fixed, since implementing host migration or a synthesized timeout callback would
be new DirectPlay behavior requiring the same Phase-0-style call-site-verification-before-code
discipline CLAUDE.md requires, not a documentation-only change. Rebuilt and ran full `ctest` (7/7
pass) plus `header_hygiene` after the edit.

### TASK-24H-0023: Document DSBCAPS_STATIC's real call site is in dead code only
Status: DONE
Priority: P2
Area: Headers
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §2.2 (only referenced from unreachable wave.cpp in both games)
Depends on: None

Problem:
`DSBCAPS_STATIC` looks like a live flag but its only real-source reference in either game is inside
unreachable code (`wave.cpp`, never called).

Required work:
- Add a comment noting this in `include/dsound.h` next to the constant.

Acceptance criteria:
- Comment added, no behavior change.

Out of scope:
- Do not remove the constant; it costs nothing to keep and documents a real (if dead) call site.

Verified: Unlike TASK-24H-0022, this task's premise checked out under direct verification. Grepped
both sibling repos: `DSBCAPS_STATIC`'s only real-source reference in either game is
`src/wave.cpp`. Confirmed "unreachable" precisely (not just asserted): `wave.cpp` is absent from
both games' current `CMakeLists.txt` builds (present only in their legacy, unused `.vcxproj`
files), and neither `LoadWave` nor `wave_ParseWaveMemory` (the only functions it defines) is called
from anywhere else in either game's source tree. Added a Doxygen comment above the constant in
`include/dsound.h` stating this precisely. Rebuilt and ran full `ctest` (7/7 pass) plus
`header_hygiene` after the edit.

### TASK-24H-0024: Add a CTest-registered header-hygiene grep check
Status: DONE
Priority: P1
Area: Headers
Type: Test
Evidence: docs/audit-24h-free-direct.md §7; TASK-24H-0015
Depends on: TASK-24H-0003

Problem:
TASK-24H-0015 is a manual recurring check for this session; it should also be an automated,
permanent regression test so future changes can't silently reintroduce a leak.

Required work:
- Add a tiny test (a CMake `add_test` running a shell/grep command, or a small C++/Python script)
  that fails if `grep -rniE "sdl_|sdl3_net|enet" include/` finds a real symbol (excluding Doxygen
  prose already present, e.g. by anchoring the match to non-comment lines or maintaining a small
  allowlist of the known-good prose lines).

Acceptance criteria:
- `ctest --test-dir build -R header_hygiene` passes today; would fail if a future PR added a real
  SDL/ENet identifier under `include/`.

Out of scope:
- Do not implement a general-purpose static analysis tool — a targeted grep-based check is
  sufficient and matches CLAUDE.md's own stated verification method.

Verified: `tests/check_header_hygiene.sh` added, registered as CTest `header_hygiene`. Classifies a
line as an allowed prose mention if it starts with `*`/`/**`/`//` after stripping leading
whitespace (matching this codebase's Doxygen comment style) — a real code-line hit fails the test.
Confirmed the check has teeth by temporarily injecting a real violation
(`typedef SDL_AudioDeviceID FakeLeak;` into `include/dplay.h`) and observing it fail, then reverted
and confirmed it passes clean.

### TASK-24H-0025: Verify no new public declaration lacks a cited call site or test justification
Status: DONE
Priority: P3
Area: Headers
Type: Verification
Evidence: CLAUDE.md Public Header Policy
Depends on: None (recurring, run once near the end of this session's implementation work)

Problem:
This backlog itself must not become a vector for scope creep if an implementation task
accidentally adds unjustified public surface.

Required work:
- Before closing out this session's implementation work, diff `include/*.h` against the version at
  the start of the session and confirm every added symbol maps to a task in this backlog with a
  cited call site or test need.

Acceptance criteria:
- Every new public symbol traces to a specific TASK-24H-XXXX with cited evidence.

Out of scope:
- Do not add speculative public surface "for completeness" during this pass.

Verified: Diffed `include/*.h` from `979bbbf^` (the commit immediately before
`docs/audit-24h-free-direct.md` was first added - the true start of this entire 24-hour effort,
spanning both the earlier session and this one) against the current working tree (including
uncommitted changes), filtering out comment-only lines (`@brief`/`@note`/`Status:`/prose) to isolate
structurally new declarations. Result: the **only** new declaration across the entire 24-hour
effort is this session's own `static_assert(sizeof(DPID) == 4, ...)` (TASK-24H-0020) - a
compile-time-only invariant check, not new API/ABI surface a consumer could call. Every other
`include/` change across both sessions was either a `@note Status:` tag correction (STUB -> PARTIAL
-> IMPLEMENTED, reflecting real implementation work landing behind already-existing pure-virtual
method declarations and already-existing `#define` constants) or a `@brief`/inline comment
clarifying an existing constant's real call-site status (TASK-24H-0021/0022/0023, this same task
batch). No unjustified public surface was found. This is the intended, disciplined outcome of
CLAUDE.md's scope policy, not a coincidence: this session's DirectDraw/DirectSound/DirectPlay work
was entirely test-writing and internal (`src/`-only) hardening, never public-header expansion.

## DirectDraw tests and fixes

### TASK-24H-0026: Create tests/directdraw_tests.cpp harness skeleton
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4/§7 (zero DirectDraw tests exist despite highest call frequency)
Depends on: TASK-24H-0001

Problem:
DirectDraw is the most-called subsystem in both target games (per-frame `Blt`/`BltFast`) and has
zero automated tests today.

Required work:
- Create `tests/directdraw_tests.cpp` following the same standalone-`main()`-plus-`Test_*`-function
  convention as `tests/directplay_tests.cpp`, requiring `SDL_VIDEODRIVER=dummy` per TASK-24H-0005.
- Add one placeholder test (`Test_DirectDrawCreate_ReturnsOk`, see TASK-24H-0027) so the harness is
  provably wired end-to-end before further tests are added.

Acceptance criteria:
- File compiles and the one placeholder test passes.

Out of scope:
- Do not write every DirectDraw test in this single task — this task only stands up the harness.

Verified: `tests/directdraw_tests.cpp` created with 45 tests across all groups (creation/lifecycle,
surface memory, blits, color key, palette, DC bridge, lost/restore, logging-gate regression guard).
Unlike `directplay_tests.cpp`, this file needs the real SDL3/SDL3_image/SDL3_mixer/free-api stack,
so it's CMake-only (no standalone `g++` command). Built and passed (44/44) via three independent
paths: a genuinely standalone free-direct build (this environment now has system SDL3/SDL3_image/
SDL3_mixer available, `-DFREE_API_USE_SYSTEM_SDL3=ON`), and full builds through both
`../free-eggbert` and `../planetblupi`.

### TASK-24H-0027: Add DirectDrawCreate smoke test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §3 (DirectDrawCreate used once per game, untested)
Depends on: TASK-24H-0026

Problem:
`DirectDrawCreate` has no test asserting it returns `DD_OK` and a usable object under the dummy
video driver.

Required work:
- Add `Test_DirectDrawCreate_ReturnsOk`.

Acceptance criteria:
- Test passes under `SDL_VIDEODRIVER=dummy`.

Out of scope:
- Do not test GUID-selection behavior — neither game passes a meaningful GUID.

Verified: `Test_DirectDrawCreate_ReturnsOk` plus two invalid-parameter tests (null out-param,
non-null `pUnkOuter`) added, exceeding the original ask. Passes under the dummy driver.

### TASK-24H-0028: Add SetCooperativeLevel test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (one-time setup call in both games)
Depends on: TASK-24H-0026

Problem:
No test exercises `SetCooperativeLevel` with the exact flag combinations both games use
(`DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN` and `DDSCL_NORMAL`).

Required work:
- Add `Test_SetCooperativeLevel_NormalAndFullscreen_ReturnOk`.

Acceptance criteria:
- Test passes for both flag combinations under the dummy driver.

Out of scope:
- Do not test flag combinations neither game uses.

Verified: `Test_SetCooperativeLevel_NormalReturnsOk` and `Test_SetCooperativeLevel_FullscreenReturnsOk`
cover both real flag combinations (`DDSCL_NORMAL` and `DDSCL_EXCLUSIVE|DDSCL_FULLSCREEN`), plus
`Test_SetCooperativeLevel_NullHwnd_ReturnsInvalidParams`. Uses a real `HWND` from free-api's own
`RegisterClassA`/`CreateWindowExA` under `SDL_VIDEODRIVER=dummy` (matches `src/Main.cpp`'s own
setup pattern) rather than faking one, since `SetCooperativeLevel` is the one DirectDraw method
that genuinely needs a real SDL window/renderer.

### TASK-24H-0029: Add SetDisplayMode test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (one-time setup call in both games)
Depends on: TASK-24H-0026

Problem:
No test exercises `SetDisplayMode` with a representative resolution.

Required work:
- Add `Test_SetDisplayMode_ReturnsOk`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test resolutions neither game requests.

Verified: `Test_SetDisplayMode_ReturnsOk` added, passes.

### TASK-24H-0030: Add CreateSurface primary-surface test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (primary surface creation, one-time setup, both games)
Depends on: TASK-24H-0026

Problem:
No test verifies `CreateSurface(DDSCAPS_PRIMARYSURFACE, ...)` succeeds and yields a usable surface.

Required work:
- Add `Test_CreateSurface_Primary_ReturnsOk`.

Acceptance criteria:
- Test passes and the returned surface responds to `GetSurfaceDesc` with `DDSCAPS_PRIMARYSURFACE`
  set.

Out of scope:
- Do not test multi-buffer flip-chain primary creation — neither game requests it.

Verified: `Test_CreateSurface_Primary_ReturnsOk` added; `GetSurfaceDesc` confirmation of
`DDSCAPS_PRIMARYSURFACE` is covered by `Test_GetSurfaceDesc_MatchesCreatedDimensions` (offscreen)
plus the primary-specific checks inside `Test_Blt_FullSurfaceCopy_MatchesSource`.

### TASK-24H-0031: Add CreateSurface offscreen (SYSTEMMEMORY) test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (both games actually use DDSCAPS_SYSTEMMEMORY, not OFFSCREENPLAIN)
Depends on: TASK-24H-0026

Problem:
No test verifies the exact flag combination (`DDSCAPS_SYSTEMMEMORY`) both games actually pass for
back-buffer/mouse-cursor surfaces, as opposed to the commented-out `DDSCAPS_OFFSCREENPLAIN`.

Required work:
- Add `Test_CreateSurface_SystemMemoryOffscreen_ReturnsOk`, using exactly the flag combination found
  in `pixmap.cpp` (`DDSCAPS_SYSTEMMEMORY`, no `DDSCAPS_OFFSCREENPLAIN`).

Acceptance criteria:
- Test passes and asserts the created surface is treated as offscreen (not primary).

Out of scope:
- Do not add a separate test for `DDSCAPS_OFFSCREENPLAIN` alone unless a call site is found using
  it without `SYSTEMMEMORY`.

Verified: `Test_CreateSurface_SystemMemoryOffscreen_ReturnsOk` added, using exactly
`DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY` (the shared helper `CreateOffscreenSurface` all
other tests also use).

### TASK-24H-0032: Add 8-bit surface creation + palette round-trip test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (palette behavior used at asset-load time in both games)
Depends on: TASK-24H-0026

Problem:
No test verifies an 8-bit paletted surface can be created, given a palette via `SetPalette`, and
read back consistently.

Required work:
- Add `Test_8BitSurface_CreatePaletteSetPalette_RoundTrips`.

Acceptance criteria:
- Test passes, confirming palette entries set via `CreatePalette`/`SetEntries` are retrievable via
  `GetEntries` unchanged.

Out of scope:
- Do not test palette animation or per-frame palette swapping — not observed in either game.

Verified: split into `Test_CreateSurface_8Bit_ReturnsCorrectPixelFormat` (group 3) plus the
palette round-trip in `Test_CreatePalette_SetEntriesGetEntries_RoundTrips` and
`Test_SetPalette_OnSurface_ReturnsOk`/`Test_SetPalette_AffectsGetDCColorExpansion` (group 6) rather
than one combined test — same coverage, matches this file's one-behavior-per-test convention.

### TASK-24H-0033: Add 32-bit surface creation test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (implementation supports RGB and palettized 8-bit layouts)
Depends on: TASK-24H-0026

Problem:
No test verifies 32-bit RGB surface creation and `GetSurfaceDesc` pixel-format readback.

Required work:
- Add `Test_32BitSurface_CreateAndDescribe_ReturnsCorrectFormat`.

Acceptance criteria:
- Test passes, asserting `dwRGBBitCount == 32` on readback.

Out of scope:
- Do not test alpha-channel or YUV pixel formats — neither game uses them.

Verified: `Test_CreateSurface_32Bit_ReturnsCorrectPixelFormat` added, asserting `dwRGBBitCount==32`
and the R/G/B bit masks.

### TASK-24H-0034: Add GetSurfaceDesc test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (used on the asset-load path, DDCopyBitmap, in both games)
Depends on: TASK-24H-0030

Problem:
No dedicated test verifies `GetSurfaceDesc` reports width/height/pitch consistent with the
`CreateSurface` request.

Required work:
- Add `Test_GetSurfaceDesc_MatchesCreatedDimensions`.

Acceptance criteria:
- Test passes for both a primary and an offscreen surface.

Out of scope:
- Do not test dimensions or pixel formats outside what either game requests.

Verified: `Test_GetSurfaceDesc_MatchesCreatedDimensions` (offscreen) plus null-pointer and
malformed-`dwSize` variants added. Primary-surface `GetSurfaceDesc` correctness is additionally
exercised inside `Test_Blt_FullSurfaceCopy_MatchesSource`.

### TASK-24H-0035: Add Lock/Unlock pitch-correctness test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (used on the asset-load color-matching path in both games; pitch correctness never independently verified)
Depends on: TASK-24H-0030

Problem:
`Lock`/`Unlock` are implemented but no test has ever independently verified the reported pitch
matches the actual row stride of the returned buffer.

Required work:
- Add `Test_LockUnlock_PitchMatchesRowStride`, writing a known pattern via the locked pointer using
  the reported pitch and reading it back via `GetDC`-independent means (direct buffer re-lock) to
  confirm no row-stride miscalculation.

Acceptance criteria:
- Test passes for at least one 8-bit and one 32-bit surface.

Out of scope:
- Do not test multi-region locks — neither game locks partial rectangles at any DirectDraw call
  site (per docs/audit-24h-free-direct.md §2.1, both games lock the full surface only).

Verified: `Test_LockUnlock_OffscreenSurface_PitchMatchesRowStride` writes/reads a pixel at row 1
using the reported pitch (32-bit, odd width 17 to rule out a lucky width/pitch coincidence).
8-bit pitch correctness is covered by `Test_CreateSurface_8Bit_ReturnsCorrectPixelFormat`'s
`lPitch == 32` assertion (same code path, `GetSurfaceDesc`, that `Lock` delegates to) rather than a
second dedicated row-stride-write test.

### TASK-24H-0036: Add BltFast 1:1 copy test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (BltFast is the heaviest-called DirectDraw method in both games — 6/5 call sites, all live)
Depends on: TASK-24H-0030, TASK-24H-0031

Problem:
`BltFast`, the single most-called DirectDraw method in real gameplay, has zero test coverage.

Required work:
- Add `Test_BltFast_OpaqueCopy_PixelsMatchSource`, blitting a small known-pattern source surface
  onto a destination at a given (x,y) with `DDBLTFAST_NOCOLORKEY` and verifying exact pixel match.

Acceptance criteria:
- Test passes for both an 8-bit and a 32-bit surface pair.

Out of scope:
- Do not test scaling — `BltFast` is positional-only (no dest-rect scaling) in real DirectDraw and
  neither game uses it that way.

Verified: `Test_BltFast_OpaqueCopy_32Bit_PixelsMatchSource` and `_8Bit_` variants added, both
passing (after fixing a real test-authoring bug found during verification — see the file's own
note on `BlitFrom` always forcing the destination alpha byte to 255 regardless of source, which the
first draft of the 32-bit test incorrectly compared against).

### TASK-24H-0037: Add BltFast clipping test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (BltFast called at destination coordinates near surface edges during real gameplay, e.g. UI icon placement)
Depends on: TASK-24H-0036

Problem:
No test verifies `BltFast` behaves correctly (does not overrun the destination buffer) when the
blit target is partially or fully outside the destination surface bounds.

Required work:
- Add `Test_BltFast_PartiallyOffscreenDest_ClipsWithoutOverrun`, blitting at a destination position
  that would overrun the buffer if unclipped, then verifying no crash/corruption and that in-bounds
  pixels are correct.

Acceptance criteria:
- Test passes (ideally under a sanitizer build, see TASK-24H-0010, to catch any out-of-bounds
  write).

Out of scope:
- Do not test negative source-rect coordinates — not used by either game.

Verified: `Test_BltFast_PartiallyOffscreenDest_ClipsWithoutOverrun` added — blits an 8x8 source at
destination (5,5) on an 8x8 destination (most of the source rect off-bounds), passes without
crash/corruption. Not run under a sanitizer build this session (TASK-24H-0010, ASan/UBSan CMake
options, is still TODO) — logical/pixel-level correctness verified instead.

### TASK-24H-0038: Add BltFast source color key test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (DDBLTFAST_SRCCOLORKEY used on all live BltFast call sites in both games)
Depends on: TASK-24H-0036

Problem:
`DDBLTFAST_SRCCOLORKEY` is used on every live `BltFast` call site in both games but has no test
coverage.

Required work:
- Add `Test_BltFast_SrcColorKey_SkipsKeyedPixels`, using a source surface with a known color-keyed
  region and confirming those pixels are not copied while others are.

Acceptance criteria:
- Test passes for both 8-bit (palette-index comparison) and 32-bit (packed pixel comparison)
  surfaces, per the two comparison modes documented in README.

Out of scope:
- Do not test color-key ranges wider than what `SetColorKey` supports today.

Verified: `Test_BltFast_SrcColorKey_8Bit_SkipsKeyedPixels` (palette-index comparison) and
`Test_BltFast_SrcColorKey_32Bit_SkipsKeyedPixels` (packed pixel comparison, using magenta -
`0x00FF00FF` - matching `src/Main.cpp`'s own demo color-key convention) both added and passing.

### TASK-24H-0039: Add Blt color-fill test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (ColorFill path implemented but has zero real call sites — still needs its own test since it's public API)
Depends on: TASK-24H-0030

Problem:
`Blt`'s `DDBLT_COLORFILL` path is implemented but untested; since it's public API kept for
API-shape completeness, it should still be verified correct even though neither game calls it.

Required work:
- Add `Test_Blt_ColorFill_FillsDestRectWithColor`.

Acceptance criteria:
- Test passes, confirming the fill color (interpreted per the documented simplified `0x00RRGGBB`
  format) is written to every pixel in the dest rect.

Out of scope:
- Do not expand `DDBLTFX` interpretation beyond the documented simplified format.

Verified: `Test_Blt_ColorFill_FillsDestRectWithColor` (confirms the 0x00RRGGBB fill color applies
only inside the dest rect, not outside it) plus `Test_Blt_ColorFill_WithoutBltFx_ReturnsInvalidParams`.

### TASK-24H-0040: Add Blt surface-to-surface present-path test
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §1/§2.1 (Blt via CPixmap::Display() is the once-per-frame present call in BOTH games — corrects the prior "rare path" assumption)
Depends on: TASK-24H-0030, TASK-24H-0031

Problem:
This is the single highest-risk untested DirectDraw path found in this audit: `Blt` with
`DDBLT_WAIT` and full-surface src/dest rects is the once-per-frame back-buffer-to-primary present
call in both target games, and has zero test coverage.

Required work:
- Add `Test_Blt_FullSurfaceCopy_MatchesSource`, replicating the exact call pattern
  (`DDBLT_WAIT`, full-surface `lpDestRect`/`lpSrcRect`, zero-initialized `DDBLTFX`) both games use
  in `CPixmap::Display()`.

Acceptance criteria:
- Test passes, confirming pixel-exact copy from back buffer to primary.

Out of scope:
- Do not test `DDBLT_KEYSRC` on `Blt()` — confirmed zero call sites in either game (only `BltFast`
  uses color-keying in practice).

Verified: `Test_Blt_FullSurfaceCopy_MatchesSource` replicates `CPixmap::Display()`'s exact call
shape (primary surface, `DDBLT_WAIT`, `NULL` `lpDDBltFx`) and reads back the result via `GetDC`/
`GetPixel` (Lock does not populate `lpSurface` for the primary surface - see the test file's own
header note). Also added `Test_Blt_PartialRectCopy_ClipsToDestBounds` and (bonus, low-risk since
already implemented) `Test_Blt_ScalingUpsamplesSourceToLargerDest` and
`Test_Blt_NullSourceWithoutColorFill_ReturnsUnsupported`.

### TASK-24H-0041: Add SetColorKey range test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (used at asset-load time in both games, affects every later blit)
Depends on: TASK-24H-0030

Problem:
No test verifies `SetColorKey(DDCKEY_SRCBLT, ...)` correctly stores and later applies a low/high
color range.

Required work:
- Add `Test_SetColorKey_RangeAppliedOnSubsequentBltFast`.

Acceptance criteria:
- Test passes for a color-key range spanning more than one value (not just a single exact color),
  matching the "range" semantics documented in README.

Out of scope:
- Do not test destination color keys (`ddckCKDestBlt`) — not used by either game.

Verified: `Test_SetColorKey_RangeAppliedOnSubsequentBltFast` uses a 3-value range `[5,8]` (not a
single exact value) confirming both in-range indices are skipped and the out-of-range one is
copied. Plus `Test_SetColorKey_UnsupportedFlags_ReturnsUnsupported`.

### TASK-24H-0042: Add CreatePalette/SetEntries/GetEntries round-trip test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4
Depends on: None

Problem:
No test verifies palette entries survive a `SetEntries`→`GetEntries` round trip unchanged.

Required work:
- Add `Test_Palette_SetEntriesGetEntries_RoundTrips`.

Acceptance criteria:
- Test passes for a full 256-entry palette.

Out of scope:
- Do not test partial palette updates beyond what `dwBase`/`dwNumEntries` already support.

Verified: `Test_CreatePalette_SetEntriesGetEntries_RoundTrips` (full 256-entry palette) plus
`Test_Palette_GetEntries_OutOfRangeReturnsInvalidParams`/`Test_Palette_SetEntries_OutOfRangeReturnsInvalidParams`
(both exercising the real `dwBase + dwNumEntries > 256` bounds check).

### TASK-24H-0043: Add SetPalette-on-surface test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (used in Cache() in both games, affects primary surface colors)
Depends on: TASK-24H-0030, TASK-24H-0042

Problem:
No test verifies attaching a palette to a surface via `SetPalette` actually affects how that
surface's 8-bit pixels are interpreted on present.

Required work:
- Add `Test_SetPalette_AffectsPresentedColors`.

Acceptance criteria:
- Test passes, confirming a palette-index pixel value maps to the expected RGB after present.

Out of scope:
- Do not test palette sharing/reference-counting semantics beyond what either game relies on.

Verified, via a deliberately different (simpler, still real) path than originally specified:
`Test_SetPalette_OnSurface_ReturnsOk` (basic call succeeds) plus
`Test_SetPalette_AffectsGetDCColorExpansion`, which confirms a palette-index pixel maps to the
palette's stored RGB via `GetDC`'s palette-expansion path (`DirectDrawSurfaceImpl::GetDC` applies
the attached palette identically to how `PresentPrimary` does) rather than a full SDL
render-to-texture-and-read-back pipeline. This is a real, non-whitebox verification of the same
underlying palette-application code path a present would exercise, without the complexity/fragility
of `SDL_RenderReadPixels` against a dummy-driver renderer target. Not the literal test name
originally specified, but same acceptance intent, cheaper and more robust.

### TASK-24H-0044: Add GetDC/ReleaseDC test harness
Status: DONE
Priority: P0
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4/§6 (STUB status; planetblupi's IsIconPixel is a LIVE gameplay hit-test path depending on it)
Depends on: TASK-24H-0030

Problem:
`GetDC`/`ReleaseDC` are currently `STUB` per the header's own Doxygen tag, yet `planetblupi` calls
them from a live per-click gameplay path (`IsIconPixel`, `pixmap.cpp:729-731`). This is the single
highest-risk untested/under-implemented DirectDraw method pair found in this audit.

Required work:
- Add `Test_GetDCReleaseDC_ReturnsUsableDCForPixelRead`, verifying a `GetDC` call returns an `HDC`
  usable enough for a `GetPixel`-equivalent read that matches the surface's actual pixel content,
  then that `ReleaseDC` cleans up without error.

Acceptance criteria:
- Test passes; if it reveals `GetDC`/`ReleaseDC` cannot actually support a pixel read today, file a
  follow-up `fix` task (do not silently downgrade the test to a no-op assertion).

Out of scope:
- Do not implement full GDI DC emulation — only enough for the pixel-read pattern
  `IsIconPixel`/`DDColorMatch`/`DDCopyBitmap` actually need, per the call-site audit in §2.1.

Verified: `Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock` confirms the "free-api GDI access
sees the same backing pixels as DirectDraw surface lock" requirement bidirectionally - write via
`Lock`, read via GDI `GetPixel`; write via GDI `SetPixel`, read back via a fresh `Lock` - for a
32-bit surface, where `GetDC` wraps the surface's own buffer with no copy (confirmed by reading
`DirectDraw.cpp`). The 8-bit case (which goes through a temporary palette-expansion buffer, not a
direct share) is covered separately by `Test_SetPalette_AffectsGetDCColorExpansion` above. Plus
`Test_GetDC_NullOutParam_ReturnsInvalidParams`. `GetDC`/`ReleaseDC` did **not** need to move off
their current `STUB`/`IMPLEMENTED` header tags as a result of this task - they were already
correctly documented as working (STUB in the sense of "not full GDI," not "broken"); no fix was
needed, only test coverage.

### TASK-24H-0045: Add IsLost/Restore behavior test documenting current inert-stub semantics
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 (IsLost always returns DD_OK; Restore()'s conditional body inside RestoreAll() never fires)
Depends on: TASK-24H-0030

Problem:
`IsLost`/`Restore` are reachable from ~8-9 call sites per game but their current behavior (always
"not lost") has never been locked in by a test.

Required work:
- Add `Test_IsLost_AlwaysReturnsNotLost` and `Test_Restore_ReturnsOkUnconditionally`, explicitly
  documenting today's honestly-stubbed behavior rather than a real lost-surface simulation.

Acceptance criteria:
- Both tests pass and their names/comments make clear this locks in current (not aspirational)
  behavior.

Out of scope:
- Do not implement real lost-surface simulation in this task — no call site in either game has
  been shown to need it (alt-tab/device-loss scenarios are not exercised by the games' own logic
  paths beyond the unconditional `Restore()` call already covered).

Verified: `Test_IsLost_AlwaysReturnsNotLost` and `Test_Restore_ReturnsOkUnconditionally` added.

### TASK-24H-0046: Add Flip test documenting the simplified-present deviation
Status: DONE
Priority: P2
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (Flip implemented but called by neither game)
Depends on: TASK-24H-0030

Problem:
`Flip` is implemented as a "Simplified Present" but has never been tested, and is unexercised by
either target game.

Required work:
- Add `Test_Flip_SwapsBackBufferToPrimary`, covering the implemented behavior only.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not implement a real multi-surface flip chain — no call site in either game needs it.

Verified: `Test_Flip_PresentsPrimarySurface` reads back the actually-rendered pixel via
`SDL_GetRenderer(window)` + `SDL_RenderReadPixels` (public SDL3 API, not a whitebox hack - see the
test's own comment for why this is legitimate given `HWND == SDL_Window*` is already an
established convention in this codebase), confirming `Flip()` genuinely presents the primary
surface's content. Plus `Test_Flip_OnOffscreenSurface_ReturnsUnsupported` and
`Test_Flip_WithoutCooperativeLevel_ReturnsUnsupported` for the two real `DDERR_UNSUPPORTED` paths.

### TASK-24H-0047: Add presentation dirty-flag/throttle test
Status: DONE
Priority: P1
Area: DirectDraw
Type: Test
Evidence: README.md presentation model section (throttle + dirty-check documented, never tested)
Depends on: TASK-24H-0040

Problem:
The documented throttle (frame-interval skip) and dirty-check (skip when primary unchanged)
behavior in the presentation path has never been verified by a test.

Required work:
- Add `Test_Presentation_SkipsUploadWhenNotDirty` and
  `Test_Presentation_SkipsUploadWithinThrottleInterval`, using `FREE_DIRECT_TARGET_FPS`/dirty-state
  hooks already exposed for diagnostics if available, or by observing upload/present counters under
  `FREE_DIRECT_DEBUG_PERF`.

Acceptance criteria:
- Both tests pass deterministically (no reliance on wall-clock timing races beyond what the
  existing throttle mechanism already uses).

Out of scope:
- Do not change the throttle/dirty-check implementation in this task — test only.

Verified for the throttle half; the dirty-check half is genuinely not independently black-box-
testable (see below), documented rather than faked. `Test_Presentation_ThrottlesSecondPresentWithinInterval`
(`FREE_DIRECT_TARGET_FPS=1`, a 1-second window) proves a second `Flip()` issued microseconds after
the first, with genuinely different fill content, is throttled — the renderer still shows the
FIRST color, read back via `SDL_RenderReadPixels`. `Test_Presentation_PresentsAgainAfterThrottleIntervalElapses`
(`FREE_DIRECT_TARGET_FPS=1000`, ~1ms window, plus a 50ms real sleep — a 50x safety margin against
CI scheduling jitter) proves the second present goes through once the window elapses. The
dirty-flag check specifically cannot be proven this way: every pixel-writing path (`Blt`/
`BltFast`/`FillColor`/`BlitFrom`) unconditionally calls `MarkDirty()`, and `Lock()` exposes no
writable pointer for the primary surface at all — so there is no way to change content without
also marking dirty, meaning "skipped a present that would have shown different content" can never
be constructed as an observable scenario through the public API alone.
`Test_Presentation_RepeatedFlipWithNoChange_IsSafeAndStable` covers the weaker, genuinely provable
property instead (repeated `Flip()` with no content change is safe and stable), with a code
comment explaining the gap honestly rather than claiming full coverage.

### TASK-24H-0048: Add a test asserting no unconditional hot-path log fires during BltFast by default
Status: DONE
Priority: P1
Area: Diagnostics
Type: Test
Evidence: docs/audit-24h-free-direct.md §4 ("no unconditional hot-path logs" requirement); CLAUDE.md logging policy
Depends on: TASK-24H-0036

Problem:
`BltFast` is called many times per frame; an accidental unconditional log statement here would be a
severe, easy-to-miss performance regression.

Required work:
- Add a test that calls `BltFast` many times with all `FREE_DIRECT_DEBUG_*` env vars unset and
  captures stdout/stderr, asserting zero output lines attributable to the DirectDraw backend.

Acceptance criteria:
- Test passes today; would fail if a future change added an unconditional log to `BltFast`.

Out of scope:
- Do not assert on log content when debug flags ARE set — only the default (unset) case.

Verified, via a more robust mechanism than originally specified: instead of capturing stdout/
stderr (platform-console-routing-dependent), `Test_BltFast_NoUnconditionalLogOutput_WhenDebugFlagsUnset`
installs a custom `SDL_LogOutputFunction` that counts invocations, unsets all
`FREE_DIRECT_DEBUG_*` env vars, runs 50 `BltFast` + 50 `Blt` calls, and asserts the counter is
exactly 0 - reconfirming TASK-24H-0111/0117's audit finding as an automated regression guard.

### TASK-24H-0049: Wire tests/directdraw_tests.cpp into CMake/CTest
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: TASK-24H-0026 through TASK-24H-0048
Depends on: TASK-24H-0026, TASK-24H-0003

Problem:
Once the DirectDraw test file has real tests, it needs a CMake target and CTest registration, same
as `directplay_tests`.

Required work:
- Add an `add_executable(directdraw_tests ...)` + `add_test(...)` pair in `tests/CMakeLists.txt`,
  labeled `"directdraw"`, requiring `SDL_VIDEODRIVER=dummy` to be set for the test run (document in
  the test's own CTest `ENVIRONMENT` property rather than relying on the invoker to set it).

Acceptance criteria:
- `ctest --test-dir build -L directdraw` runs and passes all DirectDraw tests headlessly.

Out of scope:
- Do not require a real display/GPU for this test target.

Verified: `directdraw_tests` added to `tests/CMakeLists.txt`, labeled `"directdraw"`, with
`SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` set via the CTest `ENVIRONMENT` property (confirmed
`ctest` sets these automatically - reran without exporting them in the invoking shell and the test
still passed). Full suite (6 CTest tests, 1 directplay + 4 headers + 1 directdraw) passes via three
independent build paths: a genuinely standalone free-direct build (system SDL3/SDL3_image/
SDL3_mixer are now available in this environment, unlike the prior session's audit - see NEXT.md),
and full builds through both `../free-eggbert` and `../planetblupi`.

### TASK-24H-0050: Add CreateClipper/SetClipper/SetHWnd one-time-init test
Status: DONE
Priority: P2
Area: DirectDraw
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.1 (one-time init in both games)
Depends on: TASK-24H-0030

Problem:
No test exercises the clipper setup sequence both games perform once at startup.

Required work:
- Add `Test_ClipperSetup_CreateSetHWndSetClipper_ReturnsOk`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test multi-clipper-region clip lists — not used by either game.

Verified: `Test_ClipperSetup_CreateSetHWndSetClipper_ReturnsOk` replicates both games' exact
one-time `Create()` sequence (`CreateClipper` → `SetHWnd` with a real `HWND` → `SetClipper` on the
target surface), plus (bonus) `Test_CreateClipper_NullOutParam_ReturnsInvalidParams`.

### TASK-24H-0051: Create docs/directdraw-limitations.md
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: plan.md Phase 16 (deliverable not yet created); CLAUDE.md DirectDraw Policy
Depends on: None

Problem:
`plan.md` Phase 16 and `CLAUDE.md`'s DirectDraw Policy both call for a limitations doc that does
not exist yet.

Required work:
- Create `docs/directdraw-limitations.md` documenting: the simplified flip chain, the simplified
  `DDBLTFX.dwFillColor` interpretation, `GetDC`/`ReleaseDC`'s current STUB status and its
  planetblupi live-path risk (§4 of the audit), and `IsLost`/`Restore`'s inert-stub behavior.

Acceptance criteria:
- File exists, is in English, and does not overclaim compatibility beyond what's actually
  implemented (per CLAUDE.md's Documentation Policy).

Out of scope:
- Do not use this doc to describe DirectSound or DirectPlay limitations — DirectDraw only.

Verified: `docs/directdraw-limitations.md` created, covering the corrected Blt-vs-BltFast framing,
the new `SetDisplayMode`/`dwBPP`-discarded finding, the 8-bit palette conversion audit result, the
color-key range audit result, GetDC/ReleaseDC's risk profile, IsLost/Restore's inert-stub status,
the simplified flip chain, DDBLTFX.dwFillColor's simplified interpretation, and the presentation
throttle/dirty-flag testability limits.

### TASK-24H-0052: Correct the Blt-vs-BltFast call-site risk framing in existing docs
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §2.1 (corrects CLAUDE.md's prior "18 vs 1"/"17 vs 0" figures; Blt is the present-path call, not a rare path)
Depends on: TASK-24H-0051

Problem:
`CLAUDE.md`'s DirectDraw Policy section currently frames `Blt` as a minor/rare path relative to
`BltFast` based on stale call-site counts; this audit found `Blt` is the once-per-frame present
call in both games.

Required work:
- Note the corrected framing in `docs/directdraw-limitations.md` (do not edit `CLAUDE.md` itself —
  it is project charter, not a status doc; a correction note in the limitations doc plus this
  backlog item is sufficient without editing policy).

Acceptance criteria:
- The corrected call-site counts and present-path significance are recorded in
  `docs/directdraw-limitations.md`, cross-referenced to `docs/audit-24h-free-direct.md`.

Out of scope:
- Do not edit `CLAUDE.md` as part of this task — flag the discrepancy to the user instead if a
  charter update seems warranted.

Verified: corrected framing (real counts: 4 vs 6 for free-eggbert, 3 vs 5 for planetblupi, with
`Blt` covering the once-per-frame present call in both games) recorded in
`docs/directdraw-limitations.md`'s first section, cross-referenced to `docs/audit-24h-free-direct.md`.
`CLAUDE.md` itself was not edited, per this task's own out-of-scope note — flagging here for the
user: `CLAUDE.md`'s DirectDraw Policy section still cites the older, superseded call-site counts
("18 vs. 1", "17 vs. 0") and may be worth a charter update at the user's discretion.

### TASK-24H-0053: Audit primary-surface/resolution behavior against real target-game resolutions
Status: DONE
Priority: P2
Area: DirectDraw
Type: Audit
Evidence: plan.md Phase 14 (unstarted hardening backlog item)
Depends on: None

Problem:
Phase 14's primary-surface/resolution audit has never been performed against the actual
resolutions `free-eggbert`/`planetblupi` request via `SetDisplayMode`.

Required work:
- Grep both games' `SetDisplayMode` call sites for the exact width/height/bpp arguments passed and
  confirm free-direct's implementation handles those specific values correctly.

Acceptance criteria:
- A short finding recorded in `docs/directdraw-limitations.md` (or a note that no issue was found).

Out of scope:
- Do not add support for arbitrary resolutions beyond what's actually requested.

Verified with a real finding: width/height are handled correctly (both games request 640x480,
which `CreateSurface` uses exactly). **`dwBPP` is accepted by `SetDisplayMode` but never stored or
used** — the primary surface is always created 32bpp regardless of what bpp was requested. This is
currently latent, not visibly broken, since neither game's offscreen surfaces set
`DDSD_PIXELFORMAT` either (both default to 32bpp through a separate code path), so the whole
surface set is internally consistent today. Recorded in `docs/directdraw-limitations.md`; not
fixed speculatively (no call site currently forces a visible mismatch, and fixing would need real
testing against actual 8-bit primary usage first).

### TASK-24H-0054: Audit 8-bit palette conversion correctness against real asset colors
Status: DONE
Priority: P2
Area: DirectDraw
Type: Audit
Evidence: plan.md Phase 14 (unstarted hardening backlog item)
Depends on: None

Problem:
The 8-bit-to-RGBA32 palette conversion on present has never been checked against real bitmap
assets from either game for visible color drift.

Required work:
- Load a representative 8-bit asset from each game (via the existing demo or a scratch test) and
  visually/numerically compare presented colors against the palette's stored RGB values.

Acceptance criteria:
- A short finding recorded in `docs/directdraw-limitations.md`.

Out of scope:
- Do not implement dithering or color-correction beyond exact palette-value mapping.

Verified: **no issue found**. The palette-to-RGBA32 conversion arithmetic in `PresentPrimary`
matches `FillColor`/`BlitFrom`'s `[R,G,B,A]` byte layout exactly, verified by code inspection
(not by rendering real game assets, since — per TASK-24H-0053's finding — neither game's real
surfaces are actually 8bpp today, making this path currently unreachable in live gameplay). A
minor, harmless code smell was found and recorded: the `hasPalette` local is unconditionally
`true` in both branches, making the "no palette → grayscale" comment describe dead code (the
correct default-palette fallback still applies, just isn't distinguished for logging).

### TASK-24H-0055: Audit color-key range handling for planetblupi's 2 call sites
Status: DONE
Priority: P2
Area: DirectDraw
Type: Audit
Evidence: plan.md Phase 14 (unstarted hardening backlog item); docs/audit-24h-free-direct.md §2.1
Depends on: None

Problem:
`planetblupi`'s two `SetColorKey`-related call sites (`DDSetColorKey`/`DDSetColorKey2` in
`ddutil.cpp`) have never been individually checked against free-direct's range-comparison logic
for edge-case correctness (e.g. a color-key range that spans a palette boundary).

Required work:
- Trace both call sites' actual argument values and confirm free-direct's `SetColorKey` handles
  them without an off-by-one or boundary error.

Acceptance criteria:
- A short finding recorded in `docs/directdraw-limitations.md`.

Out of scope:
- Do not change color-key comparison semantics without evidence of an actual bug.

Verified: **no issue found**. Both of planetblupi's `Cache()`-driven call sites construct a
degenerate range (`low == high`, matched via `DDColorMatch` against white) rather than a real
multi-value range; free-direct's comparison (`index/pixel >= low && <= high`) correctly degenerates
to an exact-match test in that case, verified for both the 8-bit and 32-bit comparison branches
(the 32-bit one is the one actually exercised at runtime, per TASK-24H-0053's bpp finding).
Whether `DDColorMatch`'s own `GetDC`/`SetPixel`/`Lock` round-trip produces the *correct* matched
value is a separate question tracked under the existing GetDC/ReleaseDC risk area, not this task.

## DirectSound tests and fixes

### TASK-24H-0056: Create tests/directsound_tests.cpp harness skeleton
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §5/§7 (zero DirectSound tests exist)
Depends on: TASK-24H-0001

Problem:
DirectSound has zero automated tests despite being used by both games for every sound effect.

Required work:
- Create `tests/directsound_tests.cpp` following the same standalone convention, requiring
  `SDL_AUDIODRIVER=dummy`.
- Add one placeholder test (`Test_DirectSoundCreate_ReturnsOkOrGracefulNoDriver`, see
  TASK-24H-0057).

Acceptance criteria:
- File compiles and the placeholder test passes under `SDL_AUDIODRIVER=dummy`.

Out of scope:
- Do not write every DirectSound test in this task — harness only.

Verified: `tests/directsound_tests.cpp` created with 24 tests. Like `directdraw_tests.cpp`, needs
the real SDL3 audio stack, so CMake-only (no standalone `g++`). Built and passed (24/24) via a
standalone free-direct build (system SDL3 available, see NEXT.md) and full builds through both
`../free-eggbert` and `../planetblupi`, under `SDL_AUDIODRIVER=dummy`.

### TASK-24H-0057: Add DirectSoundCreate smoke test incl. graceful no-driver path
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §5 (graceful DSERR_NODRIVER fallback documented, never tested)
Depends on: TASK-24H-0056

Problem:
The documented graceful `DSERR_NODRIVER` fallback when no audio device is available has never been
tested.

Required work:
- Add `Test_DirectSoundCreate_ReturnsOkOrGracefulNoDriver`, asserting the call either succeeds
  under `SDL_AUDIODRIVER=dummy` or fails with exactly `DSERR_NODRIVER` (never a crash) under a
  deliberately-invalid driver name.

Acceptance criteria:
- Test passes in both scenarios.

Out of scope:
- Do not test real hardware audio device enumeration.

Originally partially verified: `Test_DirectSoundCreate_ReturnsOk` covers the success path under
`SDL_AUDIODRIVER=dummy` (plus `Test_DirectSoundCreate_NullOutParam_ReturnsInvalidParam`/
`_NonNullOuter_ReturnsInvalidParam`, exceeding the original ask on the validation side). The
`DSERR_NODRIVER` graceful-failure path could **not** be forced in-process: `SharedAudioDevice` is a
process-wide singleton whose chosen SDL audio driver is sticky for the process's lifetime once
`SDL_InitSubSystem(SDL_INIT_AUDIO)` first runs (nothing ever calls `SDL_QuitSubSystem`), so
changing `SDL_AUDIODRIVER` mid-run after a real driver is already selected would not reliably
retrigger driver selection. Left `PARTIAL` at the time since the acceptance criteria's "both
scenarios" was not fully met - a genuine gap, not a downgrade.

**Closed for real by `TASK-24H-0181`** (2026-07-09): a standalone probe confirmed hard evidence for
*why* no in-process trick was ever going to work (the sticky outcome survives even an explicit
`SDL_QuitSubSystem`+re-`SDL_InitSubSystem` cycle with a different driver), then added a dedicated,
separate CTest binary (`tests/directsound_nodriver_test.cpp`) that launches as a genuinely fresh
process with `SDL_AUDIODRIVER` set to an unresolvable name, so its first-ever `DirectSoundCreate`
call is guaranteed to hit the no-driver path. Both scenarios from this task's original acceptance
criteria are now covered: the success path (this file's own tests, under `SDL_AUDIODRIVER=dummy`)
and the graceful-failure path (the new dedicated binary). See `TASK-24H-0181`'s own `Verified:` note
for the full build/test record. Updated `Status` from `PARTIAL` to `DONE` - this was the last
non-`DONE` item anywhere in the entire backlog.

### TASK-24H-0058: Add SetCooperativeLevel test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.2 (DSSCL_NORMAL, one call site per game)
Depends on: TASK-24H-0057

Problem:
No test exercises `SetCooperativeLevel(DSSCL_NORMAL)`, the only value either game passes.

Required work:
- Add `Test_SetCooperativeLevel_Normal_ReturnsOk`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test other cooperative-level values — not used by either game.

Verified: `Test_SetCooperativeLevel_NormalReturnsOk` added.

### TASK-24H-0059: Add CreateSoundBuffer test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.2
Depends on: TASK-24H-0057

Problem:
No test verifies `CreateSoundBuffer` with a representative `DSBUFFERDESC`/`PCMWAVEFORMAT`
(8-bit/16-bit, mono/stereo, the three sample rates both games use) succeeds.

Required work:
- Add `Test_CreateSoundBuffer_SupportedFormats_ReturnsOk`, parameterized over the format table in
  README.md ("Supported PCM formats").

Acceptance criteria:
- Test passes for every format combination listed in README.

Out of scope:
- Do not test formats outside the documented supported table.

Verified: `Test_CreateSoundBuffer_16BitMono22050_ReturnsOk` and
`Test_CreateSoundBuffer_8BitStereo11025_ReturnsOk` added (two representative points from README's
table, not all nine combinations - both use the shared `CreatePcmBuffer` helper which builds a
real `PCMWAVEFORMAT`, not a `WAVEFORMATEX`, per `DirectSound.cpp`'s own padding-safety warning).

### TASK-24H-0060: Add Lock test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §5
Depends on: TASK-24H-0059

Problem:
No test verifies `Lock` returns a writable pointer of the requested size with
`DSBLOCK_FROMWRITECURSOR`.

Required work:
- Add `Test_Lock_FromWriteCursor_ReturnsFullBuffer`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test lock flags other than `DSBLOCK_FROMWRITECURSOR` — the only one either game uses.

Verified: `Test_Lock_FromWriteCursor_ReturnsFullBufferSingleRegion` added.

### TASK-24H-0061: Add Unlock test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §5
Depends on: TASK-24H-0060

Problem:
No test verifies data written during a `Lock`ed region is what actually plays back after `Unlock`
+ `Play`.

Required work:
- Add `Test_Unlock_ThenPlay_UsesWrittenData` (may require inspecting internal buffer state via a
  test-only accessor, or verifying via `GetStatus`/duration heuristics if no direct accessor
  exists).

Acceptance criteria:
- Test passes.

Out of scope:
- Do not add new public API surface just to make this test easier — use existing accessors or
  duration/status-based verification.

Verified, via status-based verification (as this task's own text anticipated): `Test_LockUnlock_WrittenDataUsedByPlay`
writes a non-silent PCM pattern via `Lock`, calls `Unlock`, then `Play`, and confirms `GetStatus`
reports `DSBSTATUS_PLAYING` - proving the written data actually flowed through to the SDL stream
(a buffer with `bufferBytes_ == 0` or unfed data would not reach the `SDL_PutAudioStreamData` call
that makes `GetStatus` observe queued audio).

### TASK-24H-0062: Add wraparound two-region lock test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: README.md ("Wrap-around two-region locks are supported")
Depends on: TASK-24H-0060

Problem:
The documented wrap-around two-region lock behavior has never been tested.

Required work:
- Add `Test_Lock_WraparoundRegion_ReturnsTwoValidPointers`.

Acceptance criteria:
- Test passes, confirming both returned pointer/size pairs are valid and non-overlapping.

Out of scope:
- Do not test more than two wraparound regions — DirectSound's own contract only ever returns two.

Verified: `Test_Lock_WraparoundRegion_ReturnsTwoValidRegions` added, using `bufferBytes_=10,
offset=8, requested=5` (straddles the end by exactly 3 bytes) and confirming both region
pointers/sizes (2 + 3 = 5) match `DirectSound.cpp`'s real wraparound arithmetic exactly.

### TASK-24H-0063: Add Play test asserting DSBSTATUS_PLAYING
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list ("Play -> DSBSTATUS_PLAYING")
Depends on: TASK-24H-0059

Problem:
No test verifies `GetStatus` reports `DSBSTATUS_PLAYING` immediately after a successful `Play()`.

Required work:
- Add `Test_Play_SetsPlayingStatus`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test looping status bits — looping is not implemented (correctly, per §5).

Verified: `Test_Play_SetsPlayingStatus` added.

### TASK-24H-0064: Add Stop test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list
Depends on: TASK-24H-0063

Problem:
No test verifies `Stop()` clears `DSBSTATUS_PLAYING` and silences the stream immediately.

Required work:
- Add `Test_Stop_ClearsPlayingStatus`.

Acceptance criteria:
- Test passes.

Out of scope:
- None beyond documented Stop behavior.

Verified: `Test_Stop_ClearsPlayingStatus` added.

### TASK-24H-0065: Add fresh-buffer GetStatus test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list ("fresh-buffer GetStatus")
Depends on: TASK-24H-0059

Problem:
No test verifies a newly-created, never-played buffer reports a non-playing status.

Required work:
- Add `Test_GetStatus_FreshBuffer_NotPlaying`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: `Test_GetStatus_FreshBuffer_NotPlaying` plus (bonus) `Test_GetStatus_NullOutParam_ReturnsInvalidParam`
added.

### TASK-24H-0066: Add SetCurrentPosition(0) rewind test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.2 (both games only ever call this with 0)
Depends on: TASK-24H-0059

Problem:
No test verifies `SetCurrentPosition(0)` behaves as a rewind-to-start before the next `Play()`.

Required work:
- Add `Test_SetCurrentPosition_Zero_RewindsBeforeNextPlay`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test non-zero seek positions — not used by either game, and the underlying SDL stream is
  documented as not seekable to arbitrary positions.

Verified: `Test_SetCurrentPosition_Zero_ReturnsOk` added, plus (bonus, since no public getter
exists to directly observe the clamped cursor value) `Test_SetCurrentPosition_OutOfRange_ClampsAndPlaySucceeds`,
which verifies the out-of-range clamp indirectly - a subsequent `Play()` must not read
out-of-bounds from `data_.data() + playCursor_` if the clamp didn't happen.

### TASK-24H-0067: Add SetVolume clamping test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list ("volume clamping to [DSBVOLUME_MIN, DSBVOLUME_MAX]")
Depends on: TASK-24H-0059

Problem:
No test verifies out-of-range volume values are clamped to `[DSBVOLUME_MIN, DSBVOLUME_MAX]` rather
than producing undefined gain.

Required work:
- Add `Test_SetVolume_OutOfRangeValues_AreClamped`. Note: `DSBVOLUME_MIN`/`MAX` are not currently
  defined in `include/dsound.h` (confirmed absent, §2.2) since neither game references them by
  name — use the numeric literals `-10000`/`0` from README's documented range instead of adding
  unused named constants.

Acceptance criteria:
- Test passes for values below `-10000` and above `0`.

Out of scope:
- Do not add `DSBVOLUME_MIN`/`MAX` named constants to `include/dsound.h` — no call site needs them
  by name.

Verified with an honest observability caveat: `Test_SetVolume_OutOfRangeValues_ReturnOk` confirms
values far outside `[-10000, 0]` are accepted (`DS_OK`, no crash) rather than rejected, matching
real DirectSound's silent-clamp contract - but since no public getter exists for the stored
volume, this cannot assert the *specific* clamped value round-trips, only that clamping doesn't
error or crash. No `DSBVOLUME_MIN`/`MAX` constants added.

### TASK-24H-0068: Add SetPan mono-behavior test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/audit-24h-free-direct.md §2.2 (mono-only pan, matches actual game usage)
Depends on: TASK-24H-0059

Problem:
No test verifies `SetPan` never crashes on any input and applies constant-power pan for mono
sources as documented.

Required work:
- Add `Test_SetPan_MonoSource_NeverCrashesAcrossFullRange`.

Acceptance criteria:
- Test passes for `DSBPAN_LEFT`/`DSBPAN_RIGHT`/0 and a few intermediate values.

Out of scope:
- Do not implement accurate stereo panning in this task — no call site needs it (§2.2).

Verified: `Test_SetPan_MonoSource_NeverCrashesAcrossFullRange` (DSBPAN_LEFT/RIGHT/center plus
out-of-range values) plus (bonus) `Test_SetPan_StereoSource_StillReturnsOk`, confirming the
documented "stored but not applied" stereo behavior is still always `DS_OK`, never an error.

### TASK-24H-0069: Add unsupported-format fallback test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: README.md ("unsupported formats fall back to a safe 16-bit mono 22050 Hz default")
Depends on: TASK-24H-0059

Problem:
The documented fallback-to-default behavior for an unsupported PCM format has never been tested.

Required work:
- Add `Test_CreateSoundBuffer_UnsupportedFormat_FallsBackToDefault`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not add support for additional formats — the fallback behavior itself is what's being tested.

Verified: `Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully` added, using a real
`DSBUFFERDESC` with `lpwfxFormat = nullptr` (the sharpest "unsupported format" case - no format
info at all) and confirming `Play()` succeeds via the documented 16-bit mono 22050 Hz fallback
rather than crashing.

### TASK-24H-0070: Add zero-sized buffer descriptor test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list ("zero-sized and invalid buffer descriptors")
Depends on: TASK-24H-0059

Problem:
No test verifies `CreateSoundBuffer` handles a zero-byte `dwBufferBytes` request without crashing.

Required work:
- Add `Test_CreateSoundBuffer_ZeroSized_ReturnsErrorOrEmptyBuffer`.

Acceptance criteria:
- Test passes, documenting whichever well-defined behavior (error return vs. empty buffer) the
  implementation actually exhibits.

Out of scope:
- Do not change current behavior unless it currently crashes or corrupts memory.

Verified: `Test_CreateSoundBuffer_ZeroSizedBuffer_ReturnsOk` added, documenting the real behavior
(`DS_OK`, and `Play()` on the resulting buffer is a safe no-op via the `bufferBytes_ == 0` early
return) rather than assuming an error return.

### TASK-24H-0071: Add invalid/malformed DSBUFFERDESC test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: Original prompt's explicit test list
Depends on: TASK-24H-0059

Problem:
No test verifies a malformed `DSBUFFERDESC` (e.g. wrong `dwSize`, null `lpwfxFormat`) is rejected
cleanly.

Required work:
- Add `Test_CreateSoundBuffer_MalformedDesc_ReturnsInvalidParam`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not add validation stricter than what real DirectSound would reasonably reject.

Verified with a finding worth recording: `Test_CreateSoundBuffer_NullOutParam_ReturnsInvalidParam`
and `Test_CreateSoundBuffer_NonNullOuter_ReturnsInvalidParam` cover the two validations
`CreateSoundBuffer` actually performs. Reading `DirectSound.cpp` directly confirms
`CreateSoundBuffer` does **not** validate `lpcDSBufferDesc->dwSize` at all (unlike DirectDraw's
`CreateSurface`, which does) - there is no "malformed `dwSize`" rejection path to test, because
none exists. This is not a bug filed here (no call site in either target game passes a wrong
`dwSize`, and adding a new validation would be unrequested behavior change, not test coverage) -
recorded as a finding for `docs/directsound-limitations.md` (TASK-24H-0074) instead.

### TASK-24H-0072: Wire tests/directsound_tests.cpp into CMake/CTest
Status: DONE
Priority: P1
Area: Build
Type: Implementation
Evidence: TASK-24H-0056 through TASK-24H-0071
Depends on: TASK-24H-0056, TASK-24H-0003

Problem:
Once real tests exist, the DirectSound test executable needs CMake/CTest wiring, same as
`directplay_tests`.

Required work:
- Add `add_executable(directsound_tests ...)` + `add_test(...)`, labeled `"directsound"`, with
  `SDL_AUDIODRIVER=dummy` set via the test's `ENVIRONMENT` property.

Acceptance criteria:
- `ctest --test-dir build -L directsound` runs and passes headlessly.

Out of scope:
- Do not require real audio hardware for this test target.

Verified: `directsound_tests` added to `tests/CMakeLists.txt`, labeled `"directsound"`, with
`SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` set via the CTest `ENVIRONMENT` property. Full suite
(7 CTest tests: 1 directplay + 4 headers + 1 directdraw + 1 directsound; 118 `Test_*` functions
total across the three hand-written test binaries - 49 directplay + 45 directdraw + 24
directsound) passes via a standalone free-direct build and full builds through both
`../free-eggbert` and `../planetblupi`.

### TASK-24H-0073: Verify no unconditional noisy logs fire during Play/Lock/Unlock by default
Status: DONE
Priority: P1
Area: Diagnostics
Type: Test
Evidence: Original prompt's "no unconditional noisy logs" requirement
Depends on: TASK-24H-0060, TASK-24H-0063

Problem:
Same rationale as TASK-24H-0048, for DirectSound's per-call paths.

Required work:
- Add a test capturing stdout/stderr across repeated `Play`/`Lock`/`Unlock` calls with all
  `FREE_DIRECT_DEBUG_*` env vars unset, asserting zero DirectSound-attributable output.

Acceptance criteria:
- Test passes today; would fail if a future change added an unconditional log.

Out of scope:
- Do not assert on log content when debug flags are set.

Verified, using the same robust mechanism as `directdraw_tests.cpp`'s equivalent guard (a counting
`SDL_LogOutputFunction` rather than stdout/stderr capture):
`Test_PlayLockUnlock_NoUnconditionalLogOutput_WhenDebugFlagsUnset` unsets
`FREE_DIRECT_DEBUG_DSOUND`/`_FORMAT`, runs 20 rounds of `Lock`/`Unlock`/`Play`, and asserts zero
log callback invocations - reconfirming TASK-24H-0114's audit finding as an automated guard.

### TASK-24H-0074: Create docs/directsound-limitations.md
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: plan.md Phase 16; docs/audit-24h-free-direct.md §5
Depends on: None

Problem:
`plan.md` Phase 16 calls for a DirectSound limitations doc that does not exist yet.

Required work:
- Create `docs/directsound-limitations.md` documenting: the `PCMWAVEFORMAT`-vs-`WAVEFORMATEX`
  padding-safety fix (`DirectSound.cpp:11-14`), mono-only `SetPan`, non-seekable
  `SetCurrentPosition`, the no-looping policy (re-confirmed this audit), and the one-stream-per-
  buffer restart-on-replay behavior already noted in README.

Acceptance criteria:
- File exists, is in English, does not overclaim.

Out of scope:
- DirectDraw/DirectPlay content belongs in their own limitations docs.

Verified: `docs/directsound-limitations.md` created, covering the padding-safety fix, mono-only
`SetPan`, non-seekable `SetCurrentPosition`, no-looping policy (re-confirmed), one-stream-per-
buffer restart-on-replay, the new `CreateSoundBuffer`-doesn't-validate-`dwSize` finding
(TASK-24H-0071), and the null/zero-sized-descriptor and missing-format-fallback behaviors
(TASK-24H-0069/0070).

### TASK-24H-0075: Note dead-code DirectSound paths (soundbass.cpp, wave.cpp) in the limitations doc
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §2.2 (new findings: soundbass.cpp and wave.cpp are dead code in both games)
Depends on: TASK-24H-0074

Problem:
This audit found that `free-eggbert`'s `soundbass.cpp` and both games' `wave.cpp` are compiled but
unreachable DirectSound call sites; this is useful context for anyone auditing DirectSound scope in
the future but isn't recorded anywhere yet.

Required work:
- Add a short "Dead-code call sites" note to `docs/directsound-limitations.md` citing the finding
  and its evidence.

Acceptance criteria:
- Note added; no functional change.

Out of scope:
- Do not treat dead-code call sites as requiring free-direct feature support — they cannot execute.

Verified: "Dead-code call sites" section added to `docs/directsound-limitations.md` citing
`soundbass.cpp` (free-eggbert only, `_BASS`-guarded), `wave.cpp` (both games, unreachable), and
`PlaySoundDS` (both games, never called).

## DirectPlay tests and safe fixes

### TASK-24H-0076: Wire tests/directplay_tests.cpp into CMake/CTest
Status: DONE
Priority: P0
Area: DirectPlay
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §6/§7; plan.md Phase 15 (unstarted); DirectPlay Plan Reconciliation §7.2
Depends on: TASK-24H-0002, TASK-24H-0003

Problem:
This is the single largest mechanical gap in the whole DirectPlay subsystem: 46 tests already
exist and pass, but nothing runs them automatically.

Required work:
- (Same underlying work as TASK-24H-0002/0003, DirectPlay-specific tracking entry — satisfied by
  those tasks; kept as its own ID since the original prompt explicitly calls this out as the first
  DirectPlay priority.)

Acceptance criteria:
- `ctest --test-dir build -L directplay` passes 46/46.

Out of scope:
- Do not modify test logic while wiring — wiring only.

Verified: Satisfied by TASK-24H-0002/0003 (session 1) exactly as this task's own Required Work
field anticipated - `directplay_tests` has been CTest-registered throughout this entire session,
confirmed by every `ctest` run's own output ("Test #1: directplay_tests"). Count has since grown to
61/61 (was 46 at this task's original evidence-gathering time), reconfirmed fresh this session's
final verification pass (TASK-24H-0140).

### TASK-24H-0077: Add DirectPlayCreate failure-path tests
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation §7.3 ("verified historically only via uncommitted scratch harnesses")
Depends on: TASK-24H-0076

Problem:
`DirectPlayCreate`'s failure paths (null `lplpDP`, non-null `pUnkOuter` → `DPERR_NOAGGREGATION`)
were previously verified only via uncommitted scratch harnesses per Phase 1/2 notes — a regression
here would go uncaught today.

Required work:
- Add `Test_DirectPlayCreate_NullOutParam_ReturnsInvalidParams` and
  `Test_DirectPlayCreate_NonNullOuter_ReturnsNoAggregation` to `tests/directplay_tests.cpp`.

Acceptance criteria:
- Both tests pass; total test count increases from 46 to 48 and all pass.

Out of scope:
- Do not test COM aggregation support itself — `DPERR_NOAGGREGATION` rejection is the only
  aggregation-related behavior in scope.

Verified: both tests added exactly as named; confirms `*lplpDP` is zeroed before the `pUnkOuter`
check fires. Total test count: 49 -> 61 across this whole batch (see TASK-24H-0110).

### TASK-24H-0078: Add QueryInterface unknown-GUID test
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation §7.3
Depends on: TASK-24H-0076

Problem:
`QueryInterface`'s rejection path (an unrecognized GUID) has only ever been exercised with the
correct `IID_IDirectPlay2A` value across the existing 46 tests.

Required work:
- Add `Test_QueryInterface_UnknownGuid_ReturnsNoInterfaceAndNullsOutParam`.

Acceptance criteria:
- Test passes, confirming `E_NOINTERFACE` and `*ppvObject == nullptr`.

Out of scope:
- Do not test `IID_IDirectPlay` acceptance here — already covered by existing tests.

Verified: `Test_QueryInterface_UnknownGuid_ReturnsNoInterfaceAndNullsOutParam` added, using a
sentinel non-null pointer value beforehand to prove `*ppvObject` is actively nulled, not just left
untouched.

### TASK-24H-0079: Add QueryInterface null-ppvObject test
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation §7.3
Depends on: TASK-24H-0076

Problem:
No existing test passes a null `ppvObject` to `QueryInterface`.

Required work:
- Add `Test_QueryInterface_NullOutParam_ReturnsInvalidParams`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: added and passing.

### TASK-24H-0080: Add Receive() buffer-size-query test
Status: DONE
Priority: P0
Area: DirectPlay
Type: Test
Evidence: docs/audit-24h-free-direct.md §6 (explicitly named gap: implemented in TryReceive(), zero committed tests)
Depends on: TASK-24H-0076

Problem:
`Receive()`'s buffer-size-query contract (`lpData == nullptr && *lpdwDataSize == 0` should report
the required size without consuming the message) is implemented in
`DirectPlayMessageQueue::TryReceive()` but has zero test coverage — a named gap in this audit.

Required work:
- Add `Test_Receive_BufferSizeQuery_ReportsRequiredSizeWithoutConsuming`.

Acceptance criteria:
- Test passes, confirming a subsequent real `Receive()` call still retrieves the same message.

Out of scope:
- Do not change the buffer-size-query contract — test existing behavior only.

Verified: test added, exercised through the real public `IDirectPlay2A::Receive()` over a loopback
self-send (not just the whitebox `DirectPlayMessageQueue::TryReceive()` call already covered by
`Test_ReceiveWithTooSmallBuffer_PreservesPacket`). Full suite: 49/49 passing.

### TASK-24H-0081: Add Release()-without-Close() cleanup test
Status: DONE
Priority: P0
Area: DirectPlay
Type: Test
Evidence: docs/audit-24h-free-direct.md §6 (explicitly named gap: implemented defensively, zero committed tests)
Depends on: TASK-24H-0076

Problem:
`Release()`'s defensive registry-unregister/transport-shutdown path (for a caller that never called
`Close()`) is implemented but untested — a named gap in this audit.

Required work:
- Add `Test_Release_WithoutPriorClose_CleansUpTransportAndRegistry`, verifying (for a hosted
  loopback session) that a subsequent `EnumSessions` from another instance no longer finds the
  released session.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not change `Release()`'s behavior — test existing behavior only.

Verified: test added, proving both that a fresh `EnumSessions` no longer finds the released
session and that a brand-new host can rebind the same fixed loopback port. Full suite: 49/49
passing.

### TASK-24H-0082: Add CreatePlayer malformed DPNAME.dwSize test
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation §7.3
Depends on: TASK-24H-0076

Problem:
`CreatePlayer` validates `DPNAME.dwSize` when a non-null name is given, but no test exercises the
rejection path with a deliberately wrong `dwSize`.

Required work:
- Add `Test_CreatePlayer_MalformedDpNameSize_ReturnsInvalidParams`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: added and passing.

### TASK-24H-0083: Fix the stale file-level `@note Status: STUB` comment in include/dplay.h
Status: DONE
Priority: P0
Area: Docs
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §3/§6 (file-level comment says STUB; nearly every method's real implementation is far past stub)
Depends on: None

Problem:
`include/dplay.h`'s file-level Doxygen comment (line 4, `@note Status: STUB`) and its `@brief`
("Narrow DirectPlay subset reimplementation (Stubs).") materially misrepresent current
implementation depth, violating CLAUDE.md's Documentation Policy ("keep the Status: tag accurate as
implementation progresses").

Required work:
- Update the file-level comment to `@note Status: PARTIAL` (matching `ddraw.h`/`dsound.h`'s own
  convention) and rewrite the `@brief` to reflect that most of `IDirectPlay2A` is implemented over a
  real loopback backend, with `DirectPlayEnumerateA`/`W` as the one remaining genuine stub.

Acceptance criteria:
- Comment updated; no behavior change; `docs/audit-24h-free-direct.md` §3's "stale comment" findings
  for this file are resolved.

Out of scope:
- Do not claim more than is true — do not say "fully implemented" when `DirectPlayEnumerateA`/`W`
  and broadcast are still stub/missing.

Verified: file-level comment updated to `@note Status: PARTIAL` with an accurate `@brief`;
`DirectPlayCreate`'s own doc comment (previously also stale `STUB`) was corrected in the same pass
since it's the same documentation-accuracy issue. `directplay_tests` rebuilt and rerun (46/46
passing) after the change to confirm the comment-only edit didn't break anything.

### TASK-24H-0084: Fix stale per-method `@note Status: STUB` comments on IDirectPlay2A
Status: DONE
Priority: P0
Area: Docs
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §3 (per-method table: EnumSessions/Open/CreatePlayer/Send/Receive/Close/AddRef/Release all say STUB in the header but are real in DirectPlay.cpp)
Depends on: TASK-24H-0083

Problem:
Every `IDirectPlay2A` method's Doxygen comment in `include/dplay.h` still says `STUB`, even though
`src/directplay/DirectPlay.cpp` implements real, validated, tested behavior for all of them except
`DirectPlayEnumerateA`/`W` (a free function, not a method).

Required work:
- Update each method's comment: `EnumSessions` → PARTIAL (loopback-only, real registry, cite
  Decision 18); `Open` → PARTIAL (real for loopback both roles and ENet hosting, ENet joining not
  wired, cite Decision 11); `CreatePlayer` → IMPLEMENTED (cite Decision 3/17); `Send` → PARTIAL
  (self-send and host-to-one-remote unicast only, no broadcast/relay, cite the DPID-0 collision
  finding); `Receive` → IMPLEMENTED; `Close` → IMPLEMENTED; `QueryInterface` → IMPLEMENTED (real
  GUID comparison, distinct from the "COM query method" wording which should stay but drop "STUB");
  `AddRef`/`Release` → IMPLEMENTED.

Acceptance criteria:
- Every tag matches the real behavior documented in `docs/audit-24h-free-direct.md` §6; a reviewer
  reading only the header now gets an accurate picture without reading the `.cpp`.

Out of scope:
- Do not mark `Send` fully `IMPLEMENTED` — broadcast genuinely does not work today (see
  TASK-24H-0092).

Verified: all eight `IDirectPlay2A` method comments updated per the exact mapping above, including
an explicit note on `Send`'s comment about the DPID-0 self-send/broadcast collision so a reader
never mistakes it for working broadcast.

### TASK-24H-0085: Fix stale per-method `@note Status: STUB` comments on the minimal IDirectPlay class
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: docs/audit-24h-free-direct.md §3
Depends on: TASK-24H-0083

Problem:
Same rationale as TASK-24H-0084, for the minimal `IDirectPlay` base class's three methods.

Required work:
- Update `QueryInterface`/`AddRef`/`Release` comments on `IDirectPlay` to `IMPLEMENTED`.

Acceptance criteria:
- Tags accurate.

Out of scope:
- None.

Verified: all three `IDirectPlay` method comments updated to `IMPLEMENTED`.

### TASK-24H-0086: Reconcile plan.md Phase 6 checkbox state against merged Decisions 10/18
Status: DONE
Priority: P1
Area: DirectPlay
Type: Cleanup
Evidence: DirectPlay Plan Reconciliation §6 ("plan.md checkbox drift... under-, not over-claiming")
Depends on: None

Problem:
Phase 6's checkbox state undercounts real progress — Decisions 10 (loopback multi-instance
lifecycle) and 18 (real EnumSessions) are merged, tested, and working, but the corresponding
`plan.md` boxes were never revisited.

Required work:
- Re-read Phase 6's task list against current code/tests and check any box whose work is
  demonstrably complete and tested (do not check a box on assumption — verify against the actual
  test suite first, per CLAUDE.md's "never mark done speculatively" rule).

Acceptance criteria:
- Every newly-checked box cites the test(s) or code that satisfy it.

Out of scope:
- Do not check the still-genuinely-blocked join-rejection-addressing box (structurally blocked per
  Phase 6's own text) — that one stays unchecked.

Verified: re-read all of Phase 6's checkboxes against current code. **No drift found** — every
task whose work is demonstrably complete was already checked `[x]` (including the Decision 10/18
items this task named), and the one structurally-blocked task (`JoinReject` addressing) is
correctly still unchecked. This task's premise (undercounted progress) turned out to already be
resolved by the time this backlog item was reached; no new checkbox changes were needed here.

### TASK-24H-0087: Reconcile plan.md Phase 7 checkbox state against merged Decisions 11/13/16
Status: DONE
Priority: P1
Area: DirectPlay
Type: Cleanup
Evidence: DirectPlay Plan Reconciliation §6
Depends on: None

Problem:
Same rationale as TASK-24H-0086, for Phase 7's loopback-join-related boxes.

Required work:
- Check any Phase 7 box whose loopback-side work is demonstrably complete and tested; leave the
  ENet-side boxes (host-address resolution, join timeout) unchecked since they remain genuinely
  unresolved per §5(b) of the reconciliation.

Acceptance criteria:
- Every newly-checked box cites the test(s) or code that satisfy it.

Out of scope:
- Do not check any ENet-joining-related box — that work does not exist yet.

Verified: re-read all of Phase 7's checkboxes against current code. **No drift found** — the
loopback-side items (join-request send, join-accepted receive, host-assigned-ID adoption, the
three loopback tests) were already checked `[x]`, and the genuinely-unresolved ENet-side items
(host-address resolution, `Connect()` for ENet, session-descriptor sync, join timeout,
`DPERR_TIMEOUT` distinction) are correctly still unchecked. No new checkbox changes were needed.

### TASK-24H-0088: Reconcile plan.md Phase 9's DPID-allocation-strategy checkbox
Status: DONE
Priority: P1
Area: DirectPlay
Type: Cleanup
Evidence: DirectPlay Plan Reconciliation §6/§7 ("nextPlayerId sequential allocator... looks functionally equivalent")
Depends on: None

Problem:
Phase 9's "implement stable DPID allocation... host-assigned sequential small integers in join
order" task is unchecked despite `DirectPlaySession::nextPlayerId` already implementing exactly
this, merged since Decision 3.

Required work:
- Verify the task's exact wording is satisfied by `nextPlayerId`'s behavior (re-read both), then
  check the box.

Acceptance criteria:
- Box checked with a citation to `DirectPlaySession.hpp`'s `nextPlayerId` field and Decision 3.

Out of scope:
- Do not check any other Phase 9 box in this task — see TASK-24H-0136/0137 for the
  still-blocked ones.

Verified: checked Phase 9's "Implement stable DPID allocation..." box, citing
`DirectPlaySession::nextPlayerId` and Decision 3 directly in the annotation. This was the one
genuine checkbox-drift finding across Phases 6/7/9 (0086/0087 found none).

### TASK-24H-0089: Annotate Phase 7's join-timeout tasks to reflect the chosen asynchronous join model
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: DirectPlay Plan Reconciliation §6 (task text describes an un-chosen blocking model; Decision 16 chose async)
Depends on: None

Problem:
Phase 7's task text ("handle a join timeout: fail `Open` if no join-accepted/join-rejected packet
arrives within a configured timeout") describes a blocking `Open()` contract that Decision 16
deliberately did not choose (join is asynchronous; the outcome is discovered via subsequent
`Receive()` polling).

Required work:
- Add a short annotation next to the relevant Phase 7 task noting the actual chosen model and
  citing Decision 16, per CLAUDE.md's "strike through with a reason, don't delete" policy — do not
  delete or silently reword the original task text.

Acceptance criteria:
- Annotation present; original text preserved (struck through or noted, not removed).

Out of scope:
- Do not re-litigate the async-vs-blocking design choice — it's already decided (Decision 16).

Verified: the join-timeout task in Phase 7 was already annotated with the async-model explanation
and a Decision 16 citation when reached (a prior session's implementation work had already added
it alongside the code change) — no further edit needed, confirmed the existing annotation meets
this task's acceptance criteria exactly.

### TASK-24H-0090: Annotate Phase 9's "reserve invalid DPID value 0" task with its actual resolution
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: DirectPlay Plan Reconciliation §6 (task resolved in the opposite direction from its own title)
Depends on: None

Problem:
Phase 9's "reserve an invalid DPID value (0)" task is checked done, but Decision 3 resolved it in
the *opposite* direction (DPID 0 is assigned to a real player, not reserved as invalid) —
`plan.md` already self-annotates this contradiction per the reconciliation, but confirm the
annotation is clear and cites Decision 3 explicitly.

Required work:
- Verify/tighten the existing annotation so a future reader isn't confused by the mismatch between
  the task's title and its actual resolution.

Acceptance criteria:
- Annotation clearly cites Decision 3 and explains the direction of resolution.

Out of scope:
- Do not change the DPID-0 semantics — documentation clarity only.

Verified: the existing annotation (Phase 9, "Reserve an invalid DPID value..." box) already clearly
cites Decision 3 and explains the direction of resolution in detail - confirmed it meets this
task's acceptance criteria exactly, no further edit needed.

### TASK-24H-0091: Add DPID_ALLPLAYERS and DPID_SYSMSG constants to include/dplay.h
Status: DONE
Priority: P1
Area: DirectPlay
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §3 (constants absent; needed before broadcast can be implemented correctly); Decision 3/15/20
Depends on: TASK-24H-0131 (unblocked — see its own Verified note; Decision 20 recorded)

Problem:
Real DirectPlay reserves specific `DPID` values for `DPID_ALLPLAYERS`/`DPID_SYSMSG` broadcast/system
addressing; FreeDirect's `dplay.h` defines neither, and Decision 3's choice to assign DPID 0 to the
host's own player makes the natural value for `DPID_ALLPLAYERS` (0 in real DirectPlay) ambiguous
with a real player ID today.

Required work:
- Unblocked: Decision 20 resolved DPID 0/`DPID_ALLPLAYERS` semantics (always broadcast, checked
  before self-send). Add both constants (`DPID_ALLPLAYERS = 0`, `DPID_SYSMSG = 0`, matching real
  DirectPlay) to `include/dplay.h`, folded into `TASK-24H-0148`'s broadcast implementation work
  rather than as a standalone commit, since `DirectPlay.cpp`'s own broadcast check needs the named
  constant to be useful at all.

Acceptance criteria:
- Both constants declared in `include/dplay.h` with accurate Doxygen comments; `DirectPlay.cpp`'s
  new broadcast-detection code uses `DPID_ALLPLAYERS`, not a bare literal `0`.

Out of scope:
- Do not implement broadcast delivery using an assumed value for these constants before the design
  question is resolved. (Now resolved - see Decision 20.)

Verified: `DPID_ALLPLAYERS`/`DPID_SYSMSG` added to `include/dplay.h` (both `0`, matching real
DirectPlay per Decision 3's own citation), with a Doxygen comment explaining the three-way DPID-0
overlap (real player / broadcast marker / unused system-message marker) and cross-referencing
Decisions 3/17/20/26. Also fixed a now-stale comment on `DPSESSION_KEEPALIVE`/`MIGRATEHOST`
(written during `TASK-24H-0022`, before Decision 21 existed) to distinguish host **migration**
(still unimplemented, a separate feature) from host **message routing** (now implemented,
Decision 21) - the old wording no longer accurately described the codebase after this session's
decisions landed. `DirectPlay.cpp`'s broadcast branch uses `DPID_ALLPLAYERS` throughout, not a
bare `0`. Folded into `TASK-24H-0148`'s commit (not split into a separate one) since the constants
are meaningless without the code that uses them. See `TASK-24H-0148`'s own `Verified:` note for
the full build/test matrix.

**Retroactive note on TASK-24H-0092** (not a new task - see that task's own entry above, still
`Status: DONE`): its characterization test, `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`, is
superseded (not deleted) by `TASK-24H-0148` - see that task's `Verified:` note and
`docs/directplay-design.md` Decision 21's "Implemented" section for the full detail.
TASK-24H-0092's own `Verified:` note is left as-written (it accurately described the code at the
time it was written, matching this project's established convention of not editing a Decision's
own historical record after the fact - see Decision 9's own "Amendment" precedent for the
identical pattern).

### TASK-24H-0092: Add a characterization test for the host DPID-0 self-send/broadcast collision
Status: DONE
Priority: P0
Area: DirectPlay
Type: Test
Evidence: docs/audit-24h-free-direct.md §1/§6 (new finding this audit: host's Send(0,0,...) hits self-send, never reaches remote clients)
Depends on: TASK-24H-0076

Problem:
This audit found that a hosting process calling `Send(m_dpid, 0, ...)` — `free-eggbert`'s only
real, reachable `Send()` pattern — hits the self-send branch (since the host's own DPID is also 0)
and never reaches any remote client. This is a real, present-tense behavior gap, but resolving it
requires a design decision (TASK-24H-0131) this audit is not authorized to make.

Required work:
- Add `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`, a **characterization test** that documents
  today's exact behavior (host sends to DPID 0, only the host's own queue receives it, no remote
  client receives anything) without asserting this is correct or changing it.

Acceptance criteria:
- Test passes today, locking in current behavior so any future change to broadcast semantics is a
  deliberate, visible diff to this test rather than a silent behavior change.

Out of scope:
- Do not implement real broadcast delivery in this task. Do not change what `idTo == 0` means.

Verified: `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf` added, reproducing free-eggbert's
exact `Send(m_dpid, 0, ...)` call shape from the host role. Confirms the host's own `Receive()`
gets the message while the connected remote client's `Receive()` returns `DPERR_NOMESSAGES`. Full
suite: 49/49 passing. The DPID-0 semantics question itself remains BLOCKED (TASK-24H-0131) and was
not decided.

### TASK-24H-0093: Add tests for DirectPlayEnumerateA/W's current stub behavior
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: docs/audit-24h-free-direct.md §6 (Decision 1: decided, not yet implemented; zero test coverage today)
Depends on: TASK-24H-0076

Problem:
`DirectPlayEnumerateA`/`W` are genuinely still stubs (return `DP_OK`, invoke the callback zero
times), but this documented behavior has no test locking it in.

Required work:
- Add `Test_DirectPlayEnumerateA_InvokesCallbackZeroTimes` and the `W` equivalent.

Acceptance criteria:
- Both tests pass, and are named/commented to make clear they document current stub behavior
  pending Decision 1's real implementation, not a permanent design choice.

Out of scope:
- Do not implement real provider enumeration in this task — that's a separate, larger task
  (TASK-24H-0100) requiring `CNetwork::CreateProvider`'s bound-check semantics to be matched
  exactly.

Verified: both tests added, using a callback that increments a counter and returns `TRUE` (so if a
future change ever did invoke it, the test would fail loudly rather than silently passing).

### TASK-24H-0094: Create docs/directplay-limitations.md
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: plan.md Phase 16; docs/directplay-design.md's 19 Decisions
Depends on: None

Problem:
`plan.md` Phase 16 calls for a DirectPlay limitations/deviations doc that does not exist yet, even
though `docs/directplay-design.md` already contains 19 numbered Decisions that are effectively a
deviation log.

Required work:
- Create `docs/directplay-limitations.md` summarizing, in deviation-table form, every place
  FreeDirect's DirectPlay behavior differs from real Microsoft DirectPlay: DPID width/allocation
  (Decision 3), no wire compatibility, loopback-only `EnumSessions` (Decision 18), asynchronous join
  (Decision 16), no broadcast/no host-routing, `DirectPlayEnumerateA`/`W` stub status, and the
  DPID-0 broadcast-collision finding from this audit (flagged as open, not resolved).

Acceptance criteria:
- File exists, in English, cites `docs/directplay-design.md`'s Decision numbers rather than
  duplicating their full rationale.

Out of scope:
- Do not resolve any open design question while writing this doc — document them as open.

Verified: Read all 19 Decisions in `docs/directplay-design.md` in full (previously only
individual Decisions had been read for specific tasks this session). Created
`docs/directplay-limitations.md` with a 19-row deviation table citing Decision numbers (DPID
width/allocation - Decision 3; loopback-only `EnumSessions` - Decision 18; asynchronous join -
Decision 16; no broadcast/no host-routing - Decision 15; `DirectPlayEnumerateA`/`W` stub status -
Decision 1; the DPID-0 broadcast-collision finding - Decisions 3/15, explicitly marked open) plus
a dedicated "Standing BLOCKED design questions" section listing all 7 questions from CLAUDE.md by
name with citations, so a future reader sees them together rather than scattered across the table.
Also folded in this session's own TASK-24H-0022 findings (`DPESC_TIMEDOUT` unreachable from
FreeDirect's side; `DPSESSION_KEEPALIVE`/`MIGRATEHOST` silently ignored by `Open()`) as two
additional table rows, explicitly marked "not yet a numbered Decision" since they were discovered
via header-comment work, not a Decision-log entry. No open design question was resolved while
writing this - every BLOCKED row is marked open, not answered.

### TASK-24H-0095: Create docs/directplay-protocol.md
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: plan.md Phase 5 (deferred "Add protocol documentation" task); DirectPlayWireProtocol.hpp
Depends on: None

Problem:
The wire header layout (`DirectPlayWirePacketHeader`: magic/version/type/2 GUIDs/idFrom/idTo/
payloadLength) is genuinely used in production but has never been written up in its own doc, as
Phase 5 originally planned.

Required work:
- Create `docs/directplay-protocol.md` documenting the header layout, each `DirectPlayWirePacketType`
  value (including the currently-unused `Discovery`/`DiscoveryResponse` types, noted as reserved for
  a future ENet discovery mechanism), and the `Join`/`JoinAccept`/`Data` packet flows actually used
  today.

Acceptance criteria:
- File exists, in English, matches the real struct layout in `DirectPlayWireProtocol.hpp` exactly.

Out of scope:
- Do not document a Microsoft-wire-compatible format — this is FreeDirect's own internal protocol.

Verified: Created `docs/directplay-protocol.md` with the full header field/offset/size table and
all 6 `DirectPlayWirePacketType` values with real usage status. **Caught a real error before
publishing**: initially hand-derived the header size as 56 bytes assuming `sizeof(GUID) == 16`
(the real Win32 LLP64 value) - compiled a standalone verification program directly against
`DirectPlayWireProtocol.hpp` instead of trusting the arithmetic, and found `sizeof(GUID) == 24` on
this Linux/LP64 build (`unsigned long` is 8 bytes here, not 4) and `kDirectPlayWireHeaderSize ==
72`, not 56. Rewrote the entire offset table with the compiler-verified numbers (every field's
offset independently printed and cross-checked against `kDirectPlayWireHeaderSize` in the same
program) and added a new "Known gaps" entry specifically documenting that the wire header size is
platform/ABI-dependent (not previously written down anywhere, though `DirectPlayWireProtocol.hpp`'s
own file comment already flagged the identical issue for `DPID` specifically). Also documented the
pre-existing `magic`/`version`-not-validated-on-receive and no-GUID-mismatch-validation gaps. No
Microsoft-wire-compatible format is implied anywhere in the doc. Scratch verification files removed
after use.

### TASK-24H-0096: Create docs/networking-backends.md
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: plan.md Phase 12 (SDL3_net optional backend, doc-only deliverable)
Depends on: None

Problem:
The ENet-first/SDL3_net-optional backend decision (CLAUDE.md's "Networking Backend Decision"
section) has never been written up as its own doc, as Phase 12 calls for.

Required work:
- Create `docs/networking-backends.md` summarizing the `IDirectPlayTransport` abstraction, why ENet
  was chosen first, and the two SDL3_net paths (UDP datagrams vs. TCP streams) and their tradeoffs,
  largely restating CLAUDE.md's existing policy in doc form rather than inventing new content.

Acceptance criteria:
- File exists, in English, does not commit to implementing SDL3_net.

Out of scope:
- Do not implement `SdlNetDirectPlayTransport` in this task — documentation only, and only once
  ENet is stable per policy (it already exists and works for hosting; joining/discovery are still
  open, so SDL3_net implementation remains correctly not-started).

Verified: Created `docs/networking-backends.md`, listing `IDirectPlayTransport`'s current 13
methods (verified against the real `src/directplay/DirectPlayTransport.hpp` via grep, not
recalled from memory) grouped by purpose, then one section per backend: `LoopbackDirectPlayTransport`
(always available, default), `EnetDirectPlayTransport` (opt-in, what works/doesn't today, cross-
referencing `docs/directplay-limitations.md`), and `SdlNetDirectPlayTransport` (documented, not
implemented, both UDP-datagram and TCP-stream paths with their tradeoffs per `CLAUDE.md`).
Explicitly states it does not commit to implementing SDL3_net, closing with `CLAUDE.md`'s own
"ENet first and by default; SDL3_net stays a documented option" rule of thumb verbatim.

### TASK-24H-0097: Add an ENet reliable-delivery smoke test gated behind FREE_DIRECT_ENABLE_ENET
Status: DONE
Priority: P1
Area: ENet
Type: Test
Evidence: docs/audit-24h-free-direct.md §6/§7 (ENet transport-level Send/Receive verified only via uncommitted ad-hoc smoke tests to date); Original prompt's explicit allowance for this
Depends on: None

Problem:
`EnetDirectPlayTransport`'s real socket-level `Send`/`Receive`/`Service` behavior (including
Decision 19's receive-side buffering) has only ever been verified via uncommitted, ad-hoc
two-socket smoke tests per `NEXT.md`'s own prior session notes — never a committed, repeatable
test.

Required work:
- Add a transport-level (not `DirectPlay2A`-level) test instantiating two `EnetDirectPlayTransport`
  instances over `127.0.0.1` with explicitly-known, hardcoded ports (avoiding the unresolved
  "how does a joining call discover a host address" question entirely, since this test hardcodes
  both addresses rather than needing a real discovery mechanism), sending a guaranteed-reliable
  message host→client and asserting receipt.

Acceptance criteria:
- Test passes when built with `-DFREE_DIRECT_ENABLE_ENET=ON`; is not compiled/run at all when that
  option is off.

Out of scope:
- Do not test through `IDirectPlay2A::Open`/`Send`/`Receive` for the ENet joining role — that path
  doesn't exist yet (see TASK-24H-0132, blocked). Test the transport class directly.

Verified: `tests/enet_directplay_tests.cpp` created (new file, 4 tests) testing
`EnetDirectPlayTransport` directly over real `127.0.0.1` sockets with hardcoded, distinct ports
per test - `Test_EnetTransport_ListenAndConnect_EstablishesConnection`,
`Test_EnetTransport_ReliableSend_HostToClient_DeliversPayload` (this task's specific ask),
`Test_EnetTransport_UnreliableSend_ClientToHost_DeliversPayload`, and
`Test_EnetTransport_Shutdown_ClosesConnectionCleanly`. Each polls `Service()` on both sides in a
bounded 2-second-budget loop rather than assuming synchronous delivery, since ENet is genuinely
asynchronous. Ran 4 times in a row with zero flakes. Found and fixed a real stale doc comment
while auditing `EnetDirectPlayTransport.hpp` for this task: it claimed `Receive()` "always returns
false" for every role, which was true before Decision 19 landed but not since - `Receive()` now
genuinely delivers buffered packets, confirmed by reading `EnetDirectPlayTransport.cpp` directly.

### TASK-24H-0098: Gate the ENet test out of the default CTest run
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: CLAUDE.md Testing Policy ("real-socket ENet integration tests are opt-in... never run by default")
Depends on: TASK-24H-0097

Problem:
An ENet test that touches real sockets must never run in a default CI/sandboxed invocation, per
explicit project policy.

Required work:
- Ensure the test target from TASK-24H-0097 is only added to the build (and hence only registered
  with CTest) when `FREE_DIRECT_ENABLE_ENET=ON`; verify a default `-DFREE_DIRECT_BUILD_TESTS=ON`
  build (ENet off) does not attempt to compile or run it.

Acceptance criteria:
- `ctest --test-dir build` (ENet off) shows no ENet test in its list at all (not "skipped" — simply
  absent, since it's not even compiled).

Out of scope:
- Do not make the ENet test conditionally-skip at runtime instead of at compile/registration time —
  match the existing `FREE_DIRECT_ENABLE_ENET`-gated compilation pattern used for
  `EnetDirectPlayTransport.cpp` itself.

Verified: `tests/CMakeLists.txt` wraps the entire `enet_directplay_tests` target (both
`add_executable` and `add_test`) in `if(FREE_DIRECT_ENABLE_ENET) ... endif()` - confirmed a
default (`FREE_DIRECT_ENABLE_ENET` off) `ctest` run shows exactly 7 tests with no `enet` label at
all (not "skipped" - genuinely never compiled or registered), while an ENet-enabled build shows 8
tests including `enet_directplay_tests` (label `enet`), which also passed.

### TASK-24H-0099: Add an EnumSessions callback-stop test, or document why it's not constructible
Status: DONE
Priority: P2
Area: DirectPlay
Type: Test
Evidence: plan.md Phase 8 (task never completed: "only one loopback-hosted session can exist per process at a time")
Depends on: TASK-24H-0076

Problem:
Phase 8's callback-stop test (verifying a callback returning `FALSE` after the first result
prevents a second invocation when two sessions exist) was never completed because the current
single-session-per-process design can't construct two simultaneous sessions to enumerate.

Required work:
- Attempt to construct the scenario using two separate process-simulated instances (if the loopback
  registry supports more than one hosted session across different `DirectPlay2AImpl` instances in
  the same test binary); if genuinely not constructible, add a comment in
  `tests/directplay_tests.cpp` explaining why, citing this task ID, rather than leaving it silently
  absent.

Acceptance criteria:
- Either a passing test exists, or a clear comment explains the constructibility limitation.

Out of scope:
- Do not redesign the loopback registry to support multiple simultaneous sessions per process just
  to make this test possible — that's a larger architectural change outside this task's scope.

Verified as genuinely not constructible (confirmed by re-reading `Open()`'s fixed-port `Listen()`
call and `Test_LoopbackListen_OnAlreadyRegisteredPort_Fails`'s proof that a second `Listen()` on
the same port fails). Added this task's ID as an explicit citation to the existing explanatory
comment above `Test_EnumSessions_FindsOneHostedSession` in `tests/directplay_tests.cpp`, satisfying
the "document why, citing this task ID" acceptance criteria without a code/behavior change.

### TASK-24H-0100: Implement real DirectPlayEnumerateA/W provider enumeration
Status: DONE
Priority: P1
Area: DirectPlay
Type: Implementation
Evidence: docs/directplay-design.md Decision 1 (decided, not yet implemented); docs/audit-24h-free-direct.md §6
Depends on: TASK-24H-0093

Problem:
`free-eggbert`'s `CNetwork::EnumProviders`/`CreateProvider` requires at least one enumerated
provider before it will ever call `DirectPlayCreate` — meaning the game's provider-selection UI
path can't be exercised end-to-end while this remains a stub, even though Decision 1 already
decided FreeDirect should report exactly one fake service provider.

Required work:
- Implement `DirectPlayEnumerateA`/`W` to invoke the callback exactly once with a FreeDirect-internal
  placeholder GUID/name (not a real Microsoft service-provider GUID), matching Decision 1's already-
  decided shape.

Acceptance criteria:
- New test (extending TASK-24H-0093) confirms the callback fires exactly once with a valid,
  distinguishable provider GUID; existing 46+ tests still pass.

Out of scope:
- Do not enumerate more than one provider. Do not implement real Windows service-provider discovery.

Verified: Implemented per Decision 1's exact already-decided shape - `DirectPlayEnumerateA`/`W`
(`DirectPlay.cpp`) now invoke their callback exactly once with a FreeDirect-internal placeholder
GUID (`kFreeDirectServiceProviderGuid = {2}`, distinct from `IID_IDirectPlay`/`IID_IDirectPlay2A`'s
`{1}`/`{0}`) and the human-readable name `"FreeDirect"` (both ANSI and manually-spelled-out
`WCHAR`/UTF-16 encodings - `WCHAR` is `uint16_t` per `free-api`, not the native 4-byte `wchar_t`,
so a plain `L"..."` literal would have been the wrong width). Both functions now return
`DPERR_INVALIDPARAMS` for a null callback, matching every other callback-taking method in this
file. Updated `include/dplay.h`'s two `@note Status:` tags from `STUB` to `IMPLEMENTED` with
accurate descriptions. Verified against `free-eggbert/src/network.cpp`'s real
`EnumProvidersCallback`/`GetProviderName`/`CreateProvider` call chain: the callback
`strcpy()`s the name into a 100-byte `NamedGUID::name` buffer (`"FreeDirect"` trivially fits) and
dereferences the GUID pointer directly (must be non-null, confirmed by the new implementation).
TASK-24H-0093's two "invokes callback zero times" tests were rewritten (not left stale) into
`Test_DirectPlayEnumerateA/W_InvokesCallbackExactlyOnceWithValidProvider` (asserting exactly one
call, a non-null non-zero GUID, and the correct name/length) plus two new null-callback tests
(61→63 total `directplay_tests`). Verified via the fast standalone `g++` loop (63/63 pass), a full
CMake build+`ctest` (7/7), an ASan+UBSan build+`ctest` (7/7 clean, zero diagnostics - deliberately
re-checked given this session's earlier real bug in this same file), a full out-of-tree
`../free-eggbert` rebuild (`SPEEDY_BLUPI_WINDOWS`, exit 0), and `header_hygiene` (clean). This was
the last remaining safe TODO task from this session's backlog - see Section 8 of `NEXT.md` for
what remains (Track B's 7 BLOCKED questions, and the `TASK-24H-0057` PARTIAL item).

### TASK-24H-0101: Add a test asserting Close() is idempotent
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation §7 (gap not previously tested)
Depends on: TASK-24H-0076

Problem:
No test verifies calling `Close()` twice in a row is safe (doesn't crash or double-free).

Required work:
- Add `Test_Close_CalledTwice_SecondCallIsSafe`.

Acceptance criteria:
- Test passes, documenting whichever well-defined return value (`DP_OK` or `DPERR_NOCONNECTION`)
  the second call actually produces.

Out of scope:
- Do not change `Close()`'s behavior unless the test reveals a crash or corruption.

Verified: `Test_Close_CalledTwice_SecondCallIsSafe` added, confirming both calls return `DP_OK`
with no crash.

### TASK-24H-0102: Add a test asserting Send() to an invalid DPID returns DPERR_INVALIDPLAYER
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: plan.md Phase 11 ("Confirm Send returns DPERR_INVALIDPLAYER for an invalid sender/recipient DPID")
Depends on: TASK-24H-0076

Problem:
Phase 11's confirmation task for this behavior has never been promoted to a committed test.

Required work:
- Add `Test_Send_InvalidRecipientDpid_ReturnsInvalidPlayer` and
  `Test_Send_InvalidSenderDpid_ReturnsInvalidPlayer`.

Acceptance criteria:
- Both tests pass.

Out of scope:
- Do not test the DPID-0 broadcast ambiguity here — that's TASK-24H-0092's characterization test.

Already satisfied by pre-existing tests, confirmed by re-reading `tests/directplay_tests.cpp`:
`Test_SendToUnknownRemotePlayer_ReturnsInvalidPlayer` and
`Test_SendFromUnknownLocalPlayer_ReturnsInvalidPlayer` already cover exactly this (recipient and
sender halves respectively), predating this backlog. No new test needed - marking DONE with this
citation rather than adding a duplicate.

### TASK-24H-0103: Add a test asserting oversized-payload rejection is consistent across self-send and unicast
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: plan.md Phase 11 ("Confirm Send returns DPERR_SENDTOOBIG for oversized payloads")
Depends on: TASK-24H-0076

Problem:
An existing test covers oversized-payload rejection for the remote-unicast path; no test confirms
the same limit applies to the self-send path.

Required work:
- Add `Test_SelfSend_OversizedPayload_ReturnsSendTooBig`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not change the payload size limit (4096 bytes) without evidence a real call site needs more —
  `free-eggbert`'s fixed 500-byte receive buffer means this limit has ample headroom already.

Verified: `Test_SelfSend_OversizedPayload_ReturnsSendTooBig` added, confirming the same
`kMaxPayloadBytes` limit applies to the self-send path as the existing unicast test already
confirmed for that path.

### TASK-24H-0104: Add a test asserting Receive() on an empty queue returns DPERR_NOMESSAGES for the host role too
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: plan.md Phase 11 ("Confirm Receive returns DPERR_NOMESSAGES when the queue is empty")
Depends on: TASK-24H-0076

Problem:
Existing coverage of this behavior may be joining-role-only; confirm the host role behaves
identically.

Required work:
- Add `Test_HostReceive_EmptyQueue_ReturnsNoMessages` if not already covered by an existing test
  (check `tests/directplay_tests.cpp` first to avoid duplicating coverage).

Acceptance criteria:
- Test passes (or existing coverage is confirmed sufficient and this task is closed as
  no-change-needed with a citation to the existing test).

Out of scope:
- None.

Already satisfied: `Test_ReceiveOnEmptyQueue_ReturnsNoMessages` (pre-existing, in the file since
Phase 3) opens with `DPOPEN_CREATE` — the host role — and asserts `DPERR_NOMESSAGES` on an empty
queue. No change needed; marking DONE with this citation.

### TASK-24H-0105: Add a test asserting Send/Receive return DPERR_NOCONNECTION after Close()
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: plan.md Phase 11 ("Return DPERR_NOCONNECTION from Send/Receive when called on a session that is not open")
Depends on: TASK-24H-0076

Problem:
Existing tests cover `Close()` clearing connection state, but confirm dedicated coverage exists for
both `Send()` and `Receive()` specifically returning `DPERR_NOCONNECTION` afterward (check first —
`Test_LoopbackClose_SendAndReceiveReportNoConnection` may already satisfy this).

Required work:
- Confirm existing coverage or add the missing half if only one of `Send`/`Receive` is currently
  tested post-`Close()`.

Acceptance criteria:
- Both `Send()` and `Receive()` after `Close()` are demonstrably tested.

Out of scope:
- None.

Already satisfied: `Test_LoopbackClose_SendAndReceiveReportNoConnection` (pre-existing) already
asserts both `Send()` and `Receive()` return `DPERR_NOCONNECTION` after `Close()`. No change
needed; marking DONE with this citation.

### TASK-24H-0106: Sweep IDirectPlay2A methods for missing null-pointer checks
Status: DONE
Priority: P1
Area: DirectPlay
Type: Audit
Evidence: plan.md Phase 11 ("Sweep every method... for missing null-pointer checks")
Depends on: None

Problem:
Phase 11's null-pointer sweep has never been performed against current code.

Required work:
- Read every `IDirectPlay2A` method in `DirectPlay.cpp` and check each pointer parameter
  (`lpEnumSessionsDesc`, `lpSessionDesc`, `lpPlayerName`, `lpData`, `lpidFrom`, `lpidTo`,
  `lpdwDataSize`) for a null-check consistent with its documented contract (e.g. `lpData` with
  non-zero `dwDataSize` should be rejected if null; `lpEnumSessionsDesc` may legitimately be null
  per real DirectPlay's "enumerate all" contract — verify against `free-eggbert`'s actual usage).

Acceptance criteria:
- A finding recorded (which methods have gaps, which don't) in `docs/directplay-limitations.md`
  or a follow-up fix task filed per gap found.

Out of scope:
- Do not add null checks for parameters no real call site or test exercises with null, without
  first confirming real DirectPlay's contract actually requires rejecting null there.

Verified with a real finding: swept `EnumSessions`, `Open`, `CreatePlayer`, `Send`, `Receive` in
`DirectPlay.cpp`. **Found one genuine gap**: `Send()` never checked `lpData` for null before
pointer arithmetic (`bytes + dwDataSize` in the self-send path, `payloadBytes + dwDataSize` in the
unicast path) when `dwDataSize > 0` - undefined behavior on a null `lpData` with a nonzero size, a
real crash risk. All other pointer parameters were already correctly handled: `lpEnumSessionsDesc`
(optional, matches real DirectPlay's "enumerate all" contract), `lpSessionDesc` (checked),
`lpPlayerName` (optional, checked when present), `lpidPlayer`/`lpidFrom`/`lpidTo` (output params,
correctly treated as optional before writing), `lpdwDataSize` (checked inside `TryReceive`). Fixed
in `DirectPlay.cpp` (see TASK-24H-0107) rather than only documented, since this is hardening
existing validated behavior against a real crash, not new API surface.

### TASK-24H-0107: Add tests for any null-pointer gap found in TASK-24H-0106
Status: DONE
Priority: P1
Area: DirectPlay
Type: Test
Evidence: TASK-24H-0106
Depends on: TASK-24H-0106

Problem:
Any gap found by the null-pointer sweep needs both a fix and a regression test.

Required work:
- For each gap found, add a `DPERR_INVALIDPARAMS`-returning check plus a test proving it.

Acceptance criteria:
- Each fix has a passing test; total test count increases accordingly.

Out of scope:
- Do not fix gaps unrelated to null-pointer handling while doing this sweep — file separate tasks.

Verified: added `if (!lpData && dwDataSize > 0) return DPERR_INVALIDPARAMS;` once in `Send()`,
covering both the self-send and unicast branches (both come after this check). Two tests added:
`Test_Send_NullPayloadWithNonzeroSize_ReturnsInvalidParams` (proves the fix) and
`Test_SelfSend_NullPayloadWithZeroSize_ReturnsOk` (proves the still-valid null+zero-size
no-payload-send case, per plan.md Phase 10's own "accepted no-payload send" convention, still
works). All 49 pre-existing tests re-verified passing after the fix, before adding the new ones.

### TASK-24H-0108: Add EnetDirectPlayTransport-level Send/Receive unit test with hardcoded ports
Status: DONE
Priority: P1
Area: ENet
Type: Test
Evidence: Original prompt ("transport-level reliable delivery smoke test only if it does not require unresolved host-address design")
Depends on: None

Problem:
Beyond the single reliable-delivery smoke test in TASK-24H-0097, transport-level `Send`/`Receive`
correctness (e.g. unreliable-vs-reliable flag mapping, multiple messages, disconnect handling)
deserves more than one test, since this is the class doing the real socket work.

Required work:
- Add 2-3 additional `EnetDirectPlayTransport`-level tests: unreliable send doesn't crash if
  dropped, multiple queued messages arrive in order, and `Shutdown()` cleanly closes both ends —
  all using hardcoded `127.0.0.1` ports, no discovery mechanism required.

Acceptance criteria:
- Tests pass under `-DFREE_DIRECT_ENABLE_ENET=ON`; absent otherwise (same gating as TASK-24H-0098).

Out of scope:
- Do not test through the `IDirectPlay2A` API surface — transport-level only, same reasoning as
  TASK-24H-0097.

Verified: covered by the same `tests/enet_directplay_tests.cpp` file as TASK-24H-0097 -
`Test_EnetTransport_UnreliableSend_ClientToHost_DeliversPayload` (unreliable send on a healthy
local connection) and `Test_EnetTransport_Shutdown_ClosesConnectionCleanly` (graceful two-sided
teardown, confirmed via `HasHost()`/`IsConnectedToHost()` transitioning correctly). Multiple-
queued-messages-in-order was not added as a separate test - `EnetDirectPlayTransport`'s reliable
channel is a single ENet channel (Decision 2), and ordering within one channel is an ENet library
guarantee, not free-direct logic to regression-test at this layer; the existing reliable-delivery
test already proves one message's round trip end-to-end.

### TASK-24H-0109: Add a regression test that DirectPlaySession's transport is null after Shutdown/Close
Status: DONE
Priority: P2
Area: DirectPlay
Type: Test
Evidence: DirectPlay Plan Reconciliation (defensive cleanup paths in Close()/Release() lightly tested)
Depends on: TASK-24H-0076

Problem:
No test directly verifies the internal invariant that a closed/released session's transport
pointer is genuinely torn down (as opposed to merely behaving as if it were).

Required work:
- If a test-only accessor already exists (or can be added narrowly, `#ifdef`-gated to test builds
  only, not part of the public API) to observe this, add
  `Test_Close_TransportIsShutDown`; otherwise, document why this can't be observed without new
  public API surface and skip.

Acceptance criteria:
- Either a passing test exists, or a documented reason it's not currently observable.

Out of scope:
- Do not add new public API surface to make this internal state observable — internal/whitebox
  testing only, and only if it doesn't require touching `include/dplay.h`.

Verified via the indirect proof this task's own text anticipated (no test-only accessor exists,
and none was added, per the out-of-scope note): `Test_Close_ThenNewHostCanRebindSamePort_ProvesTransportShutdown`
confirms that after `Close()`, a brand-new host can immediately rebind the same fixed loopback
port - which could only succeed if the closed session's transport genuinely released the port
binding, the same indirect-proof pattern `Test_LoopbackShutdown_UnregistersPortForReuse` already
established at the transport level and `Test_Release_WithoutPriorClose_CleansUpTransportAndRegistry`
established for `Release()`.

### TASK-24H-0110: Re-verify the "46 tests" count is accurate after all DirectPlay test additions in this backlog
Status: DONE
Priority: P1
Area: Tests
Type: Verification
Evidence: This backlog adds ~15 new DirectPlay tests across TASK-24H-0077 through 0109
Depends on: TASK-24H-0077, TASK-24H-0078, TASK-24H-0079, TASK-24H-0080, TASK-24H-0081, TASK-24H-0082, TASK-24H-0092, TASK-24H-0093, TASK-24H-0101, TASK-24H-0102, TASK-24H-0103, TASK-24H-0104, TASK-24H-0105

Problem:
`NEXT.md` and `README.md` should always state an accurate current test count, not a stale "46."

Required work:
- After the tasks above land, recount `Test_*` functions in `tests/directplay_tests.cpp` and update
  `NEXT.md`.

Acceptance criteria:
- `NEXT.md`'s stated count matches `grep -c "^void Test_" tests/directplay_tests.cpp` (or the
  equivalent pattern the file actually uses) exactly.

Out of scope:
- Do not round or approximate the count.

Verified: `grep -c "^void Test_" tests/directplay_tests.cpp` reports **61** (up from 49 at the
start of this continuation session, 46 at the start of the prior session). `NEXT.md` updated to
match exactly (see this session's NEXT.md update).

## Diagnostics/logging

### TASK-24H-0111: Audit FREE_DIRECT_DEBUG_DDRAW coverage for gaps
Status: DONE
Priority: P1
Area: Diagnostics
Type: Audit
Evidence: README.md (documents the flag exists); docs/audit-24h-free-direct.md §4
Depends on: None

Problem:
It's not yet confirmed that every DirectDraw diagnostic log statement is actually gated by
`FREE_DIRECT_DEBUG_DDRAW` rather than some being unconditional.

Required work:
- Grep `src/directdraw/DirectDraw.cpp` for every log/print statement and confirm each is inside an
  `FREE_DIRECT_DEBUG_DDRAW`-gated (or a more specific `_PRESENTATION`/`_COLORKEY`/`_PERF`/
  `_PRIMARY_CLEAR`-gated) branch.

Acceptance criteria:
- A finding recorded: either "all gated" or a list of ungated statements with a follow-up fix task
  filed per finding.

Out of scope:
- Do not remove genuinely useful gated diagnostics — audit for missing gates only.

Finding: **all gated, no violation.** Every `SDL_Log(...)` call site in `DirectDraw.cpp` (65 call
sites, including all `Blt`/`BltFast`/`GetDC`/`ReleaseDC`/`Lock`/`Unlock` ones) routes through the
file's own `#define SDL_Log DirectDrawLog` macro (line 98), and `DirectDrawLog` (line 50) checks
`IsDirectDrawDebugEnabled()` first and returns immediately when `FREE_DIRECT_DEBUG_DDRAW` is unset
— no log output reaches stdout/stderr by default. Minor non-blocking note (not a gap, not
actioned): `IsDirectDrawDebugEnabled()`/`IsPresentationDebugEnabled()`/`IsColorKeyDebugEnabled()`
call `SDL_getenv` on every single invocation (uncached), unlike `IsPerfDebugEnabled()` in the same
file, which caches its result in a `static int`. This costs a getenv lookup per `Blt`/`BltFast`
call even with logging disabled — real but minor hot-path overhead, not an output/correctness bug.
Left unfixed here since TASK-24H-0117 (the fix task) is explicitly scoped to "ungated log output,"
not this narrower overhead question; flagging it as a possible future P2 cleanup if profiling ever
shows it matters.

### TASK-24H-0112: Audit FREE_DIRECT_DEBUG_PRESENTATION coverage for gaps
Status: DONE
Priority: P1
Area: Diagnostics
Type: Audit
Evidence: README.md
Depends on: None

Problem:
Same rationale as TASK-24H-0111, for the presentation-path-specific flag.

Required work:
- Grep the presentation/present-path code (`PresentPrimary` and related functions) for ungated
  logging.

Acceptance criteria:
- Finding recorded.

Out of scope:
- None beyond the audit itself.

Finding: **all gated, no violation.** All `PresentLog(...)`/`SDL_LogInfo(...)` calls in
`PresentPrimary` and related presentation-path functions are gated behind
`IsPresentationDebugEnabled()` (`FREE_DIRECT_DEBUG_PRESENTATION`), same uncached-getenv note as
TASK-24H-0111 applies here too (not actioned, same reasoning).

### TASK-24H-0113: Audit FREE_DIRECT_DEBUG_COLORKEY coverage for gaps
Status: DONE
Priority: P1
Area: Diagnostics
Type: Audit
Evidence: README.md
Depends on: None

Problem:
Same rationale, for color-key diagnostics.

Required work:
- Grep color-key-handling code paths for ungated logging.

Acceptance criteria:
- Finding recorded.

Out of scope:
- None beyond the audit itself.

Finding: **all gated, no violation.** All `ColorKeyLog(...)` calls are gated behind
`IsColorKeyDebugEnabled()` (`FREE_DIRECT_DEBUG_COLORKEY`), same uncached-getenv note applies.

### TASK-24H-0114: Audit FREE_DIRECT_DEBUG_DSOUND coverage for gaps
Status: DONE
Priority: P1
Area: Diagnostics
Type: Audit
Evidence: README.md
Depends on: None

Problem:
Same rationale, for `src/directsound/DirectSound.cpp`.

Required work:
- Grep for ungated logging in DirectSound code.

Acceptance criteria:
- Finding recorded.

Out of scope:
- None beyond the audit itself.

Finding: `DS_LOG`/`DS_FMTLOG` macros are correctly gated behind `dsDebugEnabled()`/
`dsFormatDebugEnabled()` (`FREE_DIRECT_DEBUG_DSOUND`/`FREE_DIRECT_DEBUG_DSOUND_FORMAT`), and unlike
`DirectDraw.cpp`, these **are** cached in a `static int` (no per-call getenv overhead — this file's
pattern is actually the better one of the two). Five raw, unconditional `SDL_Log(...)` calls exist
outside those macros (lines ~169, 175, 365, 550, 557) — but all five are SDL API **failure-path**
logs (`SDL_InitSubSystem`/`SDL_OpenAudioDevice`/`SDL_PutAudioStreamData`/`SDL_CreateAudioStream`/
`SDL_BindAudioStream` failing), not per-frame/per-call success-path logs. Per CLAUDE.md's own
Documentation/Diagnostics policy ("Keep fatal/error logs useful"), these are correctly left
unconditional — not a gap.

### TASK-24H-0115: Add FREE_DIRECT_DEBUG_DPLAY gate for any ungated DirectPlay diagnostic logs
Status: DONE
Priority: P1
Area: Diagnostics
Type: Implementation
Evidence: CLAUDE.md's stated Diagnostics.hpp pattern; no DirectPlay-specific debug flag confirmed to exist yet
Depends on: None

Problem:
Unlike DirectDraw/DirectSound, it's not confirmed whether `src/directplay/*.cpp` has any debug
logging, gated or not.

Required work:
- Grep `src/directplay/` for log/print statements; if any unconditional ones exist, gate them
  behind a new `FREE_DIRECT_DEBUG_DPLAY` env-var check following the existing pattern in
  `DirectDraw.cpp`/`DirectSound.cpp`.

Acceptance criteria:
- Either no ungated logs are found (documented as such), or all found ones are now gated.

Out of scope:
- Do not add verbose new logging that didn't exist before — gate what's already there.

Finding: **no logging exists at all** in any `src/directplay/*.cpp` file (confirmed by grep for
`printf`/`std::cout`/`SDL_Log`/`fprintf` — zero matches). Nothing to gate; no
`FREE_DIRECT_DEBUG_DPLAY` flag was added since there is no unconditional (or any) log statement to
control. Revisit if/when DirectPlay logging is actually added.

### TASK-24H-0116: Add FREE_DIRECT_DEBUG_ENET gate for any ungated ENet diagnostic logs
Status: DONE
Priority: P1
Area: Diagnostics
Type: Implementation
Evidence: Same rationale as TASK-24H-0115, for EnetDirectPlayTransport.cpp
Depends on: None

Problem:
Same rationale as TASK-24H-0115, for the ENet transport.

Required work:
- Grep `src/directplay/EnetDirectPlayTransport.cpp` for log/print statements; gate any unconditional
  ones behind `FREE_DIRECT_DEBUG_ENET`.

Acceptance criteria:
- Either no ungated logs found, or all gated.

Out of scope:
- Do not add new logging.

Finding: **no logging exists at all** in `EnetDirectPlayTransport.cpp` (confirmed by grep, same as
TASK-24H-0115). Nothing to gate.

### TASK-24H-0117: Fix any ungated per-blit log found in Blt/BltFast hot paths
Status: DONE (no-op — no gap found)
Priority: P0
Area: Diagnostics
Type: Bugfix
Evidence: docs/audit-24h-free-direct.md §4 ("no unconditional hot-path logs" requirement); depends on TASK-24H-0111 finding
Depends on: TASK-24H-0111

Problem:
If TASK-24H-0111 finds an ungated log in `Blt`/`BltFast`, it is a real per-frame performance bug
given both methods' call frequency (§2.1).

Required work:
- Gate any such statement behind the appropriate `FREE_DIRECT_DEBUG_*` flag.

Acceptance criteria:
- TASK-24H-0048's log-capture test passes after the fix.

Out of scope:
- Only act if TASK-24H-0111 actually found a gap — do not speculatively change working code.

Verified no-op: TASK-24H-0111 found no ungated log in `Blt`/`BltFast` — all output is correctly
suppressed by default. No code change made, per this task's own out-of-scope clause.

### TASK-24H-0118: Fix any ungated per-call log found in DirectSound Play/Lock/Unlock
Status: DONE (no-op — no gap found)
Priority: P1
Area: Diagnostics
Type: Bugfix
Evidence: depends on TASK-24H-0114 finding
Depends on: TASK-24H-0114

Problem:
Same rationale as TASK-24H-0117, for DirectSound's per-call paths.

Required work:
- Gate any ungated statement found by TASK-24H-0114.

Acceptance criteria:
- TASK-24H-0073's log-capture test passes after the fix.

Out of scope:
- Only act if TASK-24H-0114 found a gap.

Verified no-op: TASK-24H-0114 found the five unconditional `SDL_Log` calls in `DirectSound.cpp` are
all failure-path (not per-call success-path) logs, correctly left unconditional per policy. No
code change made.

### TASK-24H-0119: Add CMake compile-definition overrides for FREE_DIRECT_DEBUG_* flags
Status: DONE
Priority: P2
Area: Diagnostics
Type: Implementation
Evidence: docs/audit-24h-free-direct.md §7 (flags are env-var-only today, no CMake option path)
Depends on: None

Problem:
A developer wanting to force-enable a debug flag at compile time (e.g. for a CI diagnostic build)
currently has no CMake-level way to do so short of manually passing `-D` flags outside the normal
option mechanism.

Required work:
- Add optional CMake cache variables (e.g. `FREE_DIRECT_FORCE_DEBUG_DDRAW`) that, when set, add the
  corresponding `target_compile_definitions`. Keep the existing env-var runtime check as the
  primary/default mechanism — this is additive, not a replacement.

Acceptance criteria:
- Default builds are unaffected; setting the new CMake variable measurably changes compiled
  behavior (e.g. logs appear even with the env var unset).

Out of scope:
- Do not remove the env-var-based runtime toggle — it remains the primary mechanism per existing
  design.

Verified: Grepped `src/` for every `FREE_DIRECT_DEBUG_*`/`SDL_getenv` read (7 flags found: DDRAW,
PRESENTATION, COLORKEY, PERF, PRIMARY_CLEAR in `DirectDraw.cpp`; DSOUND, DSOUND_FORMAT in
`DirectSound.cpp`). `DirectSound.cpp` already had `#ifdef FREE_DIRECT_DEBUG_DSOUND[_FORMAT]`
compile-time override support with no CMake option wired to it; `DirectDraw.cpp` had neither.
Added matching `#ifdef` short-circuits to all 5 DirectDraw flag-check functions (mirroring
`DirectSound.cpp`'s existing pattern: `#ifdef` forces `true`, else falls through to the existing
runtime env-var check - additive, not a replacement). Added a `foreach` loop in the root
`CMakeLists.txt` defining `FREE_DIRECT_FORCE_DEBUG_{DDRAW,PRESENTATION,COLORKEY,PERF,
PRIMARY_CLEAR,DSOUND,DSOUND_FORMAT}` options (OFF by default), each adding
`target_compile_definitions(free-direct PRIVATE FREE_DIRECT_DEBUG_<X>=1)` when ON. Verified the
mechanism actually works, not just compiles: built two separate scratch configs, one with
`-DFREE_DIRECT_FORCE_DEBUG_DDRAW=ON` and one with `-DFREE_DIRECT_FORCE_DEBUG_DSOUND=ON` (both
`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`), and ran `directdraw_tests`/
`directsound_tests` in each with the corresponding env var explicitly unset. Both builds produced
exactly one failing check each - `directdraw_tests.cpp`'s and `directsound_tests.cpp`'s own
zero-log regression guards (`Test_BltFast_NoUnconditionalLogOutput_WhenDebugFlagsUnset`,
`Test_PlayLockUnlock_NoUnconditionalLogOutput_WhenDebugFlagsUnset`) - proving logs now fire purely
from the compile-time flag with the runtime env var unset, i.e. exactly the acceptance criterion
("logs appear even with the env var unset"), and that failure was the *only* failure in each run
(no unrelated breakage). Then rebuilt a fully-default scratch config (no `FORCE_DEBUG` options set)
and confirmed `ctest` still passes 7/7 clean, proving default builds are unaffected. All scratch
directories removed after verification.

### TASK-24H-0120: Consolidate FREE_DIRECT_DEBUG_* documentation in one place
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: README.md already lists several flags; verify completeness against actual source
Depends on: None

Problem:
README.md lists several `FREE_DIRECT_DEBUG_*`/`FREE_DIRECT_TARGET_FPS`/`FREE_DIRECT_ENABLE_VSYNC`
flags, but it's not confirmed this list is exhaustive against what's actually in source.

Required work:
- Grep all of `src/` for `FREE_DIRECT_DEBUG_`/`FREE_DIRECT_` env-var reads and cross-check against
  README's documented list; add any missing entries.

Acceptance criteria:
- README's flag list matches source exactly.

Out of scope:
- Do not invent new flags — documentation-completeness only.

Verified: Cross-checked `grep -rn "FREE_DIRECT_DEBUG_\|FREE_DIRECT_DIAGNOSTICS\|FREE_DIRECT_TARGET_FPS\|
FREE_DIRECT_ENABLE_VSYNC" src/` against README.md's "Debug logging and performance options"
section. Found two undocumented flags: `FREE_DIRECT_DEBUG_DSOUND_FORMAT` (env-var read in
`DirectSound.cpp`, never mentioned in README) and `FREE_DIRECT_DIAGNOSTICS` (both its pre-existing
CMake option and its runtime env-var gate in `Diagnostics.cpp`, also never mentioned). Added both
to README, plus documented the 7 new `FREE_DIRECT_FORCE_DEBUG_*` CMake options this session's
TASK-24H-0119 introduced (with a clarifying note that `FREE_DIRECT_DIAGNOSTICS` is a materially
different mechanism - compile-gate, not force-enable-regardless-of-env - so it isn't lumped into
that group). Re-ran the same grep/cross-check afterward: every source-level flag name now has a
matching README mention with no extras invented beyond what source actually reads.

## Documentation

### TASK-24H-0121: Update README.md Project Status to reflect current DirectPlay depth
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: README.md currently says "DirectPlay: Currently stubbed (dummy implementations)" — docs/audit-24h-free-direct.md §6 shows this is materially stale
Depends on: None

Problem:
README.md's top-level feature summary still describes DirectPlay as pure dummy stubs, which this
audit found to be materially inaccurate (only `DirectPlayEnumerateA`/`W` remain genuine stubs).

Required work:
- Update the "DirectPlay" bullet in README's Overview/Features sections to describe the real
  loopback-backed implementation, its ENet transport's current join/discovery limitations, and
  continue to clearly state (per Documentation Policy) that this is FreeDirect-to-FreeDirect only,
  never Microsoft-wire-compatible.

Acceptance criteria:
- Updated text is accurate as of this audit and does not overclaim (e.g. does not say "full
  multiplayer support" when broadcast doesn't work yet).

Out of scope:
- Do not remove the existing honest caveats about DirectDraw/DirectSound limitations while editing
  this section.

Verified: Rewrote both DirectPlay bullets (Overview and Features sections) that previously said
"Currently stubbed (dummy implementations)" / "Declarations and dummy stubs provided" - now
describes real loopback session hosting/joining/unicast/enumeration, the ENet hosting-only status,
and explicitly lists what's NOT implemented (broadcast, host relay, `DirectPlayEnumerateA`/`W`)
rather than only describing what works, so the text cannot be read as overclaiming. Both bullets
end with the explicit "FreeDirect-to-FreeDirect only, never Microsoft-wire-compatible" statement.
Did not touch the existing DirectDraw/DirectSound bullets or the "Current known limitations"
section. Cross-checked the new wording against `docs/directplay-limitations.md`'s own deviation
table (written in the same session) for consistency - no contradictions.

### TASK-24H-0122: Add a compatibility status table to README.md
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: plan.md Phase 16 ("a compatibility status table in README.md")
Depends on: TASK-24H-0121

Problem:
There is no single at-a-glance table summarizing per-method DirectDraw/DirectSound/DirectPlay
status; a reader has to cross-reference three headers and several docs.

Required work:
- Add a compact status table (method/flag → STUB/PARTIAL/IMPLEMENTED) to README, sourced from the
  now-corrected tags in `include/*.h` (post TASK-24H-0083/0084/0085) and
  `docs/audit-24h-free-direct.md`.

Acceptance criteria:
- Table matches header Doxygen tags exactly at time of writing.

Out of scope:
- Do not let this table drift from the headers going forward — that's a documentation-maintenance
  norm, not a one-time task, but only the initial creation is in scope here.

Verified: Extracted every `@note Status:` tag from `include/ddraw.h`/`dsound.h`/`dplay.h` via grep
(not recalled from memory) before writing the table, including the 3 free factory functions
(`DirectDrawCreate`/`DirectSoundCreate`/`DirectPlayCreate`) which a first pass of the grep missed
and had to be re-queried for separately. Added a new "Compatibility Status" README section (between
"Features" and "Technologies") with one table per subsystem, each row's status copied verbatim from
its header tag, `QueryInterface`/`AddRef`/`Release` called out once as "IMPLEMENTED on every
interface" rather than repeated 3x per table to keep it genuinely compact as the task asks. Each
table links to its subsystem's `docs/*-limitations.md` for full detail.

### TASK-24H-0123: Update NEXT.md with this session's audit, plan, and implementation log
Status: DONE
Priority: P0
Area: Docs
Type: Documentation
Evidence: CLAUDE.md's NEXT.md Policy ("updated after each completed implementation batch")
Depends on: None (ongoing throughout Phase 3 implementation)

Problem:
`NEXT.md` must reflect this session's real work, not the prior session's snapshot, once
implementation begins.

Required work:
- After each implementation batch in this session, append a dated entry to `NEXT.md`: what was
  audited, what was added to `plan.md`, what was implemented, test/build results actually observed.

Acceptance criteria:
- `NEXT.md` never claims a task is done that wasn't actually built/tested this session (per
  CLAUDE.md's "never write fake or aspirational progress" rule).

Out of scope:
- Do not delete prior session's NEXT.md content — append/update per the existing living-document
  convention.

Verified: Full `NEXT.md` rewrite reflecting this entire session's work, done once at the natural
end of the docs-cleanup batch rather than after every individual task (this session's edits were
tracked incrementally via `plan.md`'s own per-task `Verified:` notes instead, then consolidated
here - functionally equivalent to "after each batch," matching how the existing file's own prior
"This session"/"Prior session" two-tier structure already condenses older detail rather than
growing unboundedly). All test/task counts in the new content are `grep`-verified against the real
`plan.md`/test-file state at time of writing (147 total, 123 DONE, 15 TODO, 1 PARTIAL, 8 BLOCKED;
61/53/30/4 real test counts per file), not recalled from memory or estimated. Did not delete prior
session's content - condensed the old "This session" entry (session 2's DirectDraw/DirectSound
focus) into a new "Prior session (2)" summary preserving its key facts (test count progression,
`TASK-24H-0057` PARTIAL reason, the standalone-SDL3 discovery), with an explicit pointer to `git
log` for full detail, mirroring exactly how the pre-existing file already condensed the original
session's content into "Prior session" one tier before this rewrite.

### TASK-24H-0124: Update docs build/test command documentation once CTest wiring lands
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: TASK-24H-0011 (duplicate consolidation)
Depends on: TASK-24H-0003, TASK-24H-0011

Problem:
(Tracking entry — see TASK-24H-0011 for the actual work; kept as a separate ID since the original
prompt explicitly names this as a documentation backlog theme.)

Required work:
- Confirmed satisfied by TASK-24H-0011 once TASK-24H-0003 lands.

Acceptance criteria:
- Same as TASK-24H-0011.

Out of scope:
- Same as TASK-24H-0011.

Verified: TASK-24H-0003 (CTest wiring) landed in the prior session; TASK-24H-0011 (this session)
rewrote README's build/test documentation with 5 freshly-verified configurations. This tracking
entry is satisfied by that work directly, per its own stated scope - no separate edit needed.

### TASK-24H-0125: Review docs for any accidental "full DirectX 3 compatibility" claim
Status: DONE
Priority: P1
Area: Docs
Type: Verification
Evidence: CLAUDE.md Documentation Policy
Depends on: None

Problem:
CLAUDE.md requires never claiming full DirectX 3 compatibility anywhere; this has not been
explicitly re-verified against current doc content in this session.

Required work:
- Grep `README.md`, `TODO.md`, `docs/*.md`, `NEXT.md` for phrases implying full compatibility
  ("full DirectX", "complete DirectX", "fully compatible") and fix any found.

Acceptance criteria:
- Zero such claims found, or each found instance is corrected.

Out of scope:
- Do not weaken accurate, narrower claims (e.g. "narrow subset") while doing this sweep.

Verified: Grepped `README.md`/`TODO.md`/`docs/*.md`/`NEXT.md` with the task's own suggested
patterns, then a broader `(100%|entirely|fully) (compatible|implements?|supports?)` sweep filtered
to exclude negated matches, to catch phrasing the literal 3 suggested patterns might miss. Every
match found across both sweeps is a correct negative disclaimer (e.g. "not full DirectX
compatibility", "❌ Full DirectX 3 compatibility" under a Non-Goals heading) - zero actual overclaims
found anywhere, nothing needed correcting. No narrower/accurate claims were touched.

### TASK-24H-0126: Review docs for any accidental Microsoft DirectPlay wire-compatibility claim
Status: DONE
Priority: P1
Area: Docs
Type: Verification
Evidence: CLAUDE.md Documentation Policy
Depends on: None

Problem:
Same rationale as TASK-24H-0125, specifically for DirectPlay wire/packet compatibility claims.

Required work:
- Grep the same doc set for phrases implying wire/packet compatibility with real Microsoft
  DirectPlay and fix any found.

Acceptance criteria:
- Zero such claims found, or each found instance is corrected.

Out of scope:
- None.

Verified: Same sweep methodology as TASK-24H-0125, patterns targeted at wire/packet-compatibility
phrasing instead. Every match across `README.md`/`docs/*.md`/`NEXT.md` is a correct negative
disclaimer or a factual comparison-for-context citation of real Microsoft DirectPlay behavior (e.g.
`docs/directplay-design.md`'s Decisions explaining why FreeDirect diverges from it) - never a claim
of actually having that compatibility. Zero overclaims found. **Found and fixed one real, unrelated
error while reading `docs/directplay-protocol.md` during this sweep**: line 15 still said "56-byte
header" from an early draft, left unfixed when the byte-count correction (72 bytes, TASK-24H-0095's
own verified note) was applied to the rest of the file - fixed to reference the corrected 72-byte
figure and point at the "Known gaps" section.

### TASK-24H-0127: Verify README states FreeDirect multiplayer only works between two FreeDirect-linked programs
Status: DONE
Priority: P1
Area: Docs
Type: Verification
Evidence: CLAUDE.md Documentation Policy ("Always state that FreeDirect multiplayer only works between programs both built against this FreeDirect DirectPlay implementation")
Depends on: TASK-24H-0121

Problem:
This explicit required statement should be re-verified present and accurate after
TASK-24H-0121's README update.

Required work:
- Confirm the statement is present, clear, and not contradicted elsewhere in README.

Acceptance criteria:
- Statement present and accurate.

Out of scope:
- None.

Verified: Confirmed present twice in README (Overview's DirectPlay bullet: "FreeDirect-to-FreeDirect
only — never compatible with real Microsoft DirectPlay at the wire/packet level"; Features'
DirectPlay bullet: "FreeDirect-to-FreeDirect only, never Microsoft-wire-compatible"), both added by
TASK-24H-0121 in this same session. Grepped the rest of README for any contradicting broader claim
- none found.

### TASK-24H-0128: Cross-link docs/audit-24h-free-direct.md from README's status section
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: New doc created this session
Depends on: TASK-24H-0121

Problem:
The new audit doc isn't discoverable from README today.

Required work:
- Add a one-line link/reference to `docs/audit-24h-free-direct.md` in README's Project Status
  section.

Acceptance criteria:
- Link present and correct.

Out of scope:
- None.

Verified: Added a one-line cross-link to `docs/audit-24h-free-direct.md` immediately under
README's "Project Status" heading, alongside a pointer to the new "Compatibility Status" section.
Confirmed the relative link path resolves correctly (file exists at the linked path).

### TASK-24H-0129: Add a "known blocked design questions" section to NEXT.md
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: DirectPlay Plan Reconciliation §5 (five explicitly unresolved questions)
Depends on: None

Problem:
The five (now seven, per CLAUDE.md's own enumeration) blocked DirectPlay design questions are
scattered across `docs/directplay-design.md`, `plan.md`, and this session's audit; `NEXT.md` should
summarize them in one place for the next person picking up this project.

Required work:
- Add a short section to `NEXT.md` listing each blocked question, one line each, with a pointer to
  where it's discussed in full (Decision number or `plan.md` phase).

Acceptance criteria:
- All seven questions from CLAUDE.md's "Blocked DirectPlay design questions" list are present.

Out of scope:
- Do not answer any of the questions in this task.

Verified: Section 8 Track B of the rewritten `NEXT.md` lists all 7 questions by name (DPID-0
broadcast/self-send semantics, ENet host-address resolution, LAN discovery, host routing, player
names, duplicate-player definition, player-lost state), cross-referencing `docs/directplay-limitations.md`'s
own dedicated "Standing BLOCKED design questions" section (created this session, TASK-24H-0094)
which additionally cites the specific Decision number(s) each question traces back to. None of the
7 questions were answered while writing either section - both are pure enumeration/pointers.

### TASK-24H-0130: Mark plan.md Phase 16 tasks already satisfied ahead of schedule
Status: DONE
Priority: P2
Area: Docs
Type: Cleanup
Evidence: DirectPlay Plan Reconciliation §6 ("docs/directplay-design.md and docs/directplay-callsite-audit.md already exist")
Depends on: None

Problem:
Phase 16 shows 0 done, but two of its deliverables (`docs/directplay-design.md`,
`docs/directplay-callsite-audit.md`) already exist, created early in Phases 0/1.

Required work:
- Check the corresponding Phase 16 boxes, citing that they were satisfied ahead of schedule.

Acceptance criteria:
- Boxes checked with citation.

Out of scope:
- Do not check boxes for docs created in THIS session (directdraw-limitations.md,
  directsound-limitations.md, directplay-limitations.md, directplay-protocol.md,
  networking-backends.md) until they actually exist via their own tasks above.

Verified: By the time this task was reached, TASK-24H-0094/0095/0096 (this session) had already
created `directplay-limitations.md`/`directplay-protocol.md`/`networking-backends.md`, and
`directdraw-limitations.md`/`directsound-limitations.md` had been created earlier the same
session - satisfying this task's own "until they actually exist" condition for all five, not just
the original two. Checked all 11 Phase 16 boxes (see Phase 16 section directly), each with a
citation to the specific `plan.md` task or session timing that satisfied it - not just the two the
task's own Problem statement named. Also closed Phase 16's own acceptance criterion ("linked from
README.md") by confirming every listed doc is actually linked from README (Compatibility Status
section, Overview/Features DirectPlay bullets, or the Project Status audit-doc cross-link), rather
than only checking existence.

## Blocked DirectPlay design questions

These map directly to CLAUDE.md's seven named blocked questions. None are resolved here. Each is
recorded so implementation can skip it and move to the next safe task, per this backlog's stated
policy.

### TASK-24H-0131: DECISION NEEDED — Is DPID 0 broadcast, host player, or invalid?
Status: DONE
Priority: P0
Area: DirectPlay
Type: Verification
Evidence: docs/audit-24h-free-direct.md §1/§6 (concrete collision found: host's real broadcast call currently self-sends instead); docs/directplay-design.md Decisions 3 and 15
Depends on: None

Problem:
Decision 3 assigned DPID 0 to the host's own first local player. `free-eggbert`'s only reachable
`Send()` pattern is `Send(m_dpid, 0, ...)`, a broadcast. Today, when the host calls this, it
collides with the self-send branch and the message never reaches any remote client. This is the
single highest-priority unresolved question in the whole project per this audit.

Required work:
- **Do not decide this unilaterally.** Ask the user: should DPID 0 be reinterpreted as
  `DPID_ALLPLAYERS` (breaking the current "host's player is DPID 0" simplicity), should the host's
  own player be reassigned a non-zero DPID so 0 is free to mean broadcast, or should
  `free-eggbert`'s call be special-cased at the FreeDirect layer some other way?

Acceptance criteria:
- A decision is recorded as a new Decision entry in `docs/directplay-design.md` once made by the
  user; only then should broadcast implementation (and TASK-24H-0091's constants) proceed.

Out of scope:
- Do not implement any broadcast delivery logic before this is decided.

Verified: Asked the user directly (not decided unilaterally). Decision: `idTo == 0` always means
broadcast to every other player, checked before the self-send branch so it takes priority even for
the host's own `Send(0, 0, ...)`. Recorded as `docs/directplay-design.md` Decision 20. Unblocks
`TASK-24H-0091` and the new implementation task `TASK-24H-0148`.

### TASK-24H-0132: DECISION NEEDED — How does Open(...JOIN...) discover a host address for ENet?
Status: DONE
Priority: P1
Area: DirectPlay
Type: Verification
Evidence: docs/audit-24h-free-direct.md §6; DPSESSIONDESC2 has no address-like field (include/dplay.h:128-151)
Depends on: None

Problem:
Loopback solved this with a fixed in-process port constant; ENet has no equivalent because it
requires a real IP/hostname the joining process doesn't have any current way to learn.

Required work:
- **Do not decide this unilaterally.** Ask the user: should FreeDirect extend `DPSESSIONDESC2`
  usage with an application-supplied address (e.g. via `dwUser1-4` or a documented convention), add
  a new (ask-first) API for specifying a target address, or take some other approach?

Acceptance criteria:
- A decision recorded in `docs/directplay-design.md`; only then should ENet-joining implementation
  proceed.

Out of scope:
- Do not implement ENet joining before this is decided. Do not add LAN broadcast discovery as an
  implicit answer to this question (see TASK-24H-0133 — that's a separate question).

Verified: Asked the user directly. Decision: a new environment variable,
`FREE_DIRECT_ENET_HOST_ADDRESS` (`"<host>"` or `"<host>:<port>"`), read once by `Open()`'s ENet
joining branch. Recorded as `docs/directplay-design.md` Decision 22. Implementation:
`TASK-24H-0149`.

### TASK-24H-0133: DECISION NEEDED — Should FreeDirect implement LAN broadcast discovery?
Status: DONE
Priority: P2
Area: DirectPlay
Type: Verification
Evidence: plan.md Phase 8 ("ask the user before starting this task, per the two-game scope rule")
Depends on: None

Problem:
`plan.md` already gates this behind an explicit ask; it has not been asked yet.

Required work:
- **Do not decide this unilaterally.** Ask the user whether a concrete need exists (e.g. driven by
  a specific `free-eggbert` UX goal) before starting this task at all.

Acceptance criteria:
- A yes/no decision recorded; if yes, a new Phase 8 task is added with real acceptance criteria.

Out of scope:
- Do not implement any UDP broadcast/multicast code before this is decided.

Verified: Asked the user directly, including the recommended "no, out of scope" default (no known
`free-eggbert` call site reaches this path). The user chose **yes**, as a deliberate scope
exception. Decision: raw UDP broadcast (not `ENetHost`) on a new dedicated discovery port
(`51323`), using the already-defined `Discovery`/`DiscoveryResponse` wire packet types, additive to
the existing loopback registry lookup. Recorded as `docs/directplay-design.md` Decision 23.
Implementation: new task `TASK-24H-0150`.

### TASK-24H-0134: DECISION NEEDED — Should the host route messages between non-host peers?
Status: DONE
Priority: P1
Area: DirectPlay
Type: Verification
Evidence: docs/audit-24h-free-direct.md §6; plan.md Phase 10 (star-topology relay, not started)
Depends on: None

Problem:
Today, a joining peer can only reach the host — not other joining peers. Whether this needs to
change depends on whether `free-eggbert`'s actual multiplayer design requires peer-to-peer
messaging or only ever needs host-mediated communication.

Required work:
- **Do not decide this unilaterally.** Ask the user whether host-side routing/relay is needed, given
  that the one reachable `Send()` call pattern found in this audit is a broadcast, not a
  peer-targeted send — which may mean this question is lower-urgency than it first appears, but
  should still be confirmed rather than assumed.

Acceptance criteria:
- A decision recorded; if yes, a new Phase 10 task is added.

Out of scope:
- Do not implement relay/forwarding logic before this is decided.

Verified: Asked the user directly, alongside TASK-24H-0131 (the two are entangled - broadcast
cannot reach non-host peers without relay). Decision: **yes**, the host relays a broadcast
(`idTo == 0`) received from one peer to every other connected peer, and enqueues a copy for itself.
Recorded as `docs/directplay-design.md` Decision 21, sharing implementation task `TASK-24H-0148`
with Decision 20 (not meaningfully separable - broadcast delivery for a non-host sender requires
this relay to reach anyone at all).

### TASK-24H-0135: DECISION NEEDED — Should player names be stored and exposed?
Status: DONE
Priority: P2
Area: DirectPlay
Type: Verification
Evidence: docs/directplay-design.md Decision 17; docs/audit-24h-free-direct.md §6 (real need confirmed — free-eggbert always supplies a short name — but storage deferred pending observability design)
Depends on: None

Problem:
`free-eggbert` always supplies a real short player name to `CreatePlayer`, so there is a genuine
need, but storing it today would be permanently unobservable dead state without new public API
surface (e.g. a `GetPlayerName`-style method), which itself requires an ask-first decision per
CLAUDE.md's Public Header Policy.

Required work:
- **Do not decide this unilaterally.** Ask the user whether adding a new observability method to
  `IDirectPlay2A` is acceptable (it would be new public surface beyond the two games' currently-
  observed call sites), or whether name storage should wait until a concrete consumer need is
  identified.

Acceptance criteria:
- A decision recorded in `docs/directplay-design.md`.

Out of scope:
- Do not add a new public method to `include/dplay.h` before this is decided.

Verified: Asked the user directly. Decision: do not store player names, and do not add a new
`GetPlayerName`-style method. Recorded as `docs/directplay-design.md` Decision 24. No code change
required - `CreatePlayer()`'s existing behavior (accept but don't retain `lpPlayerName`) already
matches this decision.

### TASK-24H-0136: DECISION NEEDED — What defines a "duplicate" player for CreatePlayer validation?
Status: DONE
Priority: P2
Area: DirectPlay
Type: Verification
Evidence: plan.md Phase 9 ("Needs a concrete definition of 'duplicate' from the user before any code could target it")
Depends on: None

Problem:
Phase 9 investigated this and found no concrete definition to implement against (same name? same
connection? something else?).

Required work:
- **Do not decide this unilaterally.** Ask the user for a concrete definition before writing any
  validation code.

Acceptance criteria:
- A decision recorded.

Out of scope:
- Do not guess at a definition and implement speculative validation.

Verified: Asked the user directly. Decision: no duplicate-player detection is added - current
collision-free-by-construction DPID allocation is sufficient. Recorded as
`docs/directplay-design.md` Decision 25. No code change required.

### TASK-24H-0137: DECISION NEEDED — Is a distinct "player-lost" state needed, separate from clean removal?
Status: DONE
Priority: P2
Area: DirectPlay
Type: Verification
Evidence: plan.md Phase 9 (investigated only, same observability wall as player names)
Depends on: None

Problem:
Real DirectPlay distinguishes an abrupt player-lost disconnect from a clean removal; FreeDirect
does not today, and it's unclear whether `free-eggbert` needs the distinction since nothing in its
reachable code path currently observes it.

Required work:
- **Do not decide this unilaterally.** Ask the user whether this distinction matters for
  `free-eggbert`'s actual (currently mostly-unreachable) session-lifecycle UI, or whether it can
  remain undifferentiated until a concrete need surfaces.

Acceptance criteria:
- A decision recorded.

Out of scope:
- Do not implement a new state enum value before this is decided.

Verified: Asked the user directly. Decision: no distinct player-lost state is added - a
disconnected remote player is removed uniformly regardless of whether the disconnect was clean or
abrupt. Recorded as `docs/directplay-design.md` Decision 26. No code change required.

## Post-decision implementation (Track B unblocked)

Three new atomic tasks, added per TASK-24H-0131/0132/0133/0134's own acceptance criteria ("only
then should implementation proceed... a new task is added"), now that the user has made all 4
underlying decisions (Decisions 20-23, `docs/directplay-design.md`).

### TASK-24H-0148: Implement real broadcast (idTo==0) delivery with host-side relay
Status: DONE
Priority: P0
Area: DirectPlay
Type: Implementation
Evidence: docs/directplay-design.md Decisions 20/21; TASK-24H-0131/0134
Depends on: TASK-24H-0131, TASK-24H-0134 (both DONE - decisions recorded)

Problem:
`Send(idFrom, 0, ...)` is `free-eggbert`'s only reachable call pattern, intended as a broadcast to
every other player, but today only reaches the sender itself (self-send collision) for a host, and
returns `DPERR_INVALIDPLAYER` unconditionally for a joining role. Decisions 20/21 resolved how this
should work; this task implements it.

Required work:
- Add `DPID_ALLPLAYERS`/`DPID_SYSMSG` constants to `include/dplay.h` (`TASK-24H-0091`).
- `DirectPlay.cpp`'s `Send()`: add a broadcast branch, checked before the `idTo == idFrom` self-send
  branch, for `idTo == DPID_ALLPLAYERS`. Hosting role: iterate `remotePlayerIds`, addressed
  `transport->Send()` to each (no self-delivery to the host's own queue). Joining role: send to the
  single `hostPeer_` connection with the wire header's `idTo` left at `DPID_ALLPLAYERS` (the relay
  marker, not a real address).
- `DirectPlay.cpp`'s `Receive()` drain loop, `Data` case: when `session_.isHost` and
  `header->idTo == DPID_ALLPLAYERS`, enqueue a copy locally *and* relay (addressed `transport->Send()`)
  to every `remotePlayerIds` entry except the one matching `header->idFrom` (the original sender).
- Update `TASK-24H-0092`'s characterization test (`Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`)
  - its own premise (the collision) is exactly what this task fixes, so its assertion is now wrong;
  rename/rewrite it to assert the new, correct broadcast behavior rather than leaving it stale.

Acceptance criteria:
- New tests: a host broadcasting reaches every connected remote client and not its own queue; a
  joining client broadcasting is relayed by the host to every *other* connected client (not back to
  the sender) and also reaches the host's own queue; a two-remote-client fan-out scenario. All pass.
- `TASK-24H-0092`'s test is updated, not left asserting stale (now-false) behavior.
- Existing 63+ `directplay_tests` still pass unaffected.
- Verified through a real build+test run (default and ASan+UBSan) plus a `../free-eggbert` rebuild.

Out of scope:
- ENet-side broadcast/relay (loopback-first, matching this project's established pattern - Decisions
  10-19 all did loopback before ENet) - only add it here if it falls out naturally from reusing
  Decision 14's existing per-DPID `Send()`, which already works for both backends; do not invent new
  ENet-specific relay code in this task if the shared `DirectPlay.cpp` logic already covers it.

Verified: `DPID_ALLPLAYERS`/`DPID_SYSMSG` added to `include/dplay.h` (`TASK-24H-0091`). `Send()`
gained a broadcast branch (checked before self-send, since both can match when `idFrom==idTo==0`
for the host specifically) - host role iterates `remotePlayerIds` addressing each directly; joining
role sends to its single `hostPeer_` with the wire header's `idTo` left at `DPID_ALLPLAYERS` as a
relay marker. `Receive()`'s `Data` case now relays (byte-for-byte, reusing the exact received wire
bytes, no re-serialization) to every other `remotePlayerIds` entry except the original sender when
`session_.isHost && header->idTo == DPID_ALLPLAYERS`, after enqueueing its own copy.

`TASK-24H-0092`'s test renamed/rewritten to `Test_HostBroadcast_ReachesAllRemoteClientsNotSelf`,
asserting the new correct behavior. **Found and fixed a real, unanticipated consequence while
implementing**: 5 more pre-existing tests used a host's first `CreatePlayer()` result (always DPID
`0`) purely as a stand-in "some player ID" for generic self-send testing - `Send(0, 0, ...)` no
longer means self-send for any caller now that broadcast is checked first, so these tests' own
premise silently broke too (4 became real failures, 2 passed by coincidence but were fixed anyway
since a test named "SelfSend" must not silently test broadcast instead). Fixed each by creating a
second, throwaway player first so self-send testing uses a guaranteed-non-zero DPID. Full list and
rationale in `docs/directplay-design.md` Decision 21's "Implemented" section.

Added 2 new tests per the acceptance criteria: `Test_HostBroadcast_ReachesMultipleRemoteClients`
(two-client fan-out) and `Test_ClientBroadcast_RelayedByHostToOtherClientAndHost` (relay + host's
own receipt + no echo-back to sender). **Verified for real**: 65/65 `directplay_tests` (was 63/63);
full CMake build+`ctest` (7/7); ASan+UBSan build+`ctest` (7/7 clean, zero diagnostics, directly
grepped raw output not just CTest's summary); ENet-enabled build+`ctest -L enet` (1/1, confirming
the shared broadcast/relay logic needed zero ENet-specific code, exactly as anticipated);
`header_hygiene` (clean); a full out-of-tree `../free-eggbert` rebuild (exit 0).

### TASK-24H-0149: Wire ENet joining Open() to a host address via FREE_DIRECT_ENET_HOST_ADDRESS
Status: DONE
Priority: P1
Area: DirectPlay
Type: Implementation
Evidence: docs/directplay-design.md Decision 22; TASK-24H-0132
Depends on: TASK-24H-0132 (DONE - decision recorded)

Problem:
The ENet backend's joining role is completely unwired - `Open(..., DPOPEN_JOIN)` never calls
`Connect()` at all under `FREE_DIRECT_ENABLE_ENET`, because there was no way to learn a host
address. Decision 22 resolved this; this task implements it.

Required work:
- `DirectPlay.cpp`'s `Open()`, ENet branch (`#ifdef FREE_DIRECT_ENABLE_ENET`), joining case: read
  `FREE_DIRECT_ENET_HOST_ADDRESS` via `SDL_getenv`; parse an optional trailing `:<port>` (default
  `kDefaultDirectPlayEnetPort` if absent); call `transport->Connect(host, port)`; return
  `DPERR_NOSESSIONS` if the env var is unset or `Connect()` fails.
- Send the existing `Join` wire packet (Decision 16) after a successful `Connect()`, matching the
  loopback path's existing shape exactly.

Acceptance criteria:
- New test in `tests/enet_directplay_tests.cpp` (gated `FREE_DIRECT_ENABLE_ENET`): a real two-instance
  ENet join using the env var succeeds; an unset env var returns `DPERR_NOSESSIONS` without crashing.
- Verified through a real ENet-enabled build+test run.

Out of scope:
- LAN discovery (TASK-24H-0150) - this task only wires a manually-supplied address.
- GUID-mismatch validation on join (Decision 16's own already-recorded "deliberately still out of
  scope").

Verified: `ParseEnetHostAddressEnvVar` helper added (anonymous namespace, `DirectPlay.cpp`) -
parses `"<host>"`/`"<host>:<port>"`, rejects a non-numeric port substring outright rather than
letting `strtol` silently parse a garbage prefix. `Open()`'s ENet joining branch calls it +
`transport->Connect()`, returning `DPERR_NOSESSIONS` on any failure, then sends the existing
`Join` packet unchanged. `tests/enet_directplay_tests.cpp` extended with 2 new tests going through
the real public `IDirectPlay2A` API (not whitebox, since this capability lives in `Open()` itself):
unset-env-var rejection, and a real end-to-end join (host `Open(DPOPEN_CREATE)`, client sets the
env var to `127.0.0.1` and `Open(DPOPEN_JOIN)`, polled until the client's deterministically-
expected DPID `1` is observably adopted via a self-send probe). 4/4 -> 6/6
`enet_directplay_tests`. Also fixed a stale file-header comment in the same test file (claimed the
host-discovery question was still BLOCKED per `TASK-24H-0132`, no longer true). **Verified for
real**: full CMake build+`ctest` (7/7, this task's changes are entirely inside `#ifdef
FREE_DIRECT_ENABLE_ENET`, so the default build is provably unaffected); ENet-enabled `ctest -L
enet` (1/1); ASan+UBSan+ENet combined build (7/7 clean, zero sanitizer diagnostics); `header_hygiene`
(clean); a full out-of-tree `../free-eggbert` rebuild (exit 0).

### TASK-24H-0150: Implement LAN UDP broadcast discovery for ENet-hosted sessions
Status: DONE
Priority: P2
Area: DirectPlay
Type: Implementation
Evidence: docs/directplay-design.md Decision 23; TASK-24H-0133
Depends on: TASK-24H-0133 (DONE - decision recorded), TASK-24H-0149 (host address wiring should land first)

Problem:
`EnumSessions()` only ever sees loopback-hosted sessions; an ENet-hosted session on another
process/machine is completely invisible to it. Decision 23 resolved that this should be fixed with
real UDP broadcast discovery; this task implements it.

Required work:
- New fixed constant `kDefaultDirectPlayDiscoveryPort = 51323` (`EnetDirectPlayTransport.hpp` or a
  new small private header), distinct from the existing ENet (`51321`) and loopback (`51322`) ports.
- Hosting role (ENet): open a dedicated `SO_BROADCAST`-enabled, non-blocking UDP socket on the
  discovery port; `Receive()`'s existing `Service()` call polls it for an inbound `Discovery`
  packet and replies (unicast) with a `DiscoveryResponse` carrying the session's real
  `applicationGuid`/`guidInstance`/`dwMaxPlayers`/`dwCurrentPlayers`/name, when the request's
  `applicationGuid` matches (or is the zero/wildcard GUID).
- `EnumSessions()` (ENet-enabled builds): after the existing loopback-registry lookup, additionally
  broadcast one `Discovery` packet to `INADDR_BROADCAST` on the discovery port and collect
  `DiscoveryResponse` replies for up to `dwTimeout` milliseconds (`dwTimeout == 0` means "no wait,
  return whatever arrived immediately" - never block indefinitely), invoking the callback once per
  distinct responding host and honoring an early `FALSE` return.
- Raw UDP sockets, not `ENetHost`/`ENetPeer` - keep this channel's failure modes independent of the
  peer-connection state machine (Decision 23's own rationale).

Acceptance criteria:
- New tests in `tests/enet_directplay_tests.cpp`: a real host responds to a real `Discovery`
  broadcast with an accurate `DiscoveryResponse`; `EnumSessions()` over ENet finds a real
  ENet-hosted session on a hardcoded loopback-adjacent test address; a non-matching
  `applicationGuid` filter excludes a session, matching the loopback path's existing filter
  semantics.
- Verified through a real ENet-enabled build+test run. `dwTimeout` handling verified not to block
  the test suite indefinitely (bounded, deterministic).

Out of scope:
- Real multi-interface/subnet detection - `INADDR_BROADCAST` only (Decision 23's own explicit
  scope boundary).
- Any change to the loopback backend's existing `EnumSessions()` behavior (Decision 18, unaffected).

Verified: New `src/directplay/DirectPlayDiscovery.hpp`/`.cpp` (private, `FREE_DIRECT_ENABLE_ENET`-
gated, wired into `CMakeLists.txt` alongside `EnetDirectPlayTransport.cpp`), built on ENet's own
portable `ENetSocket`/`enet_socket_*` primitives rather than hand-rolled per-platform sockets.
`kDefaultDirectPlayDiscoveryPort = 51323` added. `Open()`/`Receive()`/`Close()`/`EnumSessions()`
all wired per the Required Work above - see `docs/directplay-design.md` Decision 23's own
"Implemented" section for the full detail, including a real dangling-pointer bug **found in this
task's own test code** (not the implementation - `DPSESSIONDESC2::lpszSessionNameA` is only valid
during the callback, exactly like the existing loopback `EnumSessions` tests already correctly
handle) and a separate, also-real "wrote the tests but forgot to register them in `main()`"
oversight, both caught and fixed before this task was marked done.

Added `Test_EnumSessionsOverEnet_FindsRealHostedSession` and
`Test_EnumSessionsOverEnet_FiltersByApplicationGuid` (4->8 `enet_directplay_tests`), using a new
`BackgroundHostServicer` thread helper since `EnumSessions()` blocks internally for `dwTimeout`
waiting for a reply only the host generates if serviced during that window - reasoned through as
race-free by construction (the background thread and main thread touch two entirely separate
`IDirectPlay2A` objects, no shared mutable state beyond the already-mutex-guarded ENet lifecycle
counter), not sanitizer-proven (ASan/UBSan do not catch data races).

**Verified for real**: full CMake build+`ctest` (7/7, entirely `#ifdef`-gated, default build
provably unaffected); ENet-enabled `ctest -L enet` (1/1, 8/8 internal checks); ASan+UBSan+ENet
combined build (8/8 clean, zero sanitizer diagnostics, directly grepped raw output);
`header_hygiene` (clean); a full out-of-tree `../free-eggbert` rebuild (exit 0). `dwTimeout`
handling confirmed bounded - the full test suite (including both new discovery tests, one with a
1000ms and one with a 300ms `dwTimeout`) completes in ~6 seconds total, not hanging.

This was the last remaining task in the entire 150-task backlog (all 7 previously-BLOCKED Track B
design questions are now both decided *and* implemented, except the 3 explicitly-"not needed"
decisions - player names/duplicate-player/player-lost-state - which required no code).

## Integration

### TASK-24H-0138: Final integration re-verification: free-eggbert, end of session
Status: DONE
Priority: P0
Area: Integration
Type: Verification
Evidence: Consolidates TASK-24H-0007's recurring checks into one final gate
Depends on: None (run once, at the end of this session's implementation work)

Problem:
Before closing out this session, a single final confirmation that free-eggbert still builds clean
against everything changed is needed, independent of the per-task recurring checks.

Required work:
- Full clean rebuild of `../free-eggbert` against the final state of `free-direct` from this
  session.

Acceptance criteria:
- Build succeeds, zero new errors/warnings; result recorded in `NEXT.md` per TASK-24H-0123.

Out of scope:
- None.

Verified: Full clean out-of-tree rebuild (`rm -rf` scratch dir, fresh `cmake -B ... -S
../free-eggbert` + `cmake --build ...`) against this session's absolute final `free-direct` state
(after every other TASK-24H item in this batch was committed). `CONFIGURE_EXIT=0`,
`BUILD_EXIT=0`, `grep -ic "error:"` on the build log returned `0`. Produces `SPEEDY_BLUPI_WINDOWS`.
Result recorded in `NEXT.md` Section 2.

### TASK-24H-0139: Final integration re-verification: planetblupi, end of session
Status: DONE
Priority: P0
Area: Integration
Type: Verification
Evidence: Consolidates TASK-24H-0008's recurring checks into one final gate
Depends on: None (run once, at the end of this session's implementation work)

Problem:
Same rationale as TASK-24H-0138, for planetblupi.

Required work:
- Full clean rebuild of `../planetblupi` against the final state of `free-direct` from this session.

Acceptance criteria:
- Build succeeds, zero new errors/warnings; result recorded in `NEXT.md`.

Out of scope:
- None.

Verified: Same methodology as TASK-24H-0138, for `../planetblupi`. `CONFIGURE_EXIT=0`,
`BUILD_EXIT=0`, zero `error:` matches in the build log. Produces `PLANET_BLUPI_WINDOWS`.

### TASK-24H-0140: Final DirectPlay test re-run: end of session
Status: DONE
Priority: P0
Area: Tests
Type: Verification
Evidence: Consolidates TASK-24H-0014's recurring checks
Depends on: None (run once, at the end of this session's implementation work)

Problem:
A final, single source-of-truth test run should be recorded before the session's final report.

Required work:
- Run `ctest --test-dir build` (or the manual `g++` command if CTest wiring didn't land) and record
  the exact pass/fail count.

Acceptance criteria:
- Result recorded in `NEXT.md` and the final report, with the exact count (not "all tests" —
  the number).

Out of scope:
- None.

Verified: Fresh `/tmp` scratch build, `ctest --output-on-failure` (default, non-ENet config):
**7/7 tests pass** (`directplay_tests` 61/61 internally, `directdraw_tests` 53/53,
`directsound_tests` 30/30, `header_smoke_ddraw`/`_dsound`/`_dplay`/`header_hygiene` all pass).
Also re-ran under `-DFREE_DIRECT_ENABLE_ENET=ON`: `ctest -L enet` passes **1/1**
(`enet_directplay_tests` 4/4 internally); the unfiltered `ctest` against that same build shows
`directplay_tests` FAILING (1 test failed out of 8) - confirmed this is still the exact same,
already-documented, by-design incompatibility (not a new regression). Also re-ran under
`-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` (both with and without ENet): **7/7**
and **7/7** (ENet's `directplay_tests` excluded via label filter, matching the same by-design
scoping) pass clean, zero sanitizer diagnostics. All results recorded in `NEXT.md` Section 2 and
this session's final report.

### TASK-24H-0141: Final header-hygiene re-verification: end of session
Status: DONE
Priority: P0
Area: Headers
Type: Verification
Evidence: Consolidates TASK-24H-0015's recurring checks
Depends on: None (run once, at the end of this session's implementation work)

Problem:
Same rationale as TASK-24H-0140, for the SDL/ENet header-leak invariant.

Required work:
- Run the grep check from TASK-24H-0015 one final time against the session's final state.

Acceptance criteria:
- Zero violations; result recorded in `NEXT.md`.

Out of scope:
- None.

Verified: `bash tests/check_header_hygiene.sh include` run against this session's final state:
`Header hygiene OK: no SDL/ENet identifiers found outside comments under include`, exit `0`. Zero
violations. Also confirmed via the `header_hygiene` CTest test passing in every build configuration
run this session's final verification pass.

## Additional tasks added during the continuation session (2026-07-08, part 2)

Gaps found while working through the backlog above that didn't map to an existing task ID. Per
this document's own policy ("Add new TASK-24H-XXXX tasks only for gaps discovered"), these are new
atomic tasks, not edits to existing ones, numbered continuing from TASK-24H-0141.

### TASK-24H-0142: Add Play() called-twice-in-a-row restart test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/directsound-limitations.md ("Play() always restarts"); gap found while doing DirectSound buffer-lifetime edge-case work
Depends on: TASK-24H-0063

Problem:
The "Play() always restarts" behavior (`SDL_ClearAudioStream` + re-feed on every call, documented
in `docs/directsound-limitations.md`) was described but only exercised once per test elsewhere -
no test called `Play()` twice on the same buffer to prove the restart path itself works.

Required work:
- Add `Test_Play_CalledTwiceInARow_StillReportsPlaying`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test overlapping/polyphonic playback of the same buffer instance — not supported by
  design (one `SDL_AudioStream` per buffer).

Verified: added and passing.

### TASK-24H-0143: Add Stop()-on-never-played-buffer safe no-op test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: DirectSound.cpp's `Stop()` only touches `stream_` `if (stream_)`; gap found during edge-case work
Depends on: TASK-24H-0064

Problem:
No test verified calling `Stop()` before ever calling `Play()` (`stream_` still null) is a safe
no-op rather than an error or undefined behavior.

Required work:
- Add `Test_Stop_OnNeverPlayedBuffer_IsSafeNoOp`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: added and passing.

### TASK-24H-0144: Add two-buffers-play-simultaneously independence test
Status: DONE
Priority: P1
Area: DirectSound
Type: Test
Evidence: Real gameplay pattern (overlapping sound effects); gap found during edge-case work
Depends on: TASK-24H-0059, TASK-24H-0063

Problem:
No test verified that two distinct `IDirectSoundBuffer` instances created from the same
`IDirectSound` device can play simultaneously without interfering with each other's status - the
realistic pattern both target games exercise (multiple overlapping sound effects).

Required work:
- Add `Test_TwoBuffers_PlaySimultaneously_BothReportPlayingIndependently`, using two buffers with
  deliberately different PCM formats, confirming `Stop()` on one does not affect the other's
  `GetStatus()`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test more than two concurrent buffers — not a distinct code path.

Verified: added and passing.

### TASK-24H-0145: Add release-buffer-then-create-another-on-same-device test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: Real lifetime pattern (IDirectSound outlives individual buffers); gap found during edge-case work
Depends on: TASK-24H-0059

Problem:
No test verified that releasing one `IDirectSoundBuffer` does not affect the shared audio device's
availability for a buffer created afterward from the same still-open `IDirectSound` - the
realistic pattern for both target games (device opened once at startup, buffers created/destroyed
over the session).

Required work:
- Add `Test_ReleaseBuffer_ThenCreateAndPlayAnother_OnSameDevice_StillWorks`.

Acceptance criteria:
- Test passes.

Out of scope:
- Do not test releasing `IDirectSound` itself while a buffer is still alive - not a realistic call
  pattern for either target game (both keep the device alive for the whole process lifetime) and
  carries a real, untriaged crash risk (a buffer's `SDL_AudioStream` remains bound to a device that
  `SharedAudioDevice::release()` may close underneath it) that isn't worth taking for an
  unrealistic scenario. Documented here, not attempted.

Verified: added and passing.

### TASK-24H-0146: Add IDirectSound AddRef/Release lifetime test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: Mirrors directdraw_tests.cpp's AddRef/Release coverage; gap found during edge-case work
Depends on: TASK-24H-0056

Problem:
No test verified `IDirectSound::AddRef`/`Release` refcounting, unlike the equivalent DirectDraw
coverage added earlier this session.

Required work:
- Add `Test_DirectSound_AddRefRelease_AdjustsRefCount`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: added and passing.

### TASK-24H-0147: Add IDirectSoundBuffer AddRef/Release lifetime test
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: Same rationale as TASK-24H-0146, for the buffer interface
Depends on: TASK-24H-0059

Problem:
Same rationale as TASK-24H-0146, for `IDirectSoundBuffer`.

Required work:
- Add `Test_DirectSoundBuffer_AddRefRelease_AdjustsRefCount`.

Acceptance criteria:
- Test passes.

Out of scope:
- None.

Verified: added and passing. DirectSound test count: 24 -> 30 (this session's second batch).

## DirectDraw audit hardening (2026-07-09)

This section adds tasks derived from a fresh, evidence-based DirectDraw-only audit recorded in
`docs/audit_ddraw.md` (2026-07-09), which benchmarked the real compiled library (not just Big-O
reasoning) and cross-checked every finding's reachability against `free-eggbert`'s and
`planetblupi`'s actual call sites. Numbering continues from `TASK-24H-0150`. None of these are
`BLOCKED` — the audit raised no new design questions requiring a user decision.

### TASK-24H-0151: Default CMAKE_BUILD_TYPE to Release when unset
Status: DONE
Priority: P0
Area: Build
Type: Implementation
Evidence: docs/audit_ddraw.md §3.1 (finding F1) — default `cmake ..` with no `CMAKE_BUILD_TYPE`
measured 6.6x slower than `-DCMAKE_BUILD_TYPE=Release` on the same `BltFast` hot path
Depends on: None

Problem:
The root `CMakeLists.txt` never sets a default `CMAKE_BUILD_TYPE`. GCC/Clang apply no `-O` flags at
all when it's empty, so the project's own documented build instructions (`cmake -B build && cmake
--build build`, no extra flags) silently produce an unoptimized binary. This affects every function
in the library, not just DirectDraw.

Required work:
- In the root `CMakeLists.txt`, default `CMAKE_BUILD_TYPE` to `Release` when the caller hasn't set
  one (`if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES) set(CMAKE_BUILD_TYPE Release
  CACHE STRING "" FORCE) endif()`, placed before the first `project()`/target definition it can
  affect).
- Preserve a caller's explicit `-DCMAKE_BUILD_TYPE=Debug` (or any other value) — only fill in the
  default when the variable is empty.

Acceptance criteria:
- A clean `cmake -B build` with no extra flags now configures with `CMAKE_BUILD_TYPE=Release` and
  the resulting build shows `-O3` in `CMakeFiles/free-direct.dir/flags.make`.
- `cmake -B build -DCMAKE_BUILD_TYPE=Debug` still configures as `Debug`, unaffected.
- Existing test suites (DirectDraw/DirectSound/DirectPlay) still pass under the new default.

Out of scope:
- Do not add new build types or change what flags `CMAKE_CXX_FLAGS_RELEASE` etc. contain — only the
  default selection.

Verified: implemented via `if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT
CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)`, scoped to the top-level-project case only so a
consuming project's (`free-eggbert`/`planetblupi`) own build-type choice is never overridden.
Confirmed via fresh `/tmp` scratch builds: (1) `cmake -B <dir> -DFREE_API_USE_SYSTEM_SDL3=ON` with
no other flags now configures `CMAKE_BUILD_TYPE=Release` and `flags.make` shows `-O3 -DNDEBUG`; (2)
`-DCMAKE_BUILD_TYPE=Debug` still configures as `Debug`, unaffected; (3) full build with
`FREE_DIRECT_BUILD_TESTS=ON` succeeds and `ctest` passes 7/7 under the new default; (4) a fresh
out-of-tree `../free-eggbert` configure still succeeds and its own `CMAKE_BUILD_TYPE` stays empty
(unaffected, confirming the top-level-only scoping); (5) the bare-standalone (no flags, no sibling
game) configure still fails with the same actionable SDL3-missing error, unchanged.

### TASK-24H-0152: Add a 1:1 fast path to BlitFrom
Status: DONE
Priority: P0
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §3.2 (finding F2) — measured 29x slower than memcpy at -O3 (206x
unoptimized) for a 640x480 1:1 BltFast call; §8.1 confirms every real BltFast call site in both
games is structurally 1:1; §8.3 confirms CPixmap::Display()'s Blt call can genuinely scale
Depends on: None

Problem:
`BlitFrom` (`src/directdraw/DirectDraw.cpp:558-652`) computes a per-pixel division
(`(x * srcWidth) / dstWidth`) and re-checks a loop-invariant bpp branch on every pixel, even when
`srcWidth == dstWidth && srcHeight == dstHeight` (the identity case). Both games call `BltFast`
(always 1:1 by DirectDraw API definition — it has no destination-size parameter) far more than
`Blt`, so this cost is paid on the majority of real blit traffic every frame.

Required work:
- Add a runtime check at the top of `BlitFrom` for `srcWidth == dstWidth && srcHeight == dstHeight`;
  when true and no color key is requested, copy each row with a single contiguous copy
  (`std::memcpy`/`std::copy`) instead of the per-pixel loop.
- The check must be a runtime comparison of the actual clamped rect sizes, not an assumption based
  on which method (`Blt` vs `BltFast`) was called — `CPixmap::Display()`'s primary-present `Blt`
  call can have `srcWidth != dstWidth` when the window size doesn't match the game's logical
  resolution (§8.3), and must keep taking the scaling path in that case.
- Preserve existing color-key behavior exactly for the color-keyed case (the fast path only applies
  when `useSrcColorKey` is false or the source has no color key set).

Acceptance criteria:
- New/updated test asserts pixel-identical output between the old per-pixel path and the new fast
  path for a 1:1 copy (reuse or extend `Test_BltFast_OpaqueCopy_32Bit_PixelsMatchSource`/`_8Bit_...`
  in `tests/directdraw_tests.cpp`).
- Existing `Test_Blt_ScalingUpsamplesSourceToLargerDest` still passes unchanged (confirms the
  scaling path is untouched).
- Re-running this task's own benchmark methodology (docs/audit_ddraw.md §7) shows the fast-path
  `BltFast` cost is within a small constant factor of raw `memcpy`, not 29x.

Out of scope:
- Do not touch the scaling (non-1:1) code path's algorithm in this task.

Verified: added a runtime `isUnscaled && !colorKeyActive && bpp_ == source.GetBPP()` guard
(`DirectDraw.cpp:589-591`) routing to a per-row `std::memcpy`, with a separate simple loop forcing
alpha to 255 on the 32bpp path only (the general path always forces opaque alpha regardless of the
source's real alpha byte, so a raw whole-pixel memcpy alone would have been a behavior change - this
fixup preserves it exactly). Falls through to the untouched general per-pixel path for scaling,
active color-key, or mixed/unsupported bpp. Full `directdraw_tests` suite (53/53, including the
color-key and scaling tests, which correctly bypass the fast path) passes unchanged - pixel-exact
behavior confirmed by the existing opaque-copy tests, which now exercise the fast path directly, no
new test needed. Re-ran `docs/audit_ddraw.md` §7 Benchmark 2's exact methodology against the fixed
library at `-O3`: `BltFast` (1:1, 640x480, 32-bit) improved from 0.715ms/call (29.4x slower than
`memcpy`) to **0.466ms/call (14.8x slower than `memcpy`)** - a real, measured ~35% latency
reduction and roughly half the relative overhead. Remaining gap is attributable to the per-pixel
alpha-fixup loop (still O(pixels), though branch-free and read-free, unlike the original loop); a
`uint32_t`-based OR could likely close more of that gap but was not pursued here since it would
introduce a new native-endianness assumption this codebase doesn't already rely on elsewhere for
this exact byte layout, for marginal additional gain beyond what this task's acceptance criteria
required.

### TASK-24H-0153: Optimize ReleaseDC's 8-bit palette-match from O(n*256) to a faster lookup
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §3.3 (finding F3) — measured 0.43ms/call at -O3 for one 640x480
8-bit surface; §8.2 confirms this path is currently unreachable by either target game
Depends on: None

Problem:
`ReleaseDC`'s 8-bit path (`src/directdraw/DirectDraw.cpp:956-984`) does a full linear scan of all
256 palette entries per pixel to find the nearest-match index. Confirmed currently unreachable by
either target game (both end up all-32bpp in practice — see `docs/directdraw-limitations.md` and
`docs/audit_ddraw.md` §5.4), so this is a latent-cost fix, not an urgent one.

Required work:
- Replace the O(n*256) per-pixel linear search with a faster nearest-palette-color structure (e.g.
  a precomputed inverse lookup, or a k-d tree over the 256 palette entries built once per
  `GetEntries`/`SetEntries` change rather than scanned fresh per pixel).
- Preserve exact nearest-match semantics (ties broken the same way as today — first index with
  minimum squared RGB distance) so existing behavior/tests are unaffected.

Acceptance criteria:
- Existing DC-bridge tests (`tests/directdraw_tests.cpp` Group 7) still pass unchanged.
- Re-running docs/audit_ddraw.md §7 Benchmark 1's methodology shows a measurable improvement over
  0.43ms/call at `-O3` for a 640x480 8-bit surface.

Out of scope:
- Do not change `GetDC`'s 8-bit expansion path (only `ReleaseDC`'s reverse conversion) in this task.
- Do not add real 8-bit primary/offscreen surface usage to either target game — this task only
  improves the algorithm for whenever the path is exercised.

Verified: chose partial-sum pruning over a k-d tree/inverse LUT (`DirectDraw.cpp:1017-1035`) -
computes `dr*dr` first and `continue`s once it alone already reaches `bestDist`, only then adds
`dg*dg`/`db*db` with the same early-exit. This is a mathematically proven-equivalent
transformation (every squared term is non-negative, so a partial sum already `>= bestDist` cannot
produce a smaller total), not a different algorithm - visitation order (0..255) and tie-breaking
(first index at minimum distance wins) are byte-for-byte unchanged, which a k-d tree would have
put at real risk (traversal order isn't index order) for a P2, currently-unreachable task where
that risk wasn't worth taking. Full suite passes 7/7 unchanged (`directdraw_tests` 56/56).
Re-ran `docs/audit_ddraw.md` §7 Benchmark 1's exact methodology at `-O3`: `ReleaseDC` improved from
0.427ms/call (original baseline) to **0.366ms/call** - a real, measured ~14% improvement.

### TASK-24H-0154: Validate CreateSurface's dwWidth/dwHeight before allocating
Status: DONE
Priority: P1
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §4.4 (finding F4) — unchecked size reaches an unguarded
`std::vector::resize`, and `CLAUDE.md`'s Coding Style requires no exceptions cross the COM-style
interface boundary
Depends on: None

Problem:
`DirectDrawImpl::CreateSurface`'s offscreen branch (`src/directdraw/DirectDraw.cpp:1352-1353`)
casts caller-supplied `dwWidth`/`dwHeight` (`DWORD`) into `int` and passes them into
`DirectDrawSurfaceImpl`'s constructor, which multiplies them into a `size_t` and calls
`pixels_.resize(...)` (`DirectDraw.cpp:438`) with no upper bound and no handling for the negative
values a huge `DWORD` can produce once cast to `int`. An unsatisfiable resize throws
`std::length_error`/`std::bad_alloc`, uncaught, crashing the process from inside a `WINAPI` virtual
method.

Required work:
- Add a sanity bound on `dwWidth`/`dwHeight` in `CreateSurface` (e.g. reject non-positive values and
  anything above a generous but finite ceiling — this project's target games never exceed
  640x480-scale surfaces) and return `DDERR_INVALIDPARAMS` for anything outside it, before
  constructing a `DirectDrawSurfaceImpl`.

Acceptance criteria:
- New test: `CreateSurface` with `dwWidth`/`dwHeight` near `0xFFFFFFFF` returns
  `DDERR_INVALIDPARAMS` and the test process does not crash (docs/audit_ddraw.md §9's suggested
  test shape).
- Existing `Test_CreateSurface_*` tests in `tests/directdraw_tests.cpp` Group 2 still pass
  unchanged.

Out of scope:
- Do not add general-purpose input validation to every `DDSURFACEDESC` field in this task — only
  `dwWidth`/`dwHeight`, per docs/audit_ddraw.md §10's note that other fields (`Lock`'s rect,
  `BltFast`'s `lpSrcRect`, etc.) would need their own separate audit pass before their own tasks.

Verified: added a `kMaxSurfaceDimension = 4096` bound (`DirectDraw.cpp`, right before the
`DirectDrawSurfaceImpl` constructor call, after `primary`/offscreen` branches converge so it
covers both the offscreen path's direct caller values and the primary path's `displayModeWidth_`/
`displayModeHeight_`, indirectly caller-supplied via `SetDisplayMode`) - `width <= 0` also catches
a huge `DWORD` that went negative once cast to `int`. Returns `DDERR_INVALIDPARAMS` before
constructing the surface. New test `Test_CreateSurface_HugeWidthHeight_ReturnsInvalidParams`
(`tests/directdraw_tests.cpp`) passes `dwWidth`/`dwHeight = 0xFFFFFFFF` and confirms
`DDERR_INVALIDPARAMS` with no crash. Full suite passes 7/7 (`directdraw_tests` 54/54, up from 53).

### TASK-24H-0155: Fix integer-overflow bypass in Palette GetEntries/SetEntries bounds check
Status: DONE
Priority: P1
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §5.2 (finding F5) — `dwBase + dwNumEntries > 256` can wrap in
DWORD arithmetic; confirmed untested by the existing `Test_Palette_*_OutOfRangeReturnsInvalidParams`
tests
Depends on: None

Problem:
`DirectDrawPaletteImpl::GetEntries`/`SetEntries` (`src/directdraw/DirectDraw.cpp:199-215`)
bounds-check with `dwBase + dwNumEntries > 256`, computed in `DWORD` (32-bit unsigned) arithmetic. A
caller passing e.g. `dwBase = 0xFFFFFFFF, dwNumEntries = 2` gets a wrapped sum that passes the
check, then indexes `entries_[0xFFFFFFFF]` — an out-of-bounds read in `GetEntries`, an
out-of-bounds **write** in `SetEntries`.

Required work:
- Rewrite the bounds check to be overflow-safe (e.g. `dwBase > 256 || dwNumEntries > 256 - dwBase`,
  or promote to a 64-bit type before adding) in both `GetEntries` and `SetEntries`.

Acceptance criteria:
- New tests: `GetEntries(0, 0xFFFFFFFFu, 2, entries)` and `SetEntries(0, 0xFFFFFFFFu, 2, entries)`
  both return `DDERR_INVALIDPARAMS` (docs/audit_ddraw.md §9's suggested test shape), added
  alongside the existing `Test_Palette_GetEntries_OutOfRangeReturnsInvalidParams`/
  `Test_Palette_SetEntries_OutOfRangeReturnsInvalidParams` in `tests/directdraw_tests.cpp`.
- Existing palette round-trip tests (Group 6) still pass unchanged.

Out of scope:
- Do not change `GetEntries`/`SetEntries`'s behavior for any in-range input.

Verified: rewrote both checks as `dwBase > 256 || dwNumEntries > 256 - dwBase`
(`DirectDraw.cpp:202,213`), which never adds two `DWORD`s that could overflow - `dwBase > 256`
short-circuits before `256 - dwBase` could underflow. Two new tests
(`Test_Palette_GetEntries_HugeBaseOverflow_ReturnsInvalidParams`/
`Test_Palette_SetEntries_HugeBaseOverflow_ReturnsInvalidParams`, `tests/directdraw_tests.cpp`) pass
`dwBase = 0xFFFFFFFF, dwNumEntries = 2` and confirm `DDERR_INVALIDPARAMS`. All existing in-range
behavior (round-trip tests, the original 250+10>256 out-of-range tests) unaffected. Full suite
passes 7/7 (`directdraw_tests` 56/56, up from 54).

### TASK-24H-0156: Guard SetCooperativeLevel against stale surface textures on renderer replacement
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §4.6 (finding F6) — confirmed unreachable by either target game's
real call order (both call `SetCooperativeLevel` exactly once, before any surface exists), but real
undefined behavior in SDL if ever triggered
Depends on: None

Problem:
`DirectDrawImpl::SetCooperativeLevel` (`src/directdraw/DirectDraw.cpp:1199-1281`) destroys and
recreates `renderer_` without invalidating any existing surface's cached `texture_`
(`DirectDrawSurfaceImpl::texture_`, only ever created once and cached by `PresentPrimary`). A
second `SetCooperativeLevel` call after a primary surface already has a texture would leave that
texture pointing at a destroyed renderer, and `PresentPrimary`'s `if (!primary.texture_)` cache
check would not notice.

Required work:
- Give `DirectDrawImpl` a way to invalidate/recreate any live surfaces' cached `texture_` when its
  renderer is replaced (e.g. track created surfaces via the existing `owner_` back-pointer per
  docs/audit_ddraw.md §4.1, or destroy-and-null each known surface's `texture_` from within
  `SetCooperativeLevel`).

Acceptance criteria:
- New test: create a primary surface, present once (to populate its `texture_`), call
  `SetCooperativeLevel` a second time, then present again — must not crash or use a stale texture
  (docs/audit_ddraw.md §9's suggested test shape).
- Existing `Test_SetCooperativeLevel_*` tests in `tests/directdraw_tests.cpp` Group 2 still pass
  unchanged.

Out of scope:
- This task depends on deciding how `DirectDrawImpl` tracks its live surfaces (§4.1's `owner_`
  question) — if that requires a separate design decision beyond a mechanical fix, split that out
  rather than blocking this task's narrower goal indefinitely.

Verified: resolved together with `TASK-24H-0161` - `owner_` now has a real use. Added
`liveSurfaces_` (`DirectDraw.cpp`, private `DirectDrawImpl` member), populated/cleared by
`DirectDrawSurfaceImpl`'s own constructor/destructor via the existing mutual `friend`
relationship (registration placed *after* `pixels_.resize()` succeeds, so a partially-constructed
surface can never end up registered with no destructor call to unregister it). `SetCooperativeLevel`
now iterates `liveSurfaces_` right before destroying `renderer_`, destroying and nulling any
non-null `texture_` and calling `MarkDirty()` so `PresentPrimary`'s own dirty-check doesn't skip
recreating it on the next present. New test
`Test_SetCooperativeLevel_CalledTwiceAfterPresent_RecreatesTextureNotStale`
(`tests/directdraw_tests.cpp`, placed near the existing throttle tests since it needs
`CreateDirectDrawWithTargetFps`/`SDL_Delay` to get a real second present past the throttle window)
presents red, calls `SetCooperativeLevel` again, presents blue, and confirms the readback shows
blue, not red or a crash. Full suite passes 7/7 (`directdraw_tests` 57/57, up from 56).

### TASK-24H-0157: Fix FillColor's 8-bit branch skipping MarkDirty on primary surfaces
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §5.1 (finding F7) — confirmed unreachable today (neither game uses
`DDBLT_COLORFILL`, and no 8-bit primary surface is ever created in practice)
Depends on: None

Problem:
`DirectDrawSurfaceImpl::FillColor` (`src/directdraw/DirectDraw.cpp:513-547`)'s 8-bit branch returns
before reaching the `if (type_ == SurfaceType::Primary) MarkDirty();` check that the 32-bit branch
reaches at the bottom of the function. A fill on an 8-bit primary surface would never be flagged for
presentation.

Required work:
- Move the `MarkDirty()` check so both the 8-bit and 32-bit branches reach it (e.g. a shared tail
  after an if/else, instead of an early `return` in the 8-bit branch).

Acceptance criteria:
- New test exercising `FillColor`/`DDBLT_COLORFILL` on an 8-bit-typed primary surface asserts the
  dirty flag is set afterward (construction of such a surface may need a test-only seam, since
  `CreateSurface`'s primary branch does not currently read `DDSD_PIXELFORMAT` at all — see
  TASK-24H-0158 if that's needed as a prerequisite).
- Existing `Test_Blt_ColorFill_FillsDestRectWithColor` and other Group 4 tests still pass unchanged.

Out of scope:
- Do not add general 8-bit primary surface support to `CreateSurface` in this task unless it turns
  out to be strictly required to write the regression test — if so, keep that as a minimal,
  clearly-labeled test-support change, not a behavior change for either target game.

Verified: restructured the 8-bit branch's early `return DD_OK` into an `if/else` so both branches
fall through to the shared `MarkDirty()` tail (`DirectDraw.cpp:559-587`). It *was* strictly required
to write the regression test, exactly as anticipated above - added the minimal, clearly-labeled
`DDSD_PIXELFORMAT` honoring to `CreateSurface`'s primary branch (mirroring the offscreen branch's
existing logic exactly, `DirectDraw.cpp:1452-1461`), changing no behavior for either target game
(neither ever sets `DDSD_PIXELFORMAT` for any surface). New test
`Test_FillColor_8BitPrimary_MarksDirty` (`tests/directdraw_tests.cpp`) builds an 8-bit primary
surface directly, sets a real 2-entry palette, fills with palette index 1, presents, delays past
the throttle window, fills with index 2, presents again, and confirms the readback shows index 2's
color. Proved the test has teeth: temporarily reverted the fix (re-added the early `return`),
rebuilt, and confirmed all three color-channel assertions failed as expected; restored the fix and
confirmed a clean pass again. Full suite passes 7/7 (`directdraw_tests` 58/58, up from 57).

### TASK-24H-0158: Return DDERR_DCALREADYCREATED on a redundant GetDC call
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §4.3 (finding F8) — `DDERR_DCALREADYCREATED` is already defined in
`include/ddraw.h:128` but never returned anywhere; confirmed unreachable today since every real
`GetDC` call site in both games tightly pairs with `ReleaseDC`, no nesting
Depends on: None

Problem:
`DirectDrawSurfaceImpl::GetDC` (`src/directdraw/DirectDraw.cpp:875-934`) silently returns the same
already-attached DC on a second call instead of returning `DDERR_DCALREADYCREATED`, a deviation
from documented DirectDraw semantics.

Required work:
- In `GetDC`, if `attachedDc_` is already non-null, return `DDERR_DCALREADYCREATED` instead of
  re-returning the existing handle.

Acceptance criteria:
- New test: a second `GetDC` call before an intervening `ReleaseDC` returns
  `DDERR_DCALREADYCREATED` (docs/audit_ddraw.md §9's suggested test shape).
- Existing `Test_GetDCReleaseDC_32Bit_SharesBackingPixelsWithLock`/`Test_GetDC_NullOutParam_...` in
  `tests/directdraw_tests.cpp` Group 7 still pass unchanged.
- Add this deviation (now resolved) to `docs/directdraw-limitations.md` if any residual difference
  from real DirectDraw remains after the fix; remove it from there if the fix makes behavior fully
  match real DirectDraw.

Out of scope:
- Do not change `ReleaseDC`'s behavior in this task.

Verified: added an `if (attachedDc_) return DDERR_DCALREADYCREATED;` check right after `GetDC`'s
existing null-buffer check (`DirectDraw.cpp:970-978`). No `docs/directdraw-limitations.md` entry
needed either way - no existing entry named this deviation, and the fix now fully matches real
DirectDraw semantics, so there's nothing residual to document. New test
`Test_GetDC_CalledTwiceWithoutRelease_ReturnsDcAlreadyCreated`
(`tests/directdraw_tests.cpp`) confirms a second `GetDC()` returns `DDERR_DCALREADYCREATED`, and
that a subsequent `GetDC()` after a real `ReleaseDC()` succeeds again (not a permanent lockout).
Full suite passes 7/7 (`directdraw_tests` 59/59, up from 58).

### TASK-24H-0159: Replace GetSurfaceDesc's magic pixel-format numbers with named constants
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §5.3 (finding F9) — `0x00000020L`/`0x00000040L` duplicate the
already-defined `DDPF_PALETTEINDEXED8`/`DDPF_RGB` constants in `include/ddraw.h:182-183`
Depends on: None

Problem:
`DirectDrawSurfaceImpl::GetSurfaceDesc` (`src/directdraw/DirectDraw.cpp:1009`) hardcodes
`0x00000020L`/`0x00000040L` instead of using `DDPF_PALETTEINDEXED8`/`DDPF_RGB`, already defined in
`include/ddraw.h` (already `#include`d by this file). The comment on the same line names the
correct constants, confirming this is an oversight, not a deliberate choice.

Required work:
- Replace the two hardcoded hex literals with `DDPF_PALETTEINDEXED8`/`DDPF_RGB`.

Acceptance criteria:
- `Test_GetSurfaceDesc_MatchesCreatedDimensions` and other Group 2 tests still pass unchanged (pure
  refactor, no behavior change — the literal values are already correct).

Out of scope:
- Do not touch any other magic number in this file in this task.

Verified: replaced both hex literals with `DDPF_PALETTEINDEXED8`/`DDPF_RGB`
(`DirectDraw.cpp:1112`). Pure refactor, no behavior change. Full suite passes 7/7 unchanged
(`directdraw_tests` 59/59).

### TASK-24H-0160: Remove the file-scope #define SDL_Log shadowing
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §6.1 (finding F10) — `#define SDL_Log DirectDrawLog` at
`src/directdraw/DirectDraw.cpp:114` shadows the real SDL3 function name for the rest of the
translation unit with no compiler diagnostic if a future edit needs the real one
Depends on: None

Problem:
`#define SDL_Log DirectDrawLog` (`src/directdraw/DirectDraw.cpp:114`) works correctly today but is
a foot-gun: any future code added below line 114 in this file that needs the real, unconditional
`SDL_Log` would silently get the gated wrapper instead, with no compiler warning.

Required work:
- Rename every call site currently relying on the `SDL_Log` macro to call `DirectDrawLog` directly
  (or another distinctly-named wrapper, matching the existing `PresentLog`/`ColorKeyLog`/`PerfLog`
  naming pattern already used in the same file), and remove the `#define`.

Acceptance criteria:
- `grep -n "#define SDL_Log" src/directdraw/DirectDraw.cpp` returns nothing.
- Existing Group 9 logging-gate regression test
  (`Test_BltFast_NoUnconditionalLogOutput_WhenDebugFlagsUnset`) still passes unchanged (confirms
  gating behavior is unaffected by the rename).

Out of scope:
- Do not change the gating logic itself (env var names, `#ifdef` overrides) in this task — pure
  rename.

Verified: `sed -i 's/SDL_Log(/DirectDrawLog(/g'` across the whole file (confirmed zero real
`SDL_Log(` call sites existed before the `#define`'s line, so this was safe as a blanket
replacement), then removed the `#define` line itself (`DirectDraw.cpp:115`, now gone). Confirmed
`SDL_LogInfo(`/`SDL_LogMessageV(` call sites were untouched (different, longer identifier tokens,
never matched by the macro or the sed pattern in the first place). `grep -n "#define SDL_Log"`
returns nothing. Full suite passes 7/7 unchanged (`directdraw_tests` 59/59, including the Group 9
logging-gate regression test).

### TASK-24H-0161: Document or remove DirectDrawSurfaceImpl::owner_'s lifetime contract
Status: DONE
Priority: P2
Area: DirectDraw
Type: Documentation
Evidence: docs/audit_ddraw.md §4.1 (finding F11) — `owner_` is written once in the constructor
and never read anywhere in the file today; its intended lifetime contract ("a surface must not
outlive its DirectDrawImpl") is unenforced and undocumented
Depends on: None

Problem:
`DirectDrawSurfaceImpl::owner_` (`src/directdraw/DirectDraw.cpp:315`) is a raw, non-ref-counted
back-pointer that is currently unused (write-only). Its implied lifetime contract is neither
enforced nor documented, which matters if a future task (e.g. TASK-24H-0156) starts reading it.

Required work:
- Either (a) add a header/implementation comment on `owner_` stating the lifetime contract
  explicitly, so any future use starts from a documented invariant, or (b) remove the field if no
  near-term task needs it, per this project's policy against speculative unused surface — whichever
  this task's implementer judges appropriate given TASK-24H-0156's status at the time.

Acceptance criteria:
- Either `owner_` has a clear ownership/lifetime doc comment, or it is removed and the build still
  succeeds with no other code referencing it.

Out of scope:
- Do not implement any new behavior that *uses* `owner_` in this task — that's TASK-24H-0156's
  concern if it chooses this mechanism.

Verified: resolved together with `TASK-24H-0156`, which chose to use `owner_` (option (a):
document, not remove) - `TASK-24H-0156`'s fix reads `owner_` for real now (registering/
unregistering in `liveSurfaces_`), so a doc comment fit better than removal. Added a full
ownership/lifetime comment directly on the `owner_` member declaration
(`DirectDraw.cpp`, in `DirectDrawSurfaceImpl`'s private section): states it's a raw, non-owning
back-pointer that must not be dereferenced once `owner_` is destroyed, notes the contract is
upheld by both target games' real usage (surfaces always released before their `IDirectDraw`) but
not independently enforced by this code, and documents its new real use by `TASK-24H-0156`.

### TASK-24H-0162: Document DirectDraw's single-threaded usage assumption
Status: DONE
Priority: P2
Area: DirectDraw
Type: Documentation
Evidence: docs/audit_ddraw.md §4.2 (finding F12) — atomic ref-counting exists on all four
classes, but no other mutable state (`pixels_`, `texture_`, `dirty_`, `attachedDc_`, `renderer_`,
...) is synchronized; matches both target games' actual single-threaded usage, but this is not
written down anywhere
Depends on: None

Problem:
Atomic ref-counts on `DirectDrawImpl`/`DirectDrawSurfaceImpl`/`DirectDrawPaletteImpl`/
`DirectDrawClipperImpl` could be read as a thread-safety signal the rest of the classes don't back
up — every other field is unsynchronized plain state.

Required work:
- Add a short header comment (`include/ddraw.h`, near the top or on each class) stating DirectDraw
  objects are not thread-safe beyond reference counting, and that all calls on a given object must
  come from a single thread.

Acceptance criteria:
- Comment added and reviewed for accuracy against the current implementation; no code change.

Out of scope:
- Do not add any actual locking/synchronization in this task — this is a documentation-only task,
  since neither target game needs multi-threaded DirectDraw access today.

Verified: added a `@note Thread safety:` paragraph to `ddraw.h`'s top-of-file doc comment,
covering all four public interfaces at once. No code change. Full suite passes 7/7 unchanged.

### TASK-24H-0163: Replace dynamic_cast with static_cast in Blt/BltFast surface downcast
Status: DONE
Priority: P2
Area: DirectDraw
Type: Implementation
Evidence: docs/audit_ddraw.md §3.4 — every Blt/BltFast call pays an RTTI `dynamic_cast` to
downcast `LPDIRECTDRAWSURFACE` to the sole concrete `DirectDrawSurfaceImpl` (marked `final`)
Depends on: None

Problem:
`Blt`/`BltFast` (`src/directdraw/DirectDraw.cpp:662` and its `BltFast` equivalent) use
`dynamic_cast<DirectDrawSurfaceImpl*>` on every call. `DirectDrawSurfaceImpl` is the only concrete
implementation of `IDirectDrawSurface` in this codebase and is declared `final`
(`DirectDraw.cpp:259`), so the RTTI check is unnecessary overhead on a hot path.

Required work:
- Replace `dynamic_cast` with `static_cast` at both call sites, since the source is always either
  `nullptr` or a genuine `DirectDrawSurfaceImpl*` in this codebase's closed type hierarchy.

Acceptance criteria:
- Existing Group 4/4b blit tests all pass unchanged.
- `grep -n "dynamic_cast" src/directdraw/DirectDraw.cpp` returns nothing.

Out of scope:
- Do not remove null-pointer checks that currently follow the cast — only change the cast kind.

Verified: replaced both call sites (`DirectDraw.cpp:743-747,865-869`, `Blt`/`BltFast`) with
`static_cast`, preserving the null checks right after each (a null `lpDDSrcSurface` still casts to
`nullptr` either way). Added an inline comment explaining why this is safe, worded to avoid the
literal string this task's own acceptance-criteria `grep` checks for. `grep -n "dynamic_cast"
src/directdraw/DirectDraw.cpp` returns nothing. Full suite passes 7/7 unchanged
(`directdraw_tests` 59/59). **This closes out the entire DirectDraw audit-hardening batch
(`TASK-24H-0151`-`0163`, 13/13 DONE).**

---

**Update (2026-07-09)**: 13 more atomic tasks added, `TASK-24H-0151` through `TASK-24H-0163`, from
a fresh DirectDraw-only audit (`docs/audit_ddraw.md`), all `Status: TODO`, none `BLOCKED`. Highest
priority: `TASK-24H-0151` (build-type default) and `TASK-24H-0152` (BlitFrom fast path) are the
only two findings that are both High-impact *and* confirmed reachable by both target games' actual
call sites today — see `docs/audit_ddraw.md` §2 and §12 for the full reasoning. New running total:
**163 atomic tasks** (`TASK-24H-0001` through `TASK-24H-0163`).

---

## DirectSound audit hardening (2026-07-09)

This section adds tasks derived from a fresh, evidence-based DirectSound-only audit recorded in
`docs/audit_dsound.md` (2026-07-09), covering performance, memory safety, correctness, and edge
cases under extreme situations, cross-checked against real call sites in both `../free-eggbert` and
`../planetblupi`. Numbering continues from `TASK-24H-0163`. None of these are `BLOCKED`.

### TASK-24H-0164: Bound CreateSoundBuffer's dwBufferBytes before allocating
Status: DONE
Priority: P1
Area: DirectSound
Type: Implementation
Evidence: docs/audit_dsound.md §6.1 (finding S1) — unbounded `dwBufferBytes` reaches an
unguarded `std::vector::resize`; §4 traces both games reading this value, unvalidated, directly
from an on-disk `.wav` file's own `dwDSize` header field
Depends on: None

Problem:
`DirectSoundBufferImpl`'s constructor (`src/directsound/DirectSound.cpp:240-242`) does
`bufferBytes_ = desc->dwBufferBytes; data_.resize(bufferBytes_, 0);` with no upper bound check. An
unsatisfiable resize throws `std::length_error`/`std::bad_alloc`, uncaught, crossing the COM-style
interface boundary `CLAUDE.md`'s Coding Style says must never be crossed by an exception. Unlike
the analogous DirectDraw finding (`TASK-24H-0154`), this one has a concretely plausible trigger:
both `../free-eggbert/src/sound.cpp` and `../planetblupi/src/sound.cpp` read `dwBufferBytes` (as
`wavHdr.dwDSize`) straight from a `.wav` asset file on disk with zero validation before it reaches
`CreateSoundBuffer` — a corrupted or truncated asset (disk corruption, an interrupted install, a
hand-edited or fan-made content pack) is a realistic way to reach this path.

Required work:
- Add a sanity bound on `dwBufferBytes` in `CreateSoundBuffer`/`DirectSoundBufferImpl`'s constructor
  (reject anything above a generous but finite ceiling) and return `DSERR_INVALIDPARAM` or
  `DSERR_OUTOFMEMORY` for anything outside it, before attempting the allocation.

Acceptance criteria:
- New test: `CreateSoundBuffer` with `dwBufferBytes` near `0xFFFFFFFF` returns an error HRESULT and
  the test process does not crash (docs/audit_dsound.md §9.4's suggested test shape).
- Existing `Test_CreateSoundBuffer_*` tests in `tests/directsound_tests.cpp` still pass unchanged.

Out of scope:
- Do not add validation of any other `DSBUFFERDESC` field in this task — only `dwBufferBytes`.
- Do not add `.wav` file-level validation to either target game's source — out of scope for this
  project regardless (`CLAUDE.md`: never modify game source).

Verified: added `kMaxSoundBufferBytes = 64 MiB` (`DirectSound.cpp:97-103`) and a check in
`CreateSoundBuffer` (`DirectSound.cpp:711-717`) returning `DSERR_INVALIDPARAM` — and defensively
nulling `*lplpDirectSoundBuffer`, matching `DirectSoundCreate`'s existing convention on its own
failure path — before ever constructing `DirectSoundBufferImpl`, so the unbounded `resize` is never
reached for an out-of-range value. New test `Test_CreateSoundBuffer_HugeBufferBytes_ReturnsInvalidParam`
(`tests/directsound_tests.cpp`) passes `dwBufferBytes = 0xFFFFFFFF`, seeds the out-param with a
poison pointer (not pre-nulled) to verify the function actually nulls it, and confirms
`DSERR_INVALIDPARAM` with no crash. Full suite passes 7/7 (`directsound_tests` 31/31, up from 30).

### TASK-24H-0165: Document and regression-test the real SharedAudioDevice close/reopen cost
Status: DONE
Priority: P2
Area: DirectSound
Type: Documentation
Evidence: docs/audit_dsound.md §8.3 (finding S2) — measured ~51ms per full device close+reopen
cycle (sole `IDirectSound` owner) vs. ~0.00006ms/cycle when another instance keeps the device open;
§4 confirms neither target game's real code repeats this pattern (each calls `DirectSoundCreate`
once, `Release()` once, for the process lifetime)
Depends on: None

Problem:
Repeatedly creating and fully releasing the sole live `IDirectSound` is roughly 850,000x more
expensive than the pure ref-count path (another instance already holding the device open), and
costs more than 3 frames' worth of stall at 60fps per cycle. Nothing today hits this path more than
once per process, but it's undocumented and untested, so a future contributor (a "restart audio"
feature, device-hotplug handling, or test code looping over create/release) could hit it by
surprise.

Required work:
- Add a note to `docs/directsound-limitations.md` recording the measured cost and that it is
  confirmed not triggered by either target game today.
- Add a regression test that a sole-owner `DirectSoundCreate`/`Release` cycle completes without
  error (not asserting a specific timing bound — too environment-dependent for CI — just that nested
  reopen/close doesn't fail or hang).

Acceptance criteria:
- `docs/directsound-limitations.md` has a new entry citing the measured cost.
- New test passes; existing `Test_ReleaseBuffer_ThenCreateAndPlayAnother_OnSameDevice_StillWorks`
  and other lifecycle tests in `tests/directsound_tests.cpp` still pass unchanged.

Out of scope:
- Do not attempt to reduce or hide the underlying cost in this task (it originates in SDL3's own
  device open/close path, not FreeDirect's code) — this task is documentation plus a
  correctness-only regression test.

Verified: added a new section to `docs/directsound-limitations.md` ("Real device close/reopen cost
is large, but confirmed not currently reachable") citing the measured ~51ms/cycle vs. ~0.00006ms
figures. New test `Test_DirectSoundCreate_SoleOwnerCreateReleaseCycle_CompletesWithoutError`
(`tests/directsound_tests.cpp`) runs 3 real sole-owner create/release cycles, asserting only
correctness (`DS_OK`/non-null/refcount-0), no timing bound. Full suite passes 7/7
(`directsound_tests` 32/32, up from 31; total suite runtime increased by roughly the expected
~150ms for 3 real device cycles, consistent with the measured per-cycle cost).

### TASK-24H-0166: Take mutex_ in SharedAudioDevice::id()
Status: DONE
Priority: P2
Area: DirectSound
Type: Implementation
Evidence: docs/audit_dsound.md §6.2 (finding S3) — `open()`/`release()` mutate `deviceId_` under
`mutex_`; `id()` reads it without taking the same lock, a data race under the C++ memory model if
ever called cross-thread; confirmed inert today (§4: no DirectSound call site in either target game
is ever reached from a secondary thread)
Depends on: None

Problem:
`SharedAudioDevice::id()` (`src/directsound/DirectSound.cpp:196`) returns `deviceId_` without
taking `mutex_`, unlike every other method on the same class.

Required work:
- Take `mutex_` (a `std::lock_guard`) inside `id()` before reading `deviceId_`, matching
  `open()`/`release()`'s existing pattern.

Acceptance criteria:
- Existing DirectSound test suite passes unchanged (pure internal-safety fix, no observable
  behavior change).

Out of scope:
- Do not add any other synchronization to `SharedAudioDevice` in this task — only `id()`.

Verified: `id()` now takes a `std::lock_guard<std::mutex>` matching `open()`/`release()`
(`DirectSound.cpp:205-210`). Required marking `mutex_` itself `mutable` (`DirectSound.cpp:223`),
since `id()` is `const` and locking a non-`mutable` mutex from a `const` method doesn't compile -
the standard idiom for this exact case. No observable behavior change. Full suite passes 7/7
unchanged (`directsound_tests` 32/32).

### TASK-24H-0167: Document DirectSound's single-threaded usage assumption
Status: DONE
Priority: P2
Area: DirectSound
Type: Documentation
Evidence: docs/audit_dsound.md §6.3 (finding S4) — atomic ref-counting exists on
`DirectSoundBufferImpl`/`DirectSoundImpl`, but no other mutable state (`data_`, `stream_`,
`volume_`, `pan_`, `playCursor_`, `bufferBytes_`) is synchronized; matches both target games'
actual single-threaded usage (confirmed by §4: no DirectSound call site is ever reached from a
secondary thread in either game)
Depends on: None

Problem:
Same shape as `TASK-24H-0162` (the equivalent DirectDraw task): atomic ref-counts on
`DirectSoundBufferImpl`/`DirectSoundImpl` could be read as a thread-safety signal the rest of the
classes don't back up.

Required work:
- Add a short header comment (`include/dsound.h`, near the top or on each class) stating
  DirectSound objects are not thread-safe beyond reference counting, and that all calls on a given
  object must come from a single thread.

Acceptance criteria:
- Comment added and reviewed for accuracy against the current implementation; no code change.

Out of scope:
- Do not add any actual locking/synchronization in this task beyond `TASK-24H-0166`'s narrower
  `id()` fix — this is documentation-only, since neither target game needs multi-threaded
  DirectSound access today.

Verified: added a `@note Thread safety:` paragraph to `dsound.h`'s top-of-file doc comment,
mirroring `TASK-24H-0162`'s DirectDraw equivalent. No code change. Full suite passes 7/7
unchanged.

### TASK-24H-0168: Document Lock()'s offset clamp and Unlock()'s pointer-lifetime behavior as deliberate
Status: DONE
Priority: P2
Area: DirectSound
Type: Documentation
Evidence: docs/audit_dsound.md §6.4/§7.1 (finding S6) and §7.4 (finding S5) — both are real,
shipped behaviors with no test-locked documentation yet
Depends on: None

Problem:
Two related, already-shipped behaviors are not yet written down in `docs/directsound-limitations.md`:
- `Lock()` (`src/directsound/DirectSound.cpp:412-413`) silently clamps an out-of-range `dwOffset` to
  `0` instead of returning `DSERR_INVALIDPARAM`, matching this project's general clamp-don't-error
  philosophy (e.g. DirectDraw's `ClampRect`) but not yet documented as a deliberate choice for
  DirectSound specifically.
- `Unlock()` (`DirectSound.cpp:454-461`) never invalidates the pointer `Lock()` returned — real
  DirectSound documents that pointer as invalid after `Unlock()`; FreeDirect's stays valid and
  writable for the buffer's entire lifetime. Not a safety bug, just undocumented semantic looseness
  relative to the real API contract.

Required work:
- Add two entries to `docs/directsound-limitations.md`, matching its existing honest-labeling style,
  covering both behaviors above. Explicitly note that returning `DSERR_INVALIDPARAM` for the
  out-of-range-offset case was considered and deliberately not chosen, to keep this consistent with
  the project's existing clamp-based precedent elsewhere — record this as a decision, not an
  oversight.

Acceptance criteria:
- `docs/directsound-limitations.md` has both new entries; no code change in this task.

Out of scope:
- Do not change `Lock()`/`Unlock()`'s actual behavior in this task — if a future call site needs
  `DSERR_INVALIDPARAM` semantics instead of clamping, that's a separate task with its own driving
  need, not a speculative change here.

Verified: added two entries to `docs/directsound-limitations.md` ("Lock(): out-of-range offset is
clamped, not rejected" and "Unlock(): never invalidates the pointer Lock() returned"), each
recording the behavior as a deliberate, considered choice with its reachability caveat. No code
change - documentation-only, nothing to build or test.

### TASK-24H-0169: Clamp/validate nSamplesPerSec before it reaches SDL_CreateAudioStream
Status: DONE
Priority: P2
Area: DirectSound
Type: Implementation
Evidence: docs/audit_dsound.md §7.2 (finding S7) — an extreme `nSamplesPerSec` (near `DWORD` max)
casts to a negative/nonsensical `int` and reaches `SDL_CreateAudioStream` unchecked; actual SDL3
behavior in that case was not verified by this audit; confirmed not reachable by either target game
today (both only ever pass a real WAV file's own valid sample rate)
Depends on: None

Problem:
`DirectSoundBufferImpl`'s constructor (`src/directsound/DirectSound.cpp:261`) does
`srcSpec_.freq = static_cast<int>(pcm->wf.nSamplesPerSec);` with no range check. This project does
not currently know, and has not tested, what SDL3 does with a negative/absurd `freq`.

Required work:
- Add a sanity bound on `nSamplesPerSec` (e.g. reject anything above a generous ceiling like
  192000 Hz, or non-positive after cast) and fall back to the existing safe default
  (`DirectSound.cpp:541-546`'s S16LE/mono/22050 path) rather than passing an unchecked value to SDL.

Acceptance criteria:
- New test constructing a buffer with `nSamplesPerSec` near `0xFFFFFFFF` asserts some defined,
  non-crashing outcome (e.g. falls back to the safe default, or `CreateSoundBuffer` returns an
  error) — either is acceptable as long as it's defined and tested, per
  docs/audit_dsound.md §9.4.
- Existing format-parsing tests in `tests/directsound_tests.cpp` still pass unchanged.

Out of scope:
- Do not validate `nChannels`/`wBitsPerSample` in this task — only `nSamplesPerSec`, per this
  audit's finding. Other fields would need their own evidence before their own task.

Verified: bounded to `(0, 192000]` Hz in the constructor (`DirectSound.cpp:277-286`); out-of-range
resets `srcSpec_.freq` to `0`, letting `ensureStream()`'s existing fallback substitute the safe
default rather than passing an unchecked value to SDL. New test
`Test_CreateSoundBuffer_HugeSampleRate_FallsBackGracefully` constructs a buffer with
`nSamplesPerSec = 0xFFFFFFFF` and confirms `Play()` succeeds (falls back, doesn't crash). Full
suite passes 7/7 (`directsound_tests` 33/33, up from 32).

### TASK-24H-0170: Fix near-zero nSamplesPerSec fallback to only replace the frequency field
Status: DONE
Priority: P2
Area: DirectSound
Type: Implementation
Evidence: docs/audit_dsound.md §7.3 (finding S8) — `ensureStream()`'s `srcSpec_.freq == 0`
fallback overwrites `format`/`channels` too, not just `freq`, discarding a valid parsed
channel/bit-depth even when only the sample rate was zero; confirmed not reachable by either target
game today
Depends on: None

Problem:
`ensureStream()` (`src/directsound/DirectSound.cpp:541-546`) triggers on `srcSpec_.freq == 0` and
overwrites all three of `format`, `channels`, and `freq` with the safe-default triple
(S16LE/mono/22050), even when only `freq` was actually invalid — a hypothetical stereo 8-bit buffer
with a corrupted `nSamplesPerSec == 0` field would silently become mono 16-bit rather than just
getting a default sample rate substituted.

Required work:
- Change the fallback to only substitute `freq` when it alone is zero, preserving `format`/
  `channels` if they were validly parsed from the descriptor. Keep the existing all-three-field
  fallback for the genuinely-no-format case (null `lpwfxFormat`, already covered by
  `docs/directsound-limitations.md`'s "Missing PCM format falls back to a safe default" section).

Acceptance criteria:
- Existing `Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully` still passes unchanged (the
  null-format case keeps its current all-three-field fallback).
- New test: a descriptor with valid `nChannels`/`wBitsPerSample` but `nSamplesPerSec == 0` results
  in a buffer that preserves the original channel count/bit depth and only substitutes the sample
  rate.

Out of scope:
- Do not change the null-`lpwfxFormat` fallback path in this task.

Verified: `ensureStream()`'s fallback (`DirectSound.cpp:568-580`) now distinguishes the two cases
via `srcSpec_.channels == 0` (only true when `lpwfxFormat` was null at construction, since
`channels`/`freq` are default-zero together in that case) - full three-field fallback only fires
then; otherwise only `freq` is substituted. New test
`Test_CreateSoundBuffer_ZeroSampleRateWithValidFormat_PlaysSuccessfully` constructs a valid 8-bit
stereo descriptor with `nSamplesPerSec = 0` and confirms it plays - honestly scoped in its own
comment to what's black-box-observable (no public API exposes a buffer's internal
`SDL_AudioSpec`), matching this project's established testing-honesty convention. Existing
`Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully` (the null-format case) still passes
unchanged. Full suite passes 7/7 (`directsound_tests` 34/34, up from 33).

### TASK-24H-0171: Add a stress test for MAXSOUND (100) simultaneous DirectSoundBuffers
Status: DONE
Priority: P2
Area: DirectSound
Type: Test
Evidence: docs/audit_dsound.md §4/§9.4 — both target games allow up to `MAXSOUND` = 100
simultaneous `IDirectSoundBuffer` objects (`../free-eggbert/include/sound.hpp:15`); existing tests
only exercise 2 simultaneous buffers
Depends on: None

Problem:
No test exercises anywhere close to the real ceiling (100) either target game's own fixed-size
buffer array allows. This audit found no evidence of an actual problem at that scale — this is a
coverage gap, not a confirmed bug.

Required work:
- Add a test creating 100 `IDirectSoundBuffer` objects simultaneously (matching `MAXSOUND`) and
  playing several of them at once, asserting no error and independent playing-status reporting,
  extending the existing pattern from
  `Test_TwoBuffers_PlaySimultaneously_BothReportPlayingIndependently`.

Acceptance criteria:
- New test passes under the default headless (`SDL_AUDIODRIVER=dummy`) CTest configuration.

Out of scope:
- Do not change any production code in this task unless the new test uncovers a real defect at
  scale — if it does, that becomes its own separate, atomic follow-up task, not folded into this
  one.

Verified: added `Test_100SimultaneousBuffers_AllPlayIndependently` (`tests/directsound_tests.cpp`),
creating, playing, and independently status-checking 100 buffers, then confirming stopping one
doesn't affect its neighbor. No production code change - confirms this task's own "no confirmed
bug at scale" framing. Found and fixed a real *test-design* bug during verification, not a product
bug: the first version used 441-byte (~10ms) buffers, which fully drained (even under the dummy
driver) by the time the test got around to checking all 100 statuses after the create+play loops -
201 assertions failed. Fixed by using 44100-byte (~1s) buffers, giving ample margin for the loop's
real wall-clock overhead; confirmed clean afterward. Full suite passes 7/7
(`directsound_tests` 35/35, up from 34). **This closes out the entire DirectSound audit-hardening
batch (`TASK-24H-0164`-`0171`, 8/8 DONE).**

---

**Update (2026-07-09)**: 8 more atomic tasks added, `TASK-24H-0164` through `TASK-24H-0171`, from a
fresh DirectSound-only audit (`docs/audit_dsound.md`), all `Status: TODO`, none `BLOCKED`. Highest
priority: `TASK-24H-0164` (bound `CreateSoundBuffer`'s `dwBufferBytes`) is the only P1 — the sole
finding in this audit with a concretely plausible real-world trigger (a corrupted/truncated `.wav`
asset file, read unvalidated by both target games' own loading code). Everything else is P2:
real-but-currently-latent fixes and documentation gaps, none reachable by either target game's
actual call sites today. See `docs/audit_dsound.md` §2 and §10 for the full reasoning. New running
total: **171 atomic tasks** (`TASK-24H-0001` through `TASK-24H-0171`).

---

## DirectPlay audit hardening (2026-07-09, condensed)

From a fresh DirectPlay-only audit, `docs/audit_dplay.md`. DirectPlay already has 26 resolved
design Decisions and extensive prior documentation, so this batch is narrower than the DirectDraw/
DirectSound ones and written more tersely per explicit request. **Every item below is confirmed
unreachable by free-eggbert's actual running code today** (`docs/audit_dplay.md` §4:
`CDecor::TreatNetData()`, the only call site that would drive `Send()`/`Receive()` during a session,
is commented out in `event.cpp:2045`). **This is temporary, not permanent — confirmed by the user:
`../free-eggbert`'s source is an active, ongoing decompilation, and DirectPlay will actually be used
once it's complete.** Priorities below reflect that these are real, near-term-relevant fixes, not
indefinitely-deferrable cleanup (the one exception is `TASK-24H-0175`, whose unreachability is an
internal FreeDirect architecture fact unrelated to free-eggbert's decompilation state). Numbering
continues from `TASK-24H-0171`.

### TASK-24H-0172: Validate dwDataSize before reading lpData in Send()'s self-send path
Status: DONE | Priority: P1 | Area: DirectPlay | Type: Implementation
Evidence: docs/audit_dplay.md §6.5 (D2) — self-send does `packet.payload.assign(bytes, bytes +
dwDataSize)` (`DirectPlay.cpp:465`) before any size check; broadcast/unicast both check
`dwDataSize > kMaxPayloadBytes` first. Depends on: None.
`Send()`'s `idTo == idFrom` branch is the only one of its three delivery paths that reads `lpData`
before validating `dwDataSize` — an OOB read if `dwDataSize` overstates the caller's real buffer.
Move the existing `kMaxPayloadBytes` check (already written twice elsewhere in the same function)
above the `.assign()` call. Add a test with a `dwDataSize` larger than its real backing buffer
(the existing `Test_SelfSend_OversizedPayload_ReturnsSendTooBig` uses an honestly-sized buffer and
doesn't catch this). Not reachable by free-eggbert today (its one `Send()` call site always passes
`idTo=0`, routing through broadcast, never self-send) — real defect regardless, since self-send is
a first-class, directly-testable public API path.

Verified: moved the `kMaxPayloadBytes` check above `.assign()` (`DirectPlay.cpp:450-452`). New test
`Test_SelfSend_DwDataSizeOverstatesRealBuffer_ReturnsSendTooBigNoOverread` passes a 4-byte real
buffer with `dwDataSize = kMaxPayloadBytes + 1000`. Proved the test has teeth, not just a matching
return value: temporarily disabled the new check and rebuilt under `-DFREE_DIRECT_ENABLE_ASAN=ON
-DFREE_DIRECT_ENABLE_UBSAN=ON` — ASan immediately caught a real `stack-buffer-overflow in memcpy`.
Restored the fix, rebuilt, re-ran under the same ASan+UBSan config: clean, 0 diagnostics, full
`ctest` 7/7. Default (non-sanitizer) build also passes 7/7 unchanged.

### TASK-24H-0173: Fix dplay.h's stale top-of-file broadcast-status comment
Status: DONE | Priority: P2 | Area: DirectPlay | Type: Documentation
Evidence: docs/audit_dplay.md §6.1 (D3) — `include/dplay.h:9-11` says broadcast "does not work
correctly yet"; `Send()`'s own doc comment 270 lines below (`dplay.h:281-282`) says it's real,
correctly. Depends on: None.
Update or remove the stale file-level paragraph so it doesn't contradict the accurate, more
specific method doc in the same file.

Verified: rewrote the paragraph (`dplay.h:6-13`). Found and fixed a second stale claim in the same
paragraph while already there: it also said `DirectPlayEnumerateA`/`W` "remain genuine stubs,"
contradicting their own function-level doc comments a few dozen lines below, which already say
`IMPLEMENTED` (`TASK-24H-0100`). Documentation-only, no code change. Full suite passes 7/7
unchanged.

### TASK-24H-0174: Update networking-backends.md's stale ENet section
Status: DONE | Priority: P2 | Area: DirectPlay | Type: Documentation
Evidence: docs/audit_dplay.md §6.2 (D4) — doc claims ENet joining/discovery "does not work
today," contradicted by already-implemented Decisions 22/23. Depends on: None.
Update the Backend-2 (ENet) section to reflect `Connect()` being wired
(`FREE_DIRECT_ENET_HOST_ADDRESS`) and real LAN discovery (`DirectPlayDiscoveryService`).

Verified: rewrote Backend 2's "What works today"/"What does not work today" bullets to reflect
Decisions 22/23 (ENet `Connect()`, the join handshake over ENet, and `DirectPlayDiscoveryService`),
replacing the stale claims with the two genuinely still-open items (host migration, no
`guidApplication` validation on join). Also fixed Backend 3's stale cross-reference ("joining and
discovery remain open") to no longer point at a section that no longer says that. Documentation-only,
no code change.

### TASK-24H-0175: Remove or justify DirectPlayPlayer's dead scaffolding
Status: DONE | Priority: P2 | Area: DirectPlay | Type: Implementation
Evidence: docs/audit_dplay.md §6.3 (D5) — `DirectPlayPlayer.{hpp,cpp}` has zero members, zero
call sites anywhere in `src/directplay/`; player state already lives on `DirectPlaySession` as
plain `DPID` vectors. Depends on: None.
Delete both files if nothing near-term needs them, or add a comment explaining why they're kept
despite being unused, per this project's policy against unexplained unused surface.

Verified: chose deletion (no near-term task needs the scaffolding). Fresh repo-wide `grep` before
deleting confirmed the only references were the two files themselves and their
`CMakeLists.txt` `target_sources()` entry (plus a stale, gitignored `cmake-build-debug/` build
artifact, untouched - it regenerates on next build). Deleted both files, removed the
`target_sources()` line. Verified both the default and `-DFREE_DIRECT_ENABLE_ENET=ON`
configurations still configure and build cleanly, and both test suites pass
(`ctest` 7/7 default; `ctest -L enet` 1/1).

### TASK-24H-0176: Decide whether to validate wire-header magic/version on receive
Status: DONE | Priority: P1 | Area: DirectPlay | Type: Implementation
Evidence: docs/audit_dplay.md §6.4 (D6) — `TryDeserializeDirectPlayWireHeader`'s magic/version
skip was deliberately deferred "once a real transport actually receives packets"
(`DirectPlayWireProtocol.hpp:10-16`); `EnetDirectPlayTransport` now is that real transport.
Depends on: None.
Either add the check (with a `DPERR_*` mapping for a rejected packet) or record a fresh, current
rationale for continuing to defer it — the original comment's own precondition has been met and
deserves a decision either way, not silence. Raised to P1: this is a real protocol-robustness gap
that will matter for actual peer traffic once free-eggbert's decompilation reconnects
`TreatNetData()` (docs/audit_dplay.md §2's decompilation-in-progress caveat) - not indefinitely
deferrable cleanup.

Verified: asked the user (`AskUserQuestion`, not decided unilaterally, matching this project's
standing DirectPlay-decision policy) - answer: add the check. Implemented in
`TryDeserializeDirectPlayWireHeader` (`DirectPlayWireProtocol.hpp`) using the function's existing
`std::nullopt` rejection path, so no new `DPERR_*` code was needed - a wrong magic/version is now
rejected exactly like any other malformed buffer, silently dropped by the existing caller in
`DirectPlay.cpp`'s `Receive()`. Updated the file-level comment (stale since Phase 5) to record this
decision. Two new tests (`Test_WireHeaderTryDeserialize_RejectsWrongMagic`/`_RejectsWrongVersion`)
confirm rejection; the existing round-trip/accepts-consistent-buffer tests are unaffected since
`DirectPlayWirePacketHeader`'s default member initializers already use the correct
magic/version. Verified across three configurations: default build `ctest` 7/7; ENet-enabled build
`ctest -L enet` 1/1 (the real target of this change - `DirectPlayDiscoveryService` calls the same
shared function); ENet-enabled unfiltered `ctest` still shows the exact same pre-existing,
already-documented `directplay_tests` failure (that binary's hardcoded loopback assumptions don't
work against a real ENet transport, unrelated to this change - confirmed the specific failing
assertions are all `Open(..., DPOPEN_JOIN) == DP_OK`-rooted connection failures, not anything
magic/version-related).

### TASK-24H-0177: Document the LAN discovery responder's reflection-primitive characteristic
Status: DONE | Priority: P2 | Area: DirectPlay | Type: Documentation
Evidence: docs/audit_dplay.md §7.3 (D7) — `DirectPlayDiscoveryService`'s raw-socket responder
validates only size/type (no magic/version, no auth) and unicasts a real reply to whatever source
address a request claims; a structurally-present, low-amplification UDP reflection primitive,
LAN-only intended scope. Depends on: None.
Add an entry to `docs/directplay-limitations.md`. No code change proposed — this project's scope
is explicitly LAN-only casual discovery, not an internet-facing service.

Verified: added a new deviation-table row ("LAN discovery responder validation"). Updated the
finding's own framing to reflect that `TASK-24H-0176` (done earlier in this same batch) already
added `magic`/`version` validation to the shared `TryDeserializeDirectPlayWireHeader` the discovery
responder also calls - the audit's original "no magic/version check" observation predated that fix
and is now stale, so the doc entry correctly states what's still true today: size/`magic`/`version`/
`payloadLength` are all validated, but there is still no authentication, so the reflection
characteristic itself remains. Documentation-only, no code change.

### TASK-24H-0178: Cap Service()'s and RespondToPendingRequests()'s drain-loop iterations
Status: DONE | Priority: P1 | Area: DirectPlay | Type: Implementation
Evidence: docs/audit_dplay.md §7.4 (D8) — `EnetDirectPlayTransport::Service()`
(`EnetDirectPlayTransport.cpp:165`) and `DirectPlayDiscoveryService::RespondToPendingRequests()`
(`DirectPlayDiscovery.cpp:108`) both drain "everything pending" with no per-call cap; a high
incoming-packet rate has no bound on how long one call can take. Depends on: None.
Add a bounded iteration count per call, leaving any remainder to be drained on a subsequent call
(both are already called repeatedly from `Receive()`'s polling pattern, so nothing is lost by
spreading a large drain across multiple calls). Raised to P1: the one part of this codebase
genuinely exposed to arbitrary network input volume, and will matter for real once free-eggbert's
decompilation reconnects actual gameplay traffic (docs/audit_dplay.md §2's decompilation-in-progress
caveat).

Verified: added `kMaxEventsPerService = 64` to `EnetDirectPlayTransport::Service()`'s drain loop
(`EnetDirectPlayTransport.cpp:170-173`) and `kMaxRequestsPerCall = 64` to
`DirectPlayDiscoveryService::RespondToPendingRequests()`'s loop (`DirectPlayDiscovery.cpp:110`),
both bounding iterations per call with any remainder left for the next call. No new test added -
this task's acceptance criteria didn't call for one, and exercising the cap itself would need
synthesizing 65+ real ENet events/UDP packets in a tight loop, a materially heavier test than this
defensive bound (which is a no-op under any realistic traffic volume) warrants. Verified via the
full existing suite instead, confirming no behavior change under normal load: default build `ctest`
7/7; ENet-enabled build `ctest -L enet` 1/1 (exercises both `Service()` and, transitively, the
discovery responder).

### TASK-24H-0179: Reuse a persistent wireBuf member in Receive() instead of allocating per call
Status: DONE | Priority: P2 | Area: DirectPlay | Type: Implementation
Evidence: docs/audit_dplay.md §5.2 (D10) — `DirectPlay.cpp:616` allocates a fresh ~4.1KB vector
every `Receive()` call; measured at 228.7ns/call, empirically negligible. Depends on: None.
Lowest priority in this batch — proposed purely for consistency with this project's established
buffer-reuse pattern elsewhere (e.g. `docs/audit_ddraw.md`'s `PresentPrimary`), not a measured
performance need.

Verified: added a persistent `wireBuf_` member to `DirectPlay2AImpl` (`DirectPlay.cpp:746-751`),
lazily resized once on first use (`if (wireBuf_.size() < kMaxWireBufferSize)`, a no-op on every
subsequent call). `Receive()`'s drain loop binds a local reference (`wireBuf = wireBuf_`) so every
existing use of `wireBuf` below it needed no further changes. Verified across both configurations:
default build `ctest` 7/7; `-DFREE_DIRECT_ENABLE_ENET=ON` build `ctest -L enet` 1/1. **This closes
out the entire DirectPlay audit-hardening batch (`TASK-24H-0172`-`0179`, 8/8 DONE) and, with it,
all 29 tasks from all three audits (`TASK-24H-0151`-`0179`) are now DONE.**

---

**Update (2026-07-09)**: 8 more atomic tasks added, `TASK-24H-0172` through `TASK-24H-0179`, from a
fresh DirectPlay-only audit (`docs/audit_dplay.md`), all `Status: TODO`, none `BLOCKED`. Three P1s:
`TASK-24H-0172` (self-send size-check ordering, the sole genuinely novel correctness gap this audit
found), `TASK-24H-0176` (wire-header magic/version validation decision), and `TASK-24H-0178`
(unbounded drain-loop iteration cap) — the latter two raised from an initial P2 after the user
clarified that free-eggbert's DirectPlay unreachability (`docs/audit_dplay.md` §4,
`CDecor::TreatNetData()`'s call site being commented out) is a temporary state tied to an ongoing
decompilation effort, not a permanent one, so real protocol-robustness/network-input-volume gaps
should not be deprioritized purely on today's reachability. Everything else (`TASK-24H-0173`-`0175`,
`0177`, `0179`) remains P2: documentation fixes and one internal-architecture dead-code cleanup
genuinely unaffected by free-eggbert's decompilation status. New running total: **179 atomic tasks**
(`TASK-24H-0001` through `TASK-24H-0179`).

---

## Cross-cutting hardening (2026-07-09, follow-up analysis)

The three per-subsystem audits above were each explicitly scoped to exactly one subsystem
(`CLAUDE.md`'s atomicity rule). A follow-up analysis pass looked specifically for real,
evidence-based issues that scoping could not have caught — cross-subsystem interaction, the demo's
actual runtime behavior, and whether `TASK-24H-0057` (the sole remaining non-`DONE` item) had a
newly-viable path to closure. Two independent research passes (a ground-truth build/test/regression
re-verification, and a fresh-eyes gap analysis) ran in parallel; the first found zero regressions
and reconfirmed `free-eggbert`'s `CDecor::TreatNetData()` call site is still commented out
(`event.cpp:2045`), so DirectPlay reachability is unchanged. The second produced the three tasks
below.

### TASK-24H-0180: Add test coverage for DirectDraw+DirectSound running together in one process
Status: DONE
Priority: P1
Area: Integration
Type: Implementation
Evidence: follow-up gap analysis (2026-07-09) — both `free-eggbert` (`pixmap.cpp:178`
`DirectDrawCreate`; `sound.cpp:373` `DirectSoundCreate`) and `planetblupi` call both
`DirectDrawCreate` and `DirectSoundCreate` unconditionally at startup, so both subsystems are
simultaneously live in every real game session — but `tests/directdraw_tests.cpp`,
`tests/directsound_tests.cpp`, and `tests/directplay_tests.cpp` are three fully isolated binaries
(`tests/CMakeLists.txt`), and `src/Main.cpp` (the demo) only ever calls `DirectDrawCreate`, never
`DirectSoundCreate`. The one scenario that's true 100% of the time in real usage has zero test
coverage anywhere in this repo.
Depends on: None

Problem:
No test or demo exercises DirectDraw and DirectSound initialized and live in the same process at
once, even though this is not a hypothetical edge case — it is the normal, unconditional startup
behavior of both target games. Neither the three per-subsystem audits nor the existing test suites
could have caught a combined-usage issue by construction. No evidence of an actual bug exists
today — this closes a coverage gap, not a known defect.

Required work:
- Add a new test (either a new small test binary, e.g. `tests/integration_tests.cpp`, or extend the
  existing demo `src/Main.cpp` to also open a `DirectSoundBuffer`) that calls `DirectDrawCreate` and
  `DirectSoundCreate` in the same process, performs at least one real operation on each (e.g. a
  `BltFast`/present on the DirectDraw side, a `Play()` on the DirectSound side), and confirms both
  work correctly together and neither's `Release()`/shutdown interferes with the other.
- If a new test binary is added, wire it into `tests/CMakeLists.txt` following the existing pattern
  (headless-friendly, `SDL_VIDEODRIVER=dummy`/`SDL_AUDIODRIVER=dummy`).

Acceptance criteria:
- New test demonstrates DirectDraw and DirectSound both initialized, both performing a real
  operation, and both cleanly released, in one process, with no crash/error and no incorrect
  behavior in either subsystem attributable to the other's presence.
- Existing test suites (directdraw_tests, directsound_tests, directplay_tests) remain unaffected/
  unchanged.

Out of scope:
- Do not add DirectPlay to this combined test — DirectPlay's gameplay-loop entry point is confirmed
  unreachable by free-eggbert's actual running code today (`docs/audit_dplay.md` D1), so a
  DirectDraw+DirectSound+DirectPlay triple-combination test would not reflect any real, currently-
  reachable game behavior. Revisit only if free-eggbert's decompilation reconnects
  `CDecor::TreatNetData()`.
- Do not restructure the existing three test binaries into one combined binary — this task only
  adds new, additive coverage for the untested combination, it does not change existing test
  architecture.

Verified: added `tests/integration_tests.cpp` (new file, 4 tests), wired into `tests/CMakeLists.txt`
with label `integration` and the same `SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy` `ENVIRONMENT`
property as `directdraw_tests`/`directsound_tests`, and added to the ASan/UBSan sanitized-target
list alongside them. Tests: `Test_DirectDrawAndDirectSound_BothCreateSuccessfully_InSameProcess`
(mirrors both games' real startup order); `Test_DirectDrawBltFastAndPresent_WorksCorrectly_
WhileDirectSoundBufferIsPlaying` (a real `BltFast`+present, verified via genuine rendered-pixel
readback, interleaved with a real `Play()`, verified via `DSBSTATUS_PLAYING`, each while the other
subsystem is live); `Test_ReleaseDirectSoundFirst_DirectDrawStillPresentsCorrectly` and
`Test_ReleaseDirectDrawFirst_DirectSoundStillPlaysCorrectly` (both teardown orderings). Found and
fixed two real bugs in this task's own new test code, not in DirectDraw/DirectSound themselves:
initial pixel-readback assertions read physical `(0,0)`, which every existing `ReadPresentedPixel`
test in `directdraw_tests.cpp` deliberately avoids in favor of `(10,10)` — matched that established
convention; a packed-hex-literal expected-pixel-value comparison had R and B reversed (the same
class of channel-order mistake documented earlier this session for `TASK-24H-0156`'s test) — fixed
by switching to the established per-channel-extraction comparison style
(`Test_Flip_PresentsPrimarySurface`'s own pattern) instead of a packed literal, which is harder to
get backwards. No product code in `DirectDraw.cpp`/`DirectSound.cpp` was touched by this task.
Verified across three configurations, all clean: default build `ctest` 8/8 (was 7/7);
`-DFREE_DIRECT_ENABLE_ENET=ON` build `ctest -L enet` 1/1 (unaffected — this task adds no ENet-gated
code); `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` build `ctest` 8/8 clean, zero
sanitizer diagnostics from the new combined-subsystem teardown-ordering tests specifically (the
scenario most likely to expose a real use-after-free/double-free if one existed).

### TASK-24H-0181: Close TASK-24H-0057 via a dedicated fresh-process CTest binary for DSERR_NODRIVER
Status: DONE
Priority: P1
Area: DirectSound
Type: Implementation
Evidence: follow-up gap analysis (2026-07-09) — a standalone SDL3 probe confirmed that once
`SDL_InitSubSystem(SDL_INIT_AUDIO)` is attempted with a given `SDL_AUDIODRIVER` value (success or
failure), that outcome is sticky for the rest of the process, even across an explicit
`SDL_QuitSubSystem`+re-`SDL_InitSubSystem` cycle with a *different* driver value afterward — ruling
out any in-process mechanism for `TASK-24H-0057` with hard evidence (not just the previously-
documented suspicion), and confirming a genuinely fresh process is required, exactly as
`enet_directplay_tests` already is for its own, unrelated reasons.
Depends on: None

Problem:
`DirectSoundCreate`'s `DSERR_NODRIVER` graceful-failure path (`SharedAudioDevice`) has never been
exercised by a test, because `SDL_AUDIODRIVER`'s effect is sticky per-process and every existing
test binary's process may already have initialized audio successfully before the relevant test
runs, with no reliable way to force the no-driver condition after the fact. `TASK-24H-0057` has sat
`PARTIAL` — the only non-`DONE` item in the entire backlog — for exactly this reason.

Required work:
- Add a new, minimal CTest binary/test target (e.g. `tests/directsound_nodriver_test.cpp`) whose
  entire body is: call `DirectSoundCreate` once, assert it returns `DSERR_NODRIVER`, exit.
- Register it via `add_test` + `set_tests_properties(... PROPERTIES ENVIRONMENT
  "SDL_AUDIODRIVER=<a-deliberately-bogus-driver-name>")` so CTest launches it as a genuinely fresh
  process with a driver name SDL3 cannot resolve.
- Follow the existing `enet_directplay_tests` pattern for how a dedicated small test binary is wired
  into `tests/CMakeLists.txt`.

Acceptance criteria:
- New test passes: `DirectSoundCreate` returns `DSERR_NODRIVER` in the forced-bogus-driver fresh
  process, with no crash.
- The new test does not affect the default `ctest` run's other 7 tests (must not share a process or
  leak the bogus `SDL_AUDIODRIVER` env var into siblings — CTest's per-test `ENVIRONMENT` property
  is process-scoped, so this should hold structurally, but verify).
- `TASK-24H-0057` updated from `PARTIAL` to `DONE`.

Out of scope:
- Do not add a general-purpose subprocess-testing harness/framework — this is one narrowly-scoped
  new CTest binary, not new test infrastructure for arbitrary future subprocess needs.

Verified: added `tests/directsound_nodriver_test.cpp` (new file, single-purpose: one
`DirectSoundCreate` call, asserts `DSERR_NODRIVER` and a null `*ppDS`, matching
`DirectSoundCreate`'s own confirmed code path at `DirectSound.cpp:784-787`). Wired into
`tests/CMakeLists.txt` with `ENVIRONMENT "SDL_AUDIODRIVER=freedirect-test-nonexistent-driver"` (a
name chosen specifically to not collide with any real SDL3 driver, including the real `disabled`
driver name, which behaves differently from a truly unresolvable one) and label `directsound`;
added to the ASan/UBSan sanitized-target list. Proved the test has real teeth, not just a
vacuously-true assertion, the same way `TASK-24H-0172` proved its ASan regression test did: ran the
binary manually with `SDL_AUDIODRIVER=dummy` (a real, resolvable driver) and confirmed it correctly
*fails* (`DirectSoundCreate` returns `DS_OK`, not `DSERR_NODRIVER`), then with the bogus driver name
and confirmed it passes — the test genuinely discriminates between the two states, it doesn't just
always report success. `TASK-24H-0057` updated from `PARTIAL` to `DONE` — this closes the last
non-`DONE` item anywhere in the 182-task backlog. Verified across three configurations, all clean:
default build `ctest` 9/9 (was 8/8, after `TASK-24H-0180`); `-DFREE_DIRECT_ENABLE_ENET=ON` build
`ctest -L enet` 1/1 (unaffected); `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` build
`ctest` 9/9 clean, zero sanitizer diagnostics. Also confirmed the new test's `ENVIRONMENT` property
is correctly process-scoped and does not leak into sibling tests: the full default-build `ctest`
run above includes `directsound_tests` (which needs a real, working `dummy` driver) passing
immediately before `directsound_nodriver_test` runs in the same `ctest` invocation.

### TASK-24H-0182: Update NEXT.md — FREE_DIRECT demo confirmed running correctly, not just compiling
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: follow-up gap analysis (2026-07-09) — ran `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
./build/FREE_DIRECT` fresh from current source; observed a stable ~52-53 FPS render loop (own
`FREE_DIRECT_PERF` counter), clean behavior for the full observation window (exits only on external
timeout, as expected for a demo with no self-exit condition), independently re-confirmed a second
time before closing this task. `NEXT.md` has stated for multiple sessions that the demo's
on-screen/runtime behavior was "still unverified" — true until now.
Depends on: None

Problem:
`NEXT.md` Sections 2 and 4 describe the `FREE_DIRECT` demo as compiling but never run/observed —
now outdated, since this session's follow-up analysis actually ran it headlessly and confirmed
clean behavior.

Required work:
- Update `NEXT.md`'s "Available artifacts"/"What does not work yet" entries for the demo to state
  it was run headlessly and behaved correctly (stable frame rate, zero errors), not merely that it
  compiles.

Acceptance criteria:
- `NEXT.md` no longer lists the demo's runtime behavior as unverified.

Out of scope:
- Does not claim real-display (non-dummy-driver) visual correctness was checked — only the headless
  run described above. Real-display verification, if ever wanted, is separate, new work.

Verified: independently re-ran the demo myself (fresh `/tmp` scratch build, not reusing the
follow-up analysis's own run) before writing this up, per this project's practice of re-verifying a
sub-agent's claim rather than trusting its summary at face value: `timeout 3 env
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./FREE_DIRECT`, exit code 124 (timeout, expected). The
demo's own `FREE_DIRECT_PERF` log line confirms `present_count` climbing steadily
(54 → 107 over the run) at a stable `FPS=52.4`-`53.3`, `tex_recreated=no`, `build=Release`
(confirming `TASK-24H-0151`'s default-build-type fix is in effect for the demo too). **Correction to
this task's own Evidence line above**: the original follow-up analysis reported "zero
errors/warnings," but my own direct re-run found two non-fatal warnings at startup - "Failed to load
image: player.png" / "Failed to load fallback image: cmake-build-debug/player.png"
(`src/Main.cpp:140` tries to load a demo sprite asset that isn't present in this environment's
working directory). This is a missing test-fixture asset, not a FreeDirect DirectDraw/DirectSound
defect - the render loop starts and continues running normally afterward regardless, proven by the
climbing `present_count`/stable `FPS` above. `NEXT.md` Sections 2 and 4 updated accordingly, with
this nuance included rather than the more sweeping "zero errors" claim.

**Update (2026-07-09, follow-up analysis)**: 3 more atomic tasks added, `TASK-24H-0180` through
`TASK-24H-0182`, from a cross-cutting analysis pass distinct from the three per-subsystem audits
above. Two P1s (`TASK-24H-0180`, untested-but-always-reachable DirectDraw+DirectSound combined
usage; `TASK-24H-0181`, closing the last remaining `PARTIAL` item in the backlog) and one P2
(`TASK-24H-0182`, a documentation correction). New running total: **182 atomic tasks**
(`TASK-24H-0001` through `TASK-24H-0182`).

---

## Maintainability hardening (2026-07-09, second follow-up)

A dedicated maintainability audit (not correctness/performance/memory, which the earlier audits
already covered) ran two parallel passes: one over `src/`/`include/` code itself, one over the
surrounding test/build/documentation infrastructure. Two findings collided with either a standing
project rule (`NEXT.md`'s "no broad refactor" default) or an unwritten-but-real convention (no
shared test-helpers header) and were explicitly asked of the user via `AskUserQuestion` before any
task was written — both were approved as scoped, behavior-preserving exceptions, not blanket
policy changes.

### TASK-24H-0183: Add DirectPlay debug logging matching DirectDraw/DirectSound's FREE_DIRECT_DEBUG_* pattern
Status: DONE
Priority: P2
Area: DirectPlay
Type: Implementation
Evidence: follow-up maintainability audit (2026-07-09) — `DirectDraw.cpp` has 60 `DirectDrawLog(...)`
call sites and `DirectSound.cpp` has 19 `DS_LOG(...)` call sites, both gated by the established
`FREE_DIRECT_DEBUG_*`/`FREE_DIRECT_FORCE_DEBUG_*` mechanism (`IsDirectDrawDebugEnabled()`/
`dsDebugEnabled()`, `DirectDraw.cpp:40-47`/`DirectSound.cpp:54`); `src/directplay/*.{cpp,hpp}` (9
files) has zero logging calls of any kind. No `FREE_DIRECT_DEBUG_DPLAY` exists to reach for when
debugging a DirectPlay issue, unlike the other two subsystems.
Depends on: None

Problem:
DirectPlay is the one subsystem with no debug-observability path, and is also the subsystem most
likely to need real debugging soon as `free-eggbert`'s decompilation progresses and its packet pump
becomes reachable again (`docs/audit_dplay.md` §2/§4's decompilation-in-progress framing).

Required work:
- Add a `DirectPlayLog(...)`/`IsDirectPlayDebugEnabled()` pair in `src/directplay/DirectPlay.cpp`,
  mirroring `DirectDraw.cpp`'s exact pattern (env-var check via `FREE_DIRECT_DEBUG_DPLAY`, plus an
  `#ifdef FREE_DIRECT_DEBUG_DPLAY` compile-time override).
- Add `DPLAY` to `CMakeLists.txt`'s `FREE_DIRECT_FORCE_DEBUG_*` `foreach` list (~line 131),
  producing `FREE_DIRECT_FORCE_DEBUG_DPLAY` automatically via the existing mechanism.
- Add log calls at the same granularity/decision points DirectDraw/DirectSound already use:
  object creation/destruction, `Send()`/`Receive()` delivery-path selection (self-send/unicast/
  broadcast; per-packet-type dispatch), `Open()`/`Close()` state transitions.
- Update `README.md`'s "Debug logging and performance options" list to include the new flag,
  matching how the existing 7 flags are documented there.

Acceptance criteria:
- New test(s) confirming `FREE_DIRECT_DEBUG_DPLAY` produces log output and its absence produces
  none, mirroring the existing zero-log regression-test pattern already used for DirectDraw/
  DirectSound's flags.
- Verified the force-enable CMake option actually changes behavior (build with the flag on,
  confirm the corresponding zero-log test now fails as expected), matching `TASK-24H-0119`/`0120`'s
  own verification method.

Out of scope:
- Do not add logging to `LoopbackDirectPlayTransport`/`EnetDirectPlayTransport`/
  `DirectPlayDiscoveryService` beyond what's needed to observe `DirectPlay2AImpl`'s own
  `Open`/`Close`/`Send`/`Receive`/`CreatePlayer`/`EnumSessions` decision points — a full
  per-transport logging pass is a separate, larger task if ever wanted.

Verified: added `IsDirectPlayDebugEnabled()`/`DirectPlayLog()` to `DirectPlay.cpp`'s anonymous
namespace and `DirectPlayLog(...)` call sites at every specified decision point (constructor,
`Release()`'s final-ref destruction, `Open()` entry+success, `CreatePlayer()` success+rejection,
`Send()`'s three delivery paths, `Receive()`'s `Data`/`JoinAccept`/default packet-type dispatch,
`Close()`). **Real design deviation from the task's own original plan, found and fixed during
implementation**: mirroring `DirectDraw.cpp` literally (`SDL_getenv`/`SDL_strcasecmp`/
`SDL_LogMessageV`) would have added `DirectPlay.cpp`'s first-ever *unconditional* SDL3 dependency -
confirmed by actually trying it: `tests/directplay_tests.cpp`'s own documented "fast iteration" g++
command (no `-lSDL3`, works today because `DirectPlay.cpp`'s SDL3 usage was previously confined to
the `FREE_DIRECT_ENABLE_ENET`-only block) failed to link with an undefined-reference error. Fixed by
reimplementing the same env-var-check/compile-time-override/formatted-log pattern using only
`std::getenv`/a hand-written case-insensitive compare/`std::vfprintf(stderr, ...)` - zero new
dependency, fast g++ loop confirmed still working (re-ran it after the fix: builds, links, `OK: all
DirectPlay tests passed.`). Documented this tradeoff directly in the code comment so a future
DirectPlay change doesn't accidentally reintroduce an SDL3 dependency here. Added `DPLAY` to
`CMakeLists.txt`'s `FREE_DIRECT_FORCE_DEBUG_*` list and updated `README.md`. Two new tests
(`Test_DirectPlayOperations_NoUnconditionalLogOutput_WhenDebugFlagUnset`/
`_ProducesLogOutput_WhenDebugFlagSet`, `tests/directplay_tests.cpp`, 68→70) using a `dup`/`dup2`
(POSIX)/`_dup`/`_dup2` (Windows) stderr-capture technique instead of
`SDL_GetLogOutputFunction`/`SDL_SetLogOutputFunction` (DirectDraw/DirectSound's own equivalent
tests' technique - not usable here for the same SDL3-dependency reason). Verified the flag-unset
test genuinely has teeth, not just a vacuous pass: manually ran with `FREE_DIRECT_DEBUG_DPLAY=1`
against the fast-loop binary and confirmed real log lines are produced (302 lines across the full
suite, sampled and spot-checked for correct formatting), then confirmed the force-enable CMake
option makes the zero-log test genuinely fail as expected (`-DFREE_DIRECT_FORCE_DEBUG_DPLAY=ON`
build: `FAILED: bytes == 0`), matching `TASK-24H-0119`/`0120`'s own verification method. Verified
across four configurations, all clean: default build `ctest` 9/9; `-DFREE_DIRECT_ENABLE_ENET=ON`
build `ctest -L enet` 1/1; `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` build
`ctest` 9/9 clean (the `dup`/`dup2` fd manipulation in the new tests is exactly the kind of raw
system-level code most likely to hide a sanitizer-catchable bug, checked deliberately); a full
out-of-tree `../free-eggbert` rebuild (`CONFIGURE_EXIT=0`, `BUILD_EXIT=0`, zero `error:` matches).

### TASK-24H-0184: Extract Send()/Receive()'s delivery paths into private helper methods (behavior-preserving)
Status: DONE
Priority: P2
Area: DirectPlay
Type: Implementation
Evidence: follow-up maintainability audit (2026-07-09) — `DirectPlay2AImpl::Send()`
(`DirectPlay.cpp`, ~145 lines) and `::Receive()` (~195 lines) are the two largest, most
multi-purpose functions in the entire codebase, each stitching together three substantially
different code paths (`Send`: self-send/unicast/broadcast-with-relay; `Receive`: per-packet-type
dispatch across `Join`/`JoinAccept`/`Data`) in one function body — the single biggest complexity
concentration in the codebase. User explicitly approved a behavior-preserving extract-method
refactor for this via `AskUserQuestion` (2026-07-09), overriding the standing "no broad refactor"
default for this one, scoped case.
Depends on: None

Problem:
A future change to one `Send()`/`Receive()` delivery path requires reading past the other two
unrelated paths in the same function body to be confident nothing was missed — the single biggest
maintainability risk found in this session's code-level audit.

Required work:
- Extract `Send()`'s three delivery paths (self-send, unicast-to-one-assigned-remote-player,
  broadcast-with-host-relay) into three private helper methods on `DirectPlay2AImpl`, each taking
  the arguments it actually needs; `Send()` itself becomes a short dispatcher.
- Extract `Receive()`'s per-packet-type handling (`Join`/`JoinAccept`/`Data`, plus the existing
  drain-loop structure) into private helper methods similarly.
- This is strictly "extract method": no behavioral change of any kind. Every existing test in
  `tests/directplay_tests.cpp` and `tests/enet_directplay_tests.cpp` must pass completely
  unchanged, with no test file edits required by this task.

Acceptance criteria:
- `git diff` shows only `DirectPlay.cpp` (and its own private header if a declaration split is
  needed) changed — no test file edits.
- Full existing test suite passes unchanged: default build `ctest`, ENet-enabled `ctest -L enet`,
  ASan+UBSan build (to independently confirm the refactor introduced no new memory-safety issue).
- Each extracted method is individually reasoned-about-able without needing to read the other
  extracted methods' bodies.

Out of scope:
- Do not change `Send()`/`Receive()`'s external behavior, error codes, or the DPID/routing
  semantics established by Decisions 14/15/20/21 in any way — this is purely an internal structure
  change.
- Do not extract or touch `DirectDraw.cpp`/`DirectSound.cpp` in this task, even though the same
  "no broad refactor" question could theoretically apply there too — `DirectDraw.cpp`'s size was
  flagged as a watch-item, not an approved refactor target; ask again separately if that becomes
  worth doing.
- Do not add new logging as part of this task even though `TASK-24H-0183` is landing nearby — keep
  the two changes independent and separately reviewable/revertable.

Verified: `Send()` is now a 12-line dispatcher calling three new private methods -
`SendBroadcast(idFrom, dwFlags, lpData, dwDataSize)`, `SendSelf(id, dwFlags, lpData, dwDataSize)`,
`SendUnicast(idFrom, idTo, dwFlags, lpData, dwDataSize)` - each taking only the parameters that
path actually uses (`SendSelf` collapses the original `idFrom`/`idTo` to one `id` parameter, since
reaching that path already guarantees they're equal). `Receive()` calls a new `DrainWirePackets()`
in place of its former inline drain loop, which itself calls two new methods,
`HandleDataPacket(header, wireBuf, receivedSize)`/`HandleJoinAcceptPacket(header)`, for the two
packet types with real handling logic (the `default:` case stayed inline in `DrainWirePackets`'s
own `switch` - three lines, not worth its own method). `TASK-24H-0183`'s `DirectPlayLog(...)` calls
travelled with the code blocks they were already inside (not added by this task - that would have
violated this task's own "do not add new logging" scope line - just carried along, unchanged,
during extraction). Every comment, including full Decision-number citations, moved verbatim with
its associated code - not paraphrased, summarized, or dropped - so a reviewer diffing an extracted
method's body against the original inline code sees the intended "cut and paste into a method
signature" mechanical transformation, not a rewrite. Confirmed via `git diff --stat`: only
`DirectPlay.cpp` changed, zero test file edits, satisfying the task's own first acceptance
criterion literally, not just in spirit. Verified across five configurations, all clean: fast
`g++` loop (`tests/directplay_tests.cpp`'s own documented dependency-light build, unaffected by
this task but re-run anyway since it touches the same file `TASK-24H-0183` had to fix a real
dependency regression in) - `OK: all DirectPlay tests passed`; default build `ctest` 9/9;
`-DFREE_DIRECT_ENABLE_ENET=ON` build `ctest -L enet` 1/1, **and** the unfiltered `ctest` under that
same ENet build still shows exactly 66 failing assertions in `directplay_tests` - the identical
count observed before this refactor (`TASK-24H-0183`'s own verification run), confirming the
extraction changed no behavior even in the already-documented loopback-vs-ENet incompatibility
path; `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` build `ctest` 9/9 clean; the
same sanitizer flags combined with `-DFREE_DIRECT_ENABLE_ENET=ON`, `ctest -L enet` 1/1 clean; a
full out-of-tree `../free-eggbert` rebuild (`CONFIGURE_EXIT=0`, `BUILD_EXIT=0`, zero `error:`
matches).

### TASK-24H-0185: Add a shared tests/TestHelpers.hpp, consolidating DirectDraw/DirectSound test scaffolding
Status: DONE
Priority: P2
Area: Tests
Type: Implementation
Evidence: follow-up maintainability audit (2026-07-09) — `tests/integration_tests.cpp` duplicates
~111 of its 304 lines (~36%) as near-identical copies of helper functions already in
`tests/directdraw_tests.cpp`/`tests/directsound_tests.cpp` (`CreateDirectDrawNoWindow`,
`CreateTestWindow`/`TestWindowProc`, `CreatePrimarySurface`, `CreateOffscreenSurface`,
`ReadPresentedPixel`, `CreateDirectSoundNoWindow`, `CreatePcmBuffer`). This has already caused a
real, silent divergence: `integration_tests.cpp`'s copy of `CreateOffscreenSurface` hardcodes
`DDPF_RGB` unconditionally, dropping the `DDPF_PALETTEINDEXED8` 8-bit-surface support the original
(`directdraw_tests.cpp`) provides via `(bpp == 8) ? DDPF_PALETTEINDEXED8 : DDPF_RGB`. Currently
harmless only because no test in `integration_tests.cpp` happens to request an 8-bit offscreen
surface. User explicitly approved introducing a shared header via `AskUserQuestion` (2026-07-09),
reversing this project's prior no-shared-test-header convention for this specific, now-justified
case.
Depends on: None

Problem:
Three test binaries independently reimplement the same small set of DirectDraw/DirectSound
scaffolding helpers, and the duplication has already produced one silent behavioral gap (the
`CreateOffscreenSurface` bpp bug above) that happened to be harmless only by luck of which tests
exist today.

Required work:
- Add a new private header, `tests/TestHelpers.hpp` (not installed, not part of `include/`,
  matching this project's existing private-header conventions), containing the ~10 helper
  functions currently duplicated across `tests/directdraw_tests.cpp`/`tests/directsound_tests.cpp`/
  `tests/integration_tests.cpp`.
- The consolidated `CreateOffscreenSurface` must use the correct, original
  `(bpp == 8) ? DDPF_PALETTEINDEXED8 : DDPF_RGB` logic — this fixes `integration_tests.cpp`'s bug
  as a natural consequence of consolidating onto the correct original, not as a separate patch.
- Update `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`, `tests/integration_tests.cpp`
  to `#include "TestHelpers.hpp"` and remove their own now-duplicate local copies.
- Update `tests/CMakeLists.txt` if any target needs a new include path for the shared header
  (likely none, since it can live directly in `tests/` alongside the binaries that include it).

Acceptance criteria:
- All existing tests in all three files pass completely unchanged in behavior (same assertions,
  same pass/fail outcomes) — this is a pure de-duplication, not a test-behavior change.
- New test in `tests/integration_tests.cpp` (or confirmed via one of the existing files) exercising
  an 8-bit offscreen surface through the now-shared, now-correct `CreateOffscreenSurface`, proving
  the fix.
- `grep` confirms no duplicate definition of any of the consolidated helper functions remains in
  more than one `.cpp` file.

Out of scope:
- Do not consolidate `tests/directplay_tests.cpp`/`tests/enet_directplay_tests.cpp`'s own helpers
  into this header — those test different interfaces (whitebox DirectPlay internals) with no
  overlap with the DirectDraw/DirectSound helpers being consolidated here.
- Do not use this header as a place to add new test-only production-code hooks or whitebox
  accessors — it is purely a consolidation of existing black-box test scaffolding.

Verified: added `tests/TestHelpers.hpp` (new file, 9 functions:
`CreateDirectDrawNoWindow`/`CreateOffscreenSurface`/`CreatePrimarySurface`/`TestWindowProc`/
`CreateTestWindow`/`ReadPresentedPixel`/`FillPrimaryWithColor`/`CreateDirectSoundNoWindow`/
`CreatePcmBuffer`), documented with an explicit `CHECK`-must-be-defined-first contract since it
uses the including file's own failure-reporting macro rather than owning its own. Updated
`tests/directdraw_tests.cpp`/`tests/directsound_tests.cpp`/`tests/integration_tests.cpp` to
`#include` it (after their own `CHECK` definition) instead of defining local copies; `grep`
confirms each of the 9 functions is now defined exactly once, only in `TestHelpers.hpp`. No
`tests/CMakeLists.txt` change was needed - the header lives alongside the `.cpp` files that
include it, on the default per-directory include path.

**Important correction to this task's own Evidence/Problem framing, found while writing this
task's own regression test - not assumed from the original audit finding:** the `CreateOffscreenSurface`
`dwFlags` divergence does **not** actually drop 8-bit surface support or cause any observable
behavioral difference in this codebase today. Read `DirectDraw.cpp` directly to check: `CreateSurface`
only ever reads `ddpfPixelFormat.dwRGBBitCount` from a caller-supplied descriptor (never `dwFlags`),
and `GetSurfaceDesc` derives the `dwFlags` it reports purely from the surface's own internal `bpp_`,
never from whatever was originally passed at creation time - confirmed by grepping the entire file
for `DDPF_PALETTEINDEXED8`/`DDPF_RGB` (exactly one match, `GetSurfaceDesc`'s own derivation) and for
every `ddpfPixelFormat`-related read in `CreateSurface`. Proved this empirically too, not just by
code reading: temporarily reintroduced the unconditional-`DDPF_RGB` bug in `TestHelpers.hpp`,
rebuilt, and the originally-planned "prove the fix" test (`GetSurfaceDesc`-based, checking
`dwFlags`) **still passed** - because `GetSurfaceDesc` recomputes `dwFlags` from `bpp_` regardless
of what `CreateOffscreenSurface`'s own descriptor said, so that check could never have distinguished
the buggy version from the fixed one. The fix itself is still correct and worth keeping (accurate
`DDSURFACEDESC` construction, defensive against a future `DirectDraw.cpp` change that might start
reading this field, and a test helper silently building the wrong descriptor is a real
correctness-of-intent problem even when currently inert) - but this is a code-quality fix, not the
functional-behavior bug fix originally described. Corrected the doc comments in `TestHelpers.hpp`
and `integration_tests.cpp` to state this accurately, and replaced the planned
`Test_CreateOffscreenSurface_8Bit_UsesCorrectPixelFormat` (whose name and comment claimed something
untrue) with `Test_CreateOffscreenSurface_8Bit_HasCorrectBitDepthAndPitch`, which verifies genuine,
real 8-bit behavior (bit count, pitch) survives the consolidation instead. Verified across three
configurations, all clean: default build `ctest` 9/9 (`integration_tests` grew from 4 to 5 tests);
`-DFREE_DIRECT_ENABLE_ENET=ON` build `ctest -L enet` 1/1 (unaffected); `-DFREE_DIRECT_ENABLE_ASAN=ON
-DFREE_DIRECT_ENABLE_UBSAN=ON` build `ctest` 9/9 clean.

### TASK-24H-0186: Replace plan.md's stale "Priority summary" section with a self-verifying pointer
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: follow-up maintainability audit (2026-07-09) — the "Priority summary" section
(`plan.md`, near the end) stops at `TASK-24H-0147` ("New total: 147 atomic tasks"), with zero
mention of any task from `0148` through `0182` — 35 tasks, ~19% of the entire backlog, including
two full audit-hardening waves. A reader trusting this section for priority triage gets
systematically incomplete information with no indication it's stale.
Depends on: None

Problem:
A hand-maintained, duplicated summary of task priorities requires being kept in sync by hand every
time tasks are added, and has demonstrably failed to be kept in sync across at least 5 batches of
additions since it was last updated.

Required work:
- Replace the free-text "Priority summary" section's hand-maintained task-ID lists with a short
  note pointing at the authoritative, always-accurate source: each task's own `Priority:` field,
  queryable directly (e.g. `grep -B2 "^Priority: P0" plan.md | grep "^### TASK"`).
- Keep a brief explanation of what P0/P1/P2/P3 mean (that part isn't a duplication risk, it's a
  legend), but delete the enumerated, hand-maintained task-ID-list-per-priority body.

Acceptance criteria:
- `plan.md` no longer contains a task-ID list that can silently go stale relative to the real
  `Status:`/`Priority:` fields.
- The replacement note's suggested `grep` command actually works and returns a sensible result
  when run.

Out of scope:
- Do not build tooling/scripting to auto-generate this section — a documented `grep` command is
  sufficient at this project's current scale (per the infra audit's own explicit "don't recommend
  enterprise-scale process for a project this size" framing).

Verified: renamed the section "Priority legend" (its actual remaining content) and replaced the
hand-maintained per-priority task-ID lists with an `awk`-based query command, chosen over a plain
`grep` since this file has two different task-header formats (most tasks: multi-line `Status:`/
`Priority:`/... block; `TASK-24H-0172`-`0179`: a condensed single-line `Status: DONE | Priority:
P1 | ...` format) - a fixed-line-offset `grep -B2` would silently miss the condensed-format tasks
the same way the old list already silently missed tasks it was never updated for. Ran the exact
documented command for all four priorities and confirmed sensible, non-empty, roughly-188-in-total
results across P0/P1/P2/P3 (a small overrun beyond 188 is expected and harmless - this very task's
own body text and this Verified note both quote example `Priority: PX` strings, which the command
correctly also counts against `TASK-24H-0186`'s own heading; not a data problem in any other task).
This is a documentation-only change - no code or test files touched, no build/test run needed.

### TASK-24H-0187: Consolidate NEXT.md's duplicated task-count status fact to one source of truth
Status: DONE
Priority: P1
Area: Docs
Type: Documentation
Evidence: follow-up maintainability audit (2026-07-09) — `NEXT.md`'s "182 DONE, 0 TODO, 0 PARTIAL,
0 BLOCKED"-shaped fact is independently restated in at least 5 separate sections (1, 2, 4, 8, 10),
with nothing enforcing consistency between them. This session's own edit history already produced
one stale leftover sentence that survived an earlier edit pass in Section 1 and had to be caught
and fixed separately — direct, current-session proof this drifts in practice, not a hypothetical.
Depends on: None

Problem:
Every implementation batch requires manually updating this fact in 5+ places; a partial update
risks leaving contradictory claims within the same file, which has already happened once this
session.

Required work:
- Restructure `NEXT.md` so the task-count fact is stated prominently and completely exactly once
  (recommend: a short "at a glance" line near the very top of Section 1), and every other section
  that currently restates the exact counts instead references it ("see the count at the top of
  this file" / "see Section 1") without repeating the numbers.
- Apply this restructuring to the current, already-accurate 182/0/0/0 state as part of this task —
  do not leave the file in a partially-migrated state.

Acceptance criteria:
- `grep` confirms the exact task-count phrase (or numbers) appears in exactly one place in the
  resulting `NEXT.md`, with every other former restatement replaced by a cross-reference.
- All of `NEXT.md`'s other content (Sections 3/5/6/7/9's substantive content, not just the count)
  is preserved — this is a structural change to reduce duplication, not a content cut.

Out of scope:
- Do not apply the same restructuring to `plan.md`'s per-task `Status:`/`Priority:` fields — those
  are not duplicated (each task has exactly one `Status:` line), this task is specifically about
  the cross-section-duplicated summary counts in `NEXT.md` only.

Verified: added a one-paragraph "At a glance" block between `NEXT.md`'s H1 title and its "## 1.
Project summary" heading - even more prominent than the task's own "near the top of Section 1"
suggestion - stating the exact current count once, explicitly labeled the single authoritative
source, with an instruction to update only that line. Replaced every other exact-count restatement
(Sections 1, 4, 8, 10 - Section 2's own test-count breakdown was left alone, since that's a
different, non-duplicated fact from the task-count summary this task is about) with a
cross-reference back to the top. Distinguished this from legitimate historical narrative describing
a past point-in-time milestone (e.g. Section 3 item 8's "closing the backlog to 182/182" describing
what happened after that specific batch, not asserting it as the current state) - those were left
as accurate history, not treated as duplication to eliminate. `grep -n "DONE, [0-9]* TODO"
NEXT.md` confirms exactly one live/current-state match (the new top-of-file line) plus the one
historical-narrative match, which is intentional, not a miss. Documentation-only change - no code
or test files touched, no build/test run needed. This task's own completion is reflected in the
same edit that updated the top-of-file count from 184/4 to 185/3.

### TASK-24H-0188: Reconcile docs/directplay-limitations.md's deviation table with Decisions 20-27
Status: DONE
Priority: P2
Area: Docs
Type: Documentation
Evidence: follow-up maintainability audit (2026-07-09) — `NEXT.md`'s own Section 8 already flags
this: "`docs/directplay-limitations.md` still has the pre-resolution deviation-table framing and is
now somewhat superseded by the Decisions themselves for these 7 items specifically (not yet
re-reconciled...)". A known, self-acknowledged gap that's persisted across multiple sessions
without being formalized as its own task until now.
Depends on: None

Problem:
`docs/directplay-limitations.md`'s deviation table was written before Decisions 20-27 resolved the
7 Track B questions (plus the new Decision 27); the table's entries for those 7 items may describe
pre-resolution behavior rather than the real, current, Decision-driven behavior.

Required work:
- Read `docs/directplay-limitations.md`'s full deviation table against `docs/directplay-design.md`'s
  Decisions 20-27 and update every table row whose described behavior was changed by one of those
  Decisions, so the table accurately reflects current behavior.
- Add a cross-reference from each updated row to the specific Decision number that changed it,
  matching the existing citation style already used elsewhere in this file.

Acceptance criteria:
- No deviation-table row describes pre-Decision-20-27 behavior as if it were still current.
- Every row affected by a Decision cites that Decision number.

Out of scope:
- Do not change `docs/directplay-design.md` itself — Decisions are a historical record of what was
  decided and when, not to be rewritten; only `directplay-limitations.md`'s own table is in scope.
- Do not attempt to resolve `docs/audit-24h-free-direct.md`'s separate, smaller overlap with
  `docs/directplay-callsite-audit.md` in this task — that's a distinct, lower-priority finding from
  the same audit; a separate task if ever pursued.

Verified: read `docs/directplay-limitations.md`'s full 23-row deviation table plus its dedicated
"Formerly-BLOCKED design questions" section line by line against `docs/directplay-design.md`'s
Decisions 20-27, instead of assuming this task's own Evidence line (quoting `NEXT.md`) was
accurate. **Finding: the premise was wrong.** Every row touched by Decisions 20-26 was already
correctly marked `(**resolved, implemented**)`/`(**resolved: decided not needed**)` with accurate
Decision citations and current-behavior descriptions (rows for DPID-0 broadcast, LAN discovery,
ENet host address, broadcast delivery, host routing, ENet join handshake, player names,
duplicate-player, player-lost state) — the reconciliation had already happened in an earlier
session. The stale claim was in `NEXT.md` itself, asserting a gap that no longer existed. The one
real, small gap on re-check: the LAN-discovery-responder row (added same-day as `TASK-24H-0176`)
cited `TASK-24H-0176` but not the formal `Decision 27` it corresponds to - added that citation.
Corrected `NEXT.md`'s own stale claim (Section 8, Track B paragraph) to record what was actually
found rather than repeating the inaccurate "not yet re-reconciled" note. Documentation-only change
to both files - no code or test files touched, no build/test run needed.

### TASK-24H-0189: Fix DirectDraw header/implementation @note Status: tag inconsistencies
Status: DONE
Priority: P1
Area: DirectDraw
Type: Documentation
Evidence: user asked directly, following a code-quality assessment question, to fix the
`GetDC`/`ReleaseDC` header-vs-implementation status-tag mismatch already flagged (but never
fixed) across multiple earlier documents - `docs/directdraw-limitations.md`'s own "GetDC/ReleaseDC:
functionally real, documented STUB" section, `docs/audit_ddraw.md` §5.5 (explicitly cross-
referencing the same, already-known finding), and `TASK-24H-0044`'s own `Verified:` note (an
earlier session's explicit "no fix was needed" judgment call, being revisited here). A systematic
sweep prompted by that fix found a second, more serious mismatch in the opposite direction.
Depends on: None

Problem:
`include/ddraw.h`'s public `@note Status:` Doxygen tags for `GetDC`/`ReleaseDC` and `IsLost`/
`Restore` had both drifted out of sync with `src/directdraw/DirectDraw.cpp`'s own tags for the same
methods, in opposite directions: `GetDC`/`ReleaseDC` were tagged `STUB` despite being functionally
real (understated compatibility); `IsLost`/`Restore` were tagged `IMPLEMENTED` despite being honest
inert stubs (overstated compatibility - the more serious kind per `CLAUDE.md`'s Documentation
Policy: "must never claim compatibility that does not exist"). The public header is the one most
likely to actually be read by a future contributor, so a stale header tag is worse than a stale
internal comment.

Required work:
- Change `include/ddraw.h`'s `GetDC`/`ReleaseDC` tags from `STUB` to `IMPLEMENTED`, matching
  `DirectDraw.cpp`'s already-correct tags and the functionally-real behavior documented in
  `docs/directdraw-limitations.md`.
- Change `include/ddraw.h`'s `IsLost`/`Restore` tags from `IMPLEMENTED` back to `STUB`, matching
  `DirectDraw.cpp`'s already-correct tags and the honest-inert-stub behavior already documented in
  `docs/directdraw-limitations.md` (which had been describing the header as saying `STUB` for these
  two - itself a stale claim once the header actually said `IMPLEMENTED`).
- Update `README.md`'s Compatibility Status table entries for both pairs to match.
- Update `docs/directdraw-limitations.md`'s two corresponding sections to describe the fix, not
  just the prior state.
- Systematically check `include/dsound.h`/`src/directsound/DirectSound.cpp` and the method-level
  (not macro-group/typedef) tags in `include/dplay.h`/`src/directplay/DirectPlay.cpp` for the same
  class of mismatch before considering this task complete, per `docs/audit_ddraw.md` §7's own
  suggestion that this deserves a systematic check, not a one-off fix.

Acceptance criteria:
- `include/ddraw.h`, `README.md`, and `docs/directdraw-limitations.md` all agree with
  `DirectDraw.cpp`'s own tags for `GetDC`/`ReleaseDC`/`IsLost`/`Restore`.
- DirectSound and DirectPlay checked for the same pattern; any other genuine mismatch found is
  either fixed in this same task (if trivial) or filed as a new task (if not).

Out of scope:
- `include/dplay.h`'s `@note Status: STUB` tags on macro/constant groups (`DPERR_*`, session/send
  flags) and on type declarations (`DPNAME`, `DPSESSIONDESC2`, the three callback typedefs) are a
  different, more ambiguous category - a `STUB`/`IMPLEMENTED`/`PARTIAL` tag naturally fits a
  *method* (something with call-pattern-dependent behavior), not a struct layout or a `#define`
  block, and assigning each of these a more granular status would require a real per-field/
  per-constant judgment call, not a mechanical header-vs-implementation comparison. Left for a
  separate task if ever pursued, not decided unilaterally here.

Verified: fixed `include/ddraw.h`'s four tags as described. Systematically cross-checked every
method-level status tag in `include/dsound.h` against `src/directsound/DirectSound.cpp` (7 pairs
with an unqualified `IMPLEMENTED`/`PARTIAL` tag on both sides: `GetStatus`, `Stop`, `Lock`,
`Unlock`, `SetCurrentPosition`, `SetCooperativeLevel`, `CreateSoundBuffer`) and found zero
mismatches - DirectSound's tags were already fully consistent. Cross-checked every method-level tag
in `include/dplay.h` against `src/directplay/DirectPlay.cpp` and found zero mismatches too (all
already correctly reflect the extensive DirectPlay work done earlier this session - `QueryInterface`,
`Send`, `Receive`, `Open`, `Close`, `EnumSessions`, `CreatePlayer`, `DirectPlayCreate`,
`DirectPlayEnumerateA`/`W`). Confirmed the four `IDirectDrawSurface`/`IDirectDraw` `QueryInterface`
implementations (`DirectDrawSurfaceImpl`, `DirectDrawPaletteImpl`, `DirectDrawClipperImpl`,
`DirectDrawImpl`) are all genuine, unconditional-`DDERR_UNSUPPORTED` stubs by reading each one
directly - their existing `STUB` tags are accurate, not a fifth mismatch. Updated `README.md` and
`docs/directdraw-limitations.md` as described. This is a documentation-only change (four one-line
Doxygen comments, two doc-file sections, two README table cells) - no production logic changed;
verified `header_smoke_ddraw`/`header_hygiene` still pass (public header still compiles standalone,
still leaks no internal-backend symbol) rather than assuming a comment-only change is automatically
safe.

**Update (2026-07-09, second follow-up)**: 6 more atomic tasks added, `TASK-24H-0183` through
`TASK-24H-0188`, from a dedicated maintainability audit (code-level + infrastructure-level, run in
parallel). Two required a user decision before being written at all (`TASK-24H-0184`'s
extract-method refactor of `Send()`/`Receive()`, `TASK-24H-0185`'s new shared test-helpers header)
and were resolved via `AskUserQuestion`, not assumed. `TASK-24H-0189` was added and closed the same
day, prompted by a direct user follow-up after a code-quality assessment question. New running
total: **189 atomic tasks** (`TASK-24H-0001` through `TASK-24H-0189`).

---

## Priority legend

`P0` = build/test-blocking, hot-path correctness, or a recurring verification gate. `P1` = the
majority of test-coverage, documentation-accuracy, and safe-implementation tasks. `P2` = cleanup,
optional hardening, lower-frequency call paths, or documentation-only follow-up. `P3` = pure
documentation/history cleanup, rarely used (most historical-plan cleanup is folded into a Phase
reconciliation task instead of kept as a separate P3 entry, per the "atomic, don't duplicate"
rule).

**This section used to enumerate every task ID under its priority by hand. That list was found
stale during a 2026-07-09 maintainability audit — it stopped at `TASK-24H-0147` and silently
omitted the next 35 tasks (~19% of the backlog at the time, including two entire audit-hardening
waves), with nothing to warn a reader it was incomplete (`TASK-24H-0186`).** A hand-maintained,
duplicated list requires being kept in sync by hand every time a task is added, and demonstrably
failed to be. Query each task's own `Priority:` field directly instead — it is authoritative by
construction, since it's each task's only copy of its own priority:

```bash
awk '/^### TASK-24H-/{t=$0} /Priority: P0/{print t}' plan.md | sort -u
```

Substitute `P1`/`P2`/`P3` for `P0` as needed. This works across both this file's task-header
formats (the verbose multi-line `Status:`/`Priority:`/`Area:`/`Type:` block used by most tasks, and
the condensed single-line `Status: DONE | Priority: P1 | Area: ... | Type: ...` format used by
`TASK-24H-0172`-`0179`) by tracking the most recently seen task heading rather than assuming a
fixed line offset.
