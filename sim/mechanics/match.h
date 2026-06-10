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

#include <cstdint>
#include <vector>

#include "sim/core/event_queue.h"
#include "sim/core/fixed_trig.h"
#include "sim/core/scenario.h"
#include "sim/core/unit_state.h"
#include "sim/mechanics/swing.h"

// Deterministic match runner. Authoritative time is int64 ms; no wall-clock
// reads anywhere in sim/ (CLAUDE.md rule 3). Hash checkpoints fire every
// authoritative 1000 ms, before any same-timestamp combat event (the total
// order lives in sim/core/event_queue.h).

namespace arena {

constexpr int64_t CHECKPOINT_INTERVAL_MS = 1000;

struct SwingRecord {
    int64_t t = 0;
    uint64_t seq = 0;  // event seq (ordering only; RNG uses per-entity cursors)
    int32_t src = 0;
    int32_t tgt = 0;
    SwingResult result;
    int32_t src_rage_deci_after = 0;  // post-cap
    int32_t tgt_rage_deci_after = 0;
    int32_t tgt_hp_after = 0;
};

struct MatchResult {
    int64_t end_ms = 0;
    const char* end_reason = "duration";  // "duration" | "death"
    uint64_t swings = 0;
    uint64_t checkpoints = 0;
    uint64_t initial_hash = 0;
};

// Sink contract:
//   void on_swing(const SwingRecord&);
//   void on_checkpoint(int64_t t, uint64_t hash);
//   void on_end(int64_t t, const char* reason, uint64_t swings, uint64_t checkpoints);
// The header/init lines are the caller's business (they need scenario and
// build metadata the kernel does not own).

struct CollectSink {
    std::vector<SwingRecord> swings;
    std::vector<std::pair<int64_t, uint64_t>> checkpoints;
    int64_t end_t = -1;
    std::string end_reason;

    void on_swing(const SwingRecord& r) { swings.push_back(r); }
    void on_checkpoint(int64_t t, uint64_t h) { checkpoints.emplace_back(t, h); }
    void on_end(int64_t t, const char* reason, uint64_t, uint64_t) {
        end_t = t;
        end_reason = reason;
    }
};

struct NullSink {
    void on_swing(const SwingRecord&) {}
    void on_checkpoint(int64_t, uint64_t) {}
    void on_end(int64_t, const char*, uint64_t, uint64_t) {}
};

template <typename Sink>
MatchResult run_match(const Scenario& sc, uint64_t seed, Sink&& sink) {
    // Index units so iteration and hashing are in ascending entity_id order
    // (deterministic-iteration rule; M0 has exactly two units).
    const UnitSpec* specs[2];
    if (sc.attacker.entity_id < sc.defender.entity_id) {
        specs[0] = &sc.attacker;
        specs[1] = &sc.defender;
    } else {
        specs[0] = &sc.defender;
        specs[1] = &sc.attacker;
    }
    int32_t ids[2] = {specs[0]->entity_id, specs[1]->entity_id};
    UnitState states[2] = {initial_state(*specs[0]), initial_state(*specs[1])};
    RngCursor cursors[2];

    auto index_of = [&](int32_t entity_id) { return entity_id == ids[0] ? 0 : 1; };

    // Live events at any moment: one swing per attacker + checkpoint chain +
    // match end. 16 is comfortably above that; reserved once, never grown.
    EventQueue queue(16);
    std::vector<uint8_t> hash_buf;
    hash_buf.reserve(128);

    MatchResult res;
    res.initial_hash = hash_units(hash_buf, ids, states, 2);

    queue.push(sc.duration_ms, EventKind::MatchEnd, 0, 0);
    if (CHECKPOINT_INTERVAL_MS <= sc.duration_ms) {
        queue.push(CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint, 0, 0);
    }
    for (int i = 0; i < 2; ++i) {
        if (specs[i]->attacks) {
            const int32_t target = ids[i == 0 ? 1 : 0];
            queue.push(states[i].next_swing_ms, EventKind::Swing, ids[i], target);
        }
    }

    while (!queue.empty()) {
        const Event ev = queue.pop();
        switch (ev.kind) {
            case EventKind::Checkpoint: {
                const uint64_t h = hash_units(hash_buf, ids, states, 2);
                ++res.checkpoints;
                sink.on_checkpoint(ev.time_ms, h);
                if (ev.time_ms + CHECKPOINT_INTERVAL_MS <= sc.duration_ms) {
                    queue.push(ev.time_ms + CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint, 0, 0);
                }
                break;
            }
            case EventKind::MatchEnd: {
                res.end_ms = ev.time_ms;
                res.end_reason = "duration";
                sink.on_end(ev.time_ms, res.end_reason, res.swings, res.checkpoints);
                return res;
            }
            case EventKind::Swing: {
                const int src = index_of(ev.source_id);
                const int tgt = index_of(ev.target_id);
                const FacingClass facing =
                    in_frontal_arc(states[tgt].pos_x_cm, states[tgt].pos_y_cm,
                                   states[tgt].facing_mrad, states[src].pos_x_cm,
                                   states[src].pos_y_cm)
                        ? FacingClass::Front
                        : FacingClass::Behind;
                const SwingResult sw = resolve_swing(*specs[src], *specs[tgt], facing, seed,
                                                     cursors[src]);
                states[tgt].hp = std::max(0, states[tgt].hp - sw.damage);
                states[src].rage_deci = add_rage_deci(states[src].rage_deci, sw.rage_attacker_deci);
                states[tgt].rage_deci = add_rage_deci(states[tgt].rage_deci, sw.rage_defender_deci);
                states[src].next_swing_ms = ev.time_ms + specs[src]->weapon_speed_ms;
                ++res.swings;

                SwingRecord rec;
                rec.t = ev.time_ms;
                rec.seq = ev.seq;
                rec.src = ev.source_id;
                rec.tgt = ev.target_id;
                rec.result = sw;
                rec.src_rage_deci_after = states[src].rage_deci;
                rec.tgt_rage_deci_after = states[tgt].rage_deci;
                rec.tgt_hp_after = states[tgt].hp;
                sink.on_swing(rec);

                if (states[tgt].hp <= 0) {
                    res.end_ms = ev.time_ms;
                    res.end_reason = "death";
                    sink.on_end(ev.time_ms, res.end_reason, res.swings, res.checkpoints);
                    return res;
                }
                queue.push(states[src].next_swing_ms, EventKind::Swing, ev.source_id,
                           ev.target_id);
                break;
            }
        }
    }
    // Unreachable: the MatchEnd event always terminates the loop.
    return res;
}

} // namespace arena
