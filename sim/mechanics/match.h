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
#include <string>
#include <type_traits>
#include <vector>

#include "sim/core/event_queue.h"
#include "sim/core/fixed_trig.h"
#include "sim/core/scenario.h"
#include "sim/core/unit_state.h"
#include "sim/mechanics/abilities.h"
#include "sim/mechanics/parry_haste.h"
#include "sim/mechanics/policy.h"
#include "sim/mechanics/swing.h"

// Deterministic match engine. Authoritative time is int64 ms; no wall-clock
// reads anywhere in sim/ (CLAUDE.md rule 3). Hash checkpoints fire every
// authoritative 1000 ms, before any same-timestamp combat event (the total
// order lives in sim/core/event_queue.h). Agent decisions happen on fixed
// decision ticks (Decide events; docs/observation_action_spec.md).
//
// MatchEngine is resumable: it runs events until a decision tick, pauses
// with both observations available (pre-action snapshots — simultaneous-move
// semantics), and resumes when step() supplies both actions (applied in
// ascending entity_id order). run_match() is a thin wrapper that drives the
// engine with Policy objects, so external drivers (the C API / Python
// bindings) and native runs share one implementation.

namespace arena {

constexpr int64_t CHECKPOINT_INTERVAL_MS = 1000;

struct SwingRecord {
    int64_t t = 0;
    uint64_t seq = 0;  // event seq (ordering only; RNG uses per-entity cursors)
    int32_t src = 0;
    int32_t tgt = 0;
    SwingResult result;
    int32_t src_rage_deci_after = 0;  // post-cap
    int32_t tgt_rage_deci_after = 0;
    int32_t tgt_hp_after = 0;
};

struct AbilityRecord {
    int64_t t = 0;
    uint64_t seq = 0;  // seq of the event whose resolution triggered the cast
    int32_t src = 0;
    int32_t tgt = 0;
    const char* ability = "";  // "mortal_strike" | "heroic_strike"
    YellowResult result;
    int32_t src_rage_deci_after = 0;
    int32_t tgt_rage_deci_after = 0;
    int32_t tgt_hp_after = 0;
};

// Applied non-None agent action; ticks without records mean None.
struct DecisionRecord {
    int64_t t = 0;
    int32_t unit = 0;  // entity id
    Action action = Action::None;
};

struct MatchResult {
    int64_t end_ms = 0;
    const char* end_reason = "duration";  // "duration" | "death"
    uint64_t swings = 0;
    uint64_t abilities = 0;
    uint64_t decisions = 0;        // applied non-None actions
    uint64_t illegal_actions = 0;  // submitted actions that failed validation
    uint64_t checkpoints = 0;
    uint64_t initial_hash = 0;
};

// Sink contract:
//   void on_swing(const SwingRecord&);
//   void on_ability(const AbilityRecord&);
//   void on_decision(const DecisionRecord&);
//   void on_checkpoint(int64_t t, uint64_t hash);
//   void on_end(int64_t t, const char* reason, uint64_t swings, uint64_t checkpoints);
// The header/init lines are the caller's business (they need scenario and
// build metadata the kernel does not own).

struct CollectSink {
    std::vector<SwingRecord> swings;
    std::vector<AbilityRecord> abilities;
    std::vector<DecisionRecord> decisions;
    std::vector<std::pair<int64_t, uint64_t>> checkpoints;
    int64_t end_t = -1;
    std::string end_reason;

