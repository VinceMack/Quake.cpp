# Architecture

Quake is a client/server engine even in single player: the local game runs a server and
a client in the same process, connected through an in-memory loopback "network" driver.
Everything below follows from that.

## Layers

```
platform      main loop, file I/O, timers, SDL event pump, CRT shims
core          types, math, strings, cvars, commands, PAK filesystem, message buffers
vm            QuakeC: progs.dat loader, interpreter, entity dictionary, builtins
world         model formats (BSP/MDL/SPR) and collision hulls
network       loopback and UDP transports, reliable/unreliable messaging
server        world links, physics, client messaging (uses vm, world, network, core)
client        connection, protocol parsing, prediction, demos, view (uses world, network, render)
ui            console, menus, HUD (uses client, render)
audio         mixer and SDL output (uses core, world for ambient levels)
render        renderer interface, 2D drawing, software rasterizer
host          ties the layers together: init order, the frame, error recovery
```

The intended dependency direction is top to bottom in this list, with `host` allowed to
see everything. Each translation unit includes only the headers it uses; there is no
umbrella header. `quakedef.hpp` holds game-wide constants (stats, item bits, protocol
limits) and nothing else.

## A frame

`Host_Frame` in [host.cpp](../src/host/host.cpp) is the whole engine loop:

1. `Host_FilterTime` decides whether enough real time has passed; frames are capped at 72 Hz
   and the frame length is clamped to [1 ms, 100 ms].
2. Input: SDL events are pumped, the command buffer is executed, the network is polled.
3. Client sends its movement command to the server (through loopback for local play).
4. Server frame: accept connections, run client commands, run physics and QuakeC
   thinks, send entity updates.
5. Client reads server messages, interpolates entities, updates temp entities.
6. Screen update: the view is set up, the world and entities are rasterized, 2D overlays
   are drawn, the framebuffer is presented.
7. Audio: listener position is pushed to the mixer thread.

`Host_Error` throws `HostAbort`; `Host_Frame` catches it and the loop continues at the
console. Only `Sys_Error` terminates the process.

## Where state lives

Quake's state is global, and this engine keeps that honest rather than hiding it:

- `Server::sv` and `Server::svs`: the running level and the connected clients.
- `Client::cl` and `Client::cls`: the client's view of the level and its connection.
- `VM::progs`, `pr_globals`, `pr_global_struct`: the loaded QuakeC program and its globals.
- `Render::r_refdef`, `Vid::vid`: the view definition and the framebuffer.

Ownership of the memory behind these is described in [memory-model.md](memory-model.md).

## The QuakeC boundary

The server's game logic is not in this codebase; it is bytecode in `progs.dat` executed
by [interpreter.cpp](../src/vm/interpreter.cpp). The engine and QuakeC meet at three
points, and all three are frozen by the compatibility promise:

- `entvars_t` / `globalvars_t` in [edict.hpp](../src/vm/edict.hpp): the field layout the
  compiled progs expect. `PROGHEADER_CRC` guards it.
- The builtin table in [builtins.cpp](../src/vm/builtins.cpp): the functions QuakeC can
  call into the engine.
- Zero-initialized entity memory and the edict numbering scheme, which mods observe.

## The renderer boundary

There is no renderer interface class yet, on purpose. An earlier `IRenderer` had a
single caller and no second implementation, so it described nothing; it was removed
rather than kept as decoration. The boundary is instead documented here as the exact
set of things that cross it today, so that a hardware backend can be designed against
the data rather than against the software rasterizer's call shapes:

- **2D drawing**: `Draw_Pic`, `Draw_TransPic`, `Draw_TransPicTranslate`, `Draw_Character`,
  `Draw_String`, `Draw_Fill`, `Draw_TileClear`, `Draw_FadeScreen`, `Draw_ConsoleBackground`,
  `Draw_CachePic`, `Draw_PicFromWad`, and the loading-disc indicator.
- **Scene submission**: `R_RenderView` with `r_refdef` (view origin, angles, FOV,
  viewport) plus the client entity list (`cl_visedicts`), dynamic lights
  (`R_PushDlights`), particles (`R_RocketTrail`, `R_ParticleExplosion`,
  `R_RunParticleEffect`, ...), and lightstyles.
- **World residency**: `R_NewMap`, and entity fragments (`R_AddEfrags`, `R_RemoveEfrags`)
  that link static entities into BSP leaves.
- **Presentation**: `VID_Init`, `VID_Update`, `VID_ShiftPalette`, `VID_Shutdown`, and
  the `vid` description of the framebuffer that the 2D code writes into directly.

The last item is the real obstacle: the console, HUD and menu draw by poking bytes into
`vid.buffer`. A hardware backend needs those to become draw commands first. Once a
second backend exists, the interface should be extracted from this list.
