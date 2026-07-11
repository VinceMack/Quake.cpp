cmake_minimum_required(VERSION 3.20)
project(CleanQuake LANGUAGES CXX)

if(NOT CMAKE_GENERATOR MATCHES "^Visual Studio")
include(GNUInstallDirs)
include(FindPkgConfig)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)

if( NOT CMAKE_BUILD_TYPE )
	set( CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE )
endif()

string(TOUPPER "${CMAKE_SYSTEM_PROCESSOR}" SYSTEM_PROCESSOR_UPPER)
message(STATUS "OS: ${CMAKE_HOST_SYSTEM_NAME}-${CMAKE_HOST_SYSTEM_PROCESSOR} => ${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
message(STATUS "CXX: ${CMAKE_CXX_COMPILER_ID}/${CMAKE_CXX_COMPILER_FRONTEND_VARIANT}")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

add_compile_definitions(_FILE_OFFSET_BITS=64)
add_compile_definitions($<$<CONFIG:Debug>:DEBUG=1> $<$<NOT:$<CONFIG:Debug>>:NDEBUG=1>)
add_compile_definitions($<$<CONFIG:Debug>:_DEBUG=1> $<$<NOT:$<CONFIG:Debug>>:_NDEBUG=1>)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wno-gnu-alignof-expression)
endif()

if (CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "GNU|AppleClang")
	if( NOT CMAKE_BUILD_OPTLEVEL )
		set( CMAKE_BUILD_OPTLEVEL 3 CACHE STRING "Choose the optimization level." FORCE )
	endif()
	add_compile_options(-O${CMAKE_BUILD_OPTLEVEL})
	if (NOT CMAKE_CROSSCOMPILING)
		add_compile_options(-march=native)

		if (${SYSTEM_PROCESSOR_UPPER} MATCHES "X64|X86_64|AMD64")
			add_compile_options(-mfpmath=sse)
		endif()
		if (${SYSTEM_PROCESSOR_UPPER} MATCHES "PPC|POWERPC|PPC64|PPC64LE")
			include(CheckCSourceCompiles)

			set(CMAKE_REQUIRED_FLAGS "-maltivec")
			check_c_source_compiles("#include <altivec.h> int main() { vector int v = (vector int){0}; return 0; }" HAVE_ALTIVEC)

			if(HAVE_ALTIVEC)
				add_compile_options(-maltivec)
			endif()
		endif()
	endif()
	add_compile_options(-ftree-vectorize)
	add_compile_options(-fopenmp)
	if(NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT MATCHES "AppleClang")
		add_compile_options(-fopenmp-simd)
	endif()
	add_compile_options(-fpermissive)
	add_compile_options(-fno-asynchronous-unwind-tables)
	add_compile_options(-Wall)
	add_compile_options(-Wextra)
	add_compile_options(-Wno-write-strings)
	if (CMAKE_BUILD_TYPE MATCHES Debug)
		add_compile_options(-g)
	else()
		# Use IPO/LTO if available
		if(NOT ${CMAKE_VERSION} VERSION_LESS "3.9.0")
			cmake_policy(SET CMP0069 NEW)
			include(CheckIPOSupported)
			check_ipo_supported(RESULT result)
			if(result)
				set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
				message(STATUS "Using IPO/LTO.")
			endif()
		endif()
	endif()
	add_compile_options(-Wunreachable-code)		# Warn about unreachable code paths
	add_compile_options(-ffunction-sections)	# Put each function in its own section
	add_compile_options(-fdata-sections)		# Put each variable in its own section
	if (CMAKE_BUILD_TYPE MATCHES ReleaseStrip)
		add_link_options(-Wl,--gc-sections)		# Garbage collect empty sections
		add_link_options(-Wl,-s)				# Print out everything the linker throws away
	endif()
	link_libraries(m)
	if (WIN32 AND MINGW)
		link_libraries(mingw32)
	endif()
else (CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	if( NOT CMAKE_BUILD_OPTLEVEL )
		set( CMAKE_BUILD_OPTLEVEL 2 CACHE STRING "Choose the optimization level." FORCE )
	endif()
	add_compile_options(/openmp:experimental)
	add_compile_options(/O${CMAKE_BUILD_OPTLEVEL})
	add_compile_options(/Zc:__cplusplus)
	add_compile_options(/std:c++latest)
	add_compile_options(/Zc:preprocessor)
	add_compile_options(/W4)
	add_compile_options(/Z7)
	add_compile_options(/w14702)				# Force C4702 (Unreachable code) to be a Level 1 Warning
	add_compile_options(/wd4996)
	add_compile_options(/wd4244)
	add_compile_options(/wd4113)
	add_compile_options(/wd4047)
	add_compile_options(/wd4024)
	add_compile_options(/Gy)					# Function-level linking (puts each function in its own section)
	add_compile_options(/Gw)					# Optimize global data (puts each variable in its own section)
	if (CMAKE_BUILD_TYPE MATCHES ReleaseStrip)
		add_link_options(/DEBUG:NONE)
		add_link_options(/OPT:REF)				# Eliminate unreferenced functions and data
		add_link_options(/VERBOSE:REF)			# Print out everything the linker throws away
	endif()
	if (NOT CMAKE_CROSSCOMPILING)
		add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WIN32)
		if (CMAKE_VS_PLATFORM_NAME MATCHES "x64|Win32")
			# add_compile_options(/Gv)
		endif()
	endif()
	if (CMAKE_BUILD_TYPE MATCHES Debug)
		add_compile_options(/Zi)
		add_link_options(/DEBUG:FULL)
	endif()
	add_link_options(/INCREMENTAL:NO)
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
find_library(SDL2_MAIN_LIBRARY NAMES SDL2main)
set(SDL2_LIBRARIES SDL2::SDL2)
if(SDL2_MAIN_LIBRARY)
    list(APPEND SDL2_LIBRARIES ${SDL2_MAIN_LIBRARY})
endif()
add_compile_definitions(SDL)
include_directories(SDL2::SDL2)

# clean-quake executable
add_executable(clean-quake ${CORE_SRCS})
target_include_directories(clean-quake PRIVATE src)
target_link_libraries(clean-quake PRIVATE ${SDL2_LIBRARIES})
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
