// Bounded local navigation and cover queries. GPL-2.0-or-later.
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "doomstat.h"
#include "p_local.h"
#include "p_worthy.h"
#include "r_state.h"

extern boolean P_Move(mobj_t *actor);
extern fixed_t tmdropoffz;
extern fixed_t xspeed[8], yspeed[8];

#define GRID 33
#define CELL (32 * FRACUNIT)
#define CENTER (GRID / 2)
#define START (CENTER * GRID + CENTER)
#define EXPANSIONS 256

// Shared scratch, never retained by a monster or serialized. Searches are
// synchronous and capped globally so large crowds cannot all plan in one tic.
static struct { int cost, parent; unsigned char state; } nav_nodes[GRID * GRID];
static int budget_time = -1, plans;
static boolean planning_budget(void)
{
    if (budget_time != leveltime) { budget_time = leveltime; plans = 0; }
    if (plans >= 2) return false;
    plans++;
    return true;
}

boolean P_WorthyWalkSegment(mobj_t *actor, fixed_t x, fixed_t y,
                            fixed_t to_x, fixed_t to_y)
{
    fixed_t dx = to_x - x, dy = to_y - y;
    fixed_t length = P_AproxDistance(dx, dy);
    if (length > 768 * FRACUNIT) return false;
    int steps = length / (16 * FRACUNIT) + 1;
    fixed_t floor = R_PointInSubsector(x, y)->sector->floorheight;
    for (int i = 1; i <= steps; i++) {
        fixed_t px = x + (int64_t)dx * i / steps;
        fixed_t py = y + (int64_t)dy * i / steps;
        if (!P_CheckPositionGeometry(actor, px, py)) return false;
        if (tmceilingz - tmfloorz < actor->height) return false;
        if (!(actor->flags & MF_FLOAT)) {
            if (tmfloorz - floor > 24 * FRACUNIT
                || tmceilingz - floor < actor->height) return false;
            if (!(actor->flags & MF_DROPOFF)
                && tmfloorz - tmdropoffz > 24 * FRACUNIT) return false;
        }
        floor = tmfloorz;
    }
    return true;
}

static fixed_t node_x(mobj_t *a, int n) { return a->x + (n % GRID - CENTER) * CELL; }
static fixed_t node_y(mobj_t *a, int n) { return a->y + (n / GRID - CENTER) * CELL; }
static int heuristic(fixed_t x, fixed_t y, fixed_t gx, fixed_t gy)
{
    return P_AproxDistance(gx - x, gy - y) / FRACUNIT;
}

// Query the estimated player's view of a prospective position. Stack copies
// only: no teleporting, relinking, pickup, damage, or door activation in probes.
static boolean exposed(mobj_t *actor, fixed_t x, fixed_t y)
{
    mobj_t observer = *actor->target, probe = *actor;
    observer.x = actor->worthy.x;
    observer.y = actor->worthy.y;
    observer.z = actor->worthy.z;
    observer.subsector = R_PointInSubsector(observer.x, observer.y);
    probe.x = x; probe.y = y;
    probe.subsector = R_PointInSubsector(x, y);
    probe.z = (actor->flags & MF_FLOAT) ? actor->z : probe.subsector->sector->floorheight;
    return P_CheckSight(&observer, &probe);
}

static boolean sheltered(mobj_t *actor, fixed_t x, fixed_t y)
{
    if (exposed(actor, x, y)) return false;
    fixed_t dx = x - actor->worthy.x, dy = y - actor->worthy.y;
    fixed_t dist = P_AproxDistance(dx, dy);
    if (dist < FRACUNIT) return false;
    fixed_t sx = FixedMul(FixedDiv(-dy, dist), actor->radius);
    fixed_t sy = FixedMul(FixedDiv(dx, dist), actor->radius);
    return !exposed(actor, x + sx, y + sy) && !exposed(actor, x - sx, y - sy);
}

