# DirectPlay Call-Site Audit

This document is the Phase 0 deliverable of `plan.md`. It records what `free-eggbert` and
`planetblupi` actually call in DirectPlay (and, secondarily, in DirectDraw/DirectSound, where
Phase 0 also asked for cross-game call-site counts). Every verdict below cites a concrete file and
function/line. Where reachability could not be confirmed from source, that is stated explicitly
instead of being assumed.

**Audit provenance** (so this document is reproducible/re-checkable):

| Repository | Path | HEAD commit | Commit date |
|---|---|---|---|
| free-direct (this repo) | `.` | `fc13d9a03a5e05582f1acbfb4a6a5f1f526ff9df` | 2026-07-04 |
| free-eggbert | `../free-eggbert` | `dae5652f1566d86017f68c0eb5de16ac9a856bd6` | 2026-06-08 |
| planetblupi | `../planetblupi` | `db61ffe75bb83da0c96b01d1aa72fe8b82ffe9ad` | 2026-06-13 |

Both sibling repositories exist and were inspected directly (not guessed at). If either
repository is later updated, this audit should be re-run rather than assumed still accurate.

---

## 1. Current FreeDirect DirectPlay surface (baseline inventory)

### 1.1 `include/dplay.h` declarations

- Error codes: `DP_OK`, `DPERR_INVALIDPARAMS`, `DPERR_OUTOFMEMORY`, `DPERR_UNSUPPORTED`,
  `DPERR_NOINTERFACE`, `DPERR_GENERIC`, `DPERR_ACTIVEPLAYERS`, `DPERR_ACCESSDENIED`,
  `DPERR_CANTADDPLAYER`, `DPERR_CANTCREATEPLAYER`, `DPERR_CANTCREATEGROUP`,
  `DPERR_CANTCREATESESSION`, `DPERR_CAPSNOTAVAILABLEYET`, `DPERR_ALREADYINITIALIZED`,
  `DPERR_INVALIDFLAGS`, `DPERR_EXCEPTION`, `DPERR_INVALIDPLAYER`, `DPERR_INVALIDOBJECT`,
  `DPERR_NOCONNECTION`, `DPERR_NONAMESERVERFOUND`, `DPERR_NOMESSAGES`, `DPERR_NOSESSIONS`,
  `DPERR_NOPLAYERS`, `DPERR_TIMEOUT`, `DPERR_SENDTOOBIG`, `DPERR_BUSY`, `DPERR_UNAVAILABLE`,
  `DPERR_PLAYERLOST`, `DPERR_USERCANCEL`, `DPERR_BUFFERTOOLARGE`, `DPERR_SESSIONLOST`,
  `DPERR_APPNOTSTARTED`, `DPERR_CANTCREATEPROCESS`, `DPERR_UNKNOWNAPPLICATION`,
  `DPERR_INVALIDINTERFACE`, `DPERR_NOTLOBBIED`, plus a local `E_NOINTERFACE` fallback.
  **No `DPERR_NOAGGREGATION` is defined** (real DirectPlay has none either — `DirectPlayCreate`'s
  aggregation rejection uses `DPERR_INVALIDPARAMS` today, called out in `plan.md` Phase 1 as
  something to reconsider).
- Flags: `DPENUMSESSIONS_AVAILABLE`, `DPOPEN_CREATE`, `DPOPEN_JOIN`, `DPOPEN_OPENSESSION` (same
  numeric value as `DPOPEN_JOIN`, `0x2`), `DPRECEIVE_ALL`, `DPSEND_GUARANTEED`, `DPESC_TIMEDOUT`,
  `DPSESSION_KEEPALIVE`, `DPSESSION_MIGRATEHOST`.
  **`DPID_ALLPLAYERS` and `DPID_SYSMSG` are NOT defined** — confirmed absent by `grep` over
  `include/dplay.h`.
- Types: `LPDIRECTPLAY`, `LPDIRECTPLAY2`, `LPDIRECTPLAY2A` (all `IDirectPlay`/`IDirectPlay2A`
  aliases — **no `IDirectPlay3A` type exists in FreeDirect at all**), `DPID`/`LPDPID` typedef'd as
  `DWORD_PTR` (see §5, this is a real hazard), `DPNAME`/`LPDPNAME`, `DPSESSIONDESC2`/
  `LPDPSESSIONDESC2`, `LPDPENUMDPCALLBACKA`/`W`, `LPDPENUMSESSIONS_CALLBACK2`.
