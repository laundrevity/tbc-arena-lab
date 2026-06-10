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

// arena_diff <dist_report.json> [--base-dir DIR] [--mc-swings N] [--json FILE]
//
// Differential harness: compares an arena_dist report (this simulator's
// expected table + observed samples) against an independent model of the
// cmangos-tbc oracle (sim/oracle/, ported with citations @ 009455e).
//
// STRICT rows (decide the exit code):
//   - per-outcome table widths: exact per-myriad equality, sim vs oracle
//   - per-outcome observed counts vs oracle expected rate, 99% binomial CI
//   - damage mean: two-sample z at 99% vs oracle Monte Carlo (known offset
//     D-014 is sub-resolution at N=1e6 and reported numerically)
//   - damage min/max within +/-2 (discrete-vs-continuous rolls, D-004/D-014)
//   - attacker/defender rage mean (our conventions) vs oracle arithmetic on
//     the same basis: two-sample z at 99% (validates the integer port)
// INFO rows (never fail; quantify ledgered divergences):
//   - oracle true rage incl. miss/dodge/parry/block bookkeeping
//     (D-011/D-012/D-013) vs our rage
//
// Exit 0 iff every STRICT row passes.

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "sim/core/fixed_trig.h"
#include "sim/core/scenario.h"
#include "sim/core/trace.h"
#include "sim/mechanics/abilities.h"
#include "sim/mechanics/attack_table.h"
#include "sim/mechanics/dist.h"
#include "sim/mechanics/ruleset.h"
#include "sim/oracle/cmangos_model.h"

#ifndef SIM_COMMIT
#define SIM_COMMIT "unknown"
#endif

using namespace arena;

