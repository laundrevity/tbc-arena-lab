# Python Bindings — tbc-arena-lab (M4)

Pure-ctypes wrapper (`python/tbc_arena/`) over a C ABI shared library
(`libarena_env`, `sim/env/arena_c_api.h`). No pybind11, no python-dev, no
third-party Python dependencies — any CPython ≥3.8 with ctypes works. The C
ABI fronts the M3 `MatchEngine` (`sim/mechanics/match.h`), and `run_match`
itself is a thin wrapper over the same engine, so externally driven matches
and native runs share one implementation; parity is enforced by tests, not
hoped for.

## Build & use

```bash
cmake --build build              # produces build/libarena_env.so
export TBC_ARENA_LIB=build/libarena_env.so

python3 -c '
import sys; sys.path.insert(0, "python")
from tbc_arena import ArenaEnv, Action
env = ArenaEnv("scenarios/m2_duel.yaml", seed=42)
while not env.done:
    a0 = Action.NONE   # your policy here; see env.observe(0)/action_mask(0)
    a1 = Action.NONE
    env.step(a0, a1)
print(env.result())'
```

## API semantics (normative: docs/observation_action_spec.md)

- One `ArenaEnv` per match (deterministic in `(scenario, seed, actions)`);
  there is no `reset()` — construct a new env.
- Slots are ascending entity_id. `observe(slot)` returns a dict over
  `OBS_FIELDS` (ints; canonical order, `observe_raw` gives the flat tuple).
  Observations at a tick are pre-action snapshots for both slots.
- `action_mask(slot)` is the legality bitmask; illegal submissions degrade
  to None engine-side and are counted in `result()["illegal_actions"]`.
- `step(a0, a1)` applies both actions at the current tick (entity order) and
  advances to the next tick or match end; returns `done`.
- `result()` includes `end_reason` ("duration"/"death"), `winner_slot`
  (-1 for duration ends), final hp, and all counters. Reward shaping is a
  training-side concern by design.
- `trace_path=` writes a full JSONL trace replayable by `arena_replay`
  (verified end-to-end in `python/tests/test_env.py`); recorded `decision`
  lines feed `PlaybackPolicy` for imitation workflows.
- `run_scripted(scenario, seed)` runs the native scenario-pinned policies
  and returns counters — the cross-check that a Python driver reproduces the
  native path (tested).

## Throughput (i9-11900K, single core, 100 ms ticks, m2_duel)

| driver | steps/s | simulated-sec per wall-sec |
|---|---|---|
| C++ ScriptedPolicy (native, no Python) | — | ~6.3e5 |
| Python step() only (no observe calls) | ~9.9e5 | ~9.9e4 |
| Python full loop (2x observe + masks + dict) | ~1.4e5 | ~1.4e4 |

A full 60 s duel costs ~4 ms of Python wall time with full observation
processing — fine for early experiments. The known scaling path (future
milestone, not built): a batched C call stepping N envs at once into
preallocated observation buffers (numpy-compatible), which removes the
per-call ctypes overhead that dominates the full loop.

## Tests

`ctest` runs `python_env` (plain asserts, no pytest): observation shape and
client-parity fields, mask gates, Python-scripted-vs-native counter
equality, determinism, illegal-action accounting, and the trace round trip
through `arena_replay`. C++-side `test_engine.cpp` pins the engine/step
contract (bit-exact vs `run_match`, tick pause semantics, tick-free mode).
