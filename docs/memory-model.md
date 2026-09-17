# Memory model

## What the original engine did

Quake (1996) managed almost all of its memory by hand inside one fixed block that the
platform layer allocated at startup (`-mem`, default 8 MB). Three allocators lived in
that block:

- **Hunk.** A two-ended stack. Level data (the BSP, `progs.dat`, edicts, lightstyles)
  was pushed on the low end; the video z-buffer and surface cache on the high end. A
  level change simply reset the low mark (`Hunk_FreeToLowMark`), freeing everything the
  level had allocated in one step. Every allocation was 16-byte aligned, zeroed, and
  tagged with an 8-character name for the `hunk` report.
- **Zone.** A small first-fit heap (`Z_Malloc`, 48 KB by default) for long-lived
  variable-size objects such as console-variable strings and command aliases.
- **Cache.** The region between the two hunk ends. Alias models and sounds were loaded
  here and could be evicted at any time, least-recently-used first, when the hunk grew
  into them. Callers had to re-check (`Cache_Check`) before every use and reload on a
  miss (`Mod_Extradata`).

This design was the right answer for an 8 MB machine: no fragmentation, a whole level
freed in constant time, and assets that fit into whatever memory the level left over.
It is worth studying for that reason. It is also invisible to the player: no mod, save
file, demo, or network packet depends on it.

## What this engine does

The arena and all three allocators are gone. Each piece of data has an owner whose
lifetime is the one the Hunk used to model:

| Data                            | Owner                                             |
|---------------------------------|---------------------------------------------------|
| `progs.dat`, runtime strings    | `VM` (`progs_data`, `pr_created_strings`)         |
| edicts                          | `server_t::edicts_storage`                        |
| savegame lightstyles            | `server_t::loaded_lightstyles`                    |
| BSP data                        | `BrushModelData`, shared by a map and its submodels |
| alias model frames              | `model_t::alias_data`                             |
| sprite frames                   | `model_t::sprite_data`                            |
| sounds                          | `sfx_t::data`                                     |
| cached 2D pictures              | `Draw::CachePic::data`                            |
| gfx.wad, palette, colormap      | file-scope vectors in their subsystems            |
| z-buffer and surface cache      | `Vid::video_storage`                              |
| network and client message buffers | fixed arrays next to their `sizebuf_t`         |

Level lifetime is expressed by the owner being reassigned when the next level loads,
not by a global free. Assets are never evicted; modern machines make the cache's
memory pressure logic unnecessary, and removing it also removed a data race between
the audio thread and the loader over the cache's LRU list.

Two invariants carried over from the Hunk still matter:

- **Zeroed memory.** QuakeC assumes freshly allocated entity and global memory is zero.
  Every owner above is value-initialized.
- **Stable addresses.** Dozens of structures hold raw pointers into loaded data. Owning
  containers are sized once at load time and never resized afterwards. `known_sfx`
  reserves its maximum up front for the same reason.

The one place that still uses a bump allocator is `Mod_LoadAliasModel`. The alias frame
block is self-relative (every internal reference is a byte offset), so it is built in an
`AliasArena` sized exactly by `Mod_AliasModelMemorySize`, which walks the file the same
way the loader does. If the loader's allocation pattern changes, the size function must
change with it; the arena checks the two agree.

## Limits that used to be memory limits

`-mem`, `-minmemory`, `-zone` and the "megabyte heap" startup line no longer exist.
Large maps and mods that failed on the original engine with "Hunk_Alloc: failed" load
here as long as the machine has memory. Compile-time limits such as `MAX_EDICTS`,
`MAX_MODELS` and `MAX_SOUNDS` are unchanged because they are part of the network
protocol.
