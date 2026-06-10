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
- **M3 — DONE (2026-06-10).** Observation & action-space spec
  (`docs/observation_action_spec.md`): client-parity integer observations,
  action masks from observations alone, fixed decision ticks with
  simultaneous-move snapshots, Policy interface (idle/scripted/playback),
  decision tracing + bit-exact action playback (imitation substrate).
- **M4 — DONE (2026-06-10).** Python bindings (`docs/python_bindings.md`):
  resumable MatchEngine (run_match is a wrapper over it — one
  implementation), C ABI shared library, pure-ctypes Python package, parity
  + determinism + trace-round-trip tests, throughput recorded.
- **M5 — DONE (2026-06-10): self-play bring-up.** Outcome: substrate
  validated (learning works; interface and determinism held under ~50M env
  steps; yellow differential coverage shipped and passing), but the pinned
  pass criterion was NOT met — two full-N candidates each fail one
  different condition; the open problem is robustness-vs-specialization
  (findings F1–F4, results and M6 recommendations in
  `docs/m5_selfplay.md`). Original scope follows:
  produce the first evidence that the substrate actually trains — nothing has
  been learned against this env yet, and observation/action/reward design
  errors get more expensive with every mechanic stacked on top. Scope:
  - Minimal PPO self-play on `m2_duel` via the existing ctypes bindings,
    living in `python/train/` (in-repo by owner decision; training code stays
    outside `sim/`; experiments must be seeded and reproducible, but repo
    spec/golden-trace rules apply to the env, not to experiment code).
  - Pass criterion, statistical per CLAUDE.md rule 6: the learned policy
    beats Idle decisively and beats-or-matches ScriptedPolicy over N seeded
    evaluation matches within a binomial CI (N and thresholds pinned in the
    experiment config before training).
  - Folded in: yellow-attack differential coverage in `arena_diff` (MS/HS
    math now feeds training; the harness currently checks whites only).
  - Explicitly deferred: batched multi-env stepping — single-process Python
    already does ~430 duels/sec on the owner's M5 Max; build batching only
    when a profiled training run shows the ctypes loop is the wall.
- **M6+ — TBD with the owner.** Candidates, unordered: dual-wield; stances +
  more abilities (richer decision space, once learning demonstrably works);
  batched env stepping (numpy buffers) if M5 profiling demands it;
  movement/chase/leeway; live-oracle capture (instrumented cmangos server);
  Anniversary 2.5.x evidence pass (D-003); combat-log observation windows.
