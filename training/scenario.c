// GPL-2.0-or-later. Real map rooms, reachable placements, ordinary DOOM loadouts.
#include <stdint.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "scenario.h"

#define GRID 33
#define CENTER (GRID / 2)
#define CELL (32 * FRACUNIT)
typedef struct { fixed_t x, y; } point_t;
static point_t anchors[512], sites[GRID * GRID];
static int anchor_count, site_count;
static uint32_t rng;
extern void P_RunThinkers(void);
static unsigned random_scene(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

void Scenario_Capture(void)
{
    anchor_count = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap; th = th->next) {
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mobj_t *m = (mobj_t *)th;
        if ((m->player || (m->flags & (MF_COUNTKILL | MF_SPECIAL))) && anchor_count < 512)
            anchors[anchor_count++] = (point_t){m->x, m->y};
    }
}

static void reachable_sites(mobj_t *player)
{
    unsigned char visited[GRID * GRID] = {0};
    int queue[GRID * GRID], head = 0, tail = 1;
    const int center = CENTER * GRID + CENTER;
    queue[0] = center; visited[center] = 1; site_count = 0;
    while (head < tail) {
        int n = queue[head++], nx = n % GRID, ny = n / GRID;
        point_t from = {player->x + (nx - CENTER) * CELL, player->y + (ny - CENTER) * CELL};
        if (P_AproxDistance(from.x - player->x, from.y - player->y) >= 160 * FRACUNIT)
            sites[site_count++] = from;
        static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int x = nx + dx[d], y = ny + dy[d];
            if (x < 0 || x >= GRID || y < 0 || y >= GRID) continue;
            int next = y * GRID + x;
            if (visited[next]) continue;
            point_t to = {player->x + (x - CENTER) * CELL, player->y + (y - CENTER) * CELL};
            if (!P_WorthyWalkSegment(player, from.x, from.y, to.x, to.y)) continue;
            visited[next] = 1;
            queue[tail++] = next;
        }
    }
}

int Scenario_Build(player_t *player, unsigned seed, int mode, mobj_t **enemies, int *count)
{
    mobj_t *self = player->mo;
    rng = seed ^ 0x9e3779b9u;
    if (!rng) rng = 1;
    *count = mode == 1 ? 3 + seed % 3 : 4 + seed % 4;
    static const mobjtype_t types[] = {MT_POSSESSED, MT_TROOP, MT_SHOTGUY, MT_SERGEANT, MT_HEAD, MT_SHADOWS};
    int total_health = 0;
    for (int layout = 0; layout < 12; layout++) {
        point_t p = anchors[random_scene() % anchor_count];
        if (!P_CheckPosition(self, p.x, p.y) || !P_TeleportMove(self, p.x, p.y)) continue;
        // Begin fights on safe ground; standing in acid is not enemy skill.
        int special = self->subsector->sector->special;
        if (special == 4 || special == 5 || special == 7 || special == 11 || special == 16) continue;
        self->z = self->floorz;
        player->viewz = self->z + player->viewheight;
        reachable_sites(self);
        if (site_count < *count * 3) continue;
        int made = 0;
        total_health = 0;
        for (int n = 0; n < *count; n++) {
            // Mix regular ranged and melee roles, with occasional flying threats.
            mobjtype_t type = types[random_scene() % (mode == 1 ? 4 : 6)];
            mobj_t *enemy = NULL;
            for (int attempt = 0; attempt < 400; attempt++) {
                point_t spot = sites[random_scene() % site_count];
                enemy = P_SpawnMobj(spot.x, spot.y, ONFLOORZ, type);
                int sight = P_CheckSight(enemy, self);
                // The first two threats must be visible to start combat. Later
                // members may wait out of sight; they receive no hidden pose.
                if (P_CheckPosition(enemy, spot.x, spot.y)
                    && enemy->ceilingz - enemy->floorz >= enemy->height
                    && (n >= 2 || sight)) break;
                P_RemoveMobj(enemy); enemy = NULL;
            }
            if (!enemy) break;
            enemies[made++] = enemy;
            total_health += enemy->health;
            enemy->target = self;
            enemy->reactiontime = 8;
            enemy->spawnpoint.x = enemy->x / FRACUNIT;
            enemy->spawnpoint.y = enemy->y / FRACUNIT;
        }
        if (made == *count) {
            weapontype_t weapon = mode == 1 ? (seed % 3 == 0 ? wp_pistol : wp_shotgun)
                                           : (seed % 2 ? wp_chaingun : wp_shotgun);
            player->weaponowned[weapon] = true;
            player->readyweapon = player->pendingweapon = weapon;
            player->ammo[am_clip] = mode == 1 ? 120 : 200;
            player->ammo[am_shell] = mode == 1 ? 24 : 40;
            player->armorpoints = mode == 2 && (seed & 2) ? 100 : 0;
            player->armortype = player->armorpoints ? 1 : 0;
            P_SetupPsprites(player);
            self->angle = (random_scene() & 7) * ANG45;
            return total_health;
        }
        for (int n = 0; n < made; n++) P_RemoveMobj(enemies[n]);
        P_RunThinkers();
    }
    return 0;
}
