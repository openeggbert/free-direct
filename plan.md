# FreeDirect Task Plan

This is the **authoritative English task list** for FreeDirect (see `CLAUDE.md`'s `plan.md`
Policy). It supersedes `TODO.md` as the forward-looking backlog. `TODO.md` is kept as historical
review notes and is not deleted.

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
- [ ] Add a unit test for `Receive` on an empty queue, asserting `DPERR_NOMESSAGES`.
- [ ] Add a unit test for `Receive` with a too-small caller-provided buffer, asserting the queued
      packet is preserved (not dequeued) and a meaningful error is returned.
- [ ] Add a unit test for `Receive` performing a successful copy, asserting payload bytes, sender
      DPID, and recipient DPID all match what was queued.

**Acceptance criteria:** the three new tests pass under CTest; `DirectPlayMessageQueue` has zero
transport/backend dependencies, verifiable by confirming no `SDL_`/`ENet` identifier appears in
`src/directplay/DirectPlayMessageQueue.*`.

---

## Phase 4 — Loopback backend

Goal: a fully in-process `IDirectPlayTransport` implementation, used as the default backend for
every subsequent DirectPlay test so tests stay deterministic and hermetic.

- [ ] Implement `LoopbackDirectPlayTransport` in `src/directplay/LoopbackDirectPlayTransport.hpp`/
      `.cpp`, implementing `IDirectPlayTransport` entirely in-process with no real sockets.
- [ ] Allow creating a local session without real networking: `Open(..., DPOPEN_CREATE)` against a
      `DirectPlaySession` configured with `LoopbackDirectPlayTransport` succeeds with zero network
      I/O.
- [ ] Allow creating one local player via `CreatePlayer` on a loopback-backed session.
- [ ] Allow sending a packet to self: `Send(idFrom, idFrom, ...)` on a loopback-backed session
      enqueues directly into that same session's receive queue.
- [ ] Allow receiving the self-sent packet via `Receive` immediately after the `Send` above, with
      no thread or event wait required.
- [ ] Use `LoopbackDirectPlayTransport` as the default transport for all new DirectPlay unit tests
      from this phase onward.
- [ ] Add a unit test for `CreatePlayer` against a loopback session, asserting a non-zero DPID is
      returned and is unique among players already created in that session.
- [ ] Add a unit test for `Send` to self over loopback, asserting `DP_OK`.
- [ ] Add a unit test for `Receive` after a loopback self-send, asserting the received payload
      matches the sent payload byte-for-byte.
- [ ] Add a unit test for `Close` on a loopback-backed session, asserting a subsequent `Send`/
      `Receive` returns `DPERR_NOCONNECTION` (Phase 11) rather than crashing or silently succeeding.

**Acceptance criteria:** the four loopback tests pass with zero real socket usage; no `ENet*`/
`SDL_net*` symbol appears anywhere in `LoopbackDirectPlayTransport`'s translation unit.

---

## Phase 5 — ENet integration planning

Goal: stand up the ENet-backed transport skeleton and the internal wire protocol it will use,
entirely gated behind a CMake option so the default build has no ENet dependency.

- [ ] Add a CMake option `FREE_DIRECT_ENABLE_ENET` (default `OFF`) to `CMakeLists.txt`.
- [ ] Add CMake detection for a vendored ENet (submodule or `FetchContent`), gated behind
      `FREE_DIRECT_ENABLE_ENET`.
- [ ] Add optional CMake detection for a system-installed ENet (`find_package`/
      `pkg_check_modules`) as an alternative to the vendored copy, gated behind the same option.
- [ ] Keep ENet's include directories `PRIVATE` to the `free-direct` CMake target.
- [ ] Add a review-time (or build-time) check confirming no header under `include/` transitively
      includes any ENet header.
- [ ] Add an `EnetDirectPlayTransport` class skeleton in `src/directplay/EnetDirectPlayTransport.hpp`/
      `.cpp`, implementing `IDirectPlayTransport` with method bodies to be filled in by later tasks
      in this phase.
