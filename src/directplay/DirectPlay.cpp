/**
 * @file DirectPlay.cpp
 * @brief Narrow DirectPlay subset reimplementation over a real loopback (and, partially, ENet)
 *        transport. See include/dplay.h for accurate per-method status tags.
 * @note Status: PARTIAL
 */
#include "dplay.h"
#include "DirectPlaySession.hpp"
#include "DirectPlayWireProtocol.hpp"
#include "LoopbackDirectPlayTransport.hpp"
#ifdef FREE_DIRECT_ENABLE_ENET
#include "EnetDirectPlayTransport.hpp"
#include "DirectPlayDiscovery.hpp"
#include <SDL3/SDL.h>
#include <string>
#endif

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <random>
#include <unordered_map>
#include <vector>

namespace {
    // Deliberately std::getenv/std::fprintf, not SDL_getenv/SDL_Log: unlike DirectDraw.cpp/
    // DirectSound.cpp (both unconditionally SDL3-backed already), DirectPlay.cpp's core logic
    // (loopback transport, session state, message queue) has no SDL3 dependency outside the
    // FREE_DIRECT_ENABLE_ENET-only block - tests/directplay_tests.cpp's own documented "fast
    // iteration" build (a bare g++ command, no CMake, no -lSDL3) relies on that staying true.
    // Adding an unconditional SDL3 requirement purely for optional debug logging would silently
    // break that build path (confirmed: it does, with an undefined-reference link error, if
    // SDL_getenv/SDL_Log are used here instead) - so this logging is standard-library-only.
    bool IsEnvFlagEnabled(const char* envName)
    {
        const char* env = std::getenv(envName);
        if (!env) {
            return false;
        }

        // Manual case-insensitive ASCII compare, not strcasecmp (POSIX, not standard C++) or
        // SDL_strcasecmp (the SDL3 dependency this function exists specifically to avoid).
        auto equalsIgnoreCase = [](const char* a, const char* b) {
            while (*a && *b) {
                const char ca = (*a >= 'A' && *a <= 'Z') ? static_cast<char>(*a + 32) : *a;
                const char cb = (*b >= 'A' && *b <= 'Z') ? static_cast<char>(*b + 32) : *b;
                if (ca != cb) return false;
                ++a; ++b;
            }
            return *a == '\0' && *b == '\0';
        };
        return equalsIgnoreCase(env, "1") || equalsIgnoreCase(env, "true")
            || equalsIgnoreCase(env, "yes") || equalsIgnoreCase(env, "on");
    }

    // Mirrors DirectDraw.cpp's IsDirectDrawDebugEnabled()/DirectSound.cpp's dsDebugEnabled():
    // the FREE_DIRECT_DEBUG_DPLAY env var is the primary/default mechanism; the #ifdef is an
    // additive CMake-level force-on path (FREE_DIRECT_FORCE_DEBUG_DPLAY, TASK-24H-0183, following
    // the existing FREE_DIRECT_FORCE_DEBUG_* convention from TASK-24H-0119) for a build-time
    // override. DirectPlay previously had zero debug-logging infrastructure at all, unlike
    // DirectDraw/DirectSound (found via a 2026-07-09 maintainability audit).
    bool IsDirectPlayDebugEnabled()
    {
#ifdef FREE_DIRECT_DEBUG_DPLAY
        return true;
#else
        return IsEnvFlagEnabled("FREE_DIRECT_DEBUG_DPLAY");
#endif
    }

    void DirectPlayLog(const char* format, ...)
    {
        if (!IsDirectPlayDebugEnabled()) {
            return;
        }

        va_list args;
        va_start(args, format);
        std::vfprintf(stderr, format, args);
        std::fputc('\n', stderr);
        va_end(args);
    }

    bool IsEqualGuid(const GUID& a, const GUID& b) {
        return std::memcmp(&a, &b, sizeof(GUID)) == 0;
    }

    // Not a real UUID generator (no RFC 4122 version/variant bits) - FreeDirect's
    // DirectPlay is explicitly not wire-compatible with real DirectPlay (CLAUDE.md), so
    // there is no external protocol this needs to satisfy. Random bits in each named
    // field are enough to make session instance GUIDs "very likely unique" for
    // FreeDirect-to-FreeDirect sessions, which is all Open()'s guidInstance generation
    // actually needs. Fills fields individually rather than bulk-memcpy'ing a fixed byte
    // count, since GUID's Data1 is `unsigned long` - 8 bytes on this platform, not the 4
    // bytes real DirectPlay's Data1 documents, so sizeof(GUID) isn't portably 16.
    GUID GenerateSessionInstanceGuid() {
        static std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::uint32_t> dist32;
        std::uniform_int_distribution<std::uint16_t> dist16;
        std::uniform_int_distribution<int> distByte(0, 255);

        GUID guid{};
        guid.Data1 = dist32(rng);
        guid.Data2 = dist16(rng);
        guid.Data3 = dist16(rng);
        for (auto& byte : guid.Data4) byte = static_cast<unsigned char>(distByte(rng));
        return guid;
    }

#ifdef FREE_DIRECT_ENABLE_ENET
    // Parses FREE_DIRECT_ENET_HOST_ADDRESS (docs/directplay-design.md Decision 22): "<host>" or
    // "<host>:<port>". Returns false (leaving outHost/outPort unmodified) if the env var is
    // unset, empty, or malformed - Open() maps that to DPERR_NOSESSIONS, the same code the
    // loopback backend already uses for "nothing to connect to" (Decision 11), rather than
    // inventing a new error condition for what is, from the caller's perspective, the same kind
    // of "could not find/reach a session" outcome.
    bool ParseEnetHostAddressEnvVar(std::string& outHost, std::uint16_t& outPort) {
        const char* raw = SDL_getenv("FREE_DIRECT_ENET_HOST_ADDRESS");
        if (!raw || !*raw) return false;
        const std::string value(raw);
        const auto colonPos = value.rfind(':');
        if (colonPos == std::string::npos) {
            if (value.empty()) return false;
            outHost = value;
            outPort = free_direct_directplay::kDefaultDirectPlayEnetPort;
            return true;
        }
        const std::string host = value.substr(0, colonPos);
        const std::string portStr = value.substr(colonPos + 1);
        if (host.empty() || portStr.empty()) return false;
        // Reject anything non-numeric outright (e.g. a second colon from an unsupported IPv6
        // literal) rather than letting strtol silently parse a prefix of a garbage string.
        for (const char c : portStr) {
            if (c < '0' || c > '9') return false;
        }
        const long parsedPort = std::strtol(portStr.c_str(), nullptr, 10);
        if (parsedPort <= 0 || parsedPort > 65535) return false;
        outHost = host;
        outPort = static_cast<std::uint16_t>(parsedPort);
        return true;
    }
#endif

