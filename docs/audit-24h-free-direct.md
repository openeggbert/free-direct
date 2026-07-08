# FreeDirect 24-Hour Stabilization Audit

Date: 2026-07-08. Branch: `develop` @ `57bca7d` at audit time. This audit re-scans the two
target games from current source (not from prior documentation) and cross-references every claim
against `include/`, `src/`, `tests/`, `plan.md`, `NEXT.md`, `docs/directplay-callsite-audit.md`,
and `docs/directplay-design.md`. Evidence-gathering was performed by six parallel research passes;
this document is the human-readable synthesis. No source files were modified while producing this
audit.

---

## 1. Executive verdict

**FreeDirect is still narrow and game-driven.** No API surface was found that lacks a real call
site in `free-eggbert` or `planetblupi`, or a test. The two-game scope boundary in `CLAUDE.md`
continues to hold under re-audit — see §2 and §8.

**It is close to complete for DirectDraw and DirectSound as used by both games, but materially
incomplete for DirectPlay as a real multiplayer transport.** DirectDraw and DirectSound cover
every method/flag either game actually calls, with only presentation-performance and edge-case
hardening left (Phase 13/14). DirectPlay has a fully real, tested loopback backend (46/46 tests
passing) but the ENet backend cannot yet complete a cross-process join, and — more importantly —
**a concrete, previously-undocumented bug was found**: because the host's own first player is
DPID `0` (Decision 3 in `docs/directplay-design.md`), and `free-eggbert`'s only reachable `Send()`
call pattern is `Send(m_dpid, 0, ...)` (a broadcast, `src/network.cpp:254`), a host running today's
code executes the **self-send** branch instead of a broadcast (`idTo == idFrom` when both are `0`),
so the message never reaches any remote client. This is not a hypothetical corner case — it is
the one live gameplay `Send()` pattern that exists. See §6 and the DirectPlay Plan Reconciliation.

**Highest risk subsystem: DirectPlay**, specifically the broadcast/self-send collision above,
followed by the ENet backend's unfinished join/discovery wiring. Second-highest risk:
**build/test integration** — 46 passing DirectPlay tests and zero DirectDraw/DirectSound tests
exist today, none of them wired into CMake/CTest, so regressions are only caught by manual,
undocumented developer discipline. DirectDraw is the *most exercised* subsystem by real gameplay
(present-path `Blt`, per-sprite `BltFast`) but currently ships with **zero automated tests**,
which given its call frequency makes it a silent-regression risk even though its implementation
looks correct today. The free-api bridge migration (commit `57bca7d`) is clean.

**Areas that must not be expanded:** Direct3D, DirectInput, DirectPlay lobby/groups, wire
compatibility with real Microsoft DirectPlay, LAN broadcast discovery (not yet asked-for),
DirectSound capture/3D audio/looping, and any DirectDraw/DirectSound flag or method without a
cited call site above. See §8 for the full confirmation table.

---

## 2. Target-game DirectX usage re-audit

### 2.1 DirectDraw

#### Headers/types used

| Symbol/Behavior | free-eggbert | planetblupi | Evidence | Required behavior | Impl. status | Risk | Tests |
|---|---|---|---|---|---|---|---|
| `IDirectDraw` | Yes (18 occ.) | Yes (24 occ.) | `src/ddutil.cpp`, `src/pixmap.cpp` (`m_lpDD`) | Object creation/lifetime | IMPLEMENTED (refcounting); `QueryInterface` STUB | Low | None |
| `IDirectDrawSurface` | Yes (14 occ.) | Yes (18 occ.) | `src/pixmap.cpp` (`m_lpDDSPrimary`/`m_lpDDSBack`/`m_lpDDSMouse`/`m_lpDDSurface[]`) | Blit/lock/DC/lost-surface object | PARTIAL | Low | None |
| `IDirectDrawPalette` | Yes (4) | Yes (4) | `include/pixmap.hpp`/`.h`, `src/ddutil.cpp` (`DDLoadPalette`) | 8-bit palette object | IMPLEMENTED | Low | None |
| `IDirectDrawClipper` | via `LPDIRECTDRAWCLIPPER` | via `LPDIRECTDRAWCLIPPER` | `include/pixmap.hpp:114`/`pixmap.h:198` | Window clipper | IMPLEMENTED | Low | None |
| `DirectDrawCreate` | 1 real call | 1 real call | `src/pixmap.cpp:178` (fe), `:146` (pb) | Factory entry | PARTIAL (GUID ignored, correct for DX3) | Low | None |
| `DDSURFACEDESC` | 4 | 5 | `src/ddutil.cpp`, `src/pixmap.cpp` | Create/Lock/GetSurfaceDesc struct | PARTIAL (documented subset) | Low | None |
| `DDBLTFX` (`m_DDbltfx`) | 1 member, used at every `Blt()` | same | `include/pixmap.hpp:122`/`pixmap.h:206` | Blit-effect params | PARTIAL — always zero-initialized, only `DDBLT_WAIT` ever passed | Low | None |
| `DDCOLORKEY` | 2 | 2 | `src/ddutil.cpp` (`DDSetColorKey`/`DDSetColorKey2`) | Color-key range | IMPLEMENTED | Low | None |
| `DDPIXELFORMAT` (field access only) | `dwRGBBitCount` read | same | `src/ddutil.cpp:304-305`/`:374,377` | Bit-depth masking | PARTIAL | Low | None |
| `DDCAPS` (capability query) | Not found | Not found | — | No `GetCaps()` call site | Correctly absent from `ddraw.h` | None | None |

#### Methods used (call-site counts, re-verified — corrects CLAUDE.md's prior "18 vs 1"/"17 vs 0" Blt-vs-BltFast figures)

