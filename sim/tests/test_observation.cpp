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

#include <string>

#include "sim/mechanics/match.h"
#include "sim/mechanics/observation.h"
#include "sim/mechanics/policy.h"

using namespace arena;

namespace {

Scenario load(const char* name) {
    Scenario sc;
    std::string err;
    const bool ok = load_scenario(std::string(REPO_ROOT) + "/scenarios/" + name, sc, err);
    REQUIRE_MESSAGE(ok, err);
    return sc;
}

} // namespace

// docs/observation_action_spec.md field semantics at fixed inputs.
TEST_CASE("observation: fields derive from authoritative state, client-parity") {
    UnitSpec self;
    self.entity_id = 1;
    self.level = 70;
    self.weapon_speed_ms = 3600;
    self.max_hp = 25000;
    self.knows_mortal_strike = true;
    self.knows_heroic_strike = true;
    self.attacks = true;
    UnitSpec enemy = self;
    enemy.entity_id = 2;
    enemy.knows_mortal_strike = false;

    UnitState self_st;
    self_st.pos_x_cm = 200;
    self_st.pos_y_cm = 0;
    self_st.facing_mrad = 3142;
    self_st.hp = 20000;
    self_st.rage_deci = 450;
    self_st.next_swing_ms = 3600;
    self_st.gcd_ready_ms = 1500;
    self_st.ms_ready_ms = 6000;
    self_st.hs_queued = 1;
    UnitState enemy_st = self_st;
    enemy_st.pos_x_cm = 0;
    enemy_st.facing_mrad = 0;
    enemy_st.hp = 12500;
    enemy_st.rage_deci = 777;

    const Observation o = build_observation(self, self_st, enemy, enemy_st, 500, 60000);
    CHECK(o.t_ms == 500);
    CHECK(o.match_remaining_ms == 59500);
    CHECK(o.self_hp == 20000);
    CHECK(o.self_max_hp == 25000);
    CHECK(o.self_rage_deci == 450);
    CHECK(o.self_gcd_remaining_ms == 1000);
    CHECK(o.self_ms_cd_remaining_ms == 5500);
    CHECK(o.self_swing_remaining_ms == 3100);
    CHECK(o.self_hs_queued == 1);
    CHECK(o.self_knows_ms == 1);
    CHECK(o.self_knows_hs == 1);
    CHECK(o.self_weapon_speed_ms == 3600);
    CHECK(o.enemy_hp_pm == 5000);     // fraction only, never absolute
    CHECK(o.enemy_rage_deci == 777);  // target resource bar is visible
    CHECK(o.enemy_in_self_front == 1);
    CHECK(o.self_in_enemy_front == 1);
    CHECK(o.distance_cm == 200);

    // Enemy perspective: MS unknown reads -1 (not a hidden zero).
    const Observation e = build_observation(enemy, enemy_st, self, self_st, 500, 60000);
    CHECK(e.self_knows_ms == 0);
    CHECK(e.self_ms_cd_remaining_ms == -1);

    // Canonical serialization: 6 i64 + 11 i32 fields = 92 bytes.
    std::vector<uint8_t> buf;
    o.serialize(buf);
    CHECK(buf.size() == 92);
}

TEST_CASE("observation: legal action mask from observation alone") {
    Observation o;
    o.self_knows_ms = 1;
    o.self_knows_hs = 1;
    o.self_rage_deci = 300;
    o.self_ms_cd_remaining_ms = 0;
    o.self_gcd_remaining_ms = 0;
    o.self_hs_queued = 0;
    CHECK(legal_action_mask(o) ==
          (action_bit(Action::None) | action_bit(Action::CastMortalStrike) |
           action_bit(Action::QueueHeroicStrike)));

    o.self_rage_deci = 299;  // one deci short of MS
    CHECK((legal_action_mask(o) & action_bit(Action::CastMortalStrike)) == 0);
    o.self_rage_deci = 300;
    o.self_gcd_remaining_ms = 1;  // GCD gates
    CHECK((legal_action_mask(o) & action_bit(Action::CastMortalStrike)) == 0);
    o.self_gcd_remaining_ms = 0;
    o.self_ms_cd_remaining_ms = 1;  // cooldown gates
    CHECK((legal_action_mask(o) & action_bit(Action::CastMortalStrike)) == 0);
    o.self_ms_cd_remaining_ms = 0;
    o.self_knows_ms = 0;  // loadout gates
    CHECK((legal_action_mask(o) & action_bit(Action::CastMortalStrike)) == 0);

    o.self_hs_queued = 1;  // queued: unqueue legal, queue not
    CHECK((legal_action_mask(o) & action_bit(Action::QueueHeroicStrike)) == 0);
    CHECK((legal_action_mask(o) & action_bit(Action::UnqueueHeroicStrike)) != 0);
    o.self_knows_hs = 0;
    CHECK(legal_action_mask(o) == action_bit(Action::None));
}

// Spec "Tracing and replay": recorded decisions played back through
// PlaybackPolicy reproduce the match exactly.
TEST_CASE("playback: recorded m2_duel decisions reproduce the match bit-exactly") {
    const Scenario sc = load("m2_duel.yaml");
    CollectSink a;
    const MatchResult ra = run_match(sc, 4242, a);
    REQUIRE(!a.decisions.empty());

    PlaybackPolicy p[2];
    for (const DecisionRecord& r : a.decisions) {
        p[r.unit == sc.attacker.entity_id ? 0 : 1].script[r.t] = r.action;
    }
    // Slot order is ascending entity_id (attacker is 1 in this fixture).
    CollectSink b;
    const MatchResult rb = run_match(sc, 4242, b, &p[0], &p[1]);

    CHECK(ra.end_ms == rb.end_ms);
    CHECK(std::string(ra.end_reason) == rb.end_reason);
    CHECK(ra.swings == rb.swings);
    CHECK(ra.abilities == rb.abilities);
    CHECK(ra.decisions == rb.decisions);
    CHECK(rb.illegal_actions == 0);
    CHECK(a.checkpoints == b.checkpoints);  // bit-exact state trajectory
    REQUIRE(a.decisions.size() == b.decisions.size());
    for (size_t i = 0; i < a.decisions.size(); ++i) {
        CHECK(a.decisions[i].t == b.decisions[i].t);
        CHECK(a.decisions[i].unit == b.decisions[i].unit);
        CHECK(a.decisions[i].action == b.decisions[i].action);
    }
}

// Illegal submissions degrade to None and are counted, not crashed on.
TEST_CASE("actions: illegal submission counts and does nothing") {
    Scenario sc = load("m2_duel.yaml");
    sc.duration_ms = 1000;
    PlaybackPolicy bad0, bad1;
    bad0.script[0] = Action::CastMortalStrike;    // rage 0 at t=0: illegal
    bad1.script[0] = Action::UnqueueHeroicStrike; // nothing queued: illegal
    CollectSink s;
    const MatchResult r = run_match(sc, 1, s, &bad0, &bad1);
    CHECK(r.illegal_actions == 2);
    CHECK(r.decisions == 0);
    CHECK(s.decisions.empty());
    CHECK(s.abilities.empty());
}