- Functions: `DirectPlayEnumerateA`, `DirectPlayEnumerateW`, `DirectPlayCreate`.
- Interfaces: `IDirectPlay` (`QueryInterface`/`AddRef`/`Release` only) and `IDirectPlay2A`
  (`QueryInterface`/`AddRef`/`Release`/`EnumSessions`/`Open`/`CreatePlayer`/`Send`/`Receive`/
  `Close`). No group methods, no lobby methods, no `SetSessionDesc`/`GetPlayerName`/etc. — this is
  intentionally a small subset of the real `IDirectPlay2` vtable.
- `IID_IDirectPlay2A` is defined as a placeholder zero GUID (`{0}`), not a real Microsoft IID.
  There is no separate `IID_IDirectPlay` constant.

### 1.2 `src/directplay/DirectPlay.cpp` current stub inventory

- `DirectPlay2AImpl` (anonymous namespace): implements `IDirectPlay2A`.
  - `QueryInterface`: ignores `riid` entirely, always returns `E_NOINTERFACE` regardless of what
    is asked for (i.e. never succeeds — the opposite problem from `DirectPlayImpl`, see below).
  - `AddRef`/`Release`: real atomic refcounting, correct.
  - `EnumSessions`: ignores all parameters, always returns `DP_OK` with zero callback invocations.
  - `Open`: ignores all parameters, always returns `DP_OK`.
  - `CreatePlayer`: ignores most parameters, hardcodes `*lpidPlayer = 1` unconditionally, returns
    `DP_OK`.
  - `Send`: ignores all parameters, always returns `DP_OK`.
  - `Receive`: ignores all parameters, always returns `DP_OK` (**never `DPERR_NOMESSAGES`** — see
    §4.7, this is a confirmed behavioral mismatch against what `free-eggbert` expects).
  - `Close`: always returns `DP_OK`.
- `DirectPlayImpl` (anonymous namespace): implements `IDirectPlay`.
  - `QueryInterface`: the opposite bug from `DirectPlay2AImpl` — **always succeeds**, allocating a
    new `DirectPlay2AImpl` and returning it regardless of the requested `riid`, with a comment
    admitting this is "a hack." Never calls `AddRef()` on the object it did not just allocate (it
    happens to be fine here only because the object is freshly allocated with refcount 1, but this
    would be wrong for any future implementation that returns an existing pointer).
  - `AddRef`/`Release`: real atomic refcounting, correct.
- Free functions: `DirectPlayEnumerateA`/`DirectPlayEnumerateW` always return `DP_OK`, invoking
  the callback zero times. `DirectPlayCreate` rejects `pUnkOuter != nullptr` with
  `DPERR_INVALIDPARAMS`, otherwise allocates a `DirectPlayImpl` and returns `DP_OK`; does not
  initialize `*lplpDP` before the allocation (so a caller that ignores the return code sees
  whatever garbage was in `*lplpDP`, though the current single failure path — allocation failure —
  does set it via the `new (std::nothrow)` result, so today's only failure mode is actually safe;
  the future `DPERR_NOAGGREGATION` path fix in `plan.md` Phase 1 must preserve this).

### 1.3 Cross-reference: declaration vs. implementation gaps

No declaration in `include/dplay.h` lacks a corresponding stub method in `DirectPlay.cpp`, and
vice versa — the two files are consistent in *shape*. The gap is entirely in *behavior*: every
`IDirectPlay2A` method is an unconditional-return stub with no real state, matching `CLAUDE.md`'s
description of the current status.

---

## 2. free-eggbert DirectPlay findings

### 2.1 Files touching DirectPlay symbols

Confirmed by grepping `../free-eggbert` for `DirectPlay|dplay|IDirectPlay|DPID|DPSESSIONDESC|
EnumSessions|CreatePlayer` (excluding the vendored `dxsdk3/` reference SDK headers, which are not
compiled into the game and are not counted as call sites):

- `include/network.hpp` — `CNetwork` class declaration, `NetPlayer`/`NamedGUID` structs.
- `src/network.cpp` — `CNetwork` implementation (the actual DirectPlay client).
- `src/decnet.cpp` — gameplay-time `Send`/`Receive` usage.
- `src/event.cpp` — UI-layer wrapper methods and gameplay message handlers.
- `include/event.hpp` — declares `CEvent::NetEnumSessions`, `CEvent::NetSearchPlayer`,
  `CEvent::NetCreate`, `CEvent::NetStartPlay`, and `m_pNetwork`.

