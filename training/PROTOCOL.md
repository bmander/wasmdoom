# Improvement experiment, version 3

Frozen before the final test is run. Earlier inconclusive experiments stay in
`results/first-run` and `results/validated-run`.

The reference controllers are the original player bot and Worthy Adversaries
policy, represented by defaults in the expanded parameter schema. New decisions
default to the original behavior. Health, damage, movement limits, attack
animations and the minimum attack delay are unchanged.

Development uses separate deterministic combat encounters on E1M1–E1M3. Candidate
selection uses shared scenario batches, successive elimination, a league of
opponents, and a separate larger validation batch. Final candidates are frozen
before final evaluation; final-test results never select candidates.

The primary final test uses seed family **9,000,000**, 512 encounters per map on
E1M1–E1M3 (1,536 encounters per matchup). Seeds for each map are the base plus
10,000 times its map number plus the encounter index. The four matchups are
original/original, trained-player/original-enemies, original-player/trained-enemies,
and trained/trained. Additional generalization tests use 256 encounters each on
E1M4 and E1M5, which are excluded from development.

Success requires **both sides** to improve their win rate and mean game score
against the same original opponent on the primary fresh encounters, with positive
lower bounds in paired 97.5% bootstrap intervals. These intervals are deliberately
stricter than the earlier 95% intervals. Unseen-map results and all draws will be
reported separately; no claim of full-campaign or human-opponent competence is made.

If this test fails, preserve that result and declare a different final seed family
before another experiment. Do not repeatedly tune against this test set.

## Neural comparison (specified before final evaluation)

Train a parameter-only pair first, then residual 12–8–5 tanh networks with 149
weights each, initialized to preserve those parameter controllers. Evaluate both
pairs against the original opponents and each other. The neural pair is the
primary trained pair for the criterion above. Neural-versus-parameter differences
are reported separately, including uncertainty; a gain over the original does
not by itself demonstrate that neural adaptation improved on parameter search.
Also ablate each network's input connections at evaluation, preserving its learned
biases and output connections, to distinguish state-dependent decisions from
constant tactical offsets. These ablations cannot select final candidates.

## Evaluation infrastructure correction

The first frozen evaluation completed the primary test but stopped at impossible
layout E1M4/9040095. Its partial records remain in `results/final-v3`. The same
frozen policies are evaluated in `results/final-v3b` with admission preflight:
run one tick using original controllers solely to detect the arena's explicit
“No valid combat spawn” error. Only that error rejects a seed; no combat result
can reject or select a case. Continue ascending seeds per map until the requested
count is admitted, and record every rejection and accepted seed. The primary
cases remain identical. A replay audit checks results against the first attempt;
the correction below records the discrepancies it found. This correction neither
selects new policies nor treats invalid layouts as wins, losses, or draws.

## Episode reset correction

Replay of `final-v3b` found three order-dependent health/timing differences in
the constant-enemy ablation, despite unchanged wins and primary comparisons.
A one-tick predecessor reproduced the fault: navigation's per-tick planning
budget could remain at tick zero across episodes. Reset that transient budget
at arena initialization. Preserve both previous evaluations and evaluate the
**same frozen policies and admitted seeds** in `final-v3c`; verify all final
matches again with one worker and compare them with the four-worker run. No
policy selection or extra training uses these test results. Training used the
prior arena revision; its exact sources remain archived with each run.
