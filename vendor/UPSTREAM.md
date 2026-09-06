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

Cover/peek cycles and committed obstacle routes are implemented in
`src/p_worthy.c` and `src/p_worthy_nav.c`. `p_map.c`/`p_local.h` expose a geometry-only
position probe using the original collision rules without touching actors or
activating specials. Route and cover memory remain transient and unserialized.

Offline policy experiments add `p_policy_net.h` and `src/p_policy_net.c`, a small
bounded tanh network, plus optional network decisions in `p_worthy.c`. The browser
embeds a frozen enemy network when the mod is enabled. Parameter defaults preserve the shipped tactics;
`training/` owns the native arena, player bot, optimizer, and evaluation artifacts.

Browser hybrid combat uses deterministic attack scheduling and hitscan spread
hooks in `p_enemy.c`, plus bounded projectile interception in `p_worthy.c`.
Neural decisions still control movement and cover. The override is explicitly
enabled by the browser champion loader; archived offline arena rules stay intact.

Version 4 experiments add optional recurrent inference and structured context in
`p_policy_net.c` and `p_policy_context.c`. Per-actor neural memory in `p_mobj.h`
is transient and cleared by the existing `p_saveg.c` reset; the save format is
unchanged. Legacy networks retain their previous shape and behavior.