This matches the `plan.md` baseline exactly. **No other file in `free-eggbert/src` or
`free-eggbert/include` references any DirectPlay symbol.**

### 2.2 Per-API findings

For each API, this section states not just *whether the symbol is called*, but *whether the call
site is reachable from anything else in the codebase* — a distinction the original `plan.md`
baseline did not make, and which turned out to matter a great deal (see §2.3).

| API | Called? | Citation | Reachability |
|---|---|---|---|
| `IDirectPlay`/`IDirectPlay2A`/`IDirectPlay3A` | `IDirectPlay`, `IDirectPlay2A` only | `include/network.hpp:63` (`LPDIRECTPLAY2 m_pDP`); `src/network.cpp:89-91` | N/A (type usage, not a call) |
| `QueryInterface` | Yes | `src/network.cpp:91`: `lpDP->QueryInterface(IID_IDirectPlay2A, (LPVOID*)&m_pDP)` inside `CNetwork::CreateProvider` | Reachable — `CreateProvider` is called from `CEvent::NetCreate` (`src/event.cpp:4947`), but `NetCreate` itself has **zero callers** anywhere in `event.cpp` (see §2.3) |
| `DirectPlayEnumerateA`/`DirectPlayEnumerateW` | Yes | `src/network.cpp:60,62` inside `CNetwork::EnumProviders` | Reachable — `EnumProviders` is called from `CEvent::NetEnumSessions` (`src/event.cpp:4957`), but `NetEnumSessions` itself has **zero callers** anywhere in `event.cpp` (see §2.3) |
| `EnumSessions` | Yes | `src/network.cpp:141` inside `CNetwork::EnumSessions`, with `guidApplication` set and `DPENUMSESSIONS_AVAILABLE` flag | **Not reachable from any other file** — `CNetwork::EnumSessions()` (the public method) has zero callers anywhere in `free-eggbert/src` outside its own definition (see §2.3) |
| `Open` | Yes, both flags | `src/network.cpp:174` (`DPOPEN_OPENSESSION`, in `JoinSession`); `src/network.cpp:221` (`DPOPEN_CREATE`, in `CreateSession`) | **Not reachable** — `JoinSession`/`CreateSession` have zero callers anywhere in `free-eggbert/src` outside their own definitions (see §2.3) |
| `CreatePlayer` | Yes | `src/network.cpp:185` (in `JoinSession`); `src/network.cpp:233` (in `CreateSession`) | Same reachability caveat as `Open` — only reachable through `JoinSession`/`CreateSession`, which are themselves unreachable |
| `Send` | Yes | `src/network.cpp:254` (`Send(m_dpid, 0, !!dwFlags, lpData, dwDataSize)`, inside `CNetwork::Send`); called from `src/decnet.cpp:83,157` and `src/event.cpp:2159,2176,2212,2247,2281,4708` | **Reachable and exercised** — `src/decnet.cpp` and the `WM_PHASE_MULTI` handling in `event.cpp` (around line 4664 onward) are live, non-stub gameplay code paths |
| `Receive` | Yes | `src/network.cpp:269` (`m_pDP->Receive(&from, &to, DPRECEIVE_ALL, dataBuffer, &dataSize)`, fixed 500-byte `dataBuffer`); called from `src/decnet.cpp:166` | **Reachable and exercised**, same gameplay-time path as `Send` |
| `Close` | Yes | `src/network.cpp:189,237` (error-cleanup paths inside `JoinSession`/`CreateSession`); `src/network.cpp:293` (`CNetwork::Close`) | `CNetwork::Close()` itself has zero external callers; `m_pDP->Release()` (not `Close()`) is what actually runs when a `CNetwork` is destroyed (`CNetwork::~CNetwork`, `src/network.cpp:30`, called from `src/blupi.cpp` on `g_pNetwork` teardown) |
| Groups (`CreateGroup`/`AddPlayerToGroup`/etc.) | No | Confirmed zero matches via `grep -rn "CreateGroup\|AddPlayerToGroup\|DeletePlayerFromGroup\|DestroyGroup\|EnumGroups\|EnumGroupPlayers" src include` | N/A |
| Lobby APIs | No | Confirmed zero matches via `grep -rn "dplobby\|IDirectPlayLobby" src include` (the only `dplobby.h`-related content anywhere in the repository is the unused vendored reference header under `dxsdk3/sdk/inc/`) | N/A |
| Service provider enumeration | Yes (see `DirectPlayEnumerateA/W` row) | — | Same unreachable-wrapper caveat as `EnumSessions`/`Open` |

