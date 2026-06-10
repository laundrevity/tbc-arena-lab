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

#include "sim/mechanics/abilities.h"
#include "sim/mechanics/match.h"

using namespace arena;

namespace {

// 2H attacker with fully deterministic combat math: no miss (hit cancels
// base 5%), no crit, fixed weapon roll 400.
UnitSpec det_attacker() {
    UnitSpec a;
    a.entity_id = 1;
    a.level = 70;
    a.weapon_skill = 350;
    a.defense_skill = 350;
    a.attack_power = 2800;
    a.crit_pm = 0;
    a.hit_pm = 500;
    a.weapon_min = 400;
    a.weapon_max = 400;
    a.weapon_speed_ms = 3600;
    a.weapon_norm_ms = 3300;
    a.armor = 6200;
    a.has_shield = false;
    a.attacks = true;
    a.max_hp = 25000;
    a.pos_x_cm = 200;
    a.pos_y_cm = 0;
    a.facing_mrad = 3142;
    return a;
}

// Idle defender with no avoidance and no shield.
UnitSpec det_defender() {
    UnitSpec d = det_attacker();
    d.entity_id = 2;
    d.hit_pm = 0;
    d.attacks = false;
    d.pos_x_cm = 0;
    d.facing_mrad = 0;
    return d;
}

Scenario make_scenario(const UnitSpec& a, const UnitSpec& d, int64_t duration_ms) {
    Scenario sc;
    sc.name = "abilities_test";
    sc.ruleset = "anniversary-tbc-2.5.x";
    sc.duration_ms = duration_ms;
    sc.attacker = a;
    sc.defender = d;
    return sc;
}

} // namespace

// Spec M-012: avoidance die has no block/glance/crush sides; facing-gated.
TEST_CASE("abilities: yellow avoidance die widths") {
    UnitSpec a = det_attacker();
    a.crit_pm = 2500;
    a.hit_pm = 0;
    UnitSpec d = det_defender();
    d.dodge_pm = 800;
    d.parry_pm = 600;
    d.block_pm = 500;     // must NOT appear in the die
    d.has_shield = true;  // even with a shield

    YellowTable t = build_yellow_table(a, d, FacingClass::Front);
    CHECK(t.miss_end == 500);
    CHECK(t.dodge_end == 1300);
    CHECK(t.parry_end == 1900);
    CHECK(t.classify(0) == YellowOutcome::Miss);
    CHECK(t.classify(499) == YellowOutcome::Miss);
    CHECK(t.classify(500) == YellowOutcome::Dodge);
    CHECK(t.classify(1299) == YellowOutcome::Dodge);
    CHECK(t.classify(1300) == YellowOutcome::Parry);
    CHECK(t.classify(1899) == YellowOutcome::Parry);
    CHECK(t.classify(1900) == YellowOutcome::Hit);
    CHECK(t.classify(9999) == YellowOutcome::Hit);

    t = build_yellow_table(a, d, FacingClass::Behind);
    CHECK(t.miss_end == 500);
    CHECK(t.dodge_end == 500);  // dodge/parry facing-gated (M-008)
    CHECK(t.parry_end == 500);
}

// Specs M-012/M-013: yellow pipeline order crit -> armor -> block, with
// normalized (MS) and real (HS) AP speeds.
TEST_CASE("abilities: yellow damage pipeline at fixed inputs") {
    const UnitSpec a = det_attacker();
    UnitSpec d = det_defender();
    d.block_value = 180;
    CHECK(ap_bonus(2800, 3300) == 660);  // normalized 2H (M-013)
    // MS (normalized, +210): 210+400+660 = 1270 -> armor -> 800.
    CHECK(resolve_yellow_damage(400, false, false, a, d, true, MS_FLAT_BONUS) == 800);
    // Crit BEFORE armor (D-020): 2540 -> armor -> 1600.
    CHECK(resolve_yellow_damage(400, true, false, a, d, true, MS_FLAT_BONUS) == 1600);
    // Block value after armor, on crits too (blocked crits exist for yellows).
    CHECK(resolve_yellow_damage(400, true, true, a, d, true, MS_FLAT_BONUS) == 1420);
    CHECK(resolve_yellow_damage(400, false, true, a, d, true, MS_FLAT_BONUS) == 620);
    // HS (real weapon speed, +176): 176+400+720 = 1296 -> armor -> 816.
    CHECK(resolve_yellow_damage(400, false, false, a, d, false, HS_FLAT_BONUS) == 816);
}

// Specs M-011/M-014/M-016: full cost on avoided casts, 6 s cooldown spacing
// via Decide wake-ups. Defender dodges everything, so rage only ever drops.
TEST_CASE("abilities: Mortal Strike costs full rage on dodge and respects cooldown") {
    UnitSpec a = det_attacker();
    a.use_mortal_strike = true;
    a.initial_rage_deci = 1000;
    UnitSpec d = det_defender();
    d.dodge_pm = 10000;  // dodges whites and yellows alike

    CollectSink s;
    run_match(make_scenario(a, d, 15000), 11, s);

    REQUIRE(s.abilities.size() == 3);
    const std::vector<int64_t> expect_t = {0, 6000, 12000};
    const std::vector<int32_t> expect_rage = {700, 400, 100};
    for (size_t i = 0; i < 3; ++i) {
        CHECK(s.abilities[i].t == expect_t[i]);
        CHECK(s.abilities[i].result.outcome == YellowOutcome::Dodge);
        CHECK(s.abilities[i].result.damage == 0);
        CHECK(s.abilities[i].src_rage_deci_after == expect_rage[i]);
        CHECK(s.abilities[i].tgt_rage_deci_after == 0);  // no damage, no rage
    }
    // Dodged white swings grant no rage either, so no fourth cast at rage 100.
    for (const SwingRecord& r : s.swings) CHECK(r.result.outcome == Outcome::Dodge);
}