    void on_swing(const SwingRecord& r) { swings.push_back(r); }
    void on_ability(const AbilityRecord& r) { abilities.push_back(r); }
    void on_decision(const DecisionRecord& r) { decisions.push_back(r); }
    void on_checkpoint(int64_t t, uint64_t h) { checkpoints.emplace_back(t, h); }
    void on_end(int64_t t, const char* reason, uint64_t, uint64_t) {
        end_t = t;
        end_reason = reason;
    }
};

struct NullSink {
    void on_swing(const SwingRecord&) {}
    void on_ability(const AbilityRecord&) {}
    void on_decision(const DecisionRecord&) {}
    void on_checkpoint(int64_t, uint64_t) {}
    void on_end(int64_t, const char*, uint64_t, uint64_t) {}
};

enum class EngineStatus : int32_t {
    AwaitingActions = 0,  // paused at a decision tick; observe() then step()
    Ended = 1,
};

// `sc` and `sink` are borrowed; the caller keeps them alive for the
// engine's lifetime. `enable_ticks=false` skips the decision-tick chain
// entirely (pure auto-attack runs); external drivers pass true.
template <typename Sink>
class MatchEngine {
public:
    MatchEngine(const Scenario& sc, uint64_t seed, Sink& sink, bool enable_ticks)
        : sc_(sc), seed_(seed), sink_(sink), queue_(16) {
        if (sc_.attacker.entity_id < sc_.defender.entity_id) {
            specs_[0] = &sc_.attacker;
            specs_[1] = &sc_.defender;
        } else {
            specs_[0] = &sc_.defender;
            specs_[1] = &sc_.attacker;
        }
        ids_[0] = specs_[0]->entity_id;
        ids_[1] = specs_[1]->entity_id;
        states_[0] = initial_state(*specs_[0]);
        states_[1] = initial_state(*specs_[1]);
        hash_buf_.reserve(160);
        res_.initial_hash = hash_units(hash_buf_, ids_, states_, 2);

        queue_.push(sc_.duration_ms, EventKind::MatchEnd, 0, 0);
        if (CHECKPOINT_INTERVAL_MS <= sc_.duration_ms) {
            queue_.push(CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint, 0, 0);
        }
        if (enable_ticks) {
            queue_.push(0, EventKind::Decide, 0, 0);
        }
        for (int i = 0; i < 2; ++i) {
            if (specs_[i]->attacks) {
                queue_.push(states_[i].next_swing_ms, EventKind::Swing, ids_[i],
                            ids_[i == 0 ? 1 : 0]);
            }
        }
        advance_();
    }

    EngineStatus status() const { return status_; }
    // Valid while AwaitingActions: the current decision-tick timestamp.
    int64_t now() const { return tick_t_; }
    const MatchResult& result() const { return res_; }
    int32_t entity_id(int slot) const { return ids_[slot]; }
    const UnitState& state(int slot) const { return states_[slot]; }

    // Pre-action snapshot for the unit in `slot` (ascending entity_id).
    // While Ended, returns the terminal observation at end time.
    Observation observe(int slot) const {
        const int enemy = slot == 0 ? 1 : 0;
        const int64_t t = status_ == EngineStatus::Ended ? res_.end_ms : tick_t_;
        return build_observation(*specs_[slot], states_[slot], *specs_[enemy], states_[enemy],
                                 t, sc_.duration_ms);
    }

    // Applies both staged actions in entity order, then advances to the next
    // decision tick or the end of the match. No-op when already Ended.
    EngineStatus step(Action a0, Action a1) {
        if (status_ != EngineStatus::AwaitingActions) return status_;
        const Action acts[2] = {a0, a1};
        for (int i = 0; i < 2; ++i) {
            if (apply_action_(i, acts[i], tick_t_, tick_seq_)) {
                end_by_death_(tick_t_);
                return status_;
            }
        }
        if (tick_t_ + sc_.decision_tick_ms < sc_.duration_ms) {
            queue_.push(tick_t_ + sc_.decision_tick_ms, EventKind::Decide, 0, 0);
        }
        advance_();
        return status_;
    }

private:
    FacingClass facing_against_(int atk, int def) const {
        return mutual_frontal_arc(states_[def].pos_x_cm, states_[def].pos_y_cm,
                                  states_[def].facing_mrad, states_[atk].pos_x_cm,
                                  states_[atk].pos_y_cm, states_[atk].facing_mrad)
                   ? FacingClass::Front
                   : FacingClass::Behind;
    }

    void end_by_death_(int64_t t) {
        res_.end_ms = t;
        res_.end_reason = "death";
        sink_.on_end(t, res_.end_reason, res_.swings, res_.checkpoints);
        status_ = EngineStatus::Ended;
    }

    // Casts Mortal Strike for unit i at time t; full cost regardless of
    // outcome (spec M-016). Returns true if the target died.
    bool cast_mortal_strike_(int i, int64_t t, uint64_t seq) {
        const int tgt = i == 0 ? 1 : 0;
        UnitState& self = states_[i];
        self.rage_deci -= MS_RAGE_COST_DECI;
        self.ms_ready_ms = t + MS_COOLDOWN_MS;
        self.gcd_ready_ms = t + GCD_MS;  // spec M-011
        const YellowResult yr =
            resolve_yellow(*specs_[i], *specs_[tgt], facing_against_(i, tgt), seed_,
                           cursors_[i], /*normalized=*/true, MS_FLAT_BONUS);
        states_[tgt].hp = std::max(0, states_[tgt].hp - yr.damage);
        states_[tgt].rage_deci = add_rage_deci(states_[tgt].rage_deci, yr.rage_defender_deci);
        ++res_.abilities;

        AbilityRecord rec;
        rec.t = t;
        rec.seq = seq;
        rec.src = ids_[i];
        rec.tgt = ids_[tgt];
        rec.ability = "mortal_strike";
        rec.result = yr;
        rec.src_rage_deci_after = self.rage_deci;
        rec.tgt_rage_deci_after = states_[tgt].rage_deci;
        rec.tgt_hp_after = states_[tgt].hp;
        sink_.on_ability(rec);
        return states_[tgt].hp <= 0;
    }

