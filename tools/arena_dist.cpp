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

// arena_dist <scenario.yaml> <seed> [--swings N] [--json FILE]
// Per-outcome observed vs expected rates with binomial 99% CI and PASS/FAIL,
// damage and rage summaries. Human table on stdout; machine-readable JSON to
// --json FILE (or appended to stdout when --json is omitted). The JSON is
// the future differential-harness input and includes the table builder's
// expected ranges. Exit 0 iff every row PASSes.

#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "sim/mechanics/dist.h"
#include "sim/mechanics/ruleset.h"

#ifndef SIM_COMMIT
#define SIM_COMMIT "unknown"
#endif

using namespace arena;

namespace {

void write_json(FILE* f, const Scenario& sc, const std::string& scenario_path, uint64_t seed,
                const DistReport& r) {
    fprintf(f,
            "{\"type\":\"dist_report\",\"sim_commit\":\"%s\",\"ruleset\":\"%s\","
            "\"ruleset_hash\":\"0x%016" PRIx64 "\",\"scenario\":\"%s\",\"scenario_name\":\"%s\","
            "\"seed\":%" PRIu64 ",\"swings\":%" PRIu64 ",\"facing\":\"%s\",\"ci_z\":%.10g,",
            SIM_COMMIT, ruleset_id(), ruleset_hash(), scenario_path.c_str(), sc.name.c_str(),
            seed, r.n, r.facing == FacingClass::Front ? "front" : "behind", DIST_Z99);
    fprintf(f, "\"expected_table_pm\":{");
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        fprintf(f, "%s\"%s\":%d", i ? "," : "", outcome_name(static_cast<Outcome>(i)),
                r.table.width[static_cast<size_t>(i)]);
    }
    fprintf(f, "},\"outcomes\":[");
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        const OutcomeRow& row = r.rows[static_cast<size_t>(i)];
        fprintf(f,
                "%s{\"outcome\":\"%s\",\"expected_pm\":%d,\"expected_rate\":%.8f,"
                "\"count\":%" PRIu64 ",\"observed_rate\":%.8f,\"ci_half_width\":%.8f,"
                "\"pass\":%s}",
                i ? "," : "", outcome_name(row.outcome), row.expected_pm, row.expected_rate,
                row.count, row.observed_rate, row.ci_half_width, row.pass ? "true" : "false");
    }
    fprintf(f,
            "],\"damage\":{\"contact_swings\":%" PRIu64 ",\"min\":%d,\"max\":%d,\"mean\":%.4f,"
            "\"p50\":%d,\"p99\":%d,\"mean_per_swing\":%.4f},"
            "\"rage_deci_uncapped\":{\"attacker_mean\":%.4f,\"attacker_min\":%d,"
            "\"attacker_max\":%d,\"defender_mean\":%.4f},\"all_pass\":%s}\n",
            r.contact_count, r.damage_min, r.damage_max, r.damage_mean, r.damage_p50, r.damage_p99,
            r.damage_mean_per_swing, r.rage_att_mean_deci, r.rage_att_min_deci,
            r.rage_att_max_deci, r.rage_def_mean_deci, r.all_pass ? "true" : "false");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: arena_dist <scenario.yaml> <seed> [--swings N] [--json FILE]\n");
        return 2;
    }
    const std::string scenario_path = argv[1];
    const uint64_t seed = strtoull(argv[2], nullptr, 10);
    uint64_t n = 1000000;
    const char* json_path = nullptr;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--swings") && i + 1 < argc) {
            n = strtoull(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            json_path = argv[++i];
        } else {
            fprintf(stderr, "arena_dist: unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    Scenario sc;
    std::string err;
    if (!load_scenario(scenario_path, sc, err)) {
        fprintf(stderr, "arena_dist: %s\n", err.c_str());
        return 1;
    }

    const DistReport r = run_distribution(sc, seed, n);

    printf("scenario %s, seed %" PRIu64 ", N=%" PRIu64 " swings, facing=%s, 99%% binomial CI\n",
           sc.name.c_str(), seed, r.n, r.facing == FacingClass::Front ? "front" : "behind");
    printf("%-8s %12s %12s %12s %12s  %s\n", "outcome", "expected", "observed", "count",
           "ci_half", "result");
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        const OutcomeRow& row = r.rows[static_cast<size_t>(i)];
        printf("%-8s %12.6f %12.6f %12" PRIu64 " %12.6f  %s\n", outcome_name(row.outcome),
               row.expected_rate, row.observed_rate, row.count, row.ci_half_width,
               row.pass ? "PASS" : "FAIL");
    }
    printf("damage (contact swings: %" PRIu64 "): mean %.2f, min %d, max %d, p50 %d, p99 %d; "
           "mean/swing %.2f\n",
           r.contact_count, r.damage_mean, r.damage_min, r.damage_max, r.damage_p50, r.damage_p99,
           r.damage_mean_per_swing);
    printf("rage/swing (deci, uncapped): attacker mean %.2f min %d max %d; defender mean %.2f\n",
           r.rage_att_mean_deci, r.rage_att_min_deci, r.rage_att_max_deci, r.rage_def_mean_deci);
    printf("overall: %s\n", r.all_pass ? "PASS" : "FAIL");

    if (json_path) {
        FILE* jf = fopen(json_path, "w");
        if (!jf) {
            fprintf(stderr, "arena_dist: cannot open %s\n", json_path);
            return 1;
        }
        write_json(jf, sc, scenario_path, seed, r);
        fclose(jf);
    } else {
        write_json(stdout, sc, scenario_path, seed, r);
    }
    return r.all_pass ? 0 : 1;
}
