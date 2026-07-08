# Wired in only when FREE_DIRECT_BUILD_TESTS=ON (see root CMakeLists.txt).
# 24-Hour Stabilization Backlog TASK-24H-0002/0003/0004 (plan.md).

add_executable(directplay_tests directplay_tests.cpp)

# DirectPlayMessageQueue.hpp / DirectPlayWireProtocol.hpp / LoopbackDirectPlayTransport.hpp are
# private headers under src/directplay/, not part of free-direct's public include/ surface -
# mirrors the -I src/directplay flag documented in directplay_tests.cpp's own header comment.
# CMAKE_CURRENT_SOURCE_DIR (not CMAKE_SOURCE_DIR) is required here: when free-direct is pulled in
# via a target game's add_subdirectory(../free-direct), CMAKE_SOURCE_DIR resolves to the *parent*
# project's root, not free-direct's own - CMAKE_CURRENT_SOURCE_DIR always tracks the directory of
# the CMakeLists.txt currently being processed (this one, tests/), so ".." is always free-direct's
# root regardless of how deeply this project is nested by a consumer.
target_include_directories(directplay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/directplay)

target_link_libraries(directplay_tests
        PRIVATE
        free-direct
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
)

add_test(NAME directplay_tests COMMAND directplay_tests)
set_tests_properties(directplay_tests PROPERTIES LABELS "directplay")

# Compile-only smoke tests: each public header must compile standalone, as a real consumer would
# include it, without pulling in free-direct's own .cpp implementation.
# 24-Hour Stabilization Backlog TASK-24H-0016/0017/0018/0019 (plan.md).
foreach(header_name ddraw dsound dplay)
    set(target_name header_smoke_${header_name})
    add_executable(${target_name} header_smoke_${header_name}.cpp)
    target_link_libraries(${target_name} PRIVATE free-direct)
    add_test(NAME ${target_name} COMMAND ${target_name})
    set_tests_properties(${target_name} PROPERTIES LABELS "headers")
endforeach()

# Header-hygiene regression check: no SDL3/SDL3_net/ENet identifier may leak into include/.
# 24-Hour Stabilization Backlog TASK-24H-0024 (plan.md).
add_test(
        NAME header_hygiene
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/check_header_hygiene.sh ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
set_tests_properties(header_hygiene PROPERTIES LABELS "headers")

# DirectDraw tests: needs the real free-direct library (SDL3/SDL3_image/SDL3_mixer/free-api),
# unlike directplay_tests. Runs headlessly via the dummy SDL video/audio drivers (set as a CTest
# ENVIRONMENT property so `ctest` runs it correctly without relying on the invoker's shell state).
# 24-Hour Stabilization Backlog TASK-24H-0026 through TASK-24H-0049 (plan.md).
add_executable(directdraw_tests directdraw_tests.cpp)
target_link_libraries(directdraw_tests
        PRIVATE
        free-direct
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
)
add_test(NAME directdraw_tests COMMAND directdraw_tests)
set_tests_properties(directdraw_tests PROPERTIES
        LABELS "directdraw"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)

# DirectSound tests: same rationale as directdraw_tests, needs the real free-direct library and
# runs headlessly via the dummy SDL audio driver.
# 24-Hour Stabilization Backlog TASK-24H-0056 through TASK-24H-0072 (plan.md).
add_executable(directsound_tests directsound_tests.cpp)
target_link_libraries(directsound_tests
        PRIVATE
        free-direct
        SDL3::SDL3
        SDL3_image::SDL3_image
        SDL3_mixer::SDL3_mixer
)
add_test(NAME directsound_tests COMMAND directsound_tests)
set_tests_properties(directsound_tests PROPERTIES
        LABELS "directsound"
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy"
)

# ENet transport-level tests: gated at the CMake level (not just skipped at runtime) behind
# FREE_DIRECT_ENABLE_ENET, matching EnetDirectPlayTransport.cpp's own compilation gate - the
# target does not exist at all, and therefore cannot be registered with CTest, when ENet is off.
# Never part of the default build/test run (CLAUDE.md Testing Policy: real-socket ENet
# integration tests are opt-in, never run by default in a way that could flake on shared/CI
# network namespaces). 24-Hour Stabilization Backlog TASK-24H-0097/0098/0108 (plan.md).
if(FREE_DIRECT_ENABLE_ENET)
    add_executable(enet_directplay_tests enet_directplay_tests.cpp)
    target_include_directories(enet_directplay_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src/directplay)
    target_link_libraries(enet_directplay_tests
            PRIVATE
            free-direct
            FreeDirect::ENet
            SDL3::SDL3
            SDL3_image::SDL3_image
            SDL3_mixer::SDL3_mixer
    )
    add_test(NAME enet_directplay_tests COMMAND enet_directplay_tests)
    set_tests_properties(enet_directplay_tests PROPERTIES LABELS "enet")
endif()

# Applies the root CMakeLists.txt's optional ASan/UBSan flags (TASK-24H-0010) to every test
# executable defined above - FREE_DIRECT_SANITIZER_FLAGS is a plain (non-cache) variable set in
# the parent scope, inherited here automatically since add_subdirectory() child scopes see their
# parent's variables. Empty (and this loop a no-op) unless FREE_DIRECT_ENABLE_ASAN/UBSAN is ON.
if(FREE_DIRECT_SANITIZER_FLAGS)
    set(FREE_DIRECT_SANITIZED_TEST_TARGETS
            directplay_tests header_smoke_ddraw header_smoke_dsound header_smoke_dplay
            directdraw_tests directsound_tests)
    if(FREE_DIRECT_ENABLE_ENET)
        list(APPEND FREE_DIRECT_SANITIZED_TEST_TARGETS enet_directplay_tests)
    endif()
    foreach(sanitized_target ${FREE_DIRECT_SANITIZED_TEST_TARGETS})
        target_compile_options(${sanitized_target} PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
        target_link_options(${sanitized_target} PRIVATE ${FREE_DIRECT_SANITIZER_FLAGS})
    endforeach()
endif()
