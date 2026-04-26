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
typedef struct IDirectPlay2A* LPDIRECTPLAY2;
typedef struct IDirectPlay2A* LPDIRECTPLAY2A;

#define DP_OK ((HRESULT)0L)
#define DPERR_INVALIDPARAMS ((HRESULT)0x88770005L)
#define DPERR_OUTOFMEMORY   ((HRESULT)0x8877000EL)
#define DPERR_UNSUPPORTED   ((HRESULT)0x88770032L)
#define DPERR_NOINTERFACE ((HRESULT)0x88770002L)
#define DPERR_GENERIC ((HRESULT)0x88770000L)
#define DPERR_ACTIVEPLAYERS ((HRESULT)0x88770014L)
#define DPERR_ACCESSDENIED ((HRESULT)0x88770031L)
#define DPERR_CANTADDPLAYER ((HRESULT)0x88770015L)
#define DPERR_CANTCREATEPLAYER ((HRESULT)0x88770016L)
#define DPERR_CANTCREATEGROUP ((HRESULT)0x88770017L)
#define DPERR_CANTCREATESESSION ((HRESULT)0x88770018L)
#define DPERR_CAPSNOTAVAILABLEYET ((HRESULT)0x88770019L)
#define DPERR_ALREADYINITIALIZED ((HRESULT)0x8877001AL)
#define DPERR_INVALIDFLAGS ((HRESULT)0x8877001BL)
#define DPERR_EXCEPTION ((HRESULT)0x8877001CL)
#define DPERR_INVALIDPLAYER ((HRESULT)0x8877001DL)
#define DPERR_INVALIDOBJECT ((HRESULT)0x8877001EL)
#define DPERR_NOCONNECTION ((HRESULT)0x8877001FL)
#define DPERR_NONAMESERVERFOUND ((HRESULT)0x88770020L)
#define DPERR_NOMESSAGES ((HRESULT)0x88770021L)
#define DPERR_NOSESSIONS ((HRESULT)0x88770022L)
#define DPERR_NOPLAYERS ((HRESULT)0x88770023L)
#define DPERR_TIMEOUT ((HRESULT)0x88770024L)
#define DPERR_SENDTOOBIG ((HRESULT)0x88770025L)
#define DPERR_BUSY ((HRESULT)0x88770026L)
#define DPERR_UNAVAILABLE ((HRESULT)0x88770027L)
#define DPERR_PLAYERLOST ((HRESULT)0x88770028L)
#define DPERR_USERCANCEL ((HRESULT)0x88770029L)
#define DPERR_BUFFERTOOLARGE ((HRESULT)0x8877002AL)
#define DPERR_SESSIONLOST ((HRESULT)0x8877002BL)
#define DPERR_APPNOTSTARTED ((HRESULT)0x8877002CL)
#define DPERR_CANTCREATEPROCESS ((HRESULT)0x8877002DL)
#define DPERR_UNKNOWNAPPLICATION ((HRESULT)0x8877002EL)
#define DPERR_INVALIDINTERFACE ((HRESULT)0x8877002FL)
#define DPERR_NOTLOBBIED ((HRESULT)0x88770030L)

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

#define DPENUMSESSIONS_AVAILABLE 0x00000001L

#define DPOPEN_CREATE       0x00000001L
#define DPOPEN_JOIN         0x00000002L
#define DPOPEN_OPENSESSION  0x00000002L

#define DPRECEIVE_ALL 0x00000001L

#define DPSEND_GUARANTEED 0x00000001L

#define DPESC_TIMEDOUT 0x00000001L

#define DPSESSION_KEEPALIVE   0x00000008L
#define DPSESSION_MIGRATEHOST 0x00000004L

typedef DWORD_PTR DPID, *LPDPID;

typedef struct _DPNAME {
    DWORD dwSize;
    DWORD dwFlags;
    union {
        LPSTR lpszShortName;
        LPSTR lpszShortNameA;
        LPWSTR lpwszShortName;
    };
    union {
        LPSTR lpszLongName;
        LPSTR lpszLongNameA;
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
        LPSTR lpszSessionNameA;
        LPWSTR lpwszSessionName;
    };
    union {
        LPSTR lpszPassword;
        LPSTR lpszPasswordA;
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
typedef LPDPENUMSESSIONS_CALLBACK2 LPDPENUMSESSIONSCALLBACK2;

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext);
HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext);
HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter);

#ifdef __cplusplus
inline const GUID IID_IDirectPlay2A = {0};
#else
static const GUID IID_IDirectPlay2A = {0};
#endif

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
