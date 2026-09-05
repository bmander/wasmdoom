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
    actor->worthy.z = target->z;
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
    if (actor->worthy.cover_state == WORTHY_FIRE
        && leveltime > actor->worthy.cover_until) return false;
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

static void abandon_cover(mobj_t *actor)
{
    actor->worthy.cover_state = WORTHY_OPEN;
    actor->worthy.cover_retry = leveltime + 2 * TICRATE;
    actor->worthy.route_count = actor->worthy.route_index = 0;
}

static void cover_destination(mobj_t *actor, int state, int duration)
{
    actor->worthy.cover_state = state;
    actor->worthy.cover_until = leveltime + duration;
    actor->worthy.route_count = actor->worthy.route_index = 0;
}

// Return true while this behavior owns movement/holding. FIRE falls through to
// the ordinary attack decision, then the completed burst returns to hiding.
static boolean use_cover(mobj_t *actor, boolean visible, boolean recovering)
{
    unsigned seed = personality(actor);
    if (actor->worthy.cover_state == WORTHY_OPEN) {
        if (!visible || leveltime < actor->worthy.cover_retry
            || P_AproxDistance(actor->worthy.x - actor->x, actor->worthy.y - actor->y) < 128 * FRACUNIT)
            return false;
        actor->worthy.cover_retry = leveltime + 2 * TICRATE + seed % TICRATE;
        if (!P_WorthyFindCover(actor)) return false;
        actor->worthy.cover_state = WORTHY_HIDE;
        actor->worthy.cover_until = leveltime + 4 * TICRATE;
        actor->worthy.cover_check = leveltime + 12;
        actor->worthy.cover_cycles = 0;
    }
    if (leveltime >= actor->worthy.cover_check) {
        actor->worthy.cover_check = leveltime + 12;
        if (!P_WorthyCoverValid(actor)) { abandon_cover(actor); return false; }
    }
    if (actor->worthy.cover_state == WORTHY_FIRE) {
        if (recovering || leveltime >= actor->worthy.cover_until) {
            actor->worthy.cover_cycles++;
            cover_destination(actor, WORTHY_HIDE, 3 * TICRATE);
        } else {
            actor->movedir = 8;
            return false;
        }
    }
    if (actor->worthy.cover_state == WORTHY_WAIT) {
        actor->movedir = 8;
        if (visible) { abandon_cover(actor); return false; }
        if (leveltime < actor->worthy.cover_until) return true;
        if (actor->worthy.cover_cycles >= 3) { abandon_cover(actor); return false; }
        cover_destination(actor, WORTHY_PEEK, 2 * TICRATE);
    }
    boolean hiding = actor->worthy.cover_state == WORTHY_HIDE;
    fixed_t gx = hiding ? actor->worthy.hide_x : actor->worthy.peek_x;
    fixed_t gy = hiding ? actor->worthy.hide_y : actor->worthy.peek_y;
    fixed_t reach = (actor->info->speed + 4) * FRACUNIT;
    if (P_AproxDistance(gx - actor->x, gy - actor->y) <= reach) {
        if (hiding) {
            if (visible) { abandon_cover(actor); return false; }
            cover_destination(actor, WORTHY_WAIT, 20 + seed % 23);
        } else {
            cover_destination(actor, WORTHY_FIRE, TICRATE);
        }
        actor->movedir = 8;
        return actor->worthy.cover_state != WORTHY_FIRE;
    }
    if (leveltime >= actor->worthy.cover_until) { abandon_cover(actor); return false; }
    P_WorthyNavigate(actor, gx, gy, 0, 0);
    return true;
}

boolean P_WorthyChase(mobj_t *actor)
{
    if (!eligible(actor)) return false;
    mobj_t *target = actor->target;
    boolean visible = P_CheckSight(actor, target);
    if (visible) remember(actor, target);
    if (!actor->worthy.known || leveltime - actor->worthy.contact_time > 8 * TICRATE) {
        actor->movedir = 8;
        actor->worthy.cover_state = WORTHY_OPEN;
        actor->worthy.route_count = actor->worthy.route_index = 0;
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

    if (visible && actor->info->meleestate && P_CheckMeleeRange(actor)) {
        abandon_cover(actor);
        if (actor->info->attacksound) S_StartSound(actor, actor->info->attacksound);
        P_SetMobjState(actor, actor->info->meleestate);
        return true;
    }

    // Demons commit to closing distance. Reactive strafing is a ranged tactic.
    angle_t to_actor = R_PointToAngle2(target->x, target->y, actor->x, actor->y);
    int32_t difference = (int32_t)(to_actor - target->angle);
    boolean threatened = ranged && visible && target->player->attackdown
        && difference > -(int32_t)(ANG45 / 2) && difference < (int32_t)(ANG45 / 2);
    if (!threatened) actor->worthy.threat_since = 0;
    else if (!actor->worthy.threat_since) actor->worthy.threat_since = leveltime + 1;
    boolean dodge = threatened && leveltime + 1 - actor->worthy.threat_since >= 6;

    if (ranged && use_cover(actor, visible, recovering)) return true;
    if (ranged && visible && !recovering && leveltime >= actor->worthy.next_attack
        && (actor->worthy.cover_state == WORTHY_FIRE || !dodge || ((leveltime / 12 + seed) & 1))
        && P_WorthyCanFire(actor) && P_CheckMissileRange(actor)) {
        actor->worthy.next_attack = leveltime + 24 + seed % 12;
        actor->flags |= MF_JUSTATTACKED;
        P_SetMobjState(actor, actor->info->missilestate);
        return true;
    }
    if (actor->worthy.cover_state == WORTHY_FIRE) return true;
    if (!visible && distance < 24 * FRACUNIT) {
        actor->movedir = 8;
        actor->angle += side * ANG45;
        return true;
    }
    if (distance < FRACUNIT) distance = FRACUNIT;
    fixed_t ux = FixedDiv(dx, distance), uy = FixedDiv(dy, distance);
    fixed_t gx = actor->worthy.x, gy = actor->worthy.y;
    if (ranged && visible) {
        int preferred = actor->type == MT_SHOTGUY ? 192 : 288;
        if (actor->health < actor->info->spawnhealth / 3) preferred += 128;
        if (distance < (preferred - 64) * FRACUNIT) {
            gx = actor->x - ux * 96; gy = actor->y - uy * 96;
        } else if (distance <= (preferred + 96) * FRACUNIT) {
            // Hold a firing position instead of constantly orbiting the player.
            gx = actor->x; gy = actor->y;
            if (!P_WorthyCanFire(actor)) {
                gx -= uy * side * 64; gy += ux * side * 64;
            }
        } else if (seed % 3) {
            // A fixed approach offset, not a new sideways impulse each step.
            gx -= uy * side * 64; gy += ux * side * 64;
        }
        if (dodge) {
            gx = actor->x - uy * side * 96;
            gy = actor->y + ux * side * 96;
        }
    }
    find_spacing(actor);
    P_WorthyNavigate(actor, gx, gy, separation_x, separation_y);
    return true;
}