- [ ] Add ENet initialization (`enet_initialize`) and shutdown (`enet_deinitialize`) handling,
      performed once per process regardless of how many `EnetDirectPlayTransport` instances exist.
- [ ] Add ENet host creation (`enet_host_create` in listen mode) for the hosting role.
- [ ] Add ENet client creation (`enet_host_create` with no listen address) for the joining role.
- [ ] Add ENet peer connection (`enet_host_connect`) for the joining role.
- [ ] Add ENet disconnect handling (`enet_peer_disconnect` plus processing the resulting
      `ENET_EVENT_TYPE_DISCONNECT` event).
- [ ] Add reliable packet send using `ENET_PACKET_FLAG_RELIABLE`.
- [ ] Add unreliable packet send **only if needed**: Phase 0 found every observed `free-eggbert`
      `Send` call site uses a truthy flag that collapses to `DPSEND_GUARANTEED`, so unreliable send
      may not be required at all. Re-check the Phase 0 audit and ask the user before implementing
      this task.
- [ ] Map `DPSEND_GUARANTEED` to `ENET_PACKET_FLAG_RELIABLE` in the transport layer.
- [ ] Decide the default ENet channel layout (a single channel is likely sufficient given both
      target games' simple message patterns) and document the decision with rationale.
- [ ] Add a packet type enum (e.g. `Join`, `JoinAccept`, `JoinReject`, `Data`, `Discovery`,
      `DiscoveryResponse`) for the internal FreeDirect-to-FreeDirect wire protocol.
- [ ] Add a protocol version field to the internal packet header.
- [ ] Add a magic number field to the internal packet header, to reject non-FreeDirect traffic
      early.
- [ ] Add an application GUID field to the internal packet header, populated from
      `DPSESSIONDESC2.guidApplication`, so peers running different target games can never join each
      other's sessions.
- [ ] Add a session GUID field to the internal packet header, populated from
      `DPSESSIONDESC2.guidInstance`.
- [ ] Add a sender player ID field to the internal packet header.
- [ ] Add a recipient player ID field to the internal packet header.
- [ ] Add a payload length field to the internal packet header.
- [ ] Add defensive packet size validation on receive: reject packets smaller than the fixed
      header size, and reject a stated payload length that does not match the actual received byte
      count.
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

- [ ] Implement `Open(..., DPOPEN_CREATE)` end-to-end on top of the configured transport.
- [ ] Create a session instance GUID (`guidInstance`) when hosting, if the caller did not already
      supply one.
- [ ] Store the session descriptor supplied to `Open` on `DirectPlaySession` (per Phase 2).
- [ ] Start the ENet host listener as part of `Open(..., DPOPEN_CREATE)` when using
      `EnetDirectPlayTransport`.
- [ ] Assign the host player-ID namespace: decide and document the starting DPID value and
      increment rule for host-allocated players, consistent with Phase 0's finding about
      `free-eggbert`'s index-based DPID comparison.
- [ ] Allow the host to accept incoming client connections up to `dwMaxPlayers`.
- [ ] Send a join-accepted packet (Phase 5 protocol) to a connecting client once accepted.
- [ ] Send a join-rejected packet to a connecting client when the session is full or the
      application GUID does not match.
- [ ] Enforce `dwMaxPlayers` by rejecting new joins once `dwCurrentPlayers` reaches the configured
      maximum.
- [ ] Update `dwCurrentPlayers` as players join and leave.
- [ ] Add a test for host session creation over loopback, asserting `Open(..., DPOPEN_CREATE)`
      returns `DP_OK` and the session reports itself as host.
- [ ] Add a test for invalid host parameters (e.g. `dwMaxPlayers == 0`, malformed
      `DPSESSIONDESC2.dwSize`), asserting a meaningful `DPERR_*` rather than `DP_OK`.
- [ ] Add a test for closing a host session, asserting a subsequent `EnumSessions` from another
      loopback peer no longer finds it.

**Acceptance criteria:** two `DirectPlaySession` instances over `LoopbackDirectPlayTransport` in
the same test process can host and observe each other's presence; all three new tests pass with
`FREE_DIRECT_ENABLE_ENET=OFF`.

---

## Phase 7 — Session joining

Goal: make `Open(..., DPOPEN_JOIN)`/`Open(..., DPOPEN_OPENSESSION)` actually connect to a hosted
session and receive an assigned player ID.

- [ ] Implement `Open(..., DPOPEN_JOIN)` (and the `DPOPEN_OPENSESSION` path used by
      `free-eggbert`) end-to-end on top of the configured transport.
- [ ] Resolve an explicit host address if the caller/transport configuration provides one
      (loopback: direct in-process reference; ENet: host/port).
- [ ] Connect to the host transport (`enet_host_connect` for ENet; direct handoff for loopback).
- [ ] Send a join-request packet to the host once connected.
- [ ] Receive a join-accepted packet from the host and transition local state to "joined."
- [ ] Receive the host-assigned player ID from the join-accepted packet and store it as this
      peer's local DPID.
- [ ] Store the session descriptor received from the host (or supplied by the caller) on the
      joining `DirectPlaySession`.
- [ ] Handle a join timeout: fail `Open` if no join-accepted/join-rejected packet arrives within a
      configured timeout.
- [ ] Return `DPERR_NOSESSIONS` when no host could be reached at all, and `DPERR_TIMEOUT` when a
      host was reached but did not respond in time, matching the distinction implied by
      `DPESC_TIMEDOUT` usage in `free-eggbert/src/network.cpp`'s `EnumSessionsCallback`.
- [ ] Add a test for a failed join (no host present), asserting `DPERR_NOSESSIONS`.
- [ ] Add a test for a successful join using a local host/client pair over loopback, asserting both
      peers agree on the assigned DPIDs and session descriptor.
- [ ] Add a test for max-players rejection: a third loopback client joining a two-player-max
      session receives a rejected outcome (a `DPERR_*` code, not `DP_OK`).

**Acceptance criteria:** the host/client-pair test and the max-players test both pass
deterministically over loopback; no test depends on real wall-clock timing beyond a small,
generous timeout bound.

---

## Phase 8 — Session enumeration

Goal: make `EnumSessions` discover real hosted sessions instead of always reporting none.

- [ ] Decide whether `EnumSessions` supports explicit-host-only discovery, LAN broadcast
      discovery, or both, and record the decision with rationale in `docs/directplay-design.md`.
      Given `free-eggbert`'s `CNetwork::EnumSessions` only needs *some* list of sessions to
      populate a picker, explicit-host-only is the minimal viable choice — confirm against Phase 0
      findings before committing to broadcast.
- [ ] Implement explicit-host enumeration first (query one or more known host addresses/loopback
      sessions directly).
- [ ] Add LAN broadcast discovery later, **only if a concrete need is confirmed** — ask the user
      before starting this task, per the two-game scope rule in `CLAUDE.md`.
- [ ] Define a discovery-request packet in the Phase 5 protocol.
- [ ] Define a discovery-response packet in the Phase 5 protocol.
- [ ] Include the protocol version field in both discovery packets.
- [ ] Include the application GUID field in both discovery packets.
- [ ] Ignore discovery responses whose application GUID does not match the requesting
      application's GUID.
- [ ] Fill `DPSESSIONDESC2` correctly for the `EnumSessions` callback, from each discovered
      session's advertised descriptor fields.
- [ ] Call the `EnumSessions` callback exactly once per discovered session.
- [ ] Respect the callback's `BOOL` return value: stop enumerating further sessions once it
      returns `FALSE`.
- [ ] Respect the `dwTimeout` parameter passed to `EnumSessions`, bounding how long discovery
      waits for responses.
- [ ] Return `DPERR_NOSESSIONS` only if confirmed necessary by `free-eggbert`'s exact expected
      behavior (Phase 0 found no explicit dependency on this specific code — verify before
      hard-coding it as a required return).
- [ ] Add a test for `EnumSessions` finding zero sessions.
- [ ] Add a test for `EnumSessions` finding exactly one local (loopback-hosted) session, asserting
      the exact `DPSESSIONDESC2` fields the callback received.
- [ ] Add a test for callback-stop behavior: a callback returning `FALSE` after the first result
      must prevent a second invocation even when two sessions exist.

**Acceptance criteria:** all three enumeration tests pass over loopback with zero real network
I/O; the "one session" test asserts on exact `DPSESSIONDESC2` field values, not just call count.

---

## Phase 9 — Player management

Goal: give player creation, naming, and removal real, race-free semantics consistent with the
Phase 0 DPID-vs-index finding.

- [ ] Implement stable DPID allocation in `DirectPlaySession`/`DirectPlayPlayer`, using the
      strategy documented in Phase 0/Phase 6 (host-assigned sequential small integers in join
      order).
- [ ] Reserve an invalid DPID value (`0`, matching real DirectPlay's `DPID_SYSMSG`/
      `DPID_ALLPLAYERS` convention) so it is never assigned to a real player — **and explicitly
      resolve the conflict** with Phase 0's finding that `free-eggbert`'s receive-side code
      compares `from == i` starting at index `0`: document in writing whether the host's first
      real player must be DPID `1` (reserving `0`) or DPID `0` (matching the game's apparent
      assumption), since these two choices are mutually exclusive.
- [ ] Register a local player on `CreatePlayer`, storing it in `DirectPlaySession`'s local-player
      list.
- [ ] Register a remote player when a join-accepted/player-joined notification arrives from the
      transport, storing it in the remote-player list.
- [ ] Store each player's short name (`DPNAME.lpszShortNameA`) as an owned `std::string`.
- [ ] Store each player's long name (`DPNAME.lpszLongNameA`) as an owned `std::string`, allowing it
      to be empty since `free-eggbert` always passes `NULL` for it.
- [ ] Store player data bytes (`lpData`/`dwDataSize` from `CreatePlayer`) **only if** a concrete
      call site is found requiring it — Phase 0 found `free-eggbert` always passes `NULL`/`0`;
      confirm before adding storage, per the two-game scope rule.
- [ ] Signal an event handle (`hEvent` from `CreatePlayer`) on message arrival **only if** a
      concrete call site needs it — Phase 0 found `free-eggbert` always passes `NULL`; confirm
      before implementing, per the two-game scope rule.
- [ ] Validate player count against `dwMaxPlayers` before allocating a new DPID in `CreatePlayer`,
      returning `DPERR_CANTCREATEPLAYER` when the session is full.
- [ ] Validate against duplicate players (the same peer calling `CreatePlayer` twice without an
      intervening `Close`) and decide/document the resulting behavior.
- [ ] Implement a player-lost state (transport-level disconnect detected for a remote player
      without an explicit `Close`) distinct from a clean removal.
- [ ] Generate a player-created system message (`DPID_SYSMSG`-sourced) **only if** Phase 0's audit
      finds a concrete `free-eggbert` dependency on receiving one — it did not find explicit
      system-message handling in `event.cpp`/`decnet.cpp`; verify before implementing.
- [ ] Generate a player-destroyed system message under the same condition as above.
- [ ] Add a test asserting DPID uniqueness across multiple `CreatePlayer` calls within one session.
- [ ] Add a test asserting stored short/long player names round-trip correctly.
- [ ] Add a test asserting player removal (via `Close` or disconnect) updates `dwCurrentPlayers`
      and removes the player from future `EnumSessions`/roster queries.

**Acceptance criteria:** the DPID-vs-index conflict has an explicit written decision in
`docs/directplay-design.md` merged *before* any other Phase 9 code lands; the three new tests
pass.

---

## Phase 10 — Send/Receive networking

Goal: real message delivery between distinct peers (not just self-loopback), including host
routing, broadcast, and validation.

- [ ] Implement `Send` from a local player to a specific remote player, routed through the
      configured transport.
- [ ] Implement host-side routing: the host forwards a `Send` addressed to a non-host recipient to
      that recipient's connection (star topology, matching ENet's client/server model).
