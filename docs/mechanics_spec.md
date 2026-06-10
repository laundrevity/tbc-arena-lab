# Mechanics Spec — tbc-arena-lab

Every game formula implemented in `sim/` has an entry here, written before or
alongside its implementation (CLAUDE.md rule 4). Each entry carries:

- **formula** — the exact math, including integer/rounding choices as implemented.
- **source_status** — one of `primary | secondary | empirical | emulator_reference | guess`.
- **known_uncertainties** — anything marked `TODO(verify)` plus open questions.
- **tests** — the tests that cover the entry.

Pinned ruleset: **Anniversary TBC 2.5.x**. Bootstrap oracle: **cmangos-tbc**
(its model of 2.4.3). Differences between those two are expected and live in
`docs/divergence_ledger.md`.

Units convention (see also `sim/core/unit_state.h`): time `int64` ms, positions
`int64` cm, facing `int32` milliradians, probabilities `int32` per-myriad
(1 pm = 0.01%), rage stored as deci-rage `int32` (0–1000 ⇒ 0–100 rage).

---

## M-001 White-hit single-roll attack table

**Formula.** One uniform roll `r ∈ [0, 10000)` per-myriad is allocated across
contiguous outcome ranges in this fixed order:

```
miss | dodge | parry | glancing | block | crit | crushing | ordinary hit
```

Range widths (per-myriad), level 70 attacker vs level 70 player defender:

- `miss   = max(0, 500 + (defense_skill - weapon_skill) * 4 - hit_pm)`
- `dodge  = max(0, dodge_pm  + (defense_skill - weapon_skill) * 4)` — frontal and rear? **No**: zero when attacker is behind defender (see M-008).
- `parry  = max(0, parry_pm  + (defense_skill - weapon_skill) * 4)` — zero unless attacker in defender's frontal arc.
- `glancing = 0` vs player targets, always (`is_player_target = true`).
- `block  = max(0, block_pm + (defense_skill - weapon_skill) * 4)` — zero unless defender has a shield equipped AND attacker is in the frontal arc.
- `crit   = max(0, crit_pm - (defense_skill - weapon_skill) * 4)`
- `crushing = 0` vs player targets, always (crushing is mob-attacker-only).
- `hit    = 10000 - (sum of the above)`, floored at 0; if the sum exceeds
  10000 the later ranges are truncated cumulatively (table is clamped left to
  right), so over-full tables squeeze crit, then hit, out — documented here as
  the single place that defines clamping.

Defender avoidance (`dodge_pm`, `parry_pm`, `block_pm`) and attacker `crit_pm`,
`hit_pm` are pinned directly in scenario files as per-myriad values; derivation
from agility/ratings is out of M0 scope. For reference, at level 70: 22.08 crit
rating = 1% crit, 15.77 hit rating = 1% hit (not modeled, documentation only).

**source_status:** `emulator_reference` (table order and single-roll mechanism:
cmangos-tbc `Unit::RollMeleeOutcomeAgainst`, Unit.cpp) + `secondary` (base 5%
miss vs equal-level player and the 0.04%/point defense-vs-weapon-skill step:
community-documented TBC values, e.g. 2.4-era EJ combat compendia).

**known_uncertainties:**
- `TODO(verify)` base miss 5.00% vs same-level player with equal weapon
  skill/defense (single-roll, one-weapon). Well-attested but to be confirmed
  against the oracle.
- `TODO(verify)` the 0.04%-per-point step applies symmetrically to miss,
  dodge, parry, block (+) and crit (−) for |defense − weapon skill| ≤ 10;
  the >10 regime (0.2%/point miss) is mob-oriented and NOT implemented.
- `TODO(verify)` whether blocked attacks can also crit ("blocked crits") —
  modeled here as mutually exclusive table rows (no blocked crits). Ledger D-010.
- Clamping order for over-full tables is a `guess` (left-to-right truncation);
  unexercised by M0 scenarios (their tables sum well under 10000).

**tests:** `test_attack_table.cpp` — exact range construction at both scenario
stat blocks; positional gating (behind ⇒ dodge/parry/block widths 0); PvP
exclusions (glancing/crushing widths 0); skill-delta adjustment at synthetic
355/350 and 350/360 stat blocks; clamping at a synthetic over-full table.
`arena_dist` self-test at N=10^6 (RNG + sampler vs builder, not external fidelity).

---

## M-002 Weapon damage roll

**Formula.** Per swing that makes contact (block / crit / ordinary hit):

