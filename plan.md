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

- [ ] Confirm whether `../free-eggbert` exists as a sibling checkout relative to this repository;
      record the result and the checkout path used.
- [ ] Confirm whether `../planetblupi` exists as a sibling checkout relative to this repository;
      record the result and the checkout path used.
- [ ] If either sibling repository does not exist at audit time, stop and record that this phase is
      blocked/partial for that repository, rather than guessing at its call sites.
- [ ] List every DirectPlay-related public declaration currently in `include/dplay.h` (functions,
      interfaces, structs, typedefs, macros), as a flat inventory.
- [ ] List every DirectPlay source stub currently in `src/directplay/DirectPlay.cpp`, class by
      class and method by method, noting each method's current unconditional return value.
- [ ] Cross-reference the two inventories above and list any declaration in `include/dplay.h` that
      has no corresponding implementation in `src/directplay/DirectPlay.cpp` (or vice versa).
- [ ] Grep `../free-eggbert` for DirectPlay-related symbols
      (`DirectPlay`, `dplay`, `IDirectPlay`, `DPID`, `DPSESSIONDESC`, `EnumSessions`,
      `CreatePlayer`) and list every matching file. Known baseline as of this plan's authoring:
      `include/network.hpp`, `src/network.cpp`, `src/decnet.cpp`, `src/event.cpp`,
      `include/event.hpp` — re-verify this list rather than trusting the baseline blindly.
- [ ] Grep `../planetblupi` for the same DirectPlay-related symbols and list every matching file.
      Known baseline as of this plan's authoring: **zero matches** outside vendored
      `third_party`/SDL headers — `planetblupi` is single-player only. Re-verify rather than
      trusting the baseline blindly.
- [ ] Identify missing DirectPlay constants used by target game code: confirm whether
      `DPID_ALLPLAYERS`/`DPID_SYSMSG` should be added to `include/dplay.h`, given that
      `free-eggbert/src/network.cpp` (`CNetwork::Send`, the `Send(m_dpid, 0, ...)` call) sends to a
      literal `0` recipient rather than a named constant.
- [ ] Identify whether target game code uses `IDirectPlay`, `IDirectPlay2A`, or `IDirectPlay3A`.
      Baseline finding: `free-eggbert` uses `IDirectPlay` only to `QueryInterface` into
      `IDirectPlay2A` (via `LPDIRECTPLAY`/`LPDIRECTPLAY2` in `include/network.hpp`);
      `IDirectPlay3A` is not referenced anywhere in either target game.
- [ ] Identify whether target game code uses `QueryInterface`. Baseline finding: yes —
      `free-eggbert/src/network.cpp`'s `CNetwork::CreateProvider` calls
      `lpDP->QueryInterface(IID_IDirectPlay2A, (LPVOID*)&m_pDP)`.
- [ ] Identify whether target game code uses `EnumSessions`. Baseline finding: yes —
      `CNetwork::EnumSessions` in `free-eggbert/src/network.cpp`, populating a `DPSESSIONDESC2`
      with `guidApplication` set to a fixed app GUID and the `DPENUMSESSIONS_AVAILABLE` flag.
- [ ] Identify whether target game code uses `Open`. Baseline finding: yes — `DPOPEN_CREATE` in
      `CNetwork::CreateSession`, `DPOPEN_OPENSESSION` in `CNetwork::JoinSession`.
- [ ] Identify whether target game code uses `CreatePlayer`. Baseline finding: yes — called
      immediately after `Open` succeeds in both `CreateSession` and `JoinSession`, passing a
      `DPNAME` with a short name only (`lpszLongNameA` is always `NULL`).
- [ ] Identify whether target game code uses `Send`. Baseline finding: yes, from
      `src/network.cpp`, `src/decnet.cpp`, and `src/event.cpp`; every observed call site passes a
      nonzero/`DPSEND_GUARANTEED`-equivalent flag (`CNetwork::Send` collapses any nonzero
      `dwFlags` to `DPSEND_GUARANTEED` via `!!dwFlags`).
- [ ] Identify whether target game code uses `Receive`. Baseline finding: yes —
      `CNetwork::Receive` in `src/network.cpp` polls once per frame from `src/decnet.cpp` with
      `DPRECEIVE_ALL` into a fixed 500-byte stack buffer, and treats any result other than `DP_OK`
      as "no message" (specifically checking for `DPERR_NOMESSAGES` before logging an error).
- [ ] Identify whether target game code uses `Close`. Baseline finding: yes — `CNetwork::Close`
      and `CNetwork`'s destructor both call `m_pDP->Release()`/`Close()`.
