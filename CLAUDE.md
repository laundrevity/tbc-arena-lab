# CLAUDE.md — tbc-arena-lab

## What this project is

A scratch-built, deterministic, high-throughput simulator for WoW: The Burning Crusade arena combat, intended as an RL research environment. Fidelity is checked against a reference emulator (cmangos-tbc, bootstrap oracle) via a differential harness; the pinned ruleset is Anniversary TBC 2.5.x (ledger D-003).

**Milestones M0–M4 are complete** (each dated 2026-06-10): deterministic kernel, source-level oracle audit + `arena_diff` harness, mutual auto-attack with parry-haste, first abilities + GCD (Mortal Strike, Heroic Strike, yellow-hit pipeline), observation/action interface with decision ticks and playback, and pure-ctypes Python bindings over a C ABI. **The current milestone is M5, scope TBD with the owner.**

The long-term plan lives in `docs/project_plan_v3.md` (if absent, ask before inventing scope); per-session status in `docs/m0_status.md`. Nothing beyond the current milestone should be built speculatively.

## Current scope (post-M4)

IN (built; preserve behavior — M0–M2 golden event streams are byte-identical across later milestones and must stay so absent an explained formula change): integer-time event queue with documented total order; counter-based RNG; fixed-point authoritative state; canonical serialization + state hashing; single-roll PvP attack table; weapon damage/armor/crit/block pipeline; swing timers; deci-rage; parry-haste; GCD; Mortal Strike; Heroic Strike; yellow hit resolution; client-parity integer observations; action masks; fixed decision ticks (simultaneous-move snapshots); Policy interface (idle/scripted/playback) with decision tracing + bit-exact playback; resumable `MatchEngine`; C ABI (`libarena_env`); `python/tbc_arena` ctypes package; deterministic replay; JSONL traces; `arena_run`/`arena_replay`/`arena_dist`/`arena_diff`/`arena_bench`; golden traces.

OUT (do not build, even as stubs beyond a header comment, until a milestone admits it — stop and flag instead): auras/buffs/debuffs, stances, dual-wield, movement (units are stationary), stealth, pets, networking, RL training code (the env interface exists; learning algorithms live outside `sim/`), batched multi-env stepping, live-oracle capture, any map geometry beyond a flat plane. M5 candidates are listed in `docs/project_plan_v3.md` — decide with the owner, then update this section.

## Hard rules

1. **License: GPLv2.** Every source file gets a GPLv2 header. This is deliberate (reference-emulator compatibility); do not change it.
2. **No Blizzard assets.** No client data, DBC dumps, extracted maps, or copied art/data files in this repo, ever. Hand-authored constants in spec files are fine.
3. **Determinism is non-negotiable.**
   - Authoritative time is `int64` milliseconds. No wall-clock reads anywhere in `sim/`.
   - Authoritative state uses integers/fixed-point only (positions in cm, facing in milliradians, resources as int32). No floats in authoritative state or in any value that feeds the state hash. Transient float math is allowed only if the result is quantized before storage, and prefer integer math throughout.
   - RNG is counter-based and keyed: `roll(seed, entity_id, subsystem, event_seq)`. No global mutable RNG, no `rand()`, no `std::mt19937` shared across subsystems. Subsystem IDs live in one enum (`rng_subsystem.h`).
   - Simultaneous events resolve by a documented total order: `(time_ms, priority, source_id, target_id, seq)`. The order is defined in one place and tested.
   - Iteration over entity collections must have deterministic order (sorted containers or stable vectors — never `std::unordered_map` iteration in simulation logic).
4. **Every game formula gets a spec entry before or alongside its implementation.** Specs live in `docs/mechanics_spec.md`. Each entry has: formula, `source_status` (one of `primary | secondary | empirical | emulator_reference | guess`), `known_uncertainties`, and the tests that cover it. Do not implement a formula silently from memory — write the spec entry, mark uncertain constants with `TODO(verify)`, and proceed.
5. **Divergence ledger.** Any known or suspected difference between this simulator, cmangos-tbc behavior, and the pinned ruleset (Anniversary TBC) goes in `docs/divergence_ledger.md` with a one-line rationale. Seed entries are listed there already; append, never delete (mark resolved instead).
6. **Pass criteria are statistical, not visual.** "The logs look right" is never a justification in a commit message or test. Distribution claims are tested with explicit N, expected rates, and tolerance derived from N.
7. **No emulator code in this repo.** The reference emulator is consulted in a separate checkout. Porting its math here is acceptable under GPLv2, but cite the file/function in the spec entry's `source_status: emulator_reference` line so we can audit later.

## Combat-model facts to respect (PvP context)

- Target level 70 vs. 70, weapon skill 350 vs. defense skill 350 unless a scenario says otherwise.
- White hits use a **single-roll attack table** (one roll allocated across miss/dodge/parry/block/glancing/crit/ordinary ranges), not independent rolls. Spec the table before implementing.
- **Glancing blows do not occur against players.** The table builder must take an `is_player_target` flag; tests must confirm zero glances in PvP scenarios.
- **Parry and block are frontal-only. Dodge does not apply to attacks from behind.** Facing is therefore part of even this stationary scenario; the scenario file pins attacker position relative to defender facing.
- Block requires an equipped shield; crushing blows are mob-only (exclude in PvP, note in ledger).
- Dual-wield has its own miss penalty — still out of scope (no scenario has an off-hand); if a milestone adds it, spec it first.
- Rage generation from damage dealt/taken follows the TBC-era formula with a level-dependent conversion constant — spec it with `source_status` and `TODO(verify)` on the constant rather than guessing silently.

## Code conventions

- C++20, CMake, no exceptions in the sim hot path, no per-event heap allocation in the event loop (pre-size, reuse, or use arena allocation).
- Test framework: doctest (vendored single header is fine). Tests in `sim/tests/`.
- Tracing: JSONL, one event per line, header line carries `{sim_commit, ruleset_hash, scenario, seed}`. Hashing: xxhash or FNV-1a of canonical serialization — never of raw struct memory.
- Keep the kernel a library (`sim/core`, `sim/mechanics`) with thin binaries: `arena_run` (run scenario → trace), `arena_replay` (trace → verify hashes), `arena_dist` (N-trial distribution report), `arena_bench` (throughput).
- Scenario files: YAML or TOML, hand-parsed or via a tiny vendored parser; pin every stat explicitly (no defaults hidden in code).

## Testing requirements for any combat-math change

1. Unit test of the formula at fixed inputs.
2. Determinism test: same scenario + seed twice → identical trace hashes.
3. Replay test: recorded action/event trace re-verifies all hash checkpoints.
4. Distribution test where applicable: N ≥ 10^6 swings, observed rates within binomial CI of spec rates.
5. Golden traces under `sim/tests/golden_traces/` updated only with an explanatory commit message naming the formula change.

## Workflow

- Small commits, imperative messages, formula changes reference their spec entry.
- When a needed fact is unknown (e.g., exact base miss at equal level), do not stall and do not invent silently: write the spec entry with the best community-documented value, mark `TODO(verify)`, add a ledger entry, continue.
- Throughput numbers go in `docs/benchmarks.md` with hardware, compiler, and flags. Current throughput is orders of magnitude above any training need; do not optimize at the cost of clarity (the named scaling path, when needed, is batched multi-env stepping).
- If a task seems to require anything in the OUT list, stop and flag it instead of building it.
