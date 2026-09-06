# Recurrent v4 browser candidate

Frozen output of the one-hour run described in ../../COOK_PROTOCOL.md.
`frozen.json` contains both policies; the browser exporter embeds only the enemy.
`evaluation.json` records fresh-seed evaluation and successful serial replay of
all 6,144 evaluation matches. `progress.json` records the final training history.
Full development logs and the engine snapshot remain in `.cache/recurrent-v4`.

Across three fixed player styles on familiar maps, enemy wins increased from
179/1,440 to 206/1,440 (+1.875 percentage points; encounter-clustered 95% bootstrap
interval +0.278 to +3.472 points). The score improvement also passed the primary
gate. Gains on unseen maps were inconclusive. This is a modest benchmark gain,
not evidence of a large increase in difficulty against human players.

This first-hour candidate is preserved. The local browser now uses the
second-hour candidate in `../recurrent-v4-hour2` for user playtesting.