### 2.3 Critical finding: the DirectPlay lobby/session UI flow is unwired in this source snapshot

While tracing "how does `event.cpp`'s provider/session UI react to zero results," I found that
the state-machine handlers for every DirectPlay-related UI phase are **empty placeholders**, not
real logic:

```
src/event.cpp:3251   if (phase == WM_PHASE_DP_DOSERVICE)   { // ... }
src/event.cpp:3255   if (phase == WM_PHASE_DP_CANCELSERVICE) { // ... }
src/event.cpp:3259   if (phase == WM_PHASE_DP_JOIN)         { // ... }
src/event.cpp:3263   if (phase == WM_PHASE_DP_CREATELOBBY)  { // ... }
src/event.cpp:3267   if (phase == WM_PHASE_DP_REFRESH)      { // ... }
src/event.cpp:3271   if (phase == WM_PHASE_DP_CANCELSESSION){ // ... }
src/event.cpp:3275   if (phase == WM_PHASE_DP_DOCREATE)     { // ... }
```

(exact line numbers per `dae5652`; ten `// ...` placeholder bodies exist in `src/event.cpp` in
total, all under `WM_PHASE_DP_*` handling). Consistent with this, a whole-repository grep (across
all 23 files in `free-eggbert/src`, not just `event.cpp`) found:

- `CEvent::NetCreate` (`src/event.cpp:4942`) — declared, defined, **zero callers**.
- `CEvent::NetEnumSessions` (`src/event.cpp:4952`) — declared, defined, **zero callers**. (Note:
  despite its name, this method calls `CNetwork::EnumProviders()`, not `CNetwork::EnumSessions()`
  — a naming mismatch worth flagging on its own.)
- `CNetwork::JoinSession` (`src/network.cpp:162`) — declared, defined, **zero callers**.
- `CNetwork::CreateSession` (`src/network.cpp:207`) — declared, defined, **zero callers**.
- `CEvent::NetStartPlay` (`src/event.cpp:2196`) — declared, defined, **zero callers**.
- `CNetwork::EnumSessions` (`src/network.cpp:126`) — declared, defined, **zero callers** outside
  its own body.

**Conclusion:** `free-eggbert`'s DirectPlay support consists of a complete, self-consistent
`CNetwork` wrapper class and matching `CEvent` glue methods, plus a `WM_PHASE_DP_*` state-machine
skeleton — but the transition handlers that would actually invoke session creation/joining/
enumeration from user interaction are unimplemented placeholders in this source snapshot. Only the
**gameplay-time messaging path** (`Send`/`Receive`, exercised once a multiplayer game is already
in the `WM_PHASE_MULTI` phase) is live, reachable code. The lobby/session-browser flow
(`EnumSessions`, `Open`, `CreatePlayer`, provider/session enumeration UI reactions to zero
results) exists as a fully-formed API contract on the `CNetwork` side but **its real-world
reachability and exact expected UI behavior cannot be verified from this source snapshot** — it is
recorded here as **blocked/unverified**, not assumed working. This should be re-checked if
`free-eggbert`'s source is updated with the missing handler bodies.

This does not change what FreeDirect should implement (`CNetwork`'s calls into `IDirectPlay2A`
are still the authoritative contract to satisfy), but it substantially lowers confidence that
"expected behavior when no sessions are found" or "expected behavior when network initialization
fails" can be pinned down from UI behavior — those two Phase 0 questions are answered only at the
`CNetwork` level (see §2.4), not at the UI level.

### 2.4 Expected behavior at the `CNetwork` level (the only level that is actually verifiable)

- **No sessions found:** `CNetwork::EnumSessions()` (`src/network.cpp:126-148`) treats any non-
  `DP_OK` result from `IDirectPlay2A::EnumSessions` as failure, calls `FreeSessionList()`, and
  returns `FALSE`. It does not itself distinguish "zero sessions" from "enumeration failed" — a
  well-behaved `EnumSessions` implementation returning `DP_OK` with zero callback invocations
  would leave `m_sessions.nb == 0`, and `CNetwork::GetNbSessions()` would report `0`. No dedicated
  `DPERR_NOSESSIONS` handling exists in `CNetwork`.
