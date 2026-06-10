# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""M5 self-play PPO on the arena env (numpy-only).

One shared policy plays both slots (loadout differences are observable via
knows_ms/knows_hs and the masks). Reproducible by construction: numpy RNG
seeded from config, env seeds are a deterministic episode counter, and the
whole run is single-threaded (BLAS threads pinned below, before numpy
import, so reductions are deterministic too).

Usage:
    TBC_ARENA_LIB=build/libarena_env.so python3 python/train/selfplay.py \
        --out runs/m5 [--total-samples N] [--seed S] [--scenario PATH]
"""

import os

os.environ.setdefault("OMP_NUM_THREADS", "1")
os.environ.setdefault("OPENBLAS_NUM_THREADS", "1")
os.environ.setdefault("MKL_NUM_THREADS", "1")

import argparse  # noqa: E402
import dataclasses  # noqa: E402
import hashlib  # noqa: E402
import json  # noqa: E402
import sys  # noqa: E402
import time  # noqa: E402

import numpy as np  # noqa: E402

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from config import OBS_DIM, N_ACTIONS, TrainConfig  # noqa: E402
from envs import VecSelfPlay, featurize  # noqa: E402
from net import MLP, Adam, gae, ppo_loss_and_grads  # noqa: E402


def param_hash(net):
    h = hashlib.sha256()
    for k in net.PARAM_ORDER:
        h.update(np.ascontiguousarray(net.params[k]).tobytes())
    return h.hexdigest()


def collect(vec, net, rng, cfg, obs, mask):
    """One horizon of self-play. Returns (batch, last_obs, last_mask).

    Shaping rewards need phi of the next observation, so the shaped part of
    each transition is added right after the following observe_all().
    """
    T, E = cfg.horizon, cfg.num_envs
    F, A = OBS_DIM, N_ACTIONS
    b_obs = np.empty((T, E, 2, F), dtype=np.float32)
    b_mask = np.empty((T, E, 2, A), dtype=np.float32)
    b_act = np.empty((T, E, 2), dtype=np.int64)
    b_logp = np.empty((T, E, 2), dtype=np.float32)
    b_val = np.empty((T, E, 2), dtype=np.float32)
    b_rew = np.empty((T, E, 2), dtype=np.float32)
    b_done = np.empty((T, E), dtype=np.float32)

    b_inc = np.empty((T, E, 2), dtype=bool)  # net-controlled (trainable) slots

    for t in range(T):
        feats = featurize(obs)
        a, logp, v = net.act(feats.reshape(E * 2, F), mask.reshape(E * 2, A), rng)
        fixed = vec.fixed_actions(obs, mask)  # (E,2), -1 where net acts
        b_inc[t] = fixed < 0
        b_obs[t], b_mask[t] = feats, mask
        b_act[t] = np.where(fixed >= 0, fixed, a.reshape(E, 2))
        b_logp[t] = logp.reshape(E, 2)
        b_val[t] = v.reshape(E, 2)

        rew_term, dones, phi_pre = vec.step_all(b_act[t])
        obs, mask = vec.observe_all()  # refreshes vec.phi (next state)
        live = (1.0 - dones)[:, None]
        b_rew[t] = rew_term + cfg.shape_coef * (cfg.gamma * vec.phi - phi_pre) * live
        b_done[t] = dones

    _, bootstrap, _ = net.forward(featurize(obs).reshape(E * 2, F))
    adv, ret = gae(b_rew, b_val, b_done[..., None] * np.ones((1, 1, 2), np.float32),
                   bootstrap.reshape(E, 2).astype(np.float32),
                   cfg.gamma, cfg.gae_lambda)

    n = T * E * 2
    keep = b_inc.reshape(n)  # drop fixed-opponent slots from training
    batch = {
        "obs": b_obs.reshape(n, F)[keep],
        "mask": b_mask.reshape(n, A)[keep],
        "action": b_act.reshape(n)[keep],
        "logp_old": b_logp.reshape(n)[keep],
        "adv": adv.reshape(n).astype(np.float32)[keep],
        "ret": ret.reshape(n).astype(np.float32)[keep],
    }
    return batch, obs, mask


def _save_checkpoint(path, net, opt, rng, vec, cfg, iters_done):
    np.savez(path,
             _meta=json.dumps({"config": dataclasses.asdict(cfg),
                               "param_hash": param_hash(net),
                               "obs_dim": OBS_DIM, "n_actions": N_ACTIONS,
                               "iters_done": iters_done,
                               "adam_t": opt.t,
                               "episodes_started": vec.episodes_started,
                               "rng_state": rng.bit_generator.state}),
             **net.params,
             **{f"adam_m_{k}": v for k, v in opt.m.items()},
             **{f"adam_v_{k}": v for k, v in opt.v.items()})


def train(cfg, out_dir=None, lib_path=None, log_file=None, quiet=False,
          resume=False, max_wall_s=None):
    """Resume restarts envs fresh (in-flight episodes are discarded; the
    episode seed counter continues, so no seed is reused) and restores
    params/Adam/numpy-rng exactly. A resumed run is reproducible given the
    same segmentation; segment boundaries are visible in log.jsonl."""
    rng = np.random.default_rng(cfg.train_seed)
    net = MLP(OBS_DIM, N_ACTIONS, cfg.hidden, rng)
    opt = Adam(net.params, cfg.lr)
    iters_done, episodes_offset = 0, 0
    ckpt_path = os.path.join(out_dir, "checkpoint.npz") if out_dir else None
    if resume and ckpt_path and os.path.exists(ckpt_path):
        data = np.load(ckpt_path, allow_pickle=False)
        meta = json.loads(str(data["_meta"]))
        net.load_state({k: data[k] for k in net.PARAM_ORDER})
        for k in net.PARAM_ORDER:
            opt.m[k] = np.array(data[f"adam_m_{k}"])
            opt.v[k] = np.array(data[f"adam_v_{k}"])
        opt.t = meta["adam_t"]
        rng.bit_generator.state = meta["rng_state"]
        iters_done = meta["iters_done"]
        episodes_offset = meta["episodes_started"]
    vec = VecSelfPlay(cfg.scenario, cfg.num_envs, cfg.env_seed_base,
                      cfg.gamma, cfg.shape_coef, cfg.win_reward, lib_path,
                      opponent_mix=cfg.opponent_mix,
                      episodes_offset=episodes_offset)
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        with open(os.path.join(out_dir, "config.json"), "w") as f:
            f.write(cfg.to_json())
        log_file = log_file or os.path.join(out_dir, "log.jsonl")

    per_iter = cfg.horizon * cfg.num_envs * 2
    iters = max(1, (cfg.total_samples + per_iter - 1) // per_iter)
    obs, mask = vec.observe_all()
    t0 = time.time()
    history = []

    for it in range(iters_done, iters):
        batch, obs, mask = collect(vec, net, rng, cfg, obs, mask)
        n = batch["obs"].shape[0]
        stats = {}
        for _ in range(cfg.epochs):
            perm = rng.permutation(n)
            for lo in range(0, n, cfg.minibatch):
                idx = perm[lo:lo + cfg.minibatch]
                mb = {k: v[idx] for k, v in batch.items()}
                a = mb["adv"]
                mb["adv"] = (a - a.mean()) / (a.std() + 1e-8)
                stats, grads = ppo_loss_and_grads(
                    net, mb, cfg.clip, cfg.vf_coef, cfg.ent_coef)
                opt.step(net.params, grads)
        s0, s1 = vec.pop_completed()
        row = {
            "iter": it + 1, "samples": (it + 1) * per_iter,
            "episodes": len(s0),
            "score_slot0": round(float(np.mean(s0)), 4) if s0 else None,
            "score_slot1": round(float(np.mean(s1)), 4) if s1 else None,
            "entropy": round(stats["entropy"], 4),
            "approx_kl": round(stats["approx_kl"], 5),
            "clip_frac": round(stats["clip_frac"], 4),
            "loss_v": round(stats["loss_v"], 4),
            "illegal_total": vec.illegal_total,
            "wall_s": round(time.time() - t0, 1),
        }
        history.append(row)
        if log_file:
            with open(log_file, "a") as f:
                f.write(json.dumps(row) + "\n")
        if not quiet:
            print(json.dumps(row), flush=True)
        iters_done = it + 1
        if max_wall_s and time.time() - t0 > max_wall_s:
            break

    if ckpt_path:
        _save_checkpoint(ckpt_path, net, opt, rng, vec, cfg, iters_done)
    vec.close()
    return net, history


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scenario", default=None)
    ap.add_argument("--out", required=True)
    ap.add_argument("--total-samples", type=int, default=None)
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--lib", default=None)
    ap.add_argument("--set", action="append", default=[], metavar="KEY=VALUE",
                    help="override any TrainConfig field (recorded in config.json)")
    ap.add_argument("--resume", action="store_true",
                    help="continue from --out checkpoint.npz")
    ap.add_argument("--max-wall-s", type=float, default=None,
                    help="checkpoint and exit after this many seconds")
    args = ap.parse_args()
    overrides = {}
    if args.scenario:
        overrides["scenario"] = args.scenario
    if args.total_samples:
        overrides["total_samples"] = args.total_samples
    if args.seed is not None:
        overrides["train_seed"] = args.seed
    for kv in args.set:
        k, v = kv.split("=", 1)
        field = {f.name: f for f in dataclasses.fields(TrainConfig)}[k]
        overrides[k] = field.type(v) if callable(field.type) else type(getattr(TrainConfig(), k))(v)
    cfg = dataclasses.replace(TrainConfig(), **overrides)
    net, history = train(cfg, out_dir=args.out, lib_path=args.lib,
                         resume=args.resume, max_wall_s=args.max_wall_s)
    print(json.dumps({"done": True, "param_hash": param_hash(net),
                      "final": history[-1] if history else None}))


if __name__ == "__main__":
    main()
