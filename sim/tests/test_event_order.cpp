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

#include "sim/core/event_queue.h"

using namespace arena;

// The documented simultaneous-event total order:
// (time_ms, priority, source_id, target_id, seq) — CLAUDE.md rule 3.
TEST_CASE("event queue: time dominates, then priority, source, target, seq") {
    EventQueue q(16);
    q.push(2000, EventKind::Checkpoint, 0, 0);   // later time, highest priority
    q.push(1000, EventKind::Swing, 2, 1);        // same time, higher source
    q.push(1000, EventKind::Swing, 1, 2);        // same time, lower source
    q.push(1000, EventKind::MatchEnd, 0, 0);     // same time, MatchEnd < Swing
    q.push(1000, EventKind::Checkpoint, 0, 0);   // same time, Checkpoint first

    Event e = q.pop();
    CHECK(e.time_ms == 1000);
    CHECK(e.kind == EventKind::Checkpoint);
    e = q.pop();
    CHECK(e.time_ms == 1000);
    CHECK(e.kind == EventKind::MatchEnd);
    e = q.pop();
    CHECK(e.kind == EventKind::Swing);
    CHECK(e.source_id == 1);
    e = q.pop();
    CHECK(e.kind == EventKind::Swing);
    CHECK(e.source_id == 2);
    e = q.pop();
    CHECK(e.time_ms == 2000);
    CHECK(q.empty());
}

TEST_CASE("event queue: same source, target breaks the tie, then insertion seq") {
    EventQueue q(8);
    q.push(500, EventKind::Swing, 1, 3);
    q.push(500, EventKind::Swing, 1, 2);
    q.push(500, EventKind::Swing, 1, 2);  // identical tuple, later seq

    Event a = q.pop();
    Event b = q.pop();
    Event c = q.pop();
    CHECK(a.target_id == 2);
    CHECK(b.target_id == 2);
    CHECK(a.seq < b.seq);  // insertion order decides among identical events
    CHECK(c.target_id == 3);
}

TEST_CASE("event queue: priorities are pinned (checkpoint 0, end 1, decide 2, swing 3)") {
    // These values are part of the documented order; changing them is a
    // semantics change and must fail here first. (Decide added in M2: at an
    // equal timestamp, instant abilities resolve before swings.)
    CHECK(static_cast<int32_t>(EventKind::Checkpoint) == 0);
    CHECK(static_cast<int32_t>(EventKind::MatchEnd) == 1);
    CHECK(static_cast<int32_t>(EventKind::Decide) == 2);
    CHECK(static_cast<int32_t>(EventKind::Swing) == 3);
}

TEST_CASE("event queue: Decide pops before Swing at the same timestamp") {
    EventQueue q(8);
    q.push(1000, EventKind::Swing, 1, 2);
    q.push(1000, EventKind::Decide, 2, 0);
    CHECK(q.pop().kind == EventKind::Decide);
    CHECK(q.pop().kind == EventKind::Swing);
}