    // Applies one agent action; re-validates against authoritative state
    // (an action legal at observation time may have been invalidated by the
    // other unit's same-tick action). Returns true if someone died.
    bool apply_action_(int i, Action a, int64_t t, uint64_t seq) {
        if (a == Action::None) return false;
        const int enemy = i == 0 ? 1 : 0;
        const Observation now = build_observation(*specs_[i], states_[i], *specs_[enemy],
                                                  states_[enemy], t, sc_.duration_ms);
        if ((legal_action_mask(now) & action_bit(a)) == 0) {
            ++res_.illegal_actions;
            return false;
        }
        bool died = false;
        switch (a) {
            case Action::CastMortalStrike:
                died = cast_mortal_strike_(i, t, seq);
                break;
            case Action::QueueHeroicStrike:
                states_[i].hs_queued = 1;
                break;
            case Action::UnqueueHeroicStrike:
                states_[i].hs_queued = 0;
                break;
            case Action::None:
                break;
        }
        ++res_.decisions;
        DecisionRecord rec;
        rec.t = t;
        rec.unit = ids_[i];
        rec.action = a;
        sink_.on_decision(rec);
        return died;
    }

    // Processes events until pausing at a decision tick or ending the match.
    void advance_() {
        while (!queue_.empty()) {
            const Event ev = queue_.pop();
            switch (ev.kind) {
                case EventKind::Checkpoint: {
                    const uint64_t h = hash_units(hash_buf_, ids_, states_, 2);
                    ++res_.checkpoints;
                    sink_.on_checkpoint(ev.time_ms, h);
                    if (ev.time_ms + CHECKPOINT_INTERVAL_MS <= sc_.duration_ms) {
                        queue_.push(ev.time_ms + CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint,
                                    0, 0);
                    }
                    break;
                }
                case EventKind::MatchEnd: {
                    res_.end_ms = ev.time_ms;
                    res_.end_reason = "duration";
                    sink_.on_end(ev.time_ms, res_.end_reason, res_.swings, res_.checkpoints);
                    status_ = EngineStatus::Ended;
                    return;
                }
                case EventKind::Decide: {
                    tick_t_ = ev.time_ms;
                    tick_seq_ = ev.seq;
                    status_ = EngineStatus::AwaitingActions;
                    return;
                }
                case EventKind::Swing: {
                    if (handle_swing_(ev)) return;  // someone died
                    break;
                }
            }
        }
        // Unreachable: the MatchEnd event always terminates the loop.
    }

