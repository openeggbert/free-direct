# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so two specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is explicitly **not** an attempt at full DirectX compatibility —
scope is bounded by what the two target games' real call sites need (see `CLAUDE.md`, the project
charter).

- **Target games** (sibling repos, present on disk): `../free-eggbert` (*Speedy Blupi* —
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi* — DirectDraw +
  DirectSound only; re-confirmed zero DirectPlay usage by grep this session).
- **Main goal**: two programs both built against FreeDirect's own DirectPlay implementation can
  host/join/exchange messages with each other. This is explicitly **not** wire-compatible with
  real Microsoft DirectPlay — FreeDirect-to-FreeDirect only.
- **Current development phase**: `plan.md` Phases 0–9 remain complete (or have no more reachable
  tasks) over the **loopback** transport backend. Phase 10 (Send/Receive networking) is still
  partially done — host→one specific client unicast works over loopback; broadcast and host-side
  routing still do not exist. Phases 11–18 largely remain not started. **New this session**:
  `plan.md` gained a large appended section, **"24-Hour Autonomous Stabilization Backlog"**
  (141 atomic `TASK-24H-XXXX` tasks, 20 already completed — see Section 3), generated from a fresh
  evidence-based re-audit at `docs/audit-24h-free-direct.md`. Read that audit doc for the full
  picture; this file only summarizes.
- **Important architectural decisions** (full narrative + rationale for all of these lives in
  `docs/directplay-design.md`, Decisions 1–19 — read that file for the "why," not just the "what"):
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only. No SDL3/ENet/SDL3_net symbol may ever appear in them (Internal Backend Policy). **This
    invariant is now automatically enforced** by a CTest test (`header_hygiene`,
    `tests/check_header_hygiene.sh`), not just by manual grep.
  - DirectPlay's network transport is abstracted behind `IDirectPlayTransport`
    (`src/directplay/DirectPlayTransport.hpp`), with two concrete backends selected at
    **build time** (not runtime) via the `FREE_DIRECT_ENABLE_ENET` CMake option:
    `LoopbackDirectPlayTransport` (in-process, deterministic, used for all committed tests) and
    `EnetDirectPlayTransport` (real ENet UDP sockets).
  - DPID `0` is assigned to the host's own first local player (not reserved as
    `DPID_ALLPLAYERS`), matching `free-eggbert`'s own comparison pattern — a deliberate deviation
    from real DirectPlay that creates a **confirmed, characterization-tested** bug for broadcast:
    see Section 5.
  - Self-send (`Send()` with `idFrom == idTo`) is always a purely local operation and bypasses the
    transport entirely — it must not depend on, or be affected by, the transport's connection
    state.

## 2. Current status

**Build status: working**, both configurations, verified this session via full builds through
both `../free-eggbert` and `../planetblupi` (the only realistic way to build free-direct — it is
never a standalone-buildable project, by design):
```bash
cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON   # run from ../free-eggbert or ../planetblupi
cmake --build <build> -j8
```
Zero warnings on changed files. `grep -niE "enet|SDL_|SdlNet" include/dplay.h` finds zero real
matches (only English prose in Doxygen comments) — now also automatically checked by the
`header_hygiene` CTest test.

**Test status: 49/49 passing** (up from 46 — three new tests added this session; see Section 3).
`tests/directplay_tests.cpp` **is now wired into CMake/CTest** (`FREE_DIRECT_BUILD_TESTS=ON`,
`tests/CMakeLists.txt`) — `plan.md` Phase 15's long-standing first task is done. Two important
caveats, recorded honestly rather than glossed over:
- `ctest` only discovers these tests when run from **inside the `FREE_DIRECT` build subdirectory**
  (e.g. `cd <build>/FREE_DIRECT && ctest`), not from a target game's own top-level build directory
  — because neither `free-eggbert` nor `planetblupi`'s own top-level `CMakeLists.txt` calls
  `enable_testing()` itself (confirmed by grep; only `free-api`'s, itself nested, does). This is a
  property of how the two target games structure their own builds, not a defect in free-direct's
  CTest wiring, and fixing it would mean editing a target game's `CMakeLists.txt` (out of scope
  without asking first).
