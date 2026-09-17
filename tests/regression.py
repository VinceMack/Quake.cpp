#!/usr/bin/env python3
"""Behavioral regression harness for Quake.cpp.

Each case launches the engine with -runframes N, which advances exactly N frames at the
fixed tick rate and prints a STATEHASH line digesting every entity field, every QuakeC
global, the client entity list, and the software framebuffer. The digest must match the
recorded baseline byte for byte. Any change to physics, the QuakeC VM, the network
protocol parser, or the rasterizer shows up as a mismatch.

Usage:
    regression.py <engine-exe> <repo-root> [check|record] [case ...]
"""
import os
import pathlib
import re
import subprocess
import sys

STATEHASH = re.compile(r"STATEHASH frame=(\d+) edicts=(\d+) hash=([0-9a-f]{16})")

COMMON = ["-nosound", "-nolan", "-game", "tests/scratch"]

CASES = {
    "server_e1m1": ["-dedicated", "1", "+map", "e1m1", "-runframes", "300"],
    "server_start": ["-dedicated", "1", "+map", "start", "-runframes", "200"],
    "server_e2m1": ["-dedicated", "1", "+map", "e2m1", "-runframes", "300"],
    "client_demo1": ["-winsize", "320", "240", "+playdemo", "demo1", "-runframes", "240"],
    "client_demo2": ["-winsize", "320", "240", "+playdemo", "demo2", "-runframes", "240"],
    # Level change: reloads progs.dat, the BSP and its submodels while the server is live.
    "server_changelevel": ["-dedicated", "1", "+map", "e1m1", "+wait", "+changelevel", "e1m2", "-runframes", "300"],
    # Listen server with a local client: server, client, and rasterizer together.
    "listen_e1m1": ["-winsize", "320", "240", "+map", "e1m1", "-runframes", "200"],
}


def run_case(exe: str, root: pathlib.Path, name: str) -> str:
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    args = [exe, "-basedir", str(root)] + COMMON + CASES[name]
    proc = subprocess.run(args, cwd=root, env=env, capture_output=True, text=True, timeout=300)
    match = STATEHASH.search(proc.stdout)
    if not match:
        tail = "\n".join((proc.stdout + proc.stderr).splitlines()[-25:])
        raise RuntimeError(f"{name}: no STATEHASH in output (exit {proc.returncode})\n{tail}")
    return match.group(0)


def main() -> int:
    exe = str(pathlib.Path(sys.argv[1]).resolve())
    root = pathlib.Path(sys.argv[2]).resolve()
    mode = sys.argv[3] if len(sys.argv) > 3 else "check"
    names = sys.argv[4:] or list(CASES)
    (root / "tests" / "scratch").mkdir(exist_ok=True)
    baselines = root / "tests" / "baselines"
    baselines.mkdir(exist_ok=True)

    failures = 0
    for name in names:
        try:
            result = run_case(exe, root, name)
        except (RuntimeError, subprocess.TimeoutExpired) as error:
            print(f"ERROR  {name}: {error}")
            failures += 1
            continue
        baseline_file = baselines / f"{name}.txt"
        if mode == "record":
            baseline_file.write_text(result + "\n")
            print(f"RECORD {name}: {result}")
            continue
        expected = baseline_file.read_text().strip() if baseline_file.exists() else "<missing>"
        if result == expected:
            print(f"PASS   {name}: {result}")
        else:
            print(f"FAIL   {name}:\n  expected {expected}\n  actual   {result}")
            failures += 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
