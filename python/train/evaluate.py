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
  slot1   scripted0 vs learned1           (L_1, inverted to slot1 view)
  idle0   learned0  vs idle1              (sanity, decisive)
  idle1   idle0     vs learned1           (sanity, decisive, inverted)

All scripted matchups share one seed list (common random numbers); the
learned policy evaluates greedily (argmax), so every match is deterministic
and scores are proper per-seed samples. Draws score 0.5.

Evaluation is resumable: each matchup can be run in seed slices
(--slice name:start:count --outdir D) whose scores are saved as .npy;
--aggregate assembles full arrays, checks exact seed coverage, and applies
the criterion. One-shot in-process evaluation (evaluate()) gives identical
results — slicing only changes scheduling, never seeds.

Usage:
    TBC_ARENA_LIB=build/libarena_env.so python3 python/train/evaluate.py \
        --checkpoint runs/m5/checkpoint.npz --outdir runs/m5/eval \
        [--slice base:0:2000 | --aggregate | --quick]
"""

import argparse
import dataclasses
import glob
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
    stats dict, and optionally per-match (seed, result) pairs."""
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


def matchup_table(scenario, checkpoint, ecfg):
    """name -> (pol0, pol1, n_matches, invert_to_slot1_view)."""
    thr = parse_scripted_thresholds(scenario)
    s0, s1 = ScriptedVecPolicy(thr[0]), ScriptedVecPolicy(thr[1])
    idle = IdleVecPolicy()
    learned = LearnedVecPolicy(checkpoint)
    return learned, {
        "base": (s0, s1, ecfg.n_eval, False),
        "slot0": (learned, s1, ecfg.n_eval, False),
        "slot1": (s0, learned, ecfg.n_eval, True),
        "idle0": (learned, idle, ecfg.n_idle, False),
        "idle1": (idle, learned, ecfg.n_idle, True),
        "idle0_base": (s0, idle, ecfg.n_idle, False),
        "idle1_base": (idle, s1, ecfg.n_idle, True),
    }


def build_report(meta, ecfg, scores, stats):
    """scores: name -> slot0-view array (full N). Applies inversion and the
    pinned criterion (config.py docstring)."""
    z = ecfg.z
    view = dict(scores)
    for name in ("slot1", "idle1", "idle1_base"):
        view[name] = 1.0 - view[name]
    base1 = 1.0 - scores["base"]

    out = {"param_hash": meta["param_hash"],
           "train_config": meta["config"],
           "eval_config": json.loads(ecfg.to_json()),
           "matchups": {k: {**mean_ci(view[k], z), **stats[k]} for k in view}}
    d0 = delta_ci(view["slot0"], scores["base"], z)
    d1 = delta_ci(view["slot1"], base1, z)
    out["delta_slot0"] = d0
    out["delta_slot1"] = d1
    # vs-Idle: absolute bar for slot 0 (feasibility established by the
    # scripted reference); relative-to-scripted deltas for both slots.
    di0 = delta_ci(view["idle0"], view["idle0_base"], z)
    di1 = delta_ci(view["idle1"], view["idle1_base"], z)
    out["delta_idle0"] = di0
    out["delta_idle1"] = di1
    idle_ok = (out["matchups"]["idle0"]["lb"] >= ecfg.idle_lb_min
               and di0["lb"] > -ecfg.regress_tol
               and di1["lb"] > -ecfg.regress_tol)
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
                        "strict_improve": strict_improve, "verdict": verdict}
    return out


def evaluate(checkpoint, scenario, ecfg, lib_path=None, verbose=True):
    """One-shot in-process evaluation (used by tests and --quick)."""
    learned, table = matchup_table(scenario, checkpoint, ecfg)
    scores, stats = {}, {}
    for name, (p0, p1, n, _inv) in table.items():
        if verbose:
            print(f"matchup {name}: N={n}", flush=True)
        seeds = np.arange(ecfg.eval_seed_base, ecfg.eval_seed_base + n)
        scores[name], stats[name], _ = play_matchup(
            scenario, seeds, p0, p1, lib_path, ecfg.eval_envs)
    return build_report(learned.meta, ecfg, scores, stats)


