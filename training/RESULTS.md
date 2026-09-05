# First offline self-play experiments

**The training pipeline works, but neither experiment establishes a reliable
overall improvement for both sides.** The browser still uses the original
hand-tuned policy. Both runs, including the disappointing results, are retained.

Across the two runs, the arena simulated **38,076 training/evaluation matches**
in about four minutes of simulation wall time, plus compilation and verification.
Each drill permits up to 20 seconds of game time and ends sooner on a win/loss.

## Run 1: small selection batches

[Report](results/first-run/report.html) · [Raw results](results/first-run/matches.jsonl)

5,596 matches; 16 generations; 16 candidates per side; four encounters per
opponent in selection and four in validation. Six player and eight enemy policy
updates were promoted. Final evaluation used 96 encounters per matchup.

Against the original enemy policy, player wins increased from **82/96 to 87/96**.
Against the original player policy, enemy wins fell from **14/96 to 10/96**.
Both overall paired score-change confidence intervals included zero. Training
gains did not establish stronger opponents outside the selection encounters.

The exact version 1 experiment source is commit `f54f15f`.

## Run 2: larger validation batches and baseline protection

[Report](results/validated-run/report.html) · [Raw results](results/validated-run/matches.jsonl)

32,480 matches; 16 generations; 20 candidates per side; 16 selection and 64
validation encounters per opponent. Promotion also required no score regression
against the original opponent on the validation batch. Enemy policies gained two
additional choices: melee interception and whether to seek cover. Nine player
and six enemy updates were promoted. Final evaluation used a fresh seed family
with **192 encounters per matchup**, including 64 on E1M3, which was excluded
from training and promotion.

| Comparison against the original opponent | Initial wins | Final wins | Mean score change | Paired 95% interval |
|---|---:|---:|---:|---:|
| Player policy | 168/192 | 167/192 | +0.013 | −0.088 to +0.109 |
| Enemy policy | 24/192 | 23/192 | −0.004 | −0.097 to +0.096 |

Both results are **inconclusive**, with almost unchanged win rates. On E1M3
alone, score changes were −0.156 for the player (interval −0.411 to +0.105) and
+0.001 for enemies (−0.235 to +0.236), also inconclusive.

The final player defeated the final enemies in 175/192 encounters, but that
number by itself does not measure learning: both sides changed. The fixed
original-opponent comparisons above are the more useful progress check.

## What the policies changed

The second run's player widened its preferred distance from 192 to 396 map
units, strafed more strongly, and reversed strafe direction more often. Enemies
reduced their approach offset from 64 to 24 units and selected cover for roughly
73% of spawn personalities instead of all of them. These are learned parameter
choices, not evidence of new skills or reliable performance gains.

This limited controller family and short encounter distribution provide a useful
starting benchmark. A sensible next experiment is a policy that chooses among
explicit tactical actions from observations—engage, flank, retreat, or cover—
with more varied positions, loadouts, and opponent styles. A browser watch mode
would also make failure patterns easier to inspect before expanding training.

These runs use different held-out seed families; their absolute win rates should
not be compared as though they were the same test set. The second run changes
several design choices, so its result does not isolate which change helped or
hurt. Environment metadata and complete named policy schemas are saved alongside
the raw match results.
