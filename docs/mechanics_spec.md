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

Oracle citations reference the separate checkout at `../cmangos-tbc`
(github.com/cmangos/mangos-tbc), commit **009455e**, audited 2026-06-10.
Citation form: `Unit.cpp:NNNN` means
`src/game/Entities/Unit.cpp` line NNNN at that commit.

Units convention (see also `sim/core/unit_state.h`): time `int64` ms, positions
`int64` cm, facing `int32` milliradians, probabilities `int32` per-myriad
(1 pm = 0.01%), rage stored as deci-rage `int32` (0–1000 ⇒ 0–100 rage).

---

## M-001 White-hit single-roll attack table

**Formula.** One uniform roll `r ∈ [0, 10000)` per-myriad is allocated across
contiguous outcome ranges in this fixed order:

```
miss | dodge | parry | block | glancing | crit | crushing | ordinary hit
```

(The oracle die also has RESIST and DEFLECT sides — ability/magic-school only,
never set for plain physical white swings, so they are not modeled.)

Range widths (per-myriad), level 70 attacker vs level 70 player defender, with
`delta = defense_skill - weapon_skill`:

- `miss   = max(0, 500 + delta * 4 - hit_pm)`
- `dodge  = max(0, dodge_pm  + delta * 4)` — zero when the facing gate fails (M-008).
- `parry  = max(0, parry_pm  + delta * 4)` — zero when the facing gate fails.
- `block  = max(0, block_pm + delta * 4)` — zero unless the defender has a
  shield equipped AND the facing gate holds.
- `glancing = 0` vs player targets, always (`is_player_target = true`).
- `crit   = max(0, crit_pm + (5 * attacker_level - defense_skill) * 4)` —
  note: vs player targets the attacker's skill term is the LEVEL CAP
  (`5 * level`), not actual weapon skill; weapon-skill bonuses do not raise
  crit vs players (Unit.cpp:3957).
- `crushing = 0` vs player targets, always (crushing is mob-attacker-only).
- `hit    = 10000 - (sum of the above)`, floored at 0; if the sum exceeds
  10000 the later ranges are truncated cumulatively (table is clamped left to
  right), so over-full tables squeeze crit, then hit, out — documented here as
  the single place that defines clamping.

Defender avoidance (`dodge_pm`, `parry_pm`, `block_pm`) and attacker `crit_pm`,
`hit_pm` are pinned directly in scenario files as per-myriad values; derivation
from agility/ratings is out of M0 scope. For reference, at level 70: 22.08 crit
rating = 1% crit, 15.77 hit rating = 1% hit (not modeled, documentation only).

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
- single roll `urand(1, 10000)` and die order: Unit.cpp:2858,
  `Unit::RollMeleeOutcomeAgainst`; die sides enum Unit.h:628–640 (BLOCK before
  GLANCE); die mechanics Util.h:105–131 (`Die::roll`, inclusive cumulative).
- base miss 5.0%: Unit.cpp:3889 (`Unit::GetMissChance`); hit subtracts at
  Unit.cpp:4030; melee minimum 0 vs players at Unit.cpp:4031.
- 0.04%/point skill delta vs player victims: miss Unit.cpp:3995–4013
  (actual weapon skill), dodge Unit.cpp:3393–3400, parry Unit.cpp:3425–3437,
  block Unit.cpp:3458–3465, crit Unit.cpp:3954–3964 (level-capped skill).
- glancing excluded vs player-controlled victims: Unit.cpp:3256–3259
  (`CanGlanceInCombat`); crushing excluded for non-creature attackers /
  player-controlled victims: Unit.cpp:3242–3254 (`CanCrushInCombat`).
- shield requirement for block: Unit.cpp:3224–3232 (`CanBlockInCombat`).

**known_uncertainties:**
- Oracle converts float chance to per-myriad with round-to-nearest
  (Util.h:93–96 `chance_u`); our scenario chances are pinned as exact pm so no
  difference for fixtures.