boolean P_WorthyCoverValid(mobj_t *actor)
{
    return sheltered(actor, actor->worthy.hide_x, actor->worthy.hide_y)
        && exposed(actor, actor->worthy.peek_x, actor->worthy.peek_y)
        && P_WorthyWalkSegment(actor, actor->worthy.hide_x, actor->worthy.hide_y,
                              actor->worthy.peek_x, actor->worthy.peek_y);
}

static boolean cover_reserved(mobj_t *actor, fixed_t x, fixed_t y)
{
    int checked = 0;
    for (thinker_t *th = thinkercap.next; th != &thinkercap && checked < 128; th = th->next) {
        checked++;
        if (th->function.acp1 != (actionf_p1)P_MobjThinker) continue;
        mobj_t *other = (mobj_t *)th;
        if (other == actor || other->health <= 0 || !other->worthy.cover_state) continue;
        if (P_AproxDistance(x - other->worthy.hide_x, y - other->worthy.hide_y)
            < actor->radius + other->radius + 16 * FRACUNIT) return true;
    }
    return false;
}

static void keep_route(mobj_t *actor, int end)
{
    int reverse[GRID * GRID], count = 0;
    while (end != START && end >= 0 && count < GRID * GRID) {
        reverse[count++] = end;
        end = nav_nodes[end].parent;
    }
    actor->worthy.route_count = actor->worthy.route_index = 0;
    while (count && actor->worthy.route_count < 32) {
        int n = reverse[--count], i = actor->worthy.route_count++;
        actor->worthy.route_x[i] = node_x(actor, n);
        actor->worthy.route_y[i] = node_y(actor, n);
    }
}

