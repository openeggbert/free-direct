cmake_minimum_required(VERSION 3.20)

project(FREE_DIRECT LANGUAGES CXX)

# Default to a Release build when this is the top-level project and no build type was requested,
# neither on the command line nor by an earlier set() call (docs/audit_ddraw.md §3.1,
# TASK-24H-0151): a plain `cmake -B build` with no extra flags was silently producing an
# unoptimized binary (measured 6.6x slower than -O3 on a DirectDraw hot path). Scoped to the
# top-level case only (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR, the portable
# pre-3.21 equivalent of PROJECT_IS_TOP_LEVEL) so a consuming project's own build-type choice for
# free-eggbert/planetblupi is never overridden - only free-direct's own standalone build is
# affected. CMAKE_CONFIGURATION_TYPES guards multi-config generators (Visual Studio, Xcode),
# which select config at build time and ignore CMAKE_BUILD_TYPE entirely.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build: Debug Release RelWithDebInfo MinSizeRel" FORCE)
endif()

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT TARGET free-api)
    set(FREE_API_BUILD_TESTS OFF)
    add_subdirectory(../free-api FREE_API)
endif()

# free-api may resolve SDL3/SDL3_image/SDL3_mixer via find_package() (its own
# -DFREE_API_USE_SYSTEM_SDL3=ON path, or a sibling game's ThirdPartySDL.cmake
# vendoring script's -DFREE_USE_SYSTEM_SDL=ON path). CMake's find_package()
# (config mode) only makes imported targets visible in the directory scope
# where it was called and below - here, that is inside free-api's own
# add_subdirectory() scope, a *child* of this directory, so from this
# (parent) scope the targets do not exist at all once add_subdirectory()
# returns (confirmed via --trace-expand: `if(TARGET SDL3::SDL3)` evaluates
# false here even though the same check succeeds inside free-api's
# CMakeLists.txt). A target that doesn't exist in this scope cannot be
# promoted to global from here (there's nothing to call
# set_target_properties() on) - the fix has to re-run find_package() itself,
# directly in this scope. That is fast and reliable because free-api's
# nested find_package() call already populated SDL3_DIR / SDL3_image_DIR /
# SDL3_mixer_DIR in the cache, so this just re-locates the same config
# files. (When free-api instead vendors SDL as a real add_subdirectory()'d
# project - the default path, no -DFREE_API_USE_SYSTEM_SDL3 - those targets
# are ordinary, not IMPORTED, and are already visible everywhere, so every
# find_package() call below is skipped as a no-op.)
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG QUIET)
endif()
if(NOT TARGET SDL3_image::SDL3_image)
    find_package(SDL3_image CONFIG QUIET)
endif()
if(NOT TARGET SDL3_mixer::SDL3_mixer)
    find_package(SDL3_mixer CONFIG QUIET)
endif()

if(NOT TARGET SDL3::SDL3 OR NOT TARGET SDL3_image::SDL3_image OR NOT TARGET SDL3_mixer::SDL3_mixer)
    message(FATAL_ERROR
        "SDL targets are missing. Configure vendored SDL first (see free-eggbert/cmake/ThirdPartySDL.cmake), "
        "or provide SDL targets from the parent project.")
endif()

# ENet-backed DirectPlay transport (plan.md Phase 5). OFF by default: the default build has
# zero ENet dependency, and no header under include/ may ever transitively include an ENet
# header regardless of this option's state (CLAUDE.md's Internal Backend Policy).
option(FREE_DIRECT_ENABLE_ENET "Enable the ENet-backed DirectPlay transport (EnetDirectPlayTransport)" OFF)
option(FREE_DIRECT_USE_SYSTEM_ENET "Find a system-installed ENet instead of a vendored copy under third_party/enet" OFF)

