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
#include "sim/mechanics/trace_sink.h"

#ifndef SIM_COMMIT
#define SIM_COMMIT "unknown"
#endif

using namespace arena;

namespace {


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

    write_trace_prologue(f, SIM_COMMIT, sc, seed);
    TraceFileSink sink{f};
    run_match(sc, seed, sink);

    if (out_path) fclose(f);
    return 0;
}
