# DirectSound Limitations

This document records deliberate simplifications, known gaps, and dead-code findings in
FreeDirect's DirectSound subset (`include/dsound.h`, `src/directsound/DirectSound.cpp`). It exists
per `CLAUDE.md`'s Documentation Policy: honest "STUB"/"PARTIAL"/"IMPLEMENTED" labeling over
silence. See `docs/audit-24h-free-direct.md` §2.2/§5 for the full call-site audit this document is
based on, and `tests/directsound_tests.cpp` for the automated regression coverage of the behavior
described here.

## PCMWAVEFORMAT vs. WAVEFORMATEX padding

`DSBUFFERDESC::lpwfxFormat` is declared as `void*` (not `LPWAVEFORMATEX`) specifically because
`DirectSoundBufferImpl`'s constructor always reads it as `const PCMWAVEFORMAT*`, never as
`WAVEFORMATEX*` (`DirectSound.cpp:11-14, 244-257`). This is a deliberate, load-bearing choice:

- `WAVEFORMAT` (the base of `PCMWAVEFORMAT`) has 2 bytes of tail padding on LP64, so
  `sizeof(WAVEFORMAT) == 16` and `PCMWAVEFORMAT::wBitsPerSample` sits at byte offset 16.
- `WAVEFORMATEX::wBitsPerSample` sits at byte offset 14 instead.
- Both real target games always populate a `PCMWAVEFORMAT`, not a `WAVEFORMATEX` with a non-zero
  `cbSize` extension. Reading it through a `WAVEFORMATEX*` would read two bytes of struct padding
  (always `0x0000`) as `wBitsPerSample`, silently producing 0-bit audio and defaulting to S16LE for
  every sound in both games.

Do not change this cast without re-verifying both games' actual `lpwfxFormat` construction first.

## SetPan: mono-only

`SetPan()` computes a real constant-power left/right gain split (`dsPanToGains`), but only ever
*applies* it when the source buffer is mono (`srcSpec_.channels == 1`). For a stereo source buffer
the pan value is stored but has no audible effect - SDL3 does not directly expose per-output-
channel gain on an `SDL_AudioStream`, and implementing that would need a per-sample mixer callback
(not currently justified by either target game, both of which only pan mono sound effects per
`docs/audit-24h-free-direct.md` §2.2). `SetPan()` never crashes or errors on any input, including
out-of-range values (silently clamped to `[DSBPAN_LEFT, DSBPAN_RIGHT]`), for both mono and stereo
sources - `tests/directsound_tests.cpp`'s `Test_SetPan_MonoSource_NeverCrashesAcrossFullRange` and
`Test_SetPan_StereoSource_StillReturnsOk` lock this in.

## SetCurrentPosition: not seekable

`SetCurrentPosition()` only updates an internal `playCursor_` field; the underlying
`SDL_AudioStream` is not seekable. The stored cursor is used to skip ahead into the buffer on the
*next* `Play()` call (`data_.data() + playCursor_`), not to seek a currently-playing stream. Both
target games only ever call this with `0` (rewind-to-start before replaying a sound effect), which
this simplified model satisfies exactly - `tests/directsound_tests.cpp`'s
`Test_SetCurrentPosition_Zero_ReturnsOk` covers the real call pattern; a separate test
(`Test_SetCurrentPosition_OutOfRange_ClampsAndPlaySucceeds`) confirms an out-of-range position is
clamped back to 0 rather than causing an out-of-bounds read on the next `Play()`.

## No looping

`DSBPLAY_LOOPING` is accepted as a bit position in `Play()`'s `dwFlags` parameter but is not
implemented - looping never occurs. This audit re-confirmed (via a fresh case-sensitive and
case-insensitive grep) that neither `../free-eggbert` nor `../planetblupi` references
`DSBPLAY_LOOPING`, or the substring "LOOPING", anywhere in their source. Per `CLAUDE.md`'s
DirectSound Policy, real looping must not be implemented speculatively without a confirmed call
site or an explicit user request naming it as an exception.

## One stream per buffer; Play() always restarts

Each `IDirectSoundBuffer` owns exactly one `SDL_AudioStream`, created lazily on the first `Play()`
call. Calling `Play()` again on a buffer that is already playing clears the stream
(`SDL_ClearAudioStream`) and re-feeds the stored PCM data from the start - there is no support for
overlapping/polyphonic playback of the same buffer instance. This matches both target games' usage
pattern (a fixed pool of `CSound`-owned buffers, one per distinct sound effect, replayed from the
start each time the effect triggers), not a general-purpose audio mixer.

