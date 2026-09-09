// Browser OPL driver entry points. SPDX-License-Identifier: GPL-2.0-or-later
#ifndef OPL_WORKLET_H
#define OPL_WORKLET_H

// Start fetching the worklet and its chip before the engine needs them.
void OPL_Worklet_Preload(void);

// The page paused the whole game: freeze music time and silence the output.
void OPL_Worklet_SetBrowserPaused(int paused);

// Silence the output without touching music state (the Sound button).
void OPL_Worklet_SetMuted(int muted);

#endif
