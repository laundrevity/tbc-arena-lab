# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""M5 experiment configuration — pinned BEFORE training (repo rule 6 spirit).

Everything that affects the experiment outcome lives here: network size,
PPO hyperparameters, all seeds, evaluation N, and the pass criterion. The
training run writes this config verbatim into its output directory so the
run is auditable against what was pinned.

Pass criterion (pinned 2026-06-10, before the first training run):

  vs Idle (sanity, decisive): for each slot s, the learned policy plays
  `n_idle` seeded matches against IdlePolicy; PASS requires the 95% normal
  CI lower bound of its score (win=1, draw=0.5, loss=0) >= `idle_lb_min`
  in both slots.

  vs ScriptedPolicy (the real bar): the matchup is asymmetric (slot 0 has
  MS+HS, slot 1 HS only), so raw win rate is meaningless against 0.5.
  Instead, with `n_eval` seeded matches per matchup:
    base   = scripted0 vs scripted1   -> score of slot s = B_s
    test_s = learned in slot s vs scripted opponent -> score L_s
    delta_s = L_s - B_s, CI from independent-difference normal SE.
  PASS    requires: both slots delta_s CI lower bound > -regress_tol,
                    AND at least one slot delta_s CI lower bound > 0.
  MATCH   (soft pass per project plan "beats-or-matches"): both slots'
          delta CIs contain 0 and the vs-Idle criterion passes.
  FAIL    otherwise.
  Additionally illegal_actions must be exactly 0 across all eval matches
  (action-mask correctness; scripted policies never trigger it).

Draws (duration end) score 0.5; CIs are normal approximations on the mean
of the bounded per-match score (N is large; draw rates are reported).
"""

import dataclasses
import json

OBS_DIM = 17
N_ACTIONS = 4


@dataclasses.dataclass(frozen=True)
class TrainConfig:
    scenario: str = "scenarios/m2_duel.yaml"
    # network
    hidden: int = 64
    # ppo
    lr: float = 3e-4
    gamma: float = 0.997
    gae_lambda: float = 0.95
    clip: float = 0.2
    vf_coef: float = 0.5
    ent_coef: float = 0.005
    epochs: int = 4
    minibatch: int = 2048
    adv_norm: str = "minibatch"  # advantages normalized per minibatch
    # rollout
    num_envs: int = 32
    horizon: int = 256          # vec ticks per iteration
    total_samples: int = 5_000_000  # slot-samples (2 per env tick)
    # reward
    shape_coef: float = 1.0     # potential-based: shape*(gamma*phi' - phi),
                                # phi = self_hp_frac - enemy_hp_frac
    win_reward: float = 1.0     # terminal: +1 win / -1 loss / 0 draw
    # seeds (all of them)
    train_seed: int = 1          # numpy rng: init + action sampling + shuffles
    env_seed_base: int = 1_000_000  # episode k uses env seed env_seed_base + k

    def to_json(self):
        return json.dumps(dataclasses.asdict(self), indent=2, sort_keys=True)


@dataclasses.dataclass(frozen=True)
class EvalConfig:
    n_eval: int = 10_000        # matches per scripted matchup (base, slot0, slot1)
    n_idle: int = 2_000         # matches per idle matchup (per slot)
    eval_seed_base: int = 9_000_000  # match k of a matchup uses this + k
    z: float = 1.96             # 95% normal CI
    idle_lb_min: float = 0.90
    regress_tol: float = 0.02
    eval_envs: int = 128        # vectorization width (no effect on results)

    def to_json(self):
        return json.dumps(dataclasses.asdict(self), indent=2, sort_keys=True)
