cmake_minimum_required(VERSION 3.20)
project(CleanQuake CXX)

# Set standard C++ flags
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add SDL definition
add_compile_definitions(SDL)

# Fetch SDL2
include(FetchContent)
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.3
)
# Disable unnecessary SDL2 features for faster build
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL2)

# Gather source files
set(CORE_SRCS
    src/cd_sdl.cpp
    src/chase.cpp
    src/client.cpp
    src/cmd.cpp
    src/common.cpp
    src/console.cpp
    src/crc.cpp
    src/cvar.cpp
    src/audio.cpp
    src/rasterizer.cpp
    src/renderer.cpp
    src/draw.cpp
    src/host.cpp
    src/host_cmd.cpp
    src/keys.cpp
    src/mathlib.cpp
    src/menu.cpp
    src/model.cpp
    src/network.cpp
    src/vm.cpp
    src/sbar.cpp
    src/screen.cpp
    src/server.cpp
    src/sys_sdl.cpp
    src/vid_sdl.cpp
    src/view.cpp
    src/wad.cpp
    src/world.cpp
    src/zone.cpp
)

add_executable(Quake.cpp ${CORE_SRCS})

target_include_directories(Quake.cpp PRIVATE src)

# Link SDL2 and Windows libraries
target_link_libraries(Quake.cpp PRIVATE SDL2main SDL2-static ws2_32 winmm)

# Suppress some old C warnings for cleaner output
if(MSVC)
    target_compile_options(Quake.cpp PRIVATE 
        /W4 
        /Z7
        /w14702      # Force C4702 (Unreachable code) to be a Level 1 Warning
        /Gy          # Function-level linking (puts each function in its own section)
        /Gw          # Optimize global data (puts each variable in its own section)
    )
    # Target-specific linker overrides
    # NOTE: /INCREMENTAL:NO is required to allow /OPT:REF to function.
    target_link_options(Quake.cpp PRIVATE
        /INCREMENTAL:NO
        /OPT:REF       # Eliminate unreferenced functions and data
        /VERBOSE:REF   # Print out everything the linker throws away
    )
else()
    target_compile_options(Quake.cpp PRIVATE 
        -Wall -Wextra 
        -Wunreachable-code # Warn about unreachable code paths
        -Wno-write-strings
        -ffunction-sections # Put each function in its own section
        -fdata-sections     # Put each variable in its own section
    )
    target_link_options(Quake.cpp PRIVATE
        -Wl,--gc-sections       # Garbage collect empty sections
        -Wl,--print-gc-sections # Print out everything the linker throws away
    )
endif()