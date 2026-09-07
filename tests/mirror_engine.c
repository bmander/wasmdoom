// Mirror scenarios against the actual renderer, automap and input commands.
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "doomstat.h"
#include "g_game.h"
#include "m_controls.h"
#include "p_local.h"
#include "r_local.h"
#include "r_resolution.h"
#include "i_video.h"
#include "am_map.h"

extern void R_ExecuteSetViewSize(void);
static byte original[MAX_RENDERWIDTH * MAX_RENDERHEIGHT];

static void mirror_mode(int enabled)
{
    R_RequestMirror(enabled);
    R_ApplyResolution();
}

static void key_event(int key, boolean pressed)
{
    event_t event = {0};
    event.type = pressed ? ev_keydown : ev_keyup;
    event.data1 = key;
    G_Responder(&event);
}

void Test_Mirror(void)
{
    G_InitNew(sk_medium, 1, 1);
    player_t *player = &players[consoleplayer];
    player->viewz = player->mo->z + player->viewheight;

    // Include a monster and an item in the view, with asymmetric placement.
    angle_t angle = player->mo->angle >> ANGLETOFINESHIFT;
    fixed_t fx = finecosine[angle], fy = finesine[angle];
    P_SpawnMobj(player->mo->x + fx * 80 - fy * 24,
                player->mo->y + fy * 80 + fx * 24, ONFLOORZ, MT_POSSESSED);
    P_SpawnMobj(player->mo->x + fx * 64 + fy * 24,
                player->mo->y + fy * 64 - fx * 24, ONFLOORZ, MT_CLIP);

    for (int scale = 1; scale <= 4; scale *= 2) {
        for (int blocks = 8; blocks <= 11; blocks++) {
            mirror_mode(0);
            R_RequestResolution(scale);
            R_ApplyResolution();
            R_SetViewSize(blocks, 0);
            R_ExecuteSetViewSize();
            R_BeginDisplay();
            R_RenderPlayerView(player);
            memcpy(original, render_buffer, RENDERWIDTH * RENDERHEIGHT);

            mirror_mode(1);
            R_BeginDisplay();
            R_RenderPlayerView(player);
            int asymmetric = 0;
            for (int y = viewwindowy; y < viewwindowy + viewheight; y++) {
                for (int x = 0; x < scaledviewwidth; x++) {
                    int offset = y * RENDERWIDTH + viewwindowx + x;
                    int reflected = y * RENDERWIDTH + viewwindowx + scaledviewwidth - 1 - x;
                    assert(render_buffer[offset] == original[reflected]);
                    asymmetric += original[offset] != original[reflected];
                }
            }
            assert(asymmetric > 100);

            // A readable UI pixel must cover the mirrored world at every scale.
            I_VideoBuffer[40 * 320 + 160] = 199;
            R_OverlayRect(160, 40, 1, 1);
            byte *composite = R_CompositeFrame();
            for (int y = 40 * scale; y < 41 * scale; y++)
                for (int x = 160 * scale; x < 161 * scale; x++)
                    assert(composite[y * RENDERWIDTH + x] == 199);

            mirror_mode(0);
            R_BeginDisplay();
            R_RenderPlayerView(player);
            assert(!memcmp(original, render_buffer, RENDERWIDTH * RENDERHEIGHT));
        }
    }
    puts("PASS: Mirror reflects real world pixels at all resolutions and view sizes, preserves UI overlays, and reverses cleanly");

    // Reflection applies atomically with command mapping; held keys need no
    // rebinding and forward/fire/use retain their normal meanings.
    ticcmd_t classic, pending, reflected, idle;
    key_event(key_straferight, true);
    key_event(key_up, true);
    key_event(key_fire, true);
    key_event(key_use, true);
    G_BuildTiccmd(&classic, 0);
    R_RequestMirror(1);
    assert(!R_MirrorEnabled());
    G_BuildTiccmd(&pending, 0);
    assert(pending.sidemove == classic.sidemove);
    R_ApplyResolution();
    G_BuildTiccmd(&reflected, 0);
    assert(classic.sidemove > 0 && reflected.sidemove == -classic.sidemove);
    assert(reflected.forwardmove == classic.forwardmove && classic.forwardmove > 0);
    assert(reflected.buttons == classic.buttons);
    key_event(key_straferight, false);
    key_event(key_up, false);
    key_event(key_fire, false);
    key_event(key_use, false);
    G_BuildTiccmd(&idle, 0);
    assert(!idle.sidemove && !idle.forwardmove && !idle.buttons);

    for (int enabled = 0; enabled < 2; enabled++) {
        mirror_mode(enabled);
        key_event(key_right, true);
        G_BuildTiccmd(enabled ? &reflected : &classic, 0);
        key_event(key_right, false);
        G_BuildTiccmd(&idle, 0); // Reset the turn acceleration between samples.
    }
    assert(classic.angleturn < 0 && reflected.angleturn == -classic.angleturn);
    for (int enabled = 0; enabled < 2; enabled++) {
        mirror_mode(enabled);
        event_t mouse = {0};
        mouse.type = ev_mouse;
        mouse.data2 = 20;
        G_Responder(&mouse);
        G_BuildTiccmd(enabled ? &reflected : &classic, 0);
    }
    assert(classic.angleturn < 0 && reflected.angleturn == -classic.angleturn);
    puts("PASS: Mirror maps keyboard turning, held strafing and mouse motion while preserving forward, fire and use");

    mirror_mode(0);
    key_event(key_map_toggle, true);
    key_event(key_map_toggle, false);
    assert(automapactive);
    AM_Ticker();
    AM_Drawer();
    memcpy(original, I_VideoBuffer, 320 * 200);
    mirror_mode(1);
    AM_Drawer();
    for (int y = 0; y < 168; y++)
        for (int x = 0; x < 320; x++)
            assert(I_VideoBuffer[y * 320 + x] == original[y * 320 + 319 - x]);
    assert(!memcmp(I_VideoBuffer + 168 * 320, original + 168 * 320, 32 * 320));
    AM_Stop();
    mirror_mode(0);
    R_RequestResolution(2);
    R_ApplyResolution();
    R_SetViewSize(10, 0);
    R_ExecuteSetViewSize();
    puts("PASS: Mirror reflects the real automap while leaving the status-bar region untouched");
}
