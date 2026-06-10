# This file is part of tbc-arena-lab.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version. It is distributed WITHOUT ANY WARRANTY; see the GNU
# General Public License (LICENSE) for details.

"""Pure-ctypes wrapper over the tbc-arena-lab C ABI (libarena_env).

Deterministic two-player arena environment per docs/observation_action_spec.md:
one env per match; both slots observe pre-tick snapshots and submit one
action per decision tick. Slots are ascending entity_id.

    env = ArenaEnv("scenarios/m2_duel.yaml", seed=42)
    while not env.done:
        a0 = my_policy(env.observe(0), env.action_mask(0))
        a1 = other_policy(env.observe(1), env.action_mask(1))
        env.step(a0, a1)
    print(env.result())

The shared library is located via the TBC_ARENA_LIB environment variable or
the lib_path argument.
"""

import ctypes
import enum
import os

__all__ = ["Action", "ArenaEnv", "OBS_FIELDS", "load_library"]

OBS_SIZE = 17

# Canonical observation field order (docs/observation_action_spec.md).
OBS_FIELDS = (
    "t_ms",
    "match_remaining_ms",
    "self_hp",
    "self_max_hp",
    "self_rage_deci",
    "self_gcd_remaining_ms",
    "self_ms_cd_remaining_ms",
    "self_swing_remaining_ms",
    "self_hs_queued",
    "self_knows_ms",
    "self_knows_hs",
    "self_weapon_speed_ms",
    "enemy_hp_pm",
    "enemy_rage_deci",
    "enemy_in_self_front",
    "self_in_enemy_front",
    "distance_cm",
)


class Action(enum.IntEnum):
    NONE = 0
    CAST_MORTAL_STRIKE = 1
    QUEUE_HEROIC_STRIKE = 2
    UNQUEUE_HEROIC_STRIKE = 3


_API_VERSION = 1
_lib_cache = {}


def load_library(lib_path=None):
    """Loads and signature-checks libarena_env; results are cached per path."""
    path = lib_path or os.environ.get("TBC_ARENA_LIB")
    if not path:
        raise RuntimeError(
            "set TBC_ARENA_LIB to the built libarena_env shared library "
            "(or pass lib_path)")
    path = os.path.abspath(path)
    if path in _lib_cache:
        return _lib_cache[path]
    lib = ctypes.CDLL(path)

    lib.arena_api_version.restype = ctypes.c_int32
    lib.arena_ruleset_hash.restype = ctypes.c_uint64
    lib.arena_sim_commit.restype = ctypes.c_char_p
    lib.arena_last_error.restype = ctypes.c_char_p
    lib.arena_env_create.restype = ctypes.c_void_p
    lib.arena_env_create.argtypes = [ctypes.c_char_p, ctypes.c_uint64, ctypes.c_char_p]
    lib.arena_env_destroy.argtypes = [ctypes.c_void_p]
    lib.arena_env_status.restype = ctypes.c_int32
    lib.arena_env_status.argtypes = [ctypes.c_void_p]
    lib.arena_env_time.restype = ctypes.c_int64
    lib.arena_env_time.argtypes = [ctypes.c_void_p]
    lib.arena_env_observe.restype = ctypes.c_int32
    lib.arena_env_observe.argtypes = [
        ctypes.c_void_p, ctypes.c_int32, ctypes.POINTER(ctypes.c_int64)]
    lib.arena_env_action_mask.restype = ctypes.c_uint32
    lib.arena_env_action_mask.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.arena_env_step.restype = ctypes.c_int32
    lib.arena_env_step.argtypes = [ctypes.c_void_p, ctypes.c_int32, ctypes.c_int32]
    lib.arena_env_end_reason.restype = ctypes.c_int32
    lib.arena_env_end_reason.argtypes = [ctypes.c_void_p]
    lib.arena_env_winner_slot.restype = ctypes.c_int32
    lib.arena_env_winner_slot.argtypes = [ctypes.c_void_p]
    lib.arena_env_entity_id.restype = ctypes.c_int32
    lib.arena_env_entity_id.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.arena_env_hp.restype = ctypes.c_int32
    lib.arena_env_hp.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.arena_env_counter.restype = ctypes.c_int64
    lib.arena_env_counter.argtypes = [ctypes.c_void_p, ctypes.c_int32]
    lib.arena_run_scripted.restype = ctypes.c_int32
    lib.arena_run_scripted.argtypes = [
        ctypes.c_char_p, ctypes.c_uint64, ctypes.POINTER(ctypes.c_int64)]

    version = lib.arena_api_version()
    if version != _API_VERSION:
        raise RuntimeError(
            f"libarena_env API version {version} != wrapper version {_API_VERSION}")
    _lib_cache[path] = lib
    return lib


