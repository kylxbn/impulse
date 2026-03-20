find_package(PkgConfig REQUIRED)

# System libraries via pkg-config
pkg_check_modules(LIBAV REQUIRED IMPORTED_TARGET
    libavformat libavcodec libavutil libswresample libswscale)
pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
pkg_check_modules(SDL3 REQUIRED IMPORTED_TARGET sdl3)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)

# Third-party via FetchContent
include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking
    GIT_SHALLOW    TRUE)

FetchContent_Declare(readerwriterqueue
    GIT_REPOSITORY https://github.com/cameron314/readerwriterqueue.git
    GIT_TAG        master)

FetchContent_Declare(libvgm
    GIT_REPOSITORY https://github.com/ValleyBell/libvgm.git
    GIT_TAG        a16858c4b92e297eaf9ddd602ecb790a5b980b69
    GIT_SHALLOW    TRUE)

FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    FetchContent_Populate(imgui)
endif()

FetchContent_GetProperties(readerwriterqueue)
if(NOT readerwriterqueue_POPULATED)
    FetchContent_Populate(readerwriterqueue)
endif()

set(BUILD_LIBAUDIO OFF)
set(BUILD_LIBEMU ON)
set(BUILD_LIBPLAYER ON)
set(BUILD_TESTS OFF)
set(BUILD_PLAYER OFF)
set(BUILD_VGM2WAV OFF)
set(USE_SANITIZERS OFF)

FetchContent_GetProperties(libvgm)
if(NOT libvgm_POPULATED)
    FetchContent_MakeAvailable(libvgm)
endif()

unset(BUILD_LIBAUDIO)
unset(BUILD_LIBEMU)
unset(BUILD_LIBPLAYER)
unset(BUILD_TESTS)
unset(BUILD_PLAYER)
unset(BUILD_VGM2WAV)
unset(USE_SANITIZERS)

function(impulse_configure_third_party_target target_name)
    if(NOT TARGET "${target_name}")
        return()
    endif()

    target_compile_options("${target_name}" PRIVATE -w
        $<$<CONFIG:Debug>:-fno-sanitize=address,undefined>)
endfunction()

impulse_configure_third_party_target(vgm-utils)
impulse_configure_third_party_target(vgm-emu)
impulse_configure_third_party_target(vgm-player)

# Build ImGui as a static lib with SDL3 + SDL_Renderer backends
add_library(imgui_sdl3 STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
target_include_directories(imgui_sdl3 PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_sdl3 PUBLIC PkgConfig::SDL3)
target_compile_options(imgui_sdl3 PRIVATE -w)  # suppress warnings in third-party

# readerwriterqueue is header-only, so keep it as a simple interface target.
add_library(readerwriterqueue INTERFACE)
target_include_directories(readerwriterqueue INTERFACE
    ${readerwriterqueue_SOURCE_DIR})
