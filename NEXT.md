# NEXT.md

## 1. Project summary

**FreeDirect** is a C++20 compatibility layer that reimplements a narrow, game-driven subset of
DirectX 3 (2D) so that specific legacy Win32/DirectX games can run on modern platforms without the
original DirectX SDK or Windows. It is not an attempt at full DirectX compatibility.

- **Main goal**: support exactly two named target games — `../free-eggbert` (*Speedy Blupi*, uses
  DirectDraw + DirectSound + DirectPlay) and `../planetblupi` (*Planet Blupi*, uses DirectDraw +
  DirectSound, confirmed zero DirectPlay usage). Scope is bounded by what these two games'
  real call sites actually need, not by DirectX API coverage in general (`CLAUDE.md`).
- **Current development phase**: `plan.md` Phase 5 ("ENet integration planning") is now complete
  except for one item explicitly deferred to Phase 16 (protocol documentation). Phases 0-4 are
  complete. Phase 6 ("Session hosting") is in progress: `Open()` build-time-selects its transport
  backend, genuinely starts the ENet listener (real OS-level `bind()`), and - as of this session -
  **a real ENet connection can now complete end-to-end**, driven purely by the caller repeatedly
  calling the public `IDirectPlay2A::Receive()` (matching `free-eggbert`'s own polling pattern),
  verified with a real external (non-FreeDirect) ENet client. The previous session's blocker (ENet
  needs something to call `enet_host_service()`; nothing did) is resolved. `Open()` now also
  generates a real, unique `guidInstance` when hosting with an all-zero one, and (as of this
  session) genuinely accepts **multiple simultaneous** real ENet client connections, each
  assigned a real DPID up to `dwMaxPlayers` - verified with two independent real ENet clients
  both completing a connection to the same hosted session at once.
