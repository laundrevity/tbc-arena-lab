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

#include <cstdio>

#include "sim/core/trace.h"
#include "sim/mechanics/abilities.h"
#include "sim/mechanics/match.h"
#include "sim/mechanics/observation.h"
#include "sim/mechanics/ruleset.h"

// Shared trace-writing sink for arena_run and the C API. A null FILE* makes
// every callback a no-op, so one instantiation covers traced and untraced
// runs.

namespace arena {

struct TraceFileSink {
    FILE* f = nullptr;

    void on_swing(const SwingRecord& r) {
        if (!f) return;
        trace_write_swing(f, r.t, r.seq, r.src, r.tgt, r.result.roll_pm,
                          outcome_name(r.result.outcome), r.result.damage, r.src_rage_deci_after,
                          r.tgt_rage_deci_after, r.tgt_hp_after);
    }
    void on_ability(const AbilityRecord& r) {
        if (!f) return;
        trace_write_ability(f, r.t, r.seq, r.src, r.tgt, r.ability, r.result.roll_pm,
                            yellow_outcome_name(r.result.outcome), r.result.crit,
                            r.result.blocked, r.result.damage, r.src_rage_deci_after,
                            r.tgt_rage_deci_after, r.tgt_hp_after);
    }
    void on_decision(const DecisionRecord& r) {
        if (!f) return;
        trace_write_decision(f, r.t, r.unit, action_name(r.action));
    }
    void on_checkpoint(int64_t t, uint64_t h) {
        if (f) trace_write_checkpoint(f, t, h);
    }
    void on_end(int64_t t, const char* reason, uint64_t swings, uint64_t checkpoints) {
        if (f) trace_write_end(f, t, reason, swings, checkpoints);
    }
};

// Header + init lines (everything above the event stream).
inline void write_trace_prologue(FILE* f, const char* sim_commit, const Scenario& sc,
                                 uint64_t seed) {
    trace_write_header(f, sim_commit, ruleset_id(), ruleset_hash(), sc.source_path, sc.name,
                       seed, sc.duration_ms);
    const UnitSpec* specs[2] = {&sc.attacker, &sc.defender};
    if (specs[0]->entity_id > specs[1]->entity_id) std::swap(specs[0], specs[1]);
    int32_t ids[2] = {specs[0]->entity_id, specs[1]->entity_id};
    UnitState init[2] = {initial_state(*specs[0]), initial_state(*specs[1])};
    std::vector<uint8_t> buf;
    trace_write_init(f, hash_units(buf, ids, init, 2), ids, init, 2);
}

} // namespace arena
