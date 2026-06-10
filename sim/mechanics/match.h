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
#include <vector>

#include "sim/core/event_queue.h"
#include "sim/core/fixed_trig.h"
#include "sim/core/scenario.h"
#include "sim/core/unit_state.h"
#include "sim/mechanics/abilities.h"
#include "sim/mechanics/parry_haste.h"
#include "sim/mechanics/swing.h"

// Deterministic match runner. Authoritative time is int64 ms; no wall-clock
// reads anywhere in sim/ (CLAUDE.md rule 3). Hash checkpoints fire every
// authoritative 1000 ms, before any same-timestamp combat event (the total
// order lives in sim/core/event_queue.h). Policy evaluation (spec "Policy
// knobs") runs after every state-changing event and at scheduled Decide
// wake-ups; instant abilities resolve inline at the current timestamp.

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

struct MatchResult {
    int64_t end_ms = 0;
    const char* end_reason = "duration";  // "duration" | "death"
    uint64_t swings = 0;
    uint64_t abilities = 0;
    uint64_t checkpoints = 0;
    uint64_t initial_hash = 0;
};

// Sink contract:
//   void on_swing(const SwingRecord&);
//   void on_ability(const AbilityRecord&);
//   void on_checkpoint(int64_t t, uint64_t hash);
//   void on_end(int64_t t, const char* reason, uint64_t swings, uint64_t checkpoints);
// The header/init lines are the caller's business (they need scenario and
// build metadata the kernel does not own).

struct CollectSink {
    std::vector<SwingRecord> swings;
    std::vector<AbilityRecord> abilities;
    std::vector<std::pair<int64_t, uint64_t>> checkpoints;
    int64_t end_t = -1;
    std::string end_reason;

    void on_swing(const SwingRecord& r) { swings.push_back(r); }
    void on_ability(const AbilityRecord& r) { abilities.push_back(r); }
    void on_checkpoint(int64_t t, uint64_t h) { checkpoints.emplace_back(t, h); }
    void on_end(int64_t t, const char* reason, uint64_t, uint64_t) {
        end_t = t;
        end_reason = reason;
    }
};

struct NullSink {
    void on_swing(const SwingRecord&) {}
    void on_ability(const AbilityRecord&) {}
    void on_checkpoint(int64_t, uint64_t) {}
    void on_end(int64_t, const char*, uint64_t, uint64_t) {}
};