- The mob-side regimes (|delta| > 10 miss escalation, NPC factors 0.1/0.2/0.6,
  glancing/crushing magnitudes) are NOT implemented (`is_player_target=false`
  aborts).
- Clamping order for over-full tables remains a `guess` (oracle clamps each
  chance to [0,100] individually, Unit.cpp:3406, and its die just stops at
  10000 — same effective left-to-right truncation); unexercised by M0
  scenarios.
- `TODO(verify)` vs Anniversary TBC 2.5.x (oracle models 2.4.3; ledger D-003).

**tests:** `test_attack_table.cpp` — exact range construction at both scenario
stat blocks; positional gating; PvP exclusions; skill-delta adjustment incl.
the crit level-cap form; clamping at a synthetic over-full table.
`arena_dist` self-test at N=10^6; `arena_diff` vs the oracle model.

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

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
AP/14 × attack-speed-seconds folded into min/max before the roll,
StatSystem.cpp:377 (`Player::CalculateMinMaxDamage`); the roll itself is
`frand(min, max)` — CONTINUOUS uniform, truncated to uint32 on assignment
(Unit.cpp:2917 `Unit::CalculateDamage`, Unit.cpp:2031). Folding AP before vs
after the roll is statistically identical; continuous-vs-discrete is not
(ledger D-014).

**known_uncertainties:**
- Discrete inclusive `[min, max]` here vs oracle `floor(frand(min, max))`
  (≈ uniform on `[min, max-1]`): our mean is ~0.5 higher and we can roll the
  exact max. Kept deliberately — integer determinism — ledger D-014, D-004.

**tests:** `test_damage_rage.cpp` — fixed inputs (AP 2800, speed 3600 ⇒
bonus 720); distribution mean within CI in `arena_dist` self-test;
`arena_diff` compares damage means vs the oracle model with the documented
offset.

---

## M-003 Melee critical strikes

**Formula.** `damage_crit = damage * 2` (multiplier 2.0 for white melee vs
players; resilience — `GetCritTakenMultiplier` — is pinned to 0 in M0
scenarios and not modeled).

