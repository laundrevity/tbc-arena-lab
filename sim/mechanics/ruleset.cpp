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

#include "sim/mechanics/ruleset.h"

#include "sim/core/fixed_trig.h"
#include "sim/core/fnv1a.h"
#include "sim/mechanics/damage.h"
#include "sim/mechanics/rage.h"

namespace arena {

const char* ruleset_id() { return "anniversary-tbc-2.5.x"; }

std::string ruleset_manifest() {
    std::string m;
    m += "ruleset=";
    m += ruleset_id();
    m += ";table_order=miss,dodge,parry,block,glance,crit,crush,hit";
    m += ";roll_granularity=10000";
    m += ";base_miss_pm=500";
    m += ";skill_delta_pm_per_point=4";
    m += ";crit_skill_term=level_capped";  // spec M-001, cmangos Unit.cpp:3957
    m += ";glance_vs_players=0;crush_vs_players=0";
    m += ";crit_mult=" + std::to_string(MELEE_CRIT_MULT);
    m += ";ap_per_dps=14";
    m += ";armor_k2_l70=" + std::to_string(ARMOR_K2_L70);
    m += ";armor_kept_min_pct=" + std::to_string(ARMOR_KEPT_MIN_PCT);
    m += ";armor_min_damage=1";
    m += ";rage_c10_l70=" + std::to_string(RAGE_C10_L70);
    m += ";rage_f2_hit=" + std::to_string(RAGE_F2_HIT);
    m += ";rage_f2_crit=" + std::to_string(RAGE_F2_CRIT);
    m += ";rage_hf=floor_f_times_speed_then_halve";  // spec M-006
    m += ";rage_cap_deci=" + std::to_string(RAGE_CAP_DECI);
    m += ";rage_on_avoid=none";  // ledger D-011/D-012
    m += ";parry_haste=p20_floor_div5_windows_20_60";  // spec M-010
    m += ";frontal_arc=mutual_pi;pi_mrad=" + std::to_string(PI_MRAD);
    return m;
}

uint64_t ruleset_hash() { return fnv1a(ruleset_manifest()); }

} // namespace arena
