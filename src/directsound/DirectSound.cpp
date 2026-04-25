/**
 * @file DirectSound.cpp
 * @brief Narrow DirectSound subset reimplementation (Stubs).
 * @note Status: STUB
 */
#include "dsound.h"
#include <atomic>
#include <new>
#include <vector>

namespace {
    class DirectSoundBufferImpl final : public IDirectSoundBuffer {
    public:
        DirectSoundBufferImpl(const DSBUFFERDESC* desc) : refCount_(1) {
            if (desc) {
                bufferBytes_ = desc->dwBufferBytes;
                // Dummy buffer for Lock/Unlock
                data_.resize(bufferBytes_, 0);
            }
        }

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            (void)riid; (void)ppvObject; return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

        HRESULT WINAPI GetStatus(LPDWORD pdwStatus) override {
            if (pdwStatus) *pdwStatus = 0; // Not playing
            return DS_OK;
        }

        HRESULT WINAPI Play(DWORD dwReserved1, DWORD dwPriority, DWORD dwFlags) override {
            (void)dwReserved1; (void)dwPriority; (void)dwFlags;
            return DS_OK;
        }

        HRESULT WINAPI Stop() override { return DS_OK; }

        HRESULT WINAPI Lock(DWORD dwOffset, DWORD dwBytes, LPVOID* ppvAudioPtr1, LPDWORD pdwAudioBytes1,
                            LPVOID* ppvAudioPtr2, LPDWORD pdwAudioBytes2, DWORD dwFlags) override {
            (void)dwFlags;
            if (!ppvAudioPtr1 || !pdwAudioBytes1) return DSERR_INVALIDPARAM;

            // Simple linear lock
            DWORD size = dwBytes;
            if (dwOffset + dwBytes > bufferBytes_) {
                size = bufferBytes_ - dwOffset;
            }

            *ppvAudioPtr1 = data_.data() + dwOffset;
            *pdwAudioBytes1 = size;

            if (ppvAudioPtr2) *ppvAudioPtr2 = nullptr;
            if (pdwAudioBytes2) *pdwAudioBytes2 = 0;

            return DS_OK;
        }

        HRESULT WINAPI Unlock(LPVOID pvAudioPtr1, DWORD dwAudioBytes1, LPVOID pvAudioPtr2, DWORD dwAudioBytes2) override {
            (void)pvAudioPtr1; (void)dwAudioBytes1; (void)pvAudioPtr2; (void)dwAudioBytes2;
            return DS_OK;
        }

        HRESULT WINAPI SetCurrentPosition(DWORD dwNewPosition) override {
            (void)dwNewPosition;
            return DS_OK;
        }

        HRESULT WINAPI SetVolume(LONG lVolume) override {
            (void)lVolume;
            return DS_OK;
        }

        HRESULT WINAPI SetPan(LONG lPan) override {
            (void)lPan;
            return DS_OK;
        }

    private:
        std::atomic<ULONG> refCount_;
        DWORD bufferBytes_ = 0;
        std::vector<uint8_t> data_;
    };

    class DirectSoundImpl final : public IDirectSound {
    public:
        DirectSoundImpl() : refCount_(1) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            (void)riid; (void)ppvObject; return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

        HRESULT WINAPI SetCooperativeLevel(HWND hWnd, DWORD dwLevel) override {
            (void)hWnd; (void)dwLevel;
            return DS_OK;
        }

        HRESULT WINAPI CreateSoundBuffer(const DSBUFFERDESC* lpcDSBufferDesc, LPDIRECTSOUNDBUFFER* lplpDirectSoundBuffer, IUnknown* pUnkOuter) override {
            if (!lplpDirectSoundBuffer || pUnkOuter) return DSERR_INVALIDPARAM;
            *lplpDirectSoundBuffer = new (std::nothrow) DirectSoundBufferImpl(lpcDSBufferDesc);
            return (*lplpDirectSoundBuffer) ? DS_OK : DSERR_OUTOFMEMORY;
        }

    private:
        std::atomic<ULONG> refCount_;
    };
}

HRESULT WINAPI DirectSoundCreate(const GUID* lpGuid, LPDIRECTSOUND* ppDS, IUnknown* pUnkOuter) {
    (void)lpGuid;
    if (!ppDS || pUnkOuter) return DSERR_INVALIDPARAM;
    *ppDS = new (std::nothrow) DirectSoundImpl();
    return (*ppDS) ? DS_OK : DSERR_OUTOFMEMORY;
}