Pipeline order with armor: armor reduction is applied first, then the crit
multiplier, then block value subtraction (M-005).

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
multiplier 2.0 at Unit.cpp:3720–3724 (`GetCritMultiplier`, "Default crit
multiplier (attacks): 2x"); pipeline order confirmed — armor applies during
damage calculation BEFORE the outcome roll (Unit.cpp:2036–2038), the crit
multiplier after (Unit.cpp:2114, `CalculateCritAmount` Unit.cpp:3808–3834).

**known_uncertainties:**
- Oracle multiplies the post-armor uint32 by 2.0f then truncates
  (Unit.cpp:3825); ours multiplies the post-armor integer by 2 — identical for
  the ×2 case.
- `TODO(verify)` vs Anniversary TBC 2.5.x.

**tests:** `test_damage_rage.cpp` fixed-input crit pipeline check.

---

## M-004 Armor damage reduction (physical, level-70 attacker)

**Formula.**

```
levelModifier = L + 4.5 * (L - 59)   for L > 59  ⇒ 119.5 at L = 70
DR = (armor / (8.5 * levelModifier + 40)) * 0.1, then DR / (1 + DR)
   ≡ armor / (armor + 10557.5) at level 70
DR is capped at 75%; post-armor damage is floored at 1.
```

Integer implementation (multiply numerator and denominator by 2 to clear the
.5): `damage_after = floor(damage * 21115 / (2*armor + 21115))`, with the 75%
cap applied as kept-fraction-never-below-25%, then `max(damage_after, 1)` for
positive incoming damage.

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
Unit.cpp:2445–2471 (`Unit::CalcArmorReducedDamage`): the level modifier,
0.1/8.5/40 constants, the 0.75 cap (Unit.cpp:2465–2466) and the
minimum-1-damage floor (Unit.cpp:2470) all confirmed; algebra at L=70 reduces
exactly to armor/(armor+10557.5).

**known_uncertainties:**
- Oracle computes in float and truncates once on assignment; we floor in
  integer math — ≤1 damage difference per swing (ledger D-004).
- Armor penetration / negative armor out of scope (armor is pinned).

**tests:** `test_damage_rage.cpp` — armor 6200 ⇒ kept = 21115/33515; fixed
input checks; cap exercised with synthetic armor 60000; min-1 floor.

---

## M-005 Block

**Formula.** A blocked attack deals
`max(0, damage_after_armor - block_value)`; `block_value` is a pinned scenario
constant (shield + strength derivation out of M0 scope). Block requires an
equipped shield (scenario flag `has_shield`) and the facing gate (M-008); both
gates live in the table builder (M-001). Blocked attacks never crit (block and
crit are mutually exclusive die sides).

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
Unit.cpp:2141–2182 (MELEE_HIT_BLOCK case): flat `GetShieldBlockValue()`
subtraction from post-armor damage, full block when block value covers the
hit; the block branch never applies the crit multiplier, confirming
exclusivity (ledger D-010 resolved).

**known_uncertainties:**
- Oracle rage accounting around blocks uses pre-block damage with a doubling
  quirk — divergence ledgered (D-013), see M-006.

**tests:** `test_damage_rage.cpp` fixed-input block math incl. floor at 0;
`test_attack_table.cpp` shield/facing gating.

---

## M-006 Rage generation

**Formula.** Rage conversion constant at level `L`:
`c(L) = 0.0091107836·L² + 3.225598133·L + 4.2652911`; at level 70,
`c = 274.7000…` — stored as the integer constant `RAGE_C10_L70 = 2747` (c × 10).

Attacker, on a swing that deals `D > 0` damage (post-mitigation):

```
hit_factor hf = floor(f * weapon_speed_seconds)      // truncated BEFORE halving
  f = 3.5 (main-hand ordinary/blocked hit), 7.0 (main-hand crit)
rage = (D / c * 7.5 + hf) / 2
```

Defender, taking `D > 0` damage: `rage = 2.5 · D / c`.

Integer deci-rage implementation (matches the oracle's float pipeline exactly
for all tested values; single truncation at the end):

```
hf            = F2 * weapon_speed_ms / 2000            // F2 = 7 hit/block, 14 crit
attacker_deci = (375 * D + 5 * hf * 2747) / 2747       // = floor(375*D/274.7… + 5*hf)
defender_deci = 250 * D / 2747
```

Rage is stored as deci-rage `int32`, clamped to [0, 1000] (= 0–100 rage).
Misses, dodges, and parries generate **no** rage for either side in this
simulator — a DELIBERATE divergence from the oracle, see below.

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
conversion polynomial and `(d/c*7.5 + hf)/2` attacker form, 2.5·d/c victim
form, and deci-rage storage (`ModifyPower(POWER_RAGE, uint32(addRage * 10))`)
at Player.cpp:2332–2357 (`Player::RewardRage`); hit factors
`uint32(attack_time/1000 * 3.5)` (hit) and `* 7` (crit) at Unit.cpp:940–949;
victim rage excluded on dodge/parry at Unit.cpp:2297–2300 and 1009–1013.

**Known oracle divergences (deliberate, ledgered):**
- Oracle grants ATTACKER rage on full misses (`(0 + hf)/2`, no damage guard
  at Unit.cpp:932–967) — D-011.
- Oracle grants attacker rage on dodge/parry from a doubled "clean damage"
  (`cleanDamage = totalDamage` at Unit.cpp:2055, then `+= totalDamage` in the
  dodge/parry cases at Unit.cpp:2121/2133) — D-012.
- Oracle computes both sides' rage on blocks from post-armor PRE-block damage
  plus the blocked amount again (Unit.cpp:2055 + 2179–2180) — D-013. We use
  the post-block damage actually dealt.
All three look like emulator artifacts (the dodge/parry rage refund is
documented as a WotLK 3.0.8 change); we keep no-rage-on-avoid,
`TODO(verify)` against Anniversary TBC 2.5.x.

**known_uncertainties:**
- Integer form vs oracle float: equal on all tested values; 1-ulp boundary
  cases unverified (ledger D-016).
- `TODO(verify)` no-rage-on-avoid in 2.4.3/2.5.x retail behavior (D-011/D-012).

**tests:** `test_damage_rage.cpp` fixed-input deci-rage values for hit, crit
and block; cap at 1000; `arena_dist` rage-per-swing summary; `arena_diff`
compares contact-only rage vs the oracle model and reports the oracle's
avoid-rage behaviors as informational divergences.

---

## M-007 Swing timer

**Formula.** Each auto-attacking unit owns a main-hand ready-at timestamp
(`int64` ms). A swing resolves at `t = ready_at`; the next swing is scheduled
at `t + weapon_speed_ms`, and may be pulled earlier by parry-haste (M-010).
No spell/aura haste, no off-hand (dual-wield remains out of scope). Units
with `attacks: false` never schedule a swing (`ready_at = -1`). Death of
either unit ends the match before any further event resolves; when both
units' swings share a timestamp, the documented total order
(`event_queue.h`: source_id ascending) decides which resolves — and
therefore, if it kills, which side wins.

Retimed swings use lazy invalidation in the event queue: a popped Swing event
whose timestamp no longer equals the unit's authoritative `ready_at` is
stale and is skipped without consuming RNG or emitting trace events.

**source_status:** `primary` (trivial mechanism); same-timestamp resolution
order is our own documented choice (the oracle's update loop has no exact
analogue).

**tests:** golden traces (m0: exact 3600 ms cadence; m1_mutual: interleaved
timers); determinism tests; death-ends-match unit tests (either side);
event-order test.

---

## M-010 Parry-haste

**Formula.** When a unit parries an incoming swing, its own next main-hand
swing is accelerated. With `S = weapon_speed_ms` of the PARRYING unit and
`remaining = ready_at - now`:

```
p20 = floor(S / 5)            // 20% of weapon speed
p60 = 3 * p20                 // 60%
remaining > p60         ⇒ remaining -= 2 * p20      // subtract 40%
p20 < remaining <= p60  ⇒ remaining = p20           // clamp to 20%
remaining <= p20        ⇒ unchanged
```

Applied only if the parrying unit auto-attacks (`ready_at >= 0`). A parried
swing deals no damage, so ordering vs. death processing is moot; we apply the
retiming immediately after the swing resolves, matching the oracle (which
hastens before calling DealDamage).

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e,
Unit.cpp:2277–2315 (`DealMeleeDamage`, VICTIMSTATE_PARRY branch, main-hand
path: `percent20 = GetAttackTime(BASE_ATTACK) * 0.20f`, the
`> percent60 ⇒ -= 2*percent20` and `> percent20 ⇒ = percent20` windows).
The off-hand path (oracle prefers hastening the sooner off-hand swing) is
dual-wield scope, not implemented.

**known_uncertainties:**
- Oracle computes `percent20` in float and truncates on `setAttackTimer`;
  we use integer `floor(S/5)`. Differs by ≤1 ms only when S is not a
  multiple of 5 (ledger D-017); M1 fixture speeds (3600/2600) are exact.
- `TODO(verify)` vs Anniversary TBC 2.5.x (D-003), and whether parry-haste
  applies when the parrying unit's swing is mid-"ready" (remaining = 0 —
  unchanged under the rule as written).

