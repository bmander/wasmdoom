# Recurrent curriculum experiment, version 4

This protocol precedes the longer run's final evaluation. The current browser
champion remains the enemy from `results/neural-v3`; training never changes it.

- Network: 24 observations + 8 per-actor memory values → 32 tanh hidden units →
  10 tactical outputs + 8 next-memory values; 1,650 weights. Both sides train.
  Warm starts preserve the previous controller's actions exactly. This is
  reward-driven, gradient-free policy search (antithetic evolution strategies
  and Adam proposals), not PPO or pixel-based learning.
- Environment: original pistol drills plus random reachable rooms, 3–7 monsters,
  mixed melee/hitscan/projectile roles, partly occluded spawns, pistol/shotgun/
  chaingun loadouts, and occasional green armor. Expanded drills last 45 seconds.
  Monster health, speed, damage, animations and minimum attack delay stay normal.
  Spawn geometry is checked before combat; only invalid layouts are rejected.
- Development maps: E1M1–E1M5; training seed family 100,000,000, validation family
  500,000,000. Mix easy and harder drills throughout training; increase the harder
  fraction after eight generations. Selection cases are shared across candidates.
- Opponents: the shipped enemy, the strongest parameter player, current opponents,
  historical champions, and aggressive/cautious player styles. An enemy promotion
  must improve validation score against its league and not regress against the
  fixed player/style panel. Player promotion is similarly anchored to the shipped
  enemy. Draws are always reported and do not count as wins.
- Budget: one hour of local CPU training with four workers at reduced priority,
  followed by evaluation. Checkpoint after each side's update; support exact RNG/
  optimizer resume between updates. Save compressed raw matches and all model IDs.
- Final test: freeze first, then use seed family 920,000,000 on development maps
  and unseen E1M6–E1M8. Use 32 admitted encounters per map per scenario (0/1/2),
  shared by all matchups. Baseline, trained-player-only, trained-enemy-only,
  trained-both, and two fixed player styles versus both enemy policies.
- Report paired 95% bootstrap intervals for score and win-rate changes. Enemy
  claims require both lower bounds above zero across the fixed three-player
  panel; report unseen-map transfer separately. No automatic browser promotion.

A STOP file finishes training at the next completed update and starts evaluation.
SIGTERM/SIGINT pauses after an update and saves a resumable checkpoint instead.
Resume is rejected after final evaluation, or when the source fingerprint changes.
All final results, including regressions and inconclusive results, are retained.

Smoke tests use a separate 850,000,000 seed family. The 900,000,000 family was used
by an initial infrastructure smoke test and is excluded from the long run. Final
matchups also replay serially with disruptive short predecessors; any mismatch
fails evaluation rather than silently accepting order-dependent outcomes.

## Second-hour continuation

After the first run finished at generation 63, the user requested another hour.
The continuation preserves policies, optimizer moments, RNG, opponent pools, and
generation cursor in a separate `.cache/recurrent-v4-hour2` experiment. Training
settings and environment remain the same; four workers run for another 60 minutes.
The first run and its held-out results remain frozen. New final tests use seed
family 940,000,000, with the same admission rules, scenarios, styles, sample sizes,
bootstrap intervals and serial replay checks. Additional cells evaluate the parent
player and enemy, so gains from this second hour can be measured directly.
Maps E1M6–E1M8 remain excluded from training, though their aggregate first-run
results have now been seen. No architecture or hyperparameter tuning is based on
those results. Browser play continues to use the frozen first-hour enemy.
