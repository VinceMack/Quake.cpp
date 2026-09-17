# Quake.cpp

A fork of [CleanQuake](https://github.com/klaussilveira/clean-quake) (itself derived from
SDLQuake 1.0.9) being rebuilt as a modern C++20 engine for studying game engine
architecture. The game it runs is Quake, unchanged.

## Goals

1. **Behavioral equivalence, verified.** The engine must be indistinguishable from the
   original from the player's and the mod author's point of view. See
   [Compatibility](#compatibility) for the precise definition and
   [docs/testing.md](docs/testing.md) for how it is checked on every build.
2. **Readable, single-responsibility modules.** A new reader should be able to explain
   any one file in about five minutes. Fewer lines are welcome when they come from
   removing dead code and duplication, never from packing statements together.
3. **Clean subsystem boundaries.** Platform, core, QuakeC VM, network transport, server,
   client, UI, audio, and rendering are separate layers with an acyclic include graph.
   The renderer boundary is designed so that a hardware backend can be added later.
4. **Modern C++ over 1990s C.** Standard library containers and RAII ownership instead
   of hand-rolled arena allocators and CRT shims; `constexpr` and enums instead of
   macros; explicit casts; no exceptions to the type system.
5. **No DOS-era baggage.** Anything that only made sense on a 1996 PC (fixed heap sizes,
   shareware detection, modem and IPX networking, 16-bit color paths) is removed rather
   than modernized.

## Compatibility

"100% compatible" means compatible as observed by a player or a mod. The following are
frozen and protected by the regression harness:

- **Network protocol 15**, byte for byte, in both directions.
- **File formats**: `.pak`, `.bsp` (v29), `.mdl`, `.spr`, `.wad`, `.lmp`, `.wav`,
  `progs.dat` (v6), `.sav`, `.dem`, and `.cfg`.
- **QuakeC semantics**: the interpreter, every builtin, the entity field layout, and
  the zero-initialization of entity and global memory.
- **Movement and physics math**, including its floating-point quirks.
- **The console vocabulary** that stock progs and mods drive through `stuffcmd`: cvar
  names (including `registered`, which always reads 1) and command names.

Everything else, from console messages to memory limits to the layout of the
multiplayer menu, may change.

## Building

Requires CMake 3.20+, a C++20 compiler, and Python 3 for the regression harness. SDL2
is fetched automatically.

```
cmake -S . -B build
cmake --build build
```

Place the retail `id1/pak0.pak` and `pak1.pak` in `id1/` next to the executable (or pass
`-basedir`). Then:

```
build/Quake.cpp -winsize 1280 960
ctest --test-dir build --output-on-failure
```

## Layout

```
src/platform   OS and SDL glue: entry point, file I/O, timers, input pump
src/core       Foundation: types, strings, math, cvars, commands, filesystem, messages
src/vm         QuakeC virtual machine: program loading, interpreter, edicts, builtins
src/network    Transport: loopback and UDP datagram drivers, reliable messaging
src/server     Game server: world, physics, client messaging
src/client     Client simulation: prediction, parsing, demos, view, input
src/ui         Console, menus, HUD
src/audio      Mixer, WAV loading, SDL audio output
src/world      Model formats and collision hulls shared by client and server
src/render     Renderer interface and the software rasterizer
tests/         Unit tests and the behavioral regression harness
docs/          Design notes
```
