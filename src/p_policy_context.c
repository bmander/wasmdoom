// GPL-2.0-or-later. Extra observations for the recurrent policy; no hidden targets.
#include <stdint.h>
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"

void P_PolicyContext(mobj_t *actor, mobj_t *visible_target, int previous_health, float *out)
{
    int angle = actor->angle >> ANGLETOFINESHIFT;
    fixed_t fx = finecosine[angle], fy = finesine[angle];
    int allies = 0;
    fixed_t ally_distance = 512 * FRACUNIT, projectile = 512 * FRACUNIT;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mobj_t *other = (mobj_t *)th;
        if (other == actor || other->health <= 0) continue;
        fixed_t dx = actor->x - other->x, dy = actor->y - other->y;
        fixed_t d = P_AproxDistance(dx, dy);
        if (d >= 512 * FRACUNIT) continue;
        if (!actor->player && (other->flags & MF_COUNTKILL) && P_CheckSight(actor, other)) {
            allies++;
            if (d < ally_distance) ally_distance = d;
        }
        if ((other->flags & MF_MISSILE) && other->target != actor
            && (int64_t)dx * other->momx + (int64_t)dy * other->momy > 0
            && P_CheckSight(actor, other) && d < projectile) projectile = d;
    }
    out[0] = (FixedMul(actor->momx, fx) + FixedMul(actor->momy, fy)) / (16.0f * FRACUNIT);
    out[1] = (FixedMul(actor->momx, fy) - FixedMul(actor->momy, fx)) / (16.0f * FRACUNIT);
    out[2] = actor->player ? actor->player->readyweapon / 8.0f
        : visible_target && visible_target->player ? visible_target->player->readyweapon / 8.0f : 0;
    out[3] = allies / 6.0f;
    out[4] = ally_distance / (512.0f * FRACUNIT);
    out[5] = 1.0f - projectile / (512.0f * FRACUNIT);
    out[6] = P_WorthyWalkSegment(actor, actor->x, actor->y, actor->x - fy * 64, actor->y + fx * 64) ? 1 : -1;
    out[7] = P_WorthyWalkSegment(actor, actor->x, actor->y, actor->x + fx * 64, actor->y + fy * 64) ? 1 : -1;
    out[8] = P_WorthyWalkSegment(actor, actor->x, actor->y, actor->x + fy * 64, actor->y - fx * 64) ? 1 : -1;
    out[9] = previous_health > actor->health ? (previous_health - actor->health) / 20.0f : 0;
    out[10] = visible_target ? (visible_target->z - actor->z) / (128.0f * FRACUNIT) : 0;
    out[11] = leveltime / (35.0f * TICRATE);
}