namespace {

// --- minimal extraction over the one-line dist JSON ---

bool sub_object(const std::string& line, const char* key, std::string& out) {
    std::string pat = "\"";
    pat += key;
    pat += "\":{";
    const size_t pos = line.find(pat);
    if (pos == std::string::npos) return false;
    const size_t open = pos + pat.size() - 1;
    const size_t close = line.find('}', open);
    if (close == std::string::npos) return false;
    out = line.substr(open, close - open + 1);
    return true;
}

bool outcome_element(const std::string& line, const char* outcome, std::string& out) {
    std::string pat = "{\"outcome\":\"";
    pat += outcome;
    pat += "\"";
    const size_t pos = line.find(pat);
    if (pos == std::string::npos) return false;
    const size_t close = line.find('}', pos);
    if (close == std::string::npos) return false;
    out = line.substr(pos, close - pos + 1);
    return true;
}

bool get_double(const std::string& s, const char* key, double& out) {
    std::string pat = "\"";
    pat += key;
    pat += "\":";
    const size_t pos = s.find(pat);
    if (pos == std::string::npos) return false;
    out = strtod(s.c_str() + pos + pat.size(), nullptr);
    return true;
}

// Brace-matching extractor for nested sections (sub_object stops at the
// first '}' and only works on flat objects).
bool nested_object(const std::string& line, const char* key, std::string& out) {
    std::string pat = "\"";
    pat += key;
    pat += "\":{";
    const size_t pos = line.find(pat);
    if (pos == std::string::npos) return false;
    const size_t open = pos + pat.size() - 1;
    int depth = 0;
    for (size_t i = open; i < line.size(); ++i) {
        if (line[i] == '{') ++depth;
        if (line[i] == '}' && --depth == 0) {
            out = line.substr(open, i - open + 1);
            return true;
        }
    }
    return false;
}

struct Row {
    std::string name;
    std::string sim_value;
    std::string oracle_value;
    std::string detail;
    bool strict = true;
    bool pass = true;
};

std::string fmt(const char* f, double a) {
    char b[64];
    snprintf(b, sizeof b, f, a);
    return b;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage: arena_diff <dist_report.json> [--base-dir DIR] [--mc-swings N] "
                "[--json FILE]\n");
        return 2;
    }
    std::string base_dir;
    uint64_t mc_n = 0;  // default: same N as the dist report
    const char* json_out = nullptr;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--base-dir") && i + 1 < argc) {
            base_dir = argv[++i];
        } else if (!strcmp(argv[i], "--mc-swings") && i + 1 < argc) {
            mc_n = strtoull(argv[++i], nullptr, 10);
        } else if (!strcmp(argv[i], "--json") && i + 1 < argc) {
            json_out = argv[++i];
        } else {
            fprintf(stderr, "arena_diff: unknown argument %s\n", argv[i]);
            return 2;
        }
    }

    std::ifstream in(argv[1]);
    if (!in) {
        fprintf(stderr, "arena_diff: cannot open %s\n", argv[1]);
        return 1;
    }
    std::string line;
    std::getline(in, line);

    std::string type, scenario_rel;
    int64_t n_i64 = 0;
    uint64_t seed = 0, dist_ruleset_hash = 0;
    if (!trace_get_string(line, "type", type) || type != "dist_report" ||
        !trace_get_string(line, "scenario", scenario_rel) ||
        !trace_get_u64(line, "seed", seed) || !trace_get_i64(line, "swings", n_i64) ||
        !trace_get_hash(line, "ruleset_hash", dist_ruleset_hash)) {
        fprintf(stderr, "arena_diff: malformed dist_report JSON\n");
        return 1;
    }
    const uint64_t n = static_cast<uint64_t>(n_i64);
    if (dist_ruleset_hash != ruleset_hash()) {
        fprintf(stderr,
                "arena_diff: dist report was produced under different mechanics constants "
                "(ruleset_hash mismatch) — regenerate it with this build\n");
        return 1;
    }

    const std::string scenario_path =
        base_dir.empty() ? scenario_rel : base_dir + "/" + scenario_rel;
    Scenario sc;
    std::string err;
    if (!load_scenario(scenario_path, sc, err)) {
        fprintf(stderr, "arena_diff: %s\n", err.c_str());
        return 1;
    }

    // Rebuild the sim-side expectations and the oracle model.
    const bool gate = mutual_frontal_arc(sc.defender.pos_x_cm, sc.defender.pos_y_cm,
                                         sc.defender.facing_mrad, sc.attacker.pos_x_cm,
                                         sc.attacker.pos_y_cm, sc.attacker.facing_mrad);
    const AttackTable sim_table = build_attack_table(
        sc.attacker, sc.defender, gate ? FacingClass::Front : FacingClass::Behind, true);
    const auto oracle_table = oracle::build_table(sc.attacker, sc.defender, gate);
    if (mc_n == 0) mc_n = n;
    // Decorrelate from the sim streams (different subsystems already do, the
    // xor just makes it explicit that this is a different experiment).
    const oracle::McReport mc =
        oracle::run_mc(sc.attacker, sc.defender, oracle_table, seed ^ 0x0AC1E5EEDULL, mc_n);

    std::vector<Row> rows;
    const double z = DIST_Z99;

    // 1) Table widths, exact.
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        const char* name = outcome_name(static_cast<Outcome>(i));
        Row r;
        r.name = std::string("table_pm.") + name;
        const int32_t ours = sim_table.width[static_cast<size_t>(i)];
        const int32_t theirs = oracle_table[static_cast<size_t>(i)];
        r.sim_value = std::to_string(ours);
        r.oracle_value = std::to_string(theirs);
        r.pass = ours == theirs;
        r.detail = "exact per-myriad equality";
        rows.push_back(r);
    }

    // 2) Observed counts vs oracle expected rates, binomial 99% CI.
    for (int32_t i = 0; i < OUTCOME_COUNT; ++i) {
        const char* name = outcome_name(static_cast<Outcome>(i));
        std::string elem;
        int64_t count = 0;
        if (!outcome_element(line, name, elem) || !trace_get_i64(elem, "count", count)) {
            fprintf(stderr, "arena_diff: missing outcome '%s' in dist report\n", name);
            return 1;
        }
        const double p = static_cast<double>(oracle_table[static_cast<size_t>(i)]) / 10000.0;
        const double obs = static_cast<double>(count) / static_cast<double>(n);
        const double ci = z * std::sqrt(p * (1.0 - p) / static_cast<double>(n));
        Row r;
        r.name = std::string("rate.") + name;
        r.sim_value = fmt("%.6f", obs);
        r.oracle_value = fmt("%.6f", p);
        r.pass = p == 0.0 ? count == 0 : std::fabs(obs - p) <= ci;
        r.detail = "99% binomial CI half-width " + fmt("%.6f", ci);
        rows.push_back(r);
    }

    // 3) Damage summary.
    std::string dmg;
    double d_mean = 0, d_sd = 0, d_min = 0, d_max = 0, d_contact = 0;
    if (!sub_object(line, "damage", dmg) || !get_double(dmg, "mean", d_mean) ||
        !get_double(dmg, "sd", d_sd) || !get_double(dmg, "min", d_min) ||
        !get_double(dmg, "max", d_max) || !get_double(dmg, "contact_swings", d_contact)) {
        fprintf(stderr, "arena_diff: dist report lacks damage{mean,sd,min,max} — "
                        "regenerate with a current arena_dist build\n");
        return 1;
    }
    {
        const double se = std::sqrt(d_sd * d_sd / d_contact +
                                    mc.damage_sd * mc.damage_sd /
                                        static_cast<double>(mc.contact_count));
        const double zv = se > 0 ? (d_mean - mc.damage_mean) / se : 0.0;
        Row r;
        r.name = "damage.mean";
        r.sim_value = fmt("%.3f", d_mean);
        r.oracle_value = fmt("%.3f", mc.damage_mean);
        r.pass = std::fabs(zv) <= z;
        r.detail = "two-sample z " + fmt("%.2f", zv) +
                   " (D-014 discrete-vs-frand offset reported, sub-CI at this N)";
        rows.push_back(r);

        Row rmin{"damage.min", fmt("%.0f", d_min), std::to_string(mc.damage_min),
                 "tolerance +/-2 (D-004/D-014)", true,
                 std::fabs(d_min - mc.damage_min) <= 2.0};
        Row rmax{"damage.max", fmt("%.0f", d_max), std::to_string(mc.damage_max),
                 "tolerance +/-2 (D-004/D-014)", true,
                 std::fabs(d_max - mc.damage_max) <= 2.0};
        rows.push_back(rmin);
        rows.push_back(rmax);
    }

    // 4) Rage, our conventions (validates the integer arithmetic port).
    std::string rage;
    double ra_mean = 0, ra_sd = 0, rd_mean = 0, rd_sd = 0;
    if (!sub_object(line, "rage_deci_uncapped", rage) ||
        !get_double(rage, "attacker_mean", ra_mean) ||
        !get_double(rage, "attacker_sd", ra_sd) ||
        !get_double(rage, "defender_mean", rd_mean) ||
        !get_double(rage, "defender_sd", rd_sd)) {
        fprintf(stderr, "arena_diff: dist report lacks rage stats with sd\n");
        return 1;
    }
    const double nd = static_cast<double>(n);
    const double mcnd = static_cast<double>(mc.n);
    {
        const double se =
            std::sqrt(ra_sd * ra_sd / nd + mc.rage_att_ours_sd * mc.rage_att_ours_sd / mcnd);
        const double zv = se > 0 ? (ra_mean - mc.rage_att_ours_mean) / se : 0.0;
        rows.push_back(Row{"rage_att.mean(ours-basis)", fmt("%.3f", ra_mean),
                           fmt("%.3f", mc.rage_att_ours_mean),
                           "two-sample z " + fmt("%.2f", zv) + ", oracle arithmetic, our basis",
                           true, std::fabs(zv) <= z});
        const double se2 =
            std::sqrt(rd_sd * rd_sd / nd + mc.rage_def_ours_sd * mc.rage_def_ours_sd / mcnd);
        const double zv2 = se2 > 0 ? (rd_mean - mc.rage_def_ours_mean) / se2 : 0.0;
        rows.push_back(Row{"rage_def.mean(ours-basis)", fmt("%.3f", rd_mean),
                           fmt("%.3f", mc.rage_def_ours_mean),
                           "two-sample z " + fmt("%.2f", zv2) + ", oracle arithmetic, our basis",
                           true, std::fabs(zv2) <= z});
        // INFO: the oracle's true rage incl. avoid-rage bookkeeping.
        rows.push_back(Row{"rage_att.mean(oracle-true)", fmt("%.3f", ra_mean),
                           fmt("%.3f", mc.rage_att_true_mean),
                           "expected divergence D-011/D-012/D-013 (informational)", false,
                           true});
        rows.push_back(Row{"rage_def.mean(oracle-true)", fmt("%.3f", rd_mean),
                           fmt("%.3f", mc.rage_def_true_mean),
                           "expected divergence D-013 (informational)", false, true});
    }

    // 5) Yellow (weapon-ability) coverage, spec M-012 (M5 harness item).
    struct YellowParams {
        const char* key;
        bool normalized;
        int32_t flat_bonus;
        uint64_t salt;
    };
    const YellowParams yparams[2] = {
        {"yellow_ms", true, MS_FLAT_BONUS, 0x0AC1E13E110ULL},
        {"yellow_hs", false, HS_FLAT_BONUS, 0x0AC1E13E111ULL},
    };
    for (const YellowParams& yp : yparams) {
        std::string ysec;
        if (!nested_object(line, yp.key, ysec)) {
            fprintf(stderr, "arena_diff: dist report lacks %s — regenerate with a current "
                            "arena_dist build\n", yp.key);
            return 1;
        }
        double flat_in_report = 0;
        if (!get_double(ysec, "flat_bonus", flat_in_report) ||
            static_cast<int32_t>(flat_in_report) != yp.flat_bonus) {
            fprintf(stderr, "arena_diff: %s flat_bonus mismatch vs abilities.h\n", yp.key);
            return 1;
        }

        // Sim-side expectations and the oracle yellow model.
        const FacingClass fc = gate ? FacingClass::Front : FacingClass::Behind;
        const YellowTable sim_yt = build_yellow_table(sc.attacker, sc.defender, fc);
        const int32_t sim_w[4] = {sim_yt.miss_end, sim_yt.dodge_end - sim_yt.miss_end,
                                  sim_yt.parry_end - sim_yt.dodge_end, 10000 - sim_yt.parry_end};
        const auto ora_w = oracle::build_yellow_table(sc.attacker, sc.defender, gate);
        const bool can_block = gate && sc.defender.has_shield;
        const int32_t sim_crit = std::min(chance_crit_pm(sc.attacker, sc.defender), 10000);
        const int32_t sim_block =
            can_block ? std::min(chance_block_pm(sc.attacker, sc.defender), 10000) : 0;
        const int32_t ora_crit = oracle::yellow_crit_pm(sc.attacker, sc.defender);
        const int32_t ora_block = oracle::yellow_block_pm(sc.attacker, sc.defender, gate);
        const oracle::YellowMcReport ymc =
            oracle::run_yellow_mc(sc.attacker, sc.defender, gate, yp.normalized, yp.flat_bonus,
                                  seed ^ yp.salt, mc_n);

        static const char* ynames[4] = {"miss", "dodge", "parry", "hit"};
        const std::string prefix = std::string(yp.key) + ".";
        // Table widths + separate-roll chances, exact.
        for (size_t i = 0; i < 4; ++i) {
            rows.push_back(Row{"ytable_pm." + prefix + ynames[i], std::to_string(sim_w[i]),
                               std::to_string(ora_w[i]), "exact per-myriad equality", true,
                               sim_w[i] == ora_w[i]});
        }
        rows.push_back(Row{"ychance_pm." + prefix + "crit", std::to_string(sim_crit),
                           std::to_string(ora_crit), "exact per-myriad equality", true,
                           sim_crit == ora_crit});
        rows.push_back(Row{"ychance_pm." + prefix + "block", std::to_string(sim_block),
                           std::to_string(ora_block), "exact per-myriad equality", true,
                           sim_block == ora_block});

        // Observed rates vs oracle expectations (die rows over n; crit/block
        // over the report's hit count), 99% binomial CI.
        double hits_d = 0;
        {
            std::string dmgsec;
            if (!nested_object(ysec, "damage", dmgsec) ||
                !get_double(dmgsec, "hits", hits_d)) {
                fprintf(stderr, "arena_diff: %s lacks damage.hits\n", yp.key);
                return 1;
            }
        }
        const char* rate_rows[6] = {"miss", "dodge", "parry", "hit", "crit|hit", "block|hit"};
        const int32_t ora_pm[6] = {ora_w[0], ora_w[1], ora_w[2], ora_w[3], ora_crit, ora_block};
        for (size_t i = 0; i < 6; ++i) {
            std::string elem;
            int64_t count = 0, trials = 0;
            if (!outcome_element(ysec, rate_rows[i], elem) ||
                !trace_get_i64(elem, "count", count) ||
                !trace_get_i64(elem, "trials", trials)) {
                fprintf(stderr, "arena_diff: %s lacks outcome '%s'\n", yp.key, rate_rows[i]);
                return 1;
            }
            const double tn = static_cast<double>(trials);
            const double p = static_cast<double>(ora_pm[i]) / 10000.0;
            const double obs = tn > 0 ? static_cast<double>(count) / tn : 0.0;
            const double ci = tn > 0 ? z * std::sqrt(p * (1.0 - p) / tn) : 0.0;
            rows.push_back(Row{"yrate." + prefix + rate_rows[i], fmt("%.6f", obs),
                               fmt("%.6f", p),
                               "99% binomial CI half-width " + fmt("%.6f", ci), true,
                               p == 0.0 ? count == 0 : std::fabs(obs - p) <= ci});
        }

        // Damage over hits: two-sample z vs the oracle MC; min/max +/-2.
        std::string dmgsec;
        double ymean = 0, ysd = 0, ymin = 0, ymax = 0;
        nested_object(ysec, "damage", dmgsec);
        if (!get_double(dmgsec, "mean", ymean) || !get_double(dmgsec, "sd", ysd) ||
            !get_double(dmgsec, "min", ymin) || !get_double(dmgsec, "max", ymax)) {
            fprintf(stderr, "arena_diff: %s lacks damage{mean,sd,min,max}\n", yp.key);
            return 1;
        }
        {
            const double se = std::sqrt(ysd * ysd / hits_d +
                                        ymc.damage_sd * ymc.damage_sd /
                                            static_cast<double>(ymc.hit_count));
            const double zv = se > 0 ? (ymean - ymc.damage_mean) / se : 0.0;
            rows.push_back(Row{"ydamage." + prefix + "mean", fmt("%.3f", ymean),
                               fmt("%.3f", ymc.damage_mean),
                               "two-sample z " + fmt("%.2f", zv) +
                                   " (crit-before-armor float pipeline, D-020/D-004/D-014)",
                               true, std::fabs(zv) <= z});
            rows.push_back(Row{"ydamage." + prefix + "min", fmt("%.0f", ymin),
                               std::to_string(ymc.damage_min), "tolerance +/-2 (D-004/D-014)",
                               true, std::fabs(ymin - ymc.damage_min) <= 2.0});
            rows.push_back(Row{"ydamage." + prefix + "max", fmt("%.0f", ymax),
                               std::to_string(ymc.damage_max), "tolerance +/-2 (D-004/D-014)",
                               true, std::fabs(ymax - ymc.damage_max) <= 2.0});
        }

        // Victim rage: ours-basis strict (validates the M-016 integer port
        // against oracle arithmetic); oracle-true INFO (= 0, D-019).
        std::string rsec;
        double yrd_mean = 0, yrd_sd = 0;
        if (!nested_object(ysec, "rage_def_deci", rsec) ||
            !get_double(rsec, "mean", yrd_mean) || !get_double(rsec, "sd", yrd_sd)) {
            fprintf(stderr, "arena_diff: %s lacks rage_def_deci\n", yp.key);
            return 1;
        }
        {
            const double se =
                std::sqrt(yrd_sd * yrd_sd / static_cast<double>(n) +
                          ymc.rage_def_ours_sd * ymc.rage_def_ours_sd /
                              static_cast<double>(ymc.n));
            const double zv = se > 0 ? (yrd_mean - ymc.rage_def_ours_mean) / se : 0.0;
            rows.push_back(Row{"yrage_def." + prefix + "mean(ours-basis)",
                               fmt("%.3f", yrd_mean), fmt("%.3f", ymc.rage_def_ours_mean),
                               "two-sample z " + fmt("%.2f", zv) +
                                   ", oracle arithmetic, our basis (M-016)",
                               true, std::fabs(zv) <= z});
            rows.push_back(Row{"yrage_def." + prefix + "mean(oracle-true)",
                               fmt("%.3f", yrd_mean), "0.000",
                               "oracle grants NO victim rage on the spell path (D-019, "
                               "informational)",
                               false, true});
        }
    }

    // --- output ---
    bool all_pass = true;
    printf("arena_diff: %s vs cmangos-tbc oracle model (@009455e), N=%" PRIu64
           ", MC N=%" PRIu64 ", facing gate %s\n",
           sc.name.c_str(), n, mc.n, gate ? "front" : "behind/away");
    printf("%-28s %14s %14s  %-6s %s\n", "row", "sim", "oracle", "result", "detail");
    for (const Row& r : rows) {
        if (r.strict) all_pass = all_pass && r.pass;
        printf("%-28s %14s %14s  %-6s %s\n", r.name.c_str(), r.sim_value.c_str(),
               r.oracle_value.c_str(), r.strict ? (r.pass ? "PASS" : "FAIL") : "INFO",
               r.detail.c_str());
    }
    printf("overall: %s\n", all_pass ? "PASS" : "FAIL");

    if (json_out) {
        FILE* f = fopen(json_out, "w");
        if (!f) {
            fprintf(stderr, "arena_diff: cannot open %s\n", json_out);
            return 1;
        }
        fprintf(f,
                "{\"type\":\"diff_report\",\"sim_commit\":\"%s\",\"oracle\":\"cmangos-tbc@"
                "009455e\",\"scenario_name\":\"%s\",\"n\":%" PRIu64 ",\"mc_n\":%" PRIu64
                ",\"rows\":[",
                SIM_COMMIT, sc.name.c_str(), n, mc.n);
        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            fprintf(f,
                    "%s{\"row\":\"%s\",\"sim\":\"%s\",\"oracle\":\"%s\",\"strict\":%s,"
                    "\"pass\":%s,\"detail\":\"%s\"}",
                    i ? "," : "", r.name.c_str(), r.sim_value.c_str(), r.oracle_value.c_str(),
                    r.strict ? "true" : "false", r.pass ? "true" : "false", r.detail.c_str());
        }
        fprintf(f, "],\"all_pass\":%s}\n", all_pass ? "true" : "false");
        fclose(f);
    }
    return all_pass ? 0 : 1;
}
