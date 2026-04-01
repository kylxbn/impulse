cmake_minimum_required(VERSION 3.28)
project(impulse VERSION 1.4.2 LANGUAGES C CXX)

set(IMPULSE_APPSTREAM_RELEASE_DATE "2026-04-01")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(cmake/CompilerOptions.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(third_party/sc68)
add_subdirectory(third_party/asap)
add_subdirectory(src)

option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
