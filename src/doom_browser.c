// Browser platform adapter for DoomGeneric. SPDX-License-Identifier: GPL-2.0-or-later
#include "doomgeneric.h"
#include "d_event.h"
#include "m_controls.h"
#include "r_resolution.h"
#include "p_worthy.h"
#include <emscripten.h>
#include <SDL.h>
#include <SDL_mixer.h>

#define QUEUE_SIZE 256
static unsigned short keys[QUEUE_SIZE];
static unsigned int read_index, write_index;
static int is_paused, is_muted;
static double paused_at, paused_time;
static int mouse_dx, mouse_buttons, mouse_dirty;

// Accumulate high-frequency browser events until DOOM polls its input.
EMSCRIPTEN_KEEPALIVE void doom_mouse(int dx, int buttons)
{
    mouse_dx += dx;
    if (mouse_dx > 4096) mouse_dx = 4096;
    if (mouse_dx < -4096) mouse_dx = -4096;
    mouse_buttons = buttons & 3;
    mouse_dirty = 1;
}

EMSCRIPTEN_KEEPALIVE void doom_release_mouse(void)
{
    mouse_dx = mouse_buttons = 0;
    mouse_dirty = 1;
}

EMSCRIPTEN_KEEPALIVE int doom_text_input(void)
{
    extern int saveStringEnter;
    return saveStringEnter;
}

EMSCRIPTEN_KEEPALIVE void doom_key(int key, int pressed)
{
    unsigned int next = (write_index + 1) % QUEUE_SIZE;
    if (next == read_index) return;
    keys[write_index] = ((pressed != 0) << 8) | (key & 255);
    write_index = next;
}

EMSCRIPTEN_KEEPALIVE void doom_pause(int value)
{
    if (value && !is_paused) paused_at = emscripten_get_now();
    if (!value && is_paused) paused_time += emscripten_get_now() - paused_at;
    is_paused = value;
    Mix_MasterVolume((is_paused || is_muted) ? 0 : MIX_MAX_VOLUME);
}

EMSCRIPTEN_KEEPALIVE void doom_mute(int value)
{
    is_muted = value;
    Mix_MasterVolume((is_paused || is_muted) ? 0 : MIX_MAX_VOLUME);
}

void DG_Init(void) {}

EMSCRIPTEN_KEEPALIVE void doom_resolution(int scale) { R_RequestResolution(scale); }
EMSCRIPTEN_KEEPALIVE void doom_worthy(int enabled) { P_WorthySetEnabled(enabled); }

EM_JS(void, draw_frame, (const void *pixels, int width, int height), {
    if (!Module.frameImage || Module.frameImage.width !== width || Module.frameImage.height !== height) {
        Module.canvas.width = width;
        Module.canvas.height = height;
        Module.frameContext = Module.canvas.getContext('2d', {alpha: false});
        Module.frameImage = Module.frameContext.createImageData(width, height);
    }
    const target = Module.frameImage.data;
    for (let i = 0; i < target.length; i += 4) {
        target[i] = HEAPU8[pixels + i + 2];
        target[i + 1] = HEAPU8[pixels + i + 1];
        target[i + 2] = HEAPU8[pixels + i];
        target[i + 3] = 255;
    }
    Module.frameContext.putImageData(Module.frameImage, 0, 0);
    Module.onFrame?.(width, height);
});

void DG_DrawFrame(void) { draw_frame(DG_ScreenBuffer, RENDERWIDTH, RENDERHEIGHT); }
void DG_SleepMs(uint32_t ms) { emscripten_sleep(ms); }
uint32_t DG_GetTicksMs(void)
{
    return (uint32_t)((is_paused ? paused_at : emscripten_get_now()) - paused_time);
}
int DG_GetKey(int *pressed, unsigned char *key)
{
    if (mouse_dirty) {
        event_t event = {0};
        event.type = ev_mouse;
        event.data1 = mouse_buttons;
        event.data2 = mouse_dx;
        // Classic DOOM auto-aims vertically; vertical mouse motion never walks.
        D_PostEvent(&event);
        mouse_dx = mouse_dirty = 0;
    }
    if (read_index == write_index) return 0;
    unsigned short value = keys[read_index];
    read_index = (read_index + 1) % QUEUE_SIZE;
    *pressed = value >> 8;
    *key = value & 255;
    return 1;
}
void DG_SetWindowTitle(const char *title) { (void)title; }
static void tick(void) {
    if (!is_paused) {
        R_ApplyResolution();
        doomgeneric_Tick();
    }
}
int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    mousebfire = 0;
    mousebuse = 1;
    mousebstrafe = mousebforward = -1;
    emscripten_set_main_loop(tick, 0, 1);
    return 0;
}
