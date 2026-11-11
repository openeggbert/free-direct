# Provides FreeDirect::IXWebSocket: the WebSocket library of the native relay client
# (FREE_DIRECT_ENABLE_RELAY) and of free-direct-server.
#
# Order of preference:
#   1. an `ixwebsocket` target the parent project already defines;
#   2. a source checkout in FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR (e.g. ~/deps/IXWebSocket-v11.4.6),
#      built with TLS through OpenSSL (FREE_DIRECT_RELAY_TLS) and without zlib;
#   3. an installed package, find_package(ixwebsocket CONFIG).
include_guard(GLOBAL)

set(FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR "" CACHE PATH
    "IXWebSocket source checkout for the native relay client and free-direct-server; empty: find_package(ixwebsocket)")
option(FREE_DIRECT_RELAY_TLS "wss:// (TLS through OpenSSL) in the native relay client and in free-direct-server" ON)

if(NOT TARGET FreeDirect::IXWebSocket)
    if(TARGET ixwebsocket)
        add_library(FreeDirect::IXWebSocket ALIAS ixwebsocket)
    elseif(FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR)
        if(NOT EXISTS "${FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR=${FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR} has no CMakeLists.txt")
        endif()
        set(USE_TLS ${FREE_DIRECT_RELAY_TLS} CACHE BOOL "IXWebSocket: TLS" FORCE)
        set(USE_OPEN_SSL ${FREE_DIRECT_RELAY_TLS} CACHE BOOL "IXWebSocket: TLS through OpenSSL" FORCE)
        set(USE_ZLIB OFF CACHE BOOL "IXWebSocket: zlib (per-message deflate)" FORCE)
        set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "IXWebSocket: install rules" FORCE)
        add_subdirectory("${FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/_deps/ixwebsocket" EXCLUDE_FROM_ALL)
        add_library(FreeDirect::IXWebSocket ALIAS ixwebsocket)
    else()
        find_package(ixwebsocket CONFIG QUIET)
        if(NOT TARGET ixwebsocket::ixwebsocket)
            message(FATAL_ERROR
                "IXWebSocket is needed by the native relay client (FREE_DIRECT_ENABLE_RELAY) and by "
                "free-direct-server. Set FREE_DIRECT_IXWEBSOCKET_SOURCE_DIR to a checkout of "
                "https://github.com/machinezone/IXWebSocket (v11.4.6 is tested), or install it.")
        endif()
        add_library(free_direct_ixwebsocket INTERFACE)
        target_link_libraries(free_direct_ixwebsocket INTERFACE ixwebsocket::ixwebsocket)
        add_library(FreeDirect::IXWebSocket ALIAS free_direct_ixwebsocket)
    endif()
endif()
