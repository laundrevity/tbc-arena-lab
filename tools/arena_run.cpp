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

// arena_run <scenario.yaml> <seed> [--duration-ms N] [-o trace.jsonl]
// Runs the scenario and writes a JSONL trace to stdout (or -o file).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sim/core/trace.h"
#include "sim/mechanics/match.h"
#include "sim/mechanics/ruleset.h"

#ifndef SIM_COMMIT
#define SIM_COMMIT "unknown"
#endif

using namespace arena;

namespace {

struct FileSink {
    FILE* f;
    void on_swing(const SwingRecord& r) {
        trace_write_swing(f, r.t, r.seq, r.src, r.tgt, r.result.roll_pm,
                          outcome_name(r.result.outcome), r.result.damage, r.src_rage_deci_after,
                          r.tgt_rage_deci_after, r.tgt_hp_after);
    }
    void on_ability(const AbilityRecord& r) {
        trace_write_ability(f, r.t, r.seq, r.src, r.tgt, r.ability, r.result.roll_pm,
                            yellow_outcome_name(r.result.outcome), r.result.crit,
                            r.result.blocked, r.result.damage, r.src_rage_deci_after,
                            r.tgt_rage_deci_after, r.tgt_hp_after);
    }
    void on_decision(const DecisionRecord& r) {
        trace_write_decision(f, r.t, r.unit, action_name(r.action));
    }
    void on_checkpoint(int64_t t, uint64_t h) { trace_write_checkpoint(f, t, h); }
    void on_end(int64_t t, const char* reason, uint64_t swings, uint64_t checkpoints) {
        trace_write_end(f, t, reason, swings, checkpoints);
    }
};

int usage() {
    fprintf(stderr, "usage: arena_run <scenario.yaml> <seed> [--duration-ms N] [-o trace.jsonl]\n");
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string scenario_path = argv[1];
    const uint64_t seed = strtoull(argv[2], nullptr, 10);
    int64_t duration_override = -1;
    const char* out_path = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--duration-ms") && i + 1 < argc) {
            duration_override = strtoll(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            out_path = argv[++i];
        } else {
            return usage();
        }
    }

    Scenario sc;
    std::string err;
    if (!load_scenario(scenario_path, sc, err)) {
        fprintf(stderr, "arena_run: %s\n", err.c_str());
        return 1;
    }
    if (sc.ruleset != ruleset_id()) {
        fprintf(stderr, "arena_run: scenario pins ruleset '%s' but this build is '%s'\n",
                sc.ruleset.c_str(), ruleset_id());
        return 1;
    }
    if (duration_override > 0) sc.duration_ms = duration_override;

    FILE* f = stdout;
    if (out_path) {
        f = fopen(out_path, "w");
        if (!f) {
            fprintf(stderr, "arena_run: cannot open output file %s\n", out_path);
            return 1;
        }
    }

    trace_write_header(f, SIM_COMMIT, ruleset_id(), ruleset_hash(), scenario_path, sc.name, seed,
                       sc.duration_ms);

    // Init line: state at t=0 in ascending entity_id order, plus its hash.
    const UnitSpec* specs[2] = {&sc.attacker, &sc.defender};
    if (specs[0]->entity_id > specs[1]->entity_id) std::swap(specs[0], specs[1]);
    int32_t ids[2] = {specs[0]->entity_id, specs[1]->entity_id};
    UnitState init[2] = {initial_state(*specs[0]), initial_state(*specs[1])};
    std::vector<uint8_t> buf;
    trace_write_init(f, hash_units(buf, ids, init, 2), ids, init, 2);

    FileSink sink{f};
    run_match(sc, seed, sink);

    if (out_path) fclose(f);
    return 0;
}