    // Process-wide static registry (docs/directplay-design.md Decision 18), keyed by the fixed
    // loopback port (docs/directplay-design.md Decisions 11/12) - lets EnumSessions() find a
    // currently-hosted loopback session's *live* DirectPlaySession directly and synchronously,
    // with no wire-protocol round-trip needed. Deliberately separate from
    // LoopbackDirectPlayTransport's own port registry (Decision 10): that one maps to a
    // transport instance for Connect() to reach; this one maps to DirectPlay-level session
    // state (applicationGuid, sessionName, player counts, ...) that the transport layer must
    // never know about. Loopback-only - a session hosted over ENet is not discoverable yet (real
    // Discovery/DiscoveryResponse wire packets, already defined in DirectPlayWireProtocol.hpp,
    // are a separate, later task for that backend).
    std::unordered_map<std::uint16_t, free_direct_directplay::DirectPlaySession*>&
    LoopbackHostedSessionRegistry() {
        static std::unordered_map<std::uint16_t, free_direct_directplay::DirectPlaySession*> registry;
        return registry;
    }

    void UnregisterHostedSession(free_direct_directplay::DirectPlaySession* session) {
        auto& registry = LoopbackHostedSessionRegistry();
        for (auto it = registry.begin(); it != registry.end(); ++it) {
            if (it->second == session) {
                registry.erase(it);
                return;
            }
        }
    }

    class DirectPlay2AImpl final : public IDirectPlay2A {
    public:
        DirectPlay2AImpl() : refCount_(1) { DirectPlayLog("free-direct DirectPlay2AImpl: created"); }

        HRESULT WINAPI QueryInterface(const GUID& riid, void** ppvObject) override {
            if (!ppvObject) return DPERR_INVALIDPARAMS;

            if (IsEqualGuid(riid, IID_IDirectPlay2A)) {
                AddRef();
                *ppvObject = static_cast<IDirectPlay2A*>(this);
                return DP_OK;
            }

            *ppvObject = nullptr;
            return E_NOINTERFACE;
        }

        ULONG WINAPI AddRef() override { return ++refCount_; }

        ULONG WINAPI Release() override {
            ULONG val = --refCount_;
            if (val == 0) {
                DirectPlayLog("free-direct DirectPlay2AImpl: destroying (refCount reached 0)");
                // Covers the case where Release() is called without a prior Close() - Open()
                // (Phase 4) now assigns a real LoopbackDirectPlayTransport, and Close() already
                // shuts it down and clears session_.transport itself, so this is a no-op then.
                // Same defensive reasoning for the EnumSessions() registry (Decision 18).
                UnregisterHostedSession(&session_);
                if (session_.transport) session_.transport->Shutdown();
                delete this;
            }
            return val;
        }

        HRESULT WINAPI EnumSessions(LPDPSESSIONDESC2 lpEnumSessionsDesc, DWORD dwTimeout, LPDPENUMSESSIONS_CALLBACK2 lpEnumSessionsCallback, LPVOID lpContext, DWORD dwFlags) override {
            // lpEnumSessionsDesc is optional (null means "enumerate everything"); its dwSize is
            // only validated when a filter descriptor is actually provided.
            if (lpEnumSessionsDesc && lpEnumSessionsDesc->dwSize != sizeof(DPSESSIONDESC2)) return DPERR_INVALIDPARAMS;
            if (!lpEnumSessionsCallback) return DPERR_INVALIDPARAMS;
            // The only flag bit this project declares for EnumSessions() is
            // DPENUMSESSIONS_AVAILABLE (0x1) - the sole value free-eggbert's own
            // CNetwork::EnumSessions() ever passes (Phase 11's flag-validation sweep, plan.md) -
            // matching Open()'s existing dwFlags-validation pattern. (Real DirectPlay also has
            // DPENUMSESSIONS_ALL/ASYNC/STOPASYNC/PASSWORDREQUIRED/RETURNSTATUS; none are declared
            // in include/dplay.h since no call site needs them.)
            if (dwFlags & ~static_cast<DWORD>(DPENUMSESSIONS_AVAILABLE)) return DPERR_INVALIDFLAGS;

            // Explicit-host-only discovery (docs/directplay-design.md Decision 18), asked of and
            // confirmed by the user - a synchronous, DirectPlay-level registry lookup, not a
            // wire-protocol round-trip. guidApplication is a real filter free-eggbert's own
            // CNetwork::EnumSessions() actually supplies (docs/directplay-callsite-audit.md);
            // DPENUMSESSIONS_AVAILABLE is the other real flag that call site passes.
            const GUID zeroGuid{};
            const bool hasGuidFilter =
                lpEnumSessionsDesc && !IsEqualGuid(lpEnumSessionsDesc->guidApplication, zeroGuid);
            const bool availableOnly = (dwFlags & DPENUMSESSIONS_AVAILABLE) != 0;

            for (const auto& [port, hostedSession] : LoopbackHostedSessionRegistry()) {
                (void)port;
                if (hasGuidFilter &&
                    !IsEqualGuid(hostedSession->applicationGuid, lpEnumSessionsDesc->guidApplication)) {
                    continue;
                }
                if (availableOnly && hostedSession->maxPlayers != 0 &&
                    hostedSession->currentPlayers >= hostedSession->maxPlayers) {
                    continue;
                }

                DPSESSIONDESC2 desc{};
                desc.dwSize = sizeof(DPSESSIONDESC2);
                desc.guidApplication = hostedSession->applicationGuid;
                desc.guidInstance = hostedSession->sessionInstanceGuid;
                desc.dwMaxPlayers = hostedSession->maxPlayers;
                desc.dwCurrentPlayers = hostedSession->currentPlayers;
                // Valid only for the duration of this callback invocation, matching real
                // DirectPlay's documented descriptor lifetime - never retained past it.
                desc.lpszSessionNameA = hostedSession->sessionName.empty()
                                            ? nullptr
                                            : const_cast<char*>(hostedSession->sessionName.c_str());
                // Never revealed via enumeration, matching real DirectPlay convention - a
                // password is only required at Open(DPOPEN_JOIN)/OPENSESSION) time.
                desc.lpszPasswordA = nullptr;

                if (!lpEnumSessionsCallback(&desc, &dwTimeout, dwFlags, lpContext)) return DP_OK;
            }
#ifdef FREE_DIRECT_ENABLE_ENET
            // Real LAN discovery (docs/directplay-design.md Decision 23, TASK-24H-0150) -
            // additive to the loopback registry lookup above, never a replacement (Decision 18
            // is untouched). ensureEnetInit reuses EnetDirectPlayTransport's own process-wide
            // enet_initialize()/enet_deinitialize() reference counting (its constructor/
            // destructor already do this) rather than this method needing its own - see
            // DirectPlayDiscovery.hpp's own documented precondition.
            free_direct_directplay::EnetDirectPlayTransport ensureEnetInit;
            if (ensureEnetInit.IsEnetReady()) {
                const GUID filterGuid = hasGuidFilter ? lpEnumSessionsDesc->guidApplication : zeroGuid;
                const auto discovered =
                    free_direct_directplay::DirectPlayDiscoveryService::BroadcastAndCollect(
                        filterGuid, dwTimeout);

                std::vector<GUID> seenInstanceGuids;
                for (const auto& info : discovered) {
                    if (hasGuidFilter &&
                        !IsEqualGuid(info.applicationGuid, lpEnumSessionsDesc->guidApplication)) {
                        continue;
                    }
                    if (availableOnly && info.maxPlayers != 0 && info.currentPlayers >= info.maxPlayers) {
                        continue;
                    }
                    // A single real session should only ever answer once, but dedupe by
                    // sessionInstanceGuid anyway (e.g. a reply arriving via more than one local
                    // network interface) so the callback never sees the same session twice.
                    bool alreadySeen = false;
                    for (const auto& seen : seenInstanceGuids) {
                        if (IsEqualGuid(seen, info.sessionInstanceGuid)) { alreadySeen = true; break; }
                    }
                    if (alreadySeen) continue;
                    seenInstanceGuids.push_back(info.sessionInstanceGuid);

                    DPSESSIONDESC2 desc{};
                    desc.dwSize = sizeof(DPSESSIONDESC2);
                    desc.guidApplication = info.applicationGuid;
                    desc.guidInstance = info.sessionInstanceGuid;
                    desc.dwMaxPlayers = info.maxPlayers;
                    desc.dwCurrentPlayers = info.currentPlayers;
                    desc.lpszSessionNameA = info.sessionName.empty()
                                                 ? nullptr
                                                 : const_cast<char*>(info.sessionName.c_str());
                    desc.lpszPasswordA = nullptr;
                    if (!lpEnumSessionsCallback(&desc, &dwTimeout, dwFlags, lpContext)) break;
                }
            }
#endif
            return DP_OK;
        }

