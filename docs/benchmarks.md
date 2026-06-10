# Benchmarks — tbc-arena-lab

Throughput numbers for the simulation kernel. Every row records hardware,
compiler, and flags. Do not optimize past the M0 target at the cost of
clarity (CLAUDE.md workflow).

M0 informal bar: ≥ ~10^6 swings/sec on one core; anything under that gets a
note on where the time goes, not optimization work. Long-term target
(post-M0): ~500× realtime per core for a full 2v2.

## Results

(appended by hand from `arena_bench` runs)
