# Benchmarks — tbc-arena-lab

Throughput numbers for the simulation kernel. Every row records hardware,
compiler, and flags. Do not optimize past the M0 target at the cost of
clarity (CLAUDE.md workflow).

M0 informal bar: ≥ ~10^6 swings/sec on one core; anything under that gets a
note on where the time goes, not optimization work. Long-term target
(post-M0): ~500× realtime per core for a full 2v2.

## Results

(appended by hand from `arena_bench` runs)

### 2026-06-10 — M0 kernel (commit 3221fe6)

- Hardware: Intel Core i9-11900K @ 3.50 GHz, single core, Linux 6.17
- Compiler: GCC 13.3.0, CMake RelWithDebInfo (`-O2 -g -DNDEBUG -Wall -Wextra -Werror`)
- Command: `arena_bench scenarios/m0_front_shield.yaml --seconds 3`

| metric | 60 s matches | `--duration-ms 100000000` (ends by death ~121 s) |
|---|---|---|
| simulated-ms per wall-ms | 5,416,117 | 5,526,522 |
| swings/sec | 1.54e6 | 1.58e6 |
| checkpoints/sec | 5.42e6 | 5.51e6 |

Meets the M0 bar (≥ ~10^6 swings/sec on one core). Where the time goes: at a
3.6 s weapon there are 3.6 hash checkpoints per swing, so checkpoint
serialization+FNV dominates the event count; swing resolution itself
(table build + two keyed rolls + integer pipeline) is a minority of the
profile. No optimization warranted at M0 (CLAUDE.md workflow).

### 2026-06-10 — M1 mutual combat (commit 16b49b6+)

- Same hardware/compiler/flags as above.
- Command: `arena_bench scenarios/m1_mutual.yaml --seconds 3`

| metric | 60 s matches |
|---|---|
| simulated-ms per wall-ms | 4,738,232 |
| swings/sec | 3.28e6 |
| checkpoints/sec | 4.74e6 |

Two attackers (3.6 s + 2.6 s weapons, parry-haste active) roughly double
swings/sec; realtime multiple dips ~12% from the extra swing events and
parry-haste retiming. Still far above the bar; no optimization warranted.

### 2026-06-10 — M2 abilities + GCD (commit d2e72b4+)

- Same hardware/compiler/flags as above.
- Command: `arena_bench scenarios/m2_duel.yaml --seconds 3`

| metric | 60 s matches |
|---|---|
| simulated-ms per wall-ms | 2,432,708 |
| swings/sec | 9.60e5 |
| abilities/sec | 1.11e6 |
| checkpoints/sec | 2.43e6 |

Total attack resolutions ~2.07e6/sec (whites + yellows). Realtime multiple
halves vs M1: each combat event now also runs policy evaluation, and MS adds
Decide wake-ups plus three extra keyed rolls per cast. Per-1v1-match cost is
still ~25 µs of wall time per simulated minute — far above any M-target; no
optimization warranted (CLAUDE.md workflow).
