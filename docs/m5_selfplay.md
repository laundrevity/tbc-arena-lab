# M5 — Self-Play Bring-Up: Experiment Record

Date: 2026-06-10. Harness: `python/train/` (numpy-only PPO, manual
gradients FD-checked to 8.9e-8, bit-reproducible same-seed training,
resumable checkpoints). Criterion and all seeds pinned in
`python/train/config.py` BEFORE the first run; revisions to the fixture,
gamma, opponent mixing, and the vs-Idle bar are dated in that file's
docstring and summarized below. Training ran on the owner's M5 Max (Linux
aarch64 VM, single core); a 5M-sample run takes ~32 s wall.

## Verdict

**The substrate trains. The pinned pass criterion is NOT met** — two
candidates each fail exactly one (different) condition. Reported per repo
rule 6: no rerolling until green.

## What was established (the milestone's actual purpose)

1. **Learning works end-to-end.** From random init, PPO self-play reaches
   scripted-level play: slot 0 matches ScriptedPolicy within 0.05%
   (N=10,000), and the 20M-sample policy is the first to *strictly beat*
   scripted anywhere: slot 1 delta +0.56%, 95% CI [+0.10%, +1.02%].
2. **The interface holds up under an adversarial consumer.** Zero illegal
   actions across ~20M training decisions and 68,000 evaluation matches —
   masks are exactly the engine's legality. Decision ticks, observation
   parity, and ctypes plumbing survived ~50M env steps without a fault.
3. **Determinism held everywhere it was promised**: same-seed training is
   bit-identical (sha256 of params), every eval match is a pure function
   of its seed, and the eval path reproduces native scripted runs exactly.

## Findings (each one cheap now, expensive later)

- **F1 — m2_duel@60s is draw-dominant.** 94% of scripted-vs-scripted
  matches draw (N=500); median natural kill time at 25k hp is 72 s, above
  the 60 s cap. Win/loss signal nearly vanishes for training AND
  evaluation. Fix: `scenarios/m5_duel.yaml` (owner-approved): 10k hp per
  side (community-documented unbuffed arena warrior pools; 25k was
  unrealistic), 180 s cap. Result: 100% death-decided, median kill 28.8 s.
- **F2 — absolute pass bars must be feasibility-checked.** The pinned
  vs-Idle bar (>= 0.90 both slots) is unsatisfiable for slot 1 by ANY
  policy: scripted slot 1 itself scores 0.502 ± 0.022 vs an idle slot 0
  (the 1H+shield kit barely out-damages an idle 2H). Criterion revised
  (dated, pre-full-eval): absolute bar kept only where the scripted
  reference proves feasibility (slot 0); both slots get relative deltas
  vs the scripted-vs-idle baseline.
- **F3 — pure self-play overfits its own distribution.** The first
  m5_duel policy matched scripted head-to-head but scored 0.5 vs IDLE as
  slot 1 — it had never seen an opponent that banks rage and never casts.
  Mitigation: `opponent_mix=0.25` (a quarter of training envs face fixed
  scripted/idle opponents; only learner-slot samples are trained on).
- **F4 — specialization/robustness trade-off at 20M samples.** Longer
  training sharpened the policy (entropy 0.56 -> 0.21) into slot-1 lines
  that beat scripted (+0.56%) but regress vs idle (0.36 vs 0.50 baseline)
  despite the mix. A memoryless 64x64 MLP at this sample budget does not
  dominate scripted across all four matchups simultaneously.

## Results (final candidates, full pinned eval)

Matchup scores are means over seeded matches (win 1, draw 0.5, loss 0),
95% normal CIs; deltas vs the scripted baseline in the same matchup.
Eval seeds 9,000,000+k disjoint from training seeds 1,000,000+k.

Candidate A — `runs/m5_mix` (5M samples, mix 0.25):

| measure | value | CI | verdict component |
|---|---|---|---|
| delta slot0 vs scripted | -0.03% | [-0.47%, +0.41%] | contains 0 (match) |
| delta slot1 vs scripted | -0.69% | [-1.10%, -0.28%] | **regression** -> FAIL |
| idle0 absolute | 0.993 | lb 0.989 | >= 0.90 ok |
| delta idle1 vs scripted-idle | +1.4% | [-1.7%, +4.5%] | ok |

Candidate B — `runs/m5_20m` (20M samples, mix 0.25, resumed segments):

| measure | value | CI | verdict component |
|---|---|---|---|
| delta slot0 vs scripted | -0.05% | [-0.49%, +0.39%] | contains 0 (match) |
| delta slot1 vs scripted | **+0.56%** | [+0.10%, +1.02%] | strict improvement |
| idle0 absolute | 0.993 | lb 0.989 | >= 0.90 ok |
| delta idle1 vs scripted-idle | -13.8% | [-16.8%, -10.8%] | **regression** -> FAIL |

Baselines (N=10,000 / 2,000): scripted0-vs-scripted1 slot-0 score 0.9743
(the MS kit dominates); scripted0-vs-idle 0.994; idle-vs-scripted1 0.497
from slot 1's view. All matches death-decided; illegal_actions = 0 in
every row. History runs: `runs/m5_seed1` (m2_duel fixture, FAIL per F1),
`runs/m5_final` (pure self-play, FAIL per F3).

## Recommendations for M6 (not built)

Robustness via opponent diversity is the open problem, not raw strength:
opponent pools (past checkpoints + scripted family across thresholds +
idle), larger mix fraction or prioritized sampling of weak matchups, and
possibly memory (the trade-off in F4 is partly an observability limit —
a memoryless policy cannot identify a passive opponent early). Batched
multi-env stepping remains unjustified: training throughput was ~150k
samples/s single-core; the profiled wall was never the ctypes loop.