- These tests still only exercise the default (`FREE_DIRECT_ENABLE_ENET=OFF`, loopback) build — no
  committed test exercises the ENet backend yet.

Also new this session: `tests/CMakeLists.txt` builds and registers four more CTest tests under the
`headers` label — `header_smoke_ddraw`/`header_smoke_dsound`/`header_smoke_dplay` (each public
header compiles standalone) and `header_hygiene` (automates the SDL/ENet-leak check above).
Total: **5 CTest tests, all passing** (1 `directplay`-labeled + 4 `headers`-labeled).

**Available artifacts**:
- `libfree-direct.a` — the compatibility layer static library.
- `FREE_DIRECT` — demo executable (`src/Main.cpp`) exercising DirectDraw surfaces/blits/palette;
  compiles, but **has not been run/observed graphically** in this or recent sessions (no display
  verification performed — see Section 5).
- `tests/directplay_tests.cpp`, `tests/header_smoke_*.cpp`, `tests/check_header_hygiene.sh` — now
  all CMake/CTest-wired (`FREE_DIRECT_BUILD_TESTS=ON`), see Section 7 for exact commands.

**Recently implemented and verified this session** (see Section 3 for the full list): a large
audit (`docs/audit-24h-free-direct.md`), a 141-task `plan.md` backlog extension, and 20 of those
tasks actually implemented and verified (CMake/CTest wiring, header smoke tests + hygiene check,
three new DirectPlay regression tests, stale-documentation fixes in `include/dplay.h`, and a
diagnostics/logging gating audit that found no gaps).

**What does not work yet** (see Section 5 for full detail, unchanged from before this session
except where noted):
- Over ENet: `Open()` never calls `Connect()` for the joining role; no join handshake; not
  discoverable via `EnumSessions()`.
- Over loopback (and therefore everywhere): host-side routing between two non-host peers;
  broadcast (`idTo == 0`) — **now proven, not just suspected, to collide with self-send when the
  host is the sender** (`Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`, new this session); a
  real `JoinReject` explanation packet (structurally blocked); player name storage; a distinct
  "player-lost" state; duplicate-player validation.
- DirectSound/DirectDraw hardening (`plan.md` Phases 13–14, and the corresponding `TASK-24H-*`
  items) — audited this session (see `docs/audit-24h-free-direct.md`), but **zero automated tests
  exist for either subsystem yet** despite DirectDraw being the highest call-frequency subsystem
  in both target games (`Blt` is the once-per-frame present path; `BltFast` is the heaviest
  per-sprite path). This is the single largest remaining test-coverage gap in the project.
- The `FREE_DIRECT` demo executable's actual on-screen behavior — unverified.

## 3. Recent changes

**This session** (2026-07-08, in the order performed):

1. **Audit** (`docs/audit-24h-free-direct.md`, new file): a fresh, evidence-based re-audit of
   DirectDraw/DirectSound/DirectPlay call sites in both target games, the public header surface,
   the build/test/header-hygiene state, and a full reconciliation against the existing DirectPlay
   plan/design docs. Notable corrections to prior documentation:
   - `Blt` is not a rare path relative to `BltFast` as previously framed — it is the **once-per-frame
     back-buffer-to-primary present call** (`CPixmap::Display()`) in *both* target games.
   - `DDRAW`/`GetDC`/`ReleaseDC` are `STUB` per the header, yet `planetblupi`'s `IsIconPixel`
     (click hit-testing) is a live gameplay path that depends on them — a real risk, not just an
     unused stub.
   - **New bug found**: because the host's own first player is DPID 0, and `free-eggbert`'s only
     reachable `Send()` pattern is `Send(m_dpid, 0, ...)` (a broadcast), a hosting process's
     broadcast call today silently hits the self-send branch and never reaches remote clients.
   - `include/dplay.h`'s Doxygen `Status:` tags were stale `STUB` almost everywhere, despite most
     of `IDirectPlay2A` being real, tested, implemented behavior.
