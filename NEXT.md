# NEXT.md

**At a glance (2026-07-09, updated by `TASK-24H-0187`)**: `plan.md` carries **188** atomic
`TASK-24H-XXXX` tasks — **185 DONE, 3 TODO, 0 PARTIAL, 0 BLOCKED** (`grep`-verified directly against
`plan.md`, not estimated or recalled from memory). **This is the single authoritative count for
this file — every other section below references it instead of restating the numbers**, per
`TASK-24H-0187` (a prior version of this file independently restated this exact fact in 5+
sections with nothing enforcing consistency between them, which had already caused one stale
leftover sentence to survive an editing pass earlier this same session). Update this line, and only
this line, when the count changes.

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is explicitly **not** an attempt at full DirectX compatibility —
scope is bounded by what the two target games' real call sites need (see `CLAUDE.md`, the project
charter).

- **Target games** (sibling repos, present on disk): `../free-eggbert` (*Speedy Blupi* —
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi* — DirectDraw +
  DirectSound only; confirmed zero DirectPlay usage by grep). **Both confirmed building cleanly,
  out-of-tree, this session** (see Section 3).
- **Main DirectPlay goal**: two programs both built against FreeDirect's own DirectPlay
  implementation can host/join/exchange messages with each other. This is explicitly **not**
  wire-compatible with real Microsoft DirectPlay — FreeDirect-to-FreeDirect only.
