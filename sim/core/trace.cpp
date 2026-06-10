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

#include "sim/core/trace.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>

namespace arena {

void trace_write_header(FILE* f, const char* sim_commit, const char* ruleset,
                        uint64_t ruleset_hash, const std::string& scenario_path,
                        const std::string& scenario_name, uint64_t seed, int64_t duration_ms) {
    fprintf(f,
            "{\"type\":\"header\",\"sim_commit\":\"%s\",\"ruleset\":\"%s\","
            "\"ruleset_hash\":\"0x%016" PRIx64 "\",\"scenario\":\"%s\","
            "\"scenario_name\":\"%s\",\"seed\":%" PRIu64 ",\"duration_ms\":%" PRId64 "}\n",
            sim_commit, ruleset, ruleset_hash, scenario_path.c_str(), scenario_name.c_str(),
            seed, duration_ms);
}

void trace_write_init(FILE* f, uint64_t state_hash, const int32_t* ids, const UnitState* states,
                      size_t count) {
    fprintf(f, "{\"type\":\"init\",\"t\":0,\"hash\":\"0x%016" PRIx64 "\",\"units\":[", state_hash);
    for (size_t i = 0; i < count; ++i) {
        const UnitState& s = states[i];
        fprintf(f,
                "%s{\"id\":%d,\"pos_x_cm\":%" PRId64 ",\"pos_y_cm\":%" PRId64
                ",\"facing_mrad\":%d,\"hp\":%d,\"rage_deci\":%d,\"next_swing_ms\":%" PRId64
                ",\"gcd_ready_ms\":%" PRId64 ",\"ms_ready_ms\":%" PRId64
                ",\"hs_queued\":%d,\"next_decide_ms\":%" PRId64 "}",
                i ? "," : "", ids[i], s.pos_x_cm, s.pos_y_cm, s.facing_mrad, s.hp, s.rage_deci,
                s.next_swing_ms, s.gcd_ready_ms, s.ms_ready_ms, s.hs_queued, s.next_decide_ms);
    }
    fprintf(f, "]}\n");
}

void trace_write_ability(FILE* f, int64_t t, uint64_t seq, int32_t src, int32_t tgt,
                         const char* ability, int32_t roll_pm, const char* outcome, bool crit,
                         bool blocked, int32_t damage, int32_t src_rage_deci_after,
                         int32_t tgt_rage_deci_after, int32_t tgt_hp_after) {
    fprintf(f,
            "{\"type\":\"ability\",\"t\":%" PRId64 ",\"seq\":%" PRIu64
            ",\"src\":%d,\"tgt\":%d,\"ability\":\"%s\",\"roll_pm\":%d,\"outcome\":\"%s\","
            "\"crit\":%s,\"blocked\":%s,\"damage\":%d,\"src_rage_deci\":%d,"
            "\"tgt_rage_deci\":%d,\"tgt_hp\":%d}\n",
            t, seq, src, tgt, ability, roll_pm, outcome, crit ? "true" : "false",
            blocked ? "true" : "false", damage, src_rage_deci_after, tgt_rage_deci_after,
            tgt_hp_after);
}

void trace_write_swing(FILE* f, int64_t t, uint64_t seq, int32_t src, int32_t tgt,
                       int32_t roll_pm, const char* outcome, int32_t damage,
                       int32_t src_rage_deci_after, int32_t tgt_rage_deci_after,
                       int32_t tgt_hp_after) {
    fprintf(f,
            "{\"type\":\"swing\",\"t\":%" PRId64 ",\"seq\":%" PRIu64
            ",\"src\":%d,\"tgt\":%d,\"roll_pm\":%d,\"outcome\":\"%s\",\"damage\":%d,"
            "\"src_rage_deci\":%d,\"tgt_rage_deci\":%d,\"tgt_hp\":%d}\n",
            t, seq, src, tgt, roll_pm, outcome, damage, src_rage_deci_after, tgt_rage_deci_after,
            tgt_hp_after);
}

void trace_write_checkpoint(FILE* f, int64_t t, uint64_t state_hash) {
    fprintf(f, "{\"type\":\"checkpoint\",\"t\":%" PRId64 ",\"hash\":\"0x%016" PRIx64 "\"}\n", t,
            state_hash);
}

void trace_write_end(FILE* f, int64_t t, const char* reason, uint64_t swings,
                     uint64_t checkpoints) {
    fprintf(f,
            "{\"type\":\"end\",\"t\":%" PRId64 ",\"reason\":\"%s\",\"swings\":%" PRIu64
            ",\"checkpoints\":%" PRIu64 "}\n",
            t, reason, swings, checkpoints);
}

namespace {

// Returns pointer just past `"key":`, or nullptr.
const char* find_value(const std::string& line, const char* key) {
    std::string pat = "\"";
    pat += key;
    pat += "\":";
    size_t pos = line.find(pat);
    if (pos == std::string::npos) return nullptr;
    return line.c_str() + pos + pat.size();
}

} // namespace

bool trace_get_string(const std::string& line, const char* key, std::string& out) {
    const char* p = find_value(line, key);
    if (!p || *p != '"') return false;
    ++p;
    const char* e = strchr(p, '"');
    if (!e) return false;
    out.assign(p, e - p);
    return true;
}

bool trace_get_i64(const std::string& line, const char* key, int64_t& out) {
    const char* p = find_value(line, key);
    if (!p) return false;
    char* end = nullptr;
    out = strtoll(p, &end, 10);
    return end != p;
}

bool trace_get_u64(const std::string& line, const char* key, uint64_t& out) {
    const char* p = find_value(line, key);
    if (!p) return false;
    char* end = nullptr;
    out = strtoull(p, &end, 10);
    return end != p;
}

bool trace_get_hash(const std::string& line, const char* key, uint64_t& out) {
    std::string s;
    if (!trace_get_string(line, key, s)) return false;
    if (s.size() < 3 || s[0] != '0' || s[1] != 'x') return false;
    char* end = nullptr;
    out = strtoull(s.c_str() + 2, &end, 16);
    return end && *end == '\0';
}

} // namespace arena
