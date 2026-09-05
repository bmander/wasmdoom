// WasmDOOM native-resolution renderer, 2026-09-05.
// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef R_RESOLUTION_H
#define R_RESOLUTION_H
#include "doomtype.h"

// UI/game coordinates remain 320x200. Only the 3D rasterizer uses these sizes.
#define MAX_RENDERWIDTH 1280
#define MAX_RENDERHEIGHT 800
extern int render_scale;
#define RENDERWIDTH (320 * render_scale)
#define RENDERHEIGHT (200 * render_scale)
extern byte render_buffer[MAX_RENDERWIDTH * MAX_RENDERHEIGHT];
extern byte render_mask[320 * 200];

void R_RequestResolution(int scale);
void R_ApplyResolution(void);
void R_BeginDisplay(void);
void R_EndRender(void);
byte *R_CompositeFrame(void);
void R_OverlayRect(int x, int y, int width, int height);
#endif
