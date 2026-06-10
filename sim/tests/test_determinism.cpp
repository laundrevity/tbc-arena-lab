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

#include "sim/core/scenario.h"
#include "sim/mechanics/match.h"

using namespace arena;

namespace {

Scenario load(const char* name) {
    Scenario sc;
    std::string err;
    const bool ok = load_scenario(std::string(REPO_ROOT) + "/scenarios/" + name, sc, err);
    REQUIRE_MESSAGE(ok, err);
    return sc;
}

bool identical(const CollectSink& a, const CollectSink& b) {
    if (a.checkpoints != b.checkpoints) return false;
    if (a.end_t != b.end_t || a.end_reason != b.end_reason) return false;
    if (a.abilities.size() != b.abilities.size()) return false;
    for (size_t i = 0; i < a.abilities.size(); ++i) {
        const AbilityRecord& x = a.abilities[i];
        const AbilityRecord& y = b.abilities[i];
        if (x.t != y.t || x.src != y.src || std::string(x.ability) != y.ability ||
            x.result.outcome != y.result.outcome || x.result.crit != y.result.crit ||
            x.result.blocked != y.result.blocked || x.result.damage != y.result.damage ||
            x.src_rage_deci_after != y.src_rage_deci_after ||
            x.tgt_rage_deci_after != y.tgt_rage_deci_after || x.tgt_hp_after != y.tgt_hp_after) {
            return false;
        }
    }
    if (a.swings.size() != b.swings.size()) return false;
    for (size_t i = 0; i < a.swings.size(); ++i) {
        const SwingRecord& x = a.swings[i];
        const SwingRecord& y = b.swings[i];
        if (x.t != y.t || x.seq != y.seq || x.src != y.src || x.tgt != y.tgt ||
            x.result.outcome != y.result.outcome || x.result.roll_pm != y.result.roll_pm ||
            x.result.damage != y.result.damage ||
            x.src_rage_deci_after != y.src_rage_deci_after ||
            x.tgt_rage_deci_after != y.tgt_rage_deci_after || x.tgt_hp_after != y.tgt_hp_after) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("determinism: same scenario + seed twice produces identical traces") {
    for (const char* name :
         {"m0_front_shield.yaml", "m0_behind.yaml", "m1_mutual.yaml", "m2_duel.yaml"}) {
        CAPTURE(name);
        const Scenario sc = load(name);
        CollectSink a, b;
        const MatchResult ra = run_match(sc, 12345, a);
        const MatchResult rb = run_match(sc, 12345, b);
        CHECK(ra.initial_hash == rb.initial_hash);
        CHECK(ra.end_ms == rb.end_ms);
        CHECK(ra.swings == rb.swings);
        CHECK(identical(a, b));
        CHECK(!a.checkpoints.empty());
        CHECK(!a.swings.empty());
    }
}

TEST_CASE("determinism: different seeds diverge (sanity, not a guarantee)") {
    const Scenario sc = load("m0_front_shield.yaml");
    CollectSink a, b;
    run_match(sc, 1, a);
    run_match(sc, 2, b);
    CHECK(!identical(a, b));
}

TEST_CASE("match: death ends the match and the idle warrior never swings") {
    Scenario sc = load("m0_front_shield.yaml");
    sc.duration_ms = 1000 * 1000;  // long enough for 25000 hp to fall
    CollectSink s;
    const MatchResult r = run_match(sc, 777, s);
    CHECK(std::string(r.end_reason) == "death");
    CHECK(r.end_ms < sc.duration_ms);
    CHECK(s.swings.back().tgt_hp_after == 0);
    for (const SwingRecord& rec : s.swings) {
        CHECK(rec.src == sc.attacker.entity_id);  // idle defender never swings
        CHECK(rec.tgt == sc.defender.entity_id);
    }
    // No events after the death timestamp.
    CHECK(s.checkpoints.back().first <= r.end_ms);
}

TEST_CASE("match: checkpoints fire every authoritative 1000 ms") {
    const Scenario sc = load("m0_behind.yaml");  // 60000 ms duration
    CollectSink s;
    run_match(sc, 5, s);
    REQUIRE(s.checkpoints.size() == 60);
    for (size_t i = 0; i < s.checkpoints.size(); ++i) {
        CHECK(s.checkpoints[i].first == static_cast<int64_t>(i + 1) * 1000);
    }
    CHECK(s.end_t == 60000);
    CHECK(s.end_reason == "duration");
}
