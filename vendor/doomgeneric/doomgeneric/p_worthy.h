// WasmDOOM Worthy Adversaries, 2026-09-05. GPL-2.0-or-later.
#ifndef P_WORTHY_H
#define P_WORTHY_H
#include "p_mobj.h"

void P_WorthySetEnabled(int enabled);
// Tactical choices only; training cannot change health, damage or move speed.
typedef struct {
    int range, cover_wait, cover_retry, peek_time, dodge_reaction;
    int flank, attack_delay, lead, pursuit_lead, cover_use;
} worthy_policy_t;
void P_WorthySetPolicy(const worthy_policy_t *policy);
boolean P_WorthyEnabled(void);
boolean P_WorthyChase(mobj_t *actor);
boolean P_WorthyCanFire(mobj_t *actor);
enum { WORTHY_OPEN, WORTHY_HIDE, WORTHY_WAIT, WORTHY_PEEK, WORTHY_FIRE };
boolean P_WorthyWalkSegment(mobj_t *actor, fixed_t x, fixed_t y, fixed_t to_x, fixed_t to_y);
boolean P_WorthyFindCover(mobj_t *actor);
boolean P_WorthyCoverValid(mobj_t *actor);
void P_WorthyNavigate(mobj_t *actor, fixed_t x, fixed_t y, fixed_t space_x, fixed_t space_y);
void P_WorthyNoise(mobj_t *target);
void P_WorthyAim(mobj_t *source, mobj_t *target, fixed_t speed,
                fixed_t *x, fixed_t *y);
#endif
