/**
 * @file dplay.h
 * @brief Narrow DirectPlay subset reimplementation over a real loopback (and, partially, ENet)
 *        transport.
 *
 * Most of `IDirectPlay2A` is implemented against real session/player/message-queue state (see
 * `src/directplay/DirectPlay.cpp` and `docs/directplay-design.md`'s Decisions 1-19), not
 * unconditional dummy values. `DirectPlayEnumerateA`/`DirectPlayEnumerateW` remain genuine stubs
 * (Decision 1: decided, not yet implemented). Broadcast delivery (`Send` with `idTo == 0`) does
 * not work correctly yet - see `Send`'s own doc comment below and
 * `docs/audit-24h-free-direct.md`.
 * @note Status: PARTIAL
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

/**
 * @name DirectPlay result codes
 * @brief HRESULT values used by this compatibility subset.
 * @note Status: STUB
 */
/** @{ */
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
#define DPERR_NOAGGREGATION ((HRESULT)0x88770033L)
/** @} */

#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif

/**
 * @name Session and send flags
 * @brief Legacy DirectPlay flags required by the game build.
 * @note Status: STUB
 */
/** @{ */
#define DPENUMSESSIONS_AVAILABLE 0x00000001L

#define DPOPEN_CREATE       0x00000001L
#define DPOPEN_JOIN         0x00000002L
#define DPOPEN_OPENSESSION  0x00000002L

#define DPRECEIVE_ALL 0x00000001L

#define DPSEND_GUARANTEED 0x00000001L

#define DPESC_TIMEDOUT 0x00000001L

#define DPSESSION_KEEPALIVE   0x00000008L
#define DPSESSION_MIGRATEHOST 0x00000004L
/** @} */

/**
 * @brief DirectPlay player identifier type.
 *
 * A 4-byte `DWORD`, matching real Microsoft DirectPlay's `DPID` exactly (not
 * pointer-sized storage, despite an earlier version of this comment claiming that was
 * needed "to stay ABI-safe on both 32-bit and 64-bit hosts" - FreeDirect's own DPID
 * allocator never encodes a pointer in a DPID, so that rationale never actually
 * applied). Matching the real 4-byte width is required for `../free-eggbert`'s
 * `NetPlayer` struct, which its own `event.cpp` walks using a hardcoded 32-byte
 * pointer-arithmetic stride derived from a 4-byte `DPID` - see
 * `docs/directplay-callsite-audit.md` §5 and `docs/directplay-design.md` Decision 3.
 * @note Status: PARTIAL
 */
typedef DWORD DPID, *LPDPID;

/**
 * @brief Player/group display names used by DirectPlay APIs.
 * @note Status: STUB
 */
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

/**
 * @brief Session descriptor used for enumeration and open/join calls.
 * @note Status: STUB
 */
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

/**
 * @brief Callback signature used by `DirectPlayEnumerateA`.
 * @note Status: STUB
 */
typedef BOOL (CALLBACK *LPDPENUMDPCALLBACKA)(LPGUID, LPSTR, DWORD, DWORD, LPVOID);
/**
 * @brief Wide callback signature used by `DirectPlayEnumerateW`.
 * @note Status: STUB
 */
typedef BOOL (CALLBACK *LPDPENUMDPCALLBACKW)(LPGUID, LPWSTR, DWORD, DWORD, LPVOID);

/**
 * @brief Callback used by `IDirectPlay2A::EnumSessions`.
 * @note Status: STUB
 */
typedef BOOL (CALLBACK *LPDPENUMSESSIONS_CALLBACK2)(LPDPSESSIONDESC2, LPDWORD, DWORD, LPVOID);
typedef LPDPENUMSESSIONS_CALLBACK2 LPDPENUMSESSIONSCALLBACK2;

/**
 * @brief Enumerates available DirectPlay service providers (ANSI).
 *
 * Current behavior: returns `DP_OK` and invokes `lpEnumCallback` zero times, i.e. it always
 * reports "no service providers available." This is an intentional interim stub, not a finished
 * feature: `free-eggbert`'s `CNetwork::EnumProviders` (see `docs/directplay-callsite-audit.md`
 * and `docs/directplay-design.md`) requires at least one enumerated provider before it can ever
 * call `DirectPlayCreate`, so real provider enumeration is planned for `plan.md` Phase 8's
 * session-enumeration work.
 * @note Status: STUB
 */
HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext);
/**
 * @brief Enumerates available DirectPlay service providers (Unicode).
 *
 * Current behavior: returns `DP_OK` and invokes `lpEnumCallback` zero times. See
 * `DirectPlayEnumerateA`'s documentation for the ANSI variant, which applies identically here.
 * @note Status: STUB
 */
HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext);
/**
 * @brief Creates a DirectPlay object.
 *
 * Validates its parameters for real: a null `lplpDP` returns `DPERR_INVALIDPARAMS`, a non-null
 * `pUnkOuter` returns `DPERR_NOAGGREGATION` (COM aggregation is not supported), and an allocation
 * failure returns `DPERR_OUTOFMEMORY`. `lpGUID` is accepted but not validated against a real
 * service-provider registry (matches this project's non-Microsoft-wire-compatible scope).
 * @note Status: IMPLEMENTED
 */
HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter);

/**
 * @brief Internal identity constants for `QueryInterface`.
 *
 * These are FreeDirect-internal placeholder values, not the real Microsoft
 * DirectPlay IIDs (this project does not target Microsoft DirectPlay wire/binary
 * compatibility). They only need to be distinct from each other so
 * `QueryInterface` can tell `IDirectPlay` and `IDirectPlay2A` apart.
 * @note Status: PARTIAL
 */
/** @{ */
#ifdef __cplusplus
inline const GUID IID_IDirectPlay = {1};
inline const GUID IID_IDirectPlay2A = {0};
#else
static const GUID IID_IDirectPlay = {1};
static const GUID IID_IDirectPlay2A = {0};
#endif
/** @} */

#ifdef __cplusplus
}

class IDirectPlay2A {
public:
    /** @brief COM query method; real GUID comparison against IID_IDirectPlay/IID_IDirectPlay2A. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @brief Increments object reference count (atomic). @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @brief Decrements object reference count; deletes and defensively tears down the transport on last release. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;
    /** @brief Enumerates sessions against a real, process-wide loopback-hosted-session registry. Does not see ENet-hosted sessions and has no LAN discovery. @note Status: PARTIAL */
    virtual HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) = 0;
    /** @brief Opens or creates a session. Real for loopback (both host and join roles) and ENet hosting; ENet joining never connects yet (no host-address resolution mechanism exists). @note Status: PARTIAL */
    virtual HRESULT WINAPI Open(LPDPSESSIONDESC2 lpSessionDesc, DWORD dwFlags) = 0;
    /** @brief Creates a player endpoint with real dwMaxPlayers validation and sequential DPID allocation. Player name/data/event-handle fields are accepted but not stored. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) = 0;
    /** @brief Sends a packet. Real for self-send and host-to-one-assigned-remote-player unicast only - there is no broadcast and no host-side relay between non-host peers yet. idTo == 0 does not broadcast: it currently collides with self-send whenever the caller's own DPID is also 0 (see docs/audit-24h-free-direct.md; unresolved pending a DPID-0 semantics decision, plan.md TASK-24H-0131). @note Status: PARTIAL */
    virtual HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) = 0;
    /** @brief Receives a packet from the local message queue; also services the transport and drains connect/disconnect/join-handshake events first. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) = 0;
    /** @brief Closes the active DirectPlay session: unregisters from the EnumSessions registry, shuts down the transport, clears session/player/message state. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI Close() = 0;

protected:
    virtual ~IDirectPlay2A() = default;
};

// Minimal IDirectPlay for QueryInterface to IDirectPlay2A
class IDirectPlay {
public:
    /** @brief COM query method. @note Status: IMPLEMENTED */
    virtual HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) = 0;
    /** @brief Increments object reference count (atomic). @note Status: IMPLEMENTED */
    virtual ULONG WINAPI AddRef() = 0;
    /** @brief Decrements object reference count. @note Status: IMPLEMENTED */
    virtual ULONG WINAPI Release() = 0;

protected:
    virtual ~IDirectPlay() = default;
};

#endif

#endif // FREE_DIRECT_DPLAY_H