        HRESULT WINAPI Open(LPDPSESSIONDESC2 lpSessionDesc, DWORD dwFlags) override {
            if (!lpSessionDesc || lpSessionDesc->dwSize != sizeof(DPSESSIONDESC2)) return DPERR_INVALIDPARAMS;
            if (dwFlags & ~static_cast<DWORD>(DPOPEN_CREATE | DPOPEN_JOIN | DPOPEN_OPENSESSION)) {
                return DPERR_INVALIDFLAGS;
            }
            // Only "already open" is rejected here; re-Open() after a real Close() is allowed
            // to proceed, matching this task's specific "already-open object" wording.
            if (session_.IsOpen()) return DPERR_ALREADYINITIALIZED;

            DirectPlayLog("free-direct Open: dwFlags=0x%08lx role=%s", static_cast<unsigned long>(dwFlags),
                          (dwFlags & DPOPEN_CREATE) ? "host" : "join");
            session_.isHost = (dwFlags & DPOPEN_CREATE) != 0;
            session_.applicationGuid = lpSessionDesc->guidApplication;
            // Only generate when hosting and the caller didn't already supply one -
            // DPOPEN_JOIN/DPOPEN_OPENSESSION callers already know the target session's
            // real instance GUID (from EnumSessions, plan.md Phase 8) and must not have
            // it silently replaced. Written back into the caller's struct, matching real
            // DirectPlay's Open() behavior for a caller-omitted instance GUID.
            if (session_.isHost && IsEqualGuid(lpSessionDesc->guidInstance, GUID{})) {
                lpSessionDesc->guidInstance = GenerateSessionInstanceGuid();
            }
            session_.sessionInstanceGuid = lpSessionDesc->guidInstance;
            session_.maxPlayers = lpSessionDesc->dwMaxPlayers;
            session_.currentPlayers = lpSessionDesc->dwCurrentPlayers;
            session_.sessionName.clear();
            if (lpSessionDesc->lpszSessionNameA) session_.sessionName = lpSessionDesc->lpszSessionNameA;
            session_.password.clear();
            if (lpSessionDesc->lpszPasswordA) session_.password = lpSessionDesc->lpszPasswordA;
            // Backend selection is build-time-only (docs/directplay-design.md Decision 4), driven
            // directly by FREE_DIRECT_ENABLE_ENET - no run-time switch, no new API parameter.
#ifdef FREE_DIRECT_ENABLE_ENET
            session_.transport = std::make_unique<free_direct_directplay::EnetDirectPlayTransport>();
            if (session_.isHost) {
                // Fixed default port (docs/directplay-design.md Decision 5) - DPSESSIONDESC2
                // has no port-like field to derive one from.
                if (!session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayEnetPort)) {
                    session_.transport.reset();
                    return DPERR_CANTCREATESESSION;
                }
                // LAN discovery (docs/directplay-design.md Decision 23, TASK-24H-0150) is
                // additive and best-effort: a failure to bind the discovery port (e.g. another
                // FreeDirect process's listener already holds it) does not fail Open() - the
                // session still hosts normally over the main ENet port and is still reachable
                // via FREE_DIRECT_ENET_HOST_ADDRESS (Decision 22), it just is not LAN-discoverable.
                discoveryService_.StartListening();
            } else {
                // DPOPEN_JOIN/DPOPEN_OPENSESSION over ENet (docs/directplay-design.md Decision
                // 22): the host address is read from FREE_DIRECT_ENET_HOST_ADDRESS
                // ("<host>" or "<host>:<port>") since DPSESSIONDESC2 has no address-like field
                // (the same gap Decision 5 already found for the hosting port). A missing or
                // malformed env var, or a failed Connect(), both map to DPERR_NOSESSIONS - the
                // existing "could not find/reach a session" code the loopback path already uses
                // for the equivalent case (Decision 11), not a new error condition.
                std::string host;
                std::uint16_t port = 0;
                if (!ParseEnetHostAddressEnvVar(host, port) ||
                    !session_.transport->Connect(host.c_str(), port)) {
                    session_.transport.reset();
                    return DPERR_NOSESSIONS;
                }
                // Send a join-request packet to the host (Decision 16), fire-and-forget - the
                // exact same shape and rationale as the loopback path's own join packet below.
                free_direct_directplay::DirectPlayWirePacketHeader joinHeader;
                joinHeader.type = free_direct_directplay::DirectPlayWirePacketType::Join;
                joinHeader.applicationGuid = session_.applicationGuid;
                joinHeader.sessionGuid = session_.sessionInstanceGuid;
                std::vector<std::uint8_t> joinBytes;
                free_direct_directplay::SerializeDirectPlayWireHeader(joinHeader, joinBytes);
                session_.transport->Send(0, joinBytes.data(), joinBytes.size(), true);
            }
#else
            session_.transport = std::make_unique<free_direct_directplay::LoopbackDirectPlayTransport>();
            // Fixed default port (docs/directplay-design.md Decisions 11/12) - DPSESSIONDESC2
            // has no port-like field to derive one from. Self-send no longer routes through
            // the transport (Decision 12), so it is now safe for the hosting role to call
            // Listen() here without breaking it, unlike when this was first attempted (see
            // Decision 11's regression note).
            if (session_.isHost) {
                if (!session_.transport->Listen(free_direct_directplay::kDefaultDirectPlayLoopbackPort)) {
                    session_.transport.reset();
                    return DPERR_CANTCREATESESSION;
                }
                // Makes this session discoverable via EnumSessions() (docs/directplay-design.md
                // Decision 18) - registered by live pointer, not a snapshot, so the reported
                // dwCurrentPlayers/etc. stay accurate as the session changes after this point.
                LoopbackHostedSessionRegistry()[free_direct_directplay::kDefaultDirectPlayLoopbackPort] =
                    &session_;
            } else {
                // DPOPEN_JOIN/DPOPEN_OPENSESSION: Connect() resolves the host synchronously via
                // the fixed port's registry entry and fails immediately - no timeout needed -
                // when nothing is listening there, which maps directly to DPERR_NOSESSIONS.
                if (!session_.transport->Connect(nullptr, free_direct_directplay::kDefaultDirectPlayLoopbackPort)) {
                    session_.transport.reset();
                    return DPERR_NOSESSIONS;
                }
                // Send a join-request packet to the host (docs/directplay-design.md Decision 16),
                // fire-and-forget - no synchronous wait for a reply here (confirmed with the
                // user: matches the polling model every other event in this codebase already
                // uses, Decision 6, rather than real DirectPlay's blocking Open()). This session
                // has no DPID yet - that is exactly what the host's eventual join-accepted
                // response provides, processed by Receive()'s drain loop below - so idFrom/idTo
                // are left at their defaults (unused/meaningless for this packet type) rather than
                // routed through the validated public Send() path.
                free_direct_directplay::DirectPlayWirePacketHeader joinHeader;
                joinHeader.type = free_direct_directplay::DirectPlayWirePacketType::Join;
                joinHeader.applicationGuid = session_.applicationGuid;
                joinHeader.sessionGuid = session_.sessionInstanceGuid;
                std::vector<std::uint8_t> joinBytes;
                free_direct_directplay::SerializeDirectPlayWireHeader(joinHeader, joinBytes);
                session_.transport->Send(0, joinBytes.data(), joinBytes.size(), true);
            }
#endif
            session_.state = free_direct_directplay::DirectPlayObjectState::Open;
            DirectPlayLog("free-direct Open: success, role=%s", session_.isHost ? "host" : "join");
            return DP_OK;
        }

