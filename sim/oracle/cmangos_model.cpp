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
 *
 * Math ported (rewritten) from cmangos-tbc @ 009455e under GPLv2.
 * Citations below are src/game/... file:line at that commit.
 */

#include "sim/oracle/cmangos_model.h"

#include <algorithm>
#include <cmath>

#include "sim/core/rng.h"

namespace arena::oracle {

namespace {

// shared/Util/Util.h:93-96 chance_u: float percent -> per-myriad,
// round-to-nearest.
int32_t chance_u(float chance) {
    return static_cast<int32_t>(std::roundf(std::max(0.0f, chance) * 100.0f));
}

float clamp_chance(float c) { return std::max(0.0f, std::min(c, 100.0f)); }

// Entities/Unit.cpp:2917 frand via our keyed RNG: uniform double in [0,1).
double to_unit(uint64_t x) { return static_cast<double>(x >> 11) * 0x1.0p-53; }

} // namespace

std::array<int32_t, OUTCOME_COUNT> build_table(const UnitSpec& att, const UnitSpec& def,
                                               bool facing_gate) {
    const float delta = static_cast<float>(def.defense_skill - att.weapon_skill);

    // Player-victim path only (the harness compares PvP scenarios).
    // Miss: GetMissChance 5.0 base (Unit.cpp:3889) + 0.04/pt actual weapon
    // skill delta (Unit.cpp:3995-4013) - attacker hit (Unit.cpp:4030),
    // melee minimum 0 (Unit.cpp:4031).
    const float miss =
        clamp_chance(5.0f + delta * 0.04f - static_cast<float>(att.hit_pm) / 100.0f);
    // Dodge/parry/block: own chance + 0.04/pt (Unit.cpp:3380-3468), gated by
    // CanDodge/Parry/BlockInCombat (facing, shield; Unit.cpp:3182-3240).
    const float dodge =
        facing_gate ? clamp_chance(static_cast<float>(def.dodge_pm) / 100.0f + delta * 0.04f)
                    : 0.0f;
    const float parry =
        facing_gate ? clamp_chance(static_cast<float>(def.parry_pm) / 100.0f + delta * 0.04f)
                    : 0.0f;
    const float block =
        (facing_gate && def.has_shield)
            ? clamp_chance(static_cast<float>(def.block_pm) / 100.0f + delta * 0.04f)
            : 0.0f;
    // Crit vs players: level-capped attacker skill (Unit.cpp:3954-3964).
    const float crit_delta = static_cast<float>(5 * att.level - def.defense_skill);
    const float crit =
        clamp_chance(static_cast<float>(att.crit_pm) / 100.0f + crit_delta * 0.04f);

    std::array<int32_t, OUTCOME_COUNT> w{};
    w[static_cast<size_t>(Outcome::Miss)] = chance_u(miss);
    w[static_cast<size_t>(Outcome::Dodge)] = chance_u(dodge);
    w[static_cast<size_t>(Outcome::Parry)] = chance_u(parry);
    w[static_cast<size_t>(Outcome::Block)] = chance_u(block);
    w[static_cast<size_t>(Outcome::Glance)] = 0;  // CanGlanceInCombat, Unit.cpp:3256
    w[static_cast<size_t>(Outcome::Crit)] = chance_u(crit);
    w[static_cast<size_t>(Outcome::Crush)] = 0;  // CanCrushInCombat, Unit.cpp:3242
    // The die has no explicit hit width (default side, Util.h:111-124);
    // mirror that by leaving hit as the remainder, floored at 0.
    int32_t acc = 0;
    for (size_t i = 0; i + 1 < OUTCOME_COUNT; ++i) {
        if (acc + w[i] > 10000) w[i] = 10000 - acc;
        acc += w[i];
    }
    w[static_cast<size_t>(Outcome::Hit)] = 10000 - acc;
    return w;
}

McReport run_mc(const UnitSpec& att, const UnitSpec& def,
                const std::array<int32_t, OUTCOME_COUNT>& table, uint64_t seed, uint64_t n) {
    McReport r;
    r.n = n;

    // Player::CalculateMinMaxDamage (StatSystem.cpp:377): AP/14 * speed
    // seconds folded into the float min/max before the roll.
    const float ap_part = static_cast<float>(att.attack_power) / 14.0f *
                          (static_cast<float>(att.weapon_speed_ms) / 1000.0f);
    const float dmin = static_cast<float>(att.weapon_min) + ap_part;
    const float dmax = static_cast<float>(att.weapon_max) + ap_part;

    // Unit::CalcArmorReducedDamage (Unit.cpp:2445-2471).
    float level_mod = static_cast<float>(att.level);
    if (level_mod > 59.0f) level_mod = level_mod + (4.5f * (level_mod - 59.0f));
    float dr = 0.1f * static_cast<float>(def.armor) / (8.5f * level_mod + 40.0f);
    dr = dr / (1.0f + dr);
    dr = std::min(std::max(dr, 0.0f), 0.75f);

    // Hit factors uint32(attack_time/1000 * f), Unit.cpp:940-949.
    const float speed_s = static_cast<float>(att.weapon_speed_ms) / 1000.0f;
    const uint32_t hf_hit = static_cast<uint32_t>(speed_s * 3.5f);
    const uint32_t hf_crit = static_cast<uint32_t>(speed_s * 7.0f);

    // Player::RewardRage conversion (Player.cpp:2336), float as the oracle.
    const float lv = static_cast<float>(att.level);
    const float c =
        static_cast<float>((0.0091107836 * lv * lv) + 3.225598133 * lv) + 4.2652911f;

    auto rage_att_deci = [&](uint32_t damage_basis, uint32_t hf) {
        // Player.cpp:2340 + ModifyPower(.., uint32(addRage * 10)).
        const float add =
            (static_cast<float>(damage_basis) / c * 7.5f + static_cast<float>(hf)) / 2.0f;
        return static_cast<int64_t>(static_cast<uint32_t>(add * 10.0f));
    };
    auto rage_def_deci = [&](uint32_t damage_basis) {
        // Player.cpp:2347.
        const float add = static_cast<float>(damage_basis) / c * 2.5f;
        return static_cast<int64_t>(static_cast<uint32_t>(add * 10.0f));
    };

    // Cumulative die (inclusive roll 1..10000, Util.h:111-124, Unit.cpp:2858).
    std::array<int32_t, OUTCOME_COUNT> cum{};
    int32_t acc = 0;
    for (size_t i = 0; i < OUTCOME_COUNT; ++i) {
        acc += table[i];
        cum[i] = acc;
    }

    double dmg_sum = 0, dmg_sq = 0;
    double ra_ours_sum = 0, ra_ours_sq = 0, ra_true_sum = 0;
    double rd_ours_sum = 0, rd_ours_sq = 0, rd_true_sum = 0;
    bool first_contact = true;
    uint64_t dmg_seq = 0;

    for (uint64_t i = 0; i < n; ++i) {
        // Damage is computed BEFORE the outcome roll in the oracle
        // (CalculateMeleeDamage, Unit.cpp:2031-2064); mirror that so the
        // dodge/parry "would-be damage" rage bases are populated.
        const double u =
            to_unit(roll_u64(seed, att.entity_id, RngSubsystem::oracle_damage, dmg_seq++));
        const float rolled = dmin + static_cast<float>(u) * (dmax - dmin);
        uint32_t d = static_cast<uint32_t>(rolled);  // uint32 assign, Unit.cpp:2031
        float after_armor = static_cast<float>(d) - static_cast<float>(d) * dr;
        if (after_armor < 1.0f) after_armor = 1.0f;  // Unit.cpp:2470
        d = static_cast<uint32_t>(after_armor);      // uint32 assign, Unit.cpp:2038

        const int32_t roll =
            1 + static_cast<int32_t>(
                    rng_bound(roll_u64(seed, att.entity_id, RngSubsystem::oracle_table, i),
                              10000));
        Outcome o = Outcome::Hit;
        for (size_t s = 0; s < OUTCOME_COUNT; ++s) {
            if (table[s] && roll <= cum[s]) {
                o = static_cast<Outcome>(s);
                break;
            }
        }
        ++r.outcome_counts[static_cast<size_t>(o)];

        uint32_t dealt = 0;       // final damage to hp
        uint32_t clean_att = 0;   // oracle attacker rage basis (cleanDamage)
        uint32_t clean_def = 0;   // oracle victim rage basis
        uint32_t hf = hf_hit;
        bool contact = false;
        switch (o) {
            case Outcome::Hit:
                dealt = d;
                clean_att = clean_def = d;  // Unit.cpp:2055
                contact = true;
                break;
            case Outcome::Crit: {
                dealt = static_cast<uint32_t>(static_cast<float>(d) * 2.0f);  // Unit.cpp:3825
                clean_att = clean_def = dealt;  // CalculateCritAmount, Unit.cpp:3830-3832
                hf = hf_crit;
                contact = true;
                break;
            }
            case Outcome::Block: {
                const uint32_t blocked = std::min(static_cast<uint32_t>(def.block_value), d);
                dealt = d - blocked;
                clean_att = clean_def = d + blocked;  // Unit.cpp:2055 + 2179-2180 (D-013)
                contact = true;
                break;
            }
            case Outcome::Dodge:
            case Outcome::Parry:
                clean_att = d + d;  // Unit.cpp:2055 + 2121/2133 (D-012)
                clean_def = 0;      // excluded, Unit.cpp:2297-2300
                break;
            case Outcome::Miss:
                clean_att = 0;  // rage still granted from hf alone (D-011)
                clean_def = 0;
                break;
            default:
                break;
        }

        if (contact) {
            ++r.contact_count;
            const double dd = static_cast<double>(dealt);
            dmg_sum += dd;
            dmg_sq += dd * dd;
            const int32_t di = static_cast<int32_t>(dealt);
            if (first_contact || di < r.damage_min) r.damage_min = di;
            if (first_contact || di > r.damage_max) r.damage_max = di;
            first_contact = false;

            // ours_basis: oracle arithmetic, our conventions (post-block
            // dealt damage, contact only, no rage on 0-damage full block).
            const int64_t ra = dealt > 0 ? rage_att_deci(dealt, hf) : 0;
            const int64_t rd = dealt > 0 ? rage_def_deci(dealt) : 0;
            ra_ours_sum += static_cast<double>(ra);
            ra_ours_sq += static_cast<double>(ra) * static_cast<double>(ra);
            rd_ours_sum += static_cast<double>(rd);
            rd_ours_sq += static_cast<double>(rd) * static_cast<double>(rd);
            ra_true_sum += static_cast<double>(rage_att_deci(clean_att, hf));
            rd_true_sum += static_cast<double>(rage_def_deci(clean_def));
        } else {
            // true oracle: attacker rage on every swing incl. miss/dodge/parry.
            ra_true_sum += static_cast<double>(rage_att_deci(clean_att, hf));
        }
    }

    const double nd = static_cast<double>(n);
    const auto sd_of = [](double sum, double sq, double count) {
        if (count < 2) return 0.0;
        const double mean = sum / count;
        const double var = (sq - mean * mean * count) / (count - 1.0);
        return var > 0 ? std::sqrt(var) : 0.0;
    };
    if (r.contact_count) {
        const double cc = static_cast<double>(r.contact_count);
        r.damage_mean = dmg_sum / cc;
        r.damage_sd = sd_of(dmg_sum, dmg_sq, cc);
    }
    // Rage means/sds are per swing over ALL n (avoided swings contribute 0
    // in the ours_basis), matching arena_dist's convention.
    r.rage_att_ours_mean = ra_ours_sum / nd;
    r.rage_att_ours_sd = sd_of(ra_ours_sum, ra_ours_sq, nd);
    r.rage_att_true_mean = ra_true_sum / nd;
    r.rage_def_ours_mean = rd_ours_sum / nd;
    r.rage_def_ours_sd = sd_of(rd_ours_sum, rd_ours_sq, nd);
    r.rage_def_true_mean = rd_true_sum / nd;
    return r;
}

} // namespace arena::oracle
