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

#include "sim/mechanics/replay.h"

#include <cinttypes>
#include <cstdio>
#include <fstream>
#include <vector>

#include "sim/core/trace.h"
#include "sim/mechanics/match.h"
#include "sim/mechanics/ruleset.h"

namespace arena {

namespace {

std::string fmt(const char* f, int64_t a, int64_t b) {
    char buf[160];
    snprintf(buf, sizeof buf, f, a, b);
    return buf;
}

} // namespace

ReplayResult verify_trace_file(const std::string& trace_path, const std::string& base_dir) {
    ReplayResult res;
    std::ifstream in(trace_path);
    if (!in) {
        res.error = "cannot open trace: " + trace_path;
        return res;
    }

    std::string line;
    if (!std::getline(in, line)) {
        res.error = "empty trace";
        return res;
    }

    std::string type, scenario_rel;
    uint64_t seed = 0, trace_ruleset_hash = 0;
    int64_t duration_ms = 0;
    if (!trace_get_string(line, "type", type) || type != "header" ||
        !trace_get_string(line, "scenario", scenario_rel) ||
        !trace_get_u64(line, "seed", seed) ||
        !trace_get_hash(line, "ruleset_hash", trace_ruleset_hash) ||
        !trace_get_i64(line, "duration_ms", duration_ms)) {
        res.error = "malformed header line";
        return res;
    }
    if (trace_ruleset_hash != ruleset_hash()) {
        res.error = "ruleset_hash mismatch: trace was recorded under different mechanics "
                    "constants (regenerate goldens with an explanatory commit)";
        return res;
    }

    const std::string scenario_path =
        base_dir.empty() ? scenario_rel : base_dir + "/" + scenario_rel;
    Scenario sc;
    std::string err;
    if (!load_scenario(scenario_path, sc, err)) {
        res.error = err;
        return res;
    }
    sc.duration_ms = duration_ms;  // header records the effective duration

    CollectSink sink;
    const MatchResult m = run_match(sc, seed, sink);

    // Walk the trace and compare against the re-simulation.
    size_t next_checkpoint = 0;
    size_t next_swing = 0;
    size_t next_ability = 0;
    size_t next_decision = 0;
    bool saw_end = false;
    while (std::getline(in, line)) {
        if (!trace_get_string(line, "type", type)) {
            res.error = "malformed trace line: " + line;
            return res;
        }
        if (type == "init") {
            uint64_t h = 0;
            if (!trace_get_hash(line, "hash", h)) {
                res.error = "malformed init line";
                return res;
            }
            if (h != m.initial_hash) {
                res.error = "init state hash mismatch";
                return res;
            }
        } else if (type == "checkpoint") {
            int64_t t = 0;
            uint64_t h = 0;
            if (!trace_get_i64(line, "t", t) || !trace_get_hash(line, "hash", h)) {
                res.error = "malformed checkpoint line";
                return res;
            }
            if (next_checkpoint >= sink.checkpoints.size()) {
                res.error = fmt("trace has extra checkpoint at t=%" PRId64 " (resim has %" PRId64
                                ")",
                                t, static_cast<int64_t>(sink.checkpoints.size()));
                return res;
            }
            const auto& [st, sh] = sink.checkpoints[next_checkpoint];
            if (st != t || sh != h) {
                res.error = fmt("checkpoint mismatch at t=%" PRId64 " (resim t=%" PRId64 ")", t,
                                st);
                return res;
            }
            ++next_checkpoint;
            ++res.checkpoints_checked;
        } else if (type == "swing") {
            ++next_swing;
        } else if (type == "ability") {
            ++next_ability;
        } else if (type == "decision") {
            ++next_decision;
        } else if (type == "end") {
            int64_t t = 0;
            std::string reason;
            if (!trace_get_i64(line, "t", t) || !trace_get_string(line, "reason", reason)) {
                res.error = "malformed end line";
                return res;
            }
            if (t != m.end_ms || reason != m.end_reason) {
                res.error = fmt("end mismatch: trace t=%" PRId64 ", resim t=%" PRId64, t,
                                m.end_ms) +
                            " (trace reason '" + reason + "', resim '" + m.end_reason + "')";
                return res;
            }
            saw_end = true;
        } else if (type != "header") {
            res.error = "unknown trace line type: " + type;
            return res;
        }
    }

    res.swings_in_trace = next_swing;
    res.abilities_in_trace = next_ability;
    res.decisions_in_trace = next_decision;
    if (!saw_end) {
        res.error = "trace has no end line";
        return res;
    }
    if (next_checkpoint != sink.checkpoints.size()) {
        res.error = fmt("trace has %" PRId64 " checkpoints, resim produced %" PRId64,
                        static_cast<int64_t>(next_checkpoint),
                        static_cast<int64_t>(sink.checkpoints.size()));
        return res;
    }
    if (next_swing != sink.swings.size()) {
        res.error = fmt("trace has %" PRId64 " swings, resim produced %" PRId64,
                        static_cast<int64_t>(next_swing),
                        static_cast<int64_t>(sink.swings.size()));
        return res;
    }
    if (next_ability != sink.abilities.size()) {
        res.error = fmt("trace has %" PRId64 " abilities, resim produced %" PRId64,
                        static_cast<int64_t>(next_ability),
                        static_cast<int64_t>(sink.abilities.size()));
        return res;
    }
    if (next_decision != sink.decisions.size()) {
        res.error = fmt("trace has %" PRId64 " decisions, resim produced %" PRId64,
                        static_cast<int64_t>(next_decision),
                        static_cast<int64_t>(sink.decisions.size()));
        return res;
    }
    res.ok = true;
    return res;
}

} // namespace arena