2. **`plan.md` extension**: appended "24-Hour Autonomous Stabilization Backlog," 141 atomic
   `TASK-24H-XXXX` tasks (133 originally `TODO`, 8 `BLOCKED`) covering Build/CTest, Headers,
   DirectDraw/DirectSound/DirectPlay tests, Diagnostics, Docs, and Integration — reconciled against
   (not replacing) the existing Phase 0–18 DirectPlay plan and `docs/directplay-design.md`'s 19
   Decisions.
3. **Implementation** (20 of the 141 tasks completed and verified this session):
   - `TASK-24H-0001..0004`: `FREE_DIRECT_BUILD_TESTS` CMake option, `tests/CMakeLists.txt`,
     `directplay_tests` built and CTest-registered under a `directplay` label. Fixed a real bug
     found during verification: the include path used `CMAKE_SOURCE_DIR`, which resolves to the
     *consuming* project's root when free-direct is `add_subdirectory()`'d, not free-direct's own —
     switched to `CMAKE_CURRENT_SOURCE_DIR`.
   - `TASK-24H-0016..0019, 0024`: three header compile smoke tests
     (`header_smoke_{ddraw,dsound,dplay}.cpp`) plus an automated `header_hygiene` CTest check
     (`tests/check_header_hygiene.sh`), all under a `headers` CTest label. The hygiene check
     classifies a grep hit as an allowed prose mention only if the line is a comment (starts with
     `*`/`/**`/`//`); verified it has teeth by injecting a real violation and observing it fail.
   - `TASK-24H-0080, 0081, 0092`: three new DirectPlay tests (46 → 49) closing named audit gaps —
     `Receive()`'s buffer-size-query contract through the real public API, `Release()`-without-
     `Close()` cleanup, and a **characterization test** locking in the DPID-0 self-send/broadcast
     collision described above (proves it, does not fix or decide it).
   - `TASK-24H-0083..0085`: fixed every stale `STUB` Doxygen tag in `include/dplay.h`
     (`DirectPlayCreate` and all `IDirectPlay2A`/`IDirectPlay` methods) to match real behavior,
     including an explicit warning on `Send`'s comment about the DPID-0 collision.
   - `TASK-24H-0111..0118`: audited DirectDraw/DirectSound/DirectPlay/ENet logging for ungated
     hot-path output — **found no gaps** (DirectDraw/DirectSound logging is correctly gated;
     DirectSound's 5 unconditional logs are failure-path, not hot-path; DirectPlay/ENet have no
     logging at all yet). One non-blocking overhead note recorded, not fixed: DirectDraw's debug
     flag checks re-read the environment on every call instead of caching like `IsPerfDebugEnabled`
     already does.
   - Every task above was verified by an actual build+test run (standalone `g++` compile for quick
     iteration, then a full CMake build+CTest run through both `../free-eggbert` and
     `../planetblupi`), not assumed. Each is committed and pushed separately (see `git log`).

**Prior session** (unchanged, condensed — see `docs/directplay-design.md` for full detail):
implemented DirectPlay session hosting/joining/messaging/discovery over loopback from near-scratch
(Decisions 10–18), then began ENet catch-up with real receive-side buffering (Decision 19).

## 4. Current blocker / main problem

**There is still no build- or test-breaking blocker.** Everything builds, all 49 committed tests
pass (up from 46).

The "problem" remains the same **scope/design fork with no single obvious next step** described in
prior sessions, now with one item sharpened from "suspected ambiguity" to "confirmed, tested bug":

- **DPID-0 broadcast/self-send collision** (`TASK-24H-0131`, `BLOCKED`): now proven by
  `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`, not just theorized. This is the single
  highest-priority unresolved design question in the project — resolving it requires a human
  decision (see Section 8) before any broadcast implementation can proceed.