- [ ] Identify whether target game code uses groups. Baseline finding: no `CreateGroup`,
      `AddPlayerToGroup`, `DeletePlayerFromGroup`, or group-enumeration call found anywhere in
      `free-eggbert`.
- [ ] Identify whether target game code uses lobby APIs. Baseline finding: no `dplobby.h` include
      or `IDirectPlayLobby*` symbol found in `free-eggbert`'s own source (only present in the
      vendored `dxsdk3/sdk/inc/dplobby.h` reference header, which is not included by any
      `free-eggbert` source file).
- [ ] Identify whether target game code uses service provider enumeration. Baseline finding: yes
      — `CNetwork::EnumProviders` calls `DirectPlayEnumerateA`/`DirectPlayEnumerateW` to populate a
      provider picker, used before `CreateProvider`.
- [ ] Identify expected behavior when no sessions are found. Baseline finding: `CNetwork::EnumSessions`
      treats a non-`DP_OK` result from `IDirectPlay2A::EnumSessions` as failure and clears its
      session list; `event.cpp`'s UI shows zero selectable entries when `GetNbSessions()` is zero.
      No dedicated "no sessions" dialog/error path was found — verify this is still true.
- [ ] Identify expected behavior when network initialization fails. Baseline finding:
      `CNetwork::CreateProvider` returns `FALSE` when `DirectPlayCreate` or the subsequent
      `QueryInterface` fails, releasing any partially-created object. Trace how `event.cpp`'s
      provider-selection flow reacts to that `FALSE` and record it.
- [ ] Trace exactly how `event.cpp`'s provider-selection and session-selection UI (search for
      `NetEnumSessions`, the `WM_BUTTON*` handlers around lines 4640-4710 and 5550-5580 as of this
      plan's authoring) reacts to `EnumProviders`/`EnumSessions` returning zero results, and record
      the trace.
- [ ] Document the DPID-vs-array-index pattern in `free-eggbert/src/network.cpp`'s
      `CNetwork::Receive` (`for (int i = 0; i < MAXNETPLAYER; i++) if (m_players[i].bIsPresent &&
      from == i)` — comparing a `DPID` directly against a loop index). Record what DPID allocation
      strategy FreeDirect's host must use to stay compatible with this pattern, without modifying
      `free-eggbert` source. This finding directly feeds Phase 9's DPID-allocation task.
- [ ] Audit `../planetblupi`'s DirectDraw call sites for methods/flags not already covered by
      `../free-eggbert`. Baseline finding: `planetblupi` favors `BltFast` over plain `Blt` (17 vs.
      0 call sites) and calls `GetDC`/`ReleaseDC`/`IsLost`/`Restore` meaningfully (3-8 call sites
      each); re-verify counts rather than trusting the baseline.
- [ ] Audit `../free-eggbert`'s DirectDraw call sites the same way. Baseline finding: `BltFast`
      dominates over `Blt` (18 vs. 1 call sites); `GetDC`/`ReleaseDC`/`IsLost`/`Restore` are used
      similarly to `planetblupi` (3-8 call sites each).
- [ ] Audit both target games' DirectSound call sites for `DSBPLAY_LOOPING` usage. Baseline
      finding: zero call sites in either game as of this plan's authoring.
- [ ] Document all of the above findings in `docs/directplay-callsite-audit.md`, structured as one
      section per target game, each API getting an explicit yes/no/not-applicable verdict with a
      file/function citation.

**Acceptance criteria:** `docs/directplay-callsite-audit.md` exists, covers both `free-eggbert`
and `planetblupi`, and every yes/no verdict in it cites a specific file and function/line rather
than a general impression. Any finding that could not be verified (e.g. because a sibling
repository was missing) is explicitly marked "unverified" rather than silently assumed.

---

## Phase 1 — DirectPlay API boundary cleanup

Goal: make the existing `IDirectPlay`/`IDirectPlay2A` stub in `src/directplay/DirectPlay.cpp`
COM-correct and ready to host real state, without yet implementing real session/player/message
behavior.

- [ ] Fix `QueryInterface` null-pointer handling in `DirectPlayImpl::QueryInterface` and
      `DirectPlay2AImpl::QueryInterface` (`src/directplay/DirectPlay.cpp`): return
      `DPERR_INVALIDPARAMS` (or `E_INVALIDARG`) when `ppvObject == nullptr`, instead of
      dereferencing it unconditionally.
- [ ] Define a real internal `IID_IDirectPlay` constant in `include/dplay.h`, distinct from the
      current placeholder `IID_IDirectPlay2A = {0}`, so `QueryInterface` can compare against a real
      GUID value instead of accepting anything.
