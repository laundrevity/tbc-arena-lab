# M0 Status — deterministic warrior auto-attack kernel

Date: 2026-06-10. Session 1 complete: `ctest` green (29 cases / ~40k
assertions), both golden traces replay-verified, `arena_dist` self-test
PASSes at N=10^6 for both scenarios, throughput recorded in
`docs/benchmarks.md` (~1.5e6 swings/sec, ~5.4e6× realtime, single core).

## What exists

- `sim/core`: int64-ms clock; binary-heap event queue with the documented
  total order `(time_ms, priority, source_id, target_id, seq)`; counter-based
  SplitMix64 RNG keyed `(seed, entity, subsystem, seq)`; integer-only
  authoritative state; canonical little-endian serialization + FNV-1a state
  hash, checkpointed every authoritative 1000 ms; hand-parsed YAML-subset
  scenario loader (every stat pinned, missing key = load error); JSONL trace
  writer/reader.
- `sim/mechanics`: single-roll PvP attack table (M-001), weapon damage / armor
  / crit / block pipeline (M-002..M-005), deci-rage generation (M-006), swing
  timers + match runner (M-007), integer frontal-arc facing (M-008),
  distribution report (dist), trace replay verification.
- Tools: `arena_run`, `arena_replay`, `arena_dist` (human table + JSON with
  table-builder expected ranges — this JSON is the differential-harness input
  format), `arena_bench`.
- Scenarios: `m0_front_shield.yaml` (full table), `m0_behind.yaml`
  (miss/crit/hit via the facing gate alone).
- Golden traces under `sim/tests/golden_traces/`, seed 42, 60 s each.

## What is pinned (and where)

- Ruleset id `anniversary-tbc-2.5.x`; bootstrap oracle cmangos-tbc (2.4.3
  model) — divergence ledger D-003.
- Level 70 vs 70, weapon skill 350 vs defense 350; all chances pinned
  per-myriad in scenario files (derivation from ratings/agility out of scope).
- `ruleset_hash` = FNV-1a of the constants manifest (`sim/mechanics/ruleset.cpp`);
  it is embedded in every trace header and replay fails loudly if constants
  change. Current value: `0xaf21e9d64e57ce01`.
- RNG streams: pinned canary values in `test_rng.cpp`; damage rolls consume
  sequence numbers only on contact outcomes, checkpoints consume none.

## Open `TODO(verify)` items (all in docs/mechanics_spec.md)

- M-001: base miss 5.00% vs equal-level player; ±0.04%/point skill-delta step;
  blocked-crit exclusivity (D-010); over-full-table clamping order.
- M-002/M-003/M-004: per-stage integer flooring vs oracle float math (D-004);
  armor-then-crit pipeline order; 75% DR cap interaction with flooring.
- M-006: `RAGE_C10_L70 = 2747`; no rage on miss/dodge/parry in TBC (D-008);
  rage on post-mitigation damage; no 15D/c ceiling on the hit-factor form.
- M-008: dodge facing-gated exactly as CLAUDE.md pins it.

## Known limitations (deliberate, M0)

- Mob path of the attack table unimplemented (`is_player_target=false`
  aborts). Parry-haste absent (D-006). Single main-hand weapon only; level-70
  rage constant only (loader rejects other levels). `SIM_COMMIT` in trace
  headers is set at CMake configure time and can go stale until reconfigure.

## What the next session needs

Instrumented cmangos-tbc oracle bring-up + differential harness:

1. Separate cmangos-tbc checkout (never vendored here — CLAUDE.md rule 7),
   instrumented to log white-swing outcomes/damage/rage for the same two
   pinned stat blocks.
2. Harness consumes `arena_dist --json` output (fields: `expected_table_pm`,
   per-outcome `count`/`observed_rate`/`ci_half_width`, `damage` summary,
   `rage_deci_uncapped`) and compares oracle empirical rates against our
   expected table with explicit N and CI; discrepancies go to the divergence
   ledger, resolved TODO(verify)s get their spec entries updated to
   `source_status: emulator_reference` with file/function citations.
3. First targets: the M-001 base-miss and skill-delta constants, M-006 rage
   constants, M-004 rounding (D-004).
