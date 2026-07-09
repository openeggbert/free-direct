/**
 * @file dsound.h
 * @brief Narrow DirectSound subset reimplemented over SDL3 audio.
 *
 * Supported subset (current dsound.h):
 *   - DirectSoundCreate          IMPLEMENTED
 *   - IDirectSound::SetCooperativeLevel  IMPLEMENTED
 *   - IDirectSound::CreateSoundBuffer    IMPLEMENTED
 *   - IDirectSoundBuffer::Lock           IMPLEMENTED
 *   - IDirectSoundBuffer::Unlock         IMPLEMENTED
 *   - IDirectSoundBuffer::Play           PARTIAL (no looping)
 *   - IDirectSoundBuffer::Stop           IMPLEMENTED
 *   - IDirectSoundBuffer::GetStatus      IMPLEMENTED
 *   - IDirectSoundBuffer::SetCurrentPosition PARTIAL
 *   - IDirectSoundBuffer::SetVolume      IMPLEMENTED (approx dB)
 *   - IDirectSoundBuffer::SetPan         PARTIAL (stored; mono only)
 *   - IDirectSoundBuffer::Release        IMPLEMENTED
 *
 * @note Thread safety: DirectSound objects are not thread-safe beyond their own reference
 * counting (`AddRef`/`Release`, which use `std::atomic`). Every other operation - buffer content,
 * volume/pan, playback state, and so on - assumes all calls on a given object happen from a
 * single thread. This matches both target games' actual usage (`../free-eggbert`,
 * `../planetblupi` are both single-threaded Win32 message-loop programs) and is not
 * independently synchronized (docs/audit_dsound.md §6.3, S4, TASK-24H-0167) - do not call into
 * these objects concurrently from more than one thread.
 * @note Status: PARTIAL
 */
#ifndef FREE_DIRECT_DSOUND_H
#define FREE_DIRECT_DSOUND_H

#include <windows.h>
#include <mmsystem.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IDirectSound* LPDIRECTSOUND;
typedef struct IDirectSoundBuffer* LPDIRECTSOUNDBUFFER;

/**
 * @name DirectSound result codes
 * @brief HRESULT values used by the compatibility layer.
 * @note Status: IMPLEMENTED
 */
/** @{ */
#define DS_OK ((HRESULT)0L)
#define DSERR_INVALIDPARAM ((HRESULT)0x8878000AL)
#define DSERR_OUTOFMEMORY  ((HRESULT)0x8878001EL)
#define DSERR_UNSUPPORTED  ((HRESULT)0x88780032L)
#define DSERR_ALLOCATED ((HRESULT)0x88780001L)
#define DSERR_CONTROLUNAVAIL ((HRESULT)0x8878001FL)
#define DSERR_INVALIDCALL ((HRESULT)0x88780033L)
#define DSERR_GENERIC ((HRESULT)0x88780000L)
#define DSERR_PRIOLEVELNEEDED ((HRESULT)0x88780046L)
#define DSERR_BADFORMAT ((HRESULT)0x88780064L)
#define DSERR_NODRIVER ((HRESULT)0x88780078L)
#define DSERR_ALREADYINITIALIZED ((HRESULT)0x88780082L)
#define DSERR_NOAGGREGATION ((HRESULT)0x88780094L)
#define DSERR_BUFFERLOST ((HRESULT)0x88780096L)
#define DSERR_OTHERAPPHASPRIO ((HRESULT)0x887800A0L)
#define DSERR_UNINITIALIZED ((HRESULT)0x887800A5L)
/** @} */

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif

#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif

/**
 * @brief Cooperative level constant used by legacy DirectSound initialization.
 * @note Status: IMPLEMENTED
 */
#define DSSCL_NORMAL 0x00000001L

/**
 * @name DirectSound buffer flags
 * @brief Buffer capability subset accepted by this compatibility layer.
 * @note Status: IMPLEMENTED
 */
/** @{ */
#define DSBCAPS_CTRLFREQUENCY 0x00000020L
#define DSBCAPS_CTRLPAN       0x00000040L
#define DSBCAPS_CTRLVOLUME    0x00000080L
/** @brief Referenced in both free-eggbert's and planetblupi's src/wave.cpp, but that file is dead
 *  code in both games as of the 2026-07-08 audit: it is absent from both games' current
 *  CMakeLists.txt build (only listed in their legacy, unused .vcxproj files), and no function it
 *  defines (LoadWave, wave_ParseWaveMemory) is called from anywhere else in either game's source
 *  tree. Kept implemented/accepted here regardless, since it costs nothing and documents a real
 *  (if unreachable) call site. */