        HRESULT WINAPI CreatePlayer(LPDPID lpidPlayer, LPDPNAME lpPlayerName, HANDLE hEvent, LPVOID lpData, DWORD dwDataSize, DWORD dwFlags) override {
            (void)hEvent; (void)lpData; (void)dwDataSize;
            // `include/dplay.h` declares no DPPLAYER_* flags at all - CreatePlayer's only
            // observed call shape, both free-eggbert call sites (src/network.cpp:184,232), is a
            // literal `0` (Phase 11's flag-validation sweep, plan.md). Any nonzero value is
            // therefore unconditionally out of scope, matching Open()'s existing
            // dwFlags-validation pattern below.
            if (dwFlags != 0) return DPERR_INVALIDFLAGS;
            // lpPlayerName is optional (a player may be created without a display name); only
            // its size is validated when one is actually provided.
            if (lpPlayerName && lpPlayerName->dwSize != sizeof(DPNAME)) return DPERR_INVALIDPARAMS;

            // dwMaxPlayers == 0 means "no limit" (docs/directplay-design.md Decision 9's
            // existing convention, reused here) - a local CreatePlayer() call counts against
            // the same cap as remote assignment (Receive()'s assignment loop), since
            // dwMaxPlayers bounds the session's total player count, not just remote ones.
            if (session_.maxPlayers != 0 && session_.currentPlayers >= session_.maxPlayers) {
                DirectPlayLog("free-direct CreatePlayer: rejected, currentPlayers=%lu maxPlayers=%lu",
                              static_cast<unsigned long>(session_.currentPlayers),
                              static_cast<unsigned long>(session_.maxPlayers));
                return DPERR_CANTCREATEPLAYER;
            }

            // Sequential allocator starting at 0 (docs/directplay-design.md Decision 3) -
            // the host's own first local player gets DPID 0, matching free-eggbert's own
            // comparison pattern, not real DirectPlay's DPID_SYSMSG/DPID_ALLPLAYERS
            // reservation convention.
            const DPID newId = session_.nextPlayerId++;
            session_.localPlayerIds.push_back(newId);
            session_.currentPlayers++;
            if (lpidPlayer) *lpidPlayer = newId;
            DirectPlayLog("free-direct CreatePlayer: assigned local DPID=%lu", static_cast<unsigned long>(newId));
            return DP_OK;
        }

        HRESULT WINAPI Send(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) override {
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // The only flag bit this project declares for Send() is DPSEND_GUARANTEED (0x1);
            // free-eggbert's own call site normalizes its own dwFlags to `!!dwFlags` before
            // passing it through (src/network.cpp:254), so 0 and DPSEND_GUARANTEED are the only
            // two values ever observed (Phase 11's flag-validation sweep, plan.md) - matching
            // Open()'s existing dwFlags-validation pattern below.
            if (dwFlags & ~static_cast<DWORD>(DPSEND_GUARANTEED)) return DPERR_INVALIDFLAGS;
            // A null payload with zero length is a valid no-payload send (plan.md Phase 10); a
            // null payload with nonzero length has no bytes to actually read and was previously
            // unchecked here (TASK-24H-0106's null-pointer sweep) - both self-send's and the
            // unicast path's `bytes + dwDataSize` pointer arithmetic below are undefined behavior
            // on a null `lpData` when `dwDataSize > 0`, a real crash risk, not a hypothetical one.
            if (!lpData && dwDataSize > 0) return DPERR_INVALIDPARAMS;

            // Broadcast (docs/directplay-design.md Decision 20), checked before the
            // idTo == idFrom self-send branch below so it takes priority even for the one case
            // where they could otherwise both match: the host's own DPID is always
            // DPID_ALLPLAYERS's value (0, Decision 3), so Send(0, 0, ...) - free-eggbert's real
            // call shape for a hosting process - must mean broadcast, not self-send, even though
            // idFrom == idTo == 0 would otherwise satisfy the self-send check too.
            if (idTo == DPID_ALLPLAYERS) {
                return SendBroadcast(idFrom, dwFlags, lpData, dwDataSize);
            }
            if (idTo == idFrom) {
                return SendSelf(idFrom, dwFlags, lpData, dwDataSize);
            }
            return SendUnicast(idFrom, idTo, dwFlags, lpData, dwDataSize);
        }

