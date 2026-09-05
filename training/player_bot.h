// GPL-2.0-or-later. Structured-observation combat bot, not a pixel model.
#ifndef PLAYER_BOT_H
#define PLAYER_BOT_H
#include "d_player.h"
typedef struct {
    int range, strafe, advance, retreat, aim_gain, fire_angle, switch_time, turn_rate;
    int health_priority, hitscan_priority;
} player_policy_t;
void Bot_Reset(const player_policy_t *policy, unsigned seed);
void Bot_SetNetwork(const float *weights);
void Bot_Command(player_t *player);
#endif
