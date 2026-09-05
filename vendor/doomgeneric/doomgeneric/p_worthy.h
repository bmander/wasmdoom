// WasmDOOM Worthy Adversaries, 2026-09-05. GPL-2.0-or-later.
#ifndef P_WORTHY_H
#define P_WORTHY_H
#include "p_mobj.h"

void P_WorthySetEnabled(int enabled);
boolean P_WorthyEnabled(void);
boolean P_WorthyChase(mobj_t *actor);
boolean P_WorthyCanFire(mobj_t *actor);
void P_WorthyNoise(mobj_t *target);
void P_WorthyAim(mobj_t *source, mobj_t *target, fixed_t speed,
                fixed_t *x, fixed_t *y);
#endif