def run_slice(checkpoint, scenario, ecfg, outdir, spec, lib_path=None):
    """spec 'name:start:count' — plays that seed slice, saves scores."""
    name, start, count = spec.split(":")
    start, count = int(start), int(count)
    learned, table = matchup_table(scenario, checkpoint, ecfg)
    p0, p1, n, _inv = table[name]
    count = min(count, n - start)
    assert count > 0, f"slice past end of {name} (n={n})"
    seeds = np.arange(ecfg.eval_seed_base + start, ecfg.eval_seed_base + start + count)
    scores, stats, _ = play_matchup(scenario, seeds, p0, p1, lib_path,
                                    ecfg.eval_envs)
    os.makedirs(outdir, exist_ok=True)
    np.save(os.path.join(outdir, f"{name}.{start:08d}.{count}.npy"), scores)
    with open(os.path.join(outdir, "slices.jsonl"), "a") as f:
        f.write(json.dumps({"matchup": name, "start": start, **stats}) + "\n")
    return stats


def aggregate(checkpoint, scenario, ecfg, outdir):
    """Assembles slices into full per-matchup arrays (exact coverage
    required) and writes eval.json with the verdict."""
    learned, table = matchup_table(scenario, checkpoint, ecfg)
    slice_stats = {}
    with open(os.path.join(outdir, "slices.jsonl")) as f:
        for line in f:
            row = json.loads(line)
            slice_stats[(row["matchup"], row["start"])] = row
    scores, stats = {}, {}
    for name, (_p0, _p1, n, _inv) in table.items():
        parts = sorted(glob.glob(os.path.join(outdir, f"{name}.*.npy")))
        arrs, covered = [], 0
        agg = {"n": 0, "draws": 0, "deaths": 0, "illegal_actions": 0}
        for path in parts:
            _, start_s, count_s = os.path.basename(path)[:-4].split(".")
            start = int(start_s)
            assert start == covered, f"{name}: slice gap/overlap at {start}"
            a = np.load(path)
            assert len(a) == int(count_s)
            covered += len(a)
            arrs.append(a)
            row = slice_stats[(name, start)]
            for k in agg:
                agg[k] += row[k]
        assert covered == n, f"{name}: covered {covered} of {n} seeds"
        scores[name] = np.concatenate(arrs)
        stats[name] = agg
    report = build_report(learned.meta, ecfg, scores, stats)
    with open(os.path.join(outdir, "eval.json"), "w") as f:
        f.write(json.dumps(report, indent=2) + "\n")
    return report


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--checkpoint", required=True)
    ap.add_argument("--scenario", default="scenarios/m2_duel.yaml")
    ap.add_argument("--outdir", default=None)
    ap.add_argument("--lib", default=None)
    ap.add_argument("--slice", default=None, help="matchup:start:count")
    ap.add_argument("--aggregate", action="store_true")
    ap.add_argument("--quick", action="store_true",
                    help="one-shot at N/20 (NOT the pinned criterion)")
    args = ap.parse_args()
    ecfg = EvalConfig()
    if args.quick:
        ecfg = dataclasses.replace(ecfg, n_eval=ecfg.n_eval // 20,
                                   n_idle=ecfg.n_idle // 20)
        print(json.dumps(evaluate(args.checkpoint, args.scenario, ecfg,
                                  args.lib), indent=2))
    elif args.slice:
        stats = run_slice(args.checkpoint, args.scenario, ecfg, args.outdir,
                          args.slice, args.lib)
        print(json.dumps({"slice": args.slice, **stats}))
    elif args.aggregate:
        report = aggregate(args.checkpoint, args.scenario, ecfg, args.outdir)
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps(evaluate(args.checkpoint, args.scenario, ecfg,
                                  args.lib), indent=2))


if __name__ == "__main__":
    main()
