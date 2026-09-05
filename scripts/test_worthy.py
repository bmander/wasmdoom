#!/usr/bin/env python3
"""Run tactical AI scenarios against the actual engine, collision map, and IWAD."""
from pathlib import Path
import re
import shutil
import subprocess

root = Path(__file__).resolve().parent.parent
engine = root / 'vendor/doomgeneric/doomgeneric'
objects = re.search(r'^SRC_DOOM = (.+)$', (engine / 'Makefile.emscripten').read_text(), re.M).group(1).split()
excluded = {'doomgeneric_emscripten.o', 'i_sdlmusic.o', 'i_sdlsound.o'}
sources = [str(engine / obj.replace('.o', '.c')) for obj in objects if obj not in excluded]
binary = root / '.cache/worthy-tests'
binary.parent.mkdir(exist_ok=True)
command = [shutil.which('clang') or 'cc', '-O1', '-Wno-incompatible-function-pointer-types',
           '-I' + str(engine), '-DDOOMGENERIC_RESX=1280', '-DDOOMGENERIC_RESY=800',
           *sources, str(root / 'src/r_resolution.c'), str(root / 'src/p_worthy.c'),
           str(root / 'tests/worthy_engine.c'), '-lm', '-o', str(binary)]
subprocess.run(command, check=True, cwd=root)
subprocess.run([str(binary), '-iwad', str(root / 'public/assets/doom1.wad'),
                '-warp', '1', '1', '-skill', '3', '-nosound'], check=True, cwd=root / '.cache')