    // Returns true if the match ended (death).
    bool handle_swing_(const Event& ev) {
        const int src = ev.source_id == ids_[0] ? 0 : 1;
        const int tgt = src == 0 ? 1 : 0;
        // Lazy invalidation (spec M-007): parry-haste retimes swings by
        // pushing a new event; the superseded one no longer matches the
        // authoritative ready-at and is skipped without consuming RNG or
        // emitting trace events.
        if (ev.time_ms != states_[src].next_swing_ms) return false;
        states_[src].next_swing_ms = ev.time_ms + specs_[src]->weapon_speed_ms;

        // Heroic Strike replaces the swing when queued and payable;
        // otherwise the queue clears and the white swing proceeds (M-015).
        if (states_[src].hs_queued) {
            states_[src].hs_queued = 0;
            if (states_[src].rage_deci >= HS_RAGE_COST_DECI) {
                states_[src].rage_deci -= HS_RAGE_COST_DECI;
                const YellowResult yr =
                    resolve_yellow(*specs_[src], *specs_[tgt], facing_against_(src, tgt), seed_,
                                   cursors_[src], /*normalized=*/false, HS_FLAT_BONUS);
                states_[tgt].hp = std::max(0, states_[tgt].hp - yr.damage);
                states_[tgt].rage_deci =
                    add_rage_deci(states_[tgt].rage_deci, yr.rage_defender_deci);
                ++res_.abilities;

                AbilityRecord rec;
                rec.t = ev.time_ms;
                rec.seq = ev.seq;
                rec.src = ev.source_id;
                rec.tgt = ev.target_id;
                rec.ability = "heroic_strike";
                rec.result = yr;
                rec.src_rage_deci_after = states_[src].rage_deci;
                rec.tgt_rage_deci_after = states_[tgt].rage_deci;
                rec.tgt_hp_after = states_[tgt].hp;
                sink_.on_ability(rec);

                if (states_[tgt].hp <= 0) {
                    end_by_death_(ev.time_ms);
                    return true;
                }
                // Parried abilities do NOT parry-haste (spec M-012).
                queue_.push(states_[src].next_swing_ms, EventKind::Swing, ev.source_id,
                            ev.target_id);
                return false;
            }
        }

        const SwingResult sw = resolve_swing(*specs_[src], *specs_[tgt],
                                             facing_against_(src, tgt), seed_, cursors_[src]);
        states_[tgt].hp = std::max(0, states_[tgt].hp - sw.damage);
        states_[src].rage_deci = add_rage_deci(states_[src].rage_deci, sw.rage_attacker_deci);
        states_[tgt].rage_deci = add_rage_deci(states_[tgt].rage_deci, sw.rage_defender_deci);
        // Parry-haste (spec M-010): the parrying unit's own pending swing is
        // accelerated, if it auto-attacks at all.
        if (sw.outcome == Outcome::Parry && states_[tgt].next_swing_ms >= 0) {
            const int64_t remaining = states_[tgt].next_swing_ms - ev.time_ms;
            const int64_t hastened =
                parry_hastened_remaining(remaining, specs_[tgt]->weapon_speed_ms);
            if (hastened != remaining) {
                states_[tgt].next_swing_ms = ev.time_ms + hastened;
                queue_.push(states_[tgt].next_swing_ms, EventKind::Swing, ids_[tgt], ids_[src]);
            }
        }
        ++res_.swings;

        SwingRecord rec;
        rec.t = ev.time_ms;
        rec.seq = ev.seq;
        rec.src = ev.source_id;
        rec.tgt = ev.target_id;
        rec.result = sw;
        rec.src_rage_deci_after = states_[src].rage_deci;
        rec.tgt_rage_deci_after = states_[tgt].rage_deci;
        rec.tgt_hp_after = states_[tgt].hp;
        sink_.on_swing(rec);

        if (states_[tgt].hp <= 0) {
            end_by_death_(ev.time_ms);
            return true;
        }
        queue_.push(states_[src].next_swing_ms, EventKind::Swing, ev.source_id, ev.target_id);
        return false;
    }

    const Scenario& sc_;
    const uint64_t seed_;
    Sink& sink_;
    const UnitSpec* specs_[2] = {nullptr, nullptr};
    int32_t ids_[2] = {0, 0};
    UnitState states_[2];
    RngCursor cursors_[2];
    EventQueue queue_;
    std::vector<uint8_t> hash_buf_;
    MatchResult res_;
    EngineStatus status_ = EngineStatus::Ended;
    int64_t tick_t_ = 0;
    uint64_t tick_seq_ = 0;
};

// Policies may be injected per unit (ascending entity_id order); nullptr
// builds the scenario-pinned policy (idle or scripted) for that slot.
template <typename Sink>
MatchResult run_match(const Scenario& sc, uint64_t seed, Sink&& sink,
                      Policy* policy_override_0 = nullptr, Policy* policy_override_1 = nullptr) {
    const UnitSpec* specs[2];
    if (sc.attacker.entity_id < sc.defender.entity_id) {
        specs[0] = &sc.attacker;
        specs[1] = &sc.defender;
    } else {
        specs[0] = &sc.defender;
        specs[1] = &sc.attacker;
    }
    IdlePolicy idle_policies[2];
    ScriptedPolicy scripted_policies[2] = {
        ScriptedPolicy(specs[0]->scripted_hs_min_rage_deci),
        ScriptedPolicy(specs[1]->scripted_hs_min_rage_deci)};
    Policy* overrides[2] = {policy_override_0, policy_override_1};
    Policy* policies[2];
    for (int i = 0; i < 2; ++i) {
        if (overrides[i] != nullptr) {
            policies[i] = overrides[i];
        } else if (specs[i]->policy == PolicyKind::Scripted) {
            policies[i] = &scripted_policies[i];
        } else {
            policies[i] = &idle_policies[i];
        }
    }
    // Pure auto-attack matches skip the tick chain entirely (no decisions to
    // make; consumes no RNG either way — spec "Decision model").
    const bool run_ticks = overrides[0] != nullptr || overrides[1] != nullptr ||
                           specs[0]->policy != PolicyKind::Idle ||
                           specs[1]->policy != PolicyKind::Idle;

    MatchEngine<typename std::remove_reference<Sink>::type> engine(sc, seed, sink, run_ticks);
    while (engine.status() == EngineStatus::AwaitingActions) {
        const Action a0 = policies[0]->decide(engine.observe(0));
        const Action a1 = policies[1]->decide(engine.observe(1));
        engine.step(a0, a1);
    }
    return engine.result();
}

} // namespace arena