- Which ENet catch-up task next (wiring `Connect()` for joining, the join handshake, discovery, or
  a committed reliable-delivery smoke test) — each needs its own small ENet-specific design answer
  (e.g. how a joining ENet call resolves a host address at all, since `DPSESSIONDESC2` has no
  address field — open since Decision 5; `TASK-24H-0132`, `BLOCKED`).
- Whether to pursue Phase 9's leftover questions (player-name observability, a player-lost state,
  duplicate-player definition — `TASK-24H-0135`/`0136`/`0137`, all `BLOCKED`).
- **New, safer alternative direction available now**: `plan.md`'s 24-Hour backlog has ~113 more
  `TODO` (non-blocked) tasks spanning DirectDraw/DirectSound test coverage (currently zero for
  both, despite being real, heavily-called subsystems), more DirectPlay regression tests, and
  documentation. These do **not** require a design decision first — see Section 8.

**Minor, non-blocking**: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` has never been exercised successfully
in this environment (no `libenet` system package installed here). `ctest` only discovers tests
from inside the `FREE_DIRECT` build subdirectory, not a target game's top-level build dir (Section
2) — not a defect, just a property of the consuming projects' own CMake structure.

## 5. Known bugs and limitations

- **Confirmed by test this session, not just theorized**: broadcast (`idTo == 0`/`DPID_ALLPLAYERS`)
  isn't implemented, and worse, a **hosting process's real broadcast call
  (`Send(m_dpid, 0, ...)`, `free-eggbert`'s only reachable `Send()` pattern) collides with
  self-send** because the host's own DPID is also 0 (Decision 3) — proven by
  `Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`. Whoever implements broadcast must resolve "is
  `0` broadcast, or the specific player who happens to have DPID `0`?" — this is not decided, and
  must not be assumed either way (`TASK-24H-0131`).
- **Incomplete**: `DirectPlay.cpp`'s ENet branch of `Open()` never calls `Connect()` for the
  joining role (only `Listen()` for hosting). Transport-level receive works (Decision 19), but
  nothing above it uses ENet for joining, the join handshake, unicast `Send()`/`Receive()`, or
  `EnumSessions()` yet — all of that is loopback-only today.
- **Incomplete**: a joining-role session still cannot address any specific *other* remote player in
  `Send()` (always `DPERR_INVALIDPLAYER`).
- **Structurally blocked, not just unimplemented**: a real `JoinReject` explanation packet for an
  over-`dwMaxPlayers` rejection — a rejected pending peer is never assigned a DPID to address one
  to.
- **Incomplete**: no host-side routing/forwarding between two non-host peers (`TASK-24H-0134`,
  `BLOCKED` pending a design decision on whether this is even needed).
- **Deliberately deferred, not a bug**: player short/long name storage (`TASK-24H-0135`,
  `BLOCKED`); a distinct "player-lost" state (`TASK-24H-0137`, `BLOCKED`); "duplicate player"
  validation has no concrete definition yet (`TASK-24H-0136`, `BLOCKED`).
- **Incomplete**: `EnumSessions()` only discovers a loopback-hosted session, not an ENet-hosted one.
- **Not provably tested, honestly flagged, not a defect**: `EnumSessions()`'s
  callback-returns-`FALSE`-stops-enumeration behavior cannot be exercised by a real test today —
  only one loopback-hosted session can exist per process at a time (`TASK-24H-0099`, open).
- **New this session, zero test coverage found (not a code defect, a coverage gap)**: DirectDraw
  and DirectSound both have **zero automated tests** despite DirectDraw's `Blt`/`BltFast` being the
  highest-call-frequency methods in both target games. `plan.md`'s 24-Hour backlog has ~30 DirectDraw
  and ~20 DirectSound test tasks queued (`TASK-24H-0026` through `0075`) — none started yet.
  `GetDC`/`ReleaseDC` remain `STUB` while `planetblupi`'s `IsIconPixel` (a live gameplay hit-test
  path) depends on them — flagged as the highest concrete DirectDraw risk in
  `docs/audit-24h-free-direct.md` §4.
- **New this session, non-blocking**: DirectDraw's `IsDirectDrawDebugEnabled()`/
  `IsPresentationDebugEnabled()`/`IsColorKeyDebugEnabled()` call `SDL_getenv` on every invocation
  (uncached), unlike `IsPerfDebugEnabled()` in the same file, which caches. Minor hot-path
  overhead, not an output/correctness bug; not fixed (out of scope for the audit task that found
  it — `TASK-24H-0111`).
- **Confirmed, low priority**: `GUID::Data1` is 8 bytes on this platform, not real DirectPlay's 4 —
  only matters for wire-compatible `GUID` serialization, not a current need.
- **Confirmed, not a FreeDirect bug**: `../free-eggbert`'s own DirectPlay lobby/session UI
  (`WM_PHASE_DP_*` handlers) is unwired in the game's current source — only gameplay-time
  `Send`/`Receive` are reachable. Out of `free-direct`'s scope to fix.
- **Unknown**: whether the `FREE_DIRECT` demo executable actually runs and renders correctly on a
  real display — still not run graphically in this or recent sessions.
- **New this session, minor**: `ctest` only discovers the new tests from inside the `FREE_DIRECT`
  build subdirectory, not a target game's top-level build dir (Section 2/4) — a property of the
  consuming projects, not a free-direct defect.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped types/constants/
interfaces only. **Hard invariant, now automatically enforced by CTest** (`header_hygiene` test,
`tests/check_header_hygiene.sh`), not just manual grep:
```bash
grep -niE "enet|SDL_|SdlNet" include/dplay.h   # must report zero real (non-comment) matches
```
`include/dplay.h`'s Doxygen `Status:` tags are now accurate (fixed this session) — most of
`IDirectPlay2A` is `IMPLEMENTED`/`PARTIAL`, not `STUB`; only `DirectPlayEnumerateA`/`W` remain
genuine stubs.

**Internal implementation** (`src/directplay/`, the actively-developed subsystem) — unchanged this
session, see prior session's architecture notes (below) for full detail:
- `DirectPlay.cpp` — public entry points and two anonymous-namespace classes: `DirectPlayImpl`
  (`IDirectPlay`) and `DirectPlay2AImpl` (`IDirectPlay2A`, the real implementation).
- `DirectPlaySession.hpp` — per-object state: lifecycle, `isHost`, player ID lists, `nextPlayerId`
  (sequential DPID allocator starting at `0`), owned `transport` and `messageQueue`.
- `DirectPlayMessageQueue.hpp` — bounded FIFO + `TryReceive()` (now exercised end-to-end through
  the public API for its buffer-size-query path too, `TASK-24H-0080`).
- `DirectPlayTransport.hpp` — `IDirectPlayTransport` abstract interface, backend-agnostic.
- `LoopbackDirectPlayTransport.hpp`/`.cpp` — in-process backend (default). Process-wide static
  registry keyed by a fixed port. Only one loopback-hosted session can exist per process at a time.
- `EnetDirectPlayTransport.hpp`/`.cpp` — real ENet backend, asymmetric hosting/joining roles.
- `DirectPlayWireProtocol.hpp`/`.cpp` — packet header type + serialize/deserialize.

**New this session**: `tests/CMakeLists.txt` (new file) builds and registers 5 CTest tests when
`FREE_DIRECT_BUILD_TESTS=ON` (default `OFF`): `directplay_tests` (label `directplay`),
`header_smoke_ddraw`/`header_smoke_dsound`/`header_smoke_dplay`/`header_hygiene` (label `headers`).
Gated at the root `CMakeLists.txt` level via `option(FREE_DIRECT_BUILD_TESTS ...)` +
`enable_testing()` + `add_subdirectory(tests)`.

**Invariants that must not be broken** (unchanged):
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever — now CTest-enforced.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- `plan.md` tasks are atomic (one thing each); phases build on each other in order.
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX surface "for completeness" without asking a human first (`CLAUDE.md`'s core policy).
- Backend selection (`LoopbackDirectPlayTransport` vs `EnetDirectPlayTransport`) is build-time
  only, via `FREE_DIRECT_ENABLE_ENET` — never add a runtime switch.

## 7. Useful commands

Configure + build, with FreeDirect's own tests enabled (must be done via a target game's
`add_subdirectory`, since free-direct never vendors SDL3 itself):
```bash
cd ../free-eggbert   # or ../planetblupi
cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON
cmake --build <build> -j8
```

Run all of FreeDirect's own tests (from inside the `FREE_DIRECT` build subdirectory — see Section
2's caveat about `ctest` discovery):
```bash
cd <build>/FREE_DIRECT && ctest --output-on-failure
# or a specific subsystem: ctest -L directplay   /   ctest -L headers
```

With the ENet transport backend enabled too (vendored copy at `third_party/enet`):
```bash
cmake -B <build> -DFREE_DIRECT_BUILD_TESTS=ON -DFREE_DIRECT_ENABLE_ENET=ON
```

Build + run just the DirectPlay tests standalone (fastest iteration loop, no CMake/SDL needed):
```bash
g++ -std=c++20 -Wall -Wextra \
    -I include -I ../free-api/include -I ../free-api/include_non_windows \
    -I src/directplay \
    src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
    tests/directplay_tests.cpp \
    -o directplay_tests
