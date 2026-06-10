# Differential Harness — tbc-arena-lab

Compares this simulator's white-swing statistics against an independent model
of the bootstrap oracle (cmangos-tbc @ commit 009455e, separate checkout at
`../cmangos-tbc`). The oracle model lives in `sim/oracle/` — math ported
(rewritten, never copied) per-function under GPLv2 with file:line citations,
per CLAUDE.md rule 7. It deliberately reproduces the oracle's FLOAT pipeline
including truncation points and rage bookkeeping quirks, so divergences are
quantified rather than averaged away.

## Pipeline

```
arena_dist <scenario> <seed> --swings 1000000 --json dist.json   # sim side
arena_diff dist.json [--base-dir DIR] [--json diff.json]         # comparison
```

`arena_diff` re-loads the scenario named in the report, rebuilds both tables,
runs a deterministic oracle Monte Carlo (keyed RNG, subsystems
`oracle_table`/`oracle_damage`; same N as the report unless `--mc-swings`),
and emits a row-per-claim verdict. It refuses reports whose `ruleset_hash`
does not match the current build.

## Row semantics

STRICT rows decide the exit code (0 iff all pass):

| row | criterion |
|---|---|
| `table_pm.*` | sim table width == oracle table width, exact per-myriad |
| `rate.*` | sim observed count vs oracle expected rate, 99% binomial CI; zero-width ranges must have zero observations |
| `damage.mean` | two-sample z ≤ 2.576 vs oracle MC |
| `damage.min/max` | within ±2 (discrete vs truncated-continuous rolls, D-004/D-014) |
| `rage_att/def.mean(ours-basis)` | two-sample z ≤ 2.576 vs oracle ARITHMETIC applied under our conventions (contact-only, post-block damage) — validates the integer port of M-006 |

INFO rows never fail; they quantify ledgered divergences:

| row | meaning |
|---|---|
| `rage_att.mean(oracle-true)` | oracle's actual attacker rage incl. rage on miss (D-011), doubled clean damage on dodge/parry (D-012), pre-block+blocked basis on block (D-013) |
| `rage_def.mean(oracle-true)` | oracle's victim rage incl. the D-013 block basis |

Recorded magnitudes at N=10^6 (2026-06-10, both scenarios PASS):
m0_front_shield oracle-true attacker rage ≈ 205.9 deci/swing vs ours 165.4
(the avoid-rage quirks are worth ~24%); m0_behind ≈ 191.3 vs 188.0 (only
miss-rage applies from behind).

## Statistical caveats

- The D-014 offset (our discrete inclusive weapon roll vs the oracle's
  truncated `frand`) is ≈0.3–0.5 damage pre-crit. At N=10^6 it sits well
  inside the 99% CI; above N≈3×10^7 the `damage.mean` row would start
  failing on this known, ledgered difference. If N is ever raised, subtract
  the documented offset or widen that row's criterion deliberately.
- The `rage.*(ours-basis)` rows inherit part of the D-014 offset (rage is
  proportional to damage); observed |z| ≈ 2 at N=10^6. Same note applies.
- The oracle MC uses transient floats (mirroring the emulator). It is
  deterministic for a fixed seed on a given build/platform (tested), but
  bit-stability across compilers is not guaranteed — acceptable for a
  diagnostic tool; the simulator itself remains integer-only.
- This validates our model against cmangos-tbc SOURCE-derived math. It does
  not yet exercise a running instrumented emulator (live combat-log capture
  remains future oracle work), and says nothing about Anniversary TBC 2.5.x
  (D-003) — every confirmed formula is still `TODO(verify)` against the
  pinned ruleset where noted in the spec.
