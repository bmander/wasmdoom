// Worthy Adversaries: tactical decisions using DOOM's movement and attack rules.
// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdint.h>
#include <stdlib.h>
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "r_state.h"
#include "s_sound.h"

static int enabled;
extern boolean P_Move(mobj_t *actor);
extern boolean P_CheckMeleeRange(mobj_t *actor);
extern boolean P_CheckMissileRange(mobj_t *actor);
extern fixed_t xspeed[8], yspeed[8];

void P_WorthySetEnabled(int value) { enabled = value != 0; }
boolean P_WorthyEnabled(void)
{
    // The bundled attract demos must retain their original deterministic AI.
    return enabled && !demoplayback && !demorecording && !netgame;
}

static boolean eligible(mobj_t *actor)
{
    return P_WorthyEnabled() && actor && !actor->player
        && (actor->flags & MF_COUNTKILL) && actor->health > 0
        && actor->target && actor->target->player && actor->target->health > 0;
}

static unsigned personality(mobj_t *actor)
{
    // Stable per spawn; doesn't consume the engine's gameplay random stream.
    unsigned seed = (unsigned)(uint16_t)actor->spawnpoint.x * 73856093u
        ^ (unsigned)(uint16_t)actor->spawnpoint.y * 19349663u
        ^ (unsigned)actor->type * 83492791u;
    // Map placements often share low bits; mix them before choosing a flank.
    seed ^= seed >> 16;
    seed *= 0x7feb352du;
    return seed ^ (seed >> 15);
}

static void remember(mobj_t *actor, mobj_t *target)
{
    actor->worthy.known = 1;
    actor->worthy.x = target->x;
    actor->worthy.y = target->y;
    actor->worthy.contact_time = leveltime;
}

void P_WorthyNoise(mobj_t *target)
{
    if (!P_WorthyEnabled() || !target || !target->player) return;
    int sound_visit = validcount;
    // Only sectors reached by this particular vanilla sound flood get a report.
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mobj_t *actor = (mobj_t *)th;
        if (!(actor->flags & MF_COUNTKILL) || actor->health <= 0) continue;
        if (actor->subsector->sector->validcount != sound_visit) continue;
        // Ambush monsters keep their original requirement for visual contact.
        if ((actor->flags & MF_AMBUSH) && !P_CheckSight(actor, target)) continue;
        remember(actor, target);
    }
}

boolean P_WorthyCanFire(mobj_t *actor)
{
    if (!eligible(actor)) return true;
    if (!P_CheckSight(actor, actor->target)) return false;
    angle_t angle = R_PointToAngle2(actor->x, actor->y, actor->target->x, actor->target->y);
    P_AimLineAttack(actor, angle, MISSILERANGE);
    // Reposition if a barrel, another monster, or a wall occupies the shot lane.
    return linetarget == actor->target;
}

void P_WorthyAim(mobj_t *source, mobj_t *target, fixed_t speed,
                fixed_t *x, fixed_t *y)
{
    if (!eligible(source) || source->target != target || speed <= 0) return;
    if (!P_CheckSight(source, target)) {
        if (source->worthy.known) {
            *x = source->worthy.x;
            *y = source->worthy.y;
        }
        return;
    }
    remember(source, target);
    if (target->flags & MF_SHADOW) return;
    int flight = P_AproxDistance(target->x - source->x, target->y - source->y) / speed;
    if (flight > 16) flight = 16;
    // Partial, capped prediction: changing direction still beats the shot.
    int64_t dx = (int64_t)target->momx * flight * 2 / 3;
    int64_t dy = (int64_t)target->momy * flight * 2 / 3;
    const fixed_t cap = 96 * FRACUNIT;
    if (dx > cap) dx = cap;
    if (dx < -cap) dx = -cap;
    if (dy > cap) dy = cap;
    if (dy < -cap) dy = -cap;
    *x = target->x + (fixed_t)dx;
    *y = target->y + (fixed_t)dy;
}

// The blockmap bounds the local-spacing work even on crowded maps.
static mobj_t *neighbor_actor;
static fixed_t separation_x, separation_y;
static int neighbors;
static boolean separate(mobj_t *other)
{
    mobj_t *actor = neighbor_actor;
    if (other == actor || !(other->flags & MF_COUNTKILL) || other->health <= 0) return true;
    if (other->target != actor->target) return true;
    fixed_t dx = actor->x - other->x, dy = actor->y - other->y;
    fixed_t distance = P_AproxDistance(dx, dy);
    if (distance == 0 || distance > 112 * FRACUNIT) return true;
    if (!P_CheckSight(actor, other)) return true;
    fixed_t strength = distance < actor->radius + other->radius + 24 * FRACUNIT
        ? 2 * FRACUNIT : FRACUNIT / 2;
    separation_x += FixedMul(FixedDiv(dx, distance), strength);
    separation_y += FixedMul(FixedDiv(dy, distance), strength);
    return ++neighbors < 12;
}

