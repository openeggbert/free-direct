/**
 * @file DirectPlay.cpp
 * @brief Narrow DirectPlay subset reimplementation (Stubs).
 * @note Status: STUB
 */
#include "dplay.h"
#include <atomic>
#include <cstring>
#include <new>

namespace {
    bool IsEqualGuid(const GUID& a, const GUID& b) {
        return std::memcmp(&a, &b, sizeof(GUID)) == 0;
    }

    class DirectPlay2AImpl final : public IDirectPlay2A {
    public:
        DirectPlay2AImpl() : refCount_(1) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            (void)riid;
            if (!ppvObject) return DPERR_INVALIDPARAMS;
            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

        HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) override {
            (void)lpEnumSessionsDesc; (void)dwTimeout; (void)lpEnumSessionsCallback; (void)lpContext; (void)dwFlags;
            // No sessions found
            return DP_OK;
        }

        HRESULT WINAPI Open(LPDPSESSIONDESC2 lpSessionDesc, DWORD dwFlags) override {
            (void)lpSessionDesc; (void)dwFlags;
            return DP_OK;
        }

        HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) override {
            (void)lpidPlayer; (void)lpPlayerName; (void)hEvent; (void)lpData; (void)dwDataSize; (void)dwFlags;
            if (lpidPlayer) *lpidPlayer = 1;
            return DP_OK;
        }

        HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) override {
            (void)idFrom; (void)idTo; (void)dwFlags; (void)lpData; (void)dwDataSize;
            return DP_OK;
        }

        HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) override {
            (void)lpidFrom; (void)lpidTo; (void)dwFlags; (void)lpData; (void)lpdwDataSize;
            return DP_OK; // No messages
        }

        HRESULT WINAPI Close() override { return DP_OK; }

    private:
        std::atomic<ULONG> refCount_;
    };

    class DirectPlayImpl final : public IDirectPlay {
    public:
        DirectPlayImpl() : refCount_(1) {}

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            if (!ppvObject) return DPERR_INVALIDPARAMS;

            if (IsEqualGuid(riid, IID_IDirectPlay)) {
                // Same object already implements IDirectPlay: return it, not a new instance.
                AddRef();
                *ppvObject = static_cast<IDirectPlay*>(this);
                return DP_OK;
            }
            if (IsEqualGuid(riid, IID_IDirectPlay2A)) {
                // A distinct interface: hand back a freshly constructed implementation.
                // Its constructor already starts refCount_ at 1, so no extra AddRef() here.
                *ppvObject = new (std::nothrow) DirectPlay2AImpl();
                return (*ppvObject) ? DP_OK : DPERR_OUTOFMEMORY;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) delete this;
            return val;
        }

    private:
        std::atomic<ULONG> refCount_;
    };
}

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext) {
    (void)lpEnumCallback; (void)lpContext;
    return DP_OK;
}

HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext) {
    (void)lpEnumCallback; (void)lpContext;
    return DP_OK;
}

HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter) {
    (void)lpGUID;
    if (!lplpDP || pUnkOuter) return DPERR_INVALIDPARAMS;
    *lplpDP = new (std::nothrow) DirectPlayImpl();
    return (*lplpDP) ? DP_OK : DPERR_OUTOFMEMORY;
}