        HRESULT WINAPI Receive(LPDPID lpidFrom, LPDPID lpidTo, DWORD dwFlags, LPVOID lpData, LPDWORD lpdwDataSize) override {
            if (!session_.IsOpen()) return DPERR_NOCONNECTION;
            // The only flag bit this project declares for Receive() is DPRECEIVE_ALL (0x1) -
            // the sole value free-eggbert's own CNetwork::Receive() ever passes (Phase 11's
            // flag-validation sweep, plan.md) - matching Open()'s existing dwFlags-validation
            // pattern. (Real DirectPlay also has DPRECEIVE_PEEK/DPRECEIVE_TOPLAYER/
            // DPRECEIVE_FROMPLAYER; none are declared in include/dplay.h since no call site
            // needs them, so any nonzero bit outside DPRECEIVE_ALL is unconditionally invalid
            // here regardless.)
            if (dwFlags & ~static_cast<DWORD>(DPRECEIVE_ALL)) return DPERR_INVALIDFLAGS;
            // Piggyback event servicing on the caller's own polling pattern rather than
            // adding a new API or a background thread (docs/directplay-design.md
            // Decision 6) - free-eggbert's own CNetwork::Receive() is already called
            // repeatedly from the game's loop, so this is the one call site every real
            // Receive() path already goes through. LoopbackDirectPlayTransport's
            // Service() is a no-op; EnetDirectPlayTransport's drains pending ENet events
            // (peer connect/disconnect bookkeeping) non-blockingly.
            if (session_.transport) session_.transport->Service();
#ifdef FREE_DIRECT_ENABLE_ENET
            // LAN discovery request/response servicing (docs/directplay-design.md Decision 23,
            // TASK-24H-0150) - same piggyback-on-Receive() pattern as session_.transport->Service()
            // just above (Decision 6), non-blocking, a no-op if StartListening() was never
            // called or failed (Open()'s own best-effort handling).
            if (session_.isHost) {
                free_direct_directplay::DiscoveredSessionInfo info;
                info.applicationGuid = session_.applicationGuid;
                info.sessionInstanceGuid = session_.sessionInstanceGuid;
                info.maxPlayers = session_.maxPlayers;
                info.currentPlayers = session_.currentPlayers;
                info.sessionName = session_.sessionName;
                discoveryService_.RespondToPendingRequests(info);
            }
#endif
            // Process departures before admitting new arrivals (docs/directplay-design.md
            // Decision 8): remove each disconnected peer's DPID from remotePlayerIds and
            // decrement currentPlayers. Only an already-assigned peer's disconnect is ever
            // reported here - one that disconnects before being assigned was never added
            // to remotePlayerIds/currentPlayers in the first place, so there is nothing to
            // reconcile for it.
            if (session_.transport && session_.isHost) {
                DPID disconnectedId = 0;
                while (session_.transport->HasDisconnectedPeer() &&
                       session_.transport->TakeDisconnectedPeer(&disconnectedId)) {
                    auto it = std::find(session_.remotePlayerIds.begin(),
                                        session_.remotePlayerIds.end(), disconnectedId);
                    if (it != session_.remotePlayerIds.end()) {
                        session_.remotePlayerIds.erase(it);
                        if (session_.currentPlayers > 0) session_.currentPlayers--;
                    }
                }
            }
            // Assign a real DPID to each pending incoming connection, up to dwMaxPlayers
            // (docs/directplay-design.md Decision 7) - the transport only reports "a peer
            // connected but has no DPID yet"; allocation policy (which counter, the cap,
            // player-list bookkeeping) stays here, not in the transport.
            if (session_.transport && session_.isHost) {
                // dwMaxPlayers == 0 means "no limit" (real DirectPlay's documented
                // convention) - free-eggbert never actually relies on this (it always
                // sets a concrete MAXNETPLAYER), but nothing else validates dwMaxPlayers
                // anywhere in this codebase yet, so this is the one place that gives the
                // field its first real meaning; getting the well-known zero case wrong
                // here would be a landmine for later, not a deliberate scope decision.
                while ((session_.maxPlayers == 0 || session_.currentPlayers < session_.maxPlayers) &&
                       session_.transport->HasPendingConnection()) {
                    const DPID newId = session_.nextPlayerId++;
                    if (!session_.transport->AssignPendingConnection(newId)) break;
                    session_.remotePlayerIds.push_back(newId);
                    session_.currentPlayers++;

                    // Send a join-accepted packet to the newly-assigned peer
                    // (docs/directplay-design.md Decision 16), addressed by the DPID it was
                    // just assigned - connectedPeers_[newId] now exists on the transport, so
                    // Send() can reach it directly (Decision 14). Fire-and-forget, same as the
                    // join-request this answers; no observable error path if it fails.
                    free_direct_directplay::DirectPlayWirePacketHeader acceptHeader;
                    acceptHeader.type = free_direct_directplay::DirectPlayWirePacketType::JoinAccept;
                    acceptHeader.applicationGuid = session_.applicationGuid;
                    acceptHeader.sessionGuid = session_.sessionInstanceGuid;
                    acceptHeader.idTo = newId;
                    std::vector<std::uint8_t> acceptBytes;
                    free_direct_directplay::SerializeDirectPlayWireHeader(acceptHeader, acceptBytes);
                    session_.transport->Send(newId, acceptBytes.data(), acceptBytes.size(), true);
                }
                // Session is full for real (dwMaxPlayers != 0 - "0" never rejects
                // anything, matching the "no limit" convention above): any connection
                // still pending at this point cannot be assigned, so reject it outright
                // rather than leaving it connected-but-unassigned forever
                // (docs/directplay-design.md Decision 9).
                if (session_.maxPlayers != 0) {
                    while (session_.currentPlayers >= session_.maxPlayers &&
                           session_.transport->RejectPendingConnection()) {
                    }
                }
            }
            DrainWirePackets();
            // The buffer-size-query/DPERR_NOMESSAGES/too-small/successful-copy logic lives on
            // DirectPlayMessageQueue itself (DirectPlayMessageQueue.hpp's TryReceive), so it can
            // be exercised directly by tests/directplay_tests.cpp without needing a way to
            // inject a message into a live IDirectPlay2A object.
            const HRESULT hr = session_.messageQueue.TryReceive(lpidFrom, lpidTo, lpData, lpdwDataSize);
            // Client-side rejection/disconnection observability (docs/directplay-design.md
            // Decision 13): a joining session whose host connection is gone (rejected over
            // dwMaxPlayers, or the host shut down) reports DPERR_NOCONNECTION - but only once
            // there is truly nothing left to deliver. A locally-queued message (e.g. an earlier
            // self-send, Decision 12) is still handed back first: losing the host connection
            // must not erase messages that never depended on it.
            if (session_.transport && !session_.isHost && hr == DPERR_NOMESSAGES &&
                !session_.transport->IsConnectedToHost()) {
                return DPERR_NOCONNECTION;
            }
            return hr;
        }

