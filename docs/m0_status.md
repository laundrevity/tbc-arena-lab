# M0 Status — deterministic warrior auto-attack kernel

Date: 2026-06-10. Session 1 complete: `ctest` green, both golden traces
replay-verified, `arena_dist` self-test PASSes at N=10^6 for both scenarios,
throughput recorded in `docs/benchmarks.md` (~1.5e6 swings/sec, ~5.4e6×
realtime, single core).

**Session 5 complete (same date): M3 — observation/action interface.** The
RL-facing decision substrate per `docs/observation_action_spec.md`
(normative): client-parity observations (integer-only, canonical
serialization; enemy cooldowns/swing timer hidden, enemy HP as fraction,
enemy rage visible), a 4-action space with masks derivable from the
observation alone, fixed decision ticks (100 ms in fixtures; simultaneous
-move snapshots, entity-order application, illegal submissions degrade to
None and are counted), and the `Policy` interface (`Idle`, `Scripted`,
`Playback`). M2's event-driven scripted reflexes became tick-quantized
ScriptedPolicy decisions — same knobs, reaction latency at tick resolution;
`next_decide_ms` left authoritative state (state_v=2). Decisions are traced
(`decision` lines; un-logged tick = None) and verified by replay; the
playback round-trip test (record -> PlaybackPolicy -> bit-exact checkpoints)
is the imitation-learning substrate. Scenario schema: `decision_tick_ms`,
`knows_*` loadout, `policy`, `scripted_hs_min_rage_deci`. M0/M1 combat
event streams remain byte-identical; throughput ~6.3e5x realtime with ticks
(tick-free scenarios keep M1-era speed).

**Session 4 complete (same date): M2 — first abilities + GCD.** Mortal
Strike (instant, 6 s cooldown, 30 rage, normalized weapon damage + 210,
specs M-013/M-014) and Heroic Strike (on-next-swing, off-GCD, +176, M-015)
on top of a GCD framework (M-011) and the audited yellow hit pipeline
(M-012: avoidance die without a block side, separate crit and partial-block
rolls — blocked crits exist; crit-before-armor order, D-020). Full rage cost
on avoided casts; no attacker rage from spells; victim rage from ability
damage follows retail over the oracle (M-016, D-019). Decisions exist via
deterministic per-scenario policy knobs evaluated event-driven with Decide
wake-ups (event order now checkpoint < end < decide < swing). New fixture
`scenarios/m2_duel.yaml` + golden; hand-pinned MS/HS timeline tests; M0/M1
golden event streams byte-identical (header/init format only). Scenario
files gained pinned keys: `weapon_norm_ms`, `initial_rage_deci`,
`use_mortal_strike`, `heroic_strike_min_rage_deci`. Throughput ~2.4e6x
realtime (docs/benchmarks.md). arena_dist/arena_diff remain white-swing
validators; yellow differential coverage is listed for M3+.

**Session 3 complete (same date): M1 — both warriors attack.** Defender may
now have `attacks: true`; parry-haste implemented per the oracle's main-hand
rule (spec M-010, D-006 resolved) with lazy event invalidation in the match
loop (spec M-007); same-timestamp lethal ties resolve by the documented total
order (D-018). New fixture `scenarios/m1_mutual.yaml` (2H vs 1H+shield) with
golden trace; forced-parry test pins the hastened timeline exactly. M0
behavior unchanged (M0 goldens differ in header ruleset_hash only).
Throughput: 3.28e6 swings/sec, ~4.7e6x realtime (docs/benchmarks.md). The
project plan now exists: docs/project_plan_v3.md (M2+ scope TBD with owner).

**Session 2 complete (same date): oracle audit + differential harness.**
Every white-swing formula was audited against cmangos-tbc source @ 009455e
(separate checkout, CLAUDE.md rule 7); spec entries upgraded to
`emulator_reference` with file:line citations; mechanics aligned where the
audit found differences (crit level-cap skill term, block-before-glance die
order, mutual facing gate, armor min-1 floor, oracle rage arithmetic) with
goldens regenerated under an explanatory commit. `sim/oracle/` +
`arena_diff` form the differential harness (see
`docs/differential_harness.md`); both scenarios PASS at N=10^6. Oracle
avoid-rage quirks are deliberately NOT adopted (ledger D-011/D-012/D-013) and
are quantified by the harness's INFO rows.

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

Resolved against the ORACLE in session 2 (all now cited): base miss 5.00%,
skill-delta steps, blocked-crit exclusivity (D-010), armor formula + cap,
`RAGE_C10_L70 = 2747`, dodge facing gate. Still open:

- Everything remains `TODO(verify)` against **Anniversary TBC 2.5.x** (the
  oracle models 2.4.3; D-003) — needs retail-side evidence or a 2.5.x source.
- No-rage-on-avoid: we diverge from the oracle deliberately
  (D-011/D-012/D-013); authentic 2.4.3/2.5.x behavior unconfirmed.
- Over-full-table clamping order (unexercised by fixtures); per-stage integer
  flooring vs float pipeline (D-004); discrete vs continuous weapon roll
  (D-014); integer rage 1-ulp boundary cases (D-016).

## Known limitations (deliberate, M0)

- Mob path of the attack table unimplemented (`is_player_target=false`
  aborts). Parry-haste absent (D-006). Single main-hand weapon only; level-70
  rage constant only (loader rejects other levels). `SIM_COMMIT` in trace
  headers is set at CMake configure time and can go stale until reconfigure.

## What the next session needs

Session 2 delivered the source-level oracle audit and the `arena_diff`
harness (items 2–3 of the original plan, with the oracle consulted at source
level rather than via a running server). Remaining oracle work and candidate
next steps, in rough priority order:

1. **Live-oracle capture (optional hardening):** build and run the
   instrumented cmangos-tbc server (MySQL + DB data; heavyweight) to capture
   real combat-log swings for the two pinned stat blocks, and feed those
   empirical rates through the same `arena_diff` row format. This would catch
   runtime behaviors a source audit can miss (proc ordering, state flags).
2. **Anniversary 2.5.x evidence pass (D-003):** the pinned ruleset is still
   unverified everywhere; decide sourcing (client combat logs, community
   research) and start resolving the per-entry `TODO(verify)` markers.
3. **Milestone M1 scope decision:** per CLAUDE.md, `docs/project_plan_v3.md`
   is absent — ask before inventing scope. Natural candidates given M0:
   second attacker (parry-haste D-006 becomes observable), dual-wield, or
   movement — all currently OUT and gated on an explicit plan.