class ArenaEnv:
    """One deterministic match. Create a new instance per episode."""

    def __init__(self, scenario_path, seed, lib_path=None, trace_path=None):
        self._lib = load_library(lib_path)
        self._handle = self._lib.arena_env_create(
            os.fspath(scenario_path).encode(),
            seed,
            os.fspath(trace_path).encode() if trace_path else None)
        if not self._handle:
            raise RuntimeError(self._lib.arena_last_error().decode())

    # -- lifecycle ---------------------------------------------------------

    def close(self):
        if self._handle:
            self._lib.arena_env_destroy(self._handle)
            self._handle = None

    def __del__(self):
        self.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    # -- stepping ----------------------------------------------------------

    @property
    def done(self):
        return self._lib.arena_env_status(self._handle) == 1

    @property
    def time_ms(self):
        return self._lib.arena_env_time(self._handle)

    def observe(self, slot):
        """Observation for slot 0/1 as a dict of OBS_FIELDS -> int."""
        buf = (ctypes.c_int64 * OBS_SIZE)()
        if self._lib.arena_env_observe(self._handle, slot, buf) != 0:
            raise ValueError(f"bad slot {slot}")
        return dict(zip(OBS_FIELDS, buf))

    def observe_raw(self, slot):
        """Observation as a flat tuple in canonical field order."""
        buf = (ctypes.c_int64 * OBS_SIZE)()
        if self._lib.arena_env_observe(self._handle, slot, buf) != 0:
            raise ValueError(f"bad slot {slot}")
        return tuple(buf)

    def action_mask(self, slot):
        """Bitmask: bit Action.X set iff X is legal for slot now."""
        return self._lib.arena_env_action_mask(self._handle, slot)

    def legal_actions(self, slot):
        mask = self.action_mask(slot)
        return [a for a in Action if mask & (1 << a)]

    def step(self, action_slot0, action_slot1):
        """Applies both actions at the current tick. Returns done (bool)."""
        status = self._lib.arena_env_step(
            self._handle, int(action_slot0), int(action_slot1))
        if status < 0:
            raise RuntimeError("step() called on a finished or invalid env")
        return status == 1

    # -- results -----------------------------------------------------------

    def result(self):
        return {
            "end_ms": self._lib.arena_env_time(self._handle),
            "end_reason": "death" if self._lib.arena_env_end_reason(self._handle) else "duration",
            "winner_slot": self._lib.arena_env_winner_slot(self._handle),
            "entity_ids": (self._lib.arena_env_entity_id(self._handle, 0),
                           self._lib.arena_env_entity_id(self._handle, 1)),
            "hp": (self._lib.arena_env_hp(self._handle, 0),
                   self._lib.arena_env_hp(self._handle, 1)),
            "swings": self._lib.arena_env_counter(self._handle, 0),
            "abilities": self._lib.arena_env_counter(self._handle, 1),
            "decisions": self._lib.arena_env_counter(self._handle, 2),
            "illegal_actions": self._lib.arena_env_counter(self._handle, 3),
            "checkpoints": self._lib.arena_env_counter(self._handle, 4),
        }


def run_scripted(scenario_path, seed, lib_path=None):
    """Native-path reference run with the scenario's pinned policies."""
    lib = load_library(lib_path)
    out = (ctypes.c_int64 * 5)()
    if lib.arena_run_scripted(os.fspath(scenario_path).encode(), seed, out) != 0:
        raise RuntimeError(lib.arena_last_error().decode())
    return {
        "end_ms": out[0],
        "end_reason": "death" if out[1] else "duration",
        "swings": out[2],
        "abilities": out[3],
        "decisions": out[4],
    }
