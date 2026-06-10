/*
 * This file is part of tbc-arena-lab.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#pragma once

#include <cstdint>

#include "sim/core/rng_subsystem.h"

// Counter-based keyed RNG (spec M-009). Stateless: every value is a pure
// function of (seed, entity_id, subsystem, seq). No global mutable RNG
// exists anywhere in sim/ (CLAUDE.md rule 3).

namespace arena {

// SplitMix64 finalizer step.
constexpr uint64_t rng_mix64(uint64_t z) {
    z += 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

constexpr uint64_t roll_u64(uint64_t seed, int32_t entity_id, RngSubsystem subsystem, uint64_t seq) {
    uint64_t h = rng_mix64(seed);
    h = rng_mix64(h ^ ((static_cast<uint64_t>(static_cast<uint32_t>(entity_id)) << 32) |
                       static_cast<uint64_t>(subsystem)));
    h = rng_mix64(h ^ seq);
    return rng_mix64(h);
}

// Bounded reduction via 128-bit multiply-shift (Lemire, no rejection).
// Bias <= n / 2^64 — accepted for determinism and speed (spec M-009).
constexpr uint64_t rng_bound(uint64_t x, uint64_t n) {
    return static_cast<uint64_t>((static_cast<unsigned __int128>(x) * n) >> 64);
}

// Uniform per-myriad roll in [0, 10000).
constexpr int32_t roll_myriad(uint64_t seed, int32_t entity_id, RngSubsystem subsystem, uint64_t seq) {
    return static_cast<int32_t>(rng_bound(roll_u64(seed, entity_id, subsystem, seq), 10000));
}

// Uniform integer in [lo, hi], inclusive. Requires lo <= hi.
constexpr int32_t roll_range(uint64_t seed, int32_t entity_id, RngSubsystem subsystem, uint64_t seq,
                             int32_t lo, int32_t hi) {
    const uint64_t span = static_cast<uint64_t>(static_cast<int64_t>(hi) - lo + 1);
    return lo + static_cast<int32_t>(rng_bound(roll_u64(seed, entity_id, subsystem, seq), span));
}

} // namespace arena