// Specs M-014/M-016/D-019: deterministic MS hit; victim gains rage from
// ability damage, attacker gains none beyond paying the cost.
TEST_CASE("abilities: Mortal Strike hit damage and victim rage at fixed inputs") {
    UnitSpec a = det_attacker();
    a.use_mortal_strike = true;
    a.initial_rage_deci = 1000;
    const UnitSpec d = det_defender();

    CollectSink s;
    run_match(make_scenario(a, d, 1000), 5, s);

    // t=0: white swing first (event order), then the MS cast.
    REQUIRE(!s.swings.empty());
    const SwingRecord& w = s.swings[0];
    CHECK(w.t == 0);
    CHECK(w.result.damage == 705);          // 400+720 -> armor
    CHECK(w.src_rage_deci_after == 1000);   // capped
    CHECK(w.tgt_rage_deci_after == 64);

    REQUIRE(!s.abilities.empty());
    const AbilityRecord& ms = s.abilities[0];
    CHECK(ms.t == 0);
    CHECK(std::string(ms.ability) == "mortal_strike");
    CHECK(ms.result.outcome == YellowOutcome::Hit);
    CHECK(!ms.result.crit);
    CHECK(!ms.result.blocked);
    CHECK(ms.result.damage == 800);          // 210+400+660 -> armor
    CHECK(ms.src_rage_deci_after == 700);    // 1000 - 300, no gain from spells
    CHECK(ms.tgt_rage_deci_after == 64 + 72);  // victim rage from ability (D-019)
    CHECK(ms.tgt_hp_after == 25000 - 705 - 800);
}

// Spec M-015: queue, replace-the-swing, and re-queue.
TEST_CASE("abilities: Heroic Strike replaces the next swing when payable") {
    UnitSpec a = det_attacker();
    a.hs_min_rage_deci = 200;
    a.initial_rage_deci = 300;
    const UnitSpec d = det_defender();

    CollectSink s;
    run_match(make_scenario(a, d, 15000), 3, s);

    // t=0 white (rage 300+156=456, HS queues after), t=3600 HS (456-150=306,
    // re-queues), t=7200 HS (306-150=156 < 200, no re-queue), t=10800 white
    // (156+156=312, re-queues), t=14400 HS (312-150=162).
    std::vector<int64_t> swing_t, ability_t;
    for (const SwingRecord& r : s.swings) swing_t.push_back(r.t);
    for (const AbilityRecord& r : s.abilities) ability_t.push_back(r.t);
    CHECK(swing_t == std::vector<int64_t>{0, 10800});
    CHECK(ability_t == std::vector<int64_t>{3600, 7200, 14400});
    for (const AbilityRecord& r : s.abilities) {
        CHECK(std::string(r.ability) == "heroic_strike");
        CHECK(r.result.outcome == YellowOutcome::Hit);
        CHECK(r.result.damage == 816);  // 176+400+720 -> armor (real speed)
    }
    CHECK(s.abilities[0].src_rage_deci_after == 306);
    CHECK(s.abilities[1].src_rage_deci_after == 156);
    CHECK(s.abilities[2].src_rage_deci_after == 162);
}

// Spec M-015: insufficient rage at swing time clears the queue and falls
// back to a white swing.
TEST_CASE("abilities: Heroic Strike falls back to white when rage is short") {
    UnitSpec a = det_attacker();
    a.use_mortal_strike = true;
    a.hs_min_rage_deci = 200;
    a.initial_rage_deci = 200;
    const UnitSpec d = det_defender();

    CollectSink s;
    run_match(make_scenario(a, d, 8000), 9, s);

    // t=0 white (rage 356), HS queues, MS casts (rage 56).
    // t=3600: HS queued but 56 < 150 -> fallback WHITE swing (rage 212).
    // t=7200: HS fires (212-150=62).
    std::vector<int64_t> swing_t, ability_t;
    for (const SwingRecord& r : s.swings) swing_t.push_back(r.t);
    for (const AbilityRecord& r : s.abilities) ability_t.push_back(r.t);
    CHECK(swing_t == std::vector<int64_t>{0, 3600});
    REQUIRE(ability_t.size() == 2);
    CHECK(ability_t[0] == 0);     // mortal strike
    CHECK(ability_t[1] == 7200);  // heroic strike after rage recovers
    CHECK(std::string(s.abilities[0].ability) == "mortal_strike");
    CHECK(std::string(s.abilities[1].ability) == "heroic_strike");
    CHECK(s.abilities[1].src_rage_deci_after == 62);
}