```
base   = uniform_int[weapon_min, weapon_max]            (inclusive)
ap_bonus = floor(attack_power * weapon_speed_ms / 14000)  // AP/14 per second
damage = base + ap_bonus
```

All integer math; the AP bonus is floored once. Normalization (as for
instant attacks) does not apply to auto-attacks.

**source_status:** `secondary` (AP/14 per weapon-speed-second is the
universally documented TBC formula; cmangos-tbc `Unit::MeleeDamageBonusDone`
agrees) — flooring choices are ours, see ledger D-004.

**known_uncertainties:**
- `TODO(verify)` rounding: the client/emulator carry floats and round at
  display/application time; we floor at each integer division. Divergence is
  ≤1 damage per stage; ledgered (D-004).

**tests:** `test_damage_rage.cpp` — fixed inputs (AP 2800, speed 3600 ⇒
bonus 720; damage bounds 1040–1200 for the pinned weapon); distribution mean
within CI in `arena_dist` self-test.

---

## M-003 Melee critical strikes

**Formula.** `damage_crit = damage * 2` (multiplier 2.0 for white melee vs
players; resilience is pinned to 0 in M0 scenarios and not modeled).

Pipeline order with armor: armor reduction is applied first, then the crit
multiplier, then block value subtraction (M-005). Armor reduction and the ×2
commute up to flooring; the chosen order is fixed here so traces are stable.

**source_status:** `secondary` (2.0× melee crit is a primary-adjacent, fully
documented constant) / pipeline order `emulator_reference` with `TODO(verify)`.

**known_uncertainties:**
- `TODO(verify)` exact application order vs cmangos-tbc
  (`Unit::CalculateMeleeDamage`): armor-then-crit vs crit-then-armor differ
  only in flooring (≤1 damage). Ledger D-004.

**tests:** `test_damage_rage.cpp` fixed-input crit pipeline check.

---

## M-004 Armor damage reduction (physical, level-70 attacker)

**Formula.**

```
DR = armor / (armor + 467.5 * attacker_level - 22167.5)      // level > 59
   = armor / (armor + 10557.5)                                // at level 70
DR is capped at 75%.
```

Integer implementation (multiply numerator and denominator by 2 to clear the
.5): `damage_after = floor(damage * 21115 / (2*armor + 21115))`, with the 75%
cap applied as `damage_after = max(damage_after, floor(damage * 25 / 100))`
— i.e. kept fraction never drops below 25%.

**source_status:** `secondary` (canonical TBC armor formula) /
`emulator_reference` (cmangos-tbc `Unit::CalcArmorReducedDamage`).

**known_uncertainties:**
- `TODO(verify)` the 75% cap interacts with flooring exactly as written here.
- Negative-armor effects (sunder etc.) out of scope in M0; armor is a pinned
  scenario constant.

**tests:** `test_damage_rage.cpp` — armor 6200 ⇒ kept = 21115/33515; fixed
input checks; cap exercised with synthetic armor 60000.

---

## M-005 Block

**Formula.** A blocked attack deals
`max(0, damage_after_armor - block_value)`; `block_value` is a pinned scenario
constant (shield + strength derivation out of M0 scope). Block requires an
equipped shield (scenario flag `has_shield`) and the attacker in the frontal
arc; both gates live in the table builder (M-001). Crushing-block interactions
are mob-only and excluded vs players.

**source_status:** `secondary`; gating `emulator_reference` (cmangos-tbc).

**known_uncertainties:**
- `TODO(verify)` block value subtraction point in the pipeline (after armor,
  no crit interaction — see M-001 blocked-crit note, ledger D-010).

**tests:** `test_damage_rage.cpp` fixed-input block math incl. floor at 0;
`test_attack_table.cpp` shield/facing gating.

---

## M-006 Rage generation

**Formula** (TBC 2.0.1+ form). Rage conversion constant at level `L`:
`c(L) = 0.0091107836·L² + 3.225598133·L + 4.2652911`; at level 70,
`c = 274.7` — stored as the integer constant `RAGE_C10_L70 = 2747` (c × 10).

Attacker, on a swing that deals `D > 0` damage (post-mitigation):

```
rage = 15·D / (4·c) + f·s / 2
  f = 3.5 (main-hand ordinary/blocked hit), 7.0 (main-hand crit)
  s = weapon speed in seconds
```

Defender, taking `D > 0` damage: `rage = 2.5 · D / c`.

Integer deci-rage implementation (floor each term):