| Method | free-eggbert | planetblupi | Evidence | Live gameplay path? | Impl. status | Risk | Tests |
|---|---|---|---|---|---|---|---|
| `->Blt(` | **4** call sites (`pixmap.cpp:640,1509,1643,1855`) | **3** (`pixmap.cpp:985,1116,1328`) | one call site per game is `CPixmap::Display()` — the **once-per-frame back-buffer→primary present** | **Yes — Display() is the single hottest path in both games**; MouseQuickDraw also live; DrawMap/MouseBackDebug dead | PARTIAL (ColorFill unused by either game; surface-to-surface path works) | **High** | **None** |
| `->BltFast(` | **6** call sites | **5** call sites | `pixmap.cpp` wrapper bodies + `QuickIcon`, called from `decor.cpp` per-sprite, many times/frame | Yes — 100% of call sites are live render paths | IMPLEMENTED | **High** | **None** |
| `->Lock(` (DirectDraw) | 1 (`ddutil.cpp:298`, inside `DDColorMatch`) | 1 (`ddutil.cpp:369`) | asset-load path (`Cache()`) | Yes, but load-time not per-frame | IMPLEMENTED | Medium | None |
| `->Unlock(` (DirectDraw) | 1 | 1 | pairs with Lock above | Load-time | IMPLEMENTED | Medium | None |
| `->Flip(` | **0** | **0** | — | Not called by either game | IMPLEMENTED (Simplified Present) but unexercised by target games | Low | None |
| `->GetDC(` | 3 (asset-load only) | **4**, incl. **`pixmap.cpp:729` `IsIconPixel`, a live per-click gameplay path** | `decblupi.cpp:3399` | High for planetblupi (pixel-perfect hit-test on click); Medium for fe | STUB | Medium-High | None |
| `->ReleaseDC(` | 3 | 4 | pairs with GetDC | Same as above | STUB | Medium-High | None |
| `->IsLost(` | 4, all inside `RestoreAll()` | 4, all inside `RestoreAll()` | `RestoreAll()` called from ~8-9 sites incl. `Display()` | Reachable, but always returns `DD_OK` so `Restore()`'s conditional body never fires | IMPLEMENTED (functionally inert stub, honestly documented) | Medium | None |
| `->Restore(` | 6, incl. one **unconditional** call from `blupi.cpp:383` not gated by IsLost | 6, incl. `blupi.cpp:332` | — | Reachable | IMPLEMENTED (returns `DD_OK` unconditionally) | Medium | None |
| `->SetColorKey(` | 2 (asset-load) | 2 | `ddutil.cpp` | Load-time, affects every later blit | IMPLEMENTED | Medium | None |
| `->CreatePalette(` | 1 | 1 | `ddutil.cpp` (`DDLoadPalette`) | Load-time | IMPLEMENTED | Medium | None |
| `->SetPalette(` (surface) | 4 | 2 | `pixmap.cpp` `Cache()` | Load-time, affects primary surface colors | IMPLEMENTED | Medium | None |
| `->CreateClipper(` | 1 | 1 | `pixmap.cpp` `Create()` | One-time init | IMPLEMENTED | Low | None |
| `->SetClipper(` | 1 | 1 | `pixmap.cpp` `Create()` | One-time init | IMPLEMENTED | Low | None |
| `->SetCooperativeLevel(` | 2 | 2 | `pixmap.cpp` `Create()` | One-time, unrecoverable on failure | IMPLEMENTED | Low | None |
| `->SetDisplayMode(` | 1 | 1 | `pixmap.cpp` `Create()` | One-time | IMPLEMENTED | Low | None |
| `->CreateSurface(` | 4 | 5 | primary/back/mouse + asset loaders | Setup + per-asset-load | PARTIAL (Primary + Offscreen only) | Low-Medium | None |
| `->GetSurfaceDesc(` | 1 | 1 | `ddutil.cpp` `DDCopyBitmap` | Load-time | IMPLEMENTED | Medium | None |

#### Flags/struct fields used

| Flag | free-eggbert | planetblupi | Notes |
|---|---|---|---|
| `DDSCAPS_PRIMARYSURFACE` | Yes | Yes | IMPLEMENTED |
| `DDSCAPS_OFFSCREENPLAIN` | commented out in `pixmap.cpp`, used only in `ddutil.cpp` loaders | same | Both games' back/mouse surfaces actually use `DDSCAPS_SYSTEMMEMORY`, not this flag |
| `DDSCAPS_SYSTEMMEMORY` | Yes — real flag used for back/mouse surfaces | Yes | IMPLEMENTED (treated as offscreen) |
| `DDBLT_WAIT` | Only flag ever passed to `Blt()` | same | No-op, correct for a synchronous CPU backend |
| `DDBLT_COLORFILL` | **Not found at any call site** | **Not found** | Implemented but dead for these two games |
| `DDBLT_KEYSRC` | **Not found at any `Blt()` call site** | **Not found** | Implemented but dead for these two games |
| `DDBLTFAST_SRCCOLORKEY`/`NOCOLORKEY` | Yes, on all live BltFast paths | Yes | IMPLEMENTED |
| `DDCKEY_SRCBLT` | Yes | Yes | IMPLEMENTED |
| `DDSCL_FULLSCREEN`/`EXCLUSIVE`/`NORMAL` | Yes | Yes | IMPLEMENTED |
| `DDPCAPS_8BIT` | Yes | Yes | IMPLEMENTED |
| `DDLOCK_*` (any) | **Not found — both games pass `0`** | same | Correctly absent from `ddraw.h` |

**Dead/unreachable code found (informational, not free-direct's concern but relevant to risk
weighting):** `CPixmap::DrawMap` (free-eggbert only) and `CPixmap::MouseBackDebug` (both games)
have zero callers anywhere in either game.

### 2.2 DirectSound

#### Methods/types used (live path only; dead-code duplicates noted separately)

| Symbol | free-eggbert (live) | planetblupi (live) | Evidence | Impl. status | Risk | Tests |
|---|---|---|---|---|---|---|
| `DirectSoundCreate` | 1 | 1 | `sound.cpp:373`/`:339` | IMPLEMENTED | Low | None |
| `SetCooperativeLevel` | 1 | 1 | `sound.cpp:380`/`:346` (`DSSCL_NORMAL`) | IMPLEMENTED | Low | None |
| `CreateSoundBuffer` | 1 | 1 | `sound.cpp:66`/`:63` | IMPLEMENTED | Low | None |
| `Lock` | 1 | 1 | `sound.cpp:105`/`:87` | IMPLEMENTED | Low | None |
| `Unlock` | 1 | 1 | `sound.cpp:133`/`:115` | IMPLEMENTED | Low | None |
| `GetStatus` | 2 | 2 | `sound.cpp:215,237`/`:179,202` | IMPLEMENTED | Low | None |
| `Play` (reachable calls always pass `dwFlags=0`) | 2 | 2 | `sound.cpp:242,494`/`:207,459` | PARTIAL (no looping — immaterial, no call site ever passes a looping flag) | Low | None |
| `Stop` | 3 | 3 | `sound.cpp:219,507,527`/`:183,477` | IMPLEMENTED | Low | None |
| `SetCurrentPosition` (always called with `0`) | 2 | 2 | `sound.cpp:508,528`/`:478` | PARTIAL (not seekable, but only the `0` value is ever passed) | Low | None |
| `SetVolume` | 1 | 1 | `sound.cpp:492`/`:457` | IMPLEMENTED | Low | None |
| `SetPan` (sources are mono) | 1 | 1 | `sound.cpp:493`/`:458` | PARTIAL (mono only — matches actual usage) | Medium (would break silently for a stereo buffer, none observed) | None |
| `QueryInterface`/`AddRef` (on DirectSound objects) | **Never called** | **Never called** | grep confirmed zero call sites | STUB / IMPLEMENTED but unexercised | Low | None |
| `DSBPLAY_LOOPING` | **Confirmed: zero references, case-sensitive and -insensitive** | **Confirmed: zero references** | fresh grep, this audit | Not defined in `dsound.h` — **CLAUDE.md's no-looping policy re-confirmed accurate** | Low | None |
| `DSBVOLUME_*`/`DSBPAN_*` range constants | Not found | Not found | Games pass raw computed `LONG`s | Correctly absent from `dsound.h` | Low | None |

**New findings this audit:**
- `free-eggbert/src/soundbass.cpp` is a full parallel DirectSound implementation but is **dead
  code** — gated by `#if _BASS && !_LEGACY`, and `include/def.hpp:24` hardcodes `_BASS FALSE` with
  no build-time override anywhere. `src/sound.cpp` is the sole live path.
- `src/wave.cpp` in **both** games (`LoadWave`/`wave_ParseWaveMemory`, using
  `CreateSoundBuffer`/`Unlock`) is compiled but unreachable — its own header is `#include`d
  nowhere else, and its functions are never called.
- `CSound::PlaySoundDS(dwSound, dwFlags)` — the one function that would forward a non-zero,
  caller-supplied flags word into `Play()` — is declared/defined in both games but never called.
  The only reachable `Play()` calls pass a literal `0`.
- `tests/` contains no DirectSound test file (confirmed).

### 2.3 DirectPlay

#### free-eggbert call sites (re-confirmed, `planetblupi` re-confirmed to have zero DirectPlay usage)

