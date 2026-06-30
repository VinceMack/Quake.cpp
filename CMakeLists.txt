cmake_minimum_required(VERSION 3.20)
project(CleanQuake LANGUAGES CXX)

include(GNUInstallDirs)
include(FindPkgConfig)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if( NOT CMAKE_BUILD_TYPE )
	set( CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE )
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_UPPER)
message(STATUS "Upper-case processor: ${SYSTEM_PROCESSOR_UPPER}")

if( CMAKE_CXX_COMPILER_ID MATCHES "Clang" )
	add_compile_options(-Wno-gnu-alignof-expression)
endif()

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "GNU" )
	if (NOT CMAKE_CROSSCOMPILING)
		add_compile_options(-march=native)
		if (${SYSTEM_PROCESSOR_UPPER} MATCHES "X64|X86_64|AMD64")
			add_compile_options(-mfpmath=sse)
		endif()
	endif()
	add_compile_options(-ftree-vectorize)
	add_compile_options(-fopenmp-simd)
	add_compile_options(-fdata-sections)
	add_compile_options(-fpermissive)
	add_compile_options(-ffunction-sections)
	add_compile_options(-fno-asynchronous-unwind-tables)
	add_compile_options(-Wall)
	add_compile_options(-Wextra)
	add_compile_options(-Wunreachable-code)
	add_compile_options(-Wno-write-strings)
	if (NOT CMAKE_BUILD_TYPE MATCHES Debug)
		add_link_options(-Wl,--gc-sections)
		add_link_options(-Wl,-s)
	endif()
else (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	add_compile_options(/openmp)
	add_compile_options(/W4)
	add_compile_options(/Z7)
	add_compile_options(/w14702)
	add_compile_options(/wd4996)
	add_compile_options(/wd4244)
	add_compile_options(/wd4113)
	add_compile_options(/wd4047)
	add_compile_options(/wd4024)
	if (NOT CMAKE_BUILD_TYPE MATCHES Debug)
		add_link_options(/DEBUG:NONE)
	endif()
	link_libraries(ws2_32 winmm)
endif()


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

cmake_policy(SET CMP0072 NEW)   #OpenGL_GL_PREFERENCE to GLVND
find_package(OpenGL)

find_package(SDL2)
if(NOT SDL2_FOUND)
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
endif()
add_compile_definitions(SDL)

# clean-quake executable
add_executable(clean-quake ${CORE_SRCS})
target_include_directories(clean-quake PRIVATE src)
target_link_libraries(clean-quake PRIVATE SDL2::SDL2 SDL2::SDL2main)

