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

// run_match drives MatchEngine internally; an external driver applying the
// same ScriptedPolicy decisions through step() must reproduce the native
// path bit-exactly (this is the contract the C API / Python wrapper rely on).
TEST_CASE("engine: externally driven steps match run_match bit-exactly") {
    const Scenario sc = load("m2_duel.yaml");

    CollectSink native;
    const MatchResult rn = run_match(sc, 777, native);

    CollectSink driven;
    MatchEngine<CollectSink> eng(sc, 777, driven, /*enable_ticks=*/true);
    ScriptedPolicy p0(sc.attacker.scripted_hs_min_rage_deci);
    ScriptedPolicy p1(sc.defender.scripted_hs_min_rage_deci);
    while (eng.status() == EngineStatus::AwaitingActions) {
        eng.step(p0.decide(eng.observe(0)), p1.decide(eng.observe(1)));
    }
    const MatchResult& rd = eng.result();

    CHECK(rn.end_ms == rd.end_ms);
    CHECK(std::string(rn.end_reason) == rd.end_reason);
    CHECK(rn.swings == rd.swings);
    CHECK(rn.abilities == rd.abilities);
    CHECK(rn.decisions == rd.decisions);
    CHECK(rn.initial_hash == rd.initial_hash);
    CHECK(native.checkpoints == driven.checkpoints);  // full state trajectory
    REQUIRE(native.decisions.size() == driven.decisions.size());
    for (size_t i = 0; i < native.decisions.size(); ++i) {
        CHECK(native.decisions[i].t == driven.decisions[i].t);
        CHECK(native.decisions[i].action == driven.decisions[i].action);
    }
}

TEST_CASE("engine: step semantics — pauses at ticks, idempotent after end") {
    Scenario sc = load("m2_duel.yaml");
    sc.duration_ms = 1000;
    NullSink sink;
    MatchEngine<NullSink> eng(sc, 1, sink, /*enable_ticks=*/true);

    // First pause is tick t=0; observations are stable across repeated reads.
    REQUIRE(eng.status() == EngineStatus::AwaitingActions);
    CHECK(eng.now() == 0);
    const Observation a = eng.observe(0);
    const Observation b = eng.observe(0);
    CHECK(a.t_ms == b.t_ms);
    CHECK(a.self_rage_deci == b.self_rage_deci);

    // 100 ms ticks within a 1000 ms match: pauses at 0,100,...,900 then end.
    int ticks = 1;
    while (eng.step(Action::None, Action::None) == EngineStatus::AwaitingActions) ++ticks;
    CHECK(ticks == 10);
    CHECK(eng.status() == EngineStatus::Ended);
    CHECK(eng.result().end_ms == 1000);
    // Stepping a finished engine is a harmless no-op.
    CHECK(eng.step(Action::None, Action::None) == EngineStatus::Ended);
    CHECK(eng.result().end_ms == 1000);
    // Terminal observation reads at end time.
    CHECK(eng.observe(0).t_ms == 1000);
}

TEST_CASE("engine: tick-free mode never pauses (pure auto-attack)") {
    const Scenario sc = load("m0_front_shield.yaml");
    NullSink sink;
    MatchEngine<NullSink> eng(sc, 42, sink, /*enable_ticks=*/false);
    CHECK(eng.status() == EngineStatus::Ended);  // ran to completion at once
    CHECK(eng.result().end_ms == 60000);
    CHECK(eng.result().decisions == 0);
}