| API | Called? | Evidence |
|---|---|---|
| `IDirectPlay`/`IDirectPlay2A` (not `IDirectPlay3A`) | Yes, both; no `IDirectPlay3A` anywhere | `include/network.hpp:63` |
| `DirectPlayCreate` | Yes | `src/network.cpp:89` |
| `QueryInterface` | Yes, 1 site | `src/network.cpp:91` (`IID_IDirectPlay2A`) |
| `DirectPlayEnumerateA`/`W` | Yes, both | `src/network.cpp:60,62` |
| `EnumSessions` | Yes | `src/network.cpp:126-148`, `guidApplication` + `DPENUMSESSIONS_AVAILABLE` |
| `Open` (`DPOPEN_CREATE`, `DPOPEN_OPENSESSION`) | Yes, both flags | `src/network.cpp:174,221` |
| `CreatePlayer` | Yes (short name only, long name always `NULL`) | `src/network.cpp:185,233` |
| `Send` (always `DPSEND_GUARANTEED`) | Yes, 8 real call sites | `src/network.cpp:254`, `src/decnet.cpp:83,157`, `src/event.cpp:2159,2176,2212,2247,2281,4708` |
| `Receive` (`DPRECEIVE_ALL`, fixed **500-byte** buffer) | Yes | `src/network.cpp:262-289`, called from `src/decnet.cpp:166` |
| `Close` | Yes (real teardown is `Release()` via destructor) | `src/network.cpp:189,237,293` |
| Groups / Lobby / `IDirectPlay3A` | **No** — confirmed absent | grep-verified |

**Critical reachability finding (re-confirmed from `docs/directplay-callsite-audit.md` §2.3):**
`free-eggbert`'s entire lobby/session-picker UI (`NetCreate`, `NetEnumSessions`, `JoinSession`,
`CreateSession`, `NetStartPlay`) is **unreachable** in current source — the ten `WM_PHASE_DP_*`
handlers that would call them are empty placeholders. **The only provably reachable DirectPlay
consumer code is the gameplay-time `Send`/`Receive` broadcast pattern in `decnet.cpp`.** This
sharpens the priority of the broadcast/self-send bug found in this audit (§6) above anything in
session hosting/joining/enumeration.

See §6 for the full DirectPlay implementation audit and the DirectPlay Plan Reconciliation section
at the end of this document for phase-by-phase status.

---

## 3. Public surface audit

Legend for **Decision**: `keep` = justified, no action; `fix` = implementation/behavior gap to
close; `document` = STUB/PARTIAL/IMPLEMENTED tag is stale and must be corrected to match reality;
`test` = needs test coverage; `remove later` = candidate for removal if it stays permanently dead
(none found this audit — everything public maps to a real call site or a test need).

### `include/ddraw.h`

| Symbol | Category | Req. by fe | Req. by pb | Req. by tests | Impl. status | Decision |
|---|---|---|---|---|---|---|
| `DirectDrawCreate` | free function | Yes | Yes | No | PARTIAL | keep |
| `IDirectDraw::QueryInterface` | method | not called (only via COM contract) | not called | No | STUB | keep — COM shape requirement, correctly stubbed |
| `IDirectDraw::SetCooperativeLevel` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDraw::CreateSurface` | method | Yes | Yes | No | PARTIAL | test |
| `IDirectDraw::SetDisplayMode` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDraw::CreatePalette` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDraw::CreateClipper` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDrawSurface::Blt` | method | Yes (hot path) | Yes (hot path) | No | PARTIAL | **test — highest DirectDraw priority** |
| `IDirectDrawSurface::BltFast` | method | Yes (hot path) | Yes (hot path) | No | IMPLEMENTED | **test — highest DirectDraw priority** |
| `IDirectDrawSurface::Flip` | method | No | No | No | IMPLEMENTED, unexercised | keep (documented deviation already noted in README) |
| `IDirectDrawSurface::SetClipper`/`SetPalette` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDrawSurface::IsLost`/`Restore` | method | Yes (reachable, inert) | Yes | No | IMPLEMENTED (functionally no-op) | document + test |
| `IDirectDrawSurface::GetDC`/`ReleaseDC` | method | Yes | Yes (incl. live gameplay hit-test) | No | STUB | **fix/test — planetblupi live-path risk** |
| `IDirectDrawSurface::GetSurfaceDesc` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDrawSurface::Lock`/`Unlock` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectDrawSurface::SetColorKey` | method | Yes | Yes | No | IMPLEMENTED | test |
| `DDSURFACEDESC`/`DDBLTFX`/`DDCOLORKEY`/`DDSCAPS`/`DDPIXELFORMAT` | structs | Yes | Yes | No | PARTIAL (documented subset) | keep |
| `DDSCAPS_*`/`DDSD_*`/`DDBLT_*`/`DDBLTFAST_*`/`DDCKEY_*`/`DDSCL_*`/`DDPCAPS_*`/`DDPF_*` flags | constants | Yes (subset) | Yes (subset) | No | keep as-is | keep |
| `DDBLT_COLORFILL`, `DDBLT_KEYSRC`, `DDBLT_ROTATIONANGLE` | constants | **No live call site** | **No live call site** | No | defined, unused by either game | document — compile-only constant, retained for API-shape completeness of the flag family already required |
| Full `DDERR_*` legacy error-code block (lines 38-132) | constants | mostly unreferenced individually | mostly unreferenced individually | No | defined | document — kept for link/compile compatibility with legacy call patterns; not a scope risk since it's inert data, not behavior |

**Scope risk found:** none — every method/struct maps to a real call site; the only "unused"
items are compile-only constants (`DDBLT_COLORFILL`/`DDBLT_KEYSRC`/full `DDERR_*` table), which
CLAUDE.md's own policy explicitly allows to remain declared for link compatibility.

### `include/dsound.h`

| Symbol | Category | Req. by fe | Req. by pb | Req. by tests | Impl. status | Decision |
|---|---|---|---|---|---|---|
| `DirectSoundCreate` | free function | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectSound::QueryInterface` | method | Never called | Never called | No | STUB (`E_NOINTERFACE`) | keep — COM shape |
| `IDirectSound::SetCooperativeLevel` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectSound::CreateSoundBuffer` | method | Yes | Yes | No | IMPLEMENTED | test |
| `IDirectSoundBuffer::GetStatus`/`Play`/`Stop`/`Lock`/`Unlock`/`SetCurrentPosition`/`SetVolume`/`SetPan` | methods | Yes (all) | Yes (all) | No | IMPLEMENTED/PARTIAL (see §2.2) | test |
| `DSBCAPS_CTRLFREQUENCY`/`CTRLPAN`/`CTRLVOLUME` | constants | Yes | Yes | No | IMPLEMENTED | keep |
| `DSBCAPS_STATIC` | constant | only in dead `wave.cpp` | only in dead `wave.cpp` | No | defined | document — real call site exists only in unreachable code |
| `DSBLOCK_FROMWRITECURSOR` | constant | Yes | Yes | No | IMPLEMENTED | keep |
| `DSBSTATUS_PLAYING` | constant | Yes | Yes | No | IMPLEMENTED | keep |
| `DSSCL_NORMAL` | constant | Yes | Yes | No | IMPLEMENTED | keep |
| `DSBUFFERDESC`/`WAVEFORMATEX`/`PCMWAVEFORMAT` | structs | Yes | Yes | No | IMPLEMENTED | keep |

**Scope risk found:** none.

### `include/dplay.h`

| Symbol | Category | Req. by fe | Req. by pb | Req. by tests | Impl. status (code) | Impl. status (header comment) | Decision |
|---|---|---|---|---|---|---|---|
| `DirectPlayCreate` | free function | Yes | N/A (no DP usage) | Yes | Real (validates null, `pUnkOuter`, alloc failure) | STUB (doc) | **document — stale comment** |
| `DirectPlayEnumerateA`/`W` | free function | Yes | N/A | No | **Genuinely still a stub** — `DP_OK`, callback invoked 0 times | STUB (doc, accurate) | keep as documented STUB (Decision 1: decided, not yet implemented — blocks `free-eggbert`'s `CNetwork::CreateProvider` bound check) |
| `IDirectPlay2A::QueryInterface` | method | Yes | N/A | Yes | Real (`memcmp`-based GUID compare, `AddRef` on success) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::EnumSessions` | method | Yes | N/A | Yes | Real (loopback registry, GUID/available-flag filtering) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::Open` | method | Yes | N/A | Yes | Real, asymmetric by role/backend (see §6) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::CreatePlayer` | method | Yes | N/A | Yes | Real (`dwMaxPlayers` validation, sequential DPID) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::Send` | method | Yes | N/A | Yes | Real for self-send + host-to-one-remote unicast; **no broadcast, no relay** | STUB (doc) | **document — stale comment, and see §6 broadcast bug** |
| `IDirectPlay2A::Receive` | method | Yes | N/A | Yes | Real, multi-stage (Service/disconnect-drain/accept/wire-drain/dequeue) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::Close` | method | Yes | N/A | Yes | Real (registry unregister, transport shutdown, state clear) | STUB (doc) | **document — stale comment** |
| `IDirectPlay2A::AddRef`/`Release` | method | Yes (via COM) | N/A | Yes | Real atomic refcounting, defensive transport shutdown | STUB (doc) | **document — stale comment** |
| `IDirectPlay::QueryInterface`/`AddRef`/`Release` | method (minimal base) | Yes (via COM) | N/A | Yes | Real | STUB (doc) | **document — stale comment** |
| `DPID` (`typedef DWORD`) | type | Yes (32-bit stride dependency) | N/A | Yes | Real 4-byte width (Decision 3) | PARTIAL (doc, accurate) | keep |
| `DPNAME` | struct | Yes (short name only) | N/A | No | Fields accepted, **not stored** (Decision 17: deliberately deferred, unobservable without new API surface — do not implement without asking first) | STUB (doc, accurate) | keep as documented STUB pending a future ask-first conversation |
| `DPSESSIONDESC2` | struct | Yes | N/A | Yes | Real, but **no address-like field** — blocks ENet join-address resolution | STUB (doc, accurate) | keep — this gap is a known, explicitly-flagged blocker, not an oversight |
| `DPID_ALLPLAYERS`/`DPID_SYSMSG` | constants | Implied by real DirectPlay semantics but not yet defined here | N/A | No | **Not defined in `dplay.h` at all** | — | **fix — add these constants** (Phase 10/11 task, needed before broadcast can be implemented correctly) |
| `IID_IDirectPlay`/`IID_IDirectPlay2A` | constants | Yes (internal use only) | N/A | Yes | Real, FreeDirect-internal placeholder GUIDs, documented as non-Microsoft | PARTIAL (doc, accurate) | keep |
| `DPSESSION_KEEPALIVE`/`MIGRATEHOST` | constants | **Not found at any real call site** | N/A | No | defined, unused | document — compile-only constant |
| `DPESC_TIMEDOUT` | constant | **Not found at any real call site** | N/A | No | defined, unused | document — compile-only constant |