**tests:** `test_parry_haste.cpp` — window boundaries at fixed inputs;
forced-parry match test (parry_pm 10000) pins the exact hastened swing
timeline; m1_mutual golden trace exercises it statistically.

---

## M-008 Facing and the frontal arc

**Formula.** Parry, block, and dodge apply only when the MUTUAL facing gate
holds (both conditions):

1. the attacker is inside the defender's frontal arc — within ±π/2 of the
   defender's facing (total arc π), and
2. the defender is inside the ATTACKER's frontal arc (the attacker is facing
   the defender).

```
in_front(A relative to B) ⟺ dot( facing_unit_vector(B), pos(A) − pos(B) ) ≥ 0
gate ⟺ in_front(attacker rel. defender) AND in_front(defender rel. attacker)
```

Facing is `int32` milliradians; positions `int64` cm. The facing unit vector
uses an integer Bhaskara-I sine approximation (`sim/core/fixed_trig.h`),
scale 10^6, max relative error ~0.2%; only the dot-product **sign** feeds the
outcome. Ties (exactly on the shoulder line) count as front. M0 scenarios pin
positions far from the boundary.

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
`IsFacingTargetsFront` (Object.cpp:1634–1648) requires BOTH
`victim->HasInArc(attacker, π)` AND `attacker->HasInArc(victim, π)`; it gates
dodge (Unit.cpp:3186–3188, "Players can't dodge attacks from behind"), parry
(Unit.cpp:3208–3211) and block (Unit.cpp:3237–3240). `HasInArcAt` uses
inclusive borders (Object.cpp:1610), matching our ties-count-as-front choice.

