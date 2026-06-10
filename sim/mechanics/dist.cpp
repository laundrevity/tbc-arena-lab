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

#include "sim/mechanics/dist.h"

#include <cmath>
#include <vector>

#include "sim/core/fixed_trig.h"
#include "sim/mechanics/damage.h"
#include "sim/mechanics/swing.h"

namespace arena {

namespace {

int32_t histogram_quantile(const std::vector<uint64_t>& hist, uint64_t total, double q) {
    if (total == 0) return 0;
    const uint64_t target = static_cast<uint64_t>(std::ceil(q * static_cast<double>(total)));
    uint64_t acc = 0;
    for (size_t v = 0; v < hist.size(); ++v) {
        acc += hist[v];
        if (acc >= target) return static_cast<int32_t>(v);
    }
    return static_cast<int32_t>(hist.size() - 1);
}

} // namespace

DistReport run_distribution(const Scenario& sc, uint64_t seed, uint64_t n) {
    const UnitSpec& att = sc.attacker;
    const UnitSpec& def = sc.defender;

    DistReport rep;
    rep.n = n;
    rep.facing = mutual_frontal_arc(def.pos_x_cm, def.pos_y_cm, def.facing_mrad, att.pos_x_cm,
                                    att.pos_y_cm, att.facing_mrad)
                     ? FacingClass::Front
                     : FacingClass::Behind;
    rep.table = build_attack_table(att, def, rep.facing, true);

    // Damage upper bound: max weapon roll + AP bonus, crit-doubled.
    const int32_t max_damage =
        (att.weapon_max + ap_bonus(att.attack_power, att.weapon_speed_ms)) * MELEE_CRIT_MULT;
    std::vector<uint64_t> damage_hist(static_cast<size_t>(max_damage) + 1, 0);

    uint64_t outcome_counts[OUTCOME_COUNT] = {};
    uint64_t damage_sum = 0, damage_sq_sum = 0;
    int32_t damage_min = 0, damage_max = 0;
    bool first_contact = true;
    uint64_t rage_att_sum = 0, rage_def_sum = 0;
    uint64_t rage_att_sq_sum = 0, rage_def_sq_sum = 0;
    int32_t rage_att_min = 0, rage_att_max = 0;

    RngCursor cursor;
    for (uint64_t i = 0; i < n; ++i) {
        const SwingResult sw = resolve_swing(att, def, rep.facing, seed, cursor);
        ++outcome_counts[static_cast<int32_t>(sw.outcome)];
        rage_att_sum += static_cast<uint64_t>(sw.rage_attacker_deci);
        rage_def_sum += static_cast<uint64_t>(sw.rage_defender_deci);
        rage_att_sq_sum += static_cast<uint64_t>(sw.rage_attacker_deci) *
                           static_cast<uint64_t>(sw.rage_attacker_deci);
        rage_def_sq_sum += static_cast<uint64_t>(sw.rage_defender_deci) *
                           static_cast<uint64_t>(sw.rage_defender_deci);
        if (i == 0 || sw.rage_attacker_deci < rage_att_min) rage_att_min = sw.rage_attacker_deci;
        if (i == 0 || sw.rage_attacker_deci > rage_att_max) rage_att_max = sw.rage_attacker_deci;
        if (outcome_makes_contact(sw.outcome)) {
            ++rep.contact_count;
            damage_sum += static_cast<uint64_t>(sw.damage);
            damage_sq_sum +=
                static_cast<uint64_t>(sw.damage) * static_cast<uint64_t>(sw.damage);
            ++damage_hist[static_cast<size_t>(sw.damage)];
            if (first_contact || sw.damage < damage_min) damage_min = sw.damage;
            if (first_contact || sw.damage > damage_max) damage_max = sw.damage;
            first_contact = false;
        }
    }

    rep.all_pass = true;
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        OutcomeRow& row = rep.rows[static_cast<size_t>(i)];
        row.outcome = static_cast<Outcome>(i);
        row.expected_pm = rep.table.width[static_cast<size_t>(i)];
        row.count = outcome_counts[i];
        const double nd = static_cast<double>(n);
        row.expected_rate = static_cast<double>(row.expected_pm) / 10000.0;
        row.observed_rate = static_cast<double>(row.count) / nd;
        row.ci_half_width =
            DIST_Z99 * std::sqrt(row.expected_rate * (1.0 - row.expected_rate) / nd);
        // Zero-width ranges (glance/crush vs players, gated avoidance) must
        // never fire at all.
        row.pass = row.expected_pm == 0
                       ? row.count == 0
                       : std::fabs(row.observed_rate - row.expected_rate) <= row.ci_half_width;
        rep.all_pass = rep.all_pass && row.pass;
    }