#define DSBCAPS_STATIC        0x00000002L
/** @} */

/**
 * @brief Lock mode used by legacy buffered writes.
 * @note Status: IMPLEMENTED
 */
#define DSBLOCK_FROMWRITECURSOR 0x00000001L

/**
 * @brief Playback status bit returned by `IDirectSoundBuffer::GetStatus`.
 * @note Status: IMPLEMENTED
 */
#define DSBSTATUS_PLAYING 0x00000001L

/**
 * @brief Legacy DirectSound buffer descriptor.
 *
 * The compatibility layer reads format and size values from this descriptor
 * to configure the SDL_AudioStream source format.
 * @note Status: IMPLEMENTED
 */
typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    void* lpwfxFormat; // Actually LPWAVEFORMATEX
} DSBUFFERDESC, *LPDSBUFFERDESC;

/**
 * @brief PCM format description used by DirectSound buffers.
 * @note Status: IMPLEMENTED
 */
typedef struct _WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;

/**
 * @brief Legacy PCM format convenience wrapper.
 * @note Status: IMPLEMENTED
 */
typedef struct _PCMWAVEFORMAT {
    WAVEFORMAT wf;
    WORD wBitsPerSample;
} PCMWAVEFORMAT, *LPPCMWAVEFORMAT;

/**
 * @brief Creates a DirectSound device object.
 *
 * Opens the SDL audio subsystem and returns a fully functional IDirectSound
 * object backed by SDL3 audio.  If no audio device is available the function
 * returns DSERR_NODRIVER gracefully.
 * @note Status: IMPLEMENTED
 */
HRESULT WINAPI DirectSoundCreate(const GUID* lpGuid, LPDIRECTSOUND* ppDS, IUnknown* pUnkOuter);

#ifdef __cplusplus
}

class IDirectSound {
public:
    /** @brief COM query method. @note Status: STUB (E_NOINTERFACE) */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @brief Increments object reference count. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @brief Decrements object reference count and frees on zero. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;
    /** @brief Accepts any cooperative level; no SDL privilege negotiation needed. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwLevel) = 0;
    /** @brief Allocates a PCM buffer and stores format for SDL_AudioStream. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI CreateSoundBuffer(const DSBUFFERDESC* lpcDSBufferDesc, LPDIRECTSOUNDBUFFER* lplpDirectSoundBuffer, IUnknown* pUnkOuter) = 0;

protected:
    virtual ~IDirectSound() = default;
};

class IDirectSoundBuffer {
public:
    /** @brief COM query method. @note Status: STUB (E_NOINTERFACE) */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @brief Increments object reference count. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @brief Decrements object reference count; frees on zero and stops SDL stream. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;
    /** @brief Returns DSBSTATUS_PLAYING when the SDL stream has queued audio. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI GetStatus(LPDWORD pdwStatus) = 0;
    /** @brief Feeds PCM data into SDL_AudioStream; looping not implemented. @note Status: PARTIAL */
    virtual HRESULT WINAPI Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) = 0;
    /** @brief Clears the SDL_AudioStream (silences output immediately). @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Stop() = 0;
    /** @brief Returns writable pointer(s) into the internal PCM buffer. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Lock(DWORD dwOffset, DWORD dwBytes, LPVOID* ppvAudioPtr1, LPDWORD pdwAudioBytes1, LPVOID* ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) = 0;
    /** @brief Marks the region written; data is used on the next Play() call. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) = 0;
    /** @brief Updates the internal play cursor; SDL stream is not seekable. @note Status: PARTIAL */
    virtual HRESULT WINAPI SetCurrentPosition(DWORD dwNewPosition) = 0;
    /** @brief Converts centibel attenuation to linear SDL gain (10^(cB/2000)). @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI SetVolume(LONG lVolume) = 0;
    /** @brief Stores pan value; constant-power pan applied for mono sources. @note Status: PARTIAL */
    virtual HRESULT WINAPI SetPan(LONG lPan) = 0;

protected:
    virtual ~IDirectSoundBuffer() = default;
};

#endif

#endif // FREE_DIRECT_DSOUND_H
