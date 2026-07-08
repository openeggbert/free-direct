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