    const auto sample_sd = [](uint64_t sum, uint64_t sq_sum, uint64_t count) {
        if (count < 2) return 0.0;
        const double mean = static_cast<double>(sum) / static_cast<double>(count);
        const double var = (static_cast<double>(sq_sum) -
                            mean * mean * static_cast<double>(count)) /
                           static_cast<double>(count - 1);
        return var > 0 ? std::sqrt(var) : 0.0;
    };

    rep.damage_min = damage_min;
    rep.damage_max = damage_max;
    rep.damage_mean = rep.contact_count
                          ? static_cast<double>(damage_sum) / static_cast<double>(rep.contact_count)
                          : 0.0;
    rep.damage_sd = sample_sd(damage_sum, damage_sq_sum, rep.contact_count);
    rep.damage_mean_per_swing = n ? static_cast<double>(damage_sum) / static_cast<double>(n) : 0.0;
    rep.damage_p50 = histogram_quantile(damage_hist, rep.contact_count, 0.50);
    rep.damage_p99 = histogram_quantile(damage_hist, rep.contact_count, 0.99);

    rep.rage_att_mean_deci = n ? static_cast<double>(rage_att_sum) / static_cast<double>(n) : 0.0;
    rep.rage_att_sd_deci = sample_sd(rage_att_sum, rage_att_sq_sum, n);
    rep.rage_att_min_deci = rage_att_min;
    rep.rage_att_max_deci = rage_att_max;
    rep.rage_def_mean_deci = n ? static_cast<double>(rage_def_sum) / static_cast<double>(n) : 0.0;
    rep.rage_def_sd_deci = sample_sd(rage_def_sum, rage_def_sq_sum, n);
    return rep;
}