## CreateSoundBuffer does not validate dwSize

Unlike DirectDraw's `CreateSurface` (which rejects a `DDSURFACEDESC` whose `dwSize` does not match
`sizeof(DDSURFACEDESC)`), `IDirectSound::CreateSoundBuffer` performs no `dwSize` check on the
incoming `DSBUFFERDESC` at all (confirmed by reading `DirectSound.cpp`'s `CreateSoundBuffer`
directly - it validates only `lplpDirectSoundBuffer`/`pUnkOuter`). Neither target game passes a
malformed `dwSize`, so this is recorded as a known asymmetry between the two subsystems, not a bug
requiring a fix - adding stricter validation here would be new behavior without a driving call
site, which `CLAUDE.md`'s scope policy requires asking about first.

## A null or zero-sized DSBUFFERDESC is accepted, not rejected

`DirectSoundBufferImpl`'s constructor treats a null `desc` pointer as a no-op
(`if (!desc) return;`), leaving a valid-but-empty buffer object (`bufferBytes_ == 0`). A real
descriptor with `dwBufferBytes == 0` behaves identically. In both cases, `Play()` on the resulting
buffer takes the `if (bufferBytes_ == 0 || data_.empty())` early-return path and safely does
nothing, returning `DS_OK` rather than an error. `tests/directsound_tests.cpp` locks in both cases
(`Test_CreateSoundBuffer_NullDescriptor_ReturnsOkWithEmptyBuffer`,
`Test_CreateSoundBuffer_ZeroSizedBuffer_ReturnsOk`).

## Missing PCM format falls back to a safe default

If `DSBUFFERDESC::lpwfxFormat` is null, `DirectSoundBufferImpl`'s constructor skips format parsing
entirely, leaving its internal `SDL_AudioSpec` all-zero. `ensureStream()` (called from `Play()`)
detects `srcSpec_.freq == 0` and substitutes a safe default (16-bit mono, 22050 Hz) rather than
passing a zeroed spec to SDL, which would fail. This is the same fallback README.md documents for
"any format SDL3 can convert from," extended to the total-absence-of-format case.
`tests/directsound_tests.cpp`'s `Test_CreateSoundBuffer_MissingFormat_FallsBackGracefully` covers
this exact path.

## Dead-code call sites (informational only)

This audit found DirectSound-shaped call sites in both target games that are compiled but
unreachable - useful context for anyone re-auditing DirectSound scope in the future, but not a
free-direct concern (dead code cannot execute, so it places no requirements on this subsystem):

- `../free-eggbert/src/soundbass.cpp` is a full parallel `IDirectSound`-based implementation of
  `CSound`, guarded by `#if _BASS && !_LEGACY`. `../free-eggbert/include/def.hpp:24` hardcodes
  `#define _BASS FALSE` with no build-time override anywhere in that project's CMake files, so this
  file compiles to an empty translation unit today. `../free-eggbert/src/sound.cpp` is the sole
  live DirectSound path.
- `src/wave.cpp` in **both** target games (`LoadWave`/`wave_ParseWaveMemory`, using
  `CreateSoundBuffer`/`Unlock`) is compiled but unreachable: its own header
  (`wave.hpp`/`wave.h`) is `#include`d nowhere except by `wave.cpp` itself, and its functions are
  never called from anywhere else in either codebase.
- `CSound::PlaySoundDS(dwSound, dwFlags)` - the one function in either game whose `Play()` call
  would forward a non-zero, caller-supplied flags word (as opposed to a literal `0`) - is declared
  and defined in both games but never called anywhere. Every reachable `Play()` call site in both
  games passes a literal `0`, which further de-risks the "no looping" limitation above: no live
  code path could pass `DSBPLAY_LOOPING` even if a caller wanted to.

## Out of scope (confirmed by audit, not implemented)

Per `CLAUDE.md`'s DirectSound Policy and the call-site audit in `docs/audit-24h-free-direct.md`
§2.2, the following are confirmed to have no call site in either target game and must not be
implemented speculatively: DirectSound capture, 3D audio, streaming (non-static) buffers, and real
per-channel accurate stereo panning for mono sources beyond the current constant-power
approximation.
