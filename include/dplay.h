/**
 * @file dplay.h
 * @brief Narrow DirectPlay subset reimplementation (Stubs).
 * @note Status: STUB
 */
#ifndef FREE_DIRECT_DPLAY_H
#define FREE_DIRECT_DPLAY_H

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IDirectPlay* LPDIRECTPLAY;
typedef struct IDirectPlay2* LPDIRECTPLAY2;
typedef struct IDirectPlay2A* LPDIRECTPLAY2A;

#define DP_OK ((HRESULT)0L)
#define DPERR_INVALIDPARAMS ((HRESULT)0x88770005L)
#define DPERR_OUTOFMEMORY   ((HRESULT)0x8877000EL)
#define DPERR_UNSUPPORTED   ((HRESULT)0x88770032L)

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

#define DPENUMSESSIONS_AVAILABLE 0x00000001L

#define DPOPEN_CREATE       0x00000001L
#define DPOPEN_JOIN         0x00000002L
#define DPOPEN_OPENSESSION  0x00000002L

#define DPRECEIVE_ALL 0x00000001L

#define DPSEND_GUARANTEED 0x00000001L

#define DPSESSION_KEEPALIVE   0x00000008L
#define DPSESSION_MIGRATEHOST 0x00000004L

typedef DWORD DPID, *LPDPID;

typedef struct _DPNAME {
    DWORD dwSize;
    DWORD dwFlags;
    union {
        LPSTR lpszShortName;
        LPWSTR lpwszShortName;
    };
    union {
        LPSTR lpszLongName;
        LPWSTR lpwszLongName;
    };
} DPNAME, *LPDPNAME;

typedef struct _DPSESSIONDESC2 {
    DWORD dwSize;
    DWORD dwFlags;
    GUID  guidInstance;
    GUID  guidApplication;
    DWORD dwMaxPlayers;
    DWORD dwCurrentPlayers;
    union {
        LPSTR lpszSessionName;
        LPWSTR lpwszSessionName;
    };
    union {
        LPSTR lpszPassword;
        LPWSTR lpwszPassword;
    };
    DWORD_PTR dwReserved1;
    DWORD_PTR dwReserved2;
    DWORD dwUser1;
    DWORD dwUser2;
    DWORD dwUser3;
    DWORD dwUser4;
} DPSESSIONDESC2, *LPDPSESSIONDESC2;

typedef BOOL (CALLBACK *LPDPENUMDPCALLBACKA)(LPGUID, LPSTR, DWORD, DWORD, LPVOID);
typedef BOOL (CALLBACK *LPDPENUMDPCALLBACKW)(LPGUID, LPWSTR, DWORD, DWORD, LPVOID);

typedef BOOL (CALLBACK *LPDPENUMSESSIONS_CALLBACK2)(LPDPSESSIONDESC2, LPDWORD, DWORD, LPVOID);

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext);
HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext);
HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter);

#ifdef __cplusplus
}

class IDirectPlay2A {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;
    virtual HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) = 0;
    virtual HRESULT WINAPI Open(LPDPSESSIONDESC2 lpSessionDesc, DWORD dwFlags) = 0;
    virtual HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) = 0;
    virtual HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) = 0;
    virtual HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) = 0;
    virtual HRESULT WINAPI Close() = 0;

protected:
    virtual ~IDirectPlay2A() = default;
};

// Minimal IDirectPlay for QueryInterface to IDirectPlay2A
class IDirectPlay {
public:
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    virtual ULONG WINAPI AddRef() = 0;
    virtual ULONG WINAPI Release() = 0;

protected:
    virtual ~IDirectPlay() = default;
};

#endif

#endif // FREE_DIRECT_DPLAY_H
