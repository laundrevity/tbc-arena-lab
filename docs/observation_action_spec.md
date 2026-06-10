# Observation & Action-Space Spec — tbc-arena-lab (M3)

The RL-facing interface: what an agent observes, what it may do, and exactly
when. This document is normative the way `docs/mechanics_spec.md` is for
game formulas; the implementation lives in `sim/mechanics/observation.h` and
`sim/mechanics/policy.h`. No Python bindings, no learning code, no networking
in M3 — this is the deterministic substrate they will bind to.

## Design principles

1. **Client parity.** An agent observes exactly what a human player at the
   TBC client could know at that moment — no more (no server-side hidden
   state like enemy cooldowns), no less. This keeps one interface valid for
   all three project goals: self-play (sim-side), learning from watched real
   matches (the same observation is reconstructible from client/combat-log
   data), and eventual live participation.
2. **Integer-only.** Observations are int32/int64 fields derived from
   authoritative state (CLAUDE.md rule 3). No floats anywhere.
3. **Masks from observations.** Action legality is computable from the
   observation alone — an agent never needs engine internals to know what it
   may do.
4. **Determinism.** Policies must be deterministic functions of the
   observation (a stochastic agent must key its own counter-based RNG on
   `(seed, entity, tick)`); given scenario + seed + policies, the match is
   bit-reproducible, and recorded decisions can be played back exactly.

## Decision model

- Decisions happen on a fixed **decision tick**, pinned per scenario
  (`decision_tick_ms`, 100 ms in current fixtures), as `Decide` events in
  the queue. The total order places ticks AFTER checkpoints/match-end and
  BEFORE swings at the same timestamp (`event_queue.h`).
- Ticks run at t = 0, tick, 2·tick, … while the match lasts. If every unit's
  policy is `idle`, the tick chain is not scheduled at all (pure auto-attack
  scenarios stay tick-free; this consumes no RNG either way).
- At each tick, both units are given observations **snapshotted before any
  tick action applies** (simultaneous-move semantics), then actions apply in
  ascending entity_id order (same tie principle as D-018). An action that
  became illegal due to the other unit's same-tick action degrades to
  `None` and is counted in `MatchResult.illegal_actions`.
- One action per unit per tick. Multi-action ticks (e.g. cast + queue in the
  same 100 ms) are future work (multi-discrete); the scripted baseline
  policy spreads them across consecutive ticks.
- Between ticks the engine state machine runs as specced: queued Heroic
  Strike fires at swing time (M-015), swings/parry-haste/cooldowns proceed
  without agent involvement. The agent acts at tick granularity only — this
  models reaction latency at tick resolution (an explicit latency model is
  future work).

## Observation (canonical field order)

All "remaining" fields are clamped at 0 and measured from the tick
timestamp. Visibility rationale per field:

| field | type | visibility rationale |
|---|---|---|
| `t_ms` | i64 | match clock, client-visible |
| `match_remaining_ms` | i64 | arena timer, client-visible |
| `self_hp` / `self_max_hp` | i32 | own unit frame, exact |
| `self_rage_deci` | i32 | own resource bar, exact |
| `self_gcd_remaining_ms` | i64 | own spinner, exact |
| `self_ms_cd_remaining_ms` | i64 | own cooldown, exact; -1 when MS unknown |
| `self_swing_remaining_ms` | i64 | own swing timer (client-derivable; addons did) |
| `self_hs_queued` | i32 | own action button glow, exact |
| `self_knows_ms` / `self_knows_hs` | i32 | own spellbook (loadout, static) |
| `self_weapon_speed_ms` | i32 | own character sheet (static) |
| `enemy_hp_pm` | i32 | hostile frames show FRACTIONS: floor(hp·10000/max), per-myriad |
| `enemy_rage_deci` | i32 | target resource bar IS visible on hostile targets |
| `enemy_in_self_front` / `self_in_enemy_front` | i32 | world-visible positions/facings |
| `distance_cm` | i64 | world-visible; integer isqrt of squared distance |

Deliberately EXCLUDED (server-side hidden from clients): enemy cooldowns,
enemy GCD, enemy swing timer, enemy Heroic Strike queue, enemy exact max/cur
HP. Deliberately deferred (reconstructible but not yet modeled): combat-log
event windows (recent outcomes/damage), buffs/auras (none exist yet).

`Observation::serialize` appends the fields in the table order as canonical
little-endian bytes (same convention as `unit_state.h`) so observations can
be hashed, golden-tested, and later flat-encoded for bindings without a
second source of truth.

## Actions and legality mask

```
0  None                 always legal
1  CastMortalStrike     knows_ms && rage >= 300 && ms_cd_remaining == 0 && gcd_remaining == 0
2  QueueHeroicStrike    knows_hs && !hs_queued      (queueing is free, M-015)
3  UnqueueHeroicStrike  knows_hs && hs_queued
```

`legal_action_mask(obs)` returns the bitmask (bit i = action i legal),
computed from the observation only. The engine re-validates on application;
an illegal submission becomes `None` and increments `illegal_actions`
(policy bug signal — the scripted policies never trigger it).

## Policies

`Policy` is the single decision interface: `Action decide(const
Observation&)`. Built-ins:

- `IdlePolicy` — always `None` (M0/M1 units).
- `ScriptedPolicy` — the deterministic baseline, priority order:
  CastMortalStrike if legal; else QueueHeroicStrike if legal and
  `rage >= scripted_hs_min_rage_deci` (scenario knob, 0 = never); else None.
- `PlaybackPolicy` — replays a recorded `(t, action)` stream verbatim; the
  substrate for imitation from watched matches and for re-running
  externally-driven games.

Scenario files pin loadout (`knows_mortal_strike`, `knows_heroic_strike`),
the policy name (`policy: idle | scripted`) and scripted parameters
(`scripted_hs_min_rage_deci`). `run_match` builds policies from the
scenario by default; callers (tests, future bindings) may inject any
`Policy*` pair.

## Tracing and replay

Applied non-`None` actions emit a `decision` trace line
(`{"type":"decision","t":..,"unit":..,"action":..}`); a tick without a line
means `None` for both units, so the full action stream is reconstructible
from the trace. `arena_replay` re-simulates with the scenario's policies and
verifies decision counts along with checkpoints/swings/abilities. The
playback test (record → replay actions through `PlaybackPolicy` → identical
checkpoints) is the round-trip guarantee.

## Future work (explicitly out of M3)

Latency/reaction-time model beyond tick granularity; combat-log observation
windows; aura/buff fields; multi-discrete actions; flat tensor encoding +
Python bindings; reward specification (terminal win/loss exists implicitly
via match end; shaped rewards are a training-side concern, not engine).
