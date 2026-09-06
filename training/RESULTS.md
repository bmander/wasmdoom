# Offline policy results

## Version 4: two hours of recurrent self-play

The [first hour](results/recurrent-v4/README.md) completed 1,038,544 matches.
Enemy panel win rate increased by 1.875 percentage points on fresh encounters
in familiar maps, with a positive 95% interval; unseen-map transfer remained
inconclusive. All 6,144 evaluation replays passed.

The [second hour](results/recurrent-v4-hour2/README.md) completed another 1,101,266
matches. Direct comparisons against the first-hour policies did not establish
further improvement. All 9,216 evaluation replays passed. These experiments use
1,650-weight recurrent policies and the richer [curriculum protocol](COOK_PROTOCOL.md).

The browser uses the second-hour enemy for playtesting, with a subsequently added
deterministic shooting controller and neural movement/cover. These archived
training results do not evaluate that hybrid shooting behavior.

## Version 3 overview

**Version 3 improves both sides on fresh encounters in the training maps.** The
player's improvement also carries to two unseen maps; enemy generalization there
is inconclusive. The neural stage adds a promising enemy gain, but its advantage
over parameter search alone is not statistically established.

[Final report](results/final-v3c/report.html) · [Complete metrics](results/final-v3c/summary.json) ·
[Neural checkpoints](results/neural-v3/checkpoint.json) · [Protocol](PROTOCOL.md)

## Version 3: parameter search followed by neural policies

Each side has a 12–8–5 tanh network with **149 weights and biases**. CPU mutation
and selection train residual tactical decisions from structured observations.
The parameter run used 162,792 development matches; neural training used 204,912.
Both ran eight generations with 48 candidates, shared screening batches,
successive elimination, and larger independent validation batches. Parameter
search took 404.5 seconds and neural training 375.7 seconds, excluding compilation
and later verification. One player and two enemy neural updates were promoted.

The final evaluation froze both pairs before playing **1,536 fresh encounters per
matchup** on E1M1–E1M3. These are paired comparisons against the same original
opponent; trained-versus-trained wins do not establish learning on their own.

| Side against its original opponent | Original wins | Neural-stage wins | Win-rate gain | Paired 97.5% interval | Score gain | Paired 97.5% interval |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| Player | 1,339/1,536 (87.2%) | 1,490/1,536 (97.0%) | +9.83 pp | +7.81 to +11.85 pp | +0.271 | +0.229 to +0.316 |
| Enemy team | 195/1,536 (12.7%) | 231/1,536 (15.0%) | +2.34 pp | +0.26 to +4.49 pp | +0.057 | +0.011 to +0.100 |

All four lower bounds are positive, satisfying the predeclared primary criterion.
Draws were 2 in the original matchup, 11 with the neural-stage player, and 1 with
the neural enemies. Health, damage, weapons, movement limits, attack animations,
and minimum enemy attack delay retain the original rules.

## What did the neural stage add?

The parameter-only player already won **1,493/1,536** encounters. Adding its
network yielded 1,490 wins: a −0.20 pp difference with interval −1.37 to +0.91 pp.
The player network therefore has **no demonstrated advantage** over the trained
parameter controller.

The parameter-only enemies won **201/1,536**, compared with **231/1,536** after
neural training. That +1.95 pp difference has interval **−0.20 to +4.04 pp**;
its score interval also includes zero. The point estimate is encouraging, but
it does not establish that the additional neural training reliably beats the
parameter pair. This comparison also includes extra training compute.

Removing each network's input connections, while retaining its biases and output
weights, yielded 1,492 player wins and 212 enemy wins against the originals.
Both neural-versus-constant intervals include zero. The networks contain learned
observation-dependent connections, but this experiment does **not** establish
that state dependence itself caused a reliable performance improvement.

## Unseen maps and limits

On **512 encounters** split between E1M4 and E1M5, the neural-stage player improved
from **446 to 500 wins** (+10.55 pp; interval +6.84 to +14.26 pp). Enemy wins
changed from **66 to 59** (−1.37 pp; interval −5.08 to +2.54 pp), an inconclusive
result with a negative point estimate. Enemy gains have not demonstrated transfer
to unfamiliar map geometry.

These are 20-second pistol combat drills near map starts, involving two or three
zombies, shotgun zombies, imps, or demons. They do not measure full-level
completion, visual perception, or difficulty against human players. This neural
enemy was used in browser playtesting before the version 4 policies above.
Bot-versus-bot watch mode remains future work.

## Infrastructure corrections and audit trail

An early development run stopped when monsters blocked all remaining spawn
locations. The arena now retries the layout before combat. That interrupted
checkpoint remains in `results/parameter-v3`; the completed parameter run is
`results/parameter-v3b`. A separate development-only probe is retained in
`results/enemy-probe-v3`.

The first frozen evaluation completed its primary cases, then encountered an
impossible E1M4 layout. Evaluation admission now rejects only the explicit
“No valid combat spawn” error and takes the next seed on the same map. Exactly
**one** unseen-map seed, 9040095, was rejected and replaced. No primary seed was
rejected. Accepted cases and the rejection reason are in
[admission.json](results/final-v3c/admission.json).

A replay check then found three health/timing differences in the constant-enemy
ablation caused by a navigation budget surviving from a preceding episode. The
arena now resets that transient budget at initialization. The same frozen
checkpoints and admitted seeds were evaluated again; primary improvement metrics
and all win counts remained unchanged. Earlier evaluation attempts remain in
`results/final-v3` and `results/final-v3b`. Training used the prior arena revision,
whose exact sources are archived with each training run. No final-test result
selected or retrained a policy.

The final matrix contains **18,432 matches**. A separate serial audit replays the
four-worker results, inserting one-tick predecessors to stress episode reset;
its detailed result is in [replay-audit.json](results/final-v3c/replay-audit.json).
Training and evaluation folders include losslessly compressed raw logs, model
dictionaries, source snapshots, compiler/platform metadata, and fingerprints.

---

## Earlier experiments

**Neither of the first two experiments established a reliable overall
improvement for both sides.** At that time the browser used the original hand-tuned policy. Both runs, including the disappointing results, are retained.

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