**known_uncertainties:**
- Integer Bhaskara sine vs oracle float trig near the arc boundary (D-007).
- `TODO(verify)` vs Anniversary TBC 2.5.x.

**tests:** `test_attack_table.cpp` facing classification at cardinal/diagonal
placements incl. mutual-gate cases (attacker facing away);
behind-scenario golden trace contains no dodge/parry/block.

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

The oracle's RNG is not reproduced (it uses a shared MT19937 via
`urand`/`frand`); only distributions are compared. Roll granularity matches:
the oracle's combat die is also per-myriad (`urand(1, 10000)`, Unit.cpp:2858)
— ledger D-005 resolved.

**source_status:** `primary` (our own construction; fidelity-neutral as long
as it is uniform).

**tests:** `test_rng.cpp` pinned outputs for fixed tuples (stream-stability
canary); bounded-roll range checks; `arena_dist` self-test at N=10^6.

---

## M-011 Global cooldown

**Formula.** Each unit owns `gcd_ready_ms` (`int64`, authoritative, hashed).
An ability with `gcd_ms > 0` requires `now >= gcd_ready_ms` to cast and sets
`gcd_ready_ms = now + gcd_ms` at cast start. Pinned values: Mortal Strike
1500 ms; Heroic Strike 0 (on-next-swing abilities are off-GCD: queueing
neither requires nor triggers the GCD). No haste scaling in M2.

**source_status:** `emulator_reference` for the mechanism — cmangos-tbc @
009455e, `WorldObject::AddGCD` (Object.cpp:2674–2681: GCD = spell data
`StartRecoveryTime`, applied at cast start, Spell.cpp:3180; zero means no
GCD). `secondary` for the per-spell values (DBC-derived community data; no
client data in this repo): MS `StartRecoveryTime` 1500, HS 0.
`TODO(verify)` HS rank 10 / MS rank 6 spell data against a clean DBC dump.

**tests:** `test_abilities.cpp` — MS never recast inside its GCD window in a
scripted match; HS queue/fire independent of GCD state.

---

## M-012 Weapon-ability hit resolution (yellow attacks)

**Formula.** Three independent keyed rolls (subsystems `yellow_table`,
`yellow_crit`, `yellow_block`; per-use counters as in M-009):

