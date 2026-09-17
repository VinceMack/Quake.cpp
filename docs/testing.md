# Testing

The project's central promise is behavioral equivalence with the original engine. Two
test layers protect it. Both run through CTest:

```
cmake --build build
ctest --test-dir build --output-on-failure
```

## Unit tests (`tests/unit_tests.cpp`)

Fast, dependency-free checks of the core foundation layer: byte swapping, the network
message serializer (including the exact bytes on the wire), the token parser, number
parsing, case-insensitive compares, the PAK CRC, and `Vector3` math. They link against
the `quake_engine` static library and need no game data.

Add a test here whenever you touch something that has a precise, externally visible
contract: file formats, protocol encoding, parsing rules.

## Behavioral regression harness (`tests/regression.py`)

The engine accepts `-runframes N`. In that mode it advances exactly `N` frames at the
fixed tick rate, ignoring wall-clock time, then prints one line:

```
STATEHASH frame=300 edicts=145 hash=cf658743e6520fba
```

The hash is an FNV-1a digest over:

- every server entity: its free flag and all QuakeC entity fields (`progs->entityfields`),
- every QuakeC global,
- the server time and entity count,
- every client-side entity's origin, angles, frame, skin, and effects,
- the entire software framebuffer.

`tests/regression.py` runs a set of scenarios (dedicated-server map runs and client demo
playback under SDL's dummy video driver) and compares each digest to the recorded value
in `tests/baselines/`. A one-bit change anywhere in physics, the QuakeC interpreter, the
network parser, model loading, or the rasterizer changes the digest.

Run it directly:

```
python tests/regression.py build/Quake.cpp.exe .              # check against baselines
python tests/regression.py build/Quake.cpp.exe . record       # re-record all baselines
python tests/regression.py build/Quake.cpp.exe . check server_e1m1
```

The scenarios use `-game tests/scratch` so that the user's own `config.cfg` cannot
influence the result, `-nosound -nolan` to remove the audio thread and the network from
the picture, and `id1/pak0.pak`, `pak1.pak` for game data (the retail data files must be
present in `id1/`).

### When a baseline changes

A baseline may only be re-recorded when the change is intentional and understood, and
the commit message must say why. Refactors that claim to preserve behavior must leave
every baseline untouched. Bug fixes that alter gameplay-visible behavior (for example,
restoring a bounds check the original engine had) must re-record and explain.

### Adding a scenario

Add an entry to `CASES` in `tests/regression.py`, run in `record` mode for that case,
and commit the new baseline file.
