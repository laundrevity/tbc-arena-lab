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

#include "sim/env/arena_c_api.h"

#include <memory>
#include <string>

#include "sim/mechanics/match.h"
#include "sim/mechanics/ruleset.h"
#include "sim/mechanics/trace_sink.h"

#ifndef SIM_COMMIT
#define SIM_COMMIT "unknown"
#endif

using namespace arena;

namespace {
std::string g_last_error;
} // namespace

struct arena_env {
    Scenario scenario;
    FILE* trace_file = nullptr;
    TraceFileSink sink;
    std::unique_ptr<MatchEngine<TraceFileSink>> engine;

    ~arena_env() {
        engine.reset();  // flush on_end before closing
        if (trace_file) fclose(trace_file);
    }
};

extern "C" {

int32_t arena_api_version(void) { return 1; }
uint64_t arena_ruleset_hash(void) { return ruleset_hash(); }
const char* arena_sim_commit(void) { return SIM_COMMIT; }
const char* arena_last_error(void) { return g_last_error.c_str(); }

arena_env* arena_env_create(const char* scenario_path, uint64_t seed, const char* trace_path) {
    if (scenario_path == nullptr) {
        g_last_error = "scenario_path is null";
        return nullptr;
    }
    auto env = std::make_unique<arena_env>();
    std::string err;
    if (!load_scenario(scenario_path, env->scenario, err)) {
        g_last_error = err;
        return nullptr;
    }
    if (env->scenario.ruleset != ruleset_id()) {
        g_last_error = "scenario pins ruleset '" + env->scenario.ruleset +
                       "' but this build is '" + ruleset_id() + "'";
        return nullptr;
    }
    if (trace_path != nullptr) {
        env->trace_file = fopen(trace_path, "w");
        if (!env->trace_file) {
            g_last_error = std::string("cannot open trace file: ") + trace_path;
            return nullptr;
        }
        write_trace_prologue(env->trace_file, SIM_COMMIT, env->scenario, seed);
    }
    env->sink.f = env->trace_file;
    // External drivers always get decision ticks, regardless of the
    // scenario's pinned policies (which the driver replaces).
    env->engine = std::make_unique<MatchEngine<TraceFileSink>>(env->scenario, seed, env->sink,
                                                               /*enable_ticks=*/true);
    return env.release();
}

void arena_env_destroy(arena_env* env) { delete env; }

int32_t arena_env_status(const arena_env* env) {
    return static_cast<int32_t>(env->engine->status());
}

int64_t arena_env_time(const arena_env* env) {
    return env->engine->status() == EngineStatus::Ended ? env->engine->result().end_ms
                                                        : env->engine->now();
}

int32_t arena_env_observe(const arena_env* env, int32_t slot, int64_t* out) {
    if (slot < 0 || slot > 1 || out == nullptr) return -1;
    const Observation o = env->engine->observe(slot);
    int64_t* p = out;
    *p++ = o.t_ms;
    *p++ = o.match_remaining_ms;
    *p++ = o.self_hp;
    *p++ = o.self_max_hp;
    *p++ = o.self_rage_deci;
    *p++ = o.self_gcd_remaining_ms;
    *p++ = o.self_ms_cd_remaining_ms;
    *p++ = o.self_swing_remaining_ms;
    *p++ = o.self_hs_queued;
    *p++ = o.self_knows_ms;
    *p++ = o.self_knows_hs;
    *p++ = o.self_weapon_speed_ms;
    *p++ = o.enemy_hp_pm;
    *p++ = o.enemy_rage_deci;
    *p++ = o.enemy_in_self_front;
    *p++ = o.self_in_enemy_front;
    *p++ = o.distance_cm;
    return 0;
}

uint32_t arena_env_action_mask(const arena_env* env, int32_t slot) {
    if (slot < 0 || slot > 1) return 0;
    return legal_action_mask(env->engine->observe(slot));
}

int32_t arena_env_step(arena_env* env, int32_t action_slot0, int32_t action_slot1) {
    if (env->engine->status() != EngineStatus::AwaitingActions) return -1;
    if (action_slot0 < 0 || action_slot0 >= ARENA_ACTION_COUNT || action_slot1 < 0 ||
        action_slot1 >= ARENA_ACTION_COUNT) {
        return -2;
    }
    return static_cast<int32_t>(env->engine->step(static_cast<Action>(action_slot0),
                                                  static_cast<Action>(action_slot1)));
}

int32_t arena_env_end_reason(const arena_env* env) {
    return std::string(env->engine->result().end_reason) == "death" ? 1 : 0;
}

int32_t arena_env_winner_slot(const arena_env* env) {
    if (env->engine->status() != EngineStatus::Ended || arena_env_end_reason(env) != 1) {
        return -1;
    }
    return env->engine->state(0).hp > 0 ? 0 : 1;
}

int32_t arena_env_entity_id(const arena_env* env, int32_t slot) {
    if (slot < 0 || slot > 1) return -1;
    return env->engine->entity_id(slot);
}

int32_t arena_env_hp(const arena_env* env, int32_t slot) {
    if (slot < 0 || slot > 1) return -1;
    return env->engine->state(slot).hp;
}

int64_t arena_env_counter(const arena_env* env, int32_t which) {
    const MatchResult& r = env->engine->result();
    switch (which) {
        case 0: return static_cast<int64_t>(r.swings);
        case 1: return static_cast<int64_t>(r.abilities);
        case 2: return static_cast<int64_t>(r.decisions);
        case 3: return static_cast<int64_t>(r.illegal_actions);
        case 4: return static_cast<int64_t>(r.checkpoints);
        default: return -1;
    }
}

int32_t arena_run_scripted(const char* scenario_path, uint64_t seed, int64_t* out) {
    if (scenario_path == nullptr || out == nullptr) return -1;
    Scenario sc;
    std::string err;
    if (!load_scenario(scenario_path, sc, err)) {
        g_last_error = err;
        return -1;
    }
    NullSink sink;
    const MatchResult r = run_match(sc, seed, sink);
    out[0] = r.end_ms;
    out[1] = std::string(r.end_reason) == "death" ? 1 : 0;
    out[2] = static_cast<int64_t>(r.swings);
    out[3] = static_cast<int64_t>(r.abilities);
    out[4] = static_cast<int64_t>(r.decisions);
    return 0;
}

} // extern "C"
