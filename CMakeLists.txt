cmake_minimum_required(VERSION 3.20)

project(FREE_DIRECT LANGUAGES CXX)

include(GNUInstallDirs)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET free-api)
    set(FREE_API_BUILD_TESTS OFF)
    add_subdirectory(../free-api FREE_API)
endif()

add_library(free-direct)

target_sources(free-direct
        PRIVATE
        src/directdraw/DirectDraw.cpp
        src/directsound/DirectSound.cpp
        src/directplay/DirectPlay.cpp
        src/diagnostics/Diagnostics.cpp
)
# Compile-time-gated diagnostics counters (off by default; zero overhead when off).
# Enable from the parent project via:
#   set(FREE_DIRECT_DIAGNOSTICS ON CACHE BOOL "Enable Free Direct diagnostics")
option(FREE_DIRECT_DIAGNOSTICS "Enable Free Direct diagnostic counters" OFF)
if(FREE_DIRECT_DIAGNOSTICS)
    target_compile_definitions(free-direct PRIVATE FREE_DIRECT_DIAGNOSTICS=1)
endif()
target_include_directories(free-direct PRIVATE src)

# Intentionally mirrors the legacy DirectX SDK include layout, e.g. #include <ddraw.h>.
target_include_directories(free-direct
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(free-direct
        PUBLIC
        free-api::free-api
        PRIVATE
        SDL3::SDL3
        SDL3_image::SDL3_image
)

add_executable(FREE_DIRECT src/Main.cpp)
target_link_libraries(FREE_DIRECT PRIVATE free-direct)
