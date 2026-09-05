// GPL-2.0-or-later. Fast headless combat drills in the actual DOOM engine.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include "p_policy_net.h"
#include "doomgeneric.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "p_tick.h"
#include "g_game.h"
#include "player_bot.h"

static uint32_t clock_ms = 1000, scenario_rng;
void DG_Init(void) {}
void DG_DrawFrame(void) {}
void DG_SleepMs(uint32_t ms) { clock_ms += ms; }
uint32_t DG_GetTicksMs(void) { return clock_ms; }
int DG_GetKey(int *pressed, unsigned char *key) { return 0; }
void DG_SetWindowTitle(const char *title) {}
extern void P_RunThinkers(void);
extern int prndindex, rndindex;
extern gameaction_t gameaction;

static unsigned random_scenario(void)
{
    scenario_rng ^= scenario_rng << 13;
    scenario_rng ^= scenario_rng >> 17;
    scenario_rng ^= scenario_rng << 5;
    return scenario_rng;
}

static void match(unsigned seed, int map, int limit, const player_policy_t *player_policy,
                  const worthy_policy_t *enemy_policy, const float *player_net, const float *enemy_net)
{
    P_WorthySetEnabled(0);
    G_InitNew(sk_medium, 1, map);
    P_WorthyResetNavigation();
    gameaction = ga_nothing;
    paused = menuactive = false;
    demoplayback = demorecording = false;
    player_t *player = &players[consoleplayer];
    mobj_t *self = player->mo;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next)
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && (mobj_t *)th != self)
            P_RemoveMobj((mobj_t *)th);
    P_RunThinkers();
    scenario_rng = seed ? seed : 1;
    prndindex = seed & 255; rndindex = (seed >> 8) & 255;
    Bot_Reset(player_policy, seed);
    Bot_SetNetwork(player_net);
    P_WorthySetNetwork(enemy_net);
    P_WorthySetPolicy(enemy_policy);
    mobj_t *enemies[3];
    static const mobjtype_t types[] = {MT_POSSESSED, MT_TROOP, MT_SHOTGUY, MT_SERGEANT};
    int count = 2 + seed % 2, total_health = 0, layout_retries = 0;
    for (int n = 0; n < count; n++) {
        mobjtype_t type = types[(seed + n) % 4];
        mobj_t *enemy = NULL;
        for (int attempt = 0; attempt < 1000; attempt++) {
            int dx = ((int)(random_scenario() % 29) - 14) * 32;
            int dy = ((int)(random_scenario() % 29) - 14) * 32;
            if (abs(dx) + abs(dy) < 160) continue;
            enemy = P_SpawnMobj(self->x + dx * FRACUNIT, self->y + dy * FRACUNIT, ONFLOORZ, type);
            if (P_CheckPosition(enemy, enemy->x, enemy->y)
                && enemy->ceilingz - enemy->floorz >= enemy->height
                && P_CheckSight(enemy, self)) break;
            P_RemoveMobj(enemy);
            enemy = NULL;
        }
        if (!enemy && layout_retries++ < 20) {
            // Earlier placements can fill a narrow starting corridor. Retry
            // the entire layout, continuing the separate scenario RNG stream.
            // This happens before combat and is independent of both policies.
            for (int i = 0; i < n; i++) P_RemoveMobj(enemies[i]);
            P_RunThinkers();
            total_health = 0;
            n = -1;
            continue;
        }
        if (!enemy) {
            printf("RESULT {\"error\":\"No valid combat spawn\",\"seed\":%u,\"map\":%d}\n", seed, map);
            fflush(stdout);
            return;
        }
        enemy->target = self;
        enemy->reactiontime = 8;
        enemy->spawnpoint.x = enemy->x / FRACUNIT;
        enemy->spawnpoint.y = enemy->y / FRACUNIT;
        enemies[n] = enemy;
        total_health += enemy->health;
    }
    P_WorthySetEnabled(1);
    for (int n = 0; n < count; n++) P_SetMobjState(enemies[n], enemies[n]->info->seestate);
    int alive = count, remaining = total_health, t;
    for (t = 0; t < limit; t++) {
        Bot_Command(player);
        P_Ticker();
        gametic++;
        alive = remaining = 0;
        for (int n = 0; n < count; n++) if (enemies[n]->health > 0) {
            alive++;
            remaining += enemies[n]->health;
        }
        if (self->health <= 0 || !alive) break;
    }
    double damage = 1.0 - (double)remaining / total_health;
    double health = self->health > 0 ? self->health / 100.0 : 0.0;
    int result = self->health <= 0 ? -1 : !alive ? 1 : 0;
    double score = result + 0.25 * (damage - (1.0 - health));
    printf("RESULT {\"seed\":%u,\"map\":%d,\"result\":%d,\"score\":%.6f,\"ticks\":%d,"
           "\"player_health\":%d,\"enemy_health\":%d,\"enemy_initial_health\":%d,\"kills\":%d,\"enemies\":%d}\n",
           seed, map, result, score, t < limit ? t + 1 : t, self->health > 0 ? self->health : 0,
           remaining, total_health, count - alive, count);
    fflush(stdout);
}

