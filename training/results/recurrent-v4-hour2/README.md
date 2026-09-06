# Second-hour recurrent policy playtest

Frozen output of the additional hour described in ../../COOK_PROTOCOL.md.
The run completed 1,101,266 additional matches and reached generation 127
(player update completed, next role enemy). It accepted 18 player and 12 enemy
updates. Full development logs and source snapshot are in `.cache/recurrent-v4-hour2`.

`frozen.json` contains both final policies; only the enemy is embedded in the
browser. `evaluation.json` contains the fresh-seed results, including direct
comparisons against the first-hour policies and 9,216 successful serial replays.
`progress.json` contains the completed second-hour training history.

Compared with the first-hour enemy, panel win rate changed by -0.347 percentage
points on familiar maps (95% interval -1.806 to +1.181) and +0.926 points on
unseen maps (-0.810 to +2.431). Neither establishes improvement. This checkpoint
is selected at the user's request for playtesting, not as a proven stronger AI.
The first-hour checkpoint remains in `../recurrent-v4`.

The local Worthy Adversaries checkbox loads this enemy policy. GitHub Pages
changes only after a separate publish.