- **Important architectural decisions**:
  - Public headers (`include/ddraw.h`, `include/dsound.h`, `include/dplay.h`) are DirectX-shaped
    only — no SDL3/ENet/SDL3_net type or symbol may ever appear in them.
  - SDL3 is the internal DirectDraw/DirectSound backend; ENet is the preferred DirectPlay network
    transport backend (over SDL3_net), chosen for built-in reliable UDP, ordering, and peer
    management.
  - DirectPlay is explicitly **not** Microsoft-wire-compatible. The only compatibility goal is
    FreeDirect-to-FreeDirect: two programs built against this same DirectPlay implementation must
    be able to host/join/exchange messages with each other.
  - Transport is abstracted behind `IDirectPlayTransport` (`src/directplay/DirectPlayTransport.hpp`),
    with `LoopbackDirectPlayTransport` (implemented, in-process, no sockets) and
    `EnetDirectPlayTransport` (real ENet lifecycle/`Listen`/`Connect`/`Shutdown`/reliable+unreliable
    `Send`/`Service`, plus - as of this session - real **multi-peer hosting**: `hostPeer_`
    (joining role) is now separate from `connectedPeers_`/`pendingPeers_` (hosting role,
    `docs/directplay-design.md` Decision 7); `Receive` (the transport-level method) still an
    honest `false` stub) as concrete backends. `Open(..., DPOPEN_CREATE)` build-time-selects
    (Decision 4), calls `Listen()` when hosting (Decision 5), and `DirectPlay2AImpl::Receive()`
    now assigns real DPIDs to incoming connections up to `dwMaxPlayers` (Decision 7) after calling
    `Service()` (Decision 6). `IDirectPlayTransport` gained `Listen`/`Connect`/`Send` parameters,
    `Service()`, and `HasPendingConnection()`/`AssignPendingConnection(DPID)` this phase - it now
    knows `DPID` as an opaque map key (a deliberate, documented narrowing of "the transport knows
    nothing about DPID", not a full reversal - it still never allocates or validates one).
  - `plan.md` is the authoritative English task list (every task atomic, one thing each);
    `CLAUDE.md` is the standing project charter/policy; this file (`NEXT.md`) is the living status
    snapshot.

## 2. Current status

**Build status**: the full CMake build works, but requires a non-default flag in this environment
because `free-direct` has no vendored SDL3 of its own:

```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```

This was broken for most of this session (a CMake target-visibility bug) and was fixed; see
Section 3. `free-api`, `free-direct` (static lib), and the `FREE_DIRECT` demo executable all
compile and link successfully with the command above. Adding `-DFREE_DIRECT_ENABLE_ENET=ON` also
now builds successfully, against a real vendored ENet copy, and now also compiles the new
`EnetDirectPlayTransport.cpp` skeleton (see Section 3). Both configurations (`ENET=OFF` default and
`ENET=ON`) were freshly configured and built end-to-end this session to confirm this.

**Test status**: `tests/directplay_tests.cpp` is a standalone, dependency-light file with its own
`main()` — **not yet wired into CMake/CTest** (that is `plan.md` Phase 15, not started). Last
confirmed run (manual, per the file's own documented build command): **14/14 tests passing**
(most recently added: two tests for `Open()`'s session-instance-`guidInstance` generation). No
other automated tests exist in the
repository.

**CLI/tools/apps/libraries currently available**:
- `libfree-direct.a` (static library) — the compatibility layer itself.
- `FREE_DIRECT` — a demo executable (`src/Main.cpp`) exercising DirectDraw surfaces/blits/palette;
  it compiles, but was not run/observed graphically in this session (no display verification was
  performed, only compilation).
- `tests/directplay_tests.cpp` — build/run manually per its own header comment; not an installed
  tool.

**Recently implemented features, condensed** (full detail for each item lives in `plan.md`'s own
per-task notes and Section 3 below; this list is intentionally a summary, not a growing log):
`plan.md` Phases 0-5 complete (call-site audit, COM correctness fixes, `DirectPlaySession` state
model, `DirectPlayMessageQueue`, `LoopbackDirectPlayTransport`, and a fully-real
`EnetDirectPlayTransport` - lifecycle/`Listen`/`Connect`/`Shutdown`/reliable+unreliable `Send`);
the SDL3 build blocker fixed. Seven decisions recorded in `docs/directplay-design.md` (single ENet
channel; `DPID` must become a `DWORD` with the host's first player at `DPID` `0`; build-time
backend selection; a fixed default ENet port; event servicing via `Service()`; multi-peer hosting
via a `DPID → ENetPeer*` map). Phase 6 in progress, and now substantially complete: `Open()`
build-time-selects its backend, genuinely starts the ENet listener (real OS-level `bind()`),
generates a real `guidInstance` when hosting, and - verified with real external ENet clients
driven purely through the public `IDirectPlay2A::Receive()` - **accepts multiple simultaneous
real connections, each assigned a real DPID up to `dwMaxPlayers`**.

**What does not work yet / is not implemented**:
- Transport-level `Receive()` (`EnetDirectPlayTransport::Receive()`, distinct from
  `IDirectPlay2A::Receive`) still unconditionally returns `false` - `Service()` destroys any
  received ENet packet without delivering it anywhere. No `plan.md` task covers real receive
  delivery yet (implied by Phase 10). `Send()`/`Receive()` on a *hosting*-role instance always
  return `false` too - no single implicit recipient exists once there can be many
  `connectedPeers_`; per-DPID-addressed send/receive is Phase 10's job (an intentional, documented
  behavior, not a bug). A *joining*-role instance's `Send()` still works as before.
- A pending connection beyond `dwMaxPlayers` is left connected-but-unassigned, not explicitly
  rejected/disconnected - `plan.md`'s separate "enforce `dwMaxPlayers` by rejecting new joins"
  task. `currentPlayers`/`remotePlayerIds` are updated on join but not yet on disconnect -
  `plan.md`'s separate "update `dwCurrentPlayers` as players join and leave" task.
- There is still no *DirectPlay-message-level* networked communication of any kind - `Send()`
  routing to anyone other than the sender is a no-op (Phase 10), and `EnumSessions`/`Connect()` at
  the `IDirectPlay2A` level don't exist yet (Phases 7/8), so nothing in `DirectPlay.cpp` itself
  actually drives an ENet client to `Connect()` to a discovered host. Every completed-connection
  proof so far used a raw external ENet client, not a second FreeDirect instance joining. The
  default (`FREE_DIRECT_ENABLE_ENET=OFF`) build's loopback self-send remains the only
  fully-FreeDirect-driven working path.
- The wire packet header (`DirectPlayWirePacketHeader`) exists, round-trips correctly, and
  validates buffer size/payload-length consistency on receive, but nothing constructs one from a
  real `DPSESSIONDESC2`/session yet, and `magic`/`version` mismatches are not rejected yet either
  (deliberately deferred — see `DirectPlayWireProtocol.hpp`'s file comment).
- The `DPID` size/starting-value decision (Section 4, `docs/directplay-design.md` Decision 3) is
  now written down, but **not implemented**: `include/dplay.h`'s `DPID` typedef is still
  `DWORD_PTR` (should become `DWORD`), and `DirectPlaySession::nextPlayerId` still starts at `1`
  (should start at `0`). Both are real `plan.md` Phase 9 (or Phase 6, for the host-namespace
  half) code tasks, not done yet.
- General `Send()` routing (to a player other than the sender) is a silent no-op — real routing,
  broadcast, and player-ID/payload validation are `plan.md` Phase 10, not started.
- `EnumSessions()` never finds anything (returns zero results) — real session discovery is
  `plan.md` Phase 8, not started. This is currently the *honestly correct* answer (nothing exists
  to discover yet), not a bug.
- DirectSound/DirectDraw hardening (`plan.md` Phases 13-14) not started.
- CTest/CI integration (`plan.md` Phase 15) not started.

## 3. Recent changes

- **Added** `CLAUDE.md` (project charter) and `plan.md` (phased task list, ~19 phases).
- **Added** `docs/directplay-callsite-audit.md` — real grep/read-based audit of DirectPlay usage
  in `../free-eggbert` and `../planetblupi`. Key findings: `planetblupi` has zero DirectPlay
  usage; `free-eggbert`'s DirectPlay lobby/session UI (`WM_PHASE_DP_*` handlers in `event.cpp`) is
  unwired (empty placeholders) — only gameplay-time `Send`/`Receive` are reachable; a DPID-size
  mismatch hazard exists (see Section 4).
- **Added** `docs/directplay-design.md` — records the "fake provider enumeration" decision
  (not yet implemented).
- **Modified** `src/directplay/DirectPlay.cpp` extensively: `QueryInterface` null-checks and
  `riid` dispatch fixed in both `DirectPlayImpl`/`DirectPlay2AImpl`; `DirectPlayCreate` output
  initialization and `DPERR_NOAGGREGATION` added; `Open`/`CreatePlayer`/`Close`/`EnumSessions`/
  `Send`/`Receive`/`Release` all rewritten from unconditional-`DP_OK` stubs to real
  state-driven behavior.
- **Added** `src/directplay/DirectPlaySession.hpp`/`.cpp` — real per-object state (lifecycle enum,
  player lists, session descriptor fields, DPID allocator, transport slot, message queue).
- **Added** `src/directplay/DirectPlayMessageQueue.hpp`/`.cpp` — `DirectPlayMessagePacket` +
  FIFO queue + `TryReceive()` (the exact logic `DirectPlay2AImpl::Receive()` delegates to) +
  size/oversize bounds (`kMaxQueuedMessages=256`, `kMaxPayloadBytes=4096`).
- **Added** `src/directplay/DirectPlayTransport.hpp` (`IDirectPlayTransport` interface) and
  `src/directplay/LoopbackDirectPlayTransport.hpp`/`.cpp` (in-memory, real implementation, wired
  into `Open`/`Close`/`Send`'s self-send path).
- **Added** `tests/directplay_tests.cpp` — 7 tests covering the message queue and loopback
  behavior end-to-end through the real `IDirectPlay2A` interface.
- **Fixed** a CMake bug in `CMakeLists.txt`: `SDL3::SDL3`/`SDL3_image::SDL3_image`/
  `SDL3_mixer::SDL3_mixer` imported targets created via `find_package()` inside `free-api`'s
  nested `add_subdirectory()` were invisible in `free-direct`'s own top-level scope. Fixed by
  re-running `find_package(... CONFIG QUIET)` directly in `free-direct`'s scope when the targets
  are still missing after the subdirectory returns.
- **Added** `FREE_DIRECT_ENABLE_ENET`/`FREE_DIRECT_USE_SYSTEM_ENET` CMake options, vendored/system
  ENet detection, and a `FreeDirect::ENet` ALIAS target linked `PRIVATE`.
- **Vendored** `third_party/enet` as a real git submodule (`https://github.com/lsalzman/enet`,
  pinned `v1.3.18-17-g5a9c537`); fixed a real upstream-CMake include-path propagation bug found
  during verification (`target_include_directories(enet PUBLIC ...)` added).
- **Updated** `README.md` with the `-DFREE_USE_SYSTEM_SDL=ON` build instructions.
- **Added** `src/directplay/DirectPlayWireProtocol.hpp`/`.cpp` — `DirectPlayWirePacketType`
  (`Join`/`JoinAccept`/`JoinReject`/`Data`/`Discovery`/`DiscoveryResponse`) and
  `DirectPlayWirePacketHeader` (magic, version, type, `applicationGuid`, `sessionGuid`, `idFrom`,
  `idTo`, `payloadLength`), plus `SerializeDirectPlayWireHeader`/`DeserializeDirectPlayWireHeader`
  (flat, padding-free, per-field `memcpy`). Zero ENet dependency — pure data structure, matching
  `plan.md` Phase 5's explicit intent to stand this up before any real ENet code exists. Wired into
  `CMakeLists.txt`'s `target_sources(free-direct ...)`. Added
  `Test_WireHeaderRoundTrip_PreservesAllFields` as an 8th test in `tests/directplay_tests.cpp`.
  Does **not** yet: populate a header from a real `DPSESSIONDESC2`/session (nothing constructs one
  outside the test).
- **Added** `TryDeserializeDirectPlayWireHeader(data, dataSize)` to the same file — validates an
  untrusted buffer before parsing it: rejects (`std::nullopt`) a buffer smaller than
  `kDirectPlayWireHeaderSize`, or one whose parsed `payloadLength` disagrees with the actual
  trailing byte count. Does not check `magic`/`version` (deliberately deferred, see the header's
  file comment). Added three tests to `tests/directplay_tests.cpp` (now 11 total): truncated
  buffer, mismatched payload length, and the accept case.
- **Added** `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp` — the `IDirectPlayTransport`
  skeleton for the real ENet backend: an `ENetHost*`/`ENetPeer*` member pair (both
  null-initialized) and every interface method stubbed (`Listen`/`Connect`/`Send`/`Receive` return
  `false`; `Shutdown` no-ops; constructor/destructor are trivial). Not selected by `Open()` yet.
  `EnetDirectPlayTransport.cpp` is added to `CMakeLists.txt`'s `target_sources(free-direct ...)`
  only inside the existing `if(FREE_DIRECT_ENABLE_ENET)` block, so the default build is unaffected.
  Verified with two fresh, separate configure+build runs: `-DFREE_DIRECT_ENABLE_ENET=OFF` (default)
  and `-DFREE_DIRECT_ENABLE_ENET=ON`, both succeeding end-to-end; also re-ran the standalone
  DirectPlay test suite (11/11, unaffected) and re-confirmed `include/dplay.h` has zero ENet/SDL
  identifiers.
- **Added real ENet lifecycle** to `EnetDirectPlayTransport`'s constructor/destructor: a
  process-wide `enet_initialize()`/`enet_deinitialize()` reference count
  (`g_enetLiveInstances`/`g_enetInitialized`, guarded by `g_enetLifecycleMutex` - an anonymous
  namespace in `EnetDirectPlayTransport.cpp`, mirroring the mutex-guarded-state pattern already
  used in `src/directsound/DirectSound.cpp`), plus a new `IsEnetReady()` accessor so this can be
  tested without needing `Listen()`/`Connect()` (still stubs) to exist first. `Listen`/`Connect`/
  `Send`/`Receive`/`Shutdown` are unchanged stubs - this task was scoped to lifecycle only.
  Verified for real (not just "didn't crash") with a standalone smoke test compiled outside the
  repo (not committed): a single instance initializes successfully; three concurrent instances
  share one init, and destroying the middle one first doesn't break the other two's readiness
  (proves shared refcounting, not per-instance init/deinit); a fresh instance after a full
  teardown to zero re-initializes successfully (proves it isn't a one-shot state). Also re-ran the
  full `FREE_DIRECT_ENABLE_ENET=ON` and default `OFF` CMake builds and the 11/11 test suite.
- **Added real ENet host creation** to `EnetDirectPlayTransport::Listen()`. Changed
  `IDirectPlayTransport::Listen()`'s signature from no-argument to `Listen(std::uint16_t port)`
  (confirmed by grep there were zero existing callers of the old signature anywhere - updated the
  only other override, `LoopbackDirectPlayTransport::Listen()`, to accept and ignore the new
  parameter). `Listen()` calls `enet_host_create` with `ENetAddress{ENET_HOST_ANY, port}` and two
  provisional placeholder constants (`kMaxPeers = 32`, `kChannelLimit = 1` - neither derived from a
  specific game requirement yet); fails (no host created) if ENet never initialized for this
  instance, or if a host already exists (no silent leak/replace on a second call). Added a
  test-only `HasHost()` accessor. `Shutdown()`/the destructor now actually destroy the created
  `ENetHost` (`enet_host_destroy`) - the necessary other half of creating it, not a separate task.
  **Verified for real**, not just "returned true", with a standalone whitebox smoke test (not
  committed, uses a `#define private public` trick to reach the private `host_` member since no
  public "service this host" method exists yet): a genuine raw ENet client
  (`enet_host_connect` to `127.0.0.1:<port>`) completed a real protocol handshake with the host
  `Listen()` created, confirmed via `ENET_EVENT_TYPE_CONNECT` on both sides after servicing both
  hosts; a second `Listen()` call while already hosting correctly failed. Also re-verified both
  CMake build configurations end-to-end and the 11/11 test suite.
- **Added real ENet client creation + peer connection** to `EnetDirectPlayTransport::Connect()`
  (implemented as one task, not plan.md's two separate checkboxes - see plan.md's own note on why
  a created-but-never-connected client host isn't independently testable). Changed
  `IDirectPlayTransport::Connect()`'s signature from no-argument to
  `Connect(const char* address, std::uint16_t port)` (same zero-existing-callers situation as
  `Listen()`'s earlier change; updated `LoopbackDirectPlayTransport::Connect()` to accept and
  ignore both). `Connect()` creates a listen-address-less client `ENetHost`, resolves `address` via
  `enet_address_set_host`, and calls `enet_host_connect`; fails cleanly with no host/peer left
  behind on any failure (ENet not ready, already have a host/peer, bad address, `enet_host_connect`
  itself failing). Added a test-only `HasPeer()` accessor (explicitly documented as "attempt
  queued", not "connected"). `Shutdown()`/the destructor now also clear `peer_` (freed along with
  its host by `enet_host_destroy`, so leaving the pointer set would dangle). **Verified for real**
  with a standalone whitebox smoke test (not committed): a `Listen()`-created server and a
  `Connect()`-created client - both real `EnetDirectPlayTransport` instances, not mixed with raw
  ENet this time - completed a genuine handshake with each other (`ENET_EVENT_TYPE_CONNECT` on
  both sides); a second `Connect()` call while already connecting/connected correctly failed; a
  syntactically-invalid address failed cleanly with no host/peer left behind. Also re-verified both
  CMake build configurations end-to-end and the 11/11 test suite.
- **Added real ENet disconnect handling** to `EnetDirectPlayTransport::Shutdown()`. Now calls
  `enet_peer_disconnect(peer_, 0)` first (when a `peer_` exists), then services `host_` in a small
  bounded loop (`kDisconnectPollAttempts = 10` × `kDisconnectPollTimeoutMs = 100`, provisional
  placeholders) waiting for `ENET_EVENT_TYPE_DISCONNECT` before destroying the host - a real,
  observable-by-the-remote-peer graceful disconnect, not a silent local teardown. Scoped to the
  single `peer_` this class already tracks (the one `Connect()` created) - a host tracking
  multiple connected peers is Phase 6/10's job. **Verified for real** with a standalone whitebox
  smoke test (not committed): after a real connection is established, the client's `Shutdown()`
  (called through the public API, no whitebox needed for that call) causes the *server*, serviced
  independently, to observe a genuine `ENET_EVENT_TYPE_DISCONNECT`; calling `Shutdown()` again on
  either instance afterward is safe. Also re-verified both CMake build configurations end-to-end
  and the 11/11 test suite.
- **Added real reliable packet send** to `EnetDirectPlayTransport::Send()`. Now calls
  `enet_packet_create(data, size, ENET_PACKET_FLAG_RELIABLE)` + `enet_peer_send(peer_, 0, packet)`
  (channel `0`, matching `Listen()`/`Connect()`'s existing single-channel assumption) +
  `enet_host_flush(host_)` so the packet is pushed out promptly. Scoped, like `Shutdown()`, to the
  single `peer_` this class tracks; fails cleanly (no leak, no crash) with no `peer_`, or if
  `enet_packet_create`/`enet_peer_send` fail. `Receive()` deliberately left untouched -
  `plan.md` Phase 5 has no separate "implement transport-level receive" checkbox. **Verified for
  real** with a standalone whitebox smoke test (not committed): a real `Listen()`/`Connect()` pair
  connects; the client's `Send()` transmits a payload the server receives byte-for-byte
  (`memcmp`-verified) via a genuine `ENET_EVENT_TYPE_RECEIVE`, serviced independently; `Send()` on
  a peer-less instance fails cleanly. Also re-verified the `FREE_DIRECT_ENABLE_ENET=ON` CMake build
  end-to-end, the 11/11 test suite, and `include/dplay.h`'s zero ENet/SDL identifiers.
- **Added real unreliable packet send**, after asking the user (the Phase 0 audit still shows no
  real `free-eggbert`/`planetblupi` call site needs it - the user chose to add it anyway for
  interface completeness). `IDirectPlayTransport::Send()` gained a `bool reliable` parameter
  (confirmed by grep there was exactly one existing caller - `DirectPlay.cpp`'s self-send path,
  updated to always pass `true`, preserving current behavior exactly; mapping the real
  `DPSEND_GUARANTEED` flag to this parameter is the next, separate task).
  `LoopbackDirectPlayTransport::Send()` accepts and ignores it. `EnetDirectPlayTransport::Send()`
  now picks `ENET_PACKET_FLAG_RELIABLE` or `ENET_PACKET_FLAG_UNSEQUENCED` based on `reliable`.
  **Verified for real** with a standalone whitebox smoke test (not committed): sent one reliable
  and one unreliable payload over a real connected pair; inspected the *received* packet's
  `flags` field on the server side for both (ENet preserves flags through delivery) and confirmed
  they differ exactly as expected - not just a local bookkeeping value. Also re-verified both CMake
  build configurations end-to-end, the 11/11 test suite, and `include/dplay.h`'s zero ENet/SDL
  identifiers.
- **Mapped `DPSEND_GUARANTEED` to the transport's `reliable` parameter.**
  `DirectPlay2AImpl::Send()` (`DirectPlay.cpp`) now computes `const bool reliable = (dwFlags &
  DPSEND_GUARANTEED) != 0;` and passes it to `session_.transport->Send(...)`, replacing the
  previous hardcoded `/*reliable=*/true`. Since every observed real `free-eggbert` call site sets
  `DPSEND_GUARANTEED`, this changes nothing for the one real call pattern - it only stops ignoring
  what the caller actually asked for. **Verified for real**: re-ran the (now 12, one new) tests in
  `tests/directplay_tests.cpp` - added `Test_LoopbackSendWithoutGuaranteedFlag_StillSucceeds`,
  which calls `Send()` with `dwFlags = 0` through the real end-to-end
  `IDirectPlay2A::Send()`/`Receive()` path and asserts `DP_OK` with the payload intact
  (`LoopbackDirectPlayTransport` ignores `reliable` entirely, so this cannot observe a behavioral
  *difference* the way the ENet smoke tests did - it pins down that the new flag-derived path
  works and gives a regression anchor for Phase 6). Also re-verified both CMake build
  configurations end-to-end and `include/dplay.h`'s zero ENet/SDL identifiers.
- **Added two decision write-ups to `docs/directplay-design.md`** — documentation only, no code
  changed, no build/test re-verification needed (nothing under `src/`/`include/`/`tests/` touched):
  - **Decision 2** (single ENet channel): closes out `plan.md` Phase 5's "decide the default ENet
    channel layout" task, formally documenting what the already-shipped `EnetDirectPlayTransport`
    code (`kChannelLimit = 1`, channel `0` everywhere) already assumed.
  - **Decision 3** (`DPID` width and starting value): resolves both `docs/
    directplay-callsite-audit.md` §5 (size: `DPID` should be a 4-byte `DWORD`, not the current
    8-byte `DWORD_PTR`, to match real DirectPlay and `free-eggbert`'s hardcoded-32-byte-stride
    `NetPlayer` layout assumption) and §6 (starting value: the host's first player must be
    assigned `DPID` `0`, not `1`, because `free-eggbert/src/event.cpp:4692-4699` populates the
    local player's slot with its real DPID and `src/network.cpp:262-289`'s `CNetwork::Receive`
    looks it up via `from == i` at `i == 0`) - traced directly against `../free-eggbert` source
    (not just the existing audit doc) to confirm both citations still hold. Satisfies `plan.md`
    Phase 9's acceptance criteria requiring this written decision to exist before Phase 9 code
    lands. **Not implemented yet**: `include/dplay.h`'s `DPID` typedef is still `DWORD_PTR`, and
    `DirectPlaySession::nextPlayerId` still starts at `1` - both remain real Phase 6/9 code tasks.
  - **Decision 4** (`Open()` backend selection): asked the user directly (real DirectPlay has no
    equivalent concept), who chose build-time selection via `FREE_DIRECT_ENABLE_ENET` over a
    run-time mechanism (env var, new API parameter) - no new `IDirectPlay`/`DPSESSIONDESC2`
    surface added.
- **Implemented Decision 4** (same session, after the decision was written down):
  `CMakeLists.txt`'s existing `if(FREE_DIRECT_ENABLE_ENET)` block (the one linking
  `FreeDirect::ENet` and adding `EnetDirectPlayTransport.cpp`) now also adds
  `target_compile_definitions(free-direct PRIVATE FREE_DIRECT_ENABLE_ENET=1)`. `DirectPlay.cpp`
  now `#ifdef FREE_DIRECT_ENABLE_ENET`-guards both the `EnetDirectPlayTransport.hpp` `#include`
  and `Open()`'s transport construction, replacing the previous unconditional
  `LoopbackDirectPlayTransport`. Starting the actual ENet listener (calling `Listen()` with a real
  port) is explicitly **not** done here - the transport is selected but never told to listen, so
  `Send()` on a freshly `Open()`ed ENet-backed session now fails with `DPERR_GENERIC` (no `peer_`)
  rather than succeeding, which is the expected, honest behavior for a listener that hasn't
  started yet. **Verified for real** (not just "compiles") with a standalone smoke test compiled
  twice from the identical `DirectPlay.cpp` - once without and once with
  `-DFREE_DIRECT_ENABLE_ENET=1` (matching what CMake defines) - observing `DP_OK` (loopback
  self-send) vs. `DPERR_GENERIC` (ENet, no peer) for the exact same `Open`/`CreatePlayer`/`Send`
  call sequence. Also re-verified both full CMake builds (`ENET=OFF`/`ON`) end-to-end, the 12/12
  `tests/directplay_tests.cpp` suite (built without the macro, confirmed unaffected), and that
  `include/dplay.h` has zero ENet/SDL identifiers.
- **Decided (asked the user) and implemented Decision 5** (default ENet listen port): added
  `kDefaultDirectPlayEnetPort = 51321` to `EnetDirectPlayTransport.hpp` (`DPSESSIONDESC2` has no
  port-like field, so this is a fixed FreeDirect-internal constant, not derived from any
  DirectPlay value). `Open(..., DPOPEN_CREATE)` now calls
  `session_.transport->Listen(kDefaultDirectPlayEnetPort)` when hosting under the ENet backend,
  resetting the transport and returning `DPERR_CANTCREATESESSION` if `Listen()` fails.
  **Verified for real** with two standalone smoke tests (not committed): a port-conflict test (a
  second, independent host on the same default port fails with `DPERR_CANTCREATESESSION` because
  the OS-level UDP socket is already bound by the first - real evidence of a genuine `bind()`;
  releasing the first and opening a third succeeds again, proving the port is genuinely released).
  **A third test - a real raw ENet client actually connecting to the hosted port - failed, and
  investigating why surfaced a genuine, previously-undocumented gap**: ENet is poll-driven with no
  internal thread, and `Open()` never services the hosted transport's `ENetHost` after `Listen()`
  returns (only `Shutdown()`'s own internal disconnect-wait loop services it, and only during
  teardown) - so no real connection can complete end-to-end today, independent of whether
  `Connect()`/`EnumSessions` exist. Documented in `docs/directplay-design.md` Decision 5's Caveat
  and added as a new, explicit `plan.md` Phase 6 task (inserted directly after this one) so it
  isn't silently left implicit. Also re-verified both CMake builds end-to-end and the 12/12 test
  suite.
- **Decided (asked the user) and implemented Decision 6** (event servicing): added
  `virtual void Service() = 0;` to `IDirectPlayTransport`; `DirectPlay2AImpl::Receive()`
  (`DirectPlay.cpp`) calls `session_.transport->Service()` once, unconditionally, before
  consulting the message queue - the user's chosen option (a new interface method called from
  `Receive()`), matching `free-eggbert`'s own already-existing call-`Receive()`-repeatedly polling
  pattern rather than a new API the game would never call, or a background thread that would add
  real thread-safety requirements to classes that have none today.
  `LoopbackDirectPlayTransport::Service()` is a no-op. `EnetDirectPlayTransport::Service()` drains
  pending events with a non-blocking `enet_host_service(host_, &event, 0)` loop:
  `ENET_EVENT_TYPE_CONNECT` adopts the peer as `peer_` if none is tracked yet (extends the
  existing single-peer model to the hosting role); `ENET_EVENT_TYPE_DISCONNECT` clears `peer_` if
  it matches; `ENET_EVENT_TYPE_RECEIVE` destroys the packet without delivering it (transport-level
  `Receive()` remains an honest `false` stub). **Verified for real, closing Decision 5's Caveat**:
  a standalone smoke test (not committed) drove a real, external (non-FreeDirect) ENet client to a
  genuinely *completed* connection with `Open()`'s hosted session, using nothing but repeated
  calls to the **public** `IDirectPlay2A::Receive()` - no whitebox access needed this time, unlike
  every earlier `EnetDirectPlayTransport`-level verification in this phase. A second smoke test
  confirmed the connect/disconnect bookkeeping directly: a first client is adopted (`HasPeer()`
  becomes `true`); a second, concurrently-connecting client is not adopted (existing tracking
  undisturbed); the first client's graceful `Shutdown()` is observed and clears `HasPeer()` back
  to `false`. Also re-verified both CMake build configurations end-to-end, the 12/12
  `tests/directplay_tests.cpp` suite (unaffected), and that `include/dplay.h` has zero ENet/SDL
  identifiers.
- **Added session instance GUID generation.** `Open()` now generates a real `guidInstance` when
  hosting (`DPOPEN_CREATE`) and the caller's `DPSESSIONDESC2.guidInstance` is all-zero, writing it
  back into the caller's struct (matching real DirectPlay's behavior); a caller-supplied non-zero
  value, or a joining call, is preserved as-is. New anonymous-namespace helper
  `GenerateSessionInstanceGuid()` in `DirectPlay.cpp` fills `Data1`/`Data2`/`Data3`/`Data4`
  individually via `std::mt19937_64`/`std::random_device` (no RFC 4122 bits - not needed, no wire
  compatibility goal). **Found a real, previously-unnoted portability wrinkle while writing this**:
  `GUID::Data1` is `unsigned long`, which is 8 bytes on this platform, not the 4 bytes real
  DirectPlay's `Data1` documents - `sizeof(GUID)` is 24 here, not 16. Not fixed (changing the
  typedef is a separate, bigger concern, same category as the `DPID` width question in Decision
  3) - just avoided assuming a fixed byte count in the new code, and noted here for whoever
  eventually looks at cross-platform `GUID` layout. Added
  `DirectPlaySession::sessionInstanceGuid` (reset in `Close()`). **Verified** with two new tests
  (`tests/directplay_tests.cpp`, now 14 total): two separate hosted sessions get different,
  non-zero, written-back GUIDs; a caller-supplied non-zero `guidInstance` is preserved exactly.
  Also re-verified both CMake build configurations end-to-end and that `include/dplay.h` has zero
  ENet/SDL identifiers.
- **Confirmed "store the session descriptor on `DirectPlaySession`" needs no new code.**
  Cross-checked every `DPSESSIONDESC2` field against `DirectPlaySession`'s members: everything
  with a real `free-eggbert` need (`guidApplication`, `guidInstance`, `dwMaxPlayers`,
  `dwCurrentPlayers`, session name, password) was already stored (Phase 2, plus `guidInstance`
  just above). `dwFlags`/`dwUser1`-`4`/reserved fields are deliberately **not** stored -
  `free-eggbert` only ever writes `dwFlags` before `Open()` and never reads it back, no
  host-migration/keep-alive behavior exists anywhere in `plan.md`, and the `dwUser*`/reserved
  fields have zero observed usage. Storing them now would be exactly the speculative "for
  completeness" storage `CLAUDE.md` prohibits.
- **Implemented real multi-peer hosting** (asked the user; chose a `DPID → ENetPeer*` map).
  `docs/directplay-design.md` Decision 7 covers the full design, including sub-questions the
  literal ask didn't resolve by itself: hosting (`Listen()`) and joining (`Connect()`) roles are
  asymmetric and now tracked separately (`EnetDirectPlayTransport`'s `hostPeer_` for the joining
  role, unchanged; new `connectedPeers_`/`pendingPeers_` for the hosting role); DPID allocation
  policy stays entirely in `DirectPlaySession`/`DirectPlay.cpp` - the transport gained two new
  `IDirectPlayTransport` methods (`HasPendingConnection()`/`AssignPendingConnection(DPID)`) that
  let a caller resolve a pending connection without the transport ever exposing an `ENetPeer*`.
  `DirectPlay2AImpl::Receive()` extends its `Service()` call (Decision 6) with a loop: while
  hosting, under `dwMaxPlayers` (`0` correctly means "no limit", matching real DirectPlay - this
  was never validated anywhere in the codebase before this task), and a pending connection exists,
  allocate the next DPID, assign it, record it in `remotePlayerIds`/`currentPlayers`. Left for
  later, separate `plan.md` tasks: explicitly rejecting an over-the-cap pending connection (it
  just stays connected-but-unassigned today), and decrementing `currentPlayers`/removing from
  `remotePlayerIds` on disconnect. `Send()`/`Receive()` on a hosting-role instance now always
  return `false` (documented, intentional - no single implicit recipient exists with potentially
  many peers; the old single-`peer_` model's "it happened to work for the first peer" was an
  accident, not a design). `Shutdown()` generalizes to gracefully disconnect every peer an
  instance knows about, not just one. **Verified for real** with two standalone smoke tests (not
  committed): (1) directly against `EnetDirectPlayTransport`, three real ENet clients all connect
  and queue as pending; two are assigned DPIDs (simulating a cap), the third stays pending;
  disconnecting one of the two assigned peers correctly shrinks `connectedPeers_` (removal by
  `ENetPeer*` match) without disturbing the still-pending third, which is then assigned
  successfully; assigning with nothing pending correctly fails. (2) End-to-end through the real
  `Open()`/`CreatePlayer()`/`Receive()` path: two independent real ENet clients both complete a
  genuine connection to the same hosted session *simultaneously* - impossible under the previous
  single-`peer_` model. (`dwMaxPlayers` enforcement itself could only be verified at the transport
  level - `IDirectPlay2A` has no player-count-observing method in this narrow subset.) Also
  re-verified both CMake build configurations end-to-end and the 14/14 test suite (unaffected - no
  committed test touches multi-peer hosting).

## 4. Current blocker / main problem

**There is no build- or test-breaking blocker right now.** The most recently-active blocker (the
SDL3 CMake target-visibility bug) was found and fixed this session (Section 3), and is verified
working. The closest thing to an open problem is an **unresolved design decision**, not a failure:

- **Symptom**: none observed yet (no test reproduces it) — this is a hazard identified by static
  audit, not a runtime failure.
- **Issue**: `include/dplay.h` defines `DPID` as `DWORD_PTR` (8 bytes on 64-bit), but real
  DirectPlay's `DPID` is `DWORD` (4 bytes). `../free-eggbert/src/event.cpp`'s `NetSearchPlayer`
  and `NetStartPlay` walk arrays of its own `NetPlayer` struct using **hardcoded 32-byte
  pointer-arithmetic strides** that only match the original 4-byte `DPID` layout. If
  `free-eggbert` is ever compiled against `free-direct`'s current `dplay.h` on a 64-bit target,
  these two functions would silently read the wrong memory offsets.
- **Failing command/test**: none — not yet reproduced by any test; found via static analysis in
  `docs/directplay-callsite-audit.md` §5.
- **Affected files**: `include/dplay.h` (the `DPID` typedef), and (read-only, not to be modified)
  `../free-eggbert/src/event.cpp`'s `NetSearchPlayer`/`NetStartPlay`.
- **Suspected cause**: `DPID` was originally typedef'd as `DWORD_PTR` "to stay ABI-safe on both
  32-bit and 64-bit hosts" (per its own header comment), without accounting for `free-eggbert`'s
  raw-pointer-arithmetic assumption about struct layout.
  A second, related but separate question (DPID *starting value*, not size) was also open: whether
  DPID `0` must stay reserved (matching real DirectPlay's `DPID_SYSMSG`/`DPID_ALLPLAYERS`
  convention, and `DirectPlaySession::nextPlayerId`'s current placeholder default of `1`) or
  whether it must be assignable to the first real player to match `free-eggbert`'s own
  `CNetwork::Receive`'s `from == i` index-based comparison.
- **What has already been tried**: **both questions now have an explicit written decision** —
  `docs/directplay-design.md` Decision 3 (added this session): `DPID` should become a 4-byte
  `DWORD` (matching real DirectPlay and `free-eggbert`'s layout assumption), and the host's first
  player should get `DPID` `0` (matching `free-eggbert`'s apparent assumption, breaking with real
  DirectPlay's reservation convention). **The decision is written but not yet implemented** —
  `include/dplay.h`'s typedef and `DirectPlaySession::nextPlayerId`'s initial value are both
  unchanged. It still does not block current work, since no code yet depends on the answer
  (`plan.md` Phase 9, real DPID allocation, has not started).

A secondary, much smaller open item: `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` (the system-package ENet
path) has never been exercised successfully in this environment, since no `libenet` system
package is installed here — only the vendored-submodule ENet path has been verified. This is not
currently blocking anything, since the vendored path is the proven, working default.

## 5. Known bugs and limitations

- **Confirmed portability wrinkle, not fixed, low priority today**: `GUID::Data1` is `unsigned
  long`, 8 bytes on this (Linux/64-bit) platform, not the 4 bytes real DirectPlay's `Data1`
  documents - `sizeof(GUID)` is 24 here, not the 16 a real Windows build would have. Discovered
  while adding `GenerateSessionInstanceGuid()` (`DirectPlay.cpp`), which was written to fill named
  fields individually rather than assume a byte count. Same category of issue as the `DPID` width
  question (Decision 3), but not yet formally decided/documented as its own Decision - only
  matters if FreeDirect ever needs wire-compatible `GUID` serialization across platforms with
  different `unsigned long` widths (e.g. a 32-bit Windows peer talking to this 64-bit Linux build),
  which is not a current, demonstrated need.
- **Confirmed hazard, decision written, not yet fixed**: `DPID` size mismatch (`DWORD_PTR` vs. real
  DirectPlay's `DWORD`) — see Section 4. `docs/directplay-callsite-audit.md` §5;
  `docs/directplay-design.md` Decision 3 records the fix (`DPID` should become `DWORD`) but the
  typedef itself is unchanged.
- **Confirmed, not a FreeDirect bug**: `../free-eggbert`'s own DirectPlay lobby/session UI
  (`WM_PHASE_DP_*` handlers in `src/event.cpp`) is unwired — ten empty placeholder bodies, and a
  whole-repository grep confirms `NetCreate`/`NetEnumSessions`/`JoinSession`/`CreateSession`/
  `NetStartPlay` all have zero callers anywhere in that game's current source. Only gameplay-time
  `Send`/`Receive` are reachable. This is a `free-eggbert` source-completeness gap, out of
  `free-direct`'s scope to fix (game source must not be modified).
- **Incomplete**: `EnetDirectPlayTransport` has real `Listen()`, `Connect()`, `Shutdown()`,
  reliable/unreliable `Send()`, and now real `Service()` (event servicing), but transport-level
  `Receive()` still unconditionally returns `false` - any received ENet packet is destroyed by
  `Service()`, not delivered anywhere.
- **Resolved this session** (previously listed here as a confirmed gap): `EnetDirectPlayTransport`
  now genuinely tracks multiple connected peers when hosting (`connectedPeers_`, a
  `DPID → ENetPeer*` map) - see `docs/directplay-design.md` Decision 7. Still incomplete: an
  over-the-cap pending connection is never explicitly rejected (just left unassigned), and
  `Send()`/`Receive()` have no way to address a *specific* connected peer yet (Phase 10).
- **Resolved this session** (previously listed here as a confirmed gap): a real ENet connection
  can now complete end-to-end through `Open()`'s hosted listener, driven by repeated calls to the
  public `IDirectPlay2A::Receive()` - see `docs/directplay-design.md` Decision 6. There is
  currently no *FreeDirect-driven* way to reach that listener though (no `EnumSessions`/`Connect()`
  at the `IDirectPlay2A` level yet - Phases 7/8), so today's verification used a raw external ENet
  client, not a second FreeDirect instance joining.
- **Incomplete**: `Send()` only handles the exact self-send case (`idTo == idFrom`); any other
  recipient is a silent no-op pending Phase 10.
- **Incomplete**: `EnumSessions()` always reports zero sessions (correct today, since nothing
  hosts yet) pending Phase 8.
- **Incomplete**: no CTest/CI wiring for `tests/directplay_tests.cpp` (Phase 15).
- **Needs verification**: the system-installed-ENet CMake path (`FREE_DIRECT_USE_SYSTEM_ENET=ON`)
  — logic is written and reviewed, but never actually exercised against a real `libenet` install.
- **Risky assumption, flagged for later refinement**: in `Send()`'s self-send path, any
  `DirectPlayMessageQueue::Enqueue()` failure (queue full **or** oversize payload) currently
  returns `DPERR_SENDTOOBIG` — imprecise for the "queue full" case specifically. Flagged in
  `plan.md` for Phase 10 to refine once full routing/error-code mapping is implemented.
- **Risky assumption**: `Open()` validates `dwFlags` against the `DPOPEN_CREATE`/`DPOPEN_JOIN`/
  `DPOPEN_OPENSESSION` mask but does not enforce that `DPOPEN_CREATE` and `DPOPEN_JOIN` are
  mutually exclusive (both-set would currently pass validation). Not currently exercised by any
  known call site.
- **Unknown**: whether the demo executable (`FREE_DIRECT`) actually runs and renders correctly on
  a real display in this environment — it compiles, but was not run graphically this session.

## 6. Architecture notes

**Public surface** (`include/`): `ddraw.h`, `dsound.h`, `dplay.h` — DirectX-shaped types/constants/
interfaces only. **Hard rule**: no SDL3, SDL3_net, or ENet type/symbol may ever appear here
(enforced by convention + manual `grep` checks in this session, not yet automated in CI).

**Internal implementation** (`src/`):
- `src/directdraw/DirectDraw.cpp`, `src/directsound/DirectSound.cpp` — SDL3-backed, largely
  pre-existing (not touched this session beyond earlier, unrelated work).
- `src/directplay/` — the actively-developed subsystem this session:
  - `DirectPlay.cpp` — the public COM-shaped entry points (`DirectPlayCreate`,
    `DirectPlayEnumerateA/W`) and two anonymous-namespace classes: `DirectPlayImpl`
    (`IDirectPlay`, a thin factory) and `DirectPlay2AImpl` (`IDirectPlay2A`, the real
    implementation, owning one `DirectPlaySession session_` member directly, not by pointer).
    `Open()` now `#ifdef FREE_DIRECT_ENABLE_ENET`-selects `EnetDirectPlayTransport` vs.
    `LoopbackDirectPlayTransport` for `session_.transport` (`docs/directplay-design.md` Decision
    4) - the `#include "EnetDirectPlayTransport.hpp"` at the top of the file is behind the same
    `#ifdef`, so the default build never sees an ENet header. When hosting (`session_.isHost`)
    under the ENet backend, `Open()` also calls `Listen(kDefaultDirectPlayEnetPort)` (Decision 5),
    resetting the transport and returning `DPERR_CANTCREATESESSION` on failure.
  - `DirectPlaySession.hpp` — per-object state: `DirectPlayObjectState` enum
    (`Created`/`Open`/`Closed`), `isHost`, `localPlayerIds`/`remotePlayerIds`, `nextPlayerId`
    (placeholder DPID counter), session descriptor fields (`sessionName`, `password`,
    `applicationGuid`, `maxPlayers`, `currentPlayers`), an owned `transport`
    (`std::unique_ptr<IDirectPlayTransport>`), and an owned `messageQueue`
    (`DirectPlayMessageQueue`). Deliberately does **not** store a raw `DPSESSIONDESC2` (its
    string pointers are caller-owned and must not be retained).
  - `DirectPlayMessageQueue.hpp` — `DirectPlayMessagePacket` (idFrom/idTo/flags/payload) in a
    bounded FIFO `std::deque`, plus `TryReceive()` — the exact buffer-size-query/
    `DPERR_NOMESSAGES`/too-small/successful-copy logic that `DirectPlay2AImpl::Receive()`
    delegates to (extracted specifically so tests exercise the real logic, not a duplicate).
  - `DirectPlayTransport.hpp` — `IDirectPlayTransport` abstract interface
    (`Listen`/`Connect`/`Send`/`Receive`/`Service`/`Shutdown`), backend-agnostic,
    byte-buffer-oriented. `Service()` (Decision 6) processes pending network events without
    blocking; `DirectPlay2AImpl::Receive()` calls it unconditionally before consulting the
    message queue.
  - `LoopbackDirectPlayTransport.hpp`/`.cpp` — real, in-memory implementation; `Service()` is a
    no-op (everything is already synchronous); assigned by `Open()` in the default
    (`FREE_DIRECT_ENABLE_ENET=OFF`) build.
  - `DirectPlayWireProtocol.hpp`/`.cpp` — `DirectPlayWirePacketType` enum and
    `DirectPlayWirePacketHeader` struct (magic/version/type/applicationGuid/sessionGuid/idFrom/
    idTo/payloadLength) plus flat serialize/deserialize free functions, plus a validating
    `TryDeserializeDirectPlayWireHeader` for untrusted receive buffers (size/length checks only,
    not magic/version). Pure data structure, zero ENet dependency, not yet used by any transport
    (`EnetDirectPlayTransport` is what will construct/consume these once it exists).
  - `EnetDirectPlayTransport.hpp`/`.cpp` — real `enet_initialize`/`enet_deinitialize`
    process-wide-refcounted lifecycle, real `Listen(port)` (creates a listening `ENetHost`,
    `HasHost()` accessor), real `Connect(address, port)` (creates a client `ENetHost` +
    `enet_host_connect`s to `hostPeer_`, `HasPeer()` accessor - joining role only), real graceful
    disconnect in `Shutdown()` (`enet_peer_disconnect` + a bounded wait for
    `ENET_EVENT_TYPE_DISCONNECT`, generalized to every peer the instance knows about - `hostPeer_`
    and/or all of `connectedPeers_`/`pendingPeers_`), real reliable/unreliable send in
    `Send(data, size, reliable)` (joining role only - always `false` for a hosting-role instance,
    since there is no single implicit recipient once there can be many `connectedPeers_`), and
    real event servicing in `Service()` (Decision 6: non-blocking `enet_host_service` drain loop).
    **Multi-peer hosting** (Decision 7, this session): `hostPeer_` (joining role, one `ENetPeer*`)
    is now separate from `connectedPeers_` (`std::unordered_map<DPID, ENetPeer*>`, hosting role,
    populated only via the new `AssignPendingConnection(DPID)`) and `pendingPeers_`
    (`std::deque<ENetPeer*>`, hosting role, populated by `Service()`'s `ENET_EVENT_TYPE_CONNECT`
    handling); `HasPendingConnection()`/`ConnectedPeerCount()` are the new test/caller-facing
    accessors. Transport-level `Receive()` still returns `false` - `Service()` never delivers
    received packets anywhere. Also exposes `kDefaultDirectPlayEnetPort = 51321` (Decision 5).
    Compiled only under `-DFREE_DIRECT_ENABLE_ENET=ON`; selected by `Open()` for construction
    (Decision 4), `Listen()` when hosting (Decision 5), `Service()` (Decision 6), and DPID
    assignment to pending connections up to `dwMaxPlayers` (Decision 7) from
    `IDirectPlay2A::Receive()`.

**Data flow for the one working networked-ish path (self-send)**: `Open(DPOPEN_CREATE)` →
`session_.transport = make_unique<LoopbackDirectPlayTransport>()` → `CreatePlayer` allocates a
DPID → `Send(id, id, ...)` round-trips the payload through `transport->Send()`/`Receive()` (to
genuinely exercise the transport, not bypass it) → wraps the result into a
`DirectPlayMessagePacket` → `session_.messageQueue.Enqueue()` → `Receive()` calls
`session_.messageQueue.TryReceive()` synchronously, no waiting.

**Invariants / boundaries that must not be broken**:
- No SDL3/SDL3_net/ENet symbol in any `include/*.h` file, ever.
- `IDirectPlayTransport` implementations must stay backend-agnostic byte-buffer interfaces;
  **narrowed by Decision 7** (multi-peer hosting): the transport may know `DPID` as an opaque map
  key (`HasPendingConnection()`/`AssignPendingConnection(DPID)`), but must never allocate one,
  validate one against session state (`dwMaxPlayers`, duplicates, etc.), or decide whether to
  accept a connection - all of that stays in `DirectPlay2AImpl`/`DirectPlaySession`. The transport
  reports connection-shaped facts (pending, assigned, gone); DirectPlay-level meaning is layered
  on top, same as before.
- `../free-eggbert` and `../planetblupi` game source must never be modified.
- DirectPlay is not, and must never be documented as, Microsoft-wire-compatible.
- `plan.md` tasks are atomic (one thing each); phases build on each other in order (e.g. Phase 9's
  real DPID allocation depends on the Section 4 decision being made first).
- FreeDirect's scope is bounded to what `free-eggbert`/`planetblupi` actually call — do not add
  DirectX surface "for completeness."

## 7. Useful commands

Configure + build (default backend, no ENet):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON
cmake --build cmake-build-debug -j4
```

Configure + build with the ENet transport backend enabled (vendored copy already present at
`third_party/enet`):
```bash
cmake -B cmake-build-debug -DFREE_USE_SYSTEM_SDL=ON -DFREE_DIRECT_ENABLE_ENET=ON
cmake --build cmake-build-debug -j4
```

Run the demo (untested graphically this session):
```bash
./cmake-build-debug/FREE_DIRECT
```

Run the DirectPlay tests (standalone, not yet CTest-integrated):
```bash
g++ -std=c++20 -Wall -Wextra \
    -I include -I ../free-api/include -I ../free-api/include_non_windows \
    -I src/directplay \
    src/directplay/DirectPlay.cpp src/directplay/LoopbackDirectPlayTransport.cpp \
    tests/directplay_tests.cpp \
    -o directplay_tests
./directplay_tests
```

No lint/format tooling is configured in this repository.

There is no known reproducible bug to "reproduce" right now (see Section 4) — the closest thing
is confirming the `DPID`-size hazard, which would require compiling `../free-eggbert` itself
against this repo's `include/dplay.h` on a 64-bit target and inspecting `NetPlayer` array
behavior; this has not been attempted.

## 8. Next smallest tasks

`plan.md` Phase 5 is done. Phase 6 ("Session hosting") is well underway: build-time backend
selection (Decision 4), starting the ENet listener on a real default port (Decision 5), event
servicing (Decision 6), session-instance-GUID generation, and multi-peer hosting with real DPID
assignment up to `dwMaxPlayers` (Decision 7) are all implemented and verified. "Store the session
descriptor" needed no new code (confirmed already satisfied).

**Next task has its own small design question, same pattern as before** - `EnetDirectPlayTransport`
currently has no way to tell a caller *which* DPID disconnected, only that *something* did
(`Service()` removes it from `connectedPeers_` internally, but nothing surfaces this outward).

1. **Update `dwCurrentPlayers` as players join and leave** (the next unchecked `plan.md` Phase 6
   task) - the "join" half is already done (this session's work increments `currentPlayers` and
   appends to `remotePlayerIds` on `AssignPendingConnection`); the "leave" half needs a way for
   `DirectPlay2AImpl::Receive()` to learn which DPID disconnected, so it can decrement
   `currentPlayers` and remove that DPID from `remotePlayerIds`.
   - Open question to resolve first (as a new Decision in `docs/directplay-design.md`): how does
     the transport surface "this DPID just disconnected" without exposing `ENetPeer*`? A queue
     mirroring `pendingPeers_`'s shape (e.g. `HasDisconnectedPeer()`/`TakeDisconnectedPeer()`
     returning the `DPID` that was removed from `connectedPeers_`, populated by `Service()`'s
     `ENET_EVENT_TYPE_DISCONNECT` handling instead of just erasing silently)? Something else?
   - Files: `src/directplay/DirectPlayTransport.hpp` (new interface method(s)),
     `src/directplay/EnetDirectPlayTransport.hpp`/`.cpp`, `src/directplay/DirectPlay.cpp`,
     `docs/directplay-design.md`.
   - Verify: a smoke test where an assigned peer disconnects, confirming
     `DirectPlaySession::currentPlayers`/`remotePlayerIds` update correctly (likely needs a
     test-only accessor on `DirectPlaySession`, or driving it through the public interface if a
     suitable observable side effect exists - check before assuming a new accessor is needed).

## 9. Do not do yet

- Do not refactor `DirectPlay.cpp`'s overall class structure — it works and is incrementally
  extended by design.
- Do not touch DirectDraw or DirectSound source (`src/directdraw/`, `src/directsound/`) — separate
  subsystems on separate, not-yet-started `plan.md` phases (13/14).
- Do not modify game source in `../free-eggbert` or `../planetblupi` under any circumstances.
- Do not implement general `Send()` routing, host forwarding, or broadcast (`plan.md` Phase 10) —
  it depends on `EnetDirectPlayTransport` having real send/receive behavior, which does not exist
  yet (still a stub).
- Do not wire `tests/directplay_tests.cpp` into CMake/CTest yet (`plan.md` Phase 15) — deliberately
  deferred until more of Phases 5-11 exist to test.
- Do not add a run-time backend-selection mechanism (env var, new API parameter, etc.) for
  `Open()`'s loopback-vs-ENet choice - `docs/directplay-design.md` Decision 4 explicitly chose
  build-time-only (`FREE_DIRECT_ENABLE_ENET`), and the "run-time instead" idea was deliberately
  rejected for now, not left open.
- Do not add an SDL3_net backend (`plan.md` Phase 12 explicitly defers this until ENet is stable).
- Do not implement disconnect-DPID surfacing (Section 8's task) by exposing `ENetPeer*` outside
  `EnetDirectPlayTransport` - same "no ENet type crosses the abstraction boundary" rule Decision 7
  already established for pending connections; write the mechanism down as a Decision first.
- Do not implement `Send()`/`Receive()` addressing a *specific* connected peer, or explicit
  rejection of an over-the-cap pending connection - both are separate, still-open `plan.md` Phase
  6/10 tasks, not implied by the multi-peer tracking that now exists.
- Do not add DirectX API surface, flags, or behavior beyond what `../free-eggbert`/
  `../planetblupi` call sites actually require (`CLAUDE.md` scope policy) — ask before expanding.
- Do not attempt a mass rewrite or "cleanup" pass — this codebase is being built up incrementally,
  one atomic `plan.md` task at a time, each verified before the next.

## 10. Resume prompt

```
Read NEXT.md first. plan.md Phase 5 is done; Phase 6 ("Session hosting") is well underway -
build-time backend selection (Decision 4), starting the ENet listener (Decision 5), event
servicing (Decision 6), session-instance-GUID generation, and multi-peer hosting with real DPID
assignment (Decision 7) are all implemented and verified. The next task (Section 8: updating
dwCurrentPlayers/remotePlayerIds on disconnect) needs a small design decision first - how does the
transport surface "this DPID disconnected" without exposing ENetPeer* outside it (same rule
Decision 7 established). Resolve and document it (e.g. as a new Decision) with the user first, do
not pick the mechanism unilaterally. Do not refactor unrelated code, do not touch
DirectDraw/DirectSound, and do not modify ../free-eggbert or ../planetblupi. Make one small,
verified improvement - implement just that one task. Run the relevant build/test command from
"Useful commands" (or the task's own "Verify" step) and confirm it actually passes before
considering the task done. Then update NEXT.md to reflect the new state.
```
