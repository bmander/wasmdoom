// GPL-2.0-or-later. Sends ordinary commands through P_PlayerThink.
#include <stdint.h>
#include <string.h>
#include "doomstat.h"
#include "d_event.h"
#include "p_local.h"
#include "p_worthy.h"
#include "p_policy_net.h"
#include "player_bot.h"

static player_policy_t policy, choice;
static policy_net_t network;
void Bot_SetNetwork(const float *weights) { P_PolicyNetLoad(&network, weights); }
void Bot_SetRecurrentNetwork(const float *weights) { P_PolicyNetLoadRecurrent(&network, weights); }
static float memory[TACTIC_MEMORY];
static int previous_health;
static int bound(int value, int low, int high) { return value < low ? low : value > high ? high : value; }
static fixed_t known_x, known_y;
static int contact, side, next_switch, observed, visible;

void Bot_Reset(const player_policy_t *value, unsigned seed)
{
    policy = choice = *value;
    memset(memory, 0, sizeof(memory));
    previous_health = 0;
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
        mobj_t *seen[64], *nearest = NULL;
        int count = 0;
        fixed_t nearest_distance = INT32_MAX;
        visible = 0;
        for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
            if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
            mobj_t *other = (mobj_t *)th;
            if (!(other->flags & MF_COUNTKILL) || other->health <= 0) continue;
            if (!P_CheckSight(self, other)) continue;
            if (count < 64) seen[count++] = other;
            fixed_t distance = P_AproxDistance(other->x - self->x, other->y - self->y);
            if (distance < nearest_distance) { nearest = other; nearest_distance = distance; }
        }
        choice = policy;
        if (network.enabled) {
            // No live hidden-enemy state. Missing observations are zero, with
            // the visibility input distinguishing them from a visible target.
            float features[TACTIC_INPUTS] = {
                player->health / 50.0f - 1, player->ammo[am_clip] / 50.0f - 1,
                nearest ? nearest_distance / (512.0f * FRACUNIT) : 0,
                nearest ? nearest->health / 100.0f : 0,
                nearest && nearest->type == MT_SERGEANT ? 1 : 0,
                nearest && (nearest->type == MT_POSSESSED || nearest->type == MT_SHOTGUY) ? 1 : 0,
                count / 3.0f, nearest ? 1 : -1,
                nearest ? (int32_t)(R_PointToAngle2(self->x, self->y, nearest->x, nearest->y) - self->angle) / (float)ANG180 : 0,
                nearest ? nearest->momx / (16.0f * FRACUNIT) : 0,
                nearest ? nearest->momy / (16.0f * FRACUNIT) : 0,
                (leveltime - contact) / (8.0f * TICRATE)
            }, output[TACTIC_OUTPUTS] = {0};
            if (network.recurrent) {
                P_PolicyContext(self, nearest, previous_health, features + POLICY_INPUTS);
                previous_health = self->health;
                P_PolicyNetRecurrent(&network, features, memory, output);
                choice.advance = bound(policy.advance + (int)(output[5] * 30), 12, 50);
                choice.retreat = bound(policy.retreat + (int)(output[6] * 30), 12, 50);
                choice.fire_angle = bound(policy.fire_angle + (int)(output[7] * 8), 1, 12);
                choice.switch_time = bound(policy.switch_time + (int)(output[8] * 70), 18, 140);
                choice.turn_rate = bound(policy.turn_rate + (int)(output[9] * 1000), 384, 2048);
            } else P_PolicyNetEvaluate(&network, features, output);
            choice.range = bound(policy.range + (int)(output[0] * 192), 96, 400);
            choice.strafe = bound(policy.strafe + (int)(output[1] * 28), 0, 40);
            choice.health_priority = bound(policy.health_priority + (int)(output[2] * 384), 0, 384);
            choice.hitscan_priority = bound(policy.hitscan_priority + (int)(output[3] * 384), 0, 384);
            choice.aim_gain = bound(policy.aim_gain + (int)(output[4] * 40), 10, 100);
        }
        int64_t closest = INT64_MAX;
        for (int i = 0; i < count; i++) {
            mobj_t *other = seen[i];
            fixed_t distance = P_AproxDistance(other->x - self->x, other->y - self->y);
            int64_t priority = distance + (int64_t)choice.health_priority * FRACUNIT * other->health / 100;
            if (other->type == MT_POSSESSED || other->type == MT_SHOTGUY || other->type == MT_CHAINGUY)
                priority -= choice.hitscan_priority * FRACUNIT;
            if (priority >= closest) continue;
            closest = priority;
            known_x = other->x; known_y = other->y;
            contact = leveltime;
            observed = visible = 1;
        }
    }
    if (leveltime >= next_switch) {
        side = -side;
        next_switch = leveltime + choice.switch_time;
    }
    if (!observed || leveltime - contact > 8 * TICRATE) {
        cmd->angleturn = choice.turn_rate / 2;
        cmd->forwardmove = choice.advance / 2;
    } else {
        angle_t aim = R_PointToAngle2(self->x, self->y, known_x, known_y);
        int32_t error = (int32_t)(aim - self->angle);
        int turn = (int64_t)error * choice.aim_gain / 100 / 65536;
        if (turn > choice.turn_rate) turn = choice.turn_rate;
        if (turn < -choice.turn_rate) turn = -choice.turn_rate;
        cmd->angleturn = turn;
        fixed_t distance = P_AproxDistance(known_x - self->x, known_y - self->y);
        if (!visible || distance > (choice.range + 32) * FRACUNIT)
            cmd->forwardmove = choice.advance;
        else if (distance < (choice.range - 32) * FRACUNIT)
            cmd->forwardmove = -choice.retreat;
        if (visible) cmd->sidemove = side * choice.strafe;
        int64_t magnitude = error < 0 ? -(int64_t)error : error;
        if (visible && magnitude <= (int64_t)ANG45 * choice.fire_angle / 45)
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
        next_switch = leveltime + choice.switch_time;
    }
    if (!(leveltime % TICRATE)) cmd->buttons |= BT_USE;
}
