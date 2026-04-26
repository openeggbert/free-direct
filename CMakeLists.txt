cmake_minimum_required(VERSION 3.20)

project(FREE_DIRECT LANGUAGES CXX)

include(GNUInstallDirs)

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(FREE_API_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(NOT TARGET free-api)
    add_subdirectory(../free-api FREE_API)
endif()

add_executable(FREE_DIRECT
        src/Main.cpp
        src/directdraw/DirectDraw.cpp
        src/directsound/DirectSound.cpp
        src/directplay/DirectPlay.cpp
)

target_include_directories(FREE_DIRECT
        PRIVATE
        include
)

target_link_libraries(FREE_DIRECT
        PRIVATE
        free-api::free-api
        SDL3::SDL3
        SDL3_image::SDL3_image
)
