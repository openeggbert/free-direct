# Optional WebRTC for free-direct-server (datagrams for browsers, docs/directplay-relay-protocol.md
# "Datagrams"): libdatachannel from a shared checkout with its deps/libjuice, deps/usrsctp and
# deps/plog submodules, built static with OpenSSL, without media or its own WebSocket.
#   -DFREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR=~/deps/libdatachannel-v0.24.6
# Without it the server answers no WebRTC offers; UDP datagrams and everything else still work.
set(FREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR "" CACHE PATH
    "libdatachannel source (WebRTC data channels for browsers in free-direct-server); empty: none")
if(FREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR AND NOT TARGET datachannel-static)
    if(NOT EXISTS "${FREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR}/deps/libjuice/CMakeLists.txt")
        message(FATAL_ERROR "FREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR needs libdatachannel with its submodules "
                            "deps/libjuice, deps/usrsctp and deps/plog")
    endif()
    set(NO_MEDIA ON CACHE BOOL "" FORCE)
    set(NO_WEBSOCKET ON CACHE BOOL "" FORCE)
    set(NO_EXAMPLES ON CACHE BOOL "" FORCE)
    set(NO_TESTS ON CACHE BOOL "" FORCE)
    set(PREFER_SYSTEM_LIB OFF CACHE BOOL "" FORCE)
    add_subdirectory("${FREE_DIRECT_LIBDATACHANNEL_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libdatachannel" EXCLUDE_FROM_ALL)
endif()
