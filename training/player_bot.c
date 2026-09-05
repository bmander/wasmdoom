// GPL-2.0-or-later. Sends ordinary commands through P_PlayerThink.
#include <stdint.h>
#include <string.h>
#include "doomstat.h"
#include "d_event.h"
#include "p_local.h"
#include "p_worthy.h"
#include "player_bot.h"

static player_policy_t policy;
static fixed_t known_x, known_y;
static int contact, side, next_switch, observed, visible;

void Bot_Reset(const player_policy_t *value, unsigned seed)
{
    policy = *value;
    contact = -10000;
    observed = visible = 0;
    side = seed & 1 ? 1 : -1;
    next_switch = policy.switch_time;
}

void Bot_Command(player_t *player)
{
    mobj_t *self = player->mo;
    ticcmd_t *cmd = &player->cmd;
    memset(cmd, 0, sizeof(*cmd));
    // Both sides use engine LOS and structured coordinates. The bot refreshes
    // observations at 8.75 Hz and never reads a hidden target's current pose.
    if (!(leveltime % 4)) {
        fixed_t closest = INT32_MAX;
        visible = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
            mobj_t *other = (mobj_t *)th;
            if (!(other->flags & MF_COUNTKILL) || other->health <= 0) continue;
            if (!P_CheckSight(self, other)) continue;
            fixed_t distance = P_AproxDistance(other->x - self->x, other->y - self->y);
            if (distance >= closest) continue;
            closest = distance;
            known_x = other->x; known_y = other->y;
            contact = leveltime;
            observed = visible = 1;
        }
    }
    if (leveltime >= next_switch) {
        side = -side;
        next_switch = leveltime + policy.switch_time;
    }
    if (!observed || leveltime - contact > 8 * TICRATE) {
        cmd->angleturn = policy.turn_rate / 2;
        cmd->forwardmove = policy.advance / 2;
    } else {
        angle_t aim = R_PointToAngle2(self->x, self->y, known_x, known_y);
        int32_t error = (int32_t)(aim - self->angle);
        int turn = (int64_t)error * policy.aim_gain / 100 / 65536;
        if (turn > policy.turn_rate) turn = policy.turn_rate;
        if (turn < -policy.turn_rate) turn = -policy.turn_rate;
        cmd->angleturn = turn;
        fixed_t distance = P_AproxDistance(known_x - self->x, known_y - self->y);
        if (!visible || distance > (policy.range + 32) * FRACUNIT)
            cmd->forwardmove = policy.advance;
        else if (distance < (policy.range - 32) * FRACUNIT)
            cmd->forwardmove = -policy.retreat;
        if (visible) cmd->sidemove = side * policy.strafe;
        int64_t magnitude = error < 0 ? -(int64_t)error : error;
        if (visible && magnitude <= (int64_t)ANG45 * policy.fire_angle / 45)
            cmd->buttons |= BT_ATTACK;
    }
    // Short geometry feelers prevent charging a wall. These are map queries,
    // not hidden-enemy observations. Actual movement remains normal player physics.
    int angle = self->angle >> ANGLETOFINESHIFT;
    fixed_t fx = finecosine[angle], fy = finesine[angle];
    if (cmd->forwardmove && !P_WorthyWalkSegment(self, self->x, self->y,
        self->x + fx * (cmd->forwardmove > 0 ? 48 : -48),
        self->y + fy * (cmd->forwardmove > 0 ? 48 : -48))) {
        cmd->forwardmove = 0;
        cmd->sidemove = side * 32;
    }
    if (cmd->sidemove && !P_WorthyWalkSegment(self, self->x, self->y,
        self->x + fy * side * 48, self->y - fx * side * 48)) {
        side = -side;
        cmd->sidemove = side * 32;
        next_switch = leveltime + policy.switch_time;
    }
    if (!(leveltime % TICRATE)) cmd->buttons |= BT_USE;
}
