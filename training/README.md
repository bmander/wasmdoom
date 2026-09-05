# Offline policy laboratory

Player bots and enemy teams train in the actual native DOOM engine. The lab now
supports **parameter search and neural policies**. Both use CPU mutation and
selection against a league of opponents; neither needs Python packages, a GPU,
or a browser. Learned policies remain offline artifacts. Browser watch mode is
future work, and the playable site's Worthy Adversaries defaults stay unchanged.

Read [results](RESULTS.md) and the [frozen evaluation protocol](PROTOCOL.md).

## Run an experiment

Python 3 and a C compiler are sufficient; the trainer builds the arena.

```sh
# First improve the parameter controllers.
npm run train:league -- --output .cache/parameters

# Then train state-dependent neural adjustments to those controllers.
npm run train:league -- --neural --initial .cache/parameters/checkpoint.json \
  --development-base 5000000 --output .cache/neural

# Freeze both checkpoints and evaluate on a fresh seed family.
# Choose a NEW --base for subsequent experiments; 9000000 belongs to version 3.
npm run train:evaluate -- --parameters .cache/parameters/checkpoint.json \
  --neural .cache/neural/checkpoint.json --base 11000000 --output .cache/evaluation

npm run test:training
npm run test:ai
```

Each command needs an empty output directory. Four native worker processes are
used by default (`--workers` changes this). `--generations`, `--population`,
`--screen-cases`, `--train-cases`, and `--validation-cases` control the budget.
Case counts are **per map**. A checkpoint can initialize a subsequent run; this
starts a new search with a new RNG, rather than resuming an interrupted search.
The earlier optimizer remains available as `npm run train`.

## Neural policies

Each side has its own **12-input, 8-hidden-unit, 5-output tanh MLP**, with 149
trainable weights and biases. Native C performs inference. All weights are bounded
in [-4, 4]; inputs are clipped to [-1, 1]. Initial hidden weights are random and
output weights are zero, so initialization exactly preserves the parent
controller. Evolution mutates hidden connections, output connections, and biases;
it also tries bias-only children. There is no backpropagation or imitation data.

These are residual tactical networks: they modulate an existing controller,
which still handles aiming, navigation, cover, and legal actions. This experiment
does not learn those engine skills from scratch or learn from pixels.

| Side | 12 observations, in input order | 5 outputs, in output order |
| --- | --- | --- |
| Player | Own health; pistol ammunition; nearest visible enemy distance, health, demon flag, hitscan flag; visible count; visibility flag; bearing error; visible enemy x/y velocity; contact age | Range, strafe amount, low-health target priority, hitscan target priority, aim response |
| Enemy | Own health; remembered distance; ranged flag; hitscan flag; visibility; under aimed fire; visible target radial/tangential velocity; cover state; contact age; blocked steps; attack cooldown | Range, willingness to seek cover, return fire under pressure, melee intercept lead, wounded spacing adjustment |

Player observations refresh every four game ticks (8.75 Hz). Enemy inference runs
at chase decisions. Hidden opponents' current coordinates and velocity are not
inputs; missing player observations are masked, and enemies use remembered
locations. Both controllers can query map geometry.

The flat checkpoint layout contains eight blocks of `[hidden bias, 12 weights]`,
then five blocks of `[output bias, 8 weights]`. The `network` field is null for
parameter-only controllers. The parameter array follows `PLAYER` or `ENEMY` in
[selfplay.py](selfplay.py). A zero-output network reproduces the parent controller.

The parameter search can additionally choose advance/retreat speed, firing
threshold, turn limits, strafe timing, cover timing, dodge reaction, flank offset,
attack spacing and projectile lead. Health, damage, monster speed, attack
animations, and weapons retain normal DOOM values. Player commands stay within
ordinary input bounds; attack delays cannot be faster than the shipped default.

## Encounters and selection

Each drill clears map things and places two or three ordinary zombies, shotgun
zombies, imps, or demons around the player start. The player begins with a pistol,
50 bullets, and 100 health. Geometry, collision, damage, infighting, and drops are
normal DOOM behavior. Crowded layouts retry before combat using a separate,
deterministic scenario RNG. Evaluation preflight logs any still-impossible layout
and takes the next seed on that map; it never rejects a combat outcome. The episode lasts at most 700 ticks (20 seconds).

Killing all enemies is a player win, player death is an enemy win, and a timeout
is a draw. The player score is:

```
win/loss/draw (+1/-1/0)
  + 0.25 × (fraction of enemy health removed - fraction of player health lost)
```

Enemy score is its exact negative. Reports show wins and draws alongside scores,
so simply stalling cannot count as winning.

Version 3 develops on E1M1–E1M3. Candidates share scenarios, the best six advance
to a larger batch, and the winning candidate faces separate validation encounters.
Opponents include the original controller, the latest adversary, and, after enough
promotions, a historical adversary. Promotion requires a positive league score
gain and no score regression against the original opponent on validation.

Final evaluation is a separate command that saves `frozen.json` **before** playing
any tests. Both sides face the same original opponent on paired fresh scenarios;
trained-versus-trained results are also reported. Paired 97.5% bootstrap intervals
measure score and win-rate differences. E1M4–E1M5 test unseen geometry. Neural
policies are additionally compared with the parameter pair and an ablation that
zeros input connections while preserving all biases and output weights.

A gain over the original controller alone cannot establish a benefit from neural
adaptation. These are short drills near five map starts, not full-level completion
or evidence of improved play against humans. If final results inform a subsequent
experiment, preserve them and declare another test seed family first.

## Artifacts and replay

Training saves `checkpoint.json`, `development.json`, `models.json`, and
`matches.jsonl`; evaluation saves `frozen.json`, `summary.json`, `models.json`, and
`matches.jsonl`. Model IDs in match logs resolve through `models.json`. Published
large logs are losslessly compressed as `matches.jsonl.gz`. Run manifests record
compiler/platform information and source fingerprints; native floating-point
results can differ across platforms.

To replay a logged encounter offline:

```python
from training.selfplay import Arena
# p/e are the model dictionaries referenced by the match's player/enemy IDs.
with Arena() as arena:
    result = arena.play(p['params'], e['params'], seed, map_id,
                        player_net=p['network'], enemy_net=e['network'])
```

The integration suite checks neural effects, neutral-network equivalence, invalid
weights, independent episode resets, crowded-layout recovery, parallel log replay, explicit invalid-layout admission,
and the full train/freeze/evaluate pipeline using separate smoke-test seeds (plus the exact invalid-layout regression seed).

Earlier artifacts retain their original schemas: version 1 is reproduced at
commit `f54f15f`, and version 2 at `96ec34a`. See their reports under
`results/first-run` and `results/validated-run`.
