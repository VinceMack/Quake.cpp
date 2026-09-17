cmake_minimum_required(VERSION 3.10...3.20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.10)
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
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)

# Fetch EABase (required dependency for EASTL)
FetchContent_Declare(
    EABase
    GIT_REPOSITORY https://github.com/electronicarts/EABase.git
    GIT_TAG        master
    GIT_SUBMODULES ""
)

# Fetch EASTL
FetchContent_Declare(
    EASTL
    GIT_REPOSITORY https://github.com/electronicarts/EASTL.git
    GIT_TAG        master
    GIT_SUBMODULES ""
)
# Disable unnecessary EASTL features for faster build
set(EASTL_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(EASTL_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(SDL2 EABase EASTL)

# Gather source files
set(CORE_SRCS
    src/core/string_utils.cpp
    src/core/cvar.cpp
    src/core/cmd.cpp
    src/core/memory.cpp
    src/core/msg.cpp
    src/core/filesystem.cpp
    src/core/wad.cpp
    src/platform/sys_win.cpp
    src/world/model.cpp
    src/world/collision.cpp
    src/host/host.cpp
    src/ui/console.cpp
    src/ui/menu.cpp
    src/ui/hud.cpp
    src/render/draw2d.cpp
    src/render/renderer.cpp
    src/client/screen.cpp
    src/client/view.cpp
    src/client/view_blend.cpp
    src/render/software/sw_vid.cpp
    src/render/software/sw_part.cpp
    src/render/software/sw_sky.cpp
    src/render/software/sw_light.cpp
    src/render/software/sw_surf.cpp
    src/render/software/sw_drawface.cpp
    src/render/software/sw_edge.cpp
    src/render/software/sw_bsp.cpp
    src/render/software/sw_sprite.cpp
    src/render/software/sw_alias_clip.cpp
    src/render/software/sw_alias.cpp
    src/render/software/sw_warp.cpp
    src/render/software/sw_poly.cpp
    src/render/software/sw_sprite_draw.cpp
    src/render/software/sw_raster.cpp
    src/render/software/sw_efrag.cpp
    src/render/software/sw_frame.cpp
    src/render/software/sw_main.cpp
    src/audio/audio_dma.cpp
    src/audio/audio_wav.cpp
    src/audio/audio_mix.cpp
    src/audio/audio_main.cpp
    src/network/loopback.cpp
    src/network/udp_driver.cpp
    src/network/datagram.cpp
    src/network/net_main.cpp
    src/vm/program.cpp
    src/vm/edict.cpp
    src/vm/interpreter.cpp
    src/vm/builtins.cpp
    src/server/server.cpp
    src/server/world.cpp
    src/server/physics.cpp
    src/server/sv_send.cpp
    src/client/cl_main.cpp
    src/client/cl_input.cpp
    src/client/cl_parse.cpp
    src/client/cl_tent.cpp
    src/client/cl_demo.cpp
    src/client/chase.cpp
    src/client/input.cpp
)

add_executable(Quake.cpp ${CORE_SRCS})

target_include_directories(Quake.cpp PRIVATE src)

# Link SDL2, EASTL, and Windows libraries
if(WIN32)
    target_link_libraries(Quake.cpp PRIVATE SDL2main SDL2-static EASTL ws2_32 winmm)
else()
    target_link_libraries(Quake.cpp PRIVATE SDL2main SDL2-static EASTL)
endif()

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
    )
else()
    target_compile_options(Quake.cpp PRIVATE 
        -Wall -Wextra 
        -Wunreachable-code # Warn about unreachable code paths
        -Wno-write-strings
        -fno-exceptions
        -ffunction-sections # Put each function in its own section
        -fdata-sections     # Put each variable in its own section
    )
    target_link_options(Quake.cpp PRIVATE
        -Wl,--gc-sections       # Garbage collect empty sections
    )
endif()