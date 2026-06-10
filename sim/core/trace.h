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
#include <cstdio>
#include <string>

#include "sim/core/unit_state.h"

// JSONL trace format (CLAUDE.md conventions): one event per line.
//   header     {"type":"header","sim_commit":..,"ruleset":..,"ruleset_hash":..,
//               "scenario":..,"scenario_name":..,"seed":..,"duration_ms":..}
//   init       {"type":"init","t":0,"hash":..,"units":[{..dynamic state..}]}
//   swing      {"type":"swing","t":..,"seq":..,"src":..,"tgt":..,"roll_pm":..,
//               "outcome":..,"damage":..,"src_rage_deci":..,"tgt_rage_deci":..,
//               "tgt_hp":..}   (rage/hp fields are AFTER the swing applies)
//   ability    {"type":"ability","t":..,"seq":..,"src":..,"tgt":..,
//               "ability":..,"roll_pm":..,"outcome":..,"crit":..,"blocked":..,
//               "damage":..,"src_rage_deci":..,"tgt_rage_deci":..,"tgt_hp":..}
//   checkpoint {"type":"checkpoint","t":..,"hash":..}
//   end        {"type":"end","t":..,"reason":..,"swings":..,"checkpoints":..}
// Hashes are "0x%016x" strings. Strings we emit contain no JSON escapes;
// scenario paths must not contain '"' or '\'.

namespace arena {

void trace_write_header(FILE* f, const char* sim_commit, const char* ruleset,
                        uint64_t ruleset_hash, const std::string& scenario_path,
                        const std::string& scenario_name, uint64_t seed, int64_t duration_ms);

void trace_write_init(FILE* f, uint64_t state_hash, const int32_t* ids, const UnitState* states,
                      size_t count);

void trace_write_swing(FILE* f, int64_t t, uint64_t seq, int32_t src, int32_t tgt,
                       int32_t roll_pm, const char* outcome, int32_t damage,
                       int32_t src_rage_deci_after, int32_t tgt_rage_deci_after,
                       int32_t tgt_hp_after);

void trace_write_ability(FILE* f, int64_t t, uint64_t seq, int32_t src, int32_t tgt,
                         const char* ability, int32_t roll_pm, const char* outcome, bool crit,
                         bool blocked, int32_t damage, int32_t src_rage_deci_after,
                         int32_t tgt_rage_deci_after, int32_t tgt_hp_after);

void trace_write_checkpoint(FILE* f, int64_t t, uint64_t state_hash);

void trace_write_end(FILE* f, int64_t t, const char* reason, uint64_t swings,
                     uint64_t checkpoints);

// --- Minimal field extraction for replaying our own traces. ---
// These only understand the exact format written above (flat keys, integer
// values, quoted strings without escapes).

bool trace_get_string(const std::string& line, const char* key, std::string& out);
bool trace_get_i64(const std::string& line, const char* key, int64_t& out);
bool trace_get_u64(const std::string& line, const char* key, uint64_t& out);
// Parses a "0x..." quoted hash value.
bool trace_get_hash(const std::string& line, const char* key, uint64_t& out);

} // namespace arena
