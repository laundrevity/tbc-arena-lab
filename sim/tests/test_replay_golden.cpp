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

#include "sim/mechanics/replay.h"

using namespace arena;

// Golden traces are committed under sim/tests/golden_traces/ and only
// regenerated with an explanatory commit naming the formula change
// (CLAUDE.md testing rule 5). A failure here means either nondeterminism or
// an undeclared mechanics change.
TEST_CASE("replay: golden traces re-verify all hash checkpoints") {
    for (const char* name : {"m0_front_shield.jsonl", "m0_behind.jsonl", "m1_mutual.jsonl",
                             "m2_duel.jsonl"}) {
        CAPTURE(name);
        const ReplayResult r = verify_trace_file(
            std::string(REPO_ROOT) + "/sim/tests/golden_traces/" + name, REPO_ROOT);
        CHECK_MESSAGE(r.ok, r.error);
        CHECK(r.checkpoints_checked > 0);  // m2 may legitimately end by death
        CHECK(r.swings_in_trace > 0);
    }
}

TEST_CASE("replay: a tampered trace fails verification") {
    // Nonexistent file is the cheap tamper-proxy; full tamper coverage comes
    // with the differential harness session.
    const ReplayResult r = verify_trace_file("/nonexistent/trace.jsonl", REPO_ROOT);
    CHECK(!r.ok);
    CHECK(!r.error.empty());
}
