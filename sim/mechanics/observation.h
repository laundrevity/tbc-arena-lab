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

#include "sim/core/fixed_trig.h"
#include "sim/core/unit_state.h"
#include "sim/mechanics/abilities.h"

// Observation and action space (docs/observation_action_spec.md). Client
// parity: every field is something a human at the TBC client could know;
// enemy cooldowns/GCD/swing timer/HS queue are deliberately absent.
// Integer-only; masks are computable from the observation alone.

namespace arena {

constexpr int64_t isqrt64(int64_t v) {
    if (v <= 0) return 0;
    int64_t x = v;
    int64_t y = (x + 1) / 2;
    while (y < x) {
        x = y;
        y = (x + v / x) / 2;
    }
    return x;
}

// Field order here is the canonical order (spec table); serialize() must
// match it exactly.
struct Observation {
    int64_t t_ms = 0;
    int64_t match_remaining_ms = 0;
    int32_t self_hp = 0;
    int32_t self_max_hp = 0;
    int32_t self_rage_deci = 0;
    int64_t self_gcd_remaining_ms = 0;
    int64_t self_ms_cd_remaining_ms = -1;  // -1 when Mortal Strike unknown
    int64_t self_swing_remaining_ms = -1;  // -1 when not auto-attacking
    int32_t self_hs_queued = 0;
    int32_t self_knows_ms = 0;
    int32_t self_knows_hs = 0;
    int32_t self_weapon_speed_ms = 0;
    int32_t enemy_hp_pm = 0;     // hostile frames show fractions only
    int32_t enemy_rage_deci = 0; // target resource bar is visible
    int32_t enemy_in_self_front = 0;
    int32_t self_in_enemy_front = 0;
    int64_t distance_cm = 0;

    void serialize(std::vector<uint8_t>& buf) const {
        put_i64(buf, t_ms);
        put_i64(buf, match_remaining_ms);
        put_i32(buf, self_hp);
        put_i32(buf, self_max_hp);
        put_i32(buf, self_rage_deci);
        put_i64(buf, self_gcd_remaining_ms);
        put_i64(buf, self_ms_cd_remaining_ms);
        put_i64(buf, self_swing_remaining_ms);
        put_i32(buf, self_hs_queued);
        put_i32(buf, self_knows_ms);
        put_i32(buf, self_knows_hs);
        put_i32(buf, self_weapon_speed_ms);
        put_i32(buf, enemy_hp_pm);
        put_i32(buf, enemy_rage_deci);
        put_i32(buf, enemy_in_self_front);
        put_i32(buf, self_in_enemy_front);
        put_i64(buf, distance_cm);
    }
};

inline Observation build_observation(const UnitSpec& self, const UnitState& self_st,
                                     const UnitSpec& enemy, const UnitState& enemy_st,
                                     int64_t t, int64_t duration_ms) {
    const auto clamp0 = [](int64_t v) { return v < 0 ? 0 : v; };
    Observation o;
    o.t_ms = t;
    o.match_remaining_ms = clamp0(duration_ms - t);
    o.self_hp = self_st.hp;
    o.self_max_hp = self.max_hp;
    o.self_rage_deci = self_st.rage_deci;
    o.self_gcd_remaining_ms = clamp0(self_st.gcd_ready_ms - t);
    o.self_ms_cd_remaining_ms =
        self.knows_mortal_strike ? clamp0(self_st.ms_ready_ms - t) : -1;
    o.self_swing_remaining_ms =
        self_st.next_swing_ms >= 0 ? clamp0(self_st.next_swing_ms - t) : -1;
    o.self_hs_queued = self_st.hs_queued;
    o.self_knows_ms = self.knows_mortal_strike ? 1 : 0;
    o.self_knows_hs = self.knows_heroic_strike ? 1 : 0;
    o.self_weapon_speed_ms = self.weapon_speed_ms;
    o.enemy_hp_pm = enemy.max_hp > 0
                        ? static_cast<int32_t>(static_cast<int64_t>(enemy_st.hp) * 10000 /
                                               enemy.max_hp)
                        : 0;
    o.enemy_rage_deci = enemy_st.rage_deci;
    o.enemy_in_self_front =
        in_frontal_arc(self_st.pos_x_cm, self_st.pos_y_cm, self_st.facing_mrad,
                       enemy_st.pos_x_cm, enemy_st.pos_y_cm)
            ? 1
            : 0;
    o.self_in_enemy_front =
        in_frontal_arc(enemy_st.pos_x_cm, enemy_st.pos_y_cm, enemy_st.facing_mrad,
                       self_st.pos_x_cm, self_st.pos_y_cm)
            ? 1
            : 0;
    const int64_t dx = enemy_st.pos_x_cm - self_st.pos_x_cm;
    const int64_t dy = enemy_st.pos_y_cm - self_st.pos_y_cm;
    o.distance_cm = isqrt64(dx * dx + dy * dy);
    return o;
}

// --- Actions (docs/observation_action_spec.md) ---

enum class Action : int32_t {
    None = 0,
    CastMortalStrike = 1,
    QueueHeroicStrike = 2,
    UnqueueHeroicStrike = 3,
};
constexpr int32_t ACTION_COUNT = 4;

const char* action_name(Action a);

constexpr uint32_t action_bit(Action a) { return 1u << static_cast<int32_t>(a); }

// Legality from the observation ALONE (spec principle 3). The engine
// re-validates on application against authoritative state.
constexpr uint32_t legal_action_mask(const Observation& o) {
    uint32_t m = action_bit(Action::None);
    if (o.self_knows_ms && o.self_rage_deci >= MS_RAGE_COST_DECI &&
        o.self_ms_cd_remaining_ms == 0 && o.self_gcd_remaining_ms == 0) {
        m |= action_bit(Action::CastMortalStrike);
    }
    if (o.self_knows_hs && !o.self_hs_queued) m |= action_bit(Action::QueueHeroicStrike);
    if (o.self_knows_hs && o.self_hs_queued) m |= action_bit(Action::UnqueueHeroicStrike);
    return m;
}

} // namespace arena
