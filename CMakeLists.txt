cmake_minimum_required(VERSION 3.20)

project(FREE_DIRECT LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CNA_BUILD_TESTS OFF CACHE BOOL "Build CNA tests" FORCE)
add_subdirectory(../cna CNA)
add_subdirectory(../free-api FREE_API)

add_executable(FREE_DIRECT FREE_API
        src/Main.cpp
)

target_link_libraries(FREE_DIRECT
        PUBLIC
        FREE_API
        PRIVATE
        CNA
)
