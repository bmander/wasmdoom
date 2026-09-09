Source: https://github.com/ozkl/doomgeneric
Revision: dcb7a8dbc7a16ce3dda29382ac9aae9d77d21284
Retrieved: 2026-09-04

Mirror, Mirror (2026-09-07) reflects the world framebuffer through
`src/r_resolution.c`, before readable UI composition. `g_game.c` reverses
horizontal player commands, `am_map.c` reflects the automap and pan direction
while preserving marker text, and `s_sound.c` reverses stereo separation.
Map data and save coordinates stay in their original format.

The optional Rocket Marines mod (2026-09-07) adds `p_mods.h` and a toggle in
`p_enemy.c` that replaces pistol-zombie shots with standard rockets. It defaults
off, bypasses demos and network games, and leaves the save format unchanged.
Worthy Adversaries treats these marines as projectile enemies when enabled.
`p_inter.c` gives killed rocket marines a standard rocket-launcher drop,
including normal pickup ammo, in place of their usual clip. Other enemy drops
and classic/demo behavior remain unchanged.

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

Cover/peek cycles and committed obstacle routes are implemented in
`src/p_worthy.c` and `src/p_worthy_nav.c`. `p_map.c`/`p_local.h` expose a geometry-only
position probe using the original collision rules without touching actors or
activating specials. Route and cover memory remain transient and unserialized.

Offline policy experiments add `p_policy_net.h` and `src/p_policy_net.c`, a small
bounded tanh network, plus optional network decisions in `p_worthy.c`. The browser
embeds a frozen enemy network when the mod is enabled. Parameter defaults preserve the shipped tactics;
`training/` owns the native arena, player bot, optimizer, and evaluation artifacts.

Browser hybrid combat uses deterministic attack scheduling in `p_enemy.c`,
plus bounded projectile interception in `p_worthy.c`. Hitscan attacks retain
vanilla random spread, pellet counts, and damage; Worthy changes firing decisions
and positioning without modifying the weapons.
Neural decisions still control movement and cover. The override is explicitly
enabled by the browser champion loader; archived offline arena rules stay intact.

Version 4 experiments add optional recurrent inference and structured context in
`p_policy_net.c` and `p_policy_context.c`. Per-actor neural memory in `p_mobj.h`
is transient and cleared by the existing `p_saveg.c` reset; the save format is
unchanged. Legacy networks retain their previous shape and behavior.

OPL music (2026-09-08) restores the game's own soundtrack. `opl/`,
`i_oplmusic.c`, `midifile.c`, and `midifile.h` come from Chocolate Doom 3.0.1
(GPL-2.0-or-later, original notices retained); doomgeneric had dropped them.
`i_sound.c` selects a music module by `snd_musicdevice` as Chocolate Doom does,
so the default Sound Blaster setting reaches the OPL player, and starts it only
after the WAD is loaded, since its instruments come from the GENMIDI lump.
`s_sound.c` picks the DMX driver version to emulate from the game version.
`doomtype.h` regains `PACKED_STRUCT`, `i_system.c` regains `I_Realloc`, and
`i_swap.h`'s little-endian WAD macros are supplemented in `midifile.c` by SDL's
byte swapping for big-endian MIDI fields.

Two changes are specific to the browser. `opl/opl.c` registers the project's
AudioWorklet driver (`src/opl_worklet.c`) in place of the SDL driver under
Emscripten, and `opl_internal.h` gains an optional `delay_func` driver hook so
that a driver with a virtual clock can satisfy chip detection's delay without
waiting on a callback; other drivers leave it NULL and keep the upstream path.
`opl/opl_sdl.c` is retained unmodified but not compiled for the browser, and
`opl/opl3.c` is compiled on its own for the audio thread. `i_oplmusic.c` treats
a missing GENMIDI lump as "no music" instead of a fatal lump lookup, because
the player supplies the WAD.
