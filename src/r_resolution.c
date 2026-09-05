// SPDX-License-Identifier: GPL-2.0-or-later
#include <string.h>
#include "i_video.h"
#include "r_local.h"
#include "r_state.h"

int render_scale = 2;
static int requested_scale = 2;
byte render_buffer[MAX_RENDERWIDTH * MAX_RENDERHEIGHT];
byte render_mask[320 * 200];
static byte composite[MAX_RENDERWIDTH * MAX_RENDERHEIGHT];

void R_RequestResolution(int scale)
{
    if (scale == 1 || scale == 2 || scale == 4) requested_scale = scale;
}

void R_ApplyResolution(void)
{
    extern int screenblocks, detailLevel;
    if (requested_scale == render_scale) return;
    render_scale = requested_scale;
    // Called only between complete engine ticks, never during an Asyncify wait.
    R_SetViewSize(screenblocks, detailLevel);
}

void R_BeginDisplay(void) { memset(render_mask, 0, sizeof(render_mask)); }

void R_EndRender(void)
{
    int left = viewwindowx / render_scale;
    int top = viewwindowy / render_scale;
    int width = scaledviewwidth / render_scale;
    int height = viewheight / render_scale;
    for (int y = top; y < top + height; y++) {
        memset(render_mask + y * 320 + left, 1, width);
        for (int x = left; x < left + width; x++) {
            // Retain a classic-size view for the legacy wipe and screenshot code.
            I_VideoBuffer[y * 320 + x] = render_buffer[y * render_scale * RENDERWIDTH + x * render_scale];
        }
    }
}

void R_OverlayRect(int x, int y, int width, int height)
{
    int endx = x + width, endy = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (endx > 320) endx = 320;
    if (endy > 200) endy = 200;
    if (endx <= x) return;
    for (; y < endy; y++) memset(render_mask + y * 320 + x, 0, endx - x);
}

byte *R_CompositeFrame(void)
{
    for (int y = 0; y < RENDERHEIGHT; y++) {
        int row = (y / render_scale) * 320;
        for (int x = 0; x < 320; x++) {
            int output = y * RENDERWIDTH + x * render_scale;
            if (render_mask[row + x])
                memcpy(composite + output, render_buffer + output, render_scale);
            else
                memset(composite + output, I_VideoBuffer[row + x], render_scale);
        }
    }
    return composite;
}