```
attacker: rage_deci = floor(375 * D / 2747) + floor(F2 * speed_ms / 400)   // F2 = 7 hit, 14 crit
defender: rage_deci = floor(250 * D / 2747)
```

Rage is stored as deci-rage `int32`, clamped to [0, 1000] (= 0–100 rage).
Misses, dodges, and parries generate **no** rage for either side in M0.

**source_status:** `secondary` (the 2.0.1 patch-note formula with hit-factor
term, as documented by the community and implemented by cmangos-tbc
`Player::RewardRage`).

**known_uncertainties:**
- `TODO(verify)` `RAGE_C10_L70 = 2747` (c(70) ≈ 274.7) — recompute against the
  oracle's constant.
- `TODO(verify)` no rage on dodge/parry for the attacker in TBC (the ~75%
  refund is WotLK 3.0.8; should be absent here). Ledger D-008.
- `TODO(verify)` rage uses post-mitigation damage dealt/taken (not pre-armor).
- `TODO(verify)` whether an uncapped-at-15D/c ceiling applies to the
  hit-factor form (some writeups cap the sum; we do not).

**tests:** `test_damage_rage.cpp` fixed-input deci-rage values for hit and
crit; cap at 1000; `arena_dist` rage-per-swing summary.

---

## M-007 Swing timer

**Formula.** Each auto-attacking unit owns a main-hand ready-at timestamp
(`int64` ms). A swing resolves at `t = ready_at`; the next swing is scheduled
at `t + weapon_speed_ms`. No haste, no parry-haste, no off-hand in M0 (ledger
D-006 for parry-haste). The idle warrior has `attacks: false` and never
schedules a swing. Death of either unit ends the match before further events.

**source_status:** `primary` (trivial mechanism), parry-haste exclusion
ledgered.

**tests:** golden traces (swing cadence at exact 3600 ms intervals);
determinism test; death-ends-match unit test.

---

## M-008 Facing and the frontal arc

**Formula.** Parry, block, and dodge apply only when the attacker is inside
the defender's frontal arc, defined as the half-plane within ±π/2 of the
defender's facing (total arc π):

```
in_front ⟺ dot( facing_unit_vector(defender), attacker_pos − defender_pos ) ≥ 0
```

Facing is `int32` milliradians; positions `int64` cm. The facing unit vector
uses an integer Bhaskara-I sine approximation (`sim/core/fixed_trig.h`),
scale 10^6, max relative error ~0.2%; only the dot-product **sign** feeds the
outcome. Ties (dot exactly 0, attacker exactly on the shoulder line) count as
front. M0 scenarios pin positions far from the boundary.

**source_status:** `emulator_reference` (arc width π: cmangos-tbc uses
`victim->HasInArc(M_PI, attacker)` for dodge/parry/block eligibility) +
`guess` for tie behavior. Integer-trig boundary divergence ledgered (D-007).

**known_uncertainties:**
- `TODO(verify)` that dodge (not just parry/block) is facing-gated in the
  oracle's 2.4.3 model exactly as CLAUDE.md pins it.

**tests:** `test_attack_table.cpp` facing classification at the four cardinal
placements; behind-scenario golden trace contains no dodge/parry/block.

---

## M-009 Counter-based RNG and bounded rolls

Not a game formula, but it feeds every probabilistic outcome, so it is specced.

**Formula.** `roll_u64(seed, entity_id, subsystem, seq)` = chained SplitMix64
finalizer over the key tuple (see `sim/core/rng.h`). Subsystem IDs live solely
in `sim/core/rng_subsystem.h`. Sequence numbers are **per (entity, subsystem)
use counters**: the attack-table roll consumes `swing_table` seq `k` on the
attacker's k-th swing; the damage roll consumes `weapon_damage` seq only on
contact outcomes (block/crit/hit). Hash checkpoints and scheduling consume no
RNG, so trace cadence changes cannot perturb combat streams.

Bounded values use the 128-bit multiply-shift reduction
(`(x * n) >> 64`, Lemire without rejection): bias is ≤ n/2^64 (< 10^-15 for
per-myriad rolls) and is accepted for determinism and speed.
`roll_myriad → [0,10000)`, `roll_range(lo,hi)` inclusive.

**source_status:** `primary` (our own construction; fidelity-neutral as long
as it is uniform — the oracle's RNG is not being reproduced).

**tests:** `test_rng.cpp` pinned outputs for fixed tuples (stream-stability
canary); bounded-roll range checks; `arena_dist` self-test at N=10^6.
