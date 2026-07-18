# CLAUDE.md — FreeDirect Project Charter

This file is the standing charter for anyone (human or AI agent) working on **FreeDirect**. It
defines mission, scope, and hard policy rules. It is written in English and must remain in
English. When policy here conflicts with a request in conversation, policy here wins unless the
user explicitly overrides it for the current task.

For the concrete task backlog, see [`plan.md`](plan.md). For current implementation status, see
`NEXT.md` (does not exist yet — see the **`NEXT.md` Policy** section below).

---

## Project Mission

FreeDirect is a **C++20 compatibility layer** that reimplements a **narrow, game-driven subset of
DirectX 3 (2D)** so that specific legacy Win32/DirectX games can run on modern platforms without
the original DirectX SDK or Windows.

**There are exactly two named target games, both sibling repositories:**

- `../free-eggbert` — *Speedy Blupi*. Uses DirectDraw, DirectSound, **and** DirectPlay
  (`include/network.hpp`, `src/network.cpp`, `src/decnet.cpp`, `src/event.cpp` implement a full
  `CNetwork` client over `IDirectPlay`/`IDirectPlay2A`).
- `../planetblupi` — *Planet Blupi*. Uses DirectDraw and DirectSound extensively
  (`GetDC`/`ReleaseDC`/`IsLost`/`Restore`/`BltFast`/palette/clipper calls throughout `src/*.cpp`).
  Confirmed by grep to have **zero** DirectPlay/`dplay`/`IDirectPlay` usage — it is single-player
  only. DirectPlay scope is therefore defined solely by `free-eggbert`.

The layer is driven by **what these two target games actually call**, not by an ambition to cover
the DirectX 3 API surface. If neither target game calls an API, that API is out of scope until a
new target call site is identified in one of them.

FreeDirect intentionally trades broad compatibility for depth on a small surface: every
implemented method should behave correctly and predictably for the call patterns that are known
to occur, rather than existing as a plausible-looking stub.

---

## Scope and Non-Scope

**FreeDirect's scope is bounded by the union of real call sites in exactly two target games:
`free-eggbert` and `planetblupi`. FreeDirect must not grow into a general-purpose DirectX 3
reimplementation, and must not accumulate API surface or behavior that neither game needs.**

**In scope:**

- DirectDraw 2D subset used by `free-eggbert` and/or `planetblupi` (surfaces, blits, palettes,
  clipping, `GetDC`/`ReleaseDC`, `IsLost`/`Restore`, flip/present).
- DirectSound subset used by `free-eggbert` and/or `planetblupi` (static PCM buffer playback over
  SDL3 audio).
- DirectPlay subset used by `free-eggbert` (session create/join, player management, guaranteed
  message send/receive) reimplemented with a real, working transport — not Microsoft-wire-compatible,
  but interoperable between two programs both linked against FreeDirect. `planetblupi` has no
  DirectPlay usage, so it places no requirements on this subsystem.
- The minimal Win32/COM-shaped scaffolding needed to host the above (HRESULT, GUID, COM-style
  `QueryInterface`/`AddRef`/`Release`, etc.), to the extent it lives in these headers.

**Out of scope (do not implement):**

- Full DirectX 3 compatibility, or any DirectX version other than the narrow DirectX 3 subset.
- **Direct3D** (the 3D pipeline). Not needed by either target game; do not add it.
- **DirectInput**. Not needed by either target game; do not add it.
- Wire compatibility with real Microsoft DirectPlay service providers or packet formats.
- Hardware-accurate emulation of legacy DirectDraw/DirectSound behavior beyond what
  `free-eggbert`/`planetblupi` observably depend on.
- Speculative API coverage, flags, or backend features added "for completeness" or "because real
  DirectX has it" without a real call site in one of the two named games.

