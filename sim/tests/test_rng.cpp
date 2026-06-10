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

#include "doctest.h"

#include "sim/core/rng.h"

using namespace arena;

// Pinned stream canary (spec M-009): if any of these change, an RNG or key
// derivation refactor has disturbed every recorded trace and golden — that
// must be a loud, deliberate event, never an accident.
TEST_CASE("rng: pinned outputs for fixed (seed, entity, subsystem, seq) tuples") {
    CHECK(roll_u64(0, 0, RngSubsystem::swing_table, 0) == UINT64_C(0x2130748aaac80268));
    CHECK(roll_u64(42, 1, RngSubsystem::swing_table, 0) == UINT64_C(0x336293d08e404b2a));
    CHECK(roll_u64(42, 1, RngSubsystem::swing_table, 1) == UINT64_C(0xabc10668217fb203));
    CHECK(roll_u64(42, 1, RngSubsystem::weapon_damage, 0) == UINT64_C(0x4767fa6950e0a913));
    CHECK(roll_u64(42, 2, RngSubsystem::swing_table, 0) == UINT64_C(0x5270e70ccb8e48eb));
    CHECK(roll_u64(12345, 1, RngSubsystem::swing_table, 999) == UINT64_C(0xb53f0b1a75244685));

    CHECK(roll_myriad(42, 1, RngSubsystem::swing_table, 0) == 2007);
    CHECK(roll_range(42, 1, RngSubsystem::weapon_damage, 0, 320, 480) == 364);
}

TEST_CASE("rng: keyed streams are pure functions (same tuple, same value)") {
    for (uint64_t seq = 0; seq < 100; ++seq) {
        CHECK(roll_u64(7, 1, RngSubsystem::swing_table, seq) ==
              roll_u64(7, 1, RngSubsystem::swing_table, seq));
    }
}

TEST_CASE("rng: distinct subsystems and entities give distinct streams") {
    int differs_subsystem = 0;
    int differs_entity = 0;
    for (uint64_t seq = 0; seq < 64; ++seq) {
        if (roll_u64(7, 1, RngSubsystem::swing_table, seq) !=
            roll_u64(7, 1, RngSubsystem::weapon_damage, seq))
            ++differs_subsystem;
        if (roll_u64(7, 1, RngSubsystem::swing_table, seq) !=
            roll_u64(7, 2, RngSubsystem::swing_table, seq))
            ++differs_entity;
    }
    CHECK(differs_subsystem == 64);
    CHECK(differs_entity == 64);
}

TEST_CASE("rng: bounded helpers stay in range") {
    for (uint64_t seq = 0; seq < 10000; ++seq) {
        const int32_t m = roll_myriad(99, 1, RngSubsystem::swing_table, seq);
        CHECK(m >= 0);
        CHECK(m < 10000);
        const int32_t r = roll_range(99, 1, RngSubsystem::weapon_damage, seq, 320, 480);
        CHECK(r >= 320);
        CHECK(r <= 480);
    }
    // Degenerate single-value range.
    CHECK(roll_range(99, 1, RngSubsystem::weapon_damage, 0, 5, 5) == 5);
}