// A* for pursuit, Dijkstra for reachable cover. A partial path is useful for
// distant goals; it is reused instead of rescoring directions every animation.
static boolean search(mobj_t *actor, fixed_t gx, fixed_t gy, boolean cover)
{
    static const int ox[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int oy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    memset(nav_nodes, 0, sizeof(nav_nodes));
    nav_nodes[START].state = 1;
    nav_nodes[START].parent = -1;
    int best = START, best_h = heuristic(actor->x, actor->y, gx, gy);
    for (int expansion = 0; expansion < EXPANSIONS; expansion++) {
        int current = -1, score = INT_MAX;
        for (int n = 0; n < GRID * GRID; n++) {
            if (nav_nodes[n].state != 1) continue;
            int f = nav_nodes[n].cost + (cover ? 0 : heuristic(node_x(actor, n), node_y(actor, n), gx, gy));
            if (f < score) { score = f; current = n; }
        }
        if (current < 0) break;
        nav_nodes[current].state = 2;
        fixed_t x = node_x(actor, current), y = node_y(actor, current);
        int h = heuristic(x, y, gx, gy);
        if (!cover && h < best_h) { best = current; best_h = h; }
        if (!cover && h < 40 && P_WorthyWalkSegment(actor, x, y, gx, gy)) break;
        if (cover && nav_nodes[current].cost > 256) break;
        if (cover && h >= 128 && h <= 640 && sheltered(actor, x, y)
            && !cover_reserved(actor, x, y)) {
            // A hide position is useful only with a short, traversable peek.
            for (int step = 1; step <= 3; step++) for (int d = 0; d < 8; d++) {
                fixed_t px = x + ox[d] * CELL * step;
                fixed_t py = y + oy[d] * CELL * step;
                if (!exposed(actor, px, py)
                    || !P_WorthyWalkSegment(actor, x, y, px, py)) continue;
                actor->worthy.hide_x = x; actor->worthy.hide_y = y;
                actor->worthy.peek_x = px; actor->worthy.peek_y = py;
                keep_route(actor, current);
                actor->worthy.goal_x = x; actor->worthy.goal_y = y;
                return true;
            }
        }
        for (int d = 0; d < 8; d++) {
            int nx = current % GRID + ox[d], ny = current / GRID + oy[d];
            if (nx < 0 || nx >= GRID || ny < 0 || ny >= GRID) continue;
            int next = ny * GRID + nx;
            int cost = nav_nodes[current].cost + ((d & 1) ? 45 : 32);
            if (nav_nodes[next].state == 2 || (nav_nodes[next].state && nav_nodes[next].cost <= cost)) continue;
            if (!P_WorthyWalkSegment(actor, x, y, node_x(actor, next), node_y(actor, next))) continue;
            nav_nodes[next].state = 1; nav_nodes[next].cost = cost; nav_nodes[next].parent = current;
        }
    }
    if (cover || best == START) return false;
    keep_route(actor, best);
    actor->worthy.goal_x = gx; actor->worthy.goal_y = gy;
    return true;
}

boolean P_WorthyFindCover(mobj_t *actor)
{
    if (!planning_budget()) return false;
    return search(actor, actor->worthy.x, actor->worthy.y, true);
}

void P_WorthyNavigate(mobj_t *actor, fixed_t gx, fixed_t gy,
                     fixed_t space_x, fixed_t space_y)
{
    fixed_t reach = actor->info->speed * FRACUNIT + 4 * FRACUNIT;
    if (P_AproxDistance(gx - actor->x, gy - actor->y) <= reach) {
        actor->movedir = 8;
        return;
    }
    if (P_AproxDistance(gx - actor->worthy.goal_x, gy - actor->worthy.goal_y) > 96 * FRACUNIT)
        actor->worthy.route_count = actor->worthy.route_index = 0;
    while (actor->worthy.route_index < actor->worthy.route_count) {
        int i = actor->worthy.route_index;
        if (P_AproxDistance(actor->worthy.route_x[i] - actor->x,
                            actor->worthy.route_y[i] - actor->y) > reach) break;
        actor->worthy.route_index++;
    }
    if (actor->worthy.route_index >= actor->worthy.route_count
        && leveltime >= actor->worthy.route_retry
        && !P_WorthyWalkSegment(actor, actor->x, actor->y, gx, gy)
        && planning_budget()) {
        actor->worthy.route_count = actor->worthy.route_index = 0;
        search(actor, gx, gy, false);
        actor->worthy.route_retry = leveltime + 18;
    }
    if (actor->worthy.route_index < actor->worthy.route_count) {
        // Smooth just the next few corners, checking the monster's full width.
        int i = actor->worthy.route_index;
        for (int j = i + 1; j < actor->worthy.route_count && j <= i + 3; j++) {
            if (!P_WorthyWalkSegment(actor, actor->x, actor->y,
                actor->worthy.route_x[j], actor->worthy.route_y[j])) break;
            actor->worthy.route_index = j;
        }
        i = actor->worthy.route_index;
        gx = actor->worthy.route_x[i]; gy = actor->worthy.route_y[i];
    }
    fixed_t dx = gx - actor->x, dy = gy - actor->y;
    fixed_t dist = P_AproxDistance(dx, dy);
    if (dist < FRACUNIT) dist = FRACUNIT;
    fixed_t wx = FixedDiv(dx, dist) + space_x / 4;
    fixed_t wy = FixedDiv(dy, dist) + space_y / 4;
    int previous = actor->movedir;
    int64_t scores[8];
    for (int d = 0; d < 8; d++) {
        scores[d] = (int64_t)xspeed[d] * wx + (int64_t)yspeed[d] * wy;
        if (d == previous) scores[d] += (int64_t)FRACUNIT * FRACUNIT / 16;
    }
    // Try forward alternatives, then wait/replan. Do not cycle through all
    // eight directions and walk backwards just because a doorway is crowded.
    for (int attempt = 0; attempt < 3; attempt++) {
        int best = 0;
        for (int d = 1; d < 8; d++) if (scores[d] > scores[best]) best = d;
        if (scores[best] <= 0) break;
        scores[best] = INT64_MIN;
        fixed_t oldx = actor->x, oldy = actor->y;
        actor->movedir = best;
        if (P_Move(actor)) {
            if (actor->x != oldx || actor->y != oldy) actor->worthy.blocked = 0;
            // Successful door use may stand still: give the door time to open.
            actor->movecount = 0;
            return;
        }
    }
    if (++actor->worthy.blocked >= 3) {
        actor->worthy.route_count = actor->worthy.route_index = 0;
        actor->worthy.blocked = 0;
    }
    actor->movedir = 8;
}