// Optional network suffix: two presence flags, each followed by 149 weights
// when present. Old parameter-only requests explicitly reset both networks.
static int read_network(char **cursor, float *weights)
{
    char *end;
    long enabled = strtol(*cursor, &end, 10);
    if (end == *cursor || (enabled != 0 && enabled != 1)) return -1;
    *cursor = end;
    if (!enabled) return 0;
    for (int i = 0; i < POLICY_WEIGHTS; i++) {
        weights[i] = strtof(*cursor, &end);
        if (end == *cursor || !isfinite(weights[i]) || fabsf(weights[i]) > 4) return -1;
        *cursor = end;
    }
    return 1;
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (int i = 0; i < 3; i++) { clock_ms += 40; doomgeneric_Tick(); }
    char line[32768];
    while (fgets(line, sizeof(line), stdin)) {
        unsigned seed;
        int map, limit, offset = 0;
        player_policy_t p;
        worthy_policy_t e;
        int fields = sscanf(line, "%u %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %n",
            &seed, &map, &limit, &p.range, &p.strafe, &p.advance, &p.retreat,
            &p.aim_gain, &p.fire_angle, &p.switch_time, &p.turn_rate, &p.health_priority, &p.hitscan_priority,
            &e.range, &e.cover_wait, &e.cover_retry, &e.peek_time,
            &e.dodge_reaction, &e.flank, &e.attack_delay, &e.lead, &e.pursuit_lead, &e.cover_use,
            &e.hitscan_range, &e.wounded_bonus, &e.pressure_fire, &offset);
        if (fields != 26 || map < 1 || map > 9 || limit < 1 || limit > 3500
            || p.range < 96 || p.range > 400 || p.strafe < 0 || p.strafe > 40
            || p.advance < 12 || p.advance > 50 || p.retreat < 12 || p.retreat > 50
            || p.aim_gain < 10 || p.aim_gain > 100 || p.fire_angle < 1 || p.fire_angle > 12
            || p.switch_time < 18 || p.switch_time > 140 || p.turn_rate < 384 || p.turn_rate > 2048
            || p.health_priority < 0 || p.health_priority > 384 || p.hitscan_priority < 0 || p.hitscan_priority > 384) {
            puts("RESULT {\"error\":\"Invalid match request\"}");
            fflush(stdout);
            continue;
        }
        float pn[POLICY_WEIGHTS], en[POLICY_WEIGHTS];
        char *cursor = line + offset;
        while (isspace((unsigned char)*cursor)) cursor++;
        int has_p = 0, has_e = 0;
        if (*cursor) {
            has_p = read_network(&cursor, pn);
            has_e = has_p < 0 ? -1 : read_network(&cursor, en);
        }
        while (isspace((unsigned char)*cursor)) cursor++;
        if (has_p < 0 || has_e < 0 || *cursor) {
            puts("RESULT {\"error\":\"Invalid neural policy\"}");
            fflush(stdout);
            continue;
        }
        match(seed, map, limit, &p, &e, has_p ? pn : NULL, has_e ? en : NULL);
    }
    return 0;
}
