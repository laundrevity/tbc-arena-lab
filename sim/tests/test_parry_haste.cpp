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

#include <vector>

#include "sim/mechanics/match.h"
#include "sim/mechanics/parry_haste.h"

using namespace arena;

// Spec M-010 window boundaries (p20 = floor(S/5), p60 = 3*p20).
TEST_CASE("parry haste: retime windows at fixed inputs") {
    // S = 3600: p20 = 720, p60 = 2160.
    CHECK(parry_hastened_remaining(3600, 3600) == 2160);  // > p60: -40%
    CHECK(parry_hastened_remaining(2161, 3600) == 721);
    CHECK(parry_hastened_remaining(2160, 3600) == 720);   // (p20, p60]: clamp to p20
    CHECK(parry_hastened_remaining(721, 3600) == 720);
    CHECK(parry_hastened_remaining(720, 3600) == 720);    // <= p20: unchanged
    CHECK(parry_hastened_remaining(0, 3600) == 0);
    // S = 2600: p20 = 520, p60 = 1560.
    CHECK(parry_hastened_remaining(1600, 2600) == 560);
    CHECK(parry_hastened_remaining(1560, 2600) == 520);
    CHECK(parry_hastened_remaining(521, 2600) == 520);
    CHECK(parry_hastened_remaining(100, 2600) == 100);
}

namespace {

// Both units attack; unit 2 parries EVERY swing from unit 1 (parry fills the
// whole table), and unit 1 avoids nothing, so unit 2's timeline is the
// hand-computed hastened sequence below.
Scenario forced_parry_scenario() {
    Scenario sc;
    sc.name = "forced_parry";
    sc.ruleset = "anniversary-tbc-2.5.x";
    sc.duration_ms = 20000;

    UnitSpec a;
    a.entity_id = 1;
    a.level = 70;
    a.weapon_skill = 350;
    a.defense_skill = 350;
    a.attack_power = 0;
    a.crit_pm = 0;
    a.hit_pm = 500;  // cancels base miss so parry can fill the table
    a.weapon_min = 10;
    a.weapon_max = 20;
    a.weapon_speed_ms = 3600;
    a.armor = 0;
    a.dodge_pm = 0;
    a.parry_pm = 0;  // unit 1 never parries, so its own timer is untouched
    a.block_pm = 0;
    a.block_value = 0;
    a.has_shield = false;
    a.attacks = true;
    a.max_hp = 1000000;
    a.pos_x_cm = 200;
    a.pos_y_cm = 0;
    a.facing_mrad = 3142;

    UnitSpec b = a;
    b.entity_id = 2;
    b.hit_pm = 0;
    b.parry_pm = 10000;  // parries everything unit 1 throws
    b.weapon_speed_ms = 2600;
    b.pos_x_cm = 0;
    b.facing_mrad = 0;

    sc.attacker = a;
    sc.defender = b;
    return sc;
}

} // namespace

TEST_CASE("parry haste: forced-parry match pins the hastened swing timeline") {
    const Scenario sc = forced_parry_scenario();
    CollectSink s;
    const MatchResult r = run_match(sc, 4242, s);
    CHECK(r.end_ms == 20000);

    std::vector<int64_t> a_times, b_times;
    for (const SwingRecord& rec : s.swings) {
        if (rec.src == 1) {
            a_times.push_back(rec.t);
            // Every unit-1 swing is parried.
            CHECK(rec.result.outcome == Outcome::Parry);
            CHECK(rec.result.damage == 0);
        } else {
            b_times.push_back(rec.t);
        }
    }

    // Unit 1 (3600 speed, never hastened): plain cadence.
    const std::vector<int64_t> a_expected = {0, 3600, 7200, 10800, 14400, 18000};
    CHECK(a_times == a_expected);

    // Unit 2 (2600 speed, hastened by each parry; p20=520, p60=1560).
    // Hand-derived (spec M-010):
    //  t=0     parry at t=0 leaves remaining 0 unchanged; swings at 0, next 2600
    //  t=2600  swing, next 5200
    //  t=3600  parry: remaining 1600 > 1560 -> 560, next 4160
    //  t=4160  swing, next 6760
    //  t=6760  swing, next 9360
    //  t=7200  parry: remaining 2160 -> 1120, next 8320
    //  t=8320  swing, next 10920
    //  t=10800 parry: remaining 120 <= 520, unchanged
    //  t=10920 swing, next 13520
    //  t=13520 swing, next 16120
    //  t=14400 parry: remaining 1720 -> 680, next 15080
    //  t=15080 swing, next 17680
    //  t=17680 swing, next 20280
    //  t=18000 parry: remaining 2280 -> 1240, next 19240
    //  t=19240 swing, next 21840 (past duration)
    const std::vector<int64_t> b_expected = {0,    2600,  4160,  6760,  8320,
                                             10920, 13520, 15080, 17680, 19240};
    CHECK(b_times == b_expected);
}

TEST_CASE("parry haste: superseded swing events are skipped without trace or RNG effects") {
    // Determinism through retiming: run the forced-parry scenario twice and
    // require identical traces (stale events consuming RNG or emitting
    // records would break this and the timeline test above).
    const Scenario sc = forced_parry_scenario();
    CollectSink x, y;
    const MatchResult rx = run_match(sc, 7, x);
    const MatchResult ry = run_match(sc, 7, y);
    CHECK(rx.swings == ry.swings);
    CHECK(x.checkpoints == y.checkpoints);
    REQUIRE(x.swings.size() == y.swings.size());
    for (size_t i = 0; i < x.swings.size(); ++i) {
        CHECK(x.swings[i].t == y.swings[i].t);
        CHECK(x.swings[i].result.roll_pm == y.swings[i].result.roll_pm);
    }
}

TEST_CASE("match: death of either unit ends an m1 mutual match") {
    Scenario sc;
    std::string err;
    REQUIRE_MESSAGE(load_scenario(std::string(REPO_ROOT) + "/scenarios/m1_mutual.yaml", sc, err),
                    err);
    sc.duration_ms = 1000 * 1000;
    CollectSink s;
    const MatchResult r = run_match(sc, 31337, s);
    CHECK(std::string(r.end_reason) == "death");
    CHECK(r.end_ms < sc.duration_ms);
    CHECK(s.swings.back().tgt_hp_after == 0);
    // Both units actually swung (mutual combat, not M0 idle-defender).
    bool saw1 = false, saw2 = false;
    for (const SwingRecord& rec : s.swings) {
        saw1 = saw1 || rec.src == 1;
        saw2 = saw2 || rec.src == 2;
    }
    CHECK(saw1);
    CHECK(saw2);
}