- **Network initialization failure:** `CNetwork::CreateProvider()` (`src/network.cpp:85-99`)
  returns `FALSE` when `DirectPlayCreate` or the subsequent `QueryInterface` fails, releasing
  `lpDP` if it was allocated. As established in §2.3, the UI-level reaction to this `FALSE` cannot
  be traced further because `NetCreate`'s only caller would be the empty `WM_PHASE_DP_DOSERVICE`
  handler.

---

## 3. planetblupi DirectPlay findings

**Zero DirectPlay usage confirmed.** `grep -rniE "directplay|dplay|IDirectPlay|DPID|
DPSESSIONDESC|EnumSessions|CreatePlayer\(|CNetwork"` over `../planetblupi/src` and
`../planetblupi/include` (excluding vendored `third_party/`) returns no matches at all.
`planetblupi` is single-player only. DirectPlay scope is therefore defined **solely** by
`free-eggbert`.

---

## 4. Missing/mismatched constants

- **`DPID_ALLPLAYERS` / `DPID_SYSMSG`**: not defined in `include/dplay.h`. `free-eggbert` does not
  reference either by name — `src/network.cpp:254`'s `Send(m_dpid, 0, !!dwFlags, ...)` uses a
  literal `0` for the broadcast recipient. Adding named constants (matching real DirectPlay's
  convention that both equal `0`) is still worthwhile for FreeDirect's own code clarity, but is
  not required to satisfy any actual `free-eggbert` call site — confirmed, not assumed.

---

## 5. Critical hazard: `DPID` size mismatch (not previously documented)

This was not in the original `plan.md` Phase 0 baseline and is a genuine ABI/memory-layout hazard,
distinct from (and more severe than) the DPID-vs-array-index semantic question in §6.

- Real Microsoft DirectPlay: `typedef DWORD DPID` — confirmed in the vendored reference header
  `../free-eggbert/dxsdk3/sdk/inc/dplay.h:51`. 4 bytes on all platforms.
- FreeDirect: `typedef DWORD_PTR DPID, *LPDPID;` — `include/dplay.h:95`, chosen (per its own
  comment) "to stay ABI-safe on both 32-bit and 64-bit hosts." This is **8 bytes on a 64-bit
  build**.
- `free-eggbert/include/network.hpp:9-19` defines:

  ```c
  typedef struct {
      char  bIsPresent;
      char  ready;
      char  unk_2;
      char  unk_3;
      DPID  dpid;
      short team;
      char  name[22];
  } NetPlayer;
  ```

  With the original 4-byte `DPID`, this struct is exactly 32 bytes (4 + 4 + 2 + 22, padded to a
  4-byte boundary). Two functions in `free-eggbert` walk arrays of this struct using **raw pointer
  arithmetic with a hardcoded stride**, not `sizeof(NetPlayer)` or field access:
  - `CEvent::NetSearchPlayer` (`src/event.cpp:2180-2193`): `for (pDpid = (BYTE*)m_pNetwork->
    m_players[0].dpid; ...; pDpid += 32)` — hardcodes a 32-byte stride.
  - `CEvent::NetStartPlay` (`src/event.cpp:2196-2225`): `player = (int*) & pNetwork->
    m_players[0].dpid; ... player = player + 8;` — advances by 8 `int`s, i.e. 32 bytes.
- **If `free-eggbert` is compiled against FreeDirect's `include/dplay.h` on a 64-bit target, `DPID`
  becomes 8 bytes, `NetPlayer` grows to at least 40 bytes (with alignment padding before the
  8-byte-aligned `dpid` field), and both hardcoded-32-byte-stride functions above will read the
  wrong memory** — silently walking into adjacent array elements at the wrong offset rather than
  crashing outright, which makes this a correctness bug that could be very hard to diagnose later.
