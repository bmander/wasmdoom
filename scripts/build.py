#!/usr/bin/env python3
"""Compile the vendored DOOM engine and browser adapter with Emscripten."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import sys

ROOT = Path(__file__).resolve().parent.parent
ENGINE = ROOT / 'vendor/doomgeneric/doomgeneric'
emcc = os.environ.get('EMCC') or shutil.which('emcc')
if not emcc:
    candidate = Path.home() / 'emsdk/upstream/emscripten/emcc'
    if candidate.exists():
        emcc = str(candidate)
if not emcc:
    raise SystemExit('Emscripten is required. Activate emsdk or set EMCC to its emcc executable.')

# Keep the upstream source list, replacing only the platform adapter.
makefile = (ENGINE / 'Makefile.emscripten').read_text()
objects = re.search(r'^SRC_DOOM = (.+)$', makefile, re.M).group(1).split()
sources = [str(ENGINE / obj.replace('.o', '.c')) for obj in objects
           if obj != 'doomgeneric_emscripten.o']
env = os.environ.copy()
env.setdefault('EM_CACHE', str(ROOT / '.cache/emscripten'))
env.setdefault('EM_PORTS', str(ROOT / '.cache/ports'))
(ROOT / 'public/engine').mkdir(parents=True, exist_ok=True)
command = [emcc, *sources, str(ROOT / 'src/doom_browser.c'), str(ROOT / 'src/r_resolution.c'), str(ROOT / 'src/p_worthy.c'), str(ROOT / 'src/p_worthy_nav.c'),
           '-I' + str(ENGINE), '-O2', '-DFEATURE_SOUND',
           '-DDOOMGENERIC_RESX=1280', '-DDOOMGENERIC_RESY=800',
           '-Wno-incompatible-function-pointer-types',
           '-sUSE_SDL=2', '-sUSE_SDL_MIXER=2', '-sSDL2_MIXER_FORMATS=[]',
           '-sASYNCIFY=1', '-sALLOW_MEMORY_GROWTH=1', '-sSTACK_SIZE=1048576',
           '-sMODULARIZE=1', '-sEXPORT_ES6=1', '-sEXPORT_NAME=createDoom',
           '-sENVIRONMENT=web', '-sFORCE_FILESYSTEM=1', '-sEXIT_RUNTIME=1',
           '-sEXPORTED_RUNTIME_METHODS=["FS","callMain"]',
           '-sEXPORTED_FUNCTIONS=["_main","_doom_key","_doom_pause","_doom_mute","_doom_mouse","_doom_release_mouse","_doom_text_input","_doom_resolution","_doom_worthy"]',
           '-lm', '-o', str(ROOT / 'public/engine/doom.js')]
if '--source-only' not in sys.argv:
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    print('Built public/engine/doom.js and doom.wasm')

licenses = ROOT / 'public/licenses'
licenses.mkdir(exist_ok=True)
shutil.copyfile(ROOT / 'vendor/doomgeneric/LICENSE', ROOT / 'LICENSE')
shutil.copyfile(ROOT / 'LICENSE', licenses / 'GPL-2.0.txt')
for port, filename in [('sdl2', 'SDL2.txt'), ('sdl2_mixer', 'SDL2_mixer.txt')]:
    notice = next((Path(env['EM_PORTS']) / port).glob('*/LICENSE.txt'))
    shutil.copyfile(notice, licenses / filename)
shutil.copyfile(Path(emcc).resolve().parent / 'LICENSE', licenses / 'emscripten.txt')

def source_filter(info):
    if '.git' in Path(info.name).parts:
        return None
    return info

with tarfile.open(ROOT / 'public/source.tar.gz', 'w:gz') as archive:
    for item in ['vendor', 'src', 'scripts', 'tests', 'training', 'README.md', 'LICENSE',
                 'package.json', 'package-lock.json', 'playwright.config.js', '.gitignore', '.github']:
        if (ROOT / item).exists():
            archive.add(ROOT / item, arcname='wasmdoom/' + item, filter=source_filter)
    for item in ['index.html', 'style.css', 'app.js', 'saves.js', 'wad.js', 'about.html', 'licenses', 'assets']:
        archive.add(ROOT / 'public' / item, arcname='wasmdoom/public/' + item)
print('Updated notices and public/source.tar.gz')