**Exceptions require asking the user first.** If a task, a request, or your own investigation
suggests FreeDirect needs something beyond what `free-eggbert` and `planetblupi` demonstrably call
— a new API, a new flag, a new backend capability, broader coverage of an existing method — do
**not** implement it speculatively. Stop and ask the user whether this is a deliberate exception
before writing code. This applies even if the addition seems small or "obviously correct" by real
DirectX semantics.

If a plan task would require expanding scope beyond what a real call site demands, the task must
first go through Phase 0-style call-site verification (see `plan.md`), not be implemented directly.

---

## DirectX 3 Subset Policy

- The subset boundary is defined by **actual call sites** in the two named target games' source,
  `../free-eggbert` and `../planetblupi`, when present as sibling checkouts.
- When one or both sibling repositories do not exist, do not guess at call sites. Add or update a
  `plan.md` task to perform the call-site inventory once the sibling repository is available, and
  be explicit in commit/PR messages that coverage decisions are provisional until that inventory
  exists.
- New API surface (functions, methods, struct fields, flags, constants) is added only when a
  concrete call site needs it. Cite the call site (file + line, or file + function name) in the
  commit message or code comment when adding new surface.
- Never widen a stub "just in case." An unused DirectX method may remain declared (for link
  compatibility with game code that references it) but should stay an honestly-labeled stub.

---

## Public Header Policy

Public headers are: `include/ddraw.h`, `include/dsound.h`, `include/dplay.h`, and any future
headers installed under `include/`.

Public headers **must** contain only DirectX-shaped, compatibility-facing content:

- HRESULT-returning `WINAPI` free functions matching legacy DirectX entry points
  (`DirectDrawCreate`, `DirectSoundCreate`, `DirectPlayCreate`, `DirectPlayEnumerateA/W`, ...).
- Abstract COM-shaped interface classes (`IDirectDraw`, `IDirectSound`, `IDirectPlay2A`, ...) with
  pure virtual methods matching the legacy method signatures needed by the subset.
- DirectX-legacy struct/typedef definitions (`DDSURFACEDESC`, `DSBUFFERDESC`, `DPSESSIONDESC2`,
  `DPNAME`, ...) and legacy flag/error-code macros (`DDERR_*`, `DSERR_*`, `DPERR_*`, `DP*`/`DD*`/`DS*`
  flags).
- Doxygen-style comments, including the existing `@note Status: STUB | PARTIAL | IMPLEMENTED`
  convention. Comments must be honest about current behavior (see Documentation Policy).

Public headers **must never** contain:

- Any `#include` of SDL3, SDL3_net, ENet, or any other internal backend library.
- Any type, forward declaration, or symbol whose name or shape leaks an internal backend
  (no `SDL_*`, `ENet*`, `SdlNet*` identifiers of any kind).
- Internal transport/session/state classes (`DirectPlaySession`, `EnetDirectPlayTransport`, etc.).
  Those are implementation details behind the public `IDirectPlay*` interfaces.

If a public header change would require including a backend header to make it compile, that is a
signal the new code belongs in a private header under `src/`, not in `include/`.

---

## Internal Backend Policy

SDL3, SDL3_net, and ENet (and any future backend library) are **implementation details** and must
stay strictly internal:

- They may be `#include`d only from `.cpp` files, or from private headers under `src/**` that are
  themselves only ever included by `.cpp` files (never transitively reachable from `include/`).
- `target_include_directories` for these backends must be `PRIVATE` in CMake, never `PUBLIC` or
  `INTERFACE`, for the `free-direct` target.
- Private headers that define an internal abstraction (e.g. `src/directplay/DirectPlayTransport.hpp`
  declaring `IDirectPlayTransport`) are allowed and encouraged, but they live under `src/`, are not
  installed, and are not `#include`d by anything under `include/`.
- A `grep`-based check ("no `SDL_`, `SDL3_net`, or `ENet` identifiers under `include/`") should pass
  at all times; treat a failure of this check as a policy violation, not a style nit.

---

## DirectDraw Policy