**Scope risk found:** none in terms of *unjustified* new surface — but a significant
**documentation-accuracy risk**: the file-level docstring (`@note Status: STUB`, line 4) and every
per-method Doxygen comment in the `IDirectPlay2A`/`IDirectPlay` classes still say `STUB`, while the
`.cpp` implementation behind nearly all of them is real, tested, and validated. This violates
`CLAUDE.md`'s Documentation Policy ("keep the Status: tag accurate as implementation progresses")
and is a cheap, safe, high-value fix (see 24-Hour Backlog, Docs area).

---

## 4. DirectDraw implementation audit

- **`DirectDrawCreate`**: works, GUID ignored (correct for DX3 usage — neither game passes a
  meaningful GUID). No test.
- **`SetCooperativeLevel`**: implemented as a minimal SDL3 mapping (creates the renderer). One-time
  setup call in both games; a failure here is unrecoverable but that matches real DirectDraw
  semantics. No test.
- **`SetDisplayMode`**: implemented, one-time. No test.
- **`CreateSurface`**: PARTIAL — supports Primary and Offscreen (including the `SYSTEMMEMORY`
  variant both games actually use, since `OFFSCREENPLAIN` is commented out in their own source).
  No test.
- **Primary surface behavior**: works; presentation model documented in README (throttle + dirty
  check + cached streaming texture + one `SDL_RenderPresent`/frame). No dedicated test.
- **Offscreen surface behavior**: works for the `SYSTEMMEMORY` case both games use. No test.
- **Palette behavior**: `CreatePalette`/`SetEntries`/`GetEntries`/`SetPalette` all implemented and
  used on real asset-load and primary-surface-color paths in both games. No test.
- **Clipper behavior**: `CreateClipper`/`SetHWnd`/`SetClipper` implemented, one-time init in both
  games. No test.
- **`Blt`**: PARTIAL — supports ColorFill and Surface-to-Surface Blit. **What works**:
  surface-to-surface copy with `DDBLT_WAIT` (the only flag combination either game exercises),
  including the once-per-frame present call in `CPixmap::Display()`. **What's deliberately
  unsupported/untested**: `DDBLT_COLORFILL`/`DDBLT_KEYSRC` paths exist in the implementation but
  have zero real call sites in either game — they cannot regress silently in a way that affects
  the games, but they also have no test coverage of their own. **Risk**: High, because this is a
  once-per-frame hot path in both games with zero test coverage today.
- **`BltFast`**: IMPLEMENTED, color-key and no-color-key variants both exercised heavily
  (per-sprite compositing, called many times per frame via `decor.cpp`'s icon-drawing family).
  **Risk**: High for the same reason as `Blt` — heaviest real call frequency of any DirectDraw
  method, zero tests.
- **Source color key behavior**: implemented per README (`SetColorKey(DDCKEY_SRCBLT,...)` stores
  low/high values; 8-bit compares against palette index, 32-bit compares packed pixel values with
  an RGB-masked fallback). Exercised at asset-load time in both games. No test.
- **Clipping**: implemented for `BltFast`; behavior for `Blt` clipping is less exercised (both
  games only pass full-surface rects at their live `Blt()` call sites). No test.
- **Scaling**: not found as a requirement — neither game passes a differently-sized source/dest
  rect at any real call site. Correctly out of scope; no test needed.
- **`Flip`**: implemented as a "Simplified Present" per its own header comment, but **not called by
  either game** — both rely on `Blt`/`BltFast` auto-present against the primary surface instead.
  Low risk purely because it's unexercised, but this also means it has never been validated
  against real gameplay.
- **Presentation path**: documented in README (throttle/dirty-check/texture-upload/present), used
  by both games' every-frame `Display()` call via `Blt`. No dedicated automated test exists for the
  dirty-flag/throttle behavior, though it is testable in principle (deterministic given fixed
  timestamps).
- **`Lock`/`Unlock`**: implemented, used on the asset-load color-matching path in both games. No
  test; pitch correctness has not been independently verified by a test.
- **`GetSurfaceDesc`**: implemented, used on the asset-load path (`DDCopyBitmap`) in both games. No
  test.
- **`GetDC`/`ReleaseDC`**: **STUB** per the header's own Doxygen tag. This is the highest concrete
  DirectDraw risk found: `planetblupi`'s `IsIconPixel` (click hit-testing, `pixmap.cpp:729-731`,
  called from `decblupi.cpp:3399`) is a **live gameplay path**, not asset-load-time, that depends
  on `GetDC`/`ReleaseDC` returning something usable. Both games also use it at asset-load time
  (`DDCopyBitmap`, `DDColorMatch`). A stub here risks either a hard failure or silently-wrong
  hit-testing in planetblupi. No test exists.
