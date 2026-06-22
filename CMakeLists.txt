cmake_minimum_required(VERSION 3.20)
project(CleanQuake C)

# Set standard C flags
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
    src/cd_sdl.c
    src/chase.c
    src/cl_demo.c
    src/cl_input.c
    src/cl_main.c
    src/cl_parse.c
    src/cl_tent.c
    src/cmd.c
    src/common.c
    src/console.c
    src/crc.c
    src/cvar.c
    src/d_edge.c
    src/d_fill.c
    src/d_init.c
    src/d_modech.c
    src/d_part.c
    src/d_polyse.c
    src/d_scan.c
    src/d_sky.c
    src/d_sprite.c
    src/d_surf.c
    src/d_vars.c
    src/d_zpoint.c
    src/draw.c
    src/host.c
    src/host_cmd.c
    src/keys.c
    src/mathlib.c
    src/menu.c
    src/model.c
    src/net_bsd.c
    src/net_dgrm.c
    src/net_loop.c
    src/net_main.c
    src/net_udp.c
    src/net_vcr.c
    src/nonintel.c
    src/pr_cmds.c
    src/pr_edict.c
    src/pr_exec.c
    src/r_aclip.c
    src/r_alias.c
    src/r_bsp.c
    src/r_draw.c
    src/r_edge.c
    src/r_efrag.c
    src/r_light.c
    src/r_main.c
    src/r_misc.c
    src/r_part.c
    src/r_sky.c
    src/r_sprite.c
    src/r_surf.c
    src/r_vars.c
    src/sbar.c
    src/screen.c
    src/snd_dma.c
    src/snd_mem.c
    src/snd_mix.c
    src/snd_sdl.c
    src/sv_main.c
    src/sv_move.c
    src/sv_phys.c
    src/sv_user.c
    src/sys_sdl.c
    src/vid_sdl.c
    src/view.c
    src/wad.c
    src/world.c
    src/zone.c
)

add_executable(clean-quake ${CORE_SRCS})

target_include_directories(clean-quake PRIVATE src)

# Link SDL2 and Windows libraries
target_link_libraries(clean-quake PRIVATE SDL2main SDL2-static ws2_32 winmm)

# Suppress some old C warnings for cleaner output
if(MSVC)
    target_compile_options(clean-quake PRIVATE /W3 /wd4996 /wd4244 /wd4113 /wd4047 /wd4024)
else()
    target_compile_options(clean-quake PRIVATE -Wall -Wno-pointer-sign -Wno-implicit-function-declaration -Wno-incompatible-pointer-types -Wno-int-conversion)
endif()
