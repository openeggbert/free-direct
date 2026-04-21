cmake_minimum_required(VERSION 3.20)

project(FREE_DIRECT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CNA_BUILD_TESTS OFF CACHE BOOL "Build CNA tests" FORCE)
add_subdirectory(../cna CNA)
add_subdirectory(../free-api FREE_API)

add_executable(FREE_DIRECT
        src/Main.cpp
        src/directdraw/DirectDraw.cpp
)

target_include_directories(FREE_DIRECT
        PRIVATE
        include
)

target_link_libraries(FREE_DIRECT
        PRIVATE
        free-api
        cna_backend_graphics_easygl
        CNA
        CNA
)