DirectDraw is the most mature subsystem and the primary implemented surface today
(`src/directdraw/DirectDraw.cpp`, declared in `include/ddraw.h`). Policy:

- Keep changes scoped to the methods, flags, and struct fields actually exercised by
  `free-eggbert` and/or `planetblupi` (and secondarily the in-repo demo, `src/Main.cpp`, which
  exists to exercise the compatibility layer, not to define its scope).
- Both target games call `BltFast` far more than plain `Blt` (`planetblupi`: 17 vs. 0 observed
  call sites; `free-eggbert`: 18 vs. 1), and both call `GetDC`/`ReleaseDC`/`IsLost`/`Restore`
  meaningfully (3-8 call sites each in both games). Treat these as the real hardening priorities
  for Phase 14, rather than assuming they are minor/rarely-used paths.
- Prefer correctness over performance until correctness is verified by tests; then optimize
  (e.g. persistent textures instead of per-frame texture creation) without changing observable
  behavior.
- Where real DirectDraw semantics are intentionally not replicated (e.g. simplified flip chain,
  simplified `DDBLTFX.dwFillColor` interpretation), document the deviation in the header comment
  and in `docs/directdraw-limitations.md` (the phase that established this, Phase 16, is done and
  archived — see `archive/plan20260718.md`) rather than silently papering over it.
- See `plan.md` Phase 14 for the concrete hardening backlog.

---

## DirectSound Policy

DirectSound is partially implemented over SDL3 audio (`src/directsound/DirectSound.cpp`, declared
in `include/dsound.h`), targeting static PCM buffer playback only. Policy:

- Do not implement DirectSound capture, 3D audio, or streaming buffers unless a real call site in
  `free-eggbert` or `planetblupi` requires them.
- Neither `free-eggbert` nor `planetblupi` was found to reference `DSBPLAY_LOOPING` at any call
  site as of the Phase 0 audit. **Do not implement real looping speculatively** — leave it accepted
  but ignored (as documented today) unless a future audit of either game shows an actual dependency,
  or the user explicitly asks for it as a named exception.
- Known partial areas (mono-only `SetPan`, non-seekable `SetCurrentPosition`) must stay honestly
  labeled `PARTIAL`/`STUB` in header comments until fixed, and must be tracked in `plan.md` Phase 13
  rather than silently left undocumented.
- See `plan.md` Phase 13 for the concrete hardening backlog.

---

## DirectPlay Policy

DirectPlay (`include/dplay.h`, `src/directplay/`) is currently a stub (`IDirectPlay`/`IDirectPlay2A`
return `DP_OK`/dummy values unconditionally) and is the primary subject of the current planning
effort. Policy:

- **The goal is NOT Microsoft DirectPlay wire compatibility.** FreeDirect's DirectPlay
  reimplementation does not need to interoperate with real Microsoft DirectPlay service providers,
  real DirectPlay network packets, or unmodified original DirectPlay-using binaries.
- **The goal IS FreeDirect-to-FreeDirect compatibility.** Two programs, both linked against this
  FreeDirect DirectPlay implementation, running the same target game, must be able to discover,
  host, join, and exchange messages in a session with each other. Compatibility is defined at the
  level of the `IDirectPlay`/`IDirectPlay2A` C++ API contract the target game already calls, not at
  the byte level of any historical wire protocol.
- Implementation work is driven by the real call-site audit (Phase 0, done and archived — see
  `archive/plan20260718.md`), which found
  that the sibling `free-eggbert` repository already contains a full DirectPlay client
  (`include/network.hpp`, `src/network.cpp`, `src/decnet.cpp`, `src/event.cpp`) using
  `IDirectPlay`/`IDirectPlay2A` (not `IDirectPlay3A`), `QueryInterface`, `DirectPlayEnumerateA/W`,
  `EnumSessions`, `Open` (`DPOPEN_CREATE` and `DPOPEN_OPENSESSION`), `CreatePlayer`, `Send`
  (always with `DPSEND_GUARANTEED` at observed call sites), `Receive` (`DPRECEIVE_ALL`, fixed
  500-byte buffer), and `Close`. It does **not** use groups, lobby APIs, or `IDirectPlay3A`. Treat
  this as the authoritative first (and, per the current audit, only) target — `planetblupi` was
  confirmed by grep to have zero DirectPlay-related symbols anywhere in its source, so it places no
  requirements on this subsystem. Re-run the audit on both repositories if either changes.
