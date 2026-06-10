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
    rep.facing = in_frontal_arc(def.pos_x_cm, def.pos_y_cm, def.facing_mrad, att.pos_x_cm,
                                att.pos_y_cm)
                     ? FacingClass::Front
                     : FacingClass::Behind;
    rep.table = build_attack_table(att, def, rep.facing, true);

    // Damage upper bound: max weapon roll + AP bonus, crit-doubled.
    const int32_t max_damage =
        (att.weapon_max + ap_bonus(att.attack_power, att.weapon_speed_ms)) * MELEE_CRIT_MULT;
    std::vector<uint64_t> damage_hist(static_cast<size_t>(max_damage) + 1, 0);

    uint64_t outcome_counts[OUTCOME_COUNT] = {};
    uint64_t damage_sum = 0;
    int32_t damage_min = 0, damage_max = 0;
    bool first_contact = true;
    uint64_t rage_att_sum = 0, rage_def_sum = 0;
    int32_t rage_att_min = 0, rage_att_max = 0;

    RngCursor cursor;
    for (uint64_t i = 0; i < n; ++i) {
        const SwingResult sw = resolve_swing(att, def, rep.facing, seed, cursor);
        ++outcome_counts[static_cast<int32_t>(sw.outcome)];
        rage_att_sum += static_cast<uint64_t>(sw.rage_attacker_deci);
        rage_def_sum += static_cast<uint64_t>(sw.rage_defender_deci);
        if (i == 0 || sw.rage_attacker_deci < rage_att_min) rage_att_min = sw.rage_attacker_deci;
        if (i == 0 || sw.rage_attacker_deci > rage_att_max) rage_att_max = sw.rage_attacker_deci;
        if (outcome_makes_contact(sw.outcome)) {
            ++rep.contact_count;
            damage_sum += static_cast<uint64_t>(sw.damage);
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

    rep.damage_min = damage_min;
    rep.damage_max = damage_max;
    rep.damage_mean = rep.contact_count
                          ? static_cast<double>(damage_sum) / static_cast<double>(rep.contact_count)
                          : 0.0;
    rep.damage_mean_per_swing = n ? static_cast<double>(damage_sum) / static_cast<double>(n) : 0.0;
    rep.damage_p50 = histogram_quantile(damage_hist, rep.contact_count, 0.50);
    rep.damage_p99 = histogram_quantile(damage_hist, rep.contact_count, 0.99);

    rep.rage_att_mean_deci = n ? static_cast<double>(rage_att_sum) / static_cast<double>(n) : 0.0;
    rep.rage_att_min_deci = rage_att_min;
    rep.rage_att_max_deci = rage_att_max;
    rep.rage_def_mean_deci = n ? static_cast<double>(rage_def_sum) / static_cast<double>(n) : 0.0;
    return rep;
}

} // namespace arena
