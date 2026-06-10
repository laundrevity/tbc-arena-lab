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

### 2026-06-10 — M3 observation/action interface (commit 92ea56c+)

- Same hardware/compiler/flags as above.
- Command: `arena_bench scenarios/m2_duel.yaml --seconds 3` (100 ms decision ticks)

| metric | 60 s matches |
|---|---|
| simulated-ms per wall-ms | 627,754 |
| swings/sec | 2.48e5 |
| abilities/sec | 2.85e5 |
| checkpoints/sec | 6.28e5 |

Decision ticks dominate the event count now: 10 ticks/sim-second, each
building two observations and running two policies (virtual dispatch).
~4x drop vs M2 as expected; still ~6e5x realtime per core — a full 60 s
duel costs ~0.1 ms of wall time. Pure auto-attack scenarios skip the tick
chain entirely and keep their M1-era throughput. No optimization
warranted (CLAUDE.md workflow).

### 2026-06-10 — M4 Python bindings (commit a039067+)

- Same hardware; Python 3.13, ctypes, single core, m2_duel, 100 ms ticks.

| driver | steps/s | simulated-sec per wall-sec |
|---|---|---|
| Python step() only | 9.9e5 | 9.9e4 |
| Python full loop (observe+masks) | 1.4e5 | 1.4e4 |

ctypes call overhead dominates the full loop (5 calls + dict per tick per
unit). Acceptable for early experiments; the scaling path is a batched
multi-env step call with preallocated buffers (future milestone). Native
C++ throughput is unchanged by the engine refactor (goldens byte-identical).