        HRESULT WINAPI Close() override {
            DirectPlayLog("free-direct Close: role=%s currentPlayers=%lu",
                          session_.isHost ? "host" : "join",
                          static_cast<unsigned long>(session_.currentPlayers));
            // Makes this session stop being discoverable via EnumSessions() (docs/
            // directplay-design.md Decision 18) - a no-op if it was never registered (joining
            // role, or hosting under FREE_DIRECT_ENABLE_ENET).
            UnregisterHostedSession(&session_);
            if (session_.transport) {
                session_.transport->Shutdown();
                session_.transport.reset();
            }
#ifdef FREE_DIRECT_ENABLE_ENET
            // A no-op if StartListening() was never called (joining role) or failed
            // (Open()'s best-effort handling, Decision 23).
            discoveryService_.StopListening();
#endif
            session_.messageQueue.Clear();
            session_.localPlayerIds.clear();
            session_.remotePlayerIds.clear();
            session_.nextPlayerId = 0;
            session_.sessionName.clear();
            session_.password.clear();
            session_.applicationGuid = GUID{};
            session_.sessionInstanceGuid = GUID{};
            session_.maxPlayers = 0;
            session_.currentPlayers = 0;
            session_.isHost = false;
            session_.state = free_direct_directplay::DirectPlayObjectState::Closed;
            return DP_OK;
        }

    private:
        // ===== Send() delivery paths, extracted from Send() itself (TASK-24H-0184
        // extract-method refactor - a behavior-preserving restructure, user-approved via
        // AskUserQuestion as a scoped exception to this project's standing "no broad refactor"
        // default, plan.md). Each method's body and comments are unchanged from Send()'s own
        // original inline code - only the split into separate methods and each one's parameter
        // list (only the arguments that specific path actually needs) are new. =====