YellowDistReport run_yellow_distribution(const Scenario& sc, uint64_t seed, uint64_t n,
                                         bool normalized, int32_t flat_bonus) {
    const UnitSpec& att = sc.attacker;
    const UnitSpec& def = sc.defender;

    YellowDistReport rep;
    rep.n = n;
    rep.normalized = normalized;
    rep.flat_bonus = flat_bonus;
    rep.facing = mutual_frontal_arc(def.pos_x_cm, def.pos_y_cm, def.facing_mrad, att.pos_x_cm,
                                    att.pos_y_cm, att.facing_mrad)
                     ? FacingClass::Front
                     : FacingClass::Behind;
    rep.table = build_yellow_table(att, def, rep.facing);
    rep.crit_pm = std::min(chance_crit_pm(att, def), 10000);
    const bool can_block = rep.facing == FacingClass::Front && def.has_shield;
    rep.block_pm = can_block ? std::min(chance_block_pm(att, def), 10000) : 0;

    uint64_t outcome_counts[4] = {};
    uint64_t crit_count = 0, block_count = 0;
    uint64_t dmg_sum = 0, dmg_sq = 0;
    uint64_t rd_sum = 0, rd_sq = 0;
    bool first_hit = true;

    RngCursor cursor;
    for (uint64_t i = 0; i < n; ++i) {
        const YellowResult yr =
            resolve_yellow(att, def, rep.facing, seed, cursor, normalized, flat_bonus);
        ++outcome_counts[static_cast<size_t>(yr.outcome)];
        rd_sum += static_cast<uint64_t>(yr.rage_defender_deci);
        rd_sq += static_cast<uint64_t>(yr.rage_defender_deci) *
                 static_cast<uint64_t>(yr.rage_defender_deci);
        if (yr.outcome != YellowOutcome::Hit) continue;
        ++rep.hit_count;
        if (yr.crit) ++crit_count;
        if (yr.blocked) ++block_count;
        dmg_sum += static_cast<uint64_t>(yr.damage);
        dmg_sq += static_cast<uint64_t>(yr.damage) * static_cast<uint64_t>(yr.damage);
        if (first_hit || yr.damage < rep.damage_min) rep.damage_min = yr.damage;
        if (first_hit || yr.damage > rep.damage_max) rep.damage_max = yr.damage;
        first_hit = false;
    }

    // Expected widths from the sim's own builder (self-test semantics, same
    // as the white path: validates RNG + sampling against the builder).
    const int32_t widths[4] = {
        rep.table.miss_end,
        rep.table.dodge_end - rep.table.miss_end,
        rep.table.parry_end - rep.table.dodge_end,
        10000 - rep.table.parry_end,
    };
    static const char* names[4] = {"miss", "dodge", "parry", "hit"};
    rep.all_pass = true;
    for (size_t i = 0; i < 4; ++i) {
        YellowRateRow& row = rep.rows[i];
        row.name = names[i];
        row.expected_pm = widths[i];
        row.trials = n;
        row.count = outcome_counts[i];
        const double nd = static_cast<double>(n);
        row.expected_rate = static_cast<double>(widths[i]) / 10000.0;
        row.observed_rate = static_cast<double>(row.count) / nd;
        row.ci_half_width =
            DIST_Z99 * std::sqrt(row.expected_rate * (1.0 - row.expected_rate) / nd);
        row.pass = row.expected_pm == 0
                       ? row.count == 0
                       : std::fabs(row.observed_rate - row.expected_rate) <= row.ci_half_width;
        rep.all_pass = rep.all_pass && row.pass;
    }
    const auto conditional_row = [&](size_t idx, const char* name, int32_t expected_pm,
                                     uint64_t count) {
        YellowRateRow& row = rep.rows[idx];
        row.name = name;
        row.expected_pm = expected_pm;
        row.trials = rep.hit_count;
        row.count = count;
        const double hd = static_cast<double>(rep.hit_count);
        row.expected_rate = static_cast<double>(expected_pm) / 10000.0;
        row.observed_rate = rep.hit_count ? static_cast<double>(count) / hd : 0.0;
        row.ci_half_width =
            rep.hit_count
                ? DIST_Z99 * std::sqrt(row.expected_rate * (1.0 - row.expected_rate) / hd)
                : 0.0;
        row.pass = expected_pm == 0
                       ? count == 0
                       : std::fabs(row.observed_rate - row.expected_rate) <= row.ci_half_width;
        rep.all_pass = rep.all_pass && row.pass;
    };
    conditional_row(4, "crit|hit", rep.crit_pm, crit_count);
    conditional_row(5, "block|hit", rep.block_pm, block_count);

    const auto sample_sd = [](uint64_t sum, uint64_t sq_sum, uint64_t count) {
        if (count < 2) return 0.0;
        const double mean = static_cast<double>(sum) / static_cast<double>(count);
        const double var =
            (static_cast<double>(sq_sum) - mean * mean * static_cast<double>(count)) /
            static_cast<double>(count - 1);
        return var > 0 ? std::sqrt(var) : 0.0;
    };
    rep.damage_mean = rep.hit_count
                          ? static_cast<double>(dmg_sum) / static_cast<double>(rep.hit_count)
                          : 0.0;
    rep.damage_sd = sample_sd(dmg_sum, dmg_sq, rep.hit_count);
    rep.rage_def_mean_deci = n ? static_cast<double>(rd_sum) / static_cast<double>(n) : 0.0;
    rep.rage_def_sd_deci = sample_sd(rd_sum, rd_sq, n);
    return rep;
}

} // namespace arena
