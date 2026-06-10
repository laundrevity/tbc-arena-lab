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

#ifndef TBC_ARENA_LAB_SIM_ENV_ARENA_C_API_H
#define TBC_ARENA_LAB_SIM_ENV_ARENA_C_API_H

// C ABI over the M3 match engine (docs/observation_action_spec.md,
// docs/python_bindings.md). Drivers create an env per match, observe both
// slots, submit one action per slot per decision tick, and read results.
// Slots are ascending entity_id. All functions are single-threaded per env.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Observation layout: ARENA_OBS_SIZE int64 values in the canonical field
// order of docs/observation_action_spec.md (same order as
// Observation::serialize).
#define ARENA_OBS_SIZE 17

// Action ids (== arena::Action).
#define ARENA_ACTION_NONE 0
#define ARENA_ACTION_CAST_MORTAL_STRIKE 1
#define ARENA_ACTION_QUEUE_HEROIC_STRIKE 2
#define ARENA_ACTION_UNQUEUE_HEROIC_STRIKE 3
#define ARENA_ACTION_COUNT 4

// Engine status (== arena::EngineStatus).
#define ARENA_STATUS_AWAITING_ACTIONS 0
#define ARENA_STATUS_ENDED 1

typedef struct arena_env arena_env;

// Library/ruleset identification for wrapper-side safety checks.
int32_t arena_api_version(void);          // bumped on ABI changes; currently 1
uint64_t arena_ruleset_hash(void);        // FNV of the mechanics manifest
const char* arena_sim_commit(void);

// Last error message for a failed arena_env_create (thread-local-free:
// single global, fine for the single-threaded contract).
const char* arena_last_error(void);

// Creates an env from a scenario file; decision ticks always run. NULL on
// failure (see arena_last_error). trace_path may be NULL; otherwise a full
// JSONL trace (replayable by arena_replay) is written there.
arena_env* arena_env_create(const char* scenario_path, uint64_t seed,
                            const char* trace_path);
void arena_env_destroy(arena_env* env);

int32_t arena_env_status(const arena_env* env);
int64_t arena_env_time(const arena_env* env);  // current tick (or end) time, ms

// Fills out[0..ARENA_OBS_SIZE) for slot 0/1; returns 0 on success.
int32_t arena_env_observe(const arena_env* env, int32_t slot, int64_t* out);
uint32_t arena_env_action_mask(const arena_env* env, int32_t slot);

// Applies both actions at the current tick; returns the new status, or a
// negative value if the env was not awaiting actions.
int32_t arena_env_step(arena_env* env, int32_t action_slot0, int32_t action_slot1);

// Result accessors (valid once ENDED; counters valid any time).
int32_t arena_env_end_reason(const arena_env* env);  // 0 duration, 1 death
// Slot whose unit survived a death-ending match; -1 when ended by duration
// (or not ended yet).
int32_t arena_env_winner_slot(const arena_env* env);
int32_t arena_env_entity_id(const arena_env* env, int32_t slot);
int32_t arena_env_hp(const arena_env* env, int32_t slot);
int64_t arena_env_counter(const arena_env* env, int32_t which);
// which: 0 swings, 1 abilities, 2 decisions, 3 illegal_actions, 4 checkpoints

// Reference run: executes the scenario's own pinned policies via run_match
// and fills out[0..4] = {end_ms, end_reason, swings, abilities, decisions}.
// Lets wrappers cross-check that driving the env externally reproduces the
// native path. Returns 0 on success.
int32_t arena_run_scripted(const char* scenario_path, uint64_t seed, int64_t* out);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // TBC_ARENA_LAB_SIM_ENV_ARENA_C_API_H