- Per project rules, this is **not fixed here** (no DirectPlay implementation in this pass, and
  game source in `free-eggbert` is not to be modified). It is recorded so that Phase 1/Phase 2 do
  not accidentally reintroduce or paper over it. Two honest resolution paths exist, to be decided
  later — not decided in this document — and always in a way that never requires editing
  `free-eggbert` source: (a) change FreeDirect's `DPID` typedef back to a 4-byte `DWORD` to match
  real DirectPlay and this game's assumption, accepting that a `DPID` can then no longer safely
  hold a truncated pointer value (FreeDirect does not need to encode pointers in DPIDs, so this is
  likely the safer choice); or (b) keep `DWORD_PTR` and accept that any target game relying on
  `sizeof(NetPlayer)`-independent raw layout assumptions is incompatible on 64-bit — which would
  make `free-eggbert` multiplayer effectively 32-bit-only. This decision is deferred to Phase 1/2
  and must be made explicitly, not by default.

**Note on source reliability:** `NetSearchPlayer`'s body (`src/event.cpp:2180-2193`) contains what
looks like a decompiler artifact / type-confusion bug independent of the above — it casts a `DPID`
(currently 8 bytes as `DWORD_PTR`, originally 4 bytes as `DWORD`) directly to a `BYTE*` starting
address and dereferences `*pDpid` (reading one byte) to compare against a full `dpid` value, and
`!pDpid[-4]` reads 4 bytes before the cast pointer. This should be treated as unreliable, possibly
already-buggy legacy code, not as a precise behavioral specification — it further reduces
confidence that this function represents intended, tested behavior of the original game.

---

## 6. DPID-vs-array-index pattern (semantic, not just size)

Separately from the size hazard in §5, `CNetwork::Receive` (`src/network.cpp:262-284`) contains:

```c
BOOL CNetwork::Receive(LPVOID pDest, DWORD dwDataSize, LPDWORD lpdwPlayer)
{
    DPID from = 0, to = 0;
    ...
    hr = m_pDP->Receive(&from, &to, DPRECEIVE_ALL, dataBuffer, &dataSize);
    ...
    *lpdwPlayer = -1;
    for (int i = 0; i < MAXNETPLAYER; i++)
    {
        if (m_players[i].bIsPresent && from == i)   // compares a DPID directly against a loop index
        {
            *lpdwPlayer = i;
            break;
        }
    }
    ...
}
```

