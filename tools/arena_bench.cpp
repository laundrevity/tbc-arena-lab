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

// arena_bench <scenario.yaml> [--seconds S] [--duration-ms N]
// Single-core throughput: runs back-to-back matches (seed varies per match)
// with a counting sink for at least S wall seconds. Wall clock lives ONLY in
// this tool — never in sim/ (CLAUDE.md rule 3). Results are appended to
// docs/benchmarks.md by hand.

#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sim/mechanics/match.h"

using namespace arena;

namespace {

struct CountSink {
    uint64_t swings = 0;
    uint64_t abilities = 0;
    uint64_t checkpoints = 0;
    void on_swing(const SwingRecord&) { ++swings; }
    void on_ability(const AbilityRecord&) { ++abilities; }
    void on_checkpoint(int64_t, uint64_t) { ++checkpoints; }
    void on_end(int64_t, const char*, uint64_t, uint64_t) {}
};

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: arena_bench <scenario.yaml> [--seconds S] [--duration-ms N]\n");
        return 2;
    }
    double seconds = 3.0;
    int64_t duration_override = -1;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--seconds") && i + 1 < argc) {
            seconds = atof(argv[++i]);
        } else if (!strcmp(argv[i], "--duration-ms") && i + 1 < argc) {
            duration_override = strtoll(argv[++i], nullptr, 10);
        } else {
            fprintf(stderr, "arena_bench: unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    Scenario sc;
    std::string err;
    if (!load_scenario(argv[1], sc, err)) {
        fprintf(stderr, "arena_bench: %s\n", err.c_str());
        return 1;
    }
    if (duration_override > 0) sc.duration_ms = duration_override;

    CountSink sink;
    uint64_t matches = 0;
    uint64_t simulated_ms = 0;

    const auto start = std::chrono::steady_clock::now();
    double wall_s = 0;
    while (wall_s < seconds) {
        const MatchResult r = run_match(sc, /*seed=*/1000003ULL + matches, sink);
        simulated_ms += static_cast<uint64_t>(r.end_ms);
        ++matches;
        wall_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }

    const double sim_per_wall = (static_cast<double>(simulated_ms)) / (wall_s * 1000.0);
    printf("scenario: %s (duration_ms %" PRId64 ", end-by-death allowed)\n", sc.name.c_str(),
           sc.duration_ms);
    printf("matches: %" PRIu64 ", simulated_ms: %" PRIu64 ", wall_s: %.3f\n", matches,
           simulated_ms, wall_s);
    printf("simulated-ms per wall-ms: %.1f (%.1fx realtime, single core)\n", sim_per_wall,
           sim_per_wall);
    printf("swings/sec: %.3e   abilities/sec: %.3e   checkpoints/sec: %.3e\n",
           static_cast<double>(sink.swings) / wall_s,
           static_cast<double>(sink.abilities) / wall_s,
           static_cast<double>(sink.checkpoints) / wall_s);
    return 0;
}
