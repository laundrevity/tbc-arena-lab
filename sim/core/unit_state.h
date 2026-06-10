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
#include <vector>

#include "sim/core/fnv1a.h"

// Authoritative state is integers/fixed-point only (CLAUDE.md rule 3):
// time int64 ms, positions int64 cm, facing int32 milliradians,
// probabilities int32 per-myriad, rage int32 deci-rage (0..1000).

namespace arena {

// Static, scenario-pinned stats. Not part of the per-checkpoint state hash
// (they cannot change in M0); the trace header's scenario reference pins them.
struct UnitSpec {
    int32_t entity_id = 0;
    int32_t level = 0;
    int32_t weapon_skill = 0;
    int32_t defense_skill = 0;
    int32_t attack_power = 0;
    int32_t crit_pm = 0;   // per-myriad
    int32_t hit_pm = 0;    // per-myriad
    int32_t weapon_min = 0;
    int32_t weapon_max = 0;
    int32_t weapon_speed_ms = 0;
    int32_t armor = 0;
    int32_t dodge_pm = 0;  // per-myriad
    int32_t parry_pm = 0;  // per-myriad
    int32_t block_pm = 0;  // per-myriad
    int32_t block_value = 0;
    bool has_shield = false;
    bool attacks = false;
    int32_t max_hp = 0;
    int64_t pos_x_cm = 0;
    int64_t pos_y_cm = 0;
    int32_t facing_mrad = 0;
};

// Dynamic authoritative state. next_swing_ms is the main-hand ready-at
// timestamp; -1 for units that never attack (spec M-007).
struct UnitState {
    int64_t pos_x_cm = 0;
    int64_t pos_y_cm = 0;
    int32_t facing_mrad = 0;
    int32_t hp = 0;
    int32_t rage_deci = 0;
    int64_t next_swing_ms = -1;
};

inline UnitState initial_state(const UnitSpec& spec) {
    UnitState s;
    s.pos_x_cm = spec.pos_x_cm;
    s.pos_y_cm = spec.pos_y_cm;
    s.facing_mrad = spec.facing_mrad;
    s.hp = spec.max_hp;
    s.rage_deci = 0;
    s.next_swing_ms = spec.attacks ? 0 : -1;
    return s;
}

// --- Canonical serialization: fixed field order, little-endian bytes. ---

inline void put_i32(std::vector<uint8_t>& buf, int32_t v) {
    const uint32_t u = static_cast<uint32_t>(v);
    buf.push_back(static_cast<uint8_t>(u));
    buf.push_back(static_cast<uint8_t>(u >> 8));
    buf.push_back(static_cast<uint8_t>(u >> 16));
    buf.push_back(static_cast<uint8_t>(u >> 24));
}

inline void put_i64(std::vector<uint8_t>& buf, int64_t v) {
    const uint64_t u = static_cast<uint64_t>(v);
    for (int shift = 0; shift < 64; shift += 8) {
        buf.push_back(static_cast<uint8_t>(u >> shift));
    }
}

inline void serialize_unit(std::vector<uint8_t>& buf, int32_t entity_id, const UnitState& s) {
    put_i32(buf, entity_id);
    put_i64(buf, s.pos_x_cm);
    put_i64(buf, s.pos_y_cm);
    put_i32(buf, s.facing_mrad);
    put_i32(buf, s.hp);
    put_i32(buf, s.rage_deci);
    put_i64(buf, s.next_swing_ms);
}

// State hash over all units in ascending entity_id order. `buf` is a caller
// owned scratch buffer so the event loop does no per-checkpoint allocation
// once it has grown.
inline uint64_t hash_units(std::vector<uint8_t>& buf, const int32_t* ids, const UnitState* states,
                           size_t count) {
    buf.clear();
    for (size_t i = 0; i < count; ++i) {
        serialize_unit(buf, ids[i], states[i]);
    }
    return fnv1a(buf.data(), buf.size());
}

} // namespace arena
