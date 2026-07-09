# DirectSound subsystem audit

This is a from-scratch, evidence-based audit of FreeDirect's DirectSound implementation
(`src/directsound/DirectSound.cpp`, 751 lines; declared in `include/dsound.h`, 205 lines), covering
performance, correctness, memory safety, edge cases under extreme situations, and a risk-analysis
synthesis, ending with a proposed-tasks list. It follows the same evidentiary standard as
`docs/audit_ddraw.md`: every finding cites exact file:line evidence, is cross-checked against real
call sites in `../free-eggbert` and `../planetblupi` (grepped directly, never assumed from memory),
and is graded on both **impact** (how bad if triggered) and **reachability** (whether either target
game's current, real code can trigger it today). Two performance claims were measured empirically
against the real compiled library, not just reasoned about (Section 8).

This document is **read-only analysis**. No code was changed to produce it, and no finding below
has been fixed, per this project's policy of treating audits and implementation as separate tasks
(`CLAUDE.md` Safety Rules).

## 1. Scope and methodology

- **Primary subject**: `src/directsound/DirectSound.cpp` (`SharedAudioDevice`,
  `DirectSoundBufferImpl`, `DirectSoundImpl`) and the public contract in `include/dsound.h`.
- **Builds on, does not duplicate**: `docs/directsound-limitations.md` already documents, in detail,
  the `PCMWAVEFORMAT`/`WAVEFORMATEX` padding fix, `SetPan`'s mono-only limitation,
  `SetCurrentPosition`'s non-seekable model, the deliberate absence of looping, the one-stream-
  per-buffer/restart-on-replay model, `CreateSoundBuffer`'s lack of `dwSize` (struct self-size)
  validation, graceful handling of a null/zero-sized descriptor, the missing-format fallback, and
  three dead-code call sites (`soundbass.cpp`, both games' `wave.cpp`, `PlaySoundDS`). All of that is
  treated as established and only referenced below, not re-derived. Everything in Sections 5-9 is a
  new finding from this audit unless explicitly marked as a reference to existing documentation.
- **Evidentiary standard**: identical to `docs/audit_ddraw.md` §1 — every finding is graded on
  Impact × Reachability, since a finding can be real and high-impact while still being confirmed
  unreachable by either target game's actual call sites today.
- **Not in scope**: DirectDraw and DirectPlay, per `CLAUDE.md`'s atomicity rule.

## 2. Executive summary

| # | Finding | Impact | Reachable today? | Section |
|---|---|---|---|---|
| S1 | `CreateSoundBuffer`'s `dwBufferBytes` is never bounded before an unguarded `std::vector::resize` | High — uncaught `std::length_error`/`std::bad_alloc` crashes the process | **Concretely plausible** — both games read this value directly, unvalidated, from an on-disk `.wav` file's header (`wavHdr.dwDSize`); a corrupted/truncated asset file reaches this path with no validation anywhere in between | 6.1, 8.2 |
| S2 | A fully-released `IDirectSound` (device close) followed by a fresh `DirectSoundCreate` (device reopen) measured **~51ms per cycle** — vs. ~0.06µs when another instance keeps the device open | High if this pattern ever occurs — ~3 frames' worth of stall at 60fps per cycle | **No** — both games call `DirectSoundCreate` exactly once and `Release()` exactly once, at startup/shutdown; confirmed by grep | 8.3 |
| S3 | `SharedAudioDevice::id()` reads `deviceId_` without taking `mutex_`, while `open()`/`release()` mutate it under the mutex | Medium (latent) — a data race under the C++ memory model if ever called cross-thread | **No** — confirmed no DirectSound call site in either game is ever reached from a secondary thread | 6.2 |
| S4 | No documented thread-safety/concurrency model for `DirectSoundBufferImpl`/`DirectSoundImpl` beyond atomic ref-counts | Medium (latent), same shape as `docs/audit_ddraw.md` F12 | Assumed no — matches both games' actual single-threaded usage | 6.3 |
| S5 | `Unlock()` never invalidates the pointer `Lock()` returned; a caller can keep writing to it indefinitely with no error | Low — semantic looseness vs. documented DirectSound contract, not a memory-safety bug (the pointer stays valid for the buffer's lifetime either way) | N/A — no known call site continues writing post-`Unlock()` | 7.4 |
| S6 | `Lock()` silently clamps an out-of-range `dwOffset` to `0` instead of returning `DSERR_INVALIDPARAM` | Low — undocumented instance of this project's general "clamp, don't error" pattern | N/A — not currently exercised with an out-of-range offset by either game | 7.1 |
| S7 | An extreme `nSamplesPerSec` (e.g. near `DWORD` max) reaches `SDL_CreateAudioStream` unchecked; its actual effect is untested here (depends on SDL3's own robustness) | Unknown — not verified either way | **No** — both games only ever pass real, valid WAV-file sample rates | 7.2, 9.3 |
| S8 | A near-zero `nSamplesPerSec` in an otherwise-valid descriptor discards the *entire* parsed format (channels and bit depth too, not just frequency) via `ensureStream()`'s all-or-nothing fallback | Low — audibly wrong output in a scenario neither game triggers, not a crash | **No** | 7.3 |

Unlike `docs/audit_ddraw.md`, this audit did **not** find an algorithmic hot-path defect —
`Play()`'s cost was measured directly and found negligible (Section 8.1). The headline findings here
are a memory-safety/robustness gap with a concretely plausible trigger (S1) and a dramatic but
currently-unreachable performance cliff (S2), rather than a routinely-reachable slowdown.

## 3. What's already solid (checked, not just assumed)

An audit should report what holds up, not only what's broken — matching `docs/audit_ddraw.md`'s
practice of noting genuine positives (e.g. its palette-conversion-buffer-reuse observation):

- **`Play()` has no missing-fast-path problem.** Measured at 0.0016ms/call for a realistic 2-second
  16-bit mono 22050Hz buffer (Section 8.1) — `DirectDraw.cpp`'s `BlitFrom` had a genuine, measured
  29x-vs-`memcpy` gap; nothing comparable exists here. `Play()` does exactly one `SDL_ClearAudioStream`
  and one `SDL_PutAudioStreamData` call per invocation, no redundant allocation, no per-call stream
  recreation (`ensureStream()` is guarded by `if (stream_) return true;`).
- **Reference counting and diagnostic-counter pairing are correctly balanced**, spot-checked the
  same way as `docs/audit_ddraw.md` §4.5: `dsBuffers`/`dsInstances`/`sdlAudioStreams` are each
  incremented and decremented from exactly the code paths that create/destroy the corresponding
  object, with no path found that leaks a live count. `~DirectSoundBufferImpl()` unconditionally
  calls `destroyStream()`, which itself is guarded by `if (stream_)` — no leaked `SDL_AudioStream`
  on any destruction order found.
- **Header `@note Status:` tags are accurate against the implementation** — unlike
  `docs/audit_ddraw.md`'s finding that DirectDraw's `GetDC`/`ReleaseDC` were mislabeled `STUB`
  despite being functionally real, every `dsound.h` status tag checked against its
  `DirectSound.cpp` implementation matched (`IMPLEMENTED` methods are fully implemented, `PARTIAL`
  methods have a real, specific, already-documented gap).
- **The `SharedAudioDevice` singleton's own open/release contract is internally consistent** — every
  `open()` success is paired with exactly one `release()` (`DirectSoundCreate`'s allocation-failure
  path correctly calls `release()` to undo a successful `open()`, Section 6 confirms this), and
  `open()`'s early-failure paths never touch `refCount_` at all, so there's no over/under-count
  possible from a failed open.

## 4. Real call-site cross-check

Traced directly in both `../free-eggbert/src/sound.cpp` and `../planetblupi/src/sound.cpp` (never
assumed from `docs/directsound-limitations.md` alone, though every prior finding there was
reconfirmed, not contradicted):

- **`DirectSoundCreate` is called exactly once per process in each game's live code path**
  (`free-eggbert/src/sound.cpp:373`, `planetblupi/src/sound.cpp:339`; the third match,
  `soundbass.cpp:336`, is inside the already-documented dead `_BASS` build). `m_lpDS->Release()` is
  likewise called exactly once, at shutdown, in both games. This is the evidence behind S2's
  "not reachable" verdict.
- **`dwBufferBytes` originates from an unvalidated on-disk `.wav` file field in both games** —
  `CSound::CreateBufferFromWaveFile` reads a `WaveHeader` struct directly via `fread`, then does
  `DWORD dwSize = wavHdr.dwDSize;` and passes it straight into `CreateSoundBuffer(dwBuf, dwSize,
  ...)` with no range check anywhere in between (`free-eggbert/src/sound.cpp:163-186`;
  `planetblupi/src/sound.cpp` has the identical pattern at its own `CreateBufferFromWaveFile`,
  `dwSize = wavHdr.dwDSize` immediately preceding its own `CreateSoundBuffer` call). This is the
  evidence behind S1's "concretely plausible" verdict — see Section 6.1 for the full chain.
- **Both games' `ReadData` helper calls `Lock(0, dwSize, ...)` *before* reading the corresponding
  bytes from disk** (`free-eggbert/src/sound.cpp:105`) — meaning the `data_.resize(bufferBytes_)`
  inside FreeDirect's `CreateSoundBuffer` has *already run*, using the raw unvalidated
  `wavHdr.dwDSize`, by the time the file's actual byte count is ever checked. A truncated file is
  caught gracefully by `fread`'s return value at that later point (`ReadData` returns `FALSE`), but
  only *after* the oversized allocation attempt already happened.
- **Music is never routed through DirectSound in either game** — confirmed via
  `midiOutOpen`/`mciSendCommand` (`free-eggbert/src/sound.cpp:293,601,621`) being the real music
  path; all `CreateSoundBuffer` call sites load short sound effects from `.wav` files via
  `CreateBufferFromWaveFile`/`PlaySoundDS`. This bounds the *realistic* buffer-size range for both
  games' own assets to short SFX clips, even though Section 6.1's validation gap is independent of
  that realistic range.
- **Up to `MAXSOUND` = 100 simultaneous `IDirectSoundBuffer` objects** are possible in both games
  (`free-eggbert/include/sound.hpp:15`, `#define MAXSOUND 100`; `m_lpDSB[MAXSOUND]`). Existing tests
  (`tests/directsound_tests.cpp`) only ever exercise 2 simultaneous buffers
  (`Test_TwoBuffers_PlaySimultaneously_BothReportPlayingIndependently`) — see Section 9.4.
- **No DirectSound call site in either game is ever reached from a secondary thread.**
  `free-eggbert/src/blupi.cpp:901` does spawn one secondary thread (`ThreadDisplay`), but its body
  (`blupi.cpp:932-953`) only calls `SetDecor()` and `g_pPixmap->Display()` — pure DirectDraw
  presentation, never anything DirectSound-related. `planetblupi` has no thread creation at all
  (`CreateThread`/`std::thread`/`_beginthread` all absent by grep). This is the evidence behind S3
  and S4's "not reachable" verdicts.
- **`SetVolume`/`SetPan` are called together, once per sound-trigger event**
  (`free-eggbert/src/sound.cpp:492-493`, `planetblupi/src/sound.cpp:457-458`, immediately before
  `Play()`), never in a per-frame loop — confirms `dsVolumeToGain`/`dsPanToGains`'s
  `std::pow`/`std::cos`/`std::sin` calls are not a hot path worth benchmarking; ruled out
  deliberately, not overlooked (see Section 8's scope note).

## 5. Performance analysis

### 5.1 No missing fast path — measured, not assumed

See Section 3 and Section 8.1. This section exists mainly to record a negative result explicitly:
`docs/audit_ddraw.md`'s methodology (measure before claiming a performance defect) was applied here
too, and it did not surface an equivalent finding. `DirectSoundBufferImpl` has no per-sample or
per-byte loop anywhere in its own code — `Play()`, `Lock()`, and `Unlock()` are all O(1) plus, at
most, one `SDL_PutAudioStreamData` call that copies `bufferBytes_` bytes once.

### 5.2 Same project-wide build-configuration gap as DirectDraw (cross-reference, not a new task)

`docs/audit_ddraw.md` §3.1 / `plan.md` `TASK-24H-0151` already covers this: the root
`CMakeLists.txt` never sets a default `CMAKE_BUILD_TYPE`, so a plain `cmake ..` builds every
translation unit — including `DirectSound.cpp` — without optimization. This audit did not measure a
DirectSound-specific before/after number (no algorithmic hot path was found worth isolating, unlike
`BlitFrom`), but the same one-line fix benefits this subsystem too. No new task is proposed here for
this — it's already tracked.

### 5.3 Device create/destroy cost is real and large — see Section 8.3

The one dramatic performance number this audit found (S2, ~51ms per full device close+reopen cycle)
is a memory/lifecycle-adjacent finding as much as a performance one; it's presented in full in
Section 8.3 rather than duplicated here.

## 6. Memory safety and lifecycle analysis

### 6.1 `CreateSoundBuffer`'s `dwBufferBytes` is never bounded (new finding, concretely evidenced)

```cpp
// DirectSound.cpp:240-242 (DirectSoundBufferImpl constructor)
bufferBytes_  = desc->dwBufferBytes;
dwFlags_      = desc->dwFlags;
data_.resize(bufferBytes_, 0);
```

`desc->dwBufferBytes` is a caller-supplied `DWORD` with no upper bound check anywhere before
`std::vector<uint8_t>::resize`. This is the same *class* of defect as `docs/audit_ddraw.md` F4
(`CreateSurface`'s unchecked `dwWidth`/`dwHeight`, now `TASK-24H-0154`) — an unsatisfiable resize
throws `std::length_error`/`std::bad_alloc`, uncaught, crossing the COM-style interface boundary
that `CLAUDE.md`'s Coding Style says must never be crossed by an exception. Unlike the DirectDraw
finding, there's no sign-conversion angle here (`DWORD` flows into `size_t` as a widening unsigned
conversion, never through an intermediate `int`), so the *only* risk is the missing upper bound, not
a negative-after-cast value.

**Reachability — more concrete than the equivalent DirectDraw finding.** Section 4 traces the exact
chain in both games: `wavHdr.dwDSize` is read directly from an on-disk `.wav` file's header via
`fread`, with **zero validation**, and flows straight into `CreateSoundBuffer`. DirectDraw's
`dwWidth`/`dwHeight` are always small hardcoded literals in both games — there is no equivalent
"parsed from a file on disk" path for DirectDraw's surface sizes. For DirectSound, a corrupted or
truncated `.wav` asset (disk corruption, an interrupted install/download, a hand-edited or
fan-made content pack) is a genuinely plausible way for an attacker-uncontrolled but
corruption-controlled `DWORD` to reach this exact code path — not merely a hypothetical malicious
API caller.

### 6.2 `SharedAudioDevice::id()` reads outside the mutex (new finding)

```cpp
// DirectSound.cpp:196
SDL_AudioDeviceID id() const { return deviceId_; }
```

`open()` and `release()` (`DirectSound.cpp:160-194`) both take `mutex_` before touching `deviceId_`.
`id()` does not. If `id()` is ever called from a different thread than the one calling `open()`/
`release()`, this is a data race on `deviceId_` under the C++ memory model (undefined behavior, even
though `SDL_AudioDeviceID` is a plain scalar). `id()` is called exactly once in the whole file,
from `ensureStream()` (`DirectSound.cpp:527`) — same-thread as every other DirectSound call, per
Section 4's confirmation that no DirectSound call site is ever reached from a secondary thread in
either game. Confirmed inert today; a five-line fix (take `mutex_` in `id()` too) would close it
permanently regardless.

### 6.3 No documented concurrency model (new finding, same shape as the DirectDraw audit's F12)

Ref-counting is atomic (`std::atomic<ULONG> refCount_` on both `DirectSoundBufferImpl` and
`DirectSoundImpl`), but no other mutable state — `data_`, `stream_`, `volume_`, `pan_`,
`playCursor_`, `bufferBytes_` — is synchronized. This mirrors `docs/audit_ddraw.md` §4.2's finding
almost exactly: atomic ref-counts can read as a thread-safety signal the rest of the class doesn't
back up. Confirmed matching both target games' actual single-threaded usage (Section 4). Worth the
same fix this audit recommended for DirectDraw: a one-line header comment stating the
single-threaded assumption explicitly, not a new locking scheme neither game needs.

### 6.4 `Lock()`'s wraparound math is memory-safe even for a nonsensical request (verified, not a defect)

```cpp
// DirectSound.cpp:426-437
DWORD size2 = dwBytes - available1;
if (ppvAudioPtr2 && pdwAudioBytes2) {
    *ppvAudioPtr2   = data_.data();
    *pdwAudioBytes2 = std::min(size2, bufferBytes_);
```

If a caller passes a `dwBytes` far larger than the buffer (e.g. near `DWORD` max), `size2` can be
huge, but it is always clamped via `std::min(size2, bufferBytes_)` before being reported — so the
two regions handed back to the caller can never describe more memory than `data_` actually owns. No
out-of-bounds access is possible here; worth stating explicitly since the unclamped intermediate
value momentarily looks alarming on first read. The only real issue is a correctness/API-contract
one, covered as S6/Section 7.1 (a wildly out-of-range request should probably fail with
`DSERR_INVALIDPARAM` rather than silently returning a memory-safe but semantically odd two-region
split).

## 7. Correctness against DirectSound semantics and code quality

### 7.1 `Lock()` clamps an out-of-range offset instead of erroring (new finding)

```cpp
// DirectSound.cpp:412-413
// Clamp offset.
if (dwOffset >= bufferBytes_) dwOffset = 0;
```

An out-of-range `dwOffset` silently redirects to offset `0` rather than returning
`DSERR_INVALIDPARAM`. This matches this project's general design philosophy elsewhere (e.g.
DirectDraw's `ClampRect` — clamp rather than reject) but, unlike several of DirectDraw's clamping
choices, is not yet written down as a deliberate decision in `docs/directsound-limitations.md`.
Confirmed not currently exercised with an out-of-range offset by either game (both always call
`Lock(0, ...)` with `DSBLOCK_FROMWRITECURSOR`, which ignores the offset argument entirely per
`DirectSound.cpp:406-410`).

### 7.2 Extreme `nSamplesPerSec` is passed to SDL unchecked and untested (new finding, deliberately not overclaimed)

```cpp
// DirectSound.cpp:261
srcSpec_.freq = static_cast<int>(pcm->wf.nSamplesPerSec);
```

`nSamplesPerSec` is a `DWORD`; casting a value near `DWORD` max to `int` produces a negative or
otherwise nonsensical value, which flows unchecked into `SDL_CreateAudioStream` (`ensureStream()`,
`DirectSound.cpp:548`). What SDL3 actually does with a negative/absurd `freq` was **not verified**
in this audit — that would require reading SDL3's own source, which is outside this project's code
and this audit's scope. This is recorded as an open, untested risk rather than a confirmed crash:
the honest finding is "this project does not validate or clamp the value before handing it to SDL,
and no test exercises this," not "this crashes." Confirmed not reachable by either game — both only
ever construct a `PCMWAVEFORMAT` from a real, valid WAV file's own sample rate field, never a
synthetic extreme value.

### 7.3 A near-zero sample rate discards the whole parsed format, not just the rate (new finding)

```cpp
// DirectSound.cpp:541-546 (ensureStream)
if (srcSpec_.freq == 0) {
    srcSpec_.format   = SDL_AUDIO_S16LE;
    srcSpec_.channels = 1;
    srcSpec_.freq     = 22050;
}
```

This fallback (already documented in `docs/directsound-limitations.md` for the *null-descriptor*
case, where all three fields are legitimately unset together) also fires whenever `nSamplesPerSec`
alone is `0` in an otherwise-valid, fully-populated descriptor — silently overwriting `format` and
`channels` too, not just `freq`. A hypothetical stereo, 8-bit buffer with a corrupted
`nSamplesPerSec == 0` field would silently become mono 16-bit rather than mono/stereo-preserving
with just a default rate substituted. Audibly wrong if it ever happened, not a crash. Confirmed not
reachable — no call site in either game constructs a format with a zero sample rate.

### 7.4 `Unlock()` never invalidates the `Lock()`-returned pointer (new finding, documentation gap)

```cpp
// DirectSound.cpp:454-461
HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1,
                      LPVOID pvAudioPtr2, DWORD dwAudioBytes2) override
{
    (void)pvAudioPtr1; (void)dwAudioBytes1;
    (void)pvAudioPtr2; (void)dwAudioBytes2;
    DS_LOG(...);
    return DS_OK;
}
```

Because `Lock()` hands out a raw pointer directly into `data_` (no staging buffer), `Unlock()` has
nothing to copy back — a reasonable simplification given the static-buffer model, already implied
by `docs/directsound-limitations.md`'s framing. What's not written down anywhere: real DirectSound
documents the returned pointer as invalid after `Unlock()`; FreeDirect's pointer stays valid and
writable for the buffer's entire lifetime, since nothing about it changes at `Unlock()` time. Not a
safety bug (no dangling pointer, no use-after-free), just a real semantic looseness relative to the
documented DirectSound contract, worth a one-line addition to `docs/directsound-limitations.md`
rather than a behavior change (no known caller relies on post-`Unlock()` writes being rejected).

### 7.5 Buffer sizes that aren't a multiple of the frame size are silently accepted (minor, new)

`bufferBytes_` is used as a raw byte count throughout with no check against
`nChannels * (wBitsPerSample / 8)` (the frame size). A `dwBufferBytes` that isn't an exact multiple
of the frame size (e.g. 3 bytes for 16-bit stereo, frame size 4) doesn't crash anything — it just
means the last partial frame is silently truncated when played. Real DirectSound's own behavior
here isn't byte-for-byte replicated (matching this project's general "not hardware-accurate beyond
what the target games need" stance), and neither game's WAV-derived buffer sizes are ever
non-frame-aligned in practice (real WAV files always contain a whole number of frames). Recorded for
completeness, not proposed as a task — too low-impact and too far from any real call site to
justify one right now.

## 8. Empirical benchmarks

Compiled and linked directly against this project's real, compiled `libfree-direct.a` (not a
reimplementation), run headlessly (`SDL_AUDIODRIVER=dummy`), against the repo's existing default
build (`build/`, no `CMAKE_BUILD_TYPE` set — see Section 5.2). All three benchmarks call only public
`IDirectSound`/`IDirectSoundBuffer` API entry points.

### 8.1 `Play()` on a realistic SFX-sized buffer

A 2-second, 16-bit mono, 22050Hz buffer (matching the realistic size range established in Section
4), `Play()`ed 200 times in a row:

| Call | Result |
|---|---|
| `Play()` | **0.0016 ms/call** (0.322ms total / 200 calls) |

This is the evidence behind Section 3's "no missing fast path" conclusion — negligible compared to
a 16.67ms 60fps frame budget, with nothing resembling `docs/audit_ddraw.md`'s 29x-vs-`memcpy` gap.

### 8.2 `DirectSoundCreate`+`Release` while another `IDirectSound` stays alive (pure ref-count path)

1000 create+release cycles, with a separate `IDirectSound` held open throughout so the underlying
SDL audio device never actually closes:

| Call | Result |
|---|---|
| `DirectSoundCreate` + `Release` (ref-count only) | **0.00006 ms/call** (0.058ms total / 1000 calls) |

### 8.3 `DirectSoundCreate`+`Release` as the sole owner (real device close+reopen every cycle)

200 create+release cycles, with **no** other `IDirectSound` held open, so every `Release()` actually
closes the SDL audio device (`SharedAudioDevice::release()`'s `refCount_` hits 0) and every
subsequent `DirectSoundCreate` actually reopens it:

| Call | Result |
|---|---|
| `DirectSoundCreate` + `Release` (real device close+reopen) | **51.39 ms/call** (10,278.666ms total / 200 calls) |

This is roughly **850,000x** slower than the pure ref-count path in 8.2, and costs more than 3 full
frames at 60fps *per cycle*. The exact internal cause (most likely OS/thread-level audio subsystem
setup inside SDL3 itself, not FreeDirect's own code — `SharedAudioDevice::open()`/`release()` are
each a handful of lines around `SDL_OpenAudioDevice`/`SDL_CloseAudioDevice`) was not traced further,
since it's outside this project's own source. What matters for FreeDirect callers is the
*consequence*: repeatedly creating and fully releasing the sole live `IDirectSound` is dramatically
expensive, real, and measured — not a theoretical concern. Section 4 confirms neither target game's
current code does this (each calls `DirectSoundCreate` once, `Release()` once, for the life of the
process), so this cost is paid exactly once, at startup, in both games today — but it's worth
knowing about for any future code path (a "restart audio" feature, device-hotplug handling, or test
code that loops over `DirectSoundCreate`/`Release`) before assuming that pattern is cheap.

Methodology notes: timing used `std::chrono::steady_clock`, single-threaded, all benchmarks call
only public API entry points. Full benchmark source was written to the session scratchpad during
this audit (`bench_dsound.cpp`), not committed to the repository, matching `docs/audit_ddraw.md`
§7's precedent — reproducible by any future contributor from the exact API calls shown above.

## 9. Edge cases under extreme situations

Beyond the specific findings above, this section collects the extreme-input/extreme-repetition
scenarios this audit specifically considered, including ones that were checked and found *not* to
be a problem — an honest edge-case sweep should record clean results too, not only defects:

| Scenario | Result | Evidence |
|---|---|---|
| `dwBufferBytes` near `DWORD` max | **Real risk** — unbounded `vector::resize`, concretely reachable via a corrupted `.wav` file | S1 / §6.1 |
| `nSamplesPerSec` near `DWORD` max | **Untested** — reaches SDL3 unchecked, actual consequence unverified | S7 / §7.2 |
| `nSamplesPerSec == 0` in an otherwise-valid descriptor | **Minor correctness gap** — silently discards channels/bit-depth too, not a crash | S8 / §7.3 |
| Repeated full device close+reopen (sole `IDirectSound` owner) | **Real, dramatic cost** (~51ms/cycle), confirmed not triggered by either game today | S2 / §8.3 |
| Rapid `Play()`/`Stop()` cycling | **Checked, no problem found** — `Play()` measured at 0.0016ms/call (§8.1); `Stop()` is a single `SDL_ClearAudioStream` call, similarly cheap by inspection | §8.1 |
| Many simultaneous buffers (both games allow up to `MAXSOUND` = 100) | **Untested at realistic scale** — existing tests cover 2 simultaneous buffers only; no confirmed problem, just no evidence either way at 100 | §4, §9.4 below |
| Out-of-range `Lock()` offset/size | **Memory-safe, semantically loose** — clamped/capped, never out-of-bounds, but doesn't return `DSERR_INVALIDPARAM` the way real DirectSound likely would | S6 / §6.4, §7.1 |
| Null or zero-sized `DSBUFFERDESC` | **Already handled gracefully** (pre-existing, documented) | `docs/directsound-limitations.md` |
| `Unlock()` called with mismatched/garbage pointer arguments | **No effect either way** — every parameter is ignored (`(void)`-cast) and the call always returns `DS_OK`; cannot corrupt state via bad `Unlock()` arguments since none of them are used | §7.4 |

### 9.4 Test coverage gaps this audit specifically surfaced

None of these are needed for either target game today (every scenario above that has an underlying
real risk is also confirmed not reachable by current game code), so none are blocking — listed as
concrete, cheap additions for whichever of Section 10's tasks gets picked up:

- `CreateSoundBuffer` with `dwBufferBytes` near `DWORD` max should return `DSERR_OUTOFMEMORY` (or
  similar) once bounded, not propagate an allocation exception — today this would crash the test
  binary, itself worth confirming once, deliberately, in an isolated process.
- A stress test creating all `MAXSOUND` (100) buffers simultaneously and playing several at once,
  matching the real ceiling both games allow but neither existing test approaches.
- A real device close+reopen cycle test (sole-owner `DirectSoundCreate`/`Release` in a loop) — not
  to assert a specific timing bound (too environment-dependent for CI), but to have *some*
  regression coverage that the cycle completes without error at all, now that Section 8.3 has
  established it's expensive enough to matter if it were ever hot.
- `Lock()` with a `dwOffset` at exactly `bufferBytes_` and one past it, asserting the clamp-to-zero
  behavior is intentional and stable (currently true by inspection, not locked in by a test).

## 10. Proposed tasks

Following `docs/audit_ddraw.md`'s precedent, findings worth fixing are listed here in priority
order; whether to promote these into `plan.md` as atomic `TASK-24H-XXXX` entries (matching
`TASK-24H-0151`-`0163`'s format) is left for a follow-up decision, not done automatically by this
audit.

1. **Bound `CreateSoundBuffer`'s `dwBufferBytes` before allocating** (S1, §6.1). Highest priority in
   this audit: the only finding with a concretely plausible real-world trigger (a corrupted or
   truncated `.wav` asset file), and the fix is small and self-contained — reject anything above a
   generous, finite ceiling with `DSERR_INVALIDPARAM` (or `DSERR_OUTOFMEMORY`) before constructing
   the buffer, mirroring `TASK-24H-0154`'s approach for DirectDraw's `CreateSurface`.
2. **Document the real device close/reopen cost** (S2, §8.3) in `docs/directsound-limitations.md`,
   and add a regression test that a sole-owner create/release cycle at least completes without
   error (§9.4) — no code fix needed today (nothing is broken), but the cost is large enough that a
   future contributor should not have to rediscover it by surprise.
3. **Take `mutex_` in `SharedAudioDevice::id()`** (S3, §6.2). Trivial, self-contained, closes a real
   (if currently inert) data race permanently.
4. **Add a one-line single-threaded-usage header comment** to `include/dsound.h` (S4, §6.3),
   mirroring the equivalent DirectDraw documentation task.
5. **Return `DSERR_INVALIDPARAM` for an out-of-range `Lock()` offset/size combination** instead of
   silently clamping (S6/S7's related concern, §6.4/§7.1), *or* explicitly document the current
   clamp-don't-error behavior as deliberate in `docs/directsound-limitations.md` if clamping is
   preferred to stay consistent with DirectDraw's own `ClampRect` philosophy — this is a judgment
   call worth a short discussion before picking either fix, not a clear-cut bug.
6. **Clamp/validate `nSamplesPerSec` before it reaches `SDL_CreateAudioStream`** (S7, §7.2), and add
   a test that at least confirms *some* defined, non-crashing behavior for an extreme value — even
   if the defined behavior is "falls back to the safe default," matching the existing near-zero
   handling.
7. **Fix the near-zero-`nSamplesPerSec` fallback to only replace the frequency field**, not
   channels/bit-depth too (S8, §7.3) — small, self-contained, no behavior change for any real call
   site.
8. **Add `docs/directsound-limitations.md` entries** for S6 (offset clamping) and S5/§7.4
   (`Unlock()` never invalidates the pointer) — documentation-only, no code change, closes the gap
   between this audit's findings and the project's existing honest-limitations-documentation
   practice.
9. **Add the four test-coverage gaps in Section 9.4** — opportunistic, bundle with whichever of the
   above tasks they most directly regression-guard, per this project's Testing Policy (new/fixed
   behavior ships with its test in the same task, not as a separate follow-up).
