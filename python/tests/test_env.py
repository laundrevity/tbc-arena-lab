# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""End-to-end tests for the ctypes bindings (run by ctest as python_env).

Requires TBC_ARENA_LIB (built libarena_env) and TBC_ARENA_ROOT (repo root).
Plain asserts, no pytest dependency.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))

from tbc_arena import Action, ArenaEnv, OBS_FIELDS, run_scripted  # noqa: E402

ROOT = os.environ["TBC_ARENA_ROOT"]
M2 = os.path.join(ROOT, "scenarios", "m2_duel.yaml")


def scripted_action(obs, mask, hs_threshold_deci):
    """Python reimplementation of ScriptedPolicy (sim/mechanics/policy.h)."""
    if mask & (1 << Action.CAST_MORTAL_STRIKE):
        return Action.CAST_MORTAL_STRIKE
    if (mask & (1 << Action.QUEUE_HEROIC_STRIKE)) and hs_threshold_deci > 0 \
            and obs["self_rage_deci"] >= hs_threshold_deci:
        return Action.QUEUE_HEROIC_STRIKE
    return Action.NONE


def drive_match(seed, trace_path=None):
    """Drives m2_duel from Python with scripted-equivalent logic."""
    trajectory = []
    with ArenaEnv(M2, seed, trace_path=trace_path) as env:
        # m2_duel pins attacker (slot 0) hs>=700, defender (slot 1) hs>=500.
        while not env.done:
            o0, o1 = env.observe(0), env.observe(1)
            a0 = scripted_action(o0, env.action_mask(0), 700)
            a1 = scripted_action(o1, env.action_mask(1), 500)
            trajectory.append((env.time_ms, int(a0), int(a1),
                               o0["self_rage_deci"], o1["self_rage_deci"]))
            env.step(a0, a1)
        return trajectory, env.result()


def test_observation_shape():
    with ArenaEnv(M2, 1) as env:
        assert not env.done
        assert env.time_ms == 0
        obs = env.observe(0)
        assert tuple(obs.keys()) == OBS_FIELDS
        assert obs["t_ms"] == 0
        assert obs["self_hp"] == 25000
        assert obs["self_max_hp"] == 25000
        assert obs["enemy_hp_pm"] == 10000
        assert obs["self_knows_ms"] == 1   # m2 attacker knows MS
        assert obs["distance_cm"] == 200
        assert env.observe(1)["self_knows_ms"] == 0
        assert env.observe(1)["self_ms_cd_remaining_ms"] == -1
        raw = env.observe_raw(0)
        assert len(raw) == len(OBS_FIELDS)
        assert raw == tuple(obs.values())


def test_action_mask():
    with ArenaEnv(M2, 1) as env:
        # Rage 0 at t=0: only None (slot 0 knows HS but queueing is legal
        # regardless of rage; MS needs rage).
        mask0 = env.action_mask(0)
        assert mask0 & (1 << Action.NONE)
        assert not (mask0 & (1 << Action.CAST_MORTAL_STRIKE))
        assert mask0 & (1 << Action.QUEUE_HEROIC_STRIKE)
        assert Action.NONE in env.legal_actions(0)


def test_python_driver_matches_native_scripted():
    """Driving the env from Python with ScriptedPolicy-equivalent logic must
    reproduce the native run_match path exactly (same counters)."""
    native = run_scripted(M2, 4242)
    _, driven = drive_match(4242)
    assert driven["end_ms"] == native["end_ms"]
    assert driven["end_reason"] == native["end_reason"]
    assert driven["swings"] == native["swings"]
    assert driven["abilities"] == native["abilities"]
    assert driven["decisions"] == native["decisions"]
    assert driven["illegal_actions"] == 0


def test_determinism():
    t1, r1 = drive_match(99)
    t2, r2 = drive_match(99)
    assert t1 == t2
    assert r1 == r2
    t3, _ = drive_match(100)
    assert t3 != t1  # different seed diverges (sanity)


def test_illegal_action_counted():
    with ArenaEnv(M2, 7) as env:
        env.step(Action.CAST_MORTAL_STRIKE, Action.UNQUEUE_HEROIC_STRIKE)  # both illegal
        while not env.done:
            env.step(Action.NONE, Action.NONE)
        res = env.result()
        assert res["illegal_actions"] == 2
        assert res["decisions"] == 0


def test_trace_round_trip(tmp="/tmp/py_env_trace.jsonl"):
    """Env-driven matches write replayable traces."""
    drive_match(4242, trace_path=tmp)
    with open(tmp) as f:
        first = f.readline()
    assert '"type":"header"' in first
    # arena_replay verifies it end-to-end if available next to the lib.
    replay = os.path.join(os.path.dirname(os.environ["TBC_ARENA_LIB"]), "arena_replay")
    if os.path.exists(replay):
        rc = os.spawnl(os.P_WAIT, replay, "arena_replay", tmp, "--base-dir", ROOT)
        assert rc == 0, "arena_replay failed on env-written trace"


def main():
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    for t in tests:
        t()
        print(f"PASS {t.__name__}")
    print(f"{len(tests)} python binding tests passed")


if __name__ == "__main__":
    main()