- **Current development phase**: `plan.md`'s original Phases 0–18 and the 24-Hour Stabilization
  Backlog are both fully closed out for implementation. This session ran three fresh subsystem
  audits - DirectDraw (`docs/audit_ddraw.md`), DirectSound (`docs/audit_dsound.md`), DirectPlay
  (`docs/audit_dplay.md`) - producing 29 tasks (`TASK-24H-0151`-`0179`), all implemented; a
  follow-up cross-cutting gap-analysis pass (looking specifically for issues the three
  subsystem-scoped audits couldn't have found by construction) produced 3 more
  (`TASK-24H-0180`-`0182`), also all implemented, closing the backlog completely for the first
  time; then a dedicated maintainability audit produced 6 more (`TASK-24H-0183`-`0188`), currently
  being implemented one task per commit - see the top of this file for the exact current count, and
  Section 3 for the full narrative. `TASK-24H-0057` (DirectSound's `DSERR_NODRIVER` path, `PARTIAL`
  for multiple sessions) was finally closed by `TASK-24H-0181`. Session 1 ended at 64 DONE.
  **All 7 of the project's standing BLOCKED
  DirectPlay design questions (Track B) were asked of, answered by, and implemented for the user in
  an earlier session** - never decided unilaterally (see Section 3 and
  `docs/directplay-design.md` Decisions 20-26); the new DirectPlay audit did not reopen or
  contradict any of those 26 Decisions, and found no new BLOCKED question, except one genuine
  micro-decision (wire-header magic/version validation, `TASK-24H-0176`) which was asked of the
  user via `AskUserQuestion` before implementing, per this project's standing policy - recorded as
  `docs/directplay-design.md` Decision 27, following the same one-question-per-Decision format as
  20-26. `TASK-24H-0057` (formerly `PARTIAL`, DirectSound's `DSERR_NODRIVER` path) was the backlog's
  last non-`DONE` item across every prior session - closed this session by `TASK-24H-0181`'s
  dedicated fresh-process test binary.
- **Important architectural decisions** (full narrative + rationale for the DirectPlay ones lives
  in `docs/directplay-design.md`, Decisions 1–19; deviation summary in
  `docs/directplay-limitations.md`, new this session):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only. No SDL3/ENet/SDL3_net symbol may ever appear in them — automatically enforced by the
    `header_hygiene` CTest test (`tests/check_header_hygiene.sh`), not just manual grep.
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`, 13 methods), with two concrete backends selected at
    **build time** (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option:
    `LoopbackDirectPlayTransport` (in-process, deterministic, used for all committed tests) and
    `EnetDirectPlayTransport` (real ENet UDP sockets - now with committed transport-level tests,
    `tests/enet_directplay_tests.cpp`, new this session).
  - DPID `0` is assigned to the host's own first local player (not reserved as
    `DPID_ALLPLAYERS`), matching `free-eggbert`'s own comparison pattern — a deliberate deviation
    from real DirectPlay that creates a **proven** bug for broadcast: see Section 5. `DPID` is now
    `static_assert`-enforced to be exactly 4 bytes (`plan.md` TASK-24H-0020, this session).
  - Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
    transport entirely.
  - **DirectDraw/DirectDrawSurface/DirectSound/DirectSoundBuffer implementation classes live in
    anonymous namespaces with no separate header** (unlike some DirectPlay internals) — all tests
    for these two subsystems go through the real public `IDirectDraw*`/`IDirectSound*` interfaces
    only, never whitebox.
  - Sanitizer builds (`FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`, new this session) and
    per-flag compile-time debug-log force-enables (`FREE_DIRECT_FORCE_DEBUG_*`, 7 flags, new this
    session) are both opt-in, `PRIVATE` to `free-direct`'s own targets, and OFF by default.

## 2. Current status

**Build status: working**. This session's implementation work (29-task audit-hardening batch, then
a 3-task follow-up batch) build+test-verified the default standalone configuration before marking
*every single task* `DONE` (one fresh `/tmp` scratch build per task, 32 in total this session), and
additionally verified the `-DFREE_DIRECT_ENABLE_ENET=ON` configuration for every task that touched
ENet-gated code, plus `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON` runs for the
tasks most likely to expose a real memory-safety issue (`TASK-24H-0172`'s self-send fix, verified
via a deliberate revert-then-restore cycle to prove the sanitizer actually catches the regression;
`TASK-24H-0180`'s combined-subsystem teardown-ordering tests, the scenario most likely to expose a
real use-after-free/double-free if one existed). The free-eggbert/planetblupi out-of-tree builds and
the bare-standalone error path were re-confirmed at specific points during this session, not on
every single task - see Section 3 for exactly which task verified what, rather than assuming
everything was re-checked on every commit.

**Test status: real counts, `grep`-verified against the actual test files, not recalled from
memory, as of the end of all of this session's implementation work:**
- `directdraw_tests` (label `directdraw`) — **59/59** passing (was 53 at the start of this
  session's DirectDraw batch).
- `directsound_tests` (label `directsound`) — **35/35** passing (was 30).
- `directsound_nodriver_test` (label `directsound`, new this session, `TASK-24H-0181`) — **1/1**
  passing; its own dedicated fresh-process binary, not a `Test_*` function inside
  `directsound_tests`, closing out `TASK-24H-0057`.
- `integration_tests` (label `integration`, new this session, `TASK-24H-0180`) — **4/4** passing.
- `directplay_tests` (label `directplay`) — **68/68** passing (was 65).
- `enet_directplay_tests` (label `enet`, opt-in, only built with `FREE_DIRECT_ENABLE_ENET=ON`) —
  **8/8** passing, unchanged this session (no new ENet-only tests were added).
- `header_smoke_ddraw` / `header_smoke_dsound` / `header_smoke_dplay` / `header_hygiene` (label
  `headers`) — all passing.

Total: **174 individual `Test_*` functions** across five hand-written multi-test binaries
(59 + 35 + 4 + 68 + 8), plus `directsound_nodriver_test`'s own single top-level assertion, all
passing, plus the 4 header-compile/hygiene checks. Default (non-ENet) `ctest` now registers **9**
tests total (was 7 at the start of this session); ENet-enabled adds a 10th.

**Available artifacts** (`DirectPlayPlayer.{hpp,cpp}` removed this session, `TASK-24H-0175` -
confirmed dead scaffolding, zero call sites anywhere; two new test binaries added, see above):
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`); **confirmed running correctly this session**
  (`TASK-24H-0182`), not merely compiling - see "What does not work yet" below for the one caveat.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/directsound_nodriver_test.cpp`, `tests/integration_tests.cpp`,
  `tests/enet_directplay_tests.cpp`, `tests/header_smoke_*.cpp`, `tests/check_header_hygiene.sh` —
  all CMake/CTest-wired.

**What does not work yet:** nothing tracked as a `plan.md` task - **every task in the 182-task
backlog is `DONE`**, including `TASK-24H-0057` (`DSERR_NODRIVER`, `PARTIAL` for multiple sessions,
finally closed this session by `TASK-24H-0181`'s dedicated fresh-process test binary). Two things
remain genuinely open but were never tracked as backlog items:
- Host migration (`DPSESSION_MIGRATEHOST`) — silently ignored, was never one of the 7 Track B
  questions, still genuinely open (unchanged this session).
- The `FREE_DIRECT` demo's `player.png` sprite asset is missing in this environment
  (`src/Main.cpp:140`), producing two non-fatal "failed to load image" warnings at startup - a
  missing test fixture, not a FreeDirect defect. Confirmed the render loop itself (frame
  presentation, FPS reporting) runs correctly regardless, via its own climbing `present_count`/
  stable `FPS` log output (`TASK-24H-0182`).
- Everything else that was previously listed here as "BLOCKED on a human decision" (ENet joining/
  discovery, broadcast, host routing, player names, duplicate-player/player-lost semantics) was
  resolved in an earlier session (Track B, `docs/directplay-design.md` Decisions 20-26) and is no
  longer accurate to list as blocked - see `git log`/earlier `NEXT.md` history if that resolution's
  own detail is needed.

## 3. Recent changes

**This session** (2026-07-09, three phases: audit + planning first (items 1-6 below), a full
29-task implementation batch closing every task those audits produced (item 7), then a follow-up
cross-cutting analysis pass and its own 3-task implementation batch (item 8), leaving the entire
backlog fully closed):

1. **Deep DirectDraw-only audit** (`docs/audit_ddraw.md`, new file): performance, memory safety,
   correctness-against-DirectDraw-semantics, and code-quality analysis of `src/directdraw/
   DirectDraw.cpp`/`include/ddraw.h`, cross-checked against real call sites in both
   `../free-eggbert` and `../planetblupi` (never assumed from memory). Two findings were measured
   empirically against the real compiled library, not just reasoned about:
   - The project's own default build (`cmake ..`, no `CMAKE_BUILD_TYPE` set anywhere in
     `CMakeLists.txt`) compiles with **no optimization flags at all** - measured **6.6x** slower
     than `-DCMAKE_BUILD_TYPE=Release` on the same hot path. Affects the whole library, not just
     DirectDraw.
   - `BlitFrom` (shared by `Blt`/`BltFast`) has no 1:1 (no-scale) fast path - measured **29x**
     slower than a raw `memcpy` of the same bytes at `-O3` (**206x** at the project's actual
     default/unoptimized build) for a 640x480 1:1 `BltFast` call. Confirmed reachable by both
     games' real, everyday code paths (every present, every sprite draw), unlike most other
     findings in this audit.
   - Several further real, code-confirmed defects were found and are currently latent (not
     reachable by either game's actual call sites today, verified by tracing, not assumed): an
     integer-overflow bypass of `DirectDrawPaletteImpl::GetEntries`/`SetEntries`'s bounds check (an
     out-of-bounds write in the `SetEntries` case), `CreateSurface` accepting unbounded
     `dwWidth`/`dwHeight` into an unguarded `std::vector::resize` (can throw uncaught, crashing the
     process - a real violation of this project's own "no exceptions cross the interface boundary"
     rule), `SetCooperativeLevel` leaving a stale `SDL_Texture*` dangling against a destroyed
     renderer if ever called twice with a surface already presented, and `FillColor`'s 8-bit branch
     skipping the `MarkDirty()` call the 32-bit branch reaches.
   - Full findings table, severity/reachability grading, benchmark methodology, and a summary of
     what's *not* yet fixed are in `docs/audit_ddraw.md` - this audit changed no code.
2. **13 new `plan.md` tasks added**, `TASK-24H-0151` through `TASK-24H-0163`, one per DirectDraw
   audit finding worth fixing, following the existing `TASK-24H-XXXX` format exactly (Status/
   Priority/Area/Type/Evidence/Problem/Required work/Acceptance criteria/Out of scope). All
   `Status: TODO` - **none implemented yet**, this was explicitly a plan-then-stop session for this
   batch.
3. **Deep DirectSound-only audit** (`docs/audit_dsound.md`, new file), explicitly structured around
   performance/correctness/memory/extreme-situation-edge-cases/risk-analysis, ending with a
   proposed-tasks list, per an explicit user request for that exact structure. Also cross-checked
   against real call sites in both target games. This audit did **not** find an algorithmic
   hot-path defect like DirectDraw's `BlitFrom` - `Play()` was measured directly at 0.0016ms/call
   for a realistic 2-second SFX buffer, ruling that class of finding out rather than assuming it.
   The two headline findings instead:
   - `CreateSoundBuffer`'s `dwBufferBytes` is never bounded before an unguarded
     `std::vector::resize` - the same *class* of defect as `TASK-24H-0154` (DirectDraw's
     `CreateSurface`), but **more concretely evidenced**: both target games read this value
     directly from an on-disk `.wav` file's own header field (`wavHdr.dwDSize`) with zero
     validation before it reaches FreeDirect, making a corrupted/truncated asset file a genuinely
     plausible trigger, not just a hypothetical caller.
   - A full `IDirectSound` device close+reopen cycle (the sole owner releasing, then a fresh
     `DirectSoundCreate`) measured **~51ms per cycle**, vs. ~0.00006ms when another instance keeps
     the device open (roughly 850,000x difference) - confirmed not triggered by either target
     game's actual create-once/release-once lifecycle, but a real, dramatic, previously-unmeasured
     cost worth knowing about.
   - Smaller findings: an unsynchronized read in `SharedAudioDevice::id()` (confirmed inert - no
     DirectSound call site in either game runs on a secondary thread), no documented
     single-threaded-usage assumption (mirrors the DirectDraw audit's equivalent finding), `Lock()`
     silently clamping an out-of-range offset instead of erroring, `Unlock()` never invalidating
     the pointer `Lock()` returned, and an extreme `nSamplesPerSec` reaching SDL3 unchecked with
     its actual consequence left honestly unverified (not overclaimed as a specific crash).
4. **8 more `plan.md` tasks added**, `TASK-24H-0164` through `TASK-24H-0171`, one per DirectSound
   audit finding worth fixing (same format as item 2). All `Status: TODO` - **none implemented
   yet**.
5. **Deep DirectPlay-only audit** (`docs/audit_dplay.md`, new file). DirectPlay already had 26
   resolved design Decisions and extensive prior documentation
   (`docs/directplay-design.md`/`-limitations.md`/`-protocol.md`/`networking-backends.md`/
   `-callsite-audit.md`) before this audit started - a background research pass digested all of it
   first specifically so this audit doesn't re-derive settled ground; none of the 26 Decisions were
   reopened or contradicted. The single most important finding is a **framing fact, not a defect**:
   `CDecor::TreatNetData()` (`../free-eggbert/src/decnet.cpp:87`) - the per-frame packet pump that
   would actually invoke `Send()`/`Receive()` during an open session - has its one call site
   commented out (`event.cpp:2045`). This sharpens the already-known fact that free-eggbert's
   DirectPlay UI entry points are unreachable: it's not just that a session can never be *opened*
   via the UI, the gameplay-loop mechanics that would *drive* an already-open session don't run
   either. Every other finding in this audit is therefore confirmed unreachable by any currently-
   running free-eggbert code path, not merely "unreachable via specific menus." Findings, all
   graded with that context:
   - `Send()`'s self-send path (`idTo == idFrom`) reads `dwDataSize` bytes from the caller's buffer
     via `.assign()` *before* checking it against `kMaxPayloadBytes` - unlike the broadcast and
     unicast paths, both of which check first. A real, precisely-located inconsistency (an
     out-of-bounds read if `dwDataSize` overstates the real buffer) - not reachable via
     free-eggbert's real traffic (its one `Send()` call site always passes `idTo=0`, routing
     through broadcast, never self-send) and not caught by the existing
     `Test_SelfSend_OversizedPayload_ReturnsSendTooBig` test (which happens to use an
     honestly-sized oversized buffer, so it never exercises the ordering issue).
   - Two stale-documentation findings: `include/dplay.h`'s top-of-file comment says broadcast
     "does not work correctly yet," directly contradicting the `Send()` method's own, correct doc
     comment 270 lines below it in the same file; `docs/networking-backends.md` still claims ENet
     joining/discovery "does not work today," contradicted by the already-implemented Decisions
     22/23.
   - `DirectPlayPlayer` (`.hpp`+`.cpp`) reconfirmed as genuinely dead, zero-member scaffolding,
     superseded early on by `DirectPlaySession`'s simpler plain-`DPID`-vector design.
   - The wire header's deliberate `magic`/`version` validation skip was originally deferred "until
     a real transport exists" - `EnetDirectPlayTransport` now is that real transport and receives
     real UDP packets, so this deferral's own precondition has been met and deserves a fresh
     decision either way.
   - `DirectPlayDiscoveryService`'s raw-socket LAN discovery responder (a deliberate Decision-23
     design choice, bypassing `ENetHost`/`ENetPeer`'s protocol-level gating for a stateless
     announce/reply exchange) validates only packet size/type, not magic/version or any
     authentication, and unicasts a real reply to whatever source address a request claims - a
     structurally-present, low-amplification UDP reflection primitive. Low real-world stakes given
     the feature's explicit LAN-only scope, but undocumented until now.
   - `EnetDirectPlayTransport::Service()` and the discovery responder both have unbounded
     "drain everything pending" loops with no per-call iteration cap - a real robustness gap under
     a high incoming-packet rate, the one part of this codebase genuinely exposed to arbitrary
     network input volume.
   - Two items checked and found *not* to be problems, empirically: `Receive()`'s per-call ~4.1KB
     buffer allocation measured at 228.7ns/call (negligible at any realistic frequency); a
     `Send()`+`Receive()` self-send round trip measured at 0.5µs/cycle.
6. **8 more `plan.md` tasks added**, `TASK-24H-0172` through `TASK-24H-0179`, one per DirectPlay
   audit finding worth fixing, written more tersely than items 2/4's tasks per explicit request for
   condensed entries this round.
7. **All 29 audit-hardening tasks implemented, tested, and committed** (`TASK-24H-0151`-`0179`,
   13 DirectDraw + 8 DirectSound + 8 DirectPlay), one task per commit, each independently
   build+test verified before being marked `DONE` in `plan.md` (no batch-marking, no speculative
   completion). Highlights, in the order done:
   - **`TASK-24H-0151`** (default `CMAKE_BUILD_TYPE` to `Release` when unset) - scoped to the
     top-level-project case only, confirmed via a fresh out-of-tree `../free-eggbert` build that a
     consuming project's own build-type choice is never overridden.
   - **`TASK-24H-0152`** (`BlitFrom` 1:1 fast path) - measured `BltFast` improving from 0.715ms to
     0.466ms/call (29.4x → 14.8x slower than raw `memcpy`) at `-O3` for a 640x480 copy.
   - **`TASK-24H-0154`/`0155`** (DirectDraw `CreateSurface` size bound; `Palette::GetEntries`/
     `SetEntries` integer-overflow bounds-check fix) and **`TASK-24H-0164`** (DirectSound
     `CreateSoundBuffer` `dwBufferBytes` bound, the single most concretely-evidenced finding
     across all three audits) - all three close a real crash/OOB-write class of gap.
   - **`TASK-24H-0172`** (`Send()`'s self-send path validation order) - proved the new regression
     test actually has teeth: temporarily reverted the fix, rebuilt under
     `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON`, and ASan immediately caught a
     real `stack-buffer-overflow in memcpy`; restored the fix and confirmed a clean ASan+UBSan run.
   - **`TASK-24H-0176`** (wire-header `magic`/`version` validation) - the one task in this whole
     batch that was a genuine design decision, not a mechanical fix; asked the user via
     `AskUserQuestion` before implementing (matching this project's standing DirectPlay-decision
     policy) rather than presuming an answer. User chose to add the check.
   - **`TASK-24H-0157`** (`FillColor`'s `MarkDirty` fix) and **`TASK-24H-0156`/`0161`**
     (`SetCooperativeLevel` stale-texture guard + `owner_`'s lifetime contract, resolved together
     since the fix gave `owner_` a real use) both needed a genuine 8-bit-primary-surface
     construction path that didn't exist yet - added the minimal, already-pre-authorized
     `DDSD_PIXELFORMAT`-honoring consistency fix to `CreateSurface`'s primary branch to unblock
     the regression tests, changing no behavior for either target game.
   - **`TASK-24H-0175`** (removed `DirectPlayPlayer.{hpp,cpp}`) - the one task involving file
     deletion; confirmed via a fresh repo-wide `grep` that nothing else referenced it before
     deleting, then verified both the default and ENet-enabled configurations still build clean.
   - Two tasks found and fixed real bugs *in their own new test code*, not the production fix
     itself, during verification - both caught by actually running the tests, not assumed passing:
     `TASK-24H-0171` (100-buffer stress test's first version used buffers too short to survive the
     test's own loop overhead under the dummy audio driver) and (see above) `TASK-24H-0172`'s
     ASan-verification step.
   - Every task's `plan.md` entry has its own detailed `Verified:` paragraph (build/test commands,
     exact before/after numbers where measured, what was and wasn't re-checked) - this summary is
     necessarily condensed; `plan.md`'s "DirectDraw/DirectSound/DirectPlay audit hardening
     (2026-07-09)" sections are the authoritative per-task record.
8. **A follow-up cross-cutting gap-analysis pass, then all 3 tasks it produced implemented and
   committed** (`TASK-24H-0180`-`0182`), closing the backlog to **182/182 DONE, 0 TODO, 0 PARTIAL,
   0 BLOCKED**. Requested explicitly by the user as a fresh "analyze current state, propose
   improvements" pass, still bounded to `free-eggbert`/`planetblupi`'s real needs. Two independent
   research passes ran first (a ground-truth build/test/regression re-verification, and a
   fresh-eyes gap analysis explicitly looking for issues the three subsystem-scoped audits above
   could not have found by construction, per `CLAUDE.md`'s atomicity rule) before any task was
   proposed to the user for approval:
   - **`TASK-24H-0180`** (new `tests/integration_tests.cpp`, 4 tests): both target games call
     `DirectDrawCreate`/`DirectSoundCreate` unconditionally at startup, so both subsystems are
     simultaneously live for every real game session - but every existing test binary and the demo
     itself only ever exercised one subsystem at a time. Added real `BltFast`+present interleaved
     with a real `Play()`, verified via genuine rendered-pixel readback and `DSBSTATUS_PLAYING`
     respectively, plus both teardown orderings. Found and fixed two bugs in this task's own new
     test code (not a product bug): pixel reads at physical `(0,0)` instead of the established
     `(10,10)` convention, and a packed-hex-literal expected-pixel value with R/B channel bytes
     reversed - the same class of mistake already documented once this session for
     `TASK-24H-0156`'s test, made again here before being caught by actually running the test and
     switching to the safer per-channel-extraction comparison style.
   - **`TASK-24H-0181`** (new `tests/directsound_nodriver_test.cpp`): closed `TASK-24H-0057`, the
     **only** non-`DONE` item anywhere in the backlog, open since an earlier session. A standalone
     probe proved *why* no in-process trick could ever work (SDL3's audio-driver init outcome is
     sticky for a process's entire lifetime, even across an explicit `SDL_QuitSubSystem`+re-init
     with a different driver), then added a dedicated fresh-process CTest binary instead. Proved the
     new test has real teeth (not vacuously true) by manually running it under both a real driver
     (correctly fails) and the bogus one (correctly passes) - same verification spirit as
     `TASK-24H-0172`'s ASan revert/restore proof earlier in this session.
   - **`TASK-24H-0182`** (documentation): ran the `FREE_DIRECT` demo headlessly for the first time
     ever (previously only confirmed to compile) - stable ~52-53 FPS, no crash. Independently
     re-verified this claim a second time before writing it into `plan.md`/`NEXT.md`, per this
     project's practice of not trusting a sub-agent's summary at face value: the original follow-up
     analysis reported "zero errors/warnings," but a direct re-run found two non-fatal
     missing-asset warnings (`player.png`) - corrected in both `plan.md` and here rather than
     carrying forward the more sweeping original claim.
   - All three verified across default, ENet-enabled, and ASan+UBSan configurations (9/9, 1/1, 9/9
     respectively - default `ctest` grew from 7 to 9 registered tests this session).

**Prior session (3)** (2026-07-08, continuation implementation session - closed 74 more
`TASK-24H-XXXX` tasks, from 64 to 138 DONE (every safe task in the backlog at the time); no new
audit, no new plan created, per explicit instruction):

1. **DirectDraw**: added `Flip`/presentation-throttle/clipper-one-time-init tests (45→53), closing
   `TASK-24H-0046`/`0047`/`0050`-`0055`. Introduced `ReadPresentedPixel()` (a genuine black-box
   technique using `SDL_GetRenderer`/`SDL_RenderReadPixels` to verify *actually-rendered* pixels,
   not just the CPU buffer) and `FREE_DIRECT_TARGET_FPS`-based deterministic throttle testing.
2. **DirectSound**: added 6 buffer-lifetime/`Play`/`Stop`/ref-count edge-case tests (24→30),
   closing `TASK-24H-0142`-`0147`.
3. **DirectPlay**: added 12 new tests (49→61) - null-argument validation, `Close()`-twice safety,
   oversized-payload rejection, and a real port-rebind-after-`Close()` proof. **Found and fixed a
   real crash-risk bug** while sweeping for null-pointer issues (`TASK-24H-0106`/`0107`): `Send()`
   never checked `lpData` for null before `bytes + dwDataSize` pointer arithmetic when
   `dwDataSize > 0` - undefined behavior, now guarded with `DPERR_INVALIDPARAMS`.
4. **ENet transport tests** (`tests/enet_directplay_tests.cpp`, new file, 4 tests,
   `TASK-24H-0097`/`0098`/`0108`): whitebox tests directly against `EnetDirectPlayTransport` (not
   through `IDirectPlay2A`), hardcoded distinct ports to sidestep the still-BLOCKED host-discovery
   question. Also found and fixed a **stale doc comment** in `EnetDirectPlayTransport.hpp` claiming
   `Receive()` "always returns false" - contradicted by Decision 19's real buffering, landed in a
   prior session but never reflected in this comment.
5. **Sanitizer builds** (`TASK-24H-0010`): added `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN`
   CMake options, `PRIVATE` to `free-direct`'s own targets only. **The first sanitizer run
   immediately caught a real bug**: `DirectPlayMessageQueue.hpp`'s `TryReceive()` passed an empty
   `std::vector::data()` (which may be `nullptr`) as `memcpy`'s nonnull-declared `src` argument for
   zero-length messages - undefined behavior even at `count == 0`. Fixed by skipping the `memcpy`
   call when `payloadSize == 0`. Audited every other `memcpy`/`memcmp`/`memmove` call site in
   `src/`/`include/` for the same pattern - this was the only instance. Re-verified across 4
   scratch-build configurations after the fix; all pass clean.
6. **Debug-flag CMake overrides** (`TASK-24H-0119`/`0120`): added `#ifdef`-based compile-time
   override support to DirectDraw's 5 debug flags (mirroring DirectSound's pre-existing pattern),
   plus 7 `FREE_DIRECT_FORCE_DEBUG_*` CMake options. Verified the mechanism actually changes
   behavior (not just compiles) by building with a force-flag on and confirming the corresponding
   zero-log regression test now fails as expected. README's flag documentation brought to full
   parity with source (`FREE_DIRECT_DEBUG_DSOUND_FORMAT`/`FREE_DIRECT_DIAGNOSTICS` were
   undocumented before).
