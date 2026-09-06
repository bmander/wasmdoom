// Integration scenarios using real E1M1 geometry and engine movement/LOS.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "doomgeneric.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "p_policy_net.h"
#include "../src/p_worthy_champion.h"
#include <math.h>
#include "g_game.h"

static uint32_t clock_ms = 1000;
void DG_Init(void) {}
void DG_DrawFrame(void) {}
void DG_SleepMs(uint32_t ms) { clock_ms += ms; }
uint32_t DG_GetTicksMs(void) { return clock_ms; }
int DG_GetKey(int *pressed, unsigned char *key) { return 0; }
void DG_SetWindowTitle(const char *title) {}
extern void P_RunThinkers(void);
extern void A_PosAttack(mobj_t *actor);
extern void A_SPosAttack(mobj_t *actor);
extern void A_CPosAttack(mobj_t *actor);
extern boolean P_CheckMissileRange(mobj_t *actor);

static mobj_t *player;
static void reset(void)
{
    P_WorthySetEnabled(0);
    P_WorthySetDeterministicFire(0);
    G_InitNew(sk_medium, 1, 1);
    player = players[consoleplayer].mo;
    players[consoleplayer].attackdown = false;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function.acp1 == (actionf_p1)P_MobjThinker && (mobj_t *)th != player)
            P_RemoveMobj((mobj_t *)th);
    }
    P_RunThinkers();
    leveltime = 100;
    P_WorthySetEnabled(1);
}

static mobj_t *spawn(mobjtype_t type, int dx, int dy)
{
    mobj_t *actor = P_SpawnMobj(player->x + dx * FRACUNIT,
                              player->y + dy * FRACUNIT, ONFLOORZ, type);
    actor->target = player;
    actor->reactiontime = 0;
    actor->movedir = 8;
    actor->worthy.next_attack = INT_MAX;
    actor->spawnpoint.x = actor->x >> FRACBITS;
    actor->spawnpoint.y = actor->y >> FRACBITS;
    return actor;
}