static void find_spacing(mobj_t *actor)
{
    neighbor_actor = actor;
    separation_x = separation_y = 0;
    neighbors = 0;
    int bx = (actor->x - bmaporgx) >> MAPBLOCKSHIFT;
    int by = (actor->y - bmaporgy) >> MAPBLOCKSHIFT;
    for (int y = by - 1; y <= by + 1 && neighbors < 12; y++)
        for (int x = bx - 1; x <= bx + 1 && neighbors < 12; x++)
            P_BlockThingsIterator(x, y, separate);
}

boolean P_WorthyChase(mobj_t *actor)
{
    if (!eligible(actor)) return false;
    mobj_t *target = actor->target;
    boolean visible = P_CheckSight(actor, target);
    if (visible) remember(actor, target);
    if (!actor->worthy.known || leveltime - actor->worthy.contact_time > 8 * TICRATE) {
        // No through-wall tracking. Wait for a new sighting or audible report.
        actor->movedir = 8;
        return true;
    }

    unsigned seed = personality(actor);
    int side = (seed & 1) ? 1 : -1;
    fixed_t dx = actor->worthy.x - actor->x;
    fixed_t dy = actor->worthy.y - actor->y;
    fixed_t distance = P_AproxDistance(dx, dy);
    boolean ranged = actor->info->missilestate != 0;
    boolean recovering = (actor->flags & MF_JUSTATTACKED) != 0;
    actor->flags &= ~MF_JUSTATTACKED;

    // Retain the normal attack animations, hit chances, damage, and cooldown.
    if (visible && actor->info->meleestate && P_CheckMeleeRange(actor)) {
        if (actor->info->attacksound) S_StartSound(actor, actor->info->attacksound);
        P_SetMobjState(actor, actor->info->meleestate);
        return true;
    }

    // Recognize sustained, visible fire after ~170ms, never read future inputs.
    angle_t to_actor = R_PointToAngle2(target->x, target->y, actor->x, actor->y);
    int32_t difference = (int32_t)(to_actor - target->angle);
    boolean threatened = visible && target->player->attackdown
        && difference > -(int32_t)(ANG45 / 2) && difference < (int32_t)(ANG45 / 2);
    if (!threatened) actor->worthy.threat_since = 0;
    else if (!actor->worthy.threat_since) actor->worthy.threat_since = leveltime + 1;
    boolean dodge = threatened && leveltime + 1 - actor->worthy.threat_since >= 6;

    if (ranged && visible && !recovering && leveltime >= actor->worthy.next_attack
        && (!dodge || ((leveltime / 12 + seed) & 1))
        && P_WorthyCanFire(actor) && P_CheckMissileRange(actor)) {
        actor->worthy.next_attack = leveltime + 24 + seed % 12;
        actor->flags |= MF_JUSTATTACKED;
        P_SetMobjState(actor, actor->info->missilestate);
        return true;
    }

    if (!visible && distance < 24 * FRACUNIT) {
        actor->movedir = 8;
        // Search turns expose the normal facing animation without knowing where
        // the player went. Contact is reacquired only on a sight/sound check.
        actor->angle += side * ANG45;
        return true;
    }
    if (distance < FRACUNIT) distance = FRACUNIT;
    fixed_t ux = FixedDiv(dx, distance), uy = FixedDiv(dy, distance);
    int preferred = actor->type == MT_SHOTGUY ? 192 : 288;
    if (actor->health < actor->info->spawnhealth / 3) preferred += 128;
    int advance = !visible || !ranged ? 1
        : distance < (preferred - 64) * FRACUNIT ? -1
        : distance > (preferred + 96) * FRACUNIT ? 1 : 0;
    // Distinct approach lanes; the center role advances while its flanks orbit.
    fixed_t strafe = !visible ? 0 : dodge ? FRACUNIT * 2
        : (seed % 3 == 0 && advance > 0) ? 0 : FRACUNIT * 3 / 4;
    if (!ranged && distance < 96 * FRACUNIT) strafe = 0;
    fixed_t wantx = ux * advance - FixedMul(uy, strafe) * side;
    fixed_t wanty = uy * advance + FixedMul(ux, strafe) * side;
    find_spacing(actor);
    wantx += separation_x;
    wanty += separation_y;

    // Commit briefly to a successful detour instead of oscillating at corners.
    if (actor->worthy.detour > 0 && !dodge) {
        actor->worthy.detour--;
        if (P_Move(actor)) return true;
    }
    int64_t scores[8];
    for (int dir = 0; dir < 8; dir++) {
        scores[dir] = (int64_t)xspeed[dir] * wantx + (int64_t)yspeed[dir] * wanty;
        if (dir == actor->movedir) scores[dir] += (int64_t)FRACUNIT * FRACUNIT / 8;
    }
    for (int attempt = 0; attempt < 8; attempt++) {
        int best = 0;
        for (int dir = 1; dir < 8; dir++) if (scores[dir] > scores[best]) best = dir;
        scores[best] = INT64_MIN;
        actor->movedir = best;
        // P_Move enforces collision, steps, drop-offs, and ordinary monster doors.
        if (P_Move(actor)) {
            actor->worthy.detour = attempt > 0 ? 2 : 0;
            actor->movecount = 0;
            return true;
        }
    }
    actor->movedir = 8;
    return true;
}
