# Offline self-play laboratory

The player bot and enemy policies fight short combat drills in the **actual native
DOOM engine**. Neither rendering nor a browser is needed for training. This is
gradient-free search over tactical parameters in existing controllers, not a
neural network learning from pixels. Trained policies are experiment artifacts;
the browser continues to use the original Worthy Adversaries settings.

Read [the first two experiments and their findings](RESULTS.md).

```sh
npm run train -- --output .cache/my-experiment
npm run test:training
```

Python 3 and a C compiler are sufficient. The command automatically compiles the
arena, then saves `checkpoint.json`, `summary.json`, `matches.jsonl`, and a
self-contained `report.html`. Open the HTML file directly in a browser. Choose a
new output directory for every run; existing experiments are preserved.

For the larger second experiment:

```sh
npm run train -- --generations 16 --population 20 --train-seeds 16 \
  --validation-seeds 64 --evaluation-seeds 64 --evaluation-base 2000000 \
  --output .cache/validated-experiment
```

## What learns

- **Player:** preferred range, lateral movement, advance/retreat speed, aim
  response, firing tolerance, strafe direction timing, and turning speed.
- **Enemies:** preferred range, cover wait/retry/peek timing, dodge reaction,
  approach offset, attack delay, projectile lead, melee interception, and
  willingness to seek cover. The same enemy policy controls the encounter's team;
  existing monster roles and per-spawn personalities still vary their behavior.
- Health, damage, monster movement speeds, weapons, collision, and attack
  animations retain normal DOOM rules. Player commands stay within ordinary
  movement input bounds. Faster-than-default monster attacks are not allowed.

The player bot observes visible enemies at 8.75 Hz and remembers their last
observed position. Enemy awareness uses the existing sight/sound memory. Both
use structured world coordinates and geometry queries; neither controller reads
the current position of an occluded opponent to choose its next destination.
This deliberately avoids the much larger problem of learning visual perception.

## Encounters and scoring

Each encounter loads E1M1 or E1M2, clears the existing things, and places two or
three ordinary monsters in valid visible positions around the player start. The
mixture includes zombies, shotgun zombies, imps, and demons. The player starts
with the normal pistol, ammunition, and health. There are no artificial arenas or
replacement combat physics; these are combat drills in existing map geometry,
rather than full-level completion tasks. A separate deterministic scenario RNG
chooses spawns, and DOOM's gameplay RNG is seeded for each match.

A player win requires killing every opponent. Player death is a loss. Reaching
the time limit is a draw. The player reward is:

```
win/loss/draw (+1/-1/0)
  + 0.25 × (fraction of enemy health removed - fraction of player health lost)
```

Enemy reward is its exact negative. The smaller health term provides feedback
between decisive outcomes, without rewarding time spent alive. Infighting and
normal enemy drops remain possible under the engine's rules. Reports show draws
separately so stalling cannot be mistaken for winning.

## Selection and evaluation

Each generation alternates a player update and an enemy update. Mutated children
and their incumbent face identical scenarios and a small league: the original
opponent, the latest opponent, and a sampled historical opponent. Duplicate
policies are removed from that mixture. The best child is tested on a separate
validation batch. It is promoted only if its average score improves and its
score against the original opponent does not regress on that batch.

The final checkpoint is chosen entirely before final evaluation. The evaluation
uses fresh seeds on E1M1, E1M2, and E1M3; E1M3 is not used for training or
promotion. Initial, middle, and final policies play a cross-match matrix. Paired
bootstrap intervals compare final and initial policies against the same original
opponent and encounters. `unseen_map_improvement` reports E1M3 separately.

Final evaluation never feeds back into policy selection. If its results inform
a new experiment design, use a new `--evaluation-base` and preserve the earlier
run. A positive score change is not proof of broader competence; interval width,
draw rates, opponent diversity, and unseen-map results all matter. The first
experiment used much smaller selection batches and is retained as a negative
result for enemy generalization.

The algorithm is simple mutation and selection, related to the policy-search
approach explored in [Evolution Strategies as a Scalable Alternative to
Reinforcement Learning](https://arxiv.org/abs/1703.03864). It is not an
implementation or reproduction of that paper's algorithm.

## Artifacts and reproduction

- [First experiment](results/first-run/report.html): version 1, eight enemy
  parameters, four encounters per opponent for both selection and validation.
  Its exact code is commit `f54f15f`; use that revision to reproduce its format.
- [Larger validation experiment](results/validated-run/report.html): version 2,
  adds learnable melee interception and cover use, more encounters per opponent,
  and a check against forgetting the original opponent. Multiple design changes
  mean this is a follow-up experiment, not an isolated causal comparison.

Each result records its configuration, parameter names, league, promotions, raw
outcomes, and a SHA-256 fingerprint of the engine/trainer/IWAD sources. Repeating
an encounter after an unrelated match must produce the identical result; the
trainer checks this before selection. Native results can depend on compiler and
platform, so exact reproduction should use the same build environment.

Checkpoints contain policy data for subsequent experiments and a future browser
watch mode. Resuming an interrupted optimizer and replaying a match in the browser
are not implemented yet. To replay an individual result offline, pass its player
and enemy vectors, map, seed, and duration to `training.selfplay.Arena.play`.