- [ ] Ensure `DirectPlayImpl::QueryInterface` returns `DPERR_NOINTERFACE`/`E_NOINTERFACE` for any
      `riid` other than `IID_IDirectPlay`/`IID_IDirectPlay2A`, instead of unconditionally
      succeeding for any request as it does today.
- [ ] Ensure `QueryInterface` calls `AddRef()` on the returned interface pointer before returning
      `DP_OK`, in both `DirectPlayImpl` and `DirectPlay2AImpl`.
- [ ] Ensure `DirectPlayCreate` initializes `*lplpDP = nullptr` before any failure return path, so
      callers never observe an uninitialized pointer on error.
- [ ] Ensure `DirectPlayCreate` rejects `pUnkOuter != nullptr` consistently, returning
      `DPERR_NOAGGREGATION` (matching the existing `DSERR_NOAGGREGATION` naming convention already
      used in `dsound.h`) instead of today's `DPERR_INVALIDPARAMS`; add `DPERR_NOAGGREGATION` to
      `include/dplay.h` if it is not already defined.
- [ ] Add a `@note Status:` comment to `DirectPlayEnumerateA` documenting its current behavior
      (returns `DP_OK`, invokes the callback zero times) as an intentional interim stub pending
      Phase 8.
- [ ] Add the same `@note Status:` documentation to `DirectPlayEnumerateW`.
- [ ] Decide whether `DirectPlayEnumerateA`/`DirectPlayEnumerateW` should invoke the callback once
      with a fake "FreeDirect" service-provider GUID/name, so `free-eggbert`'s
      `CNetwork::EnumProviders` sees at least one selectable provider. Record the decision and
      rationale in `docs/directplay-design.md` (created in Phase 16) — do not implement the
      behavior change in this task, only decide and document it.
- [ ] Add a follow-up task (tracked here, executed once the enumeration decision above is
      implemented) to test enumeration behavior against `free-eggbert`'s real provider-selection UI
      flow in `event.cpp`.
- [ ] Create `src/directplay/DirectPlaySession.hpp` and `src/directplay/DirectPlaySession.cpp`
      declaring an empty `DirectPlaySession` class (no members yet) that will own session/host/
      player-count state starting in Phase 2.
- [ ] Create `src/directplay/DirectPlayPlayer.hpp` and `src/directplay/DirectPlayPlayer.cpp`
      declaring an empty `DirectPlayPlayer` type that will own one player's DPID/names/data
      starting in Phase 2/9.
- [ ] Create `src/directplay/DirectPlayMessageQueue.hpp` and
      `src/directplay/DirectPlayMessageQueue.cpp` declaring an empty `DirectPlayMessageQueue` type
      that will back `Receive` starting in Phase 3.
- [ ] Create `src/directplay/DirectPlayTransport.hpp` declaring the internal `IDirectPlayTransport`
      abstract interface (pure virtual `Connect`/`Listen`/`Send`/`Receive`/`Shutdown`-style methods;
      exact signature finalized when Phase 4 implements the first concrete backend).
- [ ] Add the four new `src/directplay/*.cpp` files to the `free-direct` target's sources in
      `CMakeLists.txt`.
- [ ] Move `DirectPlay2AImpl`/`DirectPlayImpl` out of `src/directplay/DirectPlay.cpp` and into
      dedicated files only once they hold real state (Phase 2+) — do not split the file while it
      remains a pure stub, to avoid empty-file churn.

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

- [ ] Add a `DirectPlayObjectState` enum (`Created`, `Open`, `Closed`) to
      `src/directplay/DirectPlaySession.hpp`.
- [ ] Track whether the object is closed via the new state enum on `DirectPlaySession`.
- [ ] Track whether a session is currently open (host or joined) as derived from the state enum.
- [ ] Track whether this peer is host or client as a boolean/enum field on `DirectPlaySession`.
- [ ] Track local player DPIDs in a `std::vector<DPID>` on `DirectPlaySession`.
- [ ] Track remote player DPIDs in a separate `std::vector<DPID>` on `DirectPlaySession`.
- [ ] Track an owned copy of the session descriptor (`DPSESSIONDESC2`) on `DirectPlaySession`,
      deep-copying session name/password strings rather than retaining caller-owned pointers.
- [ ] Track the session name as an owned `std::string`, derived from the session descriptor copy.
- [ ] Track the application GUID (`guidApplication`) from the session descriptor.
- [ ] Track `dwMaxPlayers` from the session descriptor.
- [ ] Track `dwCurrentPlayers` as a live counter, incremented/decremented as players are created/
      removed.
