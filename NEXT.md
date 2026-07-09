# NEXT.md

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
  Backlog are both effectively closed out for implementation, plus three **new, audit-only
  additions** this session: a DirectDraw-only audit (`docs/audit_ddraw.md`) added
  `TASK-24H-0151`-`0163` (13 tasks), a DirectSound-only audit (`docs/audit_dsound.md`) added
  `TASK-24H-0164`-`0171` (8 tasks), and a DirectPlay-only audit (`docs/audit_dplay.md`) added
  `TASK-24H-0172`-`0179` (8 tasks) - **29 new tasks total, all `TODO`**, none implemented, see
  Section 3. Backlog now carries **179 atomic `TASK-24H-XXXX` tasks** total. As of this update (real
  count, `grep`-verified against `plan.md`, not estimated): **149 DONE, 29 TODO, 1 PARTIAL, 0
  BLOCKED**. Session 1 ended at 64 DONE. **All 7 of the project's standing BLOCKED DirectPlay design
  questions (Track B) were asked of, answered by, and implemented for the user in an earlier
  session** - never decided unilaterally (see Section 3 and `docs/directplay-design.md` Decisions
  20-26); the new DirectPlay audit did not reopen or contradict any of those 26 Decisions, and found
  no new BLOCKED question. `TASK-24H-0057` (`PARTIAL`) still needs a subprocess test harness this
  project doesn't have; the 29 new `TODO` tasks are all freshly added, unstarted DirectDraw/
  DirectSound/DirectPlay hardening work (Section 8).
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

**Build status: working**, verified fresh multiple times this session, not assumed:
- Standalone (`-DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`): configure/build exit
  0, `ctest` 7/7.
- Bare standalone (no flags, no sibling game): **confirmed still fails** with the expected
  actionable 3-option error message (system SDL3 / build via a target game / provide SDL3 targets
  manually) - not a silent regression to a generic CMake error.
- Through `../free-eggbert`: configure/build exit 0, out-of-tree in a `/tmp` scratch dir (touching
  neither sibling repo), producing `SPEEDY_BLUPI_WINDOWS` - including `wave.cpp`/`network.cpp`
  compiling cleanly.
- Through `../planetblupi`: same result, producing `PLANET_BLUPI_WINDOWS`.
- `-DFREE_DIRECT_ENABLE_ENET=ON`: configure/build exit 0; `ctest -L enet` passes 1/1
  (`enet_directplay_tests`); the *unfiltered* `ctest` against this same build fails
  `directplay_tests` (1/8) **by design**, not a regression - see Section 5.
- `-DFREE_DIRECT_ENABLE_ASAN=ON -DFREE_DIRECT_ENABLE_UBSAN=ON`: configure/build exit 0, `ctest`
  passes 7/7 with **zero sanitizer diagnostics** - after fixing one real bug the first sanitizer
  run caught (Section 3). Also verified combined with ENet (8/8).

**Test status: real counts, `grep`-verified against the actual test files, not recalled from
memory:**
- `directplay_tests` (label `directplay`) — **63/63** passing (was 49 at session-1 end).
- `header_smoke_ddraw` / `header_smoke_dsound` / `header_smoke_dplay` / `header_hygiene` (label
  `headers`) — all passing.
- `directdraw_tests` (label `directdraw`) — **53/53** passing (was 45).
- `directsound_tests` (label `directsound`) — **30/30** passing (was 24).
- `enet_directplay_tests` (label `enet`, opt-in, only built with `FREE_DIRECT_ENABLE_ENET=ON`) —
  **4/4** passing, new this session.

Total: **150 individual `Test_*` functions** across four hand-written test binaries (63 + 53 + 30 +
4), all passing, plus the 4 header-compile/hygiene checks.

The `ctest`-only-discoverable-from-inside-`FREE_DIRECT`-subdirectory caveat for target-game builds
(unchanged from before this session) does not apply to the standalone build path, which remains
the faster iteration loop.