static fixed_t distance(mobj_t *actor)
{
    return P_AproxDistance(actor->x - player->x, actor->y - player->y);
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);
    for (int i = 0; i < 3; i++) { clock_ms += 40; doomgeneric_Tick(); }
    reset();
    mobj_t *actor = spawn(MT_TROOP, 80, 0);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    assert(P_CheckSight(actor, player));
    fixed_t oldx = actor->x, oldy = actor->y;
    P_WorthySetEnabled(0);
    assert(!P_WorthyChase(actor));
    assert(actor->x == oldx && actor->y == oldy);
    P_WorthySetEnabled(1);
    demoplayback = true;
    assert(!P_WorthyChase(actor));
    demoplayback = false;
    puts("PASS: disabled mode and demos leave vanilla AI in control");

    fixed_t before = distance(actor);
    assert(P_WorthyChase(actor));
    assert(distance(actor) > before);
    assert(abs(actor->x - oldx) <= actor->info->speed * FRACUNIT);
    assert(abs(actor->y - oldy) <= actor->info->speed * FRACUNIT);
    puts("PASS: a ranged enemy retreats when crowded at normal movement speed");

    reset();
    actor = spawn(MT_SERGEANT, 80, 0);
    before = distance(actor);
    assert(P_WorthyChase(actor));
    assert(distance(actor) < before);
    puts("PASS: a melee demon closes distance instead of adopting a ranged role");

    reset();
    actor = spawn(MT_POSSESSED, 80, 0);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    assert(P_WorthyCanFire(actor));
    mobj_t *blocker = spawn(MT_TROOP, 40, 0);
    assert(!P_WorthyCanFire(actor));
    P_RemoveMobj(blocker);
    P_RunThinkers();
    assert(P_WorthyCanFire(actor));
    puts("PASS: a friendly body blocks a shot, and clearing the lane allows it");

    player->momy = 8 * FRACUNIT;
    fixed_t aimx = player->x, aimy = player->y;
    P_WorthyAim(actor, player, 10 * FRACUNIT, &aimx, &aimy);
    assert(aimy > player->y && aimy <= player->y + 96 * FRACUNIT);
    player->flags |= MF_SHADOW;
    aimx = player->x; aimy = player->y;
    P_WorthyAim(actor, player, 10 * FRACUNIT, &aimx, &aimy);
    assert(aimx == player->x && aimy == player->y);
    player->flags &= ~MF_SHADOW;
    puts("PASS: projectile lead is partial, bounded, and respects invisibility");

    reset();
    actor = spawn(MT_TROOP, 80, 0);
    mobj_t *ally = spawn(MT_TROOP, 80, 48);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    assert(P_CheckPosition(ally, ally->x, ally->y));
    assert(P_CheckSight(actor, ally));
    fixed_t gap = P_AproxDistance(actor->x - ally->x, actor->y - ally->y);
    assert(P_WorthyChase(actor));
    assert(P_AproxDistance(actor->x - ally->x, actor->y - ally->y) > gap);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    puts("PASS: nearby allies spread out while respecting map collision");

    reset();
    actor = spawn(MT_TROOP, 80, 0);
    player->angle = R_PointToAngle2(player->x, player->y, actor->x, actor->y);
    players[consoleplayer].attackdown = true;
    assert(P_WorthyChase(actor));
    assert(actor->worthy.threat_since == leveltime + 1);
    leveltime += 8;
    oldy = actor->y;
    assert(P_WorthyChase(actor));
    assert(actor->y != oldy);
    puts("PASS: sustained visible fire produces a sidestep after reaction time");

    reset();
    actor = NULL;
    for (int dy = -512; dy <= 512 && !actor; dy += 64) {
        for (int dx = -512; dx <= 512 && !actor; dx += 64) {
            if (abs(dx) + abs(dy) < 192) continue;
            mobj_t *candidate = spawn(MT_TROOP, dx, dy);
            if (P_CheckPosition(candidate, candidate->x, candidate->y)
                && !P_CheckSight(candidate, player)) actor = candidate;
            else P_RemoveMobj(candidate);
        }
    }
    assert(actor);
    oldx = actor->x; oldy = actor->y;
    assert(P_WorthyChase(actor));
    assert(actor->x == oldx && actor->y == oldy);
    actor->worthy.known = 1;
    actor->worthy.x = actor->x + 32 * FRACUNIT;
    actor->worthy.y = actor->y;
    actor->worthy.contact_time = leveltime;
    aimx = player->x; aimy = player->y;
    P_WorthyAim(actor, player, 10 * FRACUNIT, &aimx, &aimy);
    assert(aimx == actor->worthy.x && aimy == actor->worthy.y);
    leveltime += 9 * TICRATE;
    assert(P_WorthyChase(actor));
    assert(actor->x == oldx && actor->y == oldy);
    puts("PASS: occluded enemies cannot track current position; old contact expires");
    reset();
    actor = spawn(MT_POSSESSED, -256, 96);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    assert(P_CheckSight(actor, player));
    actor->worthy.next_attack = 0;
    player->health = players[consoleplayer].health = 10000;
    P_SetMobjState(actor, actor->info->seestate);
    int hidden_ticks = 0, exposed_ticks = 0, returned = 0;
    for (int t = 0; t < 440; t++) {
        leveltime++;
        P_MobjThinker(actor);
        assert(P_CheckPosition(actor, actor->x, actor->y));
        if (actor->worthy.cover_state == WORTHY_WAIT) {
            assert(!P_CheckSight(actor, player));
            hidden_ticks++;
            if (actor->worthy.cover_cycles) returned = 1;
        }
        if (actor->worthy.cover_state == WORTHY_FIRE && P_CheckSight(actor, player)) exposed_ticks++;
    }
    assert(hidden_ticks >= 20 && exposed_ticks > 0 && returned);
    assert(player->health < 10000);
    puts("PASS: a zombie reaches real cover, waits concealed, peeks, shoots and returns");

    reset();
    actor = spawn(MT_SERGEANT, -320, 96);
    assert(P_CheckPosition(actor, actor->x, actor->y));
    assert(!P_WorthyWalkSegment(actor, actor->x, actor->y, player->x, player->y));
    actor->worthy.known = 1;
    actor->worthy.x = player->x; actor->worthy.y = player->y; actor->worthy.z = player->z;
    actor->worthy.contact_time = leveltime;
    int used_route = 0, turns = 0, reversals = 0, previous = 8;
    for (int t = 0; t < 80 && distance(actor) > 64 * FRACUNIT; t++) {
        leveltime += 2;
        oldx = actor->x; oldy = actor->y;
        P_WorthyChase(actor);
        assert(P_CheckPosition(actor, actor->x, actor->y));
        assert(abs(actor->x - oldx) <= actor->info->speed * FRACUNIT);
        assert(abs(actor->y - oldy) <= actor->info->speed * FRACUNIT);
        if (actor->worthy.route_count) used_route = 1;
        if (previous < 8 && actor->movedir < 8 && actor->movedir != previous) {
            turns++;
            if ((actor->movedir - previous + 8) % 8 == 4) reversals++;
        }
        previous = actor->movedir;
    }
    assert(used_route && distance(actor) <= 64 * FRACUNIT);
    assert(turns <= 10 && reversals == 0);
    puts("PASS: a demon follows a route around an obstruction without reversing or wall clipping");

    reset();
    actor = spawn(MT_POSSESSED, -256, 96);
    actor->worthy.known = 1;
    actor->worthy.x = player->x; actor->worthy.y = player->y; actor->worthy.z = player->z;
    leveltime++;
    assert(P_WorthyFindCover(actor));
    assert(P_WorthyCoverValid(actor));
    actor->worthy.cover_state = WORTHY_HIDE;
    ally = spawn(MT_POSSESSED, -256, 160);
    assert(P_CheckPosition(ally, ally->x, ally->y));
    ally->worthy.known = 1;
    ally->worthy.x = player->x; ally->worthy.y = player->y; ally->worthy.z = player->z;
    leveltime++;
    if (P_WorthyFindCover(ally)) {
        assert(P_AproxDistance(actor->worthy.hide_x - ally->worthy.hide_x,
            actor->worthy.hide_y - ally->worthy.hide_y) >= actor->radius + ally->radius + 16 * FRACUNIT);
    }
    puts("PASS: a second monster chooses separate cover or falls back to open combat");
    // A hidden player's new coordinates must not influence speculative LOS.
    oldx = player->x; oldy = player->y;
    player->x += 1024 * FRACUNIT; player->y += 1024 * FRACUNIT;
    assert(P_WorthyCoverValid(actor));
    player->x = oldx; player->y = oldy;
    actor->worthy.x = actor->worthy.hide_x; actor->worthy.y = actor->worthy.hide_y;
    assert(!P_WorthyCoverValid(actor));
    puts("PASS: cover uses the remembered threat position and fails when that position exposes it");

    reset();
    actor = spawn(MT_TROOP, 80, 0);
    actor->flags |= MF_SKULLFLY;
    oldx = actor->x; oldy = actor->y;
    int health = player->health;
    assert(P_WorthyWalkSegment(actor, actor->x, actor->y, player->x, player->y));
    assert(actor->x == oldx && actor->y == oldy && player->health == health);
    assert(actor->flags & MF_SKULLFLY);
    assert(!P_WorthyWalkSegment(actor, actor->x, actor->y, actor->x + 128 * FRACUNIT, actor->y));
    puts("PASS: navigation probes test body clearance without moving, attacking, or touching actors");
    // One policy must respond differently to observed health, and disabling
    // or rejecting it must restore neutral outputs rather than stale weights.
    policy_net_t net;
    float weights[POLICY_WEIGHTS] = {0}, inputs[POLICY_INPUTS] = {0};
    float outputs[POLICY_OUTPUTS];
    weights[1] = 2; weights[105] = 2;
    P_PolicyNetLoad(&net, weights);
    inputs[0] = 1;
    P_PolicyNetEvaluate(&net, inputs, outputs);
    assert(outputs[0] > .9f);
    inputs[0] = -1;
    P_PolicyNetEvaluate(&net, inputs, outputs);
    assert(outputs[0] < -.9f);
    weights[0] = NAN;
    P_PolicyNetLoad(&net, weights);
    P_PolicyNetEvaluate(&net, inputs, outputs);
    assert(!net.enabled && outputs[0] == 0);
    puts("PASS: neural tactics respond to observations and invalid networks reset safely");
    reset();
    Worthy_UseChampion(1);
    assert(P_WorthyEnabled());
    actor = spawn(MT_POSSESSED, -256, 96);
    actor->worthy.next_attack = 0;
    player->health = players[consoleplayer].health = 10000;
    P_SetMobjState(actor, actor->info->seestate);
    for (int t = 0; t < 440; t++) {
        leveltime++;
        P_MobjThinker(actor);
        assert(P_CheckPosition(actor, actor->x, actor->y));
    }
    assert(player->health < 10000);
    int remembered = 0;
    for (int i = 0; i < TACTIC_MEMORY; i++) {
        assert(isfinite(actor->worthy.neural_memory[i]));
        remembered |= actor->worthy.neural_memory[i] != 0;
    }
    assert(remembered); // The browser champion actually uses the recurrent path.
    Worthy_UseChampion(0);
    assert(!P_WorthyEnabled());
    oldx = actor->x; oldy = actor->y;
    assert(!P_WorthyChase(actor));
    assert(actor->x == oldx && actor->y == oldy);
    puts("PASS: the embedded neural champion fights legally and toggles back to classic AI");
    float recurrent[TACTIC_WEIGHTS] = {0}, context[TACTIC_INPUTS] = {0};
    float memory[TACTIC_MEMORY] = {0}, actions[TACTIC_OUTPUTS];
    // Store a sighting in memory, then react to it after the observation ends.
    recurrent[1] = 1;
    recurrent[(TACTIC_INPUTS + TACTIC_MEMORY + 1) + 1 + TACTIC_INPUTS] = 1;
    const int outputs_at = (TACTIC_INPUTS + TACTIC_MEMORY + 1) * TACTIC_HIDDEN;
    recurrent[outputs_at + 2] = 1;
    recurrent[outputs_at + TACTIC_OUTPUTS * (TACTIC_HIDDEN + 1) + 1] = 1;
    P_PolicyNetLoadRecurrent(&net, recurrent);
    context[0] = 1;
    P_PolicyNetRecurrent(&net, context, memory, actions);
    assert(actions[0] == 0 && memory[0] > .5f);
    context[0] = 0;
    P_PolicyNetRecurrent(&net, context, memory, actions);
    assert(actions[0] > .4f);
    memset(memory, 0, sizeof(memory));
    P_PolicyNetRecurrent(&net, context, memory, actions);
    assert(actions[0] == 0);
    puts("PASS: recurrent policies remember past observations and reset independently");
    mobjtype_t shooters[] = {MT_POSSESSED, MT_SHOTGUY, MT_CHAINGUY};
    void (*shoot[])(mobj_t *) = {A_PosAttack, A_SPosAttack, A_CPosAttack};
    for (int kind = 0; kind < 3; kind++) {
        reset();
        Worthy_UseChampion(1);
        actor = spawn(shooters[kind], -256, 96);
        assert(P_CheckPosition(actor, actor->x, actor->y) && P_WorthyCanFire(actor));
        player->health = players[consoleplayer].health = 100000;
        int hits = 0;
        for (int shot = 0; shot < 100; shot++) {
            int health = player->health;
            shoot[kind](actor);
            hits += player->health < health;
        }
        assert(hits == 100);
        if (kind == 0) {
            // Invisibility and classic mode still produce ordinary aim errors.
            for (int mode = 0; mode < 2; mode++) {
                if (mode == 0) player->flags |= MF_SHADOW;
                else { player->flags &= ~MF_SHADOW; Worthy_UseChampion(0); }
                int misses = 0;
                for (int shot = 0; shot < 100; shot++) {
                    int health = player->health;
                    shoot[kind](actor);
                    misses += player->health == health;
                }
                assert(misses > 0);
            }
        }
    }
    puts("PASS: hybrid pistol, shotgun and chaingun hit 100/100 stationary targets; invisibility and classic spread remain");
    reset();
    Worthy_UseChampion(1);
    actor = spawn(MT_POSSESSED, 80, 0);
    blocker = spawn(MT_TROOP, 40, 0);
    health = player->health;
    A_PosAttack(actor);
    assert(player->health == health && !P_WorthyCanFire(actor));
    P_RemoveMobj(blocker); P_RunThinkers();
    actor->reactiontime = 5;
    assert(!P_CheckMissileRange(actor));
    actor->reactiontime = 0;
    for (int i = 0; i < 100; i++) assert(P_CheckMissileRange(actor));
    player->momy = 8 * FRACUNIT;
    aimx = player->x; aimy = player->y;
    P_WorthyAim(actor, player, 10 * FRACUNIT, &aimx, &aimy);
    double flight = hypot((double)aimx - actor->x, (double)aimy - actor->y) / (10 * FRACUNIT);
    assert(fabs((aimy - player->y) / (double)FRACUNIT - 8 * flight) < .01);
    // Projectile targeting no longer depends on a learned lead parameter.
    worthy_policy_t altered = worthy_champion_policy;
    altered.lead = 0;
    P_WorthySetPolicy(&altered);
    fixed_t againx = player->x, againy = player->y;
    P_WorthyAim(actor, player, 10 * FRACUNIT, &againx, &againy);
    assert(againx == aimx && againy == aimy);
    puts("PASS: hybrid fire respects shot blockers and reaction time; projectile interception is independent of learned aim");
    puts("All Worthy Adversaries engine scenarios passed.");
    return 0;
}
