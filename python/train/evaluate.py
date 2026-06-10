# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""M5 statistical evaluation against the pinned criterion (config.py).

Matchups (asymmetric scenario, so per-slot deltas vs the scripted-vs-
scripted baseline, NOT raw win rate vs 0.5):

  base    scripted0 vs scripted1          (baseline slot scores B_s)
  slot0   learned0  vs scripted1          (L_0)
  slot1   scripted0 vs learned1           (L_1)
  idle0   learned0  vs idle1              (sanity, decisive)
  idle1   idle0     vs learned1           (sanity, decisive)

All matchups share seed lists (common random numbers); the learned policy
evaluates greedily (argmax), so every match is deterministic and scores are
proper Bernoulli-with-draws samples over seeds. Draws score 0.5.

Usage:
    TBC_ARENA_LIB=build/libarena_env.so python3 python/train/evaluate.py \
        --checkpoint runs/m5/checkpoint.npz --out runs/m5/eval.json [--quick]
"""

import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from config import N_ACTIONS, OBS_DIM, EvalConfig  # noqa: E402
from envs import (IdleVecPolicy, ScriptedVecPolicy, featurize,  # noqa: E402
                  mask_bits_to_vec, parse_scripted_thresholds)
from net import MLP  # noqa: E402
from tbc_arena import ArenaEnv  # noqa: E402


class LearnedVecPolicy:
    """Greedy (argmax) policy from a training checkpoint."""

    def __init__(self, checkpoint_path):
        data = np.load(checkpoint_path, allow_pickle=False)
        meta = json.loads(str(data["_meta"]))
        self.meta = meta
        self.net = MLP(meta["obs_dim"], meta["n_actions"],
                       meta["config"]["hidden"], np.random.default_rng(0))
        self.net.load_state({k: data[k] for k in self.net.PARAM_ORDER})

    def __call__(self, raw, mask):
        return self.net.greedy(featurize(raw.astype(np.int64)), mask)


def play_matchup(scenario, seeds, pol0, pol1, lib_path=None, chunk=128,
                 collect_results=False):
    """Plays one deterministic match per seed. Returns slot0 scores (N,),
    stats dict, and optionally per-match result dicts."""
    scores = np.empty(len(seeds), dtype=np.float64)
    draws = deaths = illegal = 0
    results = [] if collect_results else None
    for lo in range(0, len(seeds), chunk):
        batch_seeds = seeds[lo:lo + chunk]
        envs = [ArenaEnv(scenario, int(s), lib_path=lib_path) for s in batch_seeds]
        active = list(range(len(envs)))
        while active:
            n = len(active)
            raw = np.empty((2, n, OBS_DIM), dtype=np.int64)
            mask = np.empty((2, n, N_ACTIONS), dtype=np.float32)
            for j, i in enumerate(active):
                raw[0, j] = envs[i].observe_raw(0)
                raw[1, j] = envs[i].observe_raw(1)
                mask[0, j] = mask_bits_to_vec(envs[i].action_mask(0))
                mask[1, j] = mask_bits_to_vec(envs[i].action_mask(1))
            a0 = pol0(raw[0], mask[0])
            a1 = pol1(raw[1], mask[1])
            still = []
            for j, i in enumerate(active):
                if not envs[i].step(int(a0[j]), int(a1[j])):
                    still.append(i)
                    continue
                res = envs[i].result()
                w = res["winner_slot"]
                scores[lo + i] = 1.0 if w == 0 else (0.0 if w == 1 else 0.5)
                draws += w == -1
                deaths += res["end_reason"] == "death"
                illegal += res["illegal_actions"]
                if collect_results:
                    results.append((int(batch_seeds[i]), res))
                envs[i].close()
            active = still
    stats = {"n": len(seeds), "draws": draws, "deaths": deaths,
             "illegal_actions": illegal}
    return scores, stats, results


def mean_ci(scores, z):
    m = float(scores.mean())
    se = float(scores.std(ddof=1) / np.sqrt(len(scores))) if len(scores) > 1 else 0.0
    return {"mean": round(m, 4), "lb": round(m - z * se, 4),
            "ub": round(m + z * se, 4), "se": round(se, 5)}


def delta_ci(a, b, z):
    """CI for mean(a) - mean(b), independent normal SE."""
    d = float(a.mean() - b.mean())
    se = float(np.sqrt(a.var(ddof=1) / len(a) + b.var(ddof=1) / len(b)))
    return {"delta": round(d, 4), "lb": round(d - z * se, 4),
            "ub": round(d + z * se, 4), "se": round(se, 5)}


def evaluate(checkpoint, scenario, ecfg, lib_path=None, verbose=True):
    thr = parse_scripted_thresholds(scenario)
    scripted = [ScriptedVecPolicy(thr[0]), ScriptedVecPolicy(thr[1])]
    idle = IdleVecPolicy()
    learned = LearnedVecPolicy(checkpoint)
    z = ecfg.z
    seeds_s = np.arange(ecfg.eval_seed_base, ecfg.eval_seed_base + ecfg.n_eval)
    seeds_i = np.arange(ecfg.eval_seed_base, ecfg.eval_seed_base + ecfg.n_idle)

    def log(msg):
        if verbose:
            print(msg, flush=True)

    out = {"checkpoint": os.path.basename(os.path.dirname(checkpoint) or checkpoint),
           "param_hash": learned.meta["param_hash"],
           "eval_config": json.loads(ecfg.to_json()), "matchups": {}}

    log(f"base: scripted vs scripted, N={ecfg.n_eval}")
    base, st, _ = play_matchup(scenario, seeds_s, scripted[0], scripted[1],
                               lib_path, ecfg.eval_envs)
    out["matchups"]["base"] = {**mean_ci(base, z), **st}

    log(f"slot0: learned vs scripted, N={ecfg.n_eval}")
    l0, st0, _ = play_matchup(scenario, seeds_s, learned, scripted[1],
                              lib_path, ecfg.eval_envs)
    out["matchups"]["learned_slot0"] = {**mean_ci(l0, z), **st0}

    log(f"slot1: scripted vs learned, N={ecfg.n_eval}")
    l1raw, st1, _ = play_matchup(scenario, seeds_s, scripted[0], learned,
                                 lib_path, ecfg.eval_envs)
    l1 = 1.0 - l1raw  # slot1 perspective
    out["matchups"]["learned_slot1"] = {**mean_ci(l1, z), **st1}

    log(f"idle: learned vs idle (both slots), N={ecfg.n_idle}")
    i0, sti0, _ = play_matchup(scenario, seeds_i, learned, idle,
                               lib_path, ecfg.eval_envs)
    i1raw, sti1, _ = play_matchup(scenario, seeds_i, idle, learned,
                                  lib_path, ecfg.eval_envs)
    i1 = 1.0 - i1raw
    out["matchups"]["idle_slot0"] = {**mean_ci(i0, z), **sti0}
    out["matchups"]["idle_slot1"] = {**mean_ci(i1, z), **sti1}

    # -- pinned criterion (config.py docstring) --------------------------------
    base1 = 1.0 - base  # baseline from slot1 perspective
    d0 = delta_ci(l0, base, z)
    d1 = delta_ci(l1, base1, z)
    out["delta_slot0"] = d0
    out["delta_slot1"] = d1
    idle_ok = (out["matchups"]["idle_slot0"]["lb"] >= ecfg.idle_lb_min
               and out["matchups"]["idle_slot1"]["lb"] >= ecfg.idle_lb_min)
    illegal_ok = all(out["matchups"][m]["illegal_actions"] == 0
                     for m in out["matchups"])
    no_regress = d0["lb"] > -ecfg.regress_tol and d1["lb"] > -ecfg.regress_tol
    strict_improve = d0["lb"] > 0 or d1["lb"] > 0
    contains_zero = (d0["lb"] <= 0 <= d0["ub"]) and (d1["lb"] <= 0 <= d1["ub"])
    if idle_ok and illegal_ok and no_regress and strict_improve:
        verdict = "PASS"
    elif idle_ok and illegal_ok and contains_zero:
        verdict = "MATCH"
    else:
        verdict = "FAIL"
    out["criterion"] = {"idle_ok": idle_ok, "illegal_ok": illegal_ok,
                        "no_regress": no_regress,
                        "strict_improve": strict_improve,
                        "verdict": verdict}
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", required=True)
    ap.add_argument("--scenario", default="scenarios/m2_duel.yaml")
    ap.add_argument("--out", default=None)
    ap.add_argument("--lib", default=None)
    ap.add_argument("--quick", action="store_true",
                    help="N/20 smoke run (NOT the pinned criterion)")
    args = ap.parse_args()
    ecfg = EvalConfig()
    if args.quick:
        import dataclasses
        ecfg = dataclasses.replace(ecfg, n_eval=ecfg.n_eval // 20,
                                   n_idle=ecfg.n_idle // 20)
    report = evaluate(args.checkpoint, args.scenario, ecfg, args.lib)
    text = json.dumps(report, indent=2)
    if args.out:
        with open(args.out, "w") as f:
            f.write(text + "\n")
    print(text)


if __name__ == "__main__":
    main()
