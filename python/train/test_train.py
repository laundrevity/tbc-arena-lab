# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""Verification of the M5 training harness (run by ctest as python_train).

Plain asserts, no pytest. Requires TBC_ARENA_LIB and TBC_ARENA_ROOT for the
env-touching tests (gradient/GAE/sampling tests are env-free).

Covers: finite-difference check of the hand-written PPO gradients; masked
sampling legality; a hand-computed GAE case; eval-path parity with the
native scripted runner; bit-exact same-seed training reproducibility; and
zero illegal actions end-to-end.
"""

import dataclasses
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from config import TrainConfig  # noqa: E402
from envs import ScriptedVecPolicy, parse_scripted_thresholds  # noqa: E402
from net import MLP, gae, ppo_loss_and_grads  # noqa: E402
import evaluate  # noqa: E402
import selfplay  # noqa: E402

ROOT = os.environ.get("TBC_ARENA_ROOT", os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", ".."))
M2 = os.path.join(ROOT, "scenarios", "m2_duel.yaml")


def _random_batch(rng, n, f, a, dtype):
    obs = rng.standard_normal((n, f)).astype(dtype)
    mask = (rng.random((n, a)) < 0.7).astype(dtype)
    mask[:, 0] = 1.0  # action 0 always legal (None), like the real env
    action = np.array([rng.choice(np.flatnonzero(m)) for m in mask])
    return {
        "obs": obs, "mask": mask, "action": action,
        "logp_old": (rng.standard_normal(n) * 0.3 - 1.0).astype(dtype),
        "adv": rng.standard_normal(n).astype(dtype),
        "ret": rng.standard_normal(n).astype(dtype),
    }


def test_gradient_check():
    """Analytic PPO gradients match central finite differences (float64)."""
    rng = np.random.default_rng(7)
    net = MLP(17, 4, 8, rng, dtype=np.float64)
    batch = _random_batch(rng, 16, 17, 4, np.float64)
    clip, vf, ce = 0.2, 0.5, 0.01

    def loss_only():
        s, _ = ppo_loss_and_grads(net, batch, clip, vf, ce)
        return s["loss"]

    _, grads = ppo_loss_and_grads(net, batch, clip, vf, ce)
    h = 1e-6
    checked = 0
    worst = 0.0
    for k in net.PARAM_ORDER:
        flat = net.params[k].reshape(-1)
        for idx in rng.choice(flat.size, size=min(6, flat.size), replace=False):
            orig = flat[idx]
            flat[idx] = orig + h
            lp = loss_only()
            flat[idx] = orig - h
            lm = loss_only()
            flat[idx] = orig
            fd = (lp - lm) / (2 * h)
            an = grads[k].reshape(-1)[idx]
            rel = abs(fd - an) / max(abs(fd), abs(an), 1e-8)
            worst = max(worst, rel)
            assert rel < 1e-4, f"grad mismatch {k}[{idx}]: fd={fd} an={an} rel={rel}"
            checked += 1
    assert checked >= 40
    print(f"  gradient check: {checked} coords, worst rel err {worst:.2e}")


def test_masked_sampling_never_illegal():
    rng = np.random.default_rng(3)
    net = MLP(17, 4, 8, rng)
    obs = rng.standard_normal((512, 17)).astype(np.float32)
    mask = np.zeros((512, 4), dtype=np.float32)
    mask[:, 0] = 1.0
    mask[np.arange(512), rng.integers(0, 4, 512)] = 1.0
    for _ in range(20):
        a, logp, _ = net.act(obs, mask, rng)
        assert (mask[np.arange(512), a] > 0).all(), "sampled an illegal action"
        assert np.isfinite(logp).all()


def test_gae_hand_case():
    """gamma=0.5, lam=0.5, hand-computed (see comments)."""
    rewards = np.array([[1.0], [0.0], [2.0]], dtype=np.float32)
    values = np.array([[0.5], [1.0], [0.25]], dtype=np.float32)
    dones = np.array([[0.0], [1.0], [0.0]], dtype=np.float32)
    bootstrap = np.array([2.0], dtype=np.float32)
    adv, ret = gae(rewards, values, dones, bootstrap, 0.5, 0.5)
    # t=2: delta = 2 + 0.5*2 - 0.25 = 2.75 -> adv 2.75
    # t=1: done; delta = 0 - 1 = -1 -> adv -1.0
    # t=0: delta = 1 + 0.5*1 - 0.5 = 1.0; adv = 1 + 0.25*(-1) = 0.75
    assert np.allclose(adv.reshape(-1), [0.75, -1.0, 2.75]), adv.reshape(-1)
    assert np.allclose(ret.reshape(-1), [1.25, 0.0, 3.0]), ret.reshape(-1)


def test_eval_path_matches_native_scripted():
    """The vectorized eval machinery with scripted policies must reproduce
    the native run_scripted path exactly (validates threshold parsing, slot
    order, mask plumbing, and the chunked stepping loop)."""
    from tbc_arena import run_scripted
    thr = parse_scripted_thresholds(M2)
    assert thr == [700, 500], thr
    seeds = np.array([4242, 99, 7])
    _, _, results = evaluate.play_matchup(
        M2, seeds, ScriptedVecPolicy(thr[0]), ScriptedVecPolicy(thr[1]),
        chunk=2, collect_results=True)
    assert len(results) == len(seeds)
    for seed, res in results:
        native = run_scripted(M2, seed)
        assert res["end_ms"] == native["end_ms"], (seed, res, native)
        assert res["end_reason"] == native["end_reason"]
        assert res["swings"] == native["swings"]
        assert res["abilities"] == native["abilities"]
        assert res["decisions"] == native["decisions"]
        assert res["illegal_actions"] == 0


def _tiny_cfg():
    return dataclasses.replace(
        TrainConfig(), scenario=M2, num_envs=4, horizon=32,
        total_samples=4 * 32 * 2 * 2, minibatch=128, hidden=16,
        train_seed=123, env_seed_base=5_000_000)


def test_training_reproducible_and_legal():
    """Same config twice -> bit-identical parameters; masked sampling never
    triggers the engine's illegal-action counter."""
    cfg = _tiny_cfg()
    net1, hist1 = selfplay.train(cfg, quiet=True)
    net2, hist2 = selfplay.train(cfg, quiet=True)
    h1, h2 = selfplay.param_hash(net1), selfplay.param_hash(net2)
    assert h1 == h2, f"non-reproducible training: {h1} != {h2}"
    assert hist1 == hist2
    assert hist1[-1]["illegal_total"] == 0, hist1[-1]
    # different seed must diverge (sanity that the hash isn't vacuous)
    net3, _ = selfplay.train(dataclasses.replace(cfg, train_seed=124), quiet=True)
    assert selfplay.param_hash(net3) != h1


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"PASS {t.__name__}")
    print(f"{len(tests)} training harness tests passed")


if __name__ == "__main__":
    main()