7. **Header hygiene/documentation** (`TASK-24H-0020`-`0023`, `0025`): added
   `static_assert(sizeof(DPID) == 4)`; documented `DDBLT_COLORFILL`/`KEYSRC`/`ROTATIONANGLE` and
   `DSBCAPS_STATIC`'s real call-site status inline. **`TASK-24H-0022`'s own evidence turned out to
   be stale** - it claimed `DPESC_TIMEDOUT`/`DPSESSION_KEEPALIVE`/`DPSESSION_MIGRATEHOST` were
   unused, but direct verification found real call sites for all three in `free-eggbert`'s
   `network.cpp`. Corrected with accurate comments instead (see Section 5). Also found (not fixed,
   inert today): `DDBLT_ROTATIONANGLE`'s value doesn't match `free-eggbert`'s own vendored SDK
   header. `TASK-24H-0025` confirmed the *only* new public declaration added all session is the
   `static_assert` itself - zero unjustified surface crept in.
8. **Build/test documentation** (`TASK-24H-0011`/`0012`): rewrote README's "Build Instructions" -
   it was itself stale (first example was exactly the config that fails; documented system-SDL flag
   name didn't exist). New content: 5 configurations, each actually run this session, including the
   verified free-eggbert/planetblupi out-of-tree builds. Added `docs/ci-matrix.md` (non-blocking
   note, no `.github/workflows` added).
9. **DirectPlay documentation** (`TASK-24H-0094`/`0095`/`0096`): created
   `docs/directplay-limitations.md` (19-Decision deviation table + all 7 standing BLOCKED
   questions listed together), `docs/directplay-protocol.md` (wire header layout), and
   `docs/networking-backends.md` (`IDirectPlayTransport` abstraction + backend tradeoffs). **Caught
   a real error before publishing** `directplay-protocol.md`: hand-derived the header size as 56
   bytes assuming a 16-byte `GUID` (the real Win32/LLP64 value); compiling a verification program
   directly against `DirectPlayWireProtocol.hpp` found `sizeof(GUID) == 24` on this Linux/LP64
   build (`unsigned long` is 8 bytes here) and the true header size is **72 bytes** - documented as
   a new, previously-unwritten "Known gap" (wire size is platform/ABI-dependent).
10. **README compatibility pass** (`TASK-24H-0121`/`0122`/`0125`-`0130`): rewrote both DirectPlay
    bullets (was "dummy stubs", now accurately describes real loopback+ENet-hosting behavior and
    explicitly lists what's NOT implemented); added a "Compatibility Status" section with a
    per-method STUB/PARTIAL/IMPLEMENTED table sourced directly from header `@note Status:` tags;
    cross-linked `docs/audit-24h-free-direct.md`; swept all docs for accidental "full DirectX 3
    compatibility" and "Microsoft DirectPlay wire-compatible" claims (zero found - every match was
    a correct disclaimer); checked all 11 `plan.md` Phase 16 documentation checkboxes, each with a
    citation.
11. Every change above was verified by an actual build+test run in a fresh `/tmp` scratch
    directory before being marked DONE - never assumed. `plan.md` has a `Verified:` note under
    every closed task's acceptance criteria citing exactly what was tested.
12. **Final verification pass and recurring-task consolidation** (`TASK-24H-0005`-`0008`,
    `0013`-`0015`, `0076`, `0138`-`0141`): closed 12 process/recurring-verification tasks whose
    discipline had been followed all session but never formally checked off, plus the 4 dedicated
    "end of session" final-integration-reverification tasks. Ran the complete verification matrix
    fresh, one more time, from clean `/tmp` scratch directories: default build (`ctest` 7/7),
    ENet-enabled (`ctest -L enet` 1/1; unfiltered `ctest` against the same build still fails
    `directplay_tests` 1/8, confirmed still the same documented by-design incompatibility, not a
    new regression), ASan+UBSan loopback-only (7/7 clean), ASan+UBSan+ENet (7/7 clean, ENet
    suite excluded via label filter), a full out-of-tree `../free-eggbert` rebuild
    (`SPEEDY_BLUPI_WINDOWS`, exit 0), a full out-of-tree `../planetblupi` rebuild
    (`PLANET_BLUPI_WINDOWS`, exit 0), and a final `header_hygiene` re-check (clean). Confirmed
    `git status` clean and fully pushed to `origin/develop` before writing this final `NEXT.md`
    update.
13. **`TASK-24H-0100`: real `DirectPlayEnumerateA`/`W` implementation** - the last safe TODO task
    in the entire backlog, done after a user check-in following item 12's final report. Both
    functions now invoke their callback exactly once with a FreeDirect-internal placeholder
    provider (GUID `{2}`, name `"FreeDirect"` in both ANSI and manually-spelled-`WCHAR` UTF-16
    encodings - `WCHAR` is `uint16_t` per `free-api`, not the native 4-byte `wchar_t`, so a plain
    `L"..."` literal would have been the wrong width), matching Decision 1's already-decided shape
    exactly. Return `DPERR_INVALIDPARAMS` for a null callback, matching every other callback-taking
    method in this file. `include/dplay.h`'s two `@note Status:` tags updated `STUB`→`IMPLEMENTED`.
    `TASK-24H-0093`'s two stale "invokes callback zero times" tests were rewritten (not left
    stale) into 4 new tests asserting the real behavior (61→63 `directplay_tests`). Verified: fast
    `g++` loop (63/63), full CMake `ctest` (7/7), ASan+UBSan `ctest` (7/7 clean - re-checked given
    this session's earlier real bug in this same file), a full out-of-tree `../free-eggbert`
    rebuild (exit 0), and `header_hygiene` (clean).
14. **All 7 Track B DirectPlay design questions asked, decided, and implemented** - the user
    explicitly asked to proceed into them after item 13's report. Presented via `AskUserQuestion`
    (never decided unilaterally), recorded as `docs/directplay-design.md` Decisions 20-26, and
    3 new tasks added/closed (`TASK-24H-0148`/`0149`/`0150`):
    - **Decision 20/21 + `TASK-24H-0148`** (broadcast + host routing): `idTo == 0`
      (`DPID_ALLPLAYERS`, new `include/dplay.h` constant) now always means broadcast, checked
      before self-send since both can match for the host specifically. Host role iterates
      `remotePlayerIds` directly; a joining role's broadcast reaches the host, which relays it to
      every *other* connected peer (never back to the sender). Found and fixed a real,
      unanticipated consequence: 5 pre-existing tests used the host's first (always-DPID-`0`)
      `CreatePlayer()` result as a generic "some player ID" for self-send testing, which silently
      broke once broadcast took priority - fixed each by creating a second player first.
      `TASK-24H-0092`'s old "proves the collision" characterization test rewritten into
      `Test_HostBroadcast_ReachesAllRemoteClientsNotSelf`. 63→65 `directplay_tests`.
    - **Decision 22 + `TASK-24H-0149`** (ENet host discovery): a new `FREE_DIRECT_ENET_HOST_ADDRESS`
      env var (`"<host>"` or `"<host>:<port>"`), read once by `Open()`'s ENet joining branch -
      previously completely unwired. 4→6 `enet_directplay_tests`.
    - **Decision 23 + `TASK-24H-0150`** (LAN UDP discovery, the most architecturally novel piece):
      new `DirectPlayDiscoveryService` (`src/directplay/DirectPlayDiscovery.hpp`/`.cpp`), built on
      ENet's own portable `ENetSocket`/`enet_socket_*` primitives (not hand-rolled per-platform
      sockets, not `ENetHost`/`ENetPeer`). A hosting session listens on a new fixed port (`51323`)
      and replies to `Discovery` broadcasts; `EnumSessions()` gained an additive broadcast-and-
      collect phase using `dwTimeout` for real (its first actual use anywhere in this codebase).
      Found and fixed two real issues in this task's own test code (not the implementation): a
      dangling-pointer read of a `DPSESSIONDESC2` field past its callback-only-valid lifetime, and
      two new tests that were written but never registered in `main()` so they silently never ran.
      4→8 `enet_directplay_tests`.
    - **Decisions 24/25/26** (player names, duplicate-player detection, player-lost state): all
      three decided **not needed** - no code changes required.
    - Every piece verified through the full matrix: default build, ASan+UBSan (clean, zero
      diagnostics, directly grepped raw output each time), ENet-enabled build, ASan+UBSan+ENet
      combined, `header_hygiene`, and a full out-of-tree `../free-eggbert` rebuild after each of
      the three implementation commits.

