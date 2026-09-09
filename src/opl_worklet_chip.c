// OPL3 chip for the audio thread. SPDX-License-Identifier: GPL-2.0-or-later
//
// Compiled on its own into public/engine/opl3.wasm and driven by
// src/opl_worklet.js inside an AudioWorklet, so that synthesis never runs on
// the main thread. The engine side is src/opl_worklet.c.
#include <stdint.h>
#include <emscripten.h>
#include "opl3.h"

// One render quantum is 128 frames; a larger request is split by the caller.
#define MAX_FRAMES 1024

static opl3_chip chip;
static int16_t samples[MAX_FRAMES * 2];

EMSCRIPTEN_KEEPALIVE void opl_reset(uint32_t sample_rate)
{
    OPL3_Reset(&chip, sample_rate);
}

EMSCRIPTEN_KEEPALIVE void opl_write(uint32_t reg, uint32_t value)
{
    OPL3_WriteRegBuffered(&chip, (uint16_t)reg, (uint8_t)value);
}

// Interleaved stereo 16-bit samples; valid until the next call.
EMSCRIPTEN_KEEPALIVE int16_t *opl_render(uint32_t frames)
{
    if (frames > MAX_FRAMES) frames = MAX_FRAMES;
    OPL3_GenerateStream(&chip, samples, frames);
    return samples;
}