- [ ] Validate `DPSESSIONDESC2.dwSize` in `Open()`, returning `DPERR_INVALIDPARAMS` when it does
      not equal `sizeof(DPSESSIONDESC2)`.
- [ ] Validate `DPNAME.dwSize` in `CreatePlayer()`, returning `DPERR_INVALIDPARAMS` when it does
      not equal `sizeof(DPNAME)`.
- [ ] Validate `dwFlags` passed to `Open()`, returning `DPERR_INVALIDFLAGS` for any bit outside
      `DPOPEN_CREATE`/`DPOPEN_JOIN`/`DPOPEN_OPENSESSION`.
- [ ] Replace `Open()`'s unconditional `DP_OK` return with real state transitions plus
      `DPERR_ALREADYINITIALIZED` when called again on an already-open object.
- [ ] Replace `EnumSessions()`'s unconditional `DP_OK` return with behavior driven by the
      enumeration decision recorded in Phase 1 (full discovery logic still lands in Phase 8; this
      task only wires the method to real state instead of an unconditional stub).
- [ ] Replace `CreatePlayer()`'s hardcoded `*lpidPlayer = 1` with a DPID allocated per the strategy
      to be finalized in Phase 9 (a simple incrementing counter is an acceptable placeholder here;
      Phase 9 revisits correctness against the Phase 0 DPID-vs-index finding).
- [ ] Replace `Send()`'s unconditional `DP_OK` return with parameter/state validation only (full
      routing/delivery lands in Phase 10).
- [ ] Replace `Receive()`'s unconditional `DP_OK` return with `DPERR_NOMESSAGES` once the message
      queue (Phase 3) reports empty.
- [ ] Make `Close()` clear all session/player/message state on `DirectPlaySession` and transition
      to the `Closed` state.
- [ ] Make `Release()` on `DirectPlay2AImpl` tear down any transport resources safely (via
      `IDirectPlayTransport::Shutdown()` or equivalent) before `delete this`.

**Acceptance criteria:** a unit test opens a session (`DPOPEN_CREATE`), closes it, and asserts
`dwCurrentPlayers` and the session-open flag both reset to their pre-open baseline; a second call
to `Open()` without an intervening `Close()` returns `DPERR_ALREADYINITIALIZED`; the project
builds and this test passes.

---

## Phase 3 — Message queue semantics

Goal: give `Receive` real FIFO semantics backed by `DirectPlayMessageQueue`, independent of any
transport backend.

- [ ] Implement an internal `DirectPlayMessagePacket` struct in
      `src/directplay/DirectPlayMessageQueue.hpp`.
- [ ] Store the source DPID (`idFrom`) in `DirectPlayMessagePacket`.
- [ ] Store the destination DPID (`idTo`) in `DirectPlayMessagePacket`.
- [ ] Store flags (e.g. the guaranteed-delivery bit) in `DirectPlayMessagePacket`.
- [ ] Store payload bytes as a `std::vector<uint8_t>` in `DirectPlayMessagePacket`.
- [ ] Implement a FIFO receive queue (e.g. `std::deque<DirectPlayMessagePacket>`) inside
      `DirectPlayMessageQueue`.
- [ ] Implement `Receive`'s buffer-size query behavior: when `lpData == nullptr` and
      `*lpdwDataSize == 0`, return the required size via `*lpdwDataSize` without dequeuing.
- [ ] Implement `Receive` returning `DPERR_NOMESSAGES` when the queue is empty, matching
      `free-eggbert/src/network.cpp`'s `CNetwork::Receive` expectation of that specific code.
- [ ] Implement `Receive` copying the sender DPID into `*lpidFrom` on a successful dequeue.
- [ ] Implement `Receive` copying the recipient DPID into `*lpidTo` on a successful dequeue.
- [ ] Implement `Receive` validating `lpdwDataSize != nullptr` before dereferencing it.
- [ ] Implement `Receive` validating the output buffer: when `*lpdwDataSize` is smaller than the
      queued packet's payload size, return an appropriate `DPERR_*` (reuse `DPERR_INVALIDPARAMS` if
      no dedicated "buffer too small" code exists in `include/dplay.h`; add one only if the target
      game's code path distinguishes it) without dequeuing the packet.
- [ ] Implement a maximum queued-message count on `DirectPlayMessageQueue` to bound memory growth
      when a peer stops calling `Receive`.
- [ ] Implement oversize-packet rejection before a packet is ever queued (see Phase 11's
      `DPERR_SENDTOOBIG`), sized to comfortably exceed the largest observed `free-eggbert` payload.
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