template <typename Sink>
MatchResult run_match(const Scenario& sc, uint64_t seed, Sink&& sink) {
    // Index units so iteration and hashing are in ascending entity_id order
    // (deterministic-iteration rule; M0 has exactly two units).
    const UnitSpec* specs[2];
    if (sc.attacker.entity_id < sc.defender.entity_id) {
        specs[0] = &sc.attacker;
        specs[1] = &sc.defender;
    } else {
        specs[0] = &sc.defender;
        specs[1] = &sc.attacker;
    }
    int32_t ids[2] = {specs[0]->entity_id, specs[1]->entity_id};
    UnitState states[2] = {initial_state(*specs[0]), initial_state(*specs[1])};
    RngCursor cursors[2];

    auto index_of = [&](int32_t entity_id) { return entity_id == ids[0] ? 0 : 1; };

    // Live events at any moment: one swing per attacker (plus at most one
    // stale superseded swing each), one Decide per unit (plus one stale),
    // checkpoint chain and match end. 16 is comfortably above that.
    EventQueue queue(16);
    std::vector<uint8_t> hash_buf;
    hash_buf.reserve(160);

    MatchResult res;
    res.initial_hash = hash_units(hash_buf, ids, states, 2);

    auto facing_against = [&](int atk, int def) {
        return mutual_frontal_arc(states[def].pos_x_cm, states[def].pos_y_cm,
                                  states[def].facing_mrad, states[atk].pos_x_cm,
                                  states[atk].pos_y_cm, states[atk].facing_mrad)
                   ? FacingClass::Front
                   : FacingClass::Behind;
    };

    // Casts Mortal Strike for unit i at time t. Returns true if the target
    // died. The cast consumes full rage regardless of outcome (spec M-016).
    auto cast_mortal_strike = [&](int i, int64_t t, uint64_t seq) {
        const int tgt = i == 0 ? 1 : 0;
        UnitState& self = states[i];
        self.rage_deci -= MS_RAGE_COST_DECI;
        self.ms_ready_ms = t + MS_COOLDOWN_MS;
        self.gcd_ready_ms = t + GCD_MS;  // spec M-011
        const YellowResult yr = resolve_yellow(*specs[i], *specs[tgt], facing_against(i, tgt),
                                               seed, cursors[i], /*normalized=*/true,
                                               MS_FLAT_BONUS);
        states[tgt].hp = std::max(0, states[tgt].hp - yr.damage);
        states[tgt].rage_deci = add_rage_deci(states[tgt].rage_deci, yr.rage_defender_deci);
        ++res.abilities;

        AbilityRecord rec;
        rec.t = t;
        rec.seq = seq;
        rec.src = ids[i];
        rec.tgt = ids[tgt];
        rec.ability = "mortal_strike";
        rec.result = yr;
        rec.src_rage_deci_after = self.rage_deci;
        rec.tgt_rage_deci_after = states[tgt].rage_deci;
        rec.tgt_hp_after = states[tgt].hp;
        sink.on_ability(rec);
        return states[tgt].hp <= 0;
    };

    // Re-evaluates both units' policies (entity order) until quiescent.
    // Returns true if a cast killed someone. Casting can enable the other
    // unit (victim rage), hence the fixpoint loop; the GCD bounds it.
    auto evaluate_policies = [&](int64_t t, uint64_t seq) {
        bool progress = true;
        while (progress) {
            progress = false;
            for (int i = 0; i < 2; ++i) {
                const UnitSpec& sp = *specs[i];
                UnitState& st = states[i];
                if (!sp.attacks || st.hp <= 0) continue;
                // Heroic Strike queue: free and off-GCD (spec M-015).
                if (sp.hs_min_rage_deci > 0 && !st.hs_queued &&
                    st.rage_deci >= sp.hs_min_rage_deci) {
                    st.hs_queued = 1;
                }
                if (sp.use_mortal_strike && st.rage_deci >= MS_RAGE_COST_DECI) {
                    if (t >= st.ms_ready_ms && t >= st.gcd_ready_ms) {
                        if (cast_mortal_strike(i, t, seq)) return true;
                        progress = true;
                    } else {
                        // Blocked only by known future times: schedule a
                        // Decide wake-up (lazy-invalidated via next_decide_ms).
                        const int64_t when = std::max(st.ms_ready_ms, st.gcd_ready_ms);
                        if (when > t && when != st.next_decide_ms) {
                            st.next_decide_ms = when;
                            queue.push(when, EventKind::Decide, ids[i], 0);
                        }
                    }
                }
            }
        }
        return false;
    };

    queue.push(sc.duration_ms, EventKind::MatchEnd, 0, 0);
    if (CHECKPOINT_INTERVAL_MS <= sc.duration_ms) {
        queue.push(CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint, 0, 0);
    }
    for (int i = 0; i < 2; ++i) {
        if (specs[i]->attacks) {
            const int32_t target = ids[i == 0 ? 1 : 0];
            queue.push(states[i].next_swing_ms, EventKind::Swing, ids[i], target);
        }
    }

    auto end_by_death = [&](int64_t t) {
        res.end_ms = t;
        res.end_reason = "death";
        sink.on_end(t, res.end_reason, res.swings, res.checkpoints);
        return res;
    };

    while (!queue.empty()) {
        const Event ev = queue.pop();
        switch (ev.kind) {
            case EventKind::Checkpoint: {
                const uint64_t h = hash_units(hash_buf, ids, states, 2);
                ++res.checkpoints;
                sink.on_checkpoint(ev.time_ms, h);
                if (ev.time_ms + CHECKPOINT_INTERVAL_MS <= sc.duration_ms) {
                    queue.push(ev.time_ms + CHECKPOINT_INTERVAL_MS, EventKind::Checkpoint, 0, 0);
                }
                break;
            }
            case EventKind::MatchEnd: {
                res.end_ms = ev.time_ms;
                res.end_reason = "duration";
                sink.on_end(ev.time_ms, res.end_reason, res.swings, res.checkpoints);
                return res;
            }
            case EventKind::Decide: {
                const int i = index_of(ev.source_id);
                if (ev.time_ms != states[i].next_decide_ms) break;  // superseded
                states[i].next_decide_ms = -1;
                if (evaluate_policies(ev.time_ms, ev.seq)) return end_by_death(ev.time_ms);
                break;
            }
            case EventKind::Swing: {
                const int src = index_of(ev.source_id);
                const int tgt = index_of(ev.target_id);
                // Lazy invalidation (spec M-007): parry-haste retimes swings
                // by pushing a new event; the superseded one no longer
                // matches the authoritative ready-at and is skipped without
                // consuming RNG or emitting trace events.
                if (ev.time_ms != states[src].next_swing_ms) break;
                states[src].next_swing_ms = ev.time_ms + specs[src]->weapon_speed_ms;

                // Heroic Strike replaces the swing when queued and payable;
                // otherwise the queue clears and the white swing proceeds
                // (spec M-015).
                if (states[src].hs_queued) {
                    states[src].hs_queued = 0;
                    if (states[src].rage_deci >= HS_RAGE_COST_DECI) {
                        states[src].rage_deci -= HS_RAGE_COST_DECI;
                        const YellowResult yr = resolve_yellow(
                            *specs[src], *specs[tgt], facing_against(src, tgt), seed,
                            cursors[src], /*normalized=*/false, HS_FLAT_BONUS);
                        states[tgt].hp = std::max(0, states[tgt].hp - yr.damage);
                        states[tgt].rage_deci =
                            add_rage_deci(states[tgt].rage_deci, yr.rage_defender_deci);
                        ++res.abilities;

                        AbilityRecord rec;
                        rec.t = ev.time_ms;
                        rec.seq = ev.seq;
                        rec.src = ev.source_id;
                        rec.tgt = ev.target_id;
                        rec.ability = "heroic_strike";
                        rec.result = yr;
                        rec.src_rage_deci_after = states[src].rage_deci;
                        rec.tgt_rage_deci_after = states[tgt].rage_deci;
                        rec.tgt_hp_after = states[tgt].hp;
                        sink.on_ability(rec);

                        if (states[tgt].hp <= 0) return end_by_death(ev.time_ms);
                        // Parried abilities do NOT parry-haste (spec M-012).
                        queue.push(states[src].next_swing_ms, EventKind::Swing, ev.source_id,
                                   ev.target_id);
                        if (evaluate_policies(ev.time_ms, ev.seq))
                            return end_by_death(ev.time_ms);
                        break;
                    }
                }

                const SwingResult sw = resolve_swing(*specs[src], *specs[tgt],
                                                     facing_against(src, tgt), seed,
                                                     cursors[src]);
                states[tgt].hp = std::max(0, states[tgt].hp - sw.damage);
                states[src].rage_deci = add_rage_deci(states[src].rage_deci, sw.rage_attacker_deci);
                states[tgt].rage_deci = add_rage_deci(states[tgt].rage_deci, sw.rage_defender_deci);
                // Parry-haste (spec M-010): the parrying unit's own pending
                // swing is accelerated, if it auto-attacks at all.
                if (sw.outcome == Outcome::Parry && states[tgt].next_swing_ms >= 0) {
                    const int64_t remaining = states[tgt].next_swing_ms - ev.time_ms;
                    const int64_t hastened =
                        parry_hastened_remaining(remaining, specs[tgt]->weapon_speed_ms);
                    if (hastened != remaining) {
                        states[tgt].next_swing_ms = ev.time_ms + hastened;
                        queue.push(states[tgt].next_swing_ms, EventKind::Swing, ids[tgt],
                                   ids[src]);
                    }
                }
                ++res.swings;

                SwingRecord rec;
                rec.t = ev.time_ms;
                rec.seq = ev.seq;
                rec.src = ev.source_id;
                rec.tgt = ev.target_id;
                rec.result = sw;
                rec.src_rage_deci_after = states[src].rage_deci;
                rec.tgt_rage_deci_after = states[tgt].rage_deci;
                rec.tgt_hp_after = states[tgt].hp;
                sink.on_swing(rec);

                if (states[tgt].hp <= 0) return end_by_death(ev.time_ms);
                queue.push(states[src].next_swing_ms, EventKind::Swing, ev.source_id,
                           ev.target_id);
                if (evaluate_policies(ev.time_ms, ev.seq)) return end_by_death(ev.time_ms);
                break;
            }
        }
    }
    // Unreachable: the MatchEnd event always terminates the loop.
    return res;
}

} // namespace arena
