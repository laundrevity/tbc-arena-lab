# Project Plan v3 — tbc-arena-lab

(Reconstructed 2026-06-10 from the project owner's statement of goals; kept
deliberately minimal. Milestone scope beyond the current one is decided with
the owner per milestone — nothing below M-current is built speculatively,
per CLAUDE.md.)

## Vision

A scratch-built, deterministic, high-throughput simulator of WoW: The Burning
Crusade arena combat (pinned ruleset: Anniversary TBC 2.5.x; bootstrap
oracle: cmangos-tbc), used as an RL research environment. Eventual goals, in
the owner's words: **train AI to play arena optimally through self-play, and
through watching — and perhaps participating in — actual arena matches.**

Implications kept in view (but not built early):
- Self-play needs the simulator side: throughput, determinism, replay,
  eventually observation models / action masks / Python bindings (all
  currently OUT, per CLAUDE.md).
- Learning from real matches needs the fidelity side: the divergence ledger,
  the differential harness, and trace formats that real combat logs can be
  mapped onto.
- Possible live participation puts a premium on rule-exact fidelity and on
  latency-realistic action timing — far future; noted so design choices
  don't paint it out.

## Milestones

- **M0 — DONE (2026-06-10).** Deterministic kernel: one warrior auto-attacking
  a stationary warrior; integer time/state, counter-based RNG, canonical
  hashing, JSONL traces, replay, distribution + bench tools, golden traces.
  See `docs/m0_status.md`.
- **M0.5 — DONE (2026-06-10).** Oracle audit of every white-swing formula
  against cmangos-tbc source @ 009455e; mechanics aligned; `sim/oracle/` +
  `arena_diff` differential harness. See `docs/differential_harness.md`.
- **M1 — DONE (2026-06-10).** Both warriors auto-attack: interleaved swing
  timers under the documented event total order, parry-haste (D-006
  resolved), rage on both sides, death of either unit.
- **M2 — DONE (2026-06-10).** First abilities + GCD: Mortal Strike (instant,
  cooldown, rage cost, normalized weapon damage), Heroic Strike
  (on-next-swing, off-GCD), yellow hit resolution (avoidance die + separate
  crit and partial-block rolls), full-cost-on-avoid, deterministic policy
  knobs pinned per scenario (decisions exist; RL still out). Specs
  M-011..M-016.
- **M3+ — TBD with the owner.** Candidates, unordered: dual-wield; stances +
  more abilities (rage sinks with tradeoffs); movement/chase/leeway;
  observation & action-space spec; Python bindings; self-play harness;
  live-oracle capture (instrumented cmangos server); Anniversary 2.5.x
  evidence pass (D-003); yellow-attack differential coverage in arena_diff.