- Never leave a method returning unconditional success (`DP_OK`) once real behavior is expected of
  it. Once a phase in `plan.md` implements real state for a method, that method must return
  meaningful `DPERR_*` codes for the failure paths the target game can hit.
- Every intentional behavioral deviation from Microsoft DirectPlay (error code choice, enumeration
  semantics, DPID allocation strategy, etc.) must be written down in
  `docs/directplay-limitations.md` (Phase 16) — never left implicit.
- Do not modify `free-eggbert` game source to make DirectPlay integration easier. FreeDirect must
  preserve the existing DirectPlay-shaped API surface the game already calls. The one narrow
  exception is a task that explicitly requires adding an integration test harness — and even then,
  prefer a separate test program over editing game source.

---

## Networking Backend Decision

FreeDirect's DirectPlay transport is architected as a **pluggable abstraction**, so the choice of
real network backend never leaks into `include/dplay.h`:

- `IDirectPlayTransport` — internal abstract interface (private header under `src/directplay/`)
  representing "however bytes actually move between peers": connect/listen, send (reliable or
  best-effort), receive, disconnect, and peer/address identification.
- `EnetDirectPlayTransport` — **preferred first real transport backend**, using
  [ENet](http://enet.bespin.org/). ENet is chosen because it provides reliable UDP, ordered
  delivery, packet fragmentation, peer connection management, and a game-oriented event model
  (connect/disconnect/receive events) essentially matching what a DirectPlay-shaped API needs,
  without FreeDirect having to hand-roll reliability/ordering/fragmentation.
- `SdlNetDirectPlayTransport` — **optional, future** backend using SDL3_net. Documented but not
  implemented until the ENet backend is stable (see `plan.md` Phase 12). Two SDL3_net paths exist,
  with different tradeoffs:
  - SDL3_net **UDP datagrams**: FreeDirect would have to implement reliability, ordering,
    fragmentation, acknowledgement, and retransmission itself — essentially re-deriving what ENet
    already provides. Only worth doing if ENet becomes unavailable on a target platform.
  - SDL3_net **TCP stream sockets**: simpler for a basic client/server mode, but a poor semantic
    match for DirectPlay-style discrete, possibly-unreliable game packets (stream framing must be
    added manually, and there's no unreliable/unordered option at all).
- `LoopbackDirectPlayTransport` — **local, in-process transport** used for deterministic unit
  tests and for a same-process "host + client" smoke test. No real sockets involved.

Rule of thumb: **ENet first and by default; SDL3_net stays a documented option, not a commitment;
raw sockets and Windows-only networking APIs are never used.**

---

## Documentation Policy

- Documentation must never claim compatibility that does not exist. In particular:
  - Never claim "full DirectX 3 compatibility."
  - Never claim DirectPlay is compatible with real Microsoft DirectPlay at the wire/packet level.
  - Always state that FreeDirect multiplayer only works between programs both built against this
    FreeDirect DirectPlay implementation.
- Prefer honest "STUB" / "PARTIAL" / "IMPLEMENTED" labeling (already used in header comments) over
  silence. If behavior is a deliberate simplification, say so, and say what the simplification is.
- Do not delete existing documentation (`README.md`, `Android and Web Compatibility.md`) to make
  room for new docs. `plan.md` is the authoritative forward-looking task list (see below). New docs
  go under `docs/`, a convention introduced by Phase 16 (done, archived — see
  `archive/plan20260718.md`).
- `TODO.md` was deleted on 2026-07-18 (explicit user override of the prior "not deleted" rule,
  after every item in it had been individually reviewed and resolved — fixed, confirmed as an
  intentional documented simplification, or confirmed stale). Its free-direct-related content is
  folded into `docs/directdraw-limitations.md`. Its free-api-related content was moved to a sibling
  file, `../freeapiissues.md`, outside this repo (in the shared `openeggbert/` checkout directory,
  which is not itself a git repo) — that file was later deleted by the user (2026-07-18, separately
  from this repo), so do not assume it still exists or link to it. Do not recreate `TODO.md` as a
  general-purpose scratch TODO file — `plan.md` is the durable task list.
- Every new `docs/*.md` file must be in English, like this file and `plan.md`.

---

## Testing Policy

- Tests must be headless-friendly (no real display/audio device required) wherever the subject
  under test allows it, so they can run in CI and in sandboxed agent environments.
- DirectPlay tests default to `LoopbackDirectPlayTransport` for determinism; real-socket ENet
  integration tests are opt-in via a CMake option (`FREE_DIRECT_ENABLE_ENET`-gated), never run by
  default in a way that could flake on a shared/CI network namespace.
- New behavior (a method moving from STUB to PARTIAL/IMPLEMENTED) must ship with tests covering at
  least: the success path, the most likely failure path, and any boundary condition explicitly
  named in the relevant `plan.md` task's acceptance criteria.
- A task is not "done" until it builds and its tests pass — see **Safety Rules** below.

---

## `plan.md` Policy

- **`plan.md` is the authoritative English task list for FreeDirect.** It formerly superseded
  `TODO.md` as the forward-looking backlog; `TODO.md` itself was deleted on 2026-07-18 once every
  item in it was resolved (see the Documentation Policy section above).
- **`archive/plan20260718.md`** holds phases and tasks confirmed fully complete as of 2026-07-18
  (verified item-by-item, not just checkbox-counted), split out to keep `plan.md` itself focused on
  actual remaining work. It is historical record, not a living document — do not edit it going
  forward, and do not assume a phase number's absence from `plan.md` means it was deleted; check
  the archive first. New phases/tasks are added to `plan.md`, never directly to the archive.
- **Every task in `plan.md` must be atomic: it must do exactly one thing.** If a task reads like it
  has an "and" joining two independent changes, split it into two tasks.
- Tasks are organized by phase, use Markdown checkboxes (`- [ ]` / `- [x]`), and important tasks
  carry explicit acceptance criteria.
- Check a box only when the corresponding code is merged, builds, and its tests (if any are
  specified in the acceptance criteria) pass — never mark a task done speculatively.
- Do not delete tasks that turn out to be unnecessary; strike them through or annotate them with a
  short reason instead, so the history of what was considered and why stays visible.
- Do not mix DirectDraw, DirectSound, and DirectPlay work inside one task unless the task is
  documentation-only (e.g. a single README update mentioning all three).

---

## `NEXT.md` Policy

- `NEXT.md` is the English **status file** describing current implementation state and next steps.
  It is distinct from `plan.md` (the durable task backlog): `NEXT.md` is a living snapshot.
- **`NEXT.md` does not exist yet.** It should be **created the first time real `plan.md` task
  implementation begins** (i.e., the first commit that does actual code work from `plan.md`, not
  the planning commit that introduces `plan.md`/`CLAUDE.md` themselves).
- Once created, `NEXT.md` must be **updated after each completed implementation batch** (a batch
  being a small, coherent set of finished `plan.md` tasks — typically what would land in one PR).
- `NEXT.md` must contain, at minimum:
  - Current branch/state.
  - Completed tasks (with references to the `plan.md` items they satisfy).
  - Partially completed tasks and exactly what remains.
  - Known blockers.
  - Next recommended tasks (a short, concrete pointer into `plan.md`).
  - Test status (what was run, pass/fail).
  - Build status (does a clean build succeed right now).
- **Never write fake or aspirational progress into `NEXT.md`.** If something wasn't built or
  tested, say so plainly rather than implying it was.

---

## Coding Style

Follow the conventions already established in this codebase:

- **C++20**, no exceptions crossing the COM-style interface boundary; internal errors are surfaced
  as `HRESULT`/`DPERR_*`/`DSERR_*`/`DDERR_*` return codes, matching legacy DirectX calling
  convention.
- Public interface classes use `WINAPI`-decorated pure virtual methods with legacy DirectX
  signatures; concrete implementations live in an anonymous namespace inside the corresponding
  `.cpp` file (see `DirectPlay2AImpl` in `src/directplay/DirectPlay.cpp` for the existing pattern).
- COM-style reference counting uses `std::atomic<ULONG>` and `delete this` on last `Release()`;
  factory functions allocate with `new (std::nothrow)` and return `*_OUTOFMEMORY` on allocation
  failure.
- Doxygen-style `/** @brief ... @note Status: STUB|PARTIAL|IMPLEMENTED */` comments on every public
  declaration; keep the `Status:` tag accurate as implementation progresses.
- File naming mirrors the class/subsystem it implements, PascalCase (`DirectDraw.cpp`,
  `DirectPlaySession.cpp`, `DirectPlayMessageQueue.hpp`), one primary class-family per file; prefer
  splitting a growing file over letting one file accumulate unrelated responsibilities (this is why
  `plan.md` Phase 1 splits `DirectPlay.cpp` into session/player/message-queue/transport files).
- Internal-only diagnostics follow the `Diagnostics.hpp` pattern: compile-time gated by a
  `FREE_DIRECT_*` macro, zero overhead when the macro is undefined, never included from `include/`.
- New CMake options follow the existing `FREE_DIRECT_*` naming convention
  (`FREE_DIRECT_DIAGNOSTICS`, `FREE_DIRECT_ENABLE_ENET`, `FREE_DIRECT_ENABLE_SDL3_NET`).
- Keep the implementation portable (Linux/Windows/macOS, and the Android/Web direction already
  described in `Android and Web Compatibility.md`); never add Windows-only networking or
  Windows-only system calls to satisfy a DirectPlay task.

---

## Safety Rules for Claude Code

- **Before adding any DirectX surface, behavior, or backend capability not demonstrably required
  by `free-eggbert` or `planetblupi`, stop and ask the user.** Do not implement speculative
  exceptions to the two-game scope on your own judgment, even if the addition looks small, obvious,
  or "correct by real DirectX semantics."
- Do not implement the DirectPlay runtime speculatively. Follow `plan.md` phase order: audit before
  interface cleanup, interface cleanup before state model, state model before message queue, before
  loopback, before any real network transport.
- Do not mark a `plan.md` task complete unless the code builds and its specified tests pass. If you
  cannot build or run tests in the current environment, say so explicitly instead of assuming
  success.
- Do not mix DirectDraw, DirectSound, and DirectPlay changes into a single commit or task unless the
  task is explicitly documentation-only.
- Do not modify game source in `../free-eggbert` (or any other target game repository) unless a
  task explicitly requires it for an integration test — and even then, prefer adding a standalone
  test program over editing the game.
- Do not add Direct3D or DirectInput implementations under any circumstances in this project's
  current scope.
- Do not add Microsoft DirectPlay wire/packet compatibility as a goal, and do not imply it exists in
  any comment, doc, or commit message.
- Do not require the original DirectX SDK to build or run FreeDirect.
- Do not let SDL3, SDL3_net, or ENet symbols/headers leak into `include/*.h`. Treat this as a hard
  build-time and review-time check, not a suggestion.
- Prefer small, single-purpose commits and small, single-purpose files, matching the atomicity rule
  applied to `plan.md` tasks themselves.
- Follow the repository's general git safety rules: never force-push, never skip hooks, never
  discard uncommitted work without checking `git status` first, and confirm before any action that
  is destructive or affects shared state.
