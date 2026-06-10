# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""Vectorized self-play wrapper over ArenaEnv + feature encoding + baselines.

Vectorization is plain Python over N independent envs (the profiled ctypes
loop is nowhere near the wall for M5; see docs/benchmarks.md). Determinism:
episode k (global counter, single-threaded) always gets env seed
`seed_base + k`, so a training run is a pure function of its config.
"""

import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from tbc_arena import Action, ArenaEnv  # noqa: E402

N_ACTIONS = 4
OBS_DIM = 17

# OBS_FIELDS indices (canonical order, docs/observation_action_spec.md)
I_T, I_REM, I_HP, I_MAXHP, I_RAGE, I_GCD, I_MSCD, I_SWING, I_HSQ, \
    I_KMS, I_KHS, I_WSPD, I_EHP, I_ERAGE, I_EFRONT, I_SFRONT, I_DIST = range(17)


def featurize(raw):
    """raw int64 (..., 17) -> float32 (..., 17), all fields ~[-1, 1]."""
    r = raw.astype(np.float32)
    f = np.empty_like(r)
    f[..., I_T] = r[..., I_T] / 60000.0
    f[..., I_REM] = r[..., I_REM] / 60000.0
    f[..., I_HP] = r[..., I_HP] / np.maximum(r[..., I_MAXHP], 1.0)
    f[..., I_MAXHP] = r[..., I_MAXHP] / 25000.0
    f[..., I_RAGE] = r[..., I_RAGE] / 1000.0
    f[..., I_GCD] = r[..., I_GCD] / 1500.0
    f[..., I_MSCD] = np.maximum(r[..., I_MSCD], 0.0) / 6000.0
    f[..., I_SWING] = r[..., I_SWING] / np.maximum(r[..., I_WSPD], 1.0)
    f[..., I_HSQ] = r[..., I_HSQ]
    f[..., I_KMS] = r[..., I_KMS]
    f[..., I_KHS] = r[..., I_KHS]
    f[..., I_WSPD] = r[..., I_WSPD] / 3600.0
    f[..., I_EHP] = r[..., I_EHP] / 10000.0
    f[..., I_ERAGE] = r[..., I_ERAGE] / 1000.0
    f[..., I_EFRONT] = r[..., I_EFRONT]
    f[..., I_SFRONT] = r[..., I_SFRONT]
    f[..., I_DIST] = r[..., I_DIST] / 1000.0
    return f


def phi_from_raw(raw):
    """Potential: own hp fraction minus enemy hp fraction, per slot."""
    return (raw[..., I_HP] / np.maximum(raw[..., I_MAXHP], 1)
            - raw[..., I_EHP] / 10000.0).astype(np.float32)


def mask_bits_to_vec(mask_int):
    return np.array([(mask_int >> a) & 1 for a in range(N_ACTIONS)],
                    dtype=np.float32)


def parse_scripted_thresholds(scenario_path):
    """Slot-ordered scripted_hs_min_rage_deci values from the scenario file.

    Sections appear in entity_id order in the pinned fixtures (slots are
    ascending entity_id); validated against native runs by test_train.py.
    """
    vals = []
    with open(scenario_path) as f:
        for line in f:
            line = line.split("#", 1)[0].strip()
            if line.startswith("scripted_hs_min_rage_deci:"):
                vals.append(int(line.split(":", 1)[1].strip()))
    if len(vals) != 2:
        raise ValueError(f"expected 2 scripted thresholds in {scenario_path}")
    return vals


# -- fixed baseline policies (vectorized) -------------------------------------

class ScriptedVecPolicy:
    """Mirror of native ScriptedPolicy (sim/mechanics/policy.h):
    CastMortalStrike if legal; else QueueHeroicStrike if legal and
    rage >= threshold (0 = never); else None."""

    def __init__(self, threshold_deci):
        self.thr = threshold_deci

    def __call__(self, raw, mask):
        n = raw.shape[0]
        a = np.zeros(n, dtype=np.int64)
        ms_ok = mask[:, Action.CAST_MORTAL_STRIKE] > 0
        hs_ok = ((mask[:, Action.QUEUE_HEROIC_STRIKE] > 0)
                 & (self.thr > 0) & (raw[:, I_RAGE] >= self.thr))
        a[hs_ok] = Action.QUEUE_HEROIC_STRIKE
        a[ms_ok] = Action.CAST_MORTAL_STRIKE  # MS priority overrides HS
        return a


class IdleVecPolicy:
    def __call__(self, raw, mask):
        return np.zeros(raw.shape[0], dtype=np.int64)


# -- vectorized self-play env --------------------------------------------------

class VecSelfPlay:
    """N independent ArenaEnvs; finished envs are replaced with the next
    global episode seed. Rewards: terminal win/loss/draw (+1/-1/0) plus
    potential-based shaping shape*(gamma*phi' - phi)."""

    def __init__(self, scenario, num_envs, seed_base, gamma, shape_coef,
                 win_reward=1.0, lib_path=None):
        self.scenario = scenario
        self.gamma = float(gamma)
        self.shape = float(shape_coef)
        self.win = float(win_reward)
        self.lib_path = lib_path
        self.seed_base = seed_base
        self.episodes_started = 0
        self.envs = [self._new_env() for _ in range(num_envs)]
        self.max_hp = np.empty((num_envs, 2), dtype=np.int64)
        for i, e in enumerate(self.envs):
            r0, r1 = e.observe_raw(0), e.observe_raw(1)
            self.max_hp[i, 0], self.max_hp[i, 1] = r0[I_MAXHP], r1[I_MAXHP]
        self.illegal_total = 0
        self.completed_scores = [[], []]  # per slot, terminal scores

    def _new_env(self):
        env = ArenaEnv(self.scenario, self.seed_base + self.episodes_started,
                       lib_path=self.lib_path)
        self.episodes_started += 1
        return env

    def observe_all(self):
        """raw int64 (E,2,17), mask float32 (E,2,4); also caches phi."""
        E = len(self.envs)
        raw = np.empty((E, 2, OBS_DIM), dtype=np.int64)
        mask = np.empty((E, 2, N_ACTIONS), dtype=np.float32)
        for i, env in enumerate(self.envs):
            raw[i, 0] = env.observe_raw(0)
            raw[i, 1] = env.observe_raw(1)
            m0, m1 = env.action_mask(0), env.action_mask(1)
            mask[i, 0] = mask_bits_to_vec(m0)
            mask[i, 1] = mask_bits_to_vec(m1)
        self.phi = phi_from_raw(raw)  # (E,2), potential of the observed state
        return raw, mask

    def step_all(self, actions):
        """actions (E,2) int. Steps every env; replaces finished ones.

        Returns (rewards_terminal_part, dones, phi_pre) where the SHAPING
        part for non-terminal transitions cannot be computed yet (it needs
        phi of the next observation): callers add
        shape*(gamma*phi_next - phi_pre) for non-done envs after the next
        observe_all(). Terminal transitions are fully rewarded here using
        the engine's final hp.
        """
        E = len(self.envs)
        rewards = np.zeros((E, 2), dtype=np.float32)
        dones = np.zeros(E, dtype=np.float32)
        phi_pre = self.phi.copy()
        for i, env in enumerate(self.envs):
            done = env.step(int(actions[i, 0]), int(actions[i, 1]))
            if not done:
                continue
            dones[i] = 1.0
            res = env.result()
            self.illegal_total += res["illegal_actions"]
            hp0, hp1 = res["hp"]
            mh0, mh1 = int(self.max_hp[i, 0]), int(self.max_hp[i, 1])
            # final potential, same form as phi_from_raw (enemy hp per-myriad)
            phi_f0 = hp0 / max(mh0, 1) - (hp1 * 10000 // max(mh1, 1)) / 10000.0
            phi_f1 = hp1 / max(mh1, 1) - (hp0 * 10000 // max(mh0, 1)) / 10000.0
            w = res["winner_slot"]
            term0 = self.win if w == 0 else (-self.win if w == 1 else 0.0)
            rewards[i, 0] = term0 + self.shape * (self.gamma * phi_f0 - phi_pre[i, 0])
            rewards[i, 1] = -term0 + self.shape * (self.gamma * phi_f1 - phi_pre[i, 1])
            self.completed_scores[0].append(0.5 + term0 / (2 * self.win))
            self.completed_scores[1].append(0.5 - term0 / (2 * self.win))
            env.close()
            self.envs[i] = self._new_env()
            r0 = self.envs[i].observe_raw(0)
            r1 = self.envs[i].observe_raw(1)
            self.max_hp[i, 0], self.max_hp[i, 1] = r0[I_MAXHP], r1[I_MAXHP]
        return rewards, dones, phi_pre

    def pop_completed(self):
        s0, s1 = self.completed_scores
        self.completed_scores = [[], []]
        return s0, s1

    def close(self):
        for e in self.envs:
            e.close()
