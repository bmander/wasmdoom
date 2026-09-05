// Integration scenarios using real E1M1 geometry and engine movement/LOS.
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "doomgeneric.h"
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "g_game.h"

static uint32_t clock_ms = 1000;
void DG_Init(void) {}
void DG_DrawFrame(void) {}
void DG_SleepMs(uint32_t ms) { clock_ms += ms; }
uint32_t DG_GetTicksMs(void) { return clock_ms; }
int DG_GetKey(int *pressed, unsigned char *key) { return 0; }
void DG_SetWindowTitle(const char *title) {}
extern void P_RunThinkers(void);

static mobj_t *player;
static void reset(void)
{
    P_WorthySetEnabled(0);
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
    puts("All Worthy Adversaries engine scenarios passed.");
    return 0;
}
