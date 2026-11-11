# free-direct-server: the Free Direct relay server for Internet multiplayer.
#
# Builds on its own, without Free API or SDL (a server machine needs neither):
#   cmake -S server -B build/server -DFREE_DIRECT_IXWEBSOCKET_SOURCE_DIR=/path/to/IXWebSocket
#   cmake --build build/server
# or as part of free-direct with -DFREE_DIRECT_BUILD_SERVER=ON.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(free_direct_server LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
    include(GNUInstallDirs)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/FreeDirectIXWebSocket.cmake)
find_package(Threads REQUIRED)

# The room logic, without network code (also compiled into the hub unit tests).
add_library(free-direct-relay-hub STATIC RelayHub.cpp)
target_include_directories(free-direct-relay-hub PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/relay)
target_compile_features(free-direct-relay-hub PUBLIC cxx_std_20)

# The WebSocket front end, shared by the program and the relay integration tests.
add_library(free-direct-relay-server STATIC RelayServer.cpp)
target_link_libraries(free-direct-relay-server PUBLIC free-direct-relay-hub FreeDirect::IXWebSocket Threads::Threads)

add_executable(free-direct-server main.cpp)
target_link_libraries(free-direct-server PRIVATE free-direct-relay-server)
install(TARGETS free-direct-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
