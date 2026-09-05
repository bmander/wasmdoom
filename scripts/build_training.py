#!/usr/bin/env python3
"""Build the native self-play arena; no renderer, Python packages, or GPU needed."""
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
engine = root / 'vendor/doomgeneric/doomgeneric'
objects = re.search(r'^SRC_DOOM = (.+)$', (engine / 'Makefile.emscripten').read_text(), re.M).group(1).split()
excluded = {'doomgeneric_emscripten.o', 'i_sdlmusic.o', 'i_sdlsound.o'}
sources = [str(engine / obj.replace('.o', '.c')) for obj in objects if obj not in excluded]
binary = root / '.cache/selfplay-engine'
binary.parent.mkdir(exist_ok=True)
command = [shutil.which('clang') or 'cc', '-O2', '-Wno-incompatible-function-pointer-types',
           '-I' + str(engine), '-I' + str(root / 'training'),
           '-DDOOMGENERIC_RESX=1280', '-DDOOMGENERIC_RESY=800', *sources,
           *[str(root / path) for path in ['src/r_resolution.c', 'src/p_worthy.c',
             'src/p_policy_net.c', 'src/p_worthy_nav.c', 'training/player_bot.c', 'training/arena.c']],
           '-lm', '-o', str(binary)]
subprocess.run(command, check=True, cwd=root)
print(binary)
