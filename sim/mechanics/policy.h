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

#include <map>

#include "sim/mechanics/observation.h"

// The single decision interface (docs/observation_action_spec.md). Policies
// MUST be deterministic functions of the observation; a stochastic agent
// keys its own counter-based RNG on (seed, entity, tick).

namespace arena {

struct Policy {
    virtual ~Policy() = default;
    virtual Action decide(const Observation& obs) = 0;
};

struct IdlePolicy final : Policy {
    Action decide(const Observation&) override { return Action::None; }
};

// Deterministic baseline: Mortal Strike whenever legal, else queue Heroic
// Strike at the scenario rage threshold (0 = never).
struct ScriptedPolicy final : Policy {
    int32_t hs_min_rage_deci = 0;

    explicit ScriptedPolicy(int32_t hs_threshold_deci) : hs_min_rage_deci(hs_threshold_deci) {}

    Action decide(const Observation& obs) override {
        const uint32_t mask = legal_action_mask(obs);
        if (mask & action_bit(Action::CastMortalStrike)) return Action::CastMortalStrike;
        if ((mask & action_bit(Action::QueueHeroicStrike)) && hs_min_rage_deci > 0 &&
            obs.self_rage_deci >= hs_min_rage_deci) {
            return Action::QueueHeroicStrike;
        }
        return Action::None;
    }
};

// Replays a recorded (tick timestamp -> action) stream verbatim; ticks with
// no entry are None. Substrate for imitation from recorded matches and for
// re-running externally-driven games (spec "Tracing and replay").
struct PlaybackPolicy final : Policy {
    std::map<int64_t, Action> script;

    Action decide(const Observation& obs) override {
        const auto it = script.find(obs.t_ms);
        return it == script.end() ? Action::None : it->second;
    }
};

} // namespace arena