**Prior session (2)** (DirectDraw/DirectSound test-coverage focus - condensed; full detail was
here before this rewrite, still in `git log`): added `tests/directdraw_tests.cpp` (45 tests) and
`tests/directsound_tests.cpp` (24 tests), both new from zero; `docs/directsound-limitations.md`;
discovered this environment has usable system SDL3 (`-DFREE_API_USE_SYSTEM_SDL3=ON`), contradicting
an earlier audit finding. Left `TASK-24H-0057` (`DSERR_NODRIVER` path) `PARTIAL` - `SharedAudioDevice`'s
SDL audio driver selection is process-lifetime-sticky, not reliably forceable in-process without a
subprocess harness.

**Prior session (1)** (audit + plan extension + 20 initial `TASK-24H-*` implementations - see
`docs/audit-24h-free-direct.md` for the full audit and `git log` for exact commits): produced the
initial audit, appended the original 141-task `plan.md` backlog (since grown to 147), CMake/CTest
wiring for `directplay_tests`, header hygiene automation, and the DPID-0 self-send/broadcast
collision characterization test.

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker and no BLOCKED design question.** See the top of this
file for the exact current task count - as of the last count there, a handful of maintainability
tasks (`TASK-24H-0183`-`0188`) were still `TODO`/in progress, the first time this backlog has had
any open item since the 182-task milestone below. Everything builds, all 174 committed `Test_*`
checks pass (68 directplay + 59 directdraw + 35 directsound + 4 integration + 8 ENet, opt-in) plus
`directsound_nodriver_test`'s own single assertion, plus 4 header-level checks, across every
verified configuration (default, ENet, ASan+UBSan, both target games) - re-verify after the
maintainability batch finishes, since two of its tasks touch production code.

