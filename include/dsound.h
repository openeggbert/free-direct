/**
 * @file dsound.h
 * @brief Narrow DirectSound subset reimplementation (Stubs).
 * @note Status: STUB
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

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif

#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif

#define DSSCL_NORMAL 0x00000001L

#define DSBCAPS_CTRLFREQUENCY 0x00000020L
#define DSBCAPS_CTRLPAN       0x00000040L
#define DSBCAPS_CTRLVOLUME    0x00000080L
#define DSBCAPS_STATIC        0x00000002L

#define DSBLOCK_FROMWRITECURSOR 0x00000001L

#define DSBSTATUS_PLAYING 0x00000001L

typedef struct _DSBUFFERDESC {
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwBufferBytes;
    DWORD dwReserved;
    void* lpwfxFormat; // Actually LPWAVEFORMATEX
} DSBUFFERDESC, *LPDSBUFFERDESC;

typedef struct _WAVEFORMATEX {
    WORD  wFormatTag;
    WORD  nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD  nBlockAlign;
    WORD  wBitsPerSample;
    WORD  cbSize;
} WAVEFORMATEX, *LPWAVEFORMATEX;

typedef struct _PCMWAVEFORMAT {
    WAVEFORMAT wf;
    WORD wBitsPerSample;
} PCMWAVEFORMAT, *LPPCMWAVEFORMAT;

HRESULT WINAPI DirectSoundCreate(const GUID* lpGuid, LPDIRECTSOUND* ppDS, IUnknown* pUnkOuter);

#ifdef __cplusplus
}

class IDirectSound {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwLevel) = 0;
    virtual HRESULT WINAPI CreateSoundBuffer(const DSBUFFERDESC* lpcDSBufferDesc, LPDIRECTSOUNDBUFFER* lplpDirectSoundBuffer, IUnknown* pUnkOuter) = 0;

protected:
    virtual ~IDirectSound() = default;
};

class IDirectSoundBuffer {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual HRESULT WINAPI GetStatus(LPDWORD pdwStatus) = 0;
    virtual HRESULT WINAPI Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) = 0;
    virtual HRESULT WINAPI Stop() = 0;
    virtual HRESULT WINAPI Lock(DWORD dwOffset, DWORD dwBytes, LPVOID* ppvAudioPtr1, LPDWORD pdwAudioBytes1, LPVOID* ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) = 0;
    virtual HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) = 0;
    virtual HRESULT WINAPI SetCurrentPosition(DWORD dwNewPosition) = 0;
    virtual HRESULT WINAPI SetVolume(LONG lVolume) = 0;
    virtual HRESULT WINAPI SetPan(LONG lPan) = 0;

protected:
    virtual ~IDirectSoundBuffer() = default;
};

#endif

#endif // FREE_DIRECT_DSOUND_H