- **`IsLost`/`Restore`**: implemented but functionally inert — `IsLost()` always returns `DD_OK`
  (honestly documented `STUB`), so the `Restore()` branch inside both games' `RestoreAll()` never
  executes today. `Restore()` itself returns `DD_OK` unconditionally. This is reachable code
  (`RestoreAll()` is called from ~8-9 sites per game, plus one unconditional direct call from each
  game's main app) that has never been exercised by a real lost-surface scenario. No test.
- **Diagnostics/logging**: `FREE_DIRECT_DEBUG_DDRAW`, `FREE_DIRECT_DEBUG_PRESENTATION`,
  `FREE_DIRECT_DEBUG_COLORKEY`, `FREE_DIRECT_DEBUG_PRIMARY_CLEAR` all exist as runtime env-var
  checks in `.cpp` source (confirmed by the build audit), all default-off, none are hot-path
  unconditional logs by design — good. They are not wired as CMake options (a developer would need
  to pass a raw `-D` flag to force-enable at compile time instead of relying on the env var), which
  is a minor build-ergonomics gap, not a correctness risk.
- **Performance counters**: `FREE_DIRECT_DEBUG_PERF` exists (presents/s, uploads/s, blts/s per
  README), same env-var-gated pattern.
- **free-api bridge functions** (`FreeApiCreateSurfaceDC`, `FreeApiDestroySurfaceDC`,
  `FreeApiSetWindowFullscreen`): declared once in `free-api/include/free_api_bridge.h`, defined in
  `free-api/src/wingdi_dc.cpp`, called from `src/directdraw/DirectDraw.cpp` at 7 call sites. The
  migration in commit `57bca7d` (replacing a hand-declared `extern "C"` block with the shared
  header) is clean — no leftover duplicate declarations found anywhere in free-direct.

---

## 5. DirectSound implementation audit

- **`DirectSoundCreate`**: implemented, opens SDL audio subsystem, gracefully returns
  `DSERR_NODRIVER` with no audio hardware (per header comment) — this is exactly the headless
  behavior CI would need, but it is not exercised by any committed test today.
- **Audio device lifecycle**: one shared `SDL_AudioDeviceID` opened once, shared by all buffers
  (per README). Not tested.
- **`SetCooperativeLevel`**: implemented, accepted and ignored beyond storing the level (SDL needs
  no privilege negotiation). Used once per game.
- **`CreateSoundBuffer`**: implemented, used once per game (real path) plus twice more in dead code
  (`soundbass.cpp`, `wave.cpp`) per §2.2. Not tested.
- **PCM format handling**: implemented, `PCMWAVEFORMAT` read (not `WAVEFORMATEX` directly) with a
  documented deliberate padding-safety fix (`DirectSound.cpp:11-14`) to avoid an LP64
  struct-padding mismatch — this deviation is in code comments but **not yet in
  `docs/directsound-limitations.md`** (which doesn't exist yet).
- **`Lock`**: implemented, returns a direct pointer into an internal PCM buffer; wrap-around
  two-region locks supported per README. Not tested.
- **`Unlock`**: implemented as a no-op (data used as-is on next `Play()`). Not tested.
- **`Play`**: PARTIAL — no looping implemented, but this is immaterial for both target games since
  neither ever passes a non-zero flags word to a reachable `Play()` call (§2.2 confirms). Not
  tested.
- **`Stop`**: implemented (`SDL_ClearAudioStream`, immediate silence). Not tested.
- **`GetStatus`**: implemented (`DSBSTATUS_PLAYING` iff `SDL_GetAudioStreamQueued() > 0`). Not
  tested.
- **`SetCurrentPosition`**: PARTIAL — SDL stream is not seekable, but both games only ever call it
  with `0` (rewind), which a stream reset satisfies exactly. Not tested.
- **`SetVolume`**: implemented, approximate but perceptually reasonable centibel→gain conversion,
  clamped to the valid DirectSound range. Not tested.
- **`SetPan`**: PARTIAL — mono-only constant-power pan; both games' sources are mono per their own
  `PCMWAVEFORMAT` setup, so this matches real usage, but would silently do nothing useful if a
  stereo buffer were ever passed (not observed today). Not tested.
- **Looping behavior**: **re-confirmed this audit** — zero references to `DSBPLAY_LOOPING` or the
  substring "LOOPING" anywhere in either game's source (case-sensitive and case-insensitive grep).
  CLAUDE.md's policy against implementing looping speculatively remains correctly grounded in
  evidence; do not implement it.
- **Dummy/headless audio behavior**: `DirectSoundCreate`'s graceful `DSERR_NODRIVER` fallback is
  the right shape for headless CI, but nothing in `CMakeLists.txt`/`tests/` currently exercises it
  (no `SDL_AUDIODRIVER=dummy` usage found anywhere in the repo).
- **Diagnostics/logging**: `FREE_DIRECT_DEBUG_DSOUND` and `FREE_DIRECT_DEBUG_DSOUND_FORMAT` exist
  as env-var-gated runtime checks in `DirectSound.cpp`, following the same pattern as DirectDraw's
  debug flags — good, no unconditional hot-path logs found.

**Do not implement**: capture, 3D audio, or any DirectSound API beyond this list — no call site in
either game needs them, confirmed by this audit.

---

## 6. DirectPlay implementation audit

This subsystem is materially more advanced than `include/dplay.h`'s file-level `@note Status:
STUB` comment implies (see §3's documentation-accuracy finding). Every `IDirectPlay2A` method
except `DirectPlayEnumerateA`/`W` has moved from unconditional-`DP_OK` stub to real, validated,
tested behavior.

- **`DirectPlayCreate`**: real — null `lplpDP` → `DPERR_INVALIDPARAMS`; `*lplpDP` zeroed before any
  failure path; `pUnkOuter != nullptr` → `DPERR_NOAGGREGATION`; allocation failure →
  `DPERR_OUTOFMEMORY`.
- **`DirectPlayEnumerateA`/`W`**: **genuinely still a stub** — both return `DP_OK` and invoke the
  callback zero times. This is Decision 1 in `docs/directplay-design.md`: decided, not yet coded.
  It blocks `free-eggbert`'s `CNetwork::EnumProviders`/`CreateProvider` bound check, which requires
  at least one enumerated provider before it will ever call `DirectPlayCreate` — meaning **the
  reachable game-integration path through provider selection is not exercised end-to-end today**,
  even though the lower-level DirectPlay object/session/player/message machinery all works.
- **`QueryInterface`**: real, `memcmp`-based GUID comparison correctly distinguishes
  `IID_IDirectPlay`/`IID_IDirectPlay2A`, `AddRef()`s on success, `E_NOINTERFACE` + null-out
  otherwise.
- **`EnumSessions`**: real — validates `dwSize` when a filter struct is given, rejects a null
  callback, applies `guidApplication`/`DPENUMSESSIONS_AVAILABLE` filters, iterates a live
  process-wide loopback-hosted-session registry. **Loopback-only** — an ENet-hosted session is
  invisible to it; the originally-planned wire-protocol Discovery/DiscoveryResponse exchange
  (packet types already defined in `DirectPlayWireProtocol.hpp`, unused) was functionally
  superseded for loopback by this synchronous-registry shortcut, but is still genuinely needed for
  ENet, where no such shortcut exists.
- **`Open`**: real but asymmetric by backend/role. Validates `dwSize`/null/`dwFlags`/already-open;
  generates `guidInstance` when hosting with an all-zero caller value.
  - Loopback: hosting calls `Listen(51322)` + registers for `EnumSessions`; joining calls
    `Connect(nullptr, 51322)` (fails synchronously with `DPERR_NOSESSIONS` if nobody's listening),
    fires a fire-and-forget `Join` wire packet, and returns immediately (async join model, Decision
    16 — this is a deliberate design choice, not a missing feature).
  - ENet: hosting calls `Listen(51321)`. **The joining branch never calls `Connect()` at all** —
    confirmed by direct code reading. ENet joining is unimplemented, not partially implemented.
- **`CreatePlayer`**: real — validates `DPNAME.dwSize` when non-null, enforces `dwMaxPlayers`
  (`DPERR_CANTCREATEPLAYER` when full), allocates sequential DPIDs starting at 0 (host's first
  local player is DPID 0 — Decision 3).
- **`Send`**: real for exactly two paths:
  1. **Self-send** (`idTo == idFrom`): validates `idFrom` is a known local player, enqueues
     directly into the local message queue, bypassing the transport entirely (Decision 12).
  2. **Host-to-one-specific-assigned-remote unicast** (host role only): validates both DPIDs,
     checks payload size ≤ 4096 bytes (`DPERR_SENDTOOBIG` otherwise), serializes a wire header,
     sends via the transport.
  A joining role's `Send()` to anything but itself unconditionally returns `DPERR_INVALIDPLAYER`.
  **No broadcast and no non-host-to-non-host relay exist anywhere** — confirmed, no relay/forward
  logic found in `Send`/`Receive` or either transport.

  **The concrete bug**: `free-eggbert`'s only reachable `Send()` call pattern is
  `Send(m_dpid, 0, ...)` (`src/network.cpp:254`) — a broadcast to DPID 0/`DPID_ALLPLAYERS`. Because
  the host's own local player is *also* assigned DPID 0 (Decision 3), when the **host** calls this,
  `idTo (0) == idFrom (0)` and the message hits the **self-send** branch instead of reaching any
  remote client. The message is silently swallowed into the host's own queue. This is not a future
  risk — it is what today's code does, right now, for the one real gameplay send pattern that
  exists. `docs/directplay-design.md` Decision 15 names the *ambiguity* ("is 0 broadcast, or the
  player whose DPID happens to be 0?") but does not resolve it, and no `plan.md` task currently
  tracks this specific collision. This is flagged as a new backlog item, not resolved here (per
  this audit's explicit instruction not to decide DPID-0 semantics unilaterally).
- **`Receive`**: real, multi-stage: services the transport; drains disconnect notifications (host
  role: decrements player count, removes from `remotePlayerIds`); accepts pending connections up to
  `dwMaxPlayers` (rejecting any still-pending once over cap); drains real wire packets (`Data` →
  enqueue, `JoinAccept` → DPID adoption for the joining role, anything else dropped); finally
  dequeues from the local message queue. A joining role reports `DPERR_NOCONNECTION` once its queue
  is empty and it's no longer connected to the host. No unconditional stub remains.
- **`Close`**: real — unregisters from the `EnumSessions` registry, shuts down/releases the
  transport, clears all session/player/message state.
- **`Release`**: real atomic refcounting; defensively unregisters and shuts down the transport
  before `delete this` even if `Close()` was never called first.
- **Loopback transport**: fully real — process-wide port-keyed static registry gives it a genuine
  multi-instance connection lifecycle; `Send`/`Receive` deliver real bytes; peer bookkeeping
  mirrors ENet's shape; `Shutdown()`/destructor scrub peer pointers symmetrically. Zero
  ENet/SDL identifiers (confirmed by header-hygiene grep — this is a `src/`-private header, but the
  invariant matters for keeping backend abstractions honest).
- **ENet transport**: real socket-level integration — process-wide refcounted
  `enet_initialize`/`enet_deinitialize`; `Listen`/`Connect` create real sockets; `Send` supports
  self-targeted and DPID-addressed delivery with reliable/unreliable mapping
  (`DPSEND_GUARANTEED` → `ENET_PACKET_FLAG_RELIABLE`); `Service()` non-blockingly drains
  `enet_host_service()`; the most recent commit (`ae484a1`) added genuine receive-side buffering.
  **Gap**: nothing in `DirectPlay.cpp` calls this class's `Connect()` for the joining role — a
  capable, tested-at-the-transport-level backend with no DirectPlay-level consumer for joining,
  unicast-from-a-joiner, or discovery.
- **Wire protocol**: real and genuinely used in production (not just tested in isolation) —
  `DirectPlayWirePacketHeader` (magic/version/type/2 GUIDs/idFrom/idTo/payloadLength) is exercised
  by live `Join`/`JoinAccept`/`Data` call sites. Defensively validates truncation and
  payload-length mismatch; does not yet check magic/version on deserialize.
- **Message queue**: real bounded FIFO (256 messages, 4096 bytes/message max), implements the full
  buffer-size-query/too-small/`DPERR_NOMESSAGES`/successful-copy contract.
- **Player/session state**: real, populated (`DirectPlaySession` holds lifecycle state, host flag,
  local/remote player ID vectors, next-DPID counter, session name/password strings, application and
  instance GUIDs, max/current player counts, owned transport and message queue).
  `DirectPlayPlayer` remains a genuinely empty scaffolding class — all real player state lives as
  loose vectors on `DirectPlaySession` instead. This matches Phase 1's own deferred task ("move
  DirectPlay2AImpl/DirectPlayImpl out of DirectPlay.cpp once they hold real state") which was never
  revisited even though its stated precondition has long been true.
- **DPID size and DPID 0 ambiguity**: DPID is correctly a 4-byte `DWORD` (Decision 3, required by
  `free-eggbert`'s 32-byte pointer-arithmetic stride over `NetPlayer`). The DPID-0-as-host-player
  decision is made and implemented; **the broadcast-collision consequence of that decision is not
  resolved** (see above) — this is the sharpest open item in the whole subsystem.
- **Broadcast behavior**: does not exist. Blocked on the DPID-0 question per this audit's explicit
  instructions — not decided here.
- **Host-side routing**: does not exist — a `Send()` addressed to a non-host recipient by a joining
  peer is not relayed by the host at all.
- **ENet join/connect problem**: unresolved design question, explicitly deferred — `DPSESSIONDESC2`
  has no address-like field for a joining `Open()` call to learn what host/port to dial. Confirmed
  by direct struct inspection (`include/dplay.h:128-151`).
- **ENet discovery problem**: unresolved — no wire-based discovery exists; the loopback
  synchronous-registry shortcut doesn't generalize to real sockets.
- **Test coverage**: `tests/directplay_tests.cpp` is 1,312 lines, 46 distinct test functions, all
  invoked from `main()`, standalone (not CMake/CTest-wired). **Confirmed: compiles standalone and
  all 46 pass** (verified this audit via direct `g++`/manual run — 0 failures, exit code 0). Covers
  self-send, loopback host/join, the join handshake, oversized-payload rejection, `dwMaxPlayers`
  enforcement, `Close()` cleanup, session registry cleanup, wire-header round-trip/validation, and
  malformed-`dwSize` rejection. **Not covered**: `Receive()`'s buffer-size-query path (implemented,
  untested), `Release()` without a prior `Close()` (implemented, untested), `QueryInterface`'s
  failure paths (unknown GUID, null `ppvObject` — implemented, untested only via historical
  uncommitted scratch harnesses per Phase 1/2 notes), `DirectPlayCreate`'s failure paths (same
  history), `DirectPlayEnumerateA`/`W` (correctly untested — they're still a real stub), and
  anything ENet-specific (the file never compiles `EnetDirectPlayTransport.cpp` or passes
  `-DFREE_DIRECT_ENABLE_ENET`).

---

## 7. Test/build audit

- **CMake test integration**: `CMakeLists.txt` (149 lines) defines the `free-direct` static library
  and `FREE_DIRECT` demo executable, plus ENet gating (`FREE_DIRECT_ENABLE_ENET`,
  `FREE_DIRECT_USE_SYSTEM_ENET`) and a `FREE_DIRECT_DIAGNOSTICS` compile-definition option. **There
  is no `FREE_DIRECT_BUILD_TESTS` option, no `tests/CMakeLists.txt`, no `add_test`/
  `enable_testing()` anywhere.** `tests/directplay_tests.cpp` is completely unwired.
- **`tests/directplay_tests.cpp` wiring**: confirmed not wired into CMake/CTest at all — the only
  `tests/` reference in `CMakeLists.txt` search results is the test file's own header-comment build
  instructions. This is `plan.md` Phase 15, entirely unstarted.
- **Standalone free-direct build**: `cmake -B ... ` on free-direct alone **fails by design**
  (documented, not a bug) — free-api requires `SDL3::SDL3`/`SDL3_image::SDL3_image`/
  `SDL3_mixer::SDL3_mixer` targets that free-direct never vendors itself; it only finds targets a
  parent project already created, or system SDL3 via `-DFREE_API_USE_SYSTEM_SDL3=ON`. This
  three-tier acquisition strategy is intentional per the build audit.
- **Build through `../free-eggbert`**: **succeeds**, confirmed this audit — full configure + build,
  zero errors, `FREE_DIRECT`/`SPEEDY_BLUPI_WINDOWS` targets both built.
- **Build through `../planetblupi`**: **succeeds**, confirmed this audit — full configure + build,
  zero errors, `FREE_DIRECT`/`PLANET_BLUPI_WINDOWS` targets both built.
- **ENet-enabled build**: `third_party/enet` submodule is initialized and populated (pinned
  `v1.3.15-64-g5a9c537` per this audit's `git submodule status`; `plan.md`/`NEXT.md` reference it as
  `v1.3.18-17-g5a9c537` in prose — the pin itself was independently re-verified in-sync, only the
  narrative version string differs slightly between sources, not a functional concern).
  `-DFREE_DIRECT_USE_SYSTEM_ENET=ON` has never been exercised successfully in this environment (no
  system `libenet` package installed) — the vendored path is the one that actually gets tested.
- **System SDL / vendored SDL paths**: free-direct itself has no `FREE_USE_SYSTEM_SDL` option (that
  name belongs to a sibling game's own vendoring script, referenced only in a comment); it purely
  consumes whatever `SDL3::SDL3` target already exists in the parent build.
- **Dummy SDL video/audio test setup**: **not present**. No `SDL_VIDEODRIVER`/`SDL_AUDIODRIVER`
  env-var usage found anywhere in `CMakeLists.txt`, `tests/`, or docs. The underlying vendored SDL3
  build does compile in `dummy`/`offscreen` video and `dummy` audio drivers (confirmed in configure
  output), so the capability exists upstream — free-direct's own build/test setup just doesn't use
  it yet.
- **Headless CI viability**: aspirational only today (`plan.md` Phase 15's unchecked acceptance
  criteria use the word "headlessly" explicitly) — not yet wired up.
- **Header hygiene**: the "no `SDL_`/`ENet`/`SdlNet` identifiers under `include/`" invariant
  **holds** — the only `SDL_` occurrences under `include/` are inside Doxygen prose comments in
  `dsound.h` (describing behavior in English, not referencing a real symbol/include). Zero
  `ENet`/`enet`/`SdlNet`/`SDL_net` hits. The three public headers only `#include <windows.h>` and
  `<mmsystem.h>` (both free-api compat headers).
- **free-api bridge**: clean, single source of truth in `free-api/include/free_api_bridge.h`, no
  leftover hand-declarations in free-direct after the `57bca7d` migration.

---

## 8. Out-of-scope confirmation

| Item | Status | Evidence this audit |
|---|---|---|
| Direct3D | Out of scope, absent | No `Direct3D`/`IDirect3D` identifiers anywhere in `include/`/`src/` |
| DirectInput | Out of scope, absent | No `DirectInput`/`IDirectInput` identifiers anywhere |
| DirectMusic | Out of scope, absent | Not referenced by either game or free-direct |
| Full DirectDraw | Out of scope | Only the subset in §2.1/§3 exists; no `DDCAPS`/scaling/`Flip`-chain beyond what's documented |
| Full DirectSound | Out of scope | No capture, no 3D audio, no looping (§2.2/§5) |
| Full DirectPlay lobby system | Out of scope, absent | No `IDirectPlayLobby`, no groups anywhere; confirmed by fresh grep of both `dplay.h` and `free-eggbert` |
| Generic network browser / LAN broadcast discovery | Not implemented, explicitly gated | `plan.md` Phase 8: "ask the user before starting this task" — not asked yet |
| Registry/COM/OLE beyond minimal interface shapes | Out of scope | Only `QueryInterface`/`AddRef`/`Release` COM shape exists; no registry, no OLE automation |
| Arbitrary DirectX 3 games | Out of scope | Scope is bound to `free-eggbert`/`planetblupi` call sites only, re-verified this audit |
| Unused DirectX flags | Minor presence, correctly labeled | `DDBLT_COLORFILL`/`DDBLT_KEYSRC`/`DPSESSION_KEEPALIVE`/`MIGRATEHOST`/`DPESC_TIMEDOUT` — defined but unused by either game; retained for API-shape/link compatibility per CLAUDE.md policy, not scope creep |
| Unused DirectX interfaces | None found | No interface exists in `include/` without a real call site or COM-shape justification |
| SDL leaking into public headers | **Not occurring** | Confirmed by grep — zero real SDL/ENet symbols under `include/`, only prose comments |

---

## DirectPlay Plan Reconciliation

### 1. Existing DirectPlay plan files found
- `plan.md` (Phases 0-12 are DirectPlay-relevant, lines 34-1767)
- `docs/directplay-callsite-audit.md` (373 lines, Phase 0 deliverable)
- `docs/directplay-design.md` (1,645 lines, 19 numbered Decisions — the authoritative,
  currently-accurate architectural log, materially ahead of `plan.md`'s own checkbox state)
- `tests/directplay_tests.cpp` (46 tests, not CTest-wired)
- `NEXT.md` (exists, one commit stale relative to HEAD — the missing commit, `57bca7d`, is an
  unrelated free-api bridge cleanup, not a DirectPlay change, so this is a minor freshness gap, not
  a substantive one)

### 2. Existing DirectPlay tasks already implemented (Phases 0-10, cross-referenced against code)
Phase 0 (audit) — done. Phase 2 (state model) — done. Phase 3 (message queue) — done, tests exist
(acceptance criteria itself says "partially met" only because CTest wiring, a Phase 15 task, is
still open). Phase 4 (loopback backend) — done, later subsumed by a fuller version (Decisions
10-14). Phase 5 (ENet planning/skeleton) — done, including real vendored ENet submodule. Phase 6
(session hosting) — loopback done; ENet hosting/Service()/accept-to-cap done. Phase 7 (session
joining, loopback slice) — done, including a real async join handshake. Phase 8 (session
enumeration, loopback slice) — done via a synchronous registry (a deliberate substitution for the
originally-planned wire-protocol discovery exchange). Phase 9 (player management, reachable
subset) — DPID-0 conflict resolved, `dwMaxPlayers` cap enforced. Phase 10 (send/receive, partial) —
host-to-one-assigned-remote unicast done and tested.

### 3. Partially implemented
- **Phase 6/7 ENet side**: ENet can genuinely move and now buffer bytes (Decision 19), but nothing
  in `DirectPlay.cpp` wires the ENet branch to the join handshake or unicast Send/Receive; ENet
  joining never calls `Connect()`.
- **Phase 7 host-address resolution**: solved for loopback (fixed port), unresolved for ENet
  (`DPSESSIONDESC2` has no address field).
- **Phase 8 discovery**: loopback's synchronous-registry shortcut satisfies the *loopback*
  acceptance criteria but doesn't address ENet, where the originally-planned wire-protocol
  Discovery/DiscoveryResponse packets (already defined in `DirectPlayWireProtocol.hpp`, unused)
  are still needed.
- **Phase 9 player identity**: cap enforcement done; name storage, player data bytes, event-handle
  signaling, duplicate detection, and lost-vs-clean-removal distinction are all explicitly
  **not implemented, each with a written "no observable consumer without new API surface" rationale**
  (Decision 17) — not silent gaps.
- **Phase 10 addressed delivery**: unicast works; **broadcast and host-routing between non-host
  peers do not exist at all** — see the new bug finding below.

### 4. Still TODO (not started)
Phase 11 (error-semantics sweep, `docs/directplay-limitations.md`), Phase 12 (SDL3_net — correctly
not started per explicit "not until ENet is stable" policy), `DPID_ALLPLAYERS`/`DPID_SYSMSG`
constants, real `DirectPlayEnumerateA`/`W` provider enumeration (Decision 1, decided not coded),
Phase 15 (CMake/CTest wiring — tests already exist and pass; only wiring is missing, the cheapest
real gap in the whole subsystem), Phase 8's LAN-broadcast-discovery task (gated on asking first).

### 5. Blocked by unresolved design questions (none resolved by this audit — reported only)

- **(a) Is DPID 0 broadcast/host/invalid?** **ANSWERED** for allocation (Decision 3: host's first
  local player gets DPID 0), but the **broadcast-collision consequence is explicitly unresolved**
  — Decision 15 names the ambiguity ("is 0 broadcast, or the specific player whose DPID is 0?")
  without resolving it. This audit adds a sharper, concrete consequence: under today's code, the
  host's only real broadcast call pattern actually executes as a self-send, silently. Still not
  decided here, per instructions — flagged as a new backlog item requiring a user decision before
  broadcast can be implemented.
- **(b) How does `Open(...JOIN...)` discover the host address for ENet?** **UNRESOLVED.** No
  address-like field exists in `DPSESSIONDESC2`; `plan.md` Phase 7 and the design doc both describe
  this as needing "its own design pass," not yet taken.
- **(c) Should FreeDirect implement LAN broadcast discovery?** **ANSWERED — decided against for
  now, pending an explicit ask.** `plan.md` Phase 8: "ask the user before starting this task." Not
  asked yet, so remains not-started, not blocked-by-ambiguity.
- **(d) Should the host route messages between non-host peers?** **NOT implemented, explicitly
  acknowledged as future work**, not silently missing — Phase 10/Decision 15 both name it directly
  as unbuilt star-topology relay.
- **(e) Should player names be stored?** **ANSWERED as "real need exists (free-eggbert always
  supplies a short name), but storage deliberately deferred pending a design conversation"** —
  Decision 17 explicitly declines to store an unobservable value and lists candidate resolution
  paths without choosing one.

### 6. Obsolete tasks (with reasons — not deleted, annotated in the new backlog instead)
- Phase 8's originally-planned wire-protocol Discovery/DiscoveryResponse round-trip is functionally
  superseded **for loopback only** by Decision 18's synchronous registry — a deliberate,
  documented simplification, not an oversight. It remains a live need for ENet specifically, so it
  is not fully obsolete, only partially superseded.
- Several Phase 7 task descriptions assume a *blocking* `Open()` join contract ("handle a join
  timeout: fail `Open`..."), superseded by Decision 16's deliberate **asynchronous** join model.
  The task text should be revised to match the chosen model rather than silently left describing
  an un-chosen approach.
- Phase 9's "reserve an invalid DPID value (0)" task resolved in the *opposite* direction from its
  own title (DPID 0 is assigned to a real player, not reserved) — `plan.md` already self-annotates
  this contradiction, so it is a self-documented resolution, not a hidden inconsistency.

### 7. Gaps requiring new `plan.md` tasks (added in the 24-Hour Backlog below)
1. **The host DPID-0 broadcast/self-send collision** — the single sharpest, most concrete gap this
   audit found. Needs a task to make the collision observable/documented (a regression test proving
   today's exact behavior) without unilaterally deciding the broadcast semantics, plus a
   design-question task asking the user to resolve it.
2. CMake/CTest wiring for `tests/directplay_tests.cpp` (Phase 15) — the largest mechanical gap.
3. Committed regression tests for `QueryInterface`/`DirectPlayCreate` failure paths (previously
   verified only via uncommitted scratch harnesses).
4. A committed test for `Receive()`'s buffer-size-query path.
5. A committed test for `Release()` without a prior `Close()`.
6. Fixing the stale `@note Status: STUB` Doxygen tags across `include/dplay.h` and
   `DirectPlay2A`/`DirectPlay` methods in `DirectPlay.cpp` to match real implementation status —
   cheap, safe, high documentation-accuracy value.
7. `docs/directplay-protocol.md`, `docs/directplay-limitations.md`, `docs/networking-backends.md`
   — referenced by name elsewhere, none exist yet (Phase 16, not yet due, but worth scheduling).
8. `plan.md` checkbox drift — Phases 6-10's checked/unchecked counts undercount real progress
   because Decision-log-driven work (Decisions 10-19) outpaced going back to tick corresponding
   boxes. Safe direction (under- not over-claiming), but reduces `plan.md`'s reliability as a
   single source of truth without cross-referencing `docs/directplay-design.md` and `NEXT.md`.

**Bottom line**: the loopback backend (Phases 0-9, and the reachable slice of Phase 10) is
genuinely complete and tested; the ENet backend has real transport plumbing but an unfinished
DirectPlay-level protocol layer; and the most urgent unaddressed item — ahead of any of the five
explicitly-flagged design questions — is that broadcast delivery doesn't exist and silently
collides with self-send under the project's own DPID-0 decision, for the one `Send()` pattern
`free-eggbert` actually, provably, and currently calls.