if(FREE_DIRECT_ENABLE_ENET)
    if(FREE_DIRECT_USE_SYSTEM_ENET)
        # Debian/Ubuntu's libenet-dev (and most other distros) ship a "libenet" pkg-config
        # module; ENet has no upstream CMake config package, only this pkg-config file.
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(FREE_DIRECT_ENET REQUIRED IMPORTED_TARGET libenet)
        add_library(FreeDirect::ENet ALIAS PkgConfig::FREE_DIRECT_ENET)
    else()
        if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/CMakeLists.txt")
            message(FATAL_ERROR
                "FREE_DIRECT_ENABLE_ENET is ON but no vendored ENet was found at "
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/CMakeLists.txt. Either vendor "
                "ENet there (e.g. as a git submodule pointing at "
                "https://github.com/lsalzman/enet), or configure with "
                "-DFREE_DIRECT_USE_SYSTEM_ENET=ON to use a system-installed libenet instead.")
        endif()
        add_subdirectory(third_party/enet EXCLUDE_FROM_ALL)
        if(NOT TARGET enet)
            message(FATAL_ERROR
                "third_party/enet was found but did not define the expected 'enet' CMake "
                "target. Its upstream CMakeLists.txt target name may have changed since this "
                "was written - update the ALIAS below to match.")
        endif()
        # Upstream ENet's own CMakeLists.txt adds its include directory via the old
        # directory-scoped include_directories(), not target_include_directories(), so it is
        # not carried as a usage requirement of the "enet" target - callers outside ENet's own
        # directory scope (i.e. us) would not see enet/enet.h without this.
        target_include_directories(enet PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/enet/include)
        add_library(FreeDirect::ENet ALIAS enet)
    endif()
endif()

add_library(free-direct STATIC)

target_sources(free-direct
        PRIVATE
        src/directdraw/DirectDraw.cpp
        src/directdraw/DirectDrawSurface.cpp
        src/directdraw/DirectDrawPalette.cpp
        src/directdraw/DirectDrawClipper.cpp
        src/directsound/DirectSound.cpp
        src/directplay/DirectPlay.cpp
        src/directplay/DirectPlay2A.cpp
        src/directplay/DirectPlaySession.cpp
        src/directplay/DirectPlayMessageQueue.cpp
        src/directplay/DirectPlayWireProtocol.cpp
        src/directplay/LoopbackDirectPlayTransport.cpp
        src/diagnostics/Diagnostics.cpp
)
# Compile-time-gated diagnostics counters (off by default; zero overhead when off).
# Enable from the parent project via:
#   set(FREE_DIRECT_DIAGNOSTICS ON CACHE BOOL "Enable Free Direct diagnostics")
option(FREE_DIRECT_DIAGNOSTICS "Enable Free Direct diagnostic counters" OFF)
if(FREE_DIRECT_DIAGNOSTICS)
    target_compile_definitions(free-direct PRIVATE FREE_DIRECT_DIAGNOSTICS=1)
endif()

# Present 8-bit DirectDraw surfaces as native SDL3 SDL_PIXELFORMAT_INDEX8 textures
# with an attached SDL_Palette instead of converting index->RGBA32 on the CPU every
# present. Lets the renderer backend's own texture sampling perform the palette
# lookup (supported by every SDL3 renderer backend: OpenGL, OpenGLES2, Direct3D
# 9/11/12, Metal, Vulkan, and the software renderer). ON by default; set to OFF to
# fall back to the original CPU-side per-pixel conversion path if ever needed.
option(FREE_DIRECT_ENABLE_INDEXED_TEXTURES "Use GPU-side palette lookup (SDL_PIXELFORMAT_INDEX8) instead of CPU-side palette conversion for 8-bit DirectDraw surfaces" ON)
if(FREE_DIRECT_ENABLE_INDEXED_TEXTURES)
    target_compile_definitions(free-direct PRIVATE FREE_DIRECT_ENABLE_INDEXED_TEXTURES=1)
endif()
target_include_directories(free-direct PRIVATE src)