1. **Avoidance die** — one per-myriad roll over
   `miss | dodge | parry | hit(remainder)`:
   - miss = white miss (M-001) WITHOUT the dual-wield penalty (n/a in M2);
     floor 0 vs equal-level players.
   - dodge/parry = white formulas (M-001), gated by the mutual facing check
     (M-008). NO block side: normal strikes (MS/HS lack
     `SPELL_ATTR_EX3_COMPLETELY_BLOCKED`) cannot be fully blocked.
     No glancing/crushing on abilities, ever.
2. **Crit roll** (only on hit) — chance = white crit (M-001: `crit_pm` +
   level-capped skill delta), independent per-myriad roll. Multiplier 2.0
   (M-003 applies to melee-class abilities).
3. **Partial block roll** (only on hit, after damage) — chance = white block
   (M-001), gated shield + mutual facing; on success subtract `block_value`,
   floored at 0. Blocked crits are therefore possible for yellows.

Damage pipeline (integer, floors documented in D-004):

```
base   = flat_bonus + uniform_int[weapon_min, weapon_max] + ap_bonus(M-013)
crit   ⇒ base *= 2          // BEFORE armor for yellows (opposite of M-003)
damage = apply_armor(base)   // M-004, incl. min-1 floor
block  ⇒ damage = max(0, damage - block_value)
```

A parried ability does NOT trigger parry-haste (M-010 is the white-swing
path only).

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e:
avoidance die `Unit::MeleeSpellHitResult` (Unit.cpp:2958–2998); ability gates
`CanDodgeAbility`/`CanParryAbility` (Unit.cpp:3271–3310); melee-ability miss
delegates to the white calc (Unit.cpp:4070–4071); separate crit roll
Spell.cpp:1673 → `CalculateEffectiveCritChance` (Unit.cpp:4044); crit-then-
armor order Spell.cpp:1284–1296; partial block
`RollAbilityPartialBlockOutcome` (Unit.cpp:3565–3571) applied in
`CalculateAbsorbResistBlock` (Unit.cpp:2704–2710) after armor; full/partial
block attribute split documented in `CanBlockAbility` (Unit.cpp:3520–3545).
parry-haste white-only: it lives in `DealMeleeDamage` (Unit.cpp:2277), which
the spell path never enters.

**known_uncertainties:**
- Crit-before-armor vs white's armor-before-crit commutes up to flooring
  (≤1 damage); both orders are ported faithfully per path (D-004).
- `TODO(verify)` vs Anniversary TBC 2.5.x (D-003).

**tests:** `test_abilities.cpp` — avoid-die widths front/behind; fixed-input
yellow damage pipeline incl. blocked crit; zero glance/crush by construction.
M5 adds: `test_dist_self.cpp` yellow self-distributions at N=10^6 (front +
behind gating); `test_oracle_model.cpp` oracle yellow table/chance equality
and MC determinism; `arena_dist`/`arena_diff` yellow rows PASS at N=10^6 on
all four fixtures (docs/differential_harness.md).

---

## M-013 Normalized weapon damage

**Formula.** Ability AP contribution uses a NORMALIZED speed instead of the
equipped weapon's speed: `ap_bonus = floor(AP * weapon_norm_ms / 14000)` with
`weapon_norm_ms` pinned per scenario unit: 3300 (two-hand), 2400 (one-hand /
fist), 1700 (dagger), 2800 (ranged, unused). Non-normalized abilities
(Heroic Strike) use the real `weapon_speed_ms` exactly as M-002.

**source_status:** `emulator_reference` — cmangos-tbc @ 009455e,
`Unit::GetAPMultiplier` (Unit.cpp:10990–11013); the normalized flag enters
the same min/max assembly as M-002 (StatSystem.cpp:357–418).

**tests:** `test_abilities.cpp` fixed inputs (AP 2800, norm 3300 ⇒ 660).

---

## M-014 Mortal Strike (rank 6)

**Formula.** Instant weapon strike: costs 300 deci-rage, 6000 ms own
cooldown (`ms_ready_ms`, hashed), triggers the 1500 ms GCD (M-011). Damage:
normalized weapon damage (M-013) + 210 flat, resolved per M-012. The Mortal
Wounds healing-reduction debuff is NOT modeled (no healing exists in the
sim; revisit when healing lands).