        HRESULT SendBroadcast(DPID idFrom, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) {
            DirectPlayLog("free-direct Send: broadcast idFrom=%lu dwDataSize=%lu",
                          static_cast<unsigned long>(idFrom), static_cast<unsigned long>(dwDataSize));
            if (std::find(session_.localPlayerIds.begin(), session_.localPlayerIds.end(), idFrom) ==
                session_.localPlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            if (dwDataSize > free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes) {
                return DPERR_SENDTOOBIG;
            }
            if (!session_.transport) return DPERR_INVALIDPLAYER;

            free_direct_directplay::DirectPlayWirePacketHeader header;
            header.applicationGuid = session_.applicationGuid;
            header.sessionGuid = session_.sessionInstanceGuid;
            header.idFrom = idFrom;
            header.idTo = DPID_ALLPLAYERS;
            header.payloadLength = dwDataSize;
            std::vector<std::uint8_t> wireBytes;
            free_direct_directplay::SerializeDirectPlayWireHeader(header, wireBytes);
            const auto* payloadBytes = static_cast<const std::uint8_t*>(lpData);
            wireBytes.insert(wireBytes.end(), payloadBytes, payloadBytes + dwDataSize);
            const bool reliable = (dwFlags & DPSEND_GUARANTEED) != 0;

            if (session_.isHost) {
                // Deliver directly to every remote player already known (Decision 14's
                // existing per-DPID addressing) - never looped back to the host's own
                // queue, since the host is the sender here and broadcast never reaches its
                // own sender (Decision 20).
                for (const DPID remoteId : session_.remotePlayerIds) {
                    session_.transport->Send(remoteId, wireBytes.data(), wireBytes.size(), reliable);
                }
                return DP_OK;
            }
            // Joining role: exactly one connection exists (the host) - targetId is accepted
            // but ignored by the transport for this role (Decision 14), so DPID_ALLPLAYERS
            // here is only a placeholder argument. Leaving the wire header's own idTo at
            // DPID_ALLPLAYERS (not resolved to any specific address) is what tells the
            // host's own Receive() drain loop this packet needs relaying to every other
            // connected peer (Decision 21), not just local delivery.
            if (!session_.transport->Send(DPID_ALLPLAYERS, wireBytes.data(), wireBytes.size(), reliable)) {
                return DPERR_GENERIC;
            }
            return DP_OK;
        }

        HRESULT SendSelf(DPID id, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) {
            DirectPlayLog("free-direct Send: self-send id=%lu dwDataSize=%lu",
                          static_cast<unsigned long>(id), static_cast<unsigned long>(dwDataSize));
            // Validated against localPlayerIds (docs/directplay-design.md Decision 16) - this
            // was previously unchecked, an inconsistency with the unicast path below now that
            // one exists. Matters concretely for a joining session: its localPlayerIds only
            // contains a real entry once a join-accepted packet has been processed (Decision
            // 16), so this doubles as an observable proof that adoption actually happened,
            // not just an abstract correctness nicety.
            if (std::find(session_.localPlayerIds.begin(), session_.localPlayerIds.end(), id) ==
                session_.localPlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            // Validated against kMaxPayloadBytes *before* reading lpData below, matching the
            // broadcast/unicast paths' existing pre-checks (docs/audit_dplay.md §6.5, D2,
            // TASK-24H-0172) - this branch used to reach packet.payload.assign() first and
            // only discover an oversized payload afterward, via Enqueue()'s own check. If
            // dwDataSize ever overstated the caller's real buffer size, that .assign() call
            // would already have read out of bounds before the eventual DPERR_SENDTOOBIG
            // rejection could stop it.
            if (dwDataSize > free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes) {
                return DPERR_SENDTOOBIG;
            }
            // Enqueued directly into session_.messageQueue rather than round-tripping
            // through session_.transport (Phase 4's original approach, changed here per
            // docs/directplay-design.md Decision 12): sending a message to yourself is
            // always a purely local operation, regardless of whether this session's
            // transport is idle, hosting (Listen()ing), or joined - it must not depend on,
            // or be affected by, the transport's connection state. This also sidesteps
            // Decision 10's deliberate `false` return from a connected transport's
            // Send()/Receive() (there is no ambiguity to avoid here: idFrom/idTo are known
            // at this layer, never passed down to the transport, which is exactly why the
            // transport itself could never distinguish "self-send" from any other traffic).
            const auto* bytes = static_cast<const std::uint8_t*>(lpData);
            free_direct_directplay::DirectPlayMessagePacket packet;
            packet.idFrom = id;
            packet.idTo = id;
            packet.flags = dwFlags;
            packet.payload.assign(bytes, bytes + dwDataSize);
            if (!session_.messageQueue.Enqueue(std::move(packet))) return DPERR_SENDTOOBIG;
            return DP_OK;
        }

        // Real unicast-to-a-specific-remote-player delivery (docs/directplay-design.md
        // Decision 15), host role only for now: idFrom must be a locally-registered
        // player, and idTo must be a remote player this host has actually assigned a DPID
        // to (Decision 7/9's assignment loop) - anything else is DPERR_INVALIDPLAYER,
        // matching plan.md Phase 10's own validation tasks rather than the old silent
        // no-op. The joining role cannot yet address a specific remote player at all - it
        // has no way to learn any remote DPID (including the host's own) before the
        // join-accepted handshake exists (blocked Phase 7 tasks) - so it always gets
        // DPERR_INVALIDPLAYER here too, an honest "not supported yet."
        HRESULT SendUnicast(DPID idFrom, DPID idTo, DWORD dwFlags, LPVOID lpData, DWORD dwDataSize) {
            DirectPlayLog("free-direct Send: unicast idFrom=%lu idTo=%lu dwDataSize=%lu",
                          static_cast<unsigned long>(idFrom), static_cast<unsigned long>(idTo),
                          static_cast<unsigned long>(dwDataSize));
            if (std::find(session_.localPlayerIds.begin(), session_.localPlayerIds.end(), idFrom) ==
                session_.localPlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            if (!session_.isHost || !session_.transport) return DPERR_INVALIDPLAYER;
            if (std::find(session_.remotePlayerIds.begin(), session_.remotePlayerIds.end(), idTo) ==
                session_.remotePlayerIds.end()) {
                return DPERR_INVALIDPLAYER;
            }
            // Enforced here, before ever reaching the transport, not just as a Receive()-side
            // nicety: an oversized packet that made it onto the wire would arrive larger than
            // Receive()'s fixed-size read buffer (see Receive(), below) and get stuck at the
            // front of the receiver's queue forever, wedging every message behind it too.
            if (dwDataSize > free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes) {
                return DPERR_SENDTOOBIG;
            }

            free_direct_directplay::DirectPlayWirePacketHeader header;
            header.applicationGuid = session_.applicationGuid;
            header.sessionGuid = session_.sessionInstanceGuid;
            header.idFrom = idFrom;
            header.idTo = idTo;
            header.payloadLength = dwDataSize;

            std::vector<std::uint8_t> wireBytes;
            free_direct_directplay::SerializeDirectPlayWireHeader(header, wireBytes);
            const auto* payloadBytes = static_cast<const std::uint8_t*>(lpData);
            wireBytes.insert(wireBytes.end(), payloadBytes, payloadBytes + dwDataSize);

            const bool reliable = (dwFlags & DPSEND_GUARANTEED) != 0;
            if (!session_.transport->Send(idTo, wireBytes.data(), wireBytes.size(), reliable)) {
                return DPERR_GENERIC;
            }
            return DP_OK;
        }

        // ===== Receive()'s per-packet-type dispatch + drain loop, extracted from Receive()
        // itself (TASK-24H-0184, same rationale as the Send() split above). Bodies and comments
        // unchanged from Receive()'s own original inline code. =====

        void HandleDataPacket(const free_direct_directplay::DirectPlayWirePacketHeader& header,
                               const std::vector<std::uint8_t>& wireBuf, std::size_t receivedSize) {
            DirectPlayLog("free-direct Receive: Data idFrom=%lu idTo=%lu payloadLength=%lu",
                          static_cast<unsigned long>(header.idFrom),
                          static_cast<unsigned long>(header.idTo),
                          static_cast<unsigned long>(header.payloadLength));
            // A full messageQueue silently drops the packet (Enqueue()'s
            // existing bounded-growth contract, unchanged) - there is no
            // send-side acknowledgement/backpressure to report the drop to yet.
            free_direct_directplay::DirectPlayMessagePacket packet;
            packet.idFrom = header.idFrom;
            packet.idTo = header.idTo;
            packet.payload.assign(
                wireBuf.begin() + free_direct_directplay::kDirectPlayWireHeaderSize,
                wireBuf.begin() + receivedSize);
            session_.messageQueue.Enqueue(std::move(packet));

            // Host-side broadcast relay (docs/directplay-design.md Decision 21):
            // a broadcast arriving from one connected peer (idTo ==
            // DPID_ALLPLAYERS) is re-sent, byte-for-byte unchanged, to every
            // OTHER connected peer - never back to the original sender
            // (header->idFrom). The host's own copy was already enqueued just
            // above - the host is itself a legitimate broadcast recipient when a
            // non-host peer is the sender, distinct from Decision 20's "broadcast
            // never reaches its own sender" rule, which is about the sender, not
            // the host acting as relay/recipient. A non-host peer's Receive()
            // never reaches this branch's effects (session_.isHost is false
            // there), so it just enqueues like any other Data packet, unchanged
            // from before this decision.
            if (session_.isHost && header.idTo == DPID_ALLPLAYERS) {
                for (const DPID remoteId : session_.remotePlayerIds) {
                    if (remoteId == header.idFrom) continue;
                    session_.transport->Send(remoteId, wireBuf.data(), receivedSize,
                                              /*reliable=*/true);
                }
            }
        }

        void HandleJoinAcceptPacket(const free_direct_directplay::DirectPlayWirePacketHeader& header) {
            DirectPlayLog("free-direct Receive: JoinAccept assignedId=%lu",
                          static_cast<unsigned long>(header.idTo));
            // Adopts the host-assigned DPID as this session's own local player
            // identity (docs/directplay-design.md Decision 16) - only meaningful
            // for a joining role that hasn't already processed this. Not
            // enqueued into messageQueue - this is session control state, not a
            // user-visible Data message.
            if (!session_.isHost) {
                session_.applicationGuid = header.applicationGuid;
                session_.sessionInstanceGuid = header.sessionGuid;
                const DPID assignedId = header.idTo;
                if (std::find(session_.localPlayerIds.begin(),
                              session_.localPlayerIds.end(),
                              assignedId) == session_.localPlayerIds.end()) {
                    session_.localPlayerIds.push_back(assignedId);
                }
                if (session_.nextPlayerId <= assignedId) {
                    session_.nextPlayerId = assignedId + 1;
                }
            }
        }

        // Drain every real transport-delivered wire packet (docs/directplay-design.md
        // Decision 15/16), for both roles - a host receiving from one of its
        // remotePlayerIds (or a join-request), or a joining session receiving from the host
        // it Connect()ed to (a join-accepted response, or ordinary data). Each blob handed
        // back by transport->Receive() is exactly one Send()-call's worth
        // (LoopbackDirectPlayTransport's buffered_ never coalesces or splits), so one wire
        // header + payload is parsed per iteration. A blob that fails to deserialize (too
        // small, or a payloadLength that disagrees with what actually arrived) is dropped
        // silently rather than crashing or corrupting the queue - the same defensive posture
        // TryDeserializeDirectPlayWireHeader was built for in Phase 5.
        void DrainWirePackets() {
            if (!session_.transport) return;

            constexpr std::size_t kMaxWireBufferSize =
                free_direct_directplay::kDirectPlayWireHeaderSize +
                free_direct_directplay::DirectPlayMessageQueue::kMaxPayloadBytes;
            // wireBuf_ is a persistent member (TASK-24H-0179), resized once and reused across
            // calls instead of allocating fresh every time - a no-op after the first call.
            if (wireBuf_.size() < kMaxWireBufferSize) {
                wireBuf_.resize(kMaxWireBufferSize);
            }
            std::vector<std::uint8_t>& wireBuf = wireBuf_;
            std::size_t receivedSize = 0;
            while (session_.transport->Receive(wireBuf.data(), wireBuf.size(), &receivedSize)) {
                const auto header = free_direct_directplay::TryDeserializeDirectPlayWireHeader(
                    wireBuf.data(), receivedSize);
                if (!header) continue;

                switch (header->type) {
                    case free_direct_directplay::DirectPlayWirePacketType::Data:
                        HandleDataPacket(*header, wireBuf, receivedSize);
                        break;
                    case free_direct_directplay::DirectPlayWirePacketType::JoinAccept:
                        HandleJoinAcceptPacket(*header);
                        break;
                    default:
                        DirectPlayLog("free-direct Receive: type=%u consumed and ignored",
                                      static_cast<unsigned>(header->type));
                        // Join (host-side only meaningful, and assignment already happens
                        // independently of it - see the assignment loop above),
                        // JoinReject/Discovery/DiscoveryResponse (not implemented yet) -
                        // consumed from the queue and otherwise ignored.
                        break;
                }
            }
        }

        std::atomic<ULONG> refCount_;
        free_direct_directplay::DirectPlaySession session_;
        // Persistent scratch buffer for Receive()'s drain loop, reused across calls instead of
        // allocating a fresh ~4.1KB vector every call (docs/audit_dplay.md §5.2, D10,
        // TASK-24H-0179) - measured negligible (228.7ns/call) even before this change, proposed
        // purely for consistency with this project's established buffer-reuse pattern elsewhere
        // (e.g. DirectDraw's PresentPrimary), not for a measured performance need.
        std::vector<std::uint8_t> wireBuf_;
#ifdef FREE_DIRECT_ENABLE_ENET
        // Owned by this object (not DirectPlaySession, which stays backend-agnostic - the
        // transport/discovery split mirrors CLAUDE.md's Internal Backend Policy) - real LAN
        // discovery, only meaningful while hosting over ENet (docs/directplay-design.md
        // Decision 23, TASK-24H-0150). Started in Open(DPOPEN_CREATE)'s ENet hosting branch,
        // serviced from Receive()'s existing Service() piggyback, stopped in Close().
        free_direct_directplay::DirectPlayDiscoveryService discoveryService_;
#endif
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

    // FreeDirect-internal placeholder service provider (docs/directplay-design.md Decision 1,
    // TASK-24H-0100). Not a real Microsoft service-provider GUID - this project is explicitly not
    // wire-compatible with real DirectPlay (CLAUDE.md), so there is no external registry this needs
    // to match. Distinct from IID_IDirectPlay/IID_IDirectPlay2A (dplay.h, Data1 1 and 0) so it can
    // never be mistaken for either COM interface ID if ever compared.
    const GUID kFreeDirectServiceProviderGuid = {2};

    // "FreeDirect" in both encodings the two callback signatures need. free-eggbert's own
    // EnumProvidersCallback (src/network.cpp) strcpy()s this into a 100-byte buffer
    // (NamedGUID::name, include/network.hpp) and later exposes it verbatim via
    // CNetwork::GetProviderName() to the game's UI text rendering (Decision 1's own note), so it
    // must be a short, human-readable, null-terminated string - not a placeholder-looking token.
    const char kFreeDirectServiceProviderNameA[] = "FreeDirect";
    // WCHAR is uint16_t (free-api/include/winnt.h), not the native (4-byte) wchar_t on this
    // platform - a plain L"..." literal would be the wrong width, so this is spelled out
    // char-by-char instead of relying on a wide-string-literal prefix.
    const WCHAR kFreeDirectServiceProviderNameW[] = {
        'F', 'r', 'e', 'e', 'D', 'i', 'r', 'e', 'c', 't', 0
    };
}

HRESULT WINAPI DirectPlayEnumerateA(LPDPENUMDPCALLBACKA lpEnumCallback, LPVOID lpContext) {
    if (!lpEnumCallback) return DPERR_INVALIDPARAMS;
    // Exactly one invocation, describing the single FreeDirect-internal placeholder provider
    // (Decision 1) - enough to satisfy free-eggbert's CNetwork::CreateProvider(0), the only index
    // its reconstructed source ever constructs. The callback's own return value is intentionally
    // not consulted: with only one provider to report, "stop enumerating" and "finished
    // enumerating" are the same outcome either way.
    lpEnumCallback(const_cast<LPGUID>(&kFreeDirectServiceProviderGuid),
                   const_cast<LPSTR>(kFreeDirectServiceProviderNameA),
                   1, 0, lpContext);
    return DP_OK;
}

HRESULT WINAPI DirectPlayEnumerateW(LPDPENUMDPCALLBACKW lpEnumCallback, LPVOID lpContext) {
    if (!lpEnumCallback) return DPERR_INVALIDPARAMS;
    lpEnumCallback(const_cast<LPGUID>(&kFreeDirectServiceProviderGuid),
                   const_cast<LPWSTR>(kFreeDirectServiceProviderNameW),
                   1, 0, lpContext);
    return DP_OK;
}

HRESULT WINAPI DirectPlayCreate(LPGUID lpGUID, LPDIRECTPLAY* lplpDP, IUnknown* pUnkOuter) {
    (void)lpGUID;
    if (!lplpDP) return DPERR_INVALIDPARAMS;
    *lplpDP = nullptr;
    if (pUnkOuter) return DPERR_NOAGGREGATION;
    *lplpDP = new (std::nothrow) DirectPlayImpl();
    return (*lplpDP) ? DP_OK : DPERR_OUTOFMEMORY;
}
