# Quake.cpp

Modern C++20 rebuild of SDLQuake. Goal one is behavioral equivalence with the original
engine as a player or mod would observe it; see README.md for the exact definition.

## Build and test

```
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

MinGW g++ is the configured toolchain in `build/`. Game data (`id1/pak0.pak`, `pak1.pak`)
must be present for the regression harness.

## Rules that protect compatibility

- Any change to physics, the QuakeC VM, protocol parsing, model loading or the
  rasterizer must leave every digest in `tests/baselines/` unchanged. If a digest
  changes, the commit message must say why the behavior change is intended. Re-record
  with `python tests/regression.py build/Quake.cpp.exe . record`. See docs/testing.md.
- Cvar names, console command names, the `entvars_t`/`globalvars_t` layout, the builtin
  table, and zero-initialized entity memory are part of the mod interface. Do not rename
  or reorder them.
- Add a regression scenario when touching a code path the existing ones do not cover.

## Conventions

- `.clang-format` is authoritative: one statement per line, 4 spaces, 120 columns,
  function braces on their own line. Do not pack statements to save lines.
- No `using namespace` at file scope. Qualify names at the use site.
- No umbrella headers. Each file includes exactly what it uses; `quakedef.hpp` is game
  constants only. Core must not include ui, host, server, network or render.
- Ownership is explicit (`std::vector`, `std::unique_ptr`, `std::shared_ptr`). There is
  no arena allocator; see docs/memory-model.md for what owns what.
- Recoverable errors go through `Host_Error` (throws `HostAbort`, caught in
  `Host_Frame`). `Sys_Error` is fatal.

## Layout

See README.md ("Layout") and docs/architecture.md.
