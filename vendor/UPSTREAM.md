Source: https://github.com/ozkl/doomgeneric
Revision: dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284
Retrieved: 2026-09-04

The original repository notices are retained. Local changes dated 2026-09-05
add native 320×200, 640×400, and 1280×800 rendering:

- Renderer buffers/tables, projection, lighting, visplane bounds, and weapon scaling.
- Composition of the native 3D view with the original 320×200 UI, including
  transparent overlays, screen borders, automap, and classic melt transitions.
- Video output emits native pixels directly instead of duplicating VGA pixels.

Changed engine files have a WasmDOOM notice at the top. The browser platform
adapter and framebuffer compositor live in `src/doom_browser.c` and
`src/r_resolution.c` at the project root. `r_resolution.h` is a new engine header.
The optional Worthy Adversaries mod adds chase, sound-report, shot-lane, and
missile-aim hooks in `p_enemy.c` and `p_mobj.c`. Tactical decisions live in
`src/p_worthy.c`. `p_mobj.h` contains transient tactical memory, zeroed on load in
`p_saveg.c`; it is not serialized, preserving the original save format. The mod
defaults off and is bypassed for demo playback/recording and network games.