# Optional CMake-level force-enable for the FREE_DIRECT_DEBUG_* runtime env-var flags
# (24-Hour Stabilization Backlog TASK-24H-0119, plan.md). The env-var check each flag already
# performs (SDL_getenv("FREE_DIRECT_DEBUG_...")) remains the primary/default mechanism - these
# options are strictly additive, for cases (e.g. a CI diagnostic build) where forcing a flag on at
# compile time is more convenient than setting an environment variable at every invocation. OFF by
# default, so default builds are unaffected. See README.md's "Debug logging and performance
# options" section for the full list of underlying FREE_DIRECT_DEBUG_* flags.
foreach(free_direct_debug_flag DDRAW PRESENTATION COLORKEY PERF PRIMARY_CLEAR DSOUND DSOUND_FORMAT DPLAY)
    option(FREE_DIRECT_FORCE_DEBUG_${free_direct_debug_flag}
            "Force-enable FREE_DIRECT_DEBUG_${free_direct_debug_flag} at compile time, regardless of the env var" OFF)
    if(FREE_DIRECT_FORCE_DEBUG_${free_direct_debug_flag})
        target_compile_definitions(free-direct PRIVATE FREE_DIRECT_DEBUG_${free_direct_debug_flag}=1)
    endif()
endforeach()

# Optional sanitizer builds (24-Hour Stabilization Backlog TASK-24H-0010, plan.md). OFF by
# default. Applied PRIVATE/directly to free-direct's own targets only (this file's FREE_DIRECT
# demo executable, and tests/CMakeLists.txt's test executables via the same cache variable,
# inherited into that add_subdirectory() scope) - never PUBLIC/INTERFACE, so a target game
# consuming free-direct via add_subdirectory() never has sanitizer flags forced onto its own
# targets just because free-direct was configured with one of these on.
option(FREE_DIRECT_ENABLE_ASAN "Build free-direct's own targets with AddressSanitizer" OFF)
option(FREE_DIRECT_ENABLE_UBSAN "Build free-direct's own targets with UndefinedBehaviorSanitizer" OFF)
set(FREE_DIRECT_SANITIZER_FLAGS "")
if(FREE_DIRECT_ENABLE_ASAN)
    list(APPEND FREE_DIRECT_SANITIZER_FLAGS -fsanitize=address -fno-omit-frame-pointer)
endif()
if(FREE_DIRECT_ENABLE_UBSAN)
    list(APPEND FREE_DIRECT_SANITIZER_FLAGS -fsanitize=undefined)
endif()
if(FREE_DIRECT_SANITIZER_FLAGS)
    target_compile_options(free-direct PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
    target_link_options(free-direct PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
endif()

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
        SDL3_mixer::SDL3_mixer
)

if(FREE_DIRECT_ENABLE_ENET)
    # PRIVATE: ENet's include directories and link requirements must never propagate to
    # consumers of the free-direct target (CLAUDE.md's Internal Backend Policy).
    target_link_libraries(free-direct PRIVATE FreeDirect::ENet)
    # Only compiled under this option, so the default build has zero ENet dependency -
    # EnetDirectPlayTransport.hpp is not (yet) included by anything else.
    target_sources(free-direct PRIVATE
            src/directplay/EnetDirectPlayTransport.cpp
            src/directplay/DirectPlayDiscovery.cpp)
    # Lets DirectPlay.cpp #ifdef its way to constructing EnetDirectPlayTransport instead of
    # LoopbackDirectPlayTransport in Open() (docs/directplay-design.md Decision 4: build-time
    # backend selection, driven by this exact option, no run-time switch). PRIVATE: this is an
    # implementation detail of free-direct itself, not something consumers should see or set.
    target_compile_definitions(free-direct PRIVATE FREE_DIRECT_ENABLE_ENET=1)
endif()

add_executable(FREE_DIRECT src/Main.cpp)
target_link_libraries(FREE_DIRECT
        PRIVATE
        free-direct
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
)
if(FREE_DIRECT_SANITIZER_FLAGS)
    target_compile_options(FREE_DIRECT PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
    target_link_options(FREE_DIRECT PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
endif()

# FreeDirect's own test executables (plan.md Phase 15 / 24-Hour Stabilization Backlog
# TASK-24H-0001). OFF by default so target-game diamond builds (free-eggbert, planetblupi)
# never gain new targets unless a developer explicitly opts in.
option(FREE_DIRECT_BUILD_TESTS "Build FreeDirect's own test executables" OFF)
if(FREE_DIRECT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