**The DirectPlay design fork described in every prior session is resolved.** All 7 standing
BLOCKED design questions (DPID-0 broadcast semantics, ENet host discovery, LAN discovery, host
routing, player names, duplicate-player semantics, player-lost state) were presented to the user
via `AskUserQuestion` in an earlier session (never decided unilaterally), and the user chose real
implementation for 4 of them and "not needed" for the other 3. All are recorded as
`docs/directplay-design.md` Decisions 20-26 and, for the 4 requiring code, implemented and tested
(`TASK-24H-0148`/`0149`/`0150`).

**This session (2026-07-09) ran three fresh subsystem audits, closed everything they found, then a
follow-up cross-cutting pass closed the rest of the backlog.** `docs/audit_ddraw.md`,
`docs/audit_dsound.md`, and `docs/audit_dplay.md` together produced 29 tasks, `TASK-24H-0151`-`0179`
(13 DirectDraw + 8 DirectSound + 8 DirectPlay), all implemented. One task (`TASK-24H-0176`,
wire-header `magic`/`version` validation) was a genuine design decision, asked via
`AskUserQuestion` rather than assumed - user chose to add the check, recorded as
`docs/directplay-design.md` Decision 27. Then, at the user's explicit request for a fresh
"analyze current state, propose improvements" pass, a follow-up cross-cutting gap analysis (looking
specifically for issues the three subsystem-scoped audits couldn't have found by construction)
produced 3 more tasks, `TASK-24H-0180`-`0182`, also all implemented - closing the entire backlog
for the first time (182/182). A dedicated maintainability audit then produced 6 more tasks,
`TASK-24H-0183`-`0188` (see the top of this file for whether that batch has finished, and `plan.md`
directly for each task's own `Verified:` paragraph - a Section 3 narrative entry for this batch
will be added once it's complete, matching how items 7/8 below were each written only after their
own batch finished). See Section 3 items 7-8 for the two completed batch summaries, and Section 5
for what each of their fixes changed.

**`TASK-24H-0057`** (`DSERR_NODRIVER` graceful-failure path) - `PARTIAL` for multiple sessions,
finally closed this session by `TASK-24H-0181`'s dedicated fresh-process CTest binary
(`tests/directsound_nodriver_test.cpp`), after a standalone probe proved no in-process mechanism
could ever have worked. This was the last non-`DONE` item anywhere in the backlog.

**Still genuinely open, but never tracked as any `TASK-24H-XXXX` item** (neither a bug nor part of
the 7 resolved Track B questions): host migration only (`free-eggbert` sets
`DPSESSION_KEEPALIVE`/`MIGRATEHOST` when hosting, but FreeDirect's `Open()` never reads
`DPSESSIONDESC2::dwFlags` at all). The `FREE_DIRECT` demo's runtime behavior, previously listed here
as unverified, was confirmed working this session (`TASK-24H-0182`) - see Section 2.

**Minor, unchanged**: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` still unexercised in this environment (no
system `libenet` package) - the vendored `third_party/enet` path is the one actually verified.

## 5. Known bugs and limitations

**DirectPlay** (all 7 Track B questions resolved this session - see Section 3, item 14, and
`docs/directplay-design.md` Decisions 20-26):
- **Fixed, not just documented**: the host's real broadcast call (`Send(m_dpid, 0, ...)`,
  `free-eggbert`'s only reachable `Send()` pattern) no longer collides with self-send - `idTo ==
  DPID_ALLPLAYERS` (`0`) is now checked first and always means broadcast (Decision 20), with
  host-side relay so non-host peers can reach each other too (Decision 21). The old
  characterization test proving the collision was rewritten (not deleted) to prove the fix.
- ENet joining is wired via `FREE_DIRECT_ENET_HOST_ADDRESS` (Decision 22); real LAN discovery
  exists via `DirectPlayDiscoveryService` (Decision 23, raw UDP on ENet's own portable sockets).
- Player names, duplicate-player detection, and a distinct player-lost state were all decided
  **not needed** (Decisions 24/25/26) - no code changes, `IDirectPlay2A`'s public surface is
  unchanged by these three.
- **Two new findings this session, both in `docs/directplay-limitations.md` now**:
  - `DPESC_TIMEDOUT`: `free-eggbert`'s `EnumSessionsCallback` really does check this flag (contrary
    to `TASK-24H-0022`'s own original, now-corrected, evidence), but FreeDirect's `EnumSessions()`
    is a synchronous registry lookup that never sets it - the check is real but unreachable from
    FreeDirect's side today. Not a bug: enumeration still terminates normally via `DP_OK`.
  - `DPSESSION_KEEPALIVE`/`DPSESSION_MIGRATEHOST`: `free-eggbert` really does set both when hosting,
    but `Open()` never reads `DPSESSIONDESC2::dwFlags` at all - silently ignored. Host **migration**
    (electing a new host if the original leaves) is a distinct, still-unimplemented feature from
    host **message routing** (relaying between two non-host peers, now implemented via Decision 21)
    - resolving Track B did not include a migration decision; this remains open but was not one of
    the 7 questions asked.
- **Fixed this session, not just documented**: `Send()`'s null-`lpData`-with-nonzero-size UB
  (pointer arithmetic on a null pointer), and `DirectPlayMessageQueue::TryReceive()`'s null-`src`
  `memcpy` UB for zero-length messages (found via ASan/UBSan). Both are real hardening of already-
  validated behavior, not new API surface.
- A dangling-pointer read of `DPSESSIONDESC2::lpszSessionNameA` past its callback-only-valid
  lifetime, found in this session's own new LAN-discovery test code (not the implementation) -
  fixed by copying the string content during the callback, matching the existing correct pattern
  already used by the loopback `EnumSessions` tests.
- **Fixed this session, from `docs/audit_dplay.md` (2026-07-09) - all 8 tasks
  `TASK-24H-0172`-`0179` now DONE**: `Send()`'s self-send path now validates `dwDataSize` against
  `kMaxPayloadBytes` *before* reading the caller's buffer, matching the broadcast/unicast paths
  (`TASK-24H-0172`, the only P1 in this batch - verified with a deliberate ASan-catches-the-revert
  check: reverted the fix, rebuilt under ASan+UBSan, confirmed a real `stack-buffer-overflow in
  memcpy` was caught, then restored the fix and confirmed a clean run). Two stale-documentation
  contradictions fixed: `include/dplay.h`'s top-of-file comment now matches its `Send()` method's
  own doc (`TASK-24H-0173`); `docs/networking-backends.md` now matches the already-implemented
  Decisions 22/23 (`TASK-24H-0174`). Confirmed-dead `DirectPlayPlayer` scaffolding removed
  entirely, `.hpp`+`.cpp` deleted after a fresh repo-wide grep found zero other references
  (`TASK-24H-0175`). The wire header now validates `magic`/`version` on deserialize
  (`TASK-24H-0176` - a genuine design decision, asked of the user via `AskUserQuestion` rather
  than decided unilaterally; user chose to add the check). The LAN discovery responder's
  low-stakes UDP reflection characteristic is now documented as a deliberate deviation in
  `docs/directplay-limitations.md` (`TASK-24H-0177`; no behavior change - the check confirmed
  magic/version validation now applies here too, thanks to `TASK-24H-0176`, but no authentication
  exists, which is documented rather than added, matching the task's LAN-only-scope framing).
  `Service()` and the discovery responder's drain loops are now capped at 64 events/requests per
  call instead of draining unboundedly (`TASK-24H-0178`). `Receive()` now reuses a persistent
  `wireBuf_` member instead of allocating a fresh ~4.1KB buffer every call (`TASK-24H-0179`,
  measured at 228.7ns/call before the fix - confirmed low-priority, fixed anyway as free
  consistency cleanup). **The audit's framing fact is unchanged by any of these fixes**:
  `CDecor::TreatNetData()` (`../free-eggbert/src/decnet.cpp:87`) - the per-frame pump that would
  call `Send()`/`Receive()` during an open session - still has its call site commented out
  (`event.cpp:2045`), so none of these fixes are exercised by free-eggbert's actual running code
  *today*. Per the user's explicit correction this session, that unreachability is **temporary**
  (tied to an in-progress decompilation of `free-eggbert`), which is exactly why these were fixed
  now rather than deferred, and why `TASK-24H-0176`/`0178` were raised from P2 to P1 mid-batch.

**DirectDraw** (deep audit done 2026-07-09, `docs/audit_ddraw.md` - all 13 tasks
`TASK-24H-0151`-`0163` now DONE, see below):
- `GetDC`/`ReleaseDC` are documented `STUB` in the header but are functionally real - unchanged.
- `Flip`, presentation throttle, and clipper one-time-init are tested (`TASK-24H-0046`/`0047`/
  `0050`-`0055`) - previously the largest DirectDraw test gap, closed in an earlier session.
- `IsLost`/`Restore` remain honestly-documented inert stubs — unchanged, still test-locked.
- **New finding, documented not fixed** (`docs/directdraw-limitations.md`, earlier session):
  `SetDisplayMode`'s `dwBPP` parameter is accepted/logged but never stored/used - the primary
  surface is always 32bpp. Currently latent for both target games; not fixed speculatively per
  scope policy.
- `DDBLT_ROTATIONANGLE`'s literal value doesn't match `free-eggbert`'s own vendored SDK header
  (found in an earlier session, `TASK-24H-0021`) - harmless today, nothing reads this constant.
- **Fixed this session, from `docs/audit_ddraw.md`**:
  - Default build now defaults `CMAKE_BUILD_TYPE` to `Release` when unset *and* this is the
    top-level project (was measured 6.6x slower unoptimized before the fix) - scoped so a
    consuming project's own build-type choice is never overridden, confirmed via a fresh
    out-of-tree `../free-eggbert` build (`TASK-24H-0151`).
  - `BlitFrom` now has a 1:1 (no-scale, no-colorkey) fast path using per-row `memcpy` - measured
    improving `BltFast` from 0.715ms to 0.466ms/call (29.4x → 14.8x slower than raw `memcpy`) at
    `-O3` for a 640x480 copy; confirmed reachable by both games every frame (`TASK-24H-0152`).
  - `ReleaseDC`'s 8-bit nearest-palette search now prunes via partial-sum early-exit, mathematically
    identical output to the original (`TASK-24H-0153`; confirmed unreachable by either game today,
    fixed anyway).
  - `CreateSurface` now bounds `dwWidth`/`dwHeight` to 4096 before allocating, returning
    `DDERR_INVALIDPARAMS` instead of risking an uncaught `vector::resize` throw (`TASK-24H-0154`);
    `Palette::GetEntries`/`SetEntries`'s bounds check is now overflow-safe
    (`dwBase > 256 || dwNumEntries > 256 - dwBase`) instead of the overflowable
    `dwBase + dwNumEntries > 256` (`TASK-24H-0155`) - both confirmed unreachable by either game
    today, fixed anyway as API-boundary hardening.
  - `SetCooperativeLevel` now tears down every live surface's texture before destroying the
    renderer, preventing a stale-texture-against-a-destroyed-renderer state if called twice after a
    present (`TASK-24H-0156`, tracked via a new `liveSurfaces_` registration list; `owner_`'s
    lifetime contract documented rather than removed, `TASK-24H-0161`, since this fix gave it a
    real read site). `FillColor`'s 8-bit branch now reaches `MarkDirty()` like the 32-bit branch
    (`TASK-24H-0157`). Double-`GetDC` without an intervening `ReleaseDC` now returns
    `DDERR_DCALREADYCREATED` instead of silently leaking the previous DC (`TASK-24H-0158`). Three
    code-quality items: named `DDPF_*` constants replace magic numbers in `GetSurfaceDesc`
    (`TASK-24H-0159`), all `SDL_Log` call sites route through `DirectDrawLog` consistently
    (`TASK-24H-0160`), a former `dynamic_cast` replaced with `static_cast` since
    `DirectDrawSurfaceImpl` is the sole `final` implementation of `IDirectDrawSurface`
    (`TASK-24H-0163`) - plus a new `@note Thread safety:` doc paragraph in `include/ddraw.h`
    (`TASK-24H-0162`). All confirmed unreachable by either game today, all documented in full in
    `docs/audit_ddraw.md`.

**DirectSound** (deep audit done 2026-07-09, `docs/audit_dsound.md` - all 8 tasks
`TASK-24H-0164`-`0171` now DONE, see below):
- `CreateSoundBuffer` performs no `dwSize` validation - documented, not a confirmed bug (pre-existing
  finding, unchanged).
- `DirectSoundCreate`'s `DSERR_NODRIVER` path remains untested (`TASK-24H-0057`, `PARTIAL`) -
  genuinely hard to force in-process given `SharedAudioDevice`'s process-lifetime-sticky driver
  selection.
- Mono-only `SetPan`, non-seekable `SetCurrentPosition`, no-looping - all pre-existing, documented.
- **No algorithmic hot-path defect found** - `Play()` measured at 0.0016ms/call for a realistic
  2-second SFX buffer; this audit deliberately checked for a `BlitFrom`-style missing fast path and
  ruled it out empirically rather than assuming DirectSound was fine.
- **Fixed this session, from `docs/audit_dsound.md`**:
  - `CreateSoundBuffer`'s `dwBufferBytes` is now bounded to 64MB before the allocation, returning
    `DSERR_INVALIDPARAM` and defensively nulling the output pointer over the bound - concretely
    evidenced: both target games read this value unvalidated from an on-disk `.wav` file's own
    header field, making a corrupted/truncated asset a plausible trigger for an uncaught allocation
    exception (`TASK-24H-0164`, the only P1 among the DirectSound tasks, and arguably the single
    most concretely-evidenced robustness gap across all three audits).
  - The ~51ms/cycle full device close+reopen cost (vs. ~0.00006ms when another instance keeps the
    device open) is now documented in `docs/directsound-limitations.md` as confirmed not triggered
    by either game's real create-once/release-once lifecycle, with a new regression test proving
    the sole-owner create/release cycle still completes without error (`TASK-24H-0165`).
  - `SharedAudioDevice::id()` now takes its own mutex before reading (`TASK-24H-0166`, required
    marking the mutex `mutable` so a `const` method can lock it); a new `@note Thread safety:` doc
    paragraph in `include/dsound.h` documents the single-threaded-usage assumption, mirroring
    DirectDraw's (`TASK-24H-0167`); `Lock()`'s offset-clamp and `Unlock()`'s pointer-lifetime
    behavior are now documented as deliberate in `docs/directsound-limitations.md`
    (`TASK-24H-0168`); `nSamplesPerSec` is now bounded to 192000Hz with a graceful fallback
    (`freq` reset to 0) instead of reaching SDL3 unchecked (`TASK-24H-0169`); a near-zero/absent
    `nSamplesPerSec` no longer discards channel/bit-depth info when a valid descriptor was
    otherwise present - only `freq` resets, not the whole format (`TASK-24H-0170`) - all confirmed
    unreachable by either game today, fixed anyway as API-boundary hardening, all documented in
    full in `docs/audit_dsound.md`.
  - Test-coverage gap closed: a new test drives `MAXSOUND` (100) simultaneous buffers, the real
    ceiling both games allow, confirming all play independently (`TASK-24H-0171` - the test's first
    version used ~10ms buffers too short to survive the test's own loop overhead under the dummy
    audio driver before all 100 statuses were checked, a test-design bug caught via
    `ctest --output-on-failure` and fixed with ~1s buffers, not a product bug).

**Cross-cutting, new this session (`TASK-24H-0180`-`0182`, follow-up analysis, all `DONE`)**:
DirectDraw+DirectSound running together in one process - the normal, unconditional startup state of
both target games - previously had zero test coverage anywhere in this repo, since every existing
test binary and the demo itself only ever exercised one subsystem at a time; now covered by
`tests/integration_tests.cpp` (real `BltFast`+present interleaved with a real `Play()`, both
teardown orderings, verified clean under ASan+UBSan). The `FREE_DIRECT` demo's runtime behavior,
previously unverified, was confirmed working (`TASK-24H-0182`) - stable ~52-53 FPS, no crash over a
multi-second headless run; the one caveat is two non-fatal "failed to load player.png" warnings at
startup from a missing demo sprite asset in this environment, unrelated to DirectDraw/DirectSound
correctness. `TASK-24H-0057` (`DSERR_NODRIVER`), open since an earlier session, is now closed via a
dedicated fresh-process test binary (`TASK-24H-0181`).

**Unchanged**: `../free-eggbert`'s own DirectPlay lobby UI is unwired in the game's current source
(not a FreeDirect bug, game source must never be modified). `docs/directplay-protocol.md`'s wire
header size (72 bytes) is platform/ABI-dependent (found in an earlier pass this session) - would be
64 bytes on a real Win32/LLP64 build, only matters if two differently-built FreeDirect peers ever
tried to talk to each other, which nothing does today.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped only, CTest-
enforced via `header_hygiene`. `dplay.h` now also carries a `static_assert(sizeof(DPID) == 4)`
(this session) and corrected inline comments for `DDBLT_*`/`DPESC_TIMEDOUT`/`DPSESSION_*` constants
whose real call-site status was previously undocumented or mis-documented.

**DirectDraw/DirectSound internals** (`src/directdraw/DirectDraw.cpp`,
`src/directsound/DirectSound.cpp`) — both remain anonymous-namespace implementations with no
separate header, tested exclusively through the public `IDirectDraw*`/`IDirectSound*` interfaces.
Key facts, largely unchanged from before this session (see prior `NEXT.md` history in `git log` for
the full list) - **new this session**: both files' debug-flag-check functions now support an
`#ifdef FREE_DIRECT_DEBUG_*` compile-time override in addition to the existing runtime env-var
check (DirectSound already had this; DirectDraw's 5 flags gained it this session).

**DirectPlay internals** (`src/directplay/`) — `IDirectPlayTransport` (13 methods:
`Listen`/`Connect`/`Send`/`Receive`/`Service`/`HasPendingConnection`/`AssignPendingConnection`/
`RejectPendingConnection`/`HasDisconnectedPeer`/`TakeDisconnectedPeer`/`IsConnectedToHost`/
`Shutdown`, plus the destructor) is implemented by `LoopbackDirectPlayTransport` (always available)
and `EnetDirectPlayTransport` (opt-in). `DirectPlayMessageQueue::TryReceive()`'s zero-length-message
`memcpy` bug (Section 5) is fixed. `DirectPlayWireProtocol.hpp`'s header is 72 bytes on this
project's Linux/LP64 build (see `docs/directplay-protocol.md`, new this session, for the exact
field-by-field layout).

**Build/test infrastructure** — `tests/CMakeLists.txt` now builds and registers up to **10** CTest
tests when `FREE_DIRECT_BUILD_TESTS=ON` (default `OFF`): 9 in the default configuration (7 from
before this session, plus `directsound_nodriver_test` and `integration_tests`, both new this
session, `TASK-24H-0180`/`0181`) plus `enet_directplay_tests` (`enet` label, only when
`FREE_DIRECT_ENABLE_ENET=ON` too). Root `CMakeLists.txt` gained `FREE_DIRECT_ENABLE_ASAN`/
`FREE_DIRECT_ENABLE_UBSAN` (both OFF by default, `PRIVATE` to `free-direct`'s own targets) and 7
`FREE_DIRECT_FORCE_DEBUG_*` options (also OFF by default, additive to the existing env-var
mechanism) - both from earlier in this session.

**Invariants that must not be broken** (unchanged):
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever — CTest-enforced.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — ask a human
  before adding DirectX surface "for completeness."
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never a runtime switch.
- Sanitizer/force-debug-flag CMake options are `PRIVATE` and OFF by default — never forced onto a
  consuming target game, never on by default.

## 7. Useful commands

Standalone build with FreeDirect's own tests:
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build> -j8
cd <build> && ctest --output-on-failure
```

Through a target game (no system-SDL3 dependency, vendored SDL):
```bash
cmake -B <build> -S ../free-eggbert    # or ../planetblupi
cmake --build <build> -j8
```

ENet-enabled (opt-in, scoped test run - see Section 5 for why `-L enet` matters):
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build <build> -j8
ctest --test-dir <build> -L enet
```

Sanitizer build (opt-in):
```bash
cmake -B <build> -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON
cmake --build <build> -j8
ctest --test-dir <build> --output-on-failure
```

Run one subsystem's tests only: `ctest -L directdraw` / `-L directsound` / `-L directplay` /
`-L integration` / `-L headers` / `-L enet`.

Check the public-header/backend-leak invariant manually (also a CTest test):
```bash
bash tests/check_header_hygiene.sh include
```

Force a debug-log flag on at compile time instead of via environment variable (new this session):
```bash
cmake -B <build> ... -DFREE_DIRECT_FORCE_DEBUG_DDRAW=ON   # or _DSOUND/_PRESENTATION/_COLORKEY/_PERF/_PRIMARY_CLEAR/_DSOUND_FORMAT
```

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

**Track A — see the top of this file for the current count.** `TASK-24H-0151`-`0182` (13 DirectDraw
+ 8 DirectSound + 8 DirectPlay audit-hardening, then 3 cross-cutting follow-up, 32 tasks total) are
all `DONE` - one commit per task, each independently build+test verified - and closed the backlog
completely for the first time in this project's history. A dedicated maintainability audit then
added 6 more, `TASK-24H-0183`-`0188` (2 code changes needing a user decision each via
`AskUserQuestion` before being written - see below; 4 documentation/consolidation fixes), which may
or may not still be in progress depending on when this is read - check the top-of-file count rather
than trusting this sentence. `TASK-24H-0057` (`DSERR_NODRIVER`), the backlog's last non-`DONE` item
across every prior session before this one, was closed by `TASK-24H-0181`.

Four tasks this session were genuine design/verification decisions rather than mechanical fixes,
all handled per this project's standing policy of not deciding or asserting things unilaterally:
`TASK-24H-0176` (wire-header `magic`/`version` validation) was asked of the user via
`AskUserQuestion` before implementing - user chose to add the check. `TASK-24H-0184` (an
extract-method refactor of `Send()`/`Receive()`) and `TASK-24H-0185` (a new shared
`tests/TestHelpers.hpp`) each reverse a standing convention (`NEXT.md`'s "no broad refactor" rule;
the prior no-shared-test-header pattern) and were likewise asked via `AskUserQuestion` before being
written into `plan.md` at all - both approved as scoped, behavior-preserving exceptions, not
blanket policy changes. `TASK-24H-0182` (the demo's runtime behavior) was independently re-verified
a second time, by direct re-run, before writing the claim into `plan.md`/`NEXT.md`, rather than
trusting a sub-agent's summary at face value - which is
exactly what caught that summary's "zero errors" claim being slightly too strong.

**Track B — resolved** (in an earlier session, unchanged this session). All 7 questions
(`TASK-24H-0091, 0131..0137`) were asked of, and answered by, the user via `AskUserQuestion` -
never decided unilaterally. 4 got real implementation (DPID-0 broadcast/self-send semantics -
Decision 20; host routing - Decision 21; ENet host-address resolution - Decision 22; LAN discovery
- Decision 23), 3 were decided not needed (player names - Decision 24; duplicate-player definition
- Decision 25; player-lost state - Decision 26). Full detail in `docs/directplay-design.md`
Decisions 20-26 (now 27, this session, for `TASK-24H-0176`). **Correction (`TASK-24H-0188`,
2026-07-09)**: this section previously claimed `docs/directplay-limitations.md` "still has the
pre-resolution deviation-table framing... not yet re-reconciled" - checked directly against the
real file and found that claim itself was stale: every row touched by Decisions 20-26 is already
marked `(**resolved, implemented**)`/`(**resolved: decided not needed**)` with correct Decision
citations, and a dedicated section (its own "Formerly-BLOCKED design questions" heading) already
walks through all 7 resolved questions. The reconciliation had already happened in an earlier
session; only the note claiming otherwise was wrong. The one real, small gap found on re-check -
the LAN-discovery-responder row not yet citing Decision 27 - was fixed directly.

**A future session's path to further progress**: first check the top-of-file count - if
`TASK-24H-0183`-`0188` still show any non-`DONE` entries, finish those first (each has its own
`plan.md` entry with full acceptance criteria). Once the backlog is empty again, options: identify
genuinely new work via a fresh call-site audit of either target game (especially worth revisiting
as `free-eggbert`'s decompilation progresses, per the user's standing note that today's DirectPlay
unreachability is temporary, not permanent - see the `project_free_eggbert_decompilation_in_progress.md`
memory file), host migration (`DPSESSION_MIGRATEHOST`, still open, not part of any resolved
Decision - see Section 4), revisiting a "not needed" Decision if a concrete consumer need is later
found, sourcing the missing `player.png` demo asset so the demo's two startup warnings go away
(cosmetic, Section 2), or simply waiting for a new user-driven feature request - the project has no
self-generating backlog left to work through mechanically once the current batch is done.

## 9. Do not do yet

- **No broad refactor** of `DirectDraw.cpp`/`DirectSound.cpp`/`DirectPlay.cpp` — each works and is
  extended incrementally by design.
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances.
- **Do not decide any future DirectPlay design question unilaterally** — ask first via
  `AskUserQuestion`, matching how all 7 now-resolved Track B questions (Section 8) and the one-off
  `TASK-24H-0176` wire-header validation decision were both handled. The DPID-0 broadcast/self-send
  test now proves the exact current (Decision 20) behavior; do not change what it asserts without a
  real decision first.
- **Do not add `DSBVOLUME_MIN`/`MAX` named constants to `include/dsound.h`** — no call site needs
  them by name.
- **Do not add stricter `DSBUFFERDESC::dwSize` validation to `CreateSoundBuffer`** without asking —
  it's a documented asymmetry with DirectDraw, not a confirmed bug.
- **Do not implement real looping, capture, 3D audio, or accurate per-channel stereo panning for
  DirectSound** — no call site in either target game needs any of them.
- **Do not add a run-time backend-selection mechanism** for loopback-vs-ENet — build-time-only via
  `FREE_DIRECT_ENABLE_ENET` is a deliberate, already-made decision.
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule. Ask first.
- **Do not implement host migration** (reading `DPSESSION_KEEPALIVE`/`MIGRATEHOST`) without asking
  — a distinct feature from host message routing (implemented via Decision 21) and still genuinely
  open; it was never one of the 7 resolved Track B questions, so there is no existing user decision
  to build from (Section 4/5).
- **Do not "fix" `DDBLT_ROTATIONANGLE`'s value mismatch** against the vendored SDK header without
  asking — currently inert (nothing reads it), found this session, documented not fixed.
- **Do not correct the wire protocol's platform-dependent header size** (72 vs. 64 bytes, Section
  5) to be portable without asking — nothing today needs cross-ABI FreeDirect-to-FreeDirect
  interoperability, and doing so would be new protocol-versioning work, not a bug fix.
- **No mass rewrites or speculative architecture changes.** Verify every change with an actual
  build+test run through at least one real consumer (standalone build or a target game) before
  considering it done.

## 10. Resume prompt

```
Read NEXT.md first (this file) - check the "At a glance" line at the very top for the exact current
task count before anything else, then Sections 4 and 8. plan.md's "24-Hour Autonomous
Stabilization Backlog" section is the authoritative task-level record, grep-verified directly
against plan.md, not recalled from memory. All 7 of the project's former BLOCKED DirectPlay design
questions were resolved in an earlier session (docs/directplay-design.md Decisions 20-26).

This session (2026-07-09) had four phases. (1) Three fresh subsystem audits - docs/audit_ddraw.md,
docs/audit_dsound.md, docs/audit_dplay.md - produced 29 tasks (TASK-24H-0151-0179: 13 DirectDraw +
8 DirectSound + 8 DirectPlay), all implemented, tested, and committed one at a time. (2) The user
then explicitly asked for a fresh "analyze current state, propose improvements" pass; a follow-up
cross-cutting gap analysis (looking specifically for issues the three subsystem-scoped audits
couldn't have found by construction) produced 3 more tasks (TASK-24H-0180-0182), also all
implemented - closing the entire backlog for the first time, including TASK-24H-0057
(DSERR_NODRIVER), which had sat PARTIAL since an earlier session. (3) The user then asked for a
dedicated maintainability audit (code-level + infrastructure-level, run in parallel); it produced 6
more tasks (TASK-24H-0183-0188) - check the top-of-file count for whether this batch is finished by
the time you're reading this. Two of these six required a user decision before being written at all
(TASK-24H-0184's extract-method refactor of Send()/Receive(), TASK-24H-0185's new shared
tests/TestHelpers.hpp - both reverse a standing convention and were resolved via AskUserQuestion,
not assumed). See plan.md's "DirectDraw/DirectSound/DirectPlay audit hardening", "Cross-cutting
hardening", and "Maintainability hardening" (all 2026-07-09) sections for each task's own Verified:
paragraph, and NEXT.md Section 3 / Section 5 for condensed summaries of what each fix changed. Two
earlier tasks were also genuine decisions rather than mechanical fixes: TASK-24H-0176 (wire-header
magic/version validation) was asked via AskUserQuestion - user chose to add the check, recorded as
docs/directplay-design.md Decision 27; TASK-24H-0182 (the demo's runtime behavior) was independently
re-verified by direct re-run before writing the claim into docs, which caught an earlier sub-agent
summary's "zero errors" claim being slightly too strong (two non-fatal missing-asset warnings
actually appear, unrelated to FreeDirect itself).

DirectDraw (59 tests), DirectSound (35 tests + 1 dedicated no-driver test), DirectPlay (68 tests +
8 opt-in ENet transport tests), and a new integration suite (4 tests, DirectDraw+DirectSound
running together - previously untested despite being both games' normal startup state) all have
solid coverage - 174 individual Test_* functions total as of the 182-task milestone (re-check after
the maintainability batch, which touches test files too). A real correctness bug found this session
(Send()'s self-send path reading the caller's buffer before validating its claimed size,
TASK-24H-0172) was fixed and verified with a deliberate ASan-catches-the-revert check - not just a
passing assertion; the new integration and no-driver tests were similarly proven to have real teeth
(manually confirmed they fail under the wrong conditions), not just asserted to pass.

Check the top-of-file count first. If it shows any TASK-24H-0183-0188 still open, finish those
before looking for new work - each has its own plan.md entry with full acceptance criteria. Once
empty again, see Section 8 for what a future session could pick up: a fresh call-site audit of
either target game (especially worth revisiting as free-eggbert's decompilation progresses), host
migration, sourcing the demo's missing player.png asset (cosmetic), or waiting for a new
user-driven request.

Note: every DirectPlay fix from the first 29-task batch is still unreachable by free-eggbert's
actual running code today - its multiplayer packet pump, CDecor::TreatNetData(), has its one call
site commented out (docs/audit_dplay.md §4) - but this is TEMPORARY: the user has confirmed
free-eggbert's source is an active, ongoing decompilation and DirectPlay will actually be used once
it's complete, so do not treat DirectPlay work as low-value just because it's unreachable today
(see the memory file project_free_eggbert_decompilation_in_progress.md).

Standalone build: `cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`. Do
not touch ../free-eggbert or ../planetblupi source. Do not resolve any DirectPlay design question
unilaterally if a new one ever comes up.
```
