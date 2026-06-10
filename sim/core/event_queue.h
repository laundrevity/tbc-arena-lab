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

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

namespace arena {

// Event kinds double as same-timestamp priorities: at equal time_ms a
// Checkpoint fires before MatchEnd fires before Swing. This is THE single
// definition of the simultaneous-event total order
//   (time_ms, priority, source_id, target_id, seq)
// required by CLAUDE.md rule 3; it is tested in test_event_order.cpp.
enum class EventKind : int32_t {
    Checkpoint = 0,
    MatchEnd   = 1,
    Swing      = 2,
};

// POD event. seq is assigned monotonically at push time and is the final
// tie-breaker, so insertion order decides among otherwise-identical events.
struct Event {
    int64_t time_ms = 0;
    int32_t priority = 0;
    int32_t source_id = 0;
    int32_t target_id = 0;
    uint64_t seq = 0;
    EventKind kind = EventKind::Checkpoint;
};

constexpr bool event_before(const Event& a, const Event& b) {
    if (a.time_ms != b.time_ms) return a.time_ms < b.time_ms;
    if (a.priority != b.priority) return a.priority < b.priority;
    if (a.source_id != b.source_id) return a.source_id < b.source_id;
    if (a.target_id != b.target_id) return a.target_id < b.target_id;
    return a.seq < b.seq;
}

// Binary min-heap over a pre-sized vector: no per-event heap allocation once
// the initial reserve covers the live event count (CLAUDE.md conventions).
class EventQueue {
public:
    explicit EventQueue(size_t reserve_events = 64) { heap_.reserve(reserve_events); }

    void push(int64_t time_ms, EventKind kind, int32_t source_id, int32_t target_id) {
        Event e;
        e.time_ms = time_ms;
        e.priority = static_cast<int32_t>(kind);
        e.source_id = source_id;
        e.target_id = target_id;
        e.seq = next_seq_++;
        e.kind = kind;
        heap_.push_back(e);
        std::push_heap(heap_.begin(), heap_.end(), after_);
    }

    bool empty() const { return heap_.empty(); }

    Event pop() {
        std::pop_heap(heap_.begin(), heap_.end(), after_);
        Event e = heap_.back();
        heap_.pop_back();
        return e;
    }

private:
    // std::push_heap builds a max-heap on its comparator; inverting
    // event_before yields a min-heap on the documented total order.
    static constexpr bool after_(const Event& a, const Event& b) { return event_before(b, a); }

    std::vector<Event> heap_;
    uint64_t next_seq_ = 0;
};

} // namespace arena