**Available artifacts** (unchanged in kind, extended in content):
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`); compiles, still not run/observed graphically.
- `tests/directplay_tests.cpp`, `tests/directdraw_tests.cpp`, `tests/directsound_tests.cpp`,
  `tests/enet_directplay_tests.cpp` (new), `tests/header_smoke_*.cpp`,
  `tests/check_header_hygiene.sh` — all CMake/CTest-wired.

**What does not work yet** (see Section 5 for full detail, unchanged in substance from before this
session - no BLOCKED question was resolved):
- DirectPlay: ENet joining/discovery, broadcast (proven to collide with self-send), host routing,
  player names, duplicate-player/player-lost semantics — all still BLOCKED on a human decision.
- `DirectPlayEnumerateA`/`W` real provider enumeration (`TASK-24H-0100`) — Decision 1 already
  decided the shape; genuinely just not yet coded. Not attempted this session (out of this
  session's DirectPlay-safe-tasks scope - implementing new stub behavior wasn't in the explicit
  "tests/cleanup only" DirectPlay priority list).
- The `FREE_DIRECT` demo executable's actual on-screen behavior — still unverified.

## 3. Recent changes

**This session** (2026-07-09, audit + planning only - no implementation, no game/library behavior
changed):

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
   condensed entries this round. All `Status: TODO` - **none implemented yet**. See Section 8 for
   the prioritized list covering all three audits' new tasks together.

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

**There is no build- or test-breaking blocker, and no more BLOCKED design questions.** Everything
builds, all 156 committed `Test_*` checks pass (65 directplay + 53 directdraw + 30 directsound + 8
ENet, opt-in), plus 4 header-level checks, across every verified configuration (default, ENet,
ASan+UBSan, ASan+UBSan+ENet combined, and both target games).

**The DirectPlay design fork described in every prior session is now resolved.** All 7 standing
BLOCKED design questions (DPID-0 broadcast semantics, ENet host discovery, LAN discovery, host
routing, player names, duplicate-player semantics, player-lost state) were presented to the user
via `AskUserQuestion` this session (never decided unilaterally), and the user chose real
implementation for 4 of them (broadcast+routing, ENet host discovery, LAN discovery) and "not
needed" for the other 3 (player names, duplicate-player, player-lost state). All are now recorded
as `docs/directplay-design.md` Decisions 20-26 and, for the 4 requiring code, implemented and
tested (`TASK-24H-0148`/`0149`/`0150`).

**What's different now**: broadcast (`idTo == DPID_ALLPLAYERS`) works for real, with host-side
relay so non-host peers can reach each other; ENet joining is wired via
`FREE_DIRECT_ENET_HOST_ADDRESS`; real LAN discovery exists via a new raw-UDP
`DirectPlayDiscoveryService`, built on ENet's own portable socket primitives. Two more real bugs
were found and fixed in that session's own newly-written test code (a dangling-pointer read past a
callback's documented lifetime, and two tests that were written but never registered so they
silently never ran) - both caught by actually running the tests, not assumed passing.

**This session (2026-07-09) added 29 new TODO tasks**: `TASK-24H-0151`-`0163` from a fresh
DirectDraw-only audit (`docs/audit_ddraw.md`), `TASK-24H-0164`-`0171` from a fresh DirectSound-only
audit (`docs/audit_dsound.md`), and `TASK-24H-0172`-`0179` from a fresh DirectPlay-only audit
(`docs/audit_dplay.md`) - none implemented yet, see Section 8. The DirectPlay *implementation*
backlog (the 26 design Decisions and their `TASK-24H-0001`-`0150`-range tasks) remains fully closed
- the new DirectPlay audit found real but low-urgency gaps, reopened nothing, and (per
`docs/audit_dplay.md` §4) confirmed every one of its findings is currently unreachable by
free-eggbert's actual running code, since the game's own multiplayer packet pump
(`CDecor::TreatNetData()`) has its call site commented out.

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
- **New this session, from `docs/audit_dplay.md` (2026-07-09), none fixed yet, 8 new `TODO`
  tasks tracked as `TASK-24H-0172`-`0179`**: the framing fact that `CDecor::TreatNetData()`
  (`../free-eggbert/src/decnet.cpp:87`) - the per-frame pump that would call `Send()`/`Receive()`
  during an open session - has its call site commented out (`event.cpp:2045`), confirming every
  finding below is currently unreachable by free-eggbert's actual running code; `Send()`'s
  self-send path reads the caller's buffer before validating its claimed size, unlike the
  broadcast/unicast paths (`TASK-24H-0172`, the only P1 in this batch); two stale-documentation
  contradictions (`include/dplay.h`'s own top-of-file comment vs. its `Send()` method doc,
  `TASK-24H-0173`; `docs/networking-backends.md` vs. the already-implemented Decisions 22/23,
  `TASK-24H-0174`); confirmed-dead `DirectPlayPlayer` scaffolding (`TASK-24H-0175`); a
  now-worth-revisiting deferred wire-header magic/version validation decision
  (`TASK-24H-0176`); an undocumented, low-stakes UDP reflection characteristic in the LAN
  discovery responder (`TASK-24H-0177`); unbounded drain loops in `Service()`/the discovery
  responder (`TASK-24H-0178`); and a negligible-but-inconsistent per-call allocation in
  `Receive()` (`TASK-24H-0179`, measured at 228.7ns/call - checked, not assumed, to be
  low-priority).

**DirectDraw** (deep audit done 2026-07-09, `docs/audit_ddraw.md` - no code fixed yet, 13 new
`TODO` tasks tracked as `TASK-24H-0151`-`0163`):
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
- **New this session, from `docs/audit_ddraw.md`, none fixed yet**:
  - Default build (no `CMAKE_BUILD_TYPE` set anywhere) is unoptimized - measured 6.6x slower than
    `-DCMAKE_BUILD_TYPE=Release` (`TASK-24H-0151`).
  - `BlitFrom` has no 1:1 fast path - measured 29x slower than `memcpy` at `-O3` (206x
    unoptimized) for a 640x480 `BltFast` call; confirmed reachable by both games every frame
    (`TASK-24H-0152`).
  - `ReleaseDC`'s 8-bit path is an O(n*256) per-pixel nearest-palette search, confirmed currently
    unreachable by either game (`TASK-24H-0153`).
  - `CreateSurface` doesn't bound `dwWidth`/`dwHeight` before an unguarded `vector::resize` that
    can throw uncaught (`TASK-24H-0154`); `Palette::GetEntries`/`SetEntries`'s bounds check can be
    bypassed by DWORD overflow, an out-of-bounds write in the `SetEntries` case
    (`TASK-24H-0155`) - both confirmed unreachable by either game today.
  - `SetCooperativeLevel` (`TASK-24H-0156`), `FillColor`'s dirty-flag asymmetry
    (`TASK-24H-0157`), missing `DDERR_DCALREADYCREATED` on double-`GetDC` (`TASK-24H-0158`), and
    three code-quality items (`TASK-24H-0159`-`0163`) - all confirmed unreachable by either game
    today, all documented in full in `docs/audit_ddraw.md`.

**DirectSound** (deep audit done 2026-07-09, `docs/audit_dsound.md` - no code fixed yet, 8 new
`TODO` tasks tracked as `TASK-24H-0164`-`0171`):
- `CreateSoundBuffer` performs no `dwSize` validation - documented, not a confirmed bug (pre-existing
  finding, unchanged).
- `DirectSoundCreate`'s `DSERR_NODRIVER` path remains untested (`TASK-24H-0057`, `PARTIAL`) -
  genuinely hard to force in-process given `SharedAudioDevice`'s process-lifetime-sticky driver
  selection.
- Mono-only `SetPan`, non-seekable `SetCurrentPosition`, no-looping - all pre-existing, documented.
- **No algorithmic hot-path defect found** - `Play()` measured at 0.0016ms/call for a realistic
  2-second SFX buffer; this audit deliberately checked for a `BlitFrom`-style missing fast path and
  ruled it out empirically rather than assuming DirectSound was fine.
- **New this session, from `docs/audit_dsound.md`, none fixed yet**:
  - `CreateSoundBuffer`'s `dwBufferBytes` is never bounded before an unguarded
    `vector::resize` - concretely evidenced: both target games read this value unvalidated from an
    on-disk `.wav` file's own header field, making a corrupted/truncated asset a plausible trigger
    for an uncaught allocation exception (`TASK-24H-0164`, the only P1 among the DirectSound tasks).
  - A full `IDirectSound` device close+reopen cycle measured ~51ms/cycle (vs. ~0.00006ms when
    another instance keeps the device open) - confirmed not triggered by either game's real
    create-once/release-once lifecycle, documented not yet fixed (`TASK-24H-0165`).
  - `SharedAudioDevice::id()` reads outside its own class's mutex (`TASK-24H-0166`), no documented
    single-threaded-usage assumption (`TASK-24H-0167`), `Lock()`'s offset-clamp and `Unlock()`'s
    pointer-lifetime behavior undocumented as deliberate (`TASK-24H-0168`), an extreme
    `nSamplesPerSec` reaching SDL3 unchecked with unverified consequences (`TASK-24H-0169`), a
    near-zero `nSamplesPerSec` silently discarding channel/bit-depth info too
    (`TASK-24H-0170`) - all confirmed unreachable by either game today, all documented in full in
    `docs/audit_dsound.md`.
  - Test-coverage gap: neither existing test approaches `MAXSOUND` (100) simultaneous buffers, the
    real ceiling both games allow (`TASK-24H-0171`) - no confirmed bug at that scale, just untested.

**Unchanged**: `../free-eggbert`'s own DirectPlay lobby UI is unwired in the game's current source
(not a FreeDirect bug, game source must never be modified). The `FREE_DIRECT` demo executable's
on-screen behavior is still unverified. `docs/directplay-protocol.md`'s wire header size (72 bytes)
is platform/ABI-dependent (found this session) - would be 64 bytes on a real Win32/LLP64 build,
only matters if two differently-built FreeDirect peers ever tried to talk to each other, which
nothing does today.

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

**Build/test infrastructure** — `tests/CMakeLists.txt` now builds and registers up to **8** CTest
tests when `FREE_DIRECT_BUILD_TESTS=ON` (default `OFF`): the 7 from before plus
`enet_directplay_tests` (`enet` label, only when `FREE_DIRECT_ENABLE_ENET=ON` too). Root
`CMakeLists.txt` gained `FREE_DIRECT_ENABLE_ASAN`/`FREE_DIRECT_ENABLE_UBSAN` (both OFF by default,
`PRIVATE` to `free-direct`'s own targets) and 7 `FREE_DIRECT_FORCE_DEBUG_*` options (also OFF by
default, additive to the existing env-var mechanism) - all new this session.

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
`-L headers` / `-L enet`.

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

**Track A — safe, no design decision needed** (real remaining `plan.md` TODO count: **29**: 13
from the 2026-07-09 DirectDraw audit (`TASK-24H-0151`-`0163`), 8 from the same-day DirectSound
audit (`TASK-24H-0164`-`0171`), and 8 from the same-day DirectPlay audit (`TASK-24H-0172`-`0179`).
None `BLOCKED`, none implemented yet). Recommended order, merging all three audits' own
severity×reachability rankings (`docs/audit_ddraw.md` §2/§12, `docs/audit_dsound.md` §2/§10,
`docs/audit_dplay.md` §2/§9):

1. `TASK-24H-0151` (P0, Area: Build) - default `CMAKE_BUILD_TYPE` to `Release` when unset. Highest
   leverage, lowest risk: one `CMakeLists.txt` change, no behavior risk, and it changes the
   baseline for every other performance number in the whole project (benefits DirectSound/
   DirectPlay too - no separate build-type task was added for either, this one already covers them).
2. `TASK-24H-0152` (P0, Area: DirectDraw) - add a runtime 1:1 fast path to `BlitFrom`. The only
   other High-impact finding confirmed reachable by both games' real, everyday code paths (every
   present, every sprite draw). Must keep the scaling path for `CPixmap::Display()`'s primary
   present blit, which can genuinely scale - see the task's own text and `docs/audit_ddraw.md` §8.3.
3. `TASK-24H-0164` (P1, Area: DirectSound) - bound `CreateSoundBuffer`'s `dwBufferBytes`. The
   DirectSound audit's own highest-priority finding, and arguably the single most concretely
   evidenced robustness gap across all three audits: both target games read this value, unvalidated,
   straight from an on-disk `.wav` file's own header field, so a corrupted/truncated asset is a
   real, not just hypothetical, way to trigger an uncaught allocation exception.
4. `TASK-24H-0154`/`0155` (P1, Area: DirectDraw) - `CreateSurface` size validation and the
   `Palette::GetEntries`/`SetEntries` integer-overflow bounds-check fix. Both are real API-boundary
   hardening gaps (one can crash the process, one is an out-of-bounds write), currently unreachable
   by either target game but cheap, low-risk, self-contained fixes with tests already sketched.
5. `TASK-24H-0172` (P1, Area: DirectPlay) - fix `Send()`'s self-send path reading the caller's
   buffer before validating its claimed size. A real, precisely-located inconsistency with the
   other two `Send()` delivery paths, currently unreachable by free-eggbert's own call pattern and
   not exercised by the existing oversized-payload test.
6. `TASK-24H-0176`/`0178` (P1, Area: DirectPlay) - wire-header magic/version validation decision,
   and an iteration cap on `Service()`'s/the discovery responder's unbounded drain loops. Both
   raised from an initial P2 after the user clarified that free-eggbert's DirectPlay
   unreachability (`docs/audit_dplay.md` §4) is temporary - tied to an ongoing decompilation, not a
   permanent state - so these two genuine protocol-robustness/network-input-volume gaps shouldn't
   be deprioritized purely on today's reachability, unlike pure documentation/cleanup items.
7. `TASK-24H-0153`, `0156`-`0163` (P2, DirectDraw), `TASK-24H-0165`-`0171` (P2, DirectSound), and
   `TASK-24H-0173`-`0175`/`0177`/`0179` (P2, DirectPlay) - remaining latent-defect fixes,
   documentation gaps, and code-quality cleanups, opportunistic, no urgency. Within this group,
   `TASK-24H-0165` (documenting the ~51ms device close/reopen cost) and `TASK-24H-0173`/`0174`
   (fixing two stale-documentation contradictions) are worth doing early relative to their siblings
   since they're pure documentation (plus one non-timing-sensitive test for 0165), not code changes.
   `TASK-24H-0175` (removing `DirectPlayPlayer`'s dead scaffolding) is the one DirectPlay item in
   this batch genuinely unaffected by decompilation progress - it's dead for internal-architecture
   reasons, not because of free-eggbert's current state.

Also still open, unchanged: `TASK-24H-0057` (`DSERR_NODRIVER`, `PARTIAL`) - remains genuinely not
closeable without a subprocess test harness this project doesn't have yet (see Section 5).

**Track B — resolved** (in an earlier session, unchanged this session). All 7 questions
(`TASK-24H-0091, 0131..0137`) were asked of, and answered by, the user via `AskUserQuestion` -
never decided unilaterally. 4 got real implementation (DPID-0 broadcast/self-send semantics -
Decision 20; host routing - Decision 21; ENet host-address resolution - Decision 22; LAN discovery
- Decision 23), 3 were decided not needed (player names - Decision 24; duplicate-player definition
- Decision 25; player-lost state - Decision 26). Full detail in `docs/directplay-design.md`
Decisions 20-26; `docs/directplay-limitations.md` still has the pre-resolution deviation-table
framing and is now somewhat superseded by the Decisions themselves for these 7 items specifically
(not yet re-reconciled - a small follow-up documentation task, not tracked as its own
`TASK-24H-XXXX` yet).

A future session's path to further progress: work through the 29 new Track A tasks above in
priority order, build the subprocess harness for `TASK-24H-0057`, or identify genuinely new work (a
fresh call-site audit of either target game, a new user-driven feature request, or revisiting a
"not needed" Decision if a concrete consumer need is later found).

## 9. Do not do yet

- **No broad refactor** of `DirectDraw.cpp`/`DirectSound.cpp`/`DirectPlay.cpp` — each works and is
  extended incrementally by design.
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances.
- **Do not decide any of the 7 BLOCKED DirectPlay design questions unilaterally** (Section 8, Track
  B) — ask first. The DPID-0 characterization test now proves the exact current behavior; do not
  change what it asserts without a real decision first.
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
  — entangled with the still-BLOCKED host-routing/player-lost-state questions (new finding this
  session, Section 5).
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
Read NEXT.md first (this file), especially Sections 4 and 8, then docs/audit_ddraw.md,
docs/audit_dsound.md, and docs/audit_dplay.md (all from 2026-07-09) and plan.md's "24-Hour
Autonomous Stabilization Backlog" section, especially its "DirectDraw audit hardening", "DirectSound
audit hardening", and "DirectPlay audit hardening" (2026-07-09) subsections, for full task detail.
149 of 179 TASK-24H-XXXX tasks are DONE, 1 PARTIAL, 29 TODO (TASK-24H-0151-0179: 13 DirectDraw + 8
DirectSound + 8 DirectPlay audit-hardening tasks, none BLOCKED, none implemented yet), 0 BLOCKED -
all 7 of the project's former BLOCKED DirectPlay design questions were resolved in an earlier
session (docs/directplay-design.md Decisions 20-26); the new DirectPlay audit reopened none of them.
DirectDraw (53 tests), DirectSound (30 tests), DirectPlay (65 tests + 8 opt-in ENet transport
tests) all have solid coverage; real memory-safety/UB bugs found in earlier sessions were fixed and
verified via ASan/UBSan. The 29 new TODO tasks are the next concrete work, in priority order per
Section 8: TASK-24H-0151 (default CMAKE_BUILD_TYPE to Release - highest leverage, one
CMakeLists.txt line, benefits all three subsystems), TASK-24H-0152 (BlitFrom 1:1 fast path -
reachable by both target games' real call paths every frame), TASK-24H-0164 (bound
CreateSoundBuffer's dwBufferBytes - the most concretely evidenced robustness gap across all three
audits, since both games read this value unvalidated from an on-disk .wav file), then
TASK-24H-0172 (Send()'s self-send path validation order). Note: every DirectPlay audit finding is
confirmed unreachable by free-eggbert's actual running code today - its multiplayer packet pump,
CDecor::TreatNetData(), has its one call site commented out (docs/audit_dplay.md §4) - but this is
TEMPORARY: the user has confirmed free-eggbert's source is an active, ongoing decompilation and
DirectPlay will actually be used once it's complete, so do not treat these findings as
indefinitely deferrable just because they're unreachable today (see the memory file
project_free_eggbert_decompilation_in_progress.md). TASK-24H-0176 and 0178 were raised from P2 to
P1 for exactly this reason. Standalone build: `cmake -B build -DFREE_API_USE_SYSTEM_SDL3=ON -DFREE_DIRECT_BUILD_TESTS=ON`. Do
not touch ../free-eggbert or ../planetblupi source. Do not resolve any DirectPlay design question
unilaterally if a new one ever comes up.
```
