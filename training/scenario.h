// GPL-2.0-or-later. Deterministic mixed-loadout combat curriculum.
#ifndef TRAINING_SCENARIO_H
#define TRAINING_SCENARIO_H
#include "p_mobj.h"
#define SCENARIO_MAX_ENEMIES 7
void Scenario_Capture(void);
int Scenario_Build(player_t *player, unsigned seed, int mode, mobj_t **enemies, int *count);
#endif