**source_status:** `secondary` — spell 30330 (Mortal Strike rank 6) values
(30 rage, 6 s cooldown, +210 damage) are DBC-derived community constants,
hand-authored here (no client data in repo). Mechanism citations per
M-011/M-012/M-013. `TODO(verify)` rank values vs a clean DBC dump and vs
Anniversary 2.5.x.

**tests:** `test_abilities.cpp` — cooldown spacing ≥ 6000 ms in a scripted
match; cost deducted on every cast including misses (M-016); fixed-input
damage.

---

## M-015 Heroic Strike (rank 10)

**Formula.** On-next-swing attack: queueing is free and off-GCD
(`hs_queued`, hashed). At the unit's next main-hand swing: if
`rage >= 150 deci`, the swing is REPLACED by a yellow attack (M-012) with
NON-normalized weapon damage + 176 flat; 150 deci-rage is paid at that
moment; the queue clears. If rage is insufficient at swing time, the queue
clears and a normal white swing (M-001/M-002) resolves instead. Either way
the swing timer advances by `weapon_speed_ms` as usual. A swing replaced by
Heroic Strike generates NO attacker swing rage (it is a spell, M-016).

**source_status:** `emulator_reference` for the replace-or-fallback
mechanism — cmangos-tbc @ 009455e `Unit::AttackerStateUpdate`
(Unit.cpp:2740–2750: pending CURRENT_MELEE_SPELL casts at main-hand swing;
on cast failure falls through to the white swing). `secondary` for spell
30324 (rank 10) values (+176 damage, 15 rage, off-GCD). `TODO(verify)` rank
values vs DBC; queue-cancel-on-low-rage timing vs real client behavior
(the client also unqueues on rage drop below cost; we only check at swing).

**tests:** `test_abilities.cpp` — queue-fire-replace semantics; fallback to
white when rage is short; off-GCD queueing.

---

## M-016 Ability resource accounting

**Formula.**
- Full rage cost is consumed at execution for every cast, INCLUDING misses,
  dodges and parries (no avoid-refund — that is a WotLK 3.0 change).
- Ability damage generates NO attacker rage (the white-swing hit-factor
  formula M-006 does not apply to spells).
- Ability damage DOES generate victim rage: `2.5 * D / c` deci (M-006
  defender form, post-mitigation D) — a DELIBERATE divergence from the
  oracle, which lacks any victim-rage path for spell damage (ledger D-019):
  retail-TBC victims observably gain rage from ability hits.

**source_status:** `emulator_reference` for cost-regardless-of-outcome
(cmangos-tbc @ 009455e `Spell::TakePower`, Spell.cpp:4566–4587 — no outcome
check) and for no-attacker-rage (the `RewardRage` attacker path requires
`damagetype == DIRECT_DAMAGE`, Unit.cpp:934; spells deal
`SPELL_DIRECT_DAMAGE`). Victim-rage-from-abilities: `empirical` (retail
behavior), diverges from oracle (D-019).

**tests:** `test_abilities.cpp` — rage deltas around casts for hit and
avoided outcomes; victim rage after MS hit.

---

## Policy knobs (not game formulas)

Superseded in M3 by the observation/action interface — see
`docs/observation_action_spec.md` (normative). Decisions happen on fixed
per-scenario decision ticks (`Decide` events; at equal timestamps `Decide`
precedes `Swing`, total order in `event_queue.h`); scenario units pin a
loadout (`knows_mortal_strike`, `knows_heroic_strike`) and a policy
(`idle | scripted`, with `scripted_hs_min_rage_deci` as the scripted knob;
loader enforces threshold >= cost when nonzero). The M2 event-driven
evaluation (instant reflexes) was replaced by tick-quantized decisions —
agents and scripted baselines react at tick granularity.
