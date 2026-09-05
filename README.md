# DOOM in WebAssembly

[Play in your browser](https://bmander.github.io/wasmdoom/).

The DOOM C engine, compiled locally with Emscripten, running in a plain HTML page.
The generated WebAssembly and the original shareware episode are included: no
build step, package install, or remote service is needed to play.

```sh
python3 scripts/serve.py
# Open http://localhost:8080 and click PLAY DOOM.
```

`npm start` runs the same server. Use `--port 8090` with the Python script to
change the port. Serve over HTTP; opening `index.html` as a file will not work.
To deploy, copy the contents of `public/` to any static web host that serves
`.wasm` as `application/wasm`.
Use HTTPS when hosting remotely (localhost works over HTTP).

## GitHub Pages

The [Pages workflow](.github/workflows/pages.yml) rebuilds the engine with
Emscripten 6.0.7, runs the native AI and browser tests, and deploys `public/` to
https://bmander.github.io/wasmdoom/ on every push to `main`. Pull requests run the
same build and tests without deploying. You can also run the workflow manually
from the repository's Actions tab. Pages uses **GitHub Actions** as its publishing
source in repository Settings → Pages.

The deployment includes the shareware episode, notices, and a freshly generated
corresponding-source archive. All runtime asset URLs are relative, so the game
works under the `/wasmdoom/` project path. Saved games stay with the browser and
site where they were created; localhost saves are separate from the hosted site.

## Playing

- W/S or up/down: move. Left/right arrows: turn. A/D: strafe.
- Ctrl or F: fire. Space or E: use/open. Shift: run.
- Enter: select. Escape: menu. Tab: map. 1–7: weapon.
- Click the game to capture the mouse. Move horizontally to turn, left-click to
  fire, and right-click to use/open. Vertical aim is automatic, as in classic DOOM.
  Adjust mouse sensitivity below the game. Escape releases capture and pauses;
  resume, then click the game to capture again. Keyboard controls also work
  without mouse capture. Use Escape while uncaptured to open the game menu.
- F2: save menu. F3: load menu. F6/F9: DOOM quicksave/quickload (select a slot first).
  Save and Load buttons provide the same menus. Select a slot, type a name, then
  press Enter to save. These are manual savegames, not automatic checkpoints.
- Pause, sound toggle, and fullscreen are available below the game.
- The resolution selector changes the native 3D view live: Classic (320×200),
  Sharp (640×400, default), or High (1280×800). The preference is remembered.
  Changing it while paused takes effect on resume. All modes keep the original
  4:3 display proportions and use the same save slots. Menus, HUD, automap, and
  melt transitions retain their original pixel-art resolution; walls, floors,
  ceilings, and sprites are rasterized at the selected resolution.
- Changing tabs or focusing another application pauses the game.
- **Worthy adversaries** is an optional checkbox you can toggle before or during
  play. The choice is remembered in this browser. Ranged enemies maintain space
  and look for reachable cover with a nearby firing position. They hide briefly,
  peek out, fire a burst, and return to cover, reconsidering after three cycles.
  Cover stops being useful when a new sighting or sound reveals a flanking player.
  Wounded ranged enemies favor more distance.
- All enemies use committed waypoints around nearby obstacles. Demons pursue
  directly without combat strafing; ranged enemies hold useful firing positions
  instead of constantly circling. Nearby allies yield space and avoid reserving
  the same cover spot. Blocked monsters wait or replan instead of cycling through
  backward steps. Movement still uses ordinary DOOM collision and door rules.
- With the mod enabled, sustained visible fire prompts a sidestep after a short
  reaction delay. Enemies avoid obstructed firing lanes; projectiles partially
  lead visible moving players, with a capped prediction that changing direction
  can beat. Invisibility still spoils prediction.
- Enemies acquire players through the normal sight/sound rules. They investigate
  the last observed/heard position for up to eight seconds instead of following
  a player's live coordinates through walls. Ordinary collision, doors, ledges,
  movement speeds, health, damage, pain states, and attack animations still apply.
  Infighting retains the original AI. Built-in demo playback uses classic AI.
- Load your own DOOM or DOOM II IWAD before starting. Files stay in browser
  memory and are never uploaded. Reload to switch games.

Sound effects are supported. MIDI music is disabled because this build does not
bundle an instrument bank. Keyboard required; touch controls and multiplayer
are not implemented. PWAD mods and modern source-port extensions are unsupported.

Save slots persist in IndexedDB after the page or browser closes. Wait for
“Saved in this browser” before closing. Each IWAD's SHA-256 hash has its own six
slots; renaming the same WAD keeps its saves, while changing its contents creates
a separate set. To load a custom WAD's saves, select that WAD again before playing.
Saves belong to this browser profile and site address (including the port).
Clearing site data removes them; private browsing storage may be temporary.
If storage is unavailable, the page says so and saves work for the current
session. Failed writes keep their data in memory and offer a Retry save button.
Worthy adversaries uses the same save slots and save format; its transient
tactical memory is rebuilt after loading, like DOOM's normal target awareness.
The checkbox's current setting governs play after loading any save.

## Rebuilding

Install and activate [Emscripten](https://emscripten.org/docs/getting_started/downloads.html), then:

```sh
python3 scripts/build.py
# or npm run build
```

The script finds `emcc` on PATH or under `~/emsdk`; override with `EMCC`.
The first build downloads Emscripten's SDL2 and SDL2_mixer ports. Build caches
stay in `.cache/`. This build was compiled with Emscripten 6.0.7, SDL 2.32.10,
and SDL_mixer 2.8.0. Outputs: `public/engine/doom.js` and `doom.wasm`.
The build also refreshes the downloadable corresponding-source archive.

`src/doom_browser.c` supplies the canvas framebuffer, key queue, mouse input, clock, and
browser event loop. SDL2_mixer provides sound effects. Asyncify lets the original
engine yield to the browser during waits instead of blocking page interaction.
`public/app.js` mounts the selected IWAD into Emscripten's filesystem and starts
the engine. `public/saves.js` restores saves before startup and snapshots each
completed slot after the engine renames its temporary save file. IndexedDB
transactions persist individual slots immediately, avoiding whole-directory
overwrites from other tabs. A pending-write guard covers leaving the page before
a commit.

`src/r_resolution.c` separates the native 3D framebuffer from the original
320×200 UI. Renderer tables accommodate 1280×800, visplane bounds use 16-bit
coordinates with explicit guard cells, and projection/lighting/weapon scaling
stay consistent between modes. UI drawing masks only the pixels it covers, so
transparent text and menu patches keep the detailed world visible behind them.
Resolution changes are applied between complete engine ticks. The vendored
renderer and video/UI integration have local changes marked in their source
headers; see `vendor/UPSTREAM.md`. The save format is retained.

Optional tactical enemy behavior is implemented in `src/p_worthy.c`, hooked into
enemy chase, shot checks, sound reports, and missile aiming. Small per-monster
memories are initialized on spawn/load and never hold extra object pointers.
`src/p_worthy_nav.c` provides bounded local A* routes and reachable cover searches.
Routes account for body width, steps, drop-offs, and ceiling clearance, with a
global limit of two searches per game tic and 256 expanded nodes per search.
Closed doors remain subject to normal monster door use. Cover visibility probes
use the last observed/heard player position and require a hidden position plus a
short, walkable path to an exposed position. Neighbor spacing uses bounded
blockmap searches; the mod does not need a server, AI service, or extra assets.

Mouse capture uses the browser's [Pointer Lock API](https://developer.mozilla.org/en-US/docs/Web/API/Pointer_Lock_API).
The save adapter uses Emscripten's [filesystem API](https://emscripten.org/docs/api_reference/Filesystem-API.html).

## Offline self-play experiments

The [training laboratory](training/README.md) trains player and enemy policies
against a league of past opponents in the native engine. It supports tactical
parameter search and small neural networks that choose tactics from visible
combat state. Start with `npm run train:league -- --output .cache/parameters`;
the laboratory guide covers neural training, frozen evaluation, and offline replay.
Only Python and a C compiler are needed. Learned policies remain offline;
browser watch mode is a future step. [Experiment results](training/RESULTS.md)
retain both earlier inconclusive runs and the newer comparisons.

## Verification

```sh
npm install
npm test
npm run test:ai
```

Browser tests use an installed Google Chrome locally and Playwright Chromium
under Xvfb in CI, and start a local server if needed.
They exercise the actual Wasm engine, entering a level, keyboard movement,
pause/resume, sound controls, local WAD loading, and download failure handling.
They also exercise mouse capture/turning, restoring a real save's viewpoint after
closing and reopening the page, separate slots for different WADs, unavailable
storage, and recovery from an aborted save transaction.
The resolution test checks for independent pixels within each original-pixel
block, switches all modes live, exercises borders/full view/automap, and loads a
Classic save in High mode. It records native PNGs and rendered frame rate in
`test-results/`. A local Chrome run measured roughly 60/60/49 rendered frames/sec
in the starting room for Classic/Sharp/High; performance varies by scene/device.
The browser suite also exercises the adversary checkbox, persistence, live
toggle, combat, and save/load. `test:ai` uses a local C compiler to run tactical
scenarios against the actual DOOM engine and E1M1 geometry: stock/demo behavior,
ranged retreat, melee pursuit, obstructed firing lanes, spacing, bounded missile
prediction, invisibility, reaction time, and loss of visual contact. They also
run complete cover/fire/return cycles and demon routes around real E1M1 corners,
check that hidden player movement cannot change cover estimates, and verify that
navigation probes never move or damage actors.

## Sources and licensing

- Engine: [DoomGeneric](https://github.com/ozkl/doomgeneric), commit
  `dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284`, included in `vendor/doomgeneric/`.
  It derives from [id Software's DOOM release](https://github.com/id-Software/DOOM)
  and Chocolate Doom/fbDOOM. GPL-2.0-or-later; see `LICENSE` and original notices.
- Browser adapter, page, and scripts: GPL-2.0-or-later, provided under `LICENSE`.
- Shareware game data: downloaded unchanged from the
  [SDL DOOM data archive](https://www.libsdl.org/projects/doom/data/doom1.wad.gz).
  SHA-256: `bb449c7480e9a02a62012d041406e8e43daa51caa0650646d1307d8650b8f837`.
  This is id Software's copyrighted shareware data, **not GPL/open-source game
  assets**. No registered/commercial episodes are included.
- SDL2 and SDL2_mixer: zlib licenses, linked by Emscripten. Emscripten:
  MIT and University of Illinois/NCSA licenses. Their notices are included in
  `public/licenses/`.

The page's credits link includes the GPL license and an archive of the complete
engine, adapter, page source, and build scripts. Refresh it with the build script
when changing the application. DOOM is a trademark of id Software; this is an
unofficial browser port.