This compares the received `DPID from` directly against a small integer loop index `i`
(`0..MAXNETPLAYER-1`), rather than against `m_players[i].dpid`. For this to ever match, the host's
DPID allocation must produce values that are themselves small integers equal to player array
index/join order — i.e., **FreeDirect's host-side DPID allocator must assign sequential small
integers starting at a value compatible with this comparison** for this specific lookup to work.
This is exactly the finding already anticipated in `plan.md` Phase 9, now confirmed with an exact
citation. It does not by itself indicate whether DPID `0` should be a valid first player (there is
a related, unresolved conflict with reserving `0` for `DPID_SYSMSG`/`DPID_ALLPLAYERS` — see
`plan.md` Phase 9's task on this exact point).

---

## 7. Cross-game DirectDraw call-site audit

Re-counted directly from source (not carried over from the prior `plan.md`/`CLAUDE.md` baseline
without verification; some baseline numbers turned out to be imprecise — see notes):

| Method | free-eggbert | planetblupi | Notes |
|---|---|---|---|
| `BltFast(` | 18 | 17 | Confirmed via direct grep of `src`/`include` in each repo. |
| Plain `Blt(` (excluding `BltFast`) | 1 | 0 | Confirmed. Both games overwhelmingly prefer `BltFast`. |
| `GetDC(` | 3 (`src/ddutil.cpp:160,287,312`) | 4 (`src/pixmap.cpp:729`, `src/ddutil.cpp:221,358,389`) | All are real `IDirectDrawSurface::GetDC` calls, no false positives. |
| `ReleaseDC(` | 3 (`src/ddutil.cpp:163,291,315`) | 4 (`src/ddutil.cpp:226,362,392`, `src/pixmap.cpp:731`) | Matches `GetDC` call count 1:1 in both games, as expected. |
| `IsLost(` | 4 (`src/pixmap.cpp:500,506,512,520`) | 4 (`src/pixmap.cpp:321,327,333,341`) | All inside each game's `CPixmap::Restore()`-adjacent surface-recovery routine. |
| Real `IDirectDrawSurface::Restore()` calls | 5 (`src/pixmap.cpp:502,508,514,522`, `src/ddutil.cpp:136`) | 5 (`src/pixmap.cpp:323,329,335,343`, `src/ddutil.cpp:192`) | **Correction to prior baseline**: a naive `grep -c "Restore("` over-counts by also matching each game's own `CPixmap::Restore()` wrapper method (definition + declaration) and an unrelated `MouseBackRestore()` helper. The real DirectDraw-interface call count is 5 in both games, not 8 as previously stated informally. `CPixmap::Restore()` itself is reachable via `g_pPixmap->Restore()` in `src/blupi.cpp` (line 383 in free-eggbert, line 332 in planetblupi). |

**Conclusion**, matching and refining the existing `CLAUDE.md`/`plan.md` guidance: `BltFast`
clipping is the higher-priority DirectDraw hardening target over plain `Blt`; `GetDC`/`ReleaseDC`/
`IsLost`/`Restore` are all real, paired, reachable call sites in both games (via each game's own
`CPixmap` surface-recovery logic) and deserve the Phase 14 audit attention already planned.

---

## 8. Cross-game DirectSound call-site audit

| Flag/method | free-eggbert | planetblupi | Notes |
|---|---|---|---|
| `DSBPLAY_LOOPING` | 0 | 0 | Confirmed absent in both games — matches prior baseline. Do not implement real looping speculatively (per `CLAUDE.md`/`plan.md` Phase 13). |
| `SetPan(` | 2 | 1 (`src/sound.cpp:458`) | Prior baseline for `free-eggbert` did not have an exact count; now confirmed as 2 call sites. |

---

## 9. Summary of Phase 0 verdicts

| Finding | Status |
|---|---|
| `../free-eggbert` exists | Confirmed |
| `../planetblupi` exists | Confirmed |
| `include/dplay.h` declaration inventory | Documented (§1.1) |
| `src/directplay/DirectPlay.cpp` stub inventory | Documented (§1.2), including two real `QueryInterface` correctness bugs (§1.2) not previously called out as precisely |
| Declaration/implementation cross-reference | No shape gaps found (§1.3) |
| `free-eggbert` DirectPlay file list | Confirmed matches prior baseline |
| `planetblupi` DirectPlay file list | Confirmed zero files (§3) |
| Missing `DPID_ALLPLAYERS`/`DPID_SYSMSG` | Confirmed absent; confirmed not required by any literal call site (§4) |
| `IDirectPlay`/`IDirectPlay2A`/`IDirectPlay3A` usage | Confirmed: only `IDirectPlay`→`IDirectPlay2A`, no `IDirectPlay3A` anywhere |
| `QueryInterface` usage | Confirmed call site; reachability from UI unverified (§2.2, §2.3) |
| `EnumSessions` usage | Confirmed call site; **reachability from anywhere outside `CNetwork` itself is unverified/blocked** (§2.2, §2.3) |
| `Open` usage | Confirmed both flag call sites; **reachability unverified/blocked** (§2.2, §2.3) |
| `CreatePlayer` usage | Confirmed call sites; **reachability unverified/blocked**, same as `Open` |
| `Send` usage | Confirmed call sites; **reachable and exercised** via gameplay code |
| `Receive` usage | Confirmed call site; **reachable and exercised** via gameplay code |
| `Close` usage | Confirmed call sites; `CNetwork::Close()` wrapper itself unreachable, but `Release()` is exercised via normal object teardown |
| Groups usage | Confirmed absent |
| Lobby usage | Confirmed absent |
| Service provider enumeration usage | Confirmed call site; **reachability unverified/blocked**, same as `EnumSessions` |
| Behavior on no sessions found | Documented at the `CNetwork` level only (§2.4); UI-level behavior unverifiable/blocked (§2.3) |
| Behavior on network init failure | Documented at the `CNetwork` level only (§2.4); UI-level behavior unverifiable/blocked (§2.3) |
| DPID-vs-array-index pattern | Confirmed and precisely cited (§6) |
| DPID size (`DWORD` vs `DWORD_PTR`) hazard | **New finding, not in prior baseline** — confirmed and documented (§5) |
| planetblupi DirectDraw call-site counts | Re-verified, one correction to `Restore()` counting method (§7) |
| planetblupi/free-eggbert DirectSound call-site counts | Re-verified (§8) |

No finding above was assumed without a direct source citation. Where reachability could not be
established, it is marked unverified/blocked rather than guessed.
