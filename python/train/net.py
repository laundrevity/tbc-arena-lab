# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""Tiny numpy MLP (shared trunk, policy + value heads), manual gradients.

No deep-learning dependency by design: the only requirement beyond the
ctypes bindings is numpy. Correctness of the hand-written backprop is
enforced by a finite-difference gradient check in test_train.py.
"""

import numpy as np

BIG = 1e9  # subtracted from masked-out logits


class MLP:
    """obs -> tanh(H) -> tanh(H) -> {4 logits, 1 value}."""

    PARAM_ORDER = ("W1", "b1", "W2", "b2", "Wp", "bp", "Wv", "bv")

    def __init__(self, obs_dim, n_actions, hidden, rng, dtype=np.float32):
        def ortho(shape, gain):
            a = rng.standard_normal(shape)
            u, _, vt = np.linalg.svd(a, full_matrices=False)
            q = u if u.shape == shape else vt
            return np.asarray(gain * q, dtype=dtype)

        self.dtype = dtype
        self.params = {
            "W1": ortho((obs_dim, hidden), np.sqrt(2.0)),
            "b1": np.zeros(hidden, dtype),
            "W2": ortho((hidden, hidden), np.sqrt(2.0)),
            "b2": np.zeros(hidden, dtype),
            "Wp": ortho((hidden, n_actions), 0.01),
            "bp": np.zeros(n_actions, dtype),
            "Wv": ortho((hidden, 1), 1.0),
            "bv": np.zeros(1, dtype),
        }

    # -- forward -------------------------------------------------------------

    def forward(self, x):
        """x (B,obs_dim) -> logits (B,A), value (B,), cache for backward."""
        p = self.params
        h1 = np.tanh(x @ p["W1"] + p["b1"])
        h2 = np.tanh(h1 @ p["W2"] + p["b2"])
        logits = h2 @ p["Wp"] + p["bp"]
        value = (h2 @ p["Wv"] + p["bv"])[:, 0]
        return logits, value, (x, h1, h2)

    @staticmethod
    def masked_log_softmax(logits, mask):
        """mask (B,A) in {0,1}. Returns log-probs with illegal ~ -BIG."""
        ml = logits - (1.0 - mask) * BIG
        ml = ml - ml.max(axis=1, keepdims=True)
        lse = np.log(np.exp(ml).sum(axis=1, keepdims=True))
        return ml - lse

    def act(self, x, mask, rng):
        """Sample actions from the masked policy. Returns a, logp, value."""
        logits, value, _ = self.forward(x)
        logp = self.masked_log_softmax(logits, mask)
        p = np.exp(logp)
        cum = p.cumsum(axis=1)
        cum /= cum[:, -1:]  # exact 1.0 tail so u<1 can never land past a legal action
        u = rng.random((x.shape[0], 1))
        a = (cum < u).sum(axis=1)
        return a, logp[np.arange(len(a)), a], value

    def greedy(self, x, mask):
        """Deterministic argmax of the masked policy (evaluation mode)."""
        logits, _, _ = self.forward(x)
        return np.argmax(logits - (1.0 - mask) * BIG, axis=1)

    # -- backward ------------------------------------------------------------

    def backward(self, cache, g_logits, g_value):
        """Grads of scalar loss given dL/dlogits (B,A) and dL/dvalue (B,)."""
        p = self.params
        x, h1, h2 = cache
        gv = g_value[:, None]
        dh2 = g_logits @ p["Wp"].T + gv @ p["Wv"].T
        da2 = dh2 * (1.0 - h2 * h2)
        dh1 = da2 @ p["W2"].T
        da1 = dh1 * (1.0 - h1 * h1)
        return {
            "W1": x.T @ da1, "b1": da1.sum(axis=0),
            "W2": h1.T @ da2, "b2": da2.sum(axis=0),
            "Wp": h2.T @ g_logits, "bp": g_logits.sum(axis=0),
            "Wv": h2.T @ gv, "bv": gv.sum(axis=0),
        }

    # -- (de)serialization -----------------------------------------------------

    def state(self):
        return {k: v.copy() for k, v in self.params.items()}

    def load_state(self, state):
        for k in self.PARAM_ORDER:
            self.params[k] = np.asarray(state[k], dtype=self.dtype)


class Adam:
    def __init__(self, params, lr, beta1=0.9, beta2=0.999, eps=1e-8):
        self.lr, self.b1, self.b2, self.eps = lr, beta1, beta2, eps
        self.m = {k: np.zeros_like(v) for k, v in params.items()}
        self.v = {k: np.zeros_like(v) for k, v in params.items()}
        self.t = 0

    def step(self, params, grads):
        self.t += 1
        b1t = 1.0 - self.b1 ** self.t
        b2t = 1.0 - self.b2 ** self.t
        for k in params:
            g = grads[k]
            self.m[k] = self.b1 * self.m[k] + (1.0 - self.b1) * g
            self.v[k] = self.b2 * self.v[k] + (1.0 - self.b2) * (g * g)
            mhat = self.m[k] / b1t
            vhat = self.v[k] / b2t
            params[k] -= np.asarray(self.lr * mhat / (np.sqrt(vhat) + self.eps),
                                    dtype=params[k].dtype)


def ppo_loss_and_grads(net, batch, clip, vf_coef, ent_coef):
    """Clipped-surrogate PPO loss + manual grads.

    batch: obs (B,F), mask (B,A), action (B,), logp_old (B,), adv (B,),
    ret (B,). Returns (stats dict, grads dict). Mean reduction over B.
    """
    obs, mask = batch["obs"], batch["mask"]
    a, logp_old = batch["action"], batch["logp_old"]
    adv, ret = batch["adv"], batch["ret"]
    B = obs.shape[0]
    idx = np.arange(B)

    logits, value, cache = net.forward(obs)
    logp_all = net.masked_log_softmax(logits, mask)
    p = np.exp(logp_all)
    logp_a = logp_all[idx, a]

    ratio = np.exp(logp_a - logp_old)
    unclipped = ratio * adv
    clipped = np.clip(ratio, 1.0 - clip, 1.0 + clip) * adv
    pi_obj = np.minimum(unclipped, clipped)
    # entropy of the masked distribution (p==0 exactly on illegal entries)
    plogp = np.where(mask > 0, p * logp_all, 0.0)
    ent = -plogp.sum(axis=1)
    v_err = value - ret

    loss_pi = -pi_obj.mean()
    loss_v = 0.5 * (v_err * v_err).mean()
    loss_ent = -ent.mean()
    loss = loss_pi + vf_coef * loss_v + ent_coef * loss_ent

    # d(loss)/d(logits): policy term — gradient flows iff min() picks the
    # unclipped branch (<= covers the interior where both are equal).
    take = (unclipped <= clipped).astype(logits.dtype)
    coef = -(take * ratio * adv) / B                      # d loss_pi / d logp_a
    onehot = np.zeros_like(p)
    onehot[idx, a] = 1.0
    g_logits = coef[:, None] * (onehot - p)
    # entropy term: dH/ds_k = -p_k (logp_k + H), so d(-H)/ds_k = p_k (logp_k + H)
    dneg_ent = p * (np.where(mask > 0, logp_all, 0.0) + ent[:, None])
    dneg_ent = np.where(mask > 0, dneg_ent, 0.0)
    g_logits += (ent_coef / B) * dneg_ent
    g_value = (vf_coef / B) * v_err

    grads = net.backward(cache, g_logits, g_value)
    stats = {
        "loss": float(loss), "loss_pi": float(loss_pi), "loss_v": float(loss_v),
        "entropy": float(ent.mean()),
        "approx_kl": float((logp_old - logp_a).mean()),
        "clip_frac": float((np.abs(ratio - 1.0) > clip).mean()),
    }
    return stats, grads


def gae(rewards, values, dones, bootstrap, gamma, lam):
    """Generalized advantage estimation over (T, ...) arrays.

    dones[t] marks transitions that END an episode (no bootstrap across).
    bootstrap is v(s_T) for trajectories still running at the horizon.
    Returns (advantages, returns) shaped like rewards.
    """
    T = rewards.shape[0]
    adv = np.zeros_like(rewards)
    next_v = bootstrap
    next_adv = np.zeros_like(bootstrap)
    for t in range(T - 1, -1, -1):
        live = 1.0 - dones[t]
        delta = rewards[t] + gamma * next_v * live - values[t]
        next_adv = delta + gamma * lam * live * next_adv
        adv[t] = next_adv
        next_v = values[t]
    return adv, adv + values