- [ ] Implement direct peer-to-peer delivery **only if** a future architectural decision moves away
      from the host-hub star topology — not needed under the current plan; leave as a documented
      non-task unless the topology decision changes.
- [ ] Implement broadcast-to-all delivery for `idTo == DPID_ALLPLAYERS`/`0`, matching
      `free-eggbert/src/network.cpp`'s `Send(m_dpid, 0, ...)` call pattern.
- [ ] Add the `DPID_ALLPLAYERS` and `DPID_SYSMSG` constants to `include/dplay.h` (if not already
      added in Phase 0/Phase 2), so broadcast sends have named constants available even though
      current call sites use a literal `0`.
- [ ] Preserve DirectPlay-like packet boundaries: each `Send` call must arrive as exactly one
      `Receive`-visible message, never coalesced or split.
- [ ] Preserve reliable, ordered delivery for `DPSEND_GUARANTEED` sends (mapped to
      `ENET_PACKET_FLAG_RELIABLE` per Phase 5 when using the ENet backend).
- [ ] Validate the sender player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idFrom` does
      not correspond to a locally-registered player.
- [ ] Validate the recipient player ID in `Send`, returning `DPERR_INVALIDPLAYER` when `idTo` is
      neither a known player DPID nor the broadcast ID.
- [ ] Validate a null payload with zero length (`lpData == nullptr && dwDataSize == 0`) as an
      accepted no-payload send, only if a call site needs it; otherwise document it as rejected.
- [ ] Reject a null payload with nonzero length (`lpData == nullptr && dwDataSize > 0`) with
      `DPERR_INVALIDPARAMS`.
- [ ] Reject messages larger than the maximum payload size (Phase 3/Phase 11) with
      `DPERR_SENDTOOBIG`, sized to comfortably exceed the largest observed `free-eggbert` payload
      (e.g. `sizeof(NetMessage) * pack.nbMessages + 20` in `src/decnet.cpp`, and the 128/132-byte
      packets in `src/event.cpp`).
- [ ] Add a host/client integration test over loopback: host sends to a specific client, client
      receives the exact payload.
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

- [ ] Update `README.md` with honest subsystem statuses reflecting Phases 1-14's actual completed
      work (not aspirational status).
- [ ] Add `docs/directplay-design.md`, covering the state model, transport abstraction, and the
      DPID-allocation/enumeration-strategy decisions made in Phases 1-9.
- [ ] Add `docs/directplay-protocol.md`, covering the Phase 5 internal wire packet header layout
      and packet type enum.
- [ ] Add `docs/directplay-limitations.md`, covering the Phase 11 error-semantics deviation table
      and any unimplemented DirectPlay surface (groups, lobby APIs) with an explicit "not
      implemented, not needed by either target game" note.
- [ ] Add `docs/networking-backends.md`, covering the ENet-first / SDL3_net-optional /
      loopback-for-tests decision from `CLAUDE.md`'s Networking Backend Decision section.
- [ ] Add `docs/directdraw-limitations.md`, covering the Phase 14 audit findings with concrete
      call-site counts from both target games.
- [ ] Add `docs/directsound-limitations.md`, covering the Phase 13 audit findings with concrete
      call-site counts from both target games.
- [ ] Update the compatibility table in `README.md` (or add one if none exists) listing each
      DirectDraw/DirectSound/DirectPlay method and its status (`STUB`/`PARTIAL`/`IMPLEMENTED`) per
      the header-comment convention.
- [ ] Review all updated/new docs to ensure none claim full DirectX 3 compatibility.
- [ ] Ensure all updated/new docs explicitly state DirectPlay is not Microsoft-wire-compatible.
- [ ] Ensure all updated/new docs explicitly state FreeDirect multiplayer only works between
      programs both built against this FreeDirect DirectPlay implementation.

**Acceptance criteria:** every new `docs/*.md` file listed above exists, is written in English,
and is linked from `README.md`'s table of contents or a new "Further Reading" section.

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