./directplay_tests   # expect "OK: all DirectPlay tests passed." (49/49)
```

Check the public-header/backend-leak invariant manually (also now a CTest test, see above):
```bash
bash tests/check_header_hygiene.sh include
```

No lint/format tooling is configured in this repository.

## 8. Next smallest tasks

Two tracks, since this session added a large non-DirectPlay-specific backlog:

**Track A — safe, no design decision needed** (pick any, in roughly priority order; see
`plan.md`'s 24-Hour backlog for full detail on each):
1. `TASK-24H-0026` through `0055`: stand up `tests/directdraw_tests.cpp` and add tests for the
   highest-risk untested paths found this session — `Blt`'s present-path copy, `BltFast`'s
   color-keyed blit, and `GetDC`/`ReleaseDC` (planetblupi's live `IsIconPixel` dependency). This is
   the single largest real coverage gap in the project today (zero DirectDraw tests despite being
   the highest-call-frequency subsystem).
2. `TASK-24H-0056` through `0075`: stand up `tests/directsound_tests.cpp` similarly.
3. `TASK-24H-0077..0079, 0082, 0093, 0100..0110`: more DirectPlay regression tests for previously
   uncommitted-scratch-harness-only behavior (`QueryInterface`/`DirectPlayCreate` failure paths,
   `CreatePlayer` malformed-size, etc.), plus implementing real `DirectPlayEnumerateA`/`W`
   (`TASK-24H-0100`, Decision 1 already decided the shape — just needs coding).
4. `TASK-24H-0051, 0074, 0094..0096`: create the four still-missing limitations/protocol docs
   (`docs/directdraw-limitations.md`, `docs/directsound-limitations.md`,
   `docs/directplay-limitations.md`, `docs/directplay-protocol.md`, `docs/networking-backends.md`).
5. `TASK-24H-0086..0090, 0130`: reconcile more `plan.md` Phase 6/7/9/16 checkbox drift against
   already-merged Decisions.
6. `TASK-24H-0097, 0098, 0108`: committed ENet transport-level tests using hardcoded loopback
   ports (does **not** require the ENet host-discovery design decision — see Track B).

**Track B — needs a human decision first** (do not start without asking; see `plan.md`'s 8
`BLOCKED` tasks, `TASK-24H-0091, 0131..0137`, for the full framing of each):
1. **Resolve the DPID-0 broadcast/self-send collision** (`TASK-24H-0131`) — now the highest-
   priority open question in the project, since it's a proven bug in `free-eggbert`'s only real
   `Send()` pattern, not a hypothetical.
2. **How does a joining ENet call resolve a host address?** (`TASK-24H-0132`)
3. LAN broadcast discovery (`TASK-24H-0133`), host-side routing (`TASK-24H-0134`), player names
   (`TASK-24H-0135`), duplicate-player definition (`TASK-24H-0136`), player-lost state
   (`TASK-24H-0137`).

## 9. Do not do yet

- **No broad refactor.** `DirectPlay.cpp`'s overall class structure works and is being extended
  incrementally by design — do not restructure it "while you're in there."
- **Never modify `../free-eggbert` or `../planetblupi` game source**, under any circumstances.
- **Do not decide the broadcast DPID-0-vs-`DPID_ALLPLAYERS` ambiguity unilaterally** — ask the user
  first (Section 4/5/8, `TASK-24H-0131`). A characterization test now exists
  (`Test_HostSendToDpidZero_CurrentlyOnlyReachesSelf`) proving today's exact behavior — do not
  change what it asserts without a real decision first.
- **Do not implement a `JoinReject` explanation packet for the over-`dwMaxPlayers` case** without a
  design conversation about pending-peer addressing first — structurally blocked, not just
  unimplemented.
- **Do not implement player name storage, duplicate-player validation, or a player-lost state**
  without a conversation first (`TASK-24H-0135/0136/0137`).
- **Do not start LAN broadcast discovery** without asking first (`TASK-24H-0133`).
- **Do not decide the ENet "how does a joining call resolve a host address" question unilaterally**
  (`TASK-24H-0132`).
- **Do not add `DPID_ALLPLAYERS`/`DPID_SYSMSG` constants to `include/dplay.h`** before
  `TASK-24H-0131` is resolved (`TASK-24H-0091` is explicitly `BLOCKED` on it) — adding the constant
  first would silently prejudge the semantics question.
- **Do not add a run-time backend-selection mechanism** for loopback-vs-ENet — build-time-only via
  `FREE_DIRECT_ENABLE_ENET` is a deliberate, already-made decision.
- **Do not add an SDL3_net backend** — explicitly deferred until ENet is stable (`plan.md` Phase 12).
- **Do not add any DirectX API surface, flag, or behavior beyond what `free-eggbert`/`planetblupi`
  call sites actually require** — this project's central scope rule (`CLAUDE.md`). Ask first.
- **Do not edit a target game's `CMakeLists.txt`** to work around the `ctest`-top-level-discovery
  caveat (Section 2/4) without asking first — it would fix a convenience issue at the cost of
  touching a file `CLAUDE.md` says to leave alone absent an explicit need.
- **No mass rewrites or speculative architecture changes.** Verify every change with an actual
  build+test run through both target games before considering it done — this session's 20
  completed tasks were each verified this way, not assumed.

## 10. Resume prompt

```
Read NEXT.md first (this file), especially Sections 4 and 8, then read
docs/audit-24h-free-direct.md and plan.md's "24-Hour Autonomous Stabilization Backlog" section for
full task detail. 121 of 141 TASK-24H-XXXX tasks remain (8 are BLOCKED on a human decision - do not
start those). Track A tasks (DirectDraw/DirectSound test coverage is the single biggest gap - zero
tests exist for either subsystem despite Blt/BltFast being the highest-call-frequency DirectDraw
methods in both target games) can be picked up directly without asking. Track B tasks need an
explicit decision from the user first - do not resolve the DPID-0 broadcast/self-send collision,
ENet host-address resolution, LAN discovery, host routing, player names, duplicate-player
definition, or player-lost state unilaterally. Pick one Track A task, implement it, verify with a
real build+test run through both ../free-eggbert and ../planetblupi (free-direct is never
standalone-buildable), mark it DONE in plan.md with a Verified note, commit+push, then update this
file's Sections 2/3/5 to match. Do not touch ../free-eggbert or ../planetblupi source. Do not
refactor unrelated code.
```
