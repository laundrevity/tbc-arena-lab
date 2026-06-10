# Divergence Ledger — tbc-arena-lab

Known or suspected differences between (a) this simulator, (b) cmangos-tbc
behavior (bootstrap oracle, models 2.4.3), and (c) the pinned ruleset
(Anniversary TBC 2.5.x). Append-only: never delete an entry; mark it
`RESOLVED(<date>): <how>` instead.

| ID | Status | Divergence | Rationale |
|----|--------|------------|-----------|
| D-001 | open | Glancing blows excluded vs. player targets (zero-width table range when `is_player_target`). | Glancing applies only to attacks against higher-level mobs; arena combat is PvP-only, so the sim hard-excludes it (spec M-001). Oracle agrees; carried here so the differential harness asserts it. |
| D-002 | open | Crushing blows excluded vs. player targets. | Crushing is mob-attacker-only; no player can receive or deliver one in arena. Spec M-001. |
| D-003 | open | Fidelity target is cmangos-tbc's model of **2.4.3** as bootstrap oracle, while the pinned ruleset is **Anniversary TBC 2.5.x**. | Divergences between those two rulesets are expected and will be cataloged as they are found — not hidden. Each future entry cites which side we follow and why. |
| D-004 | open | Integer flooring at each damage-pipeline stage (AP bonus, armor reduction, crit ×2, block subtraction) vs. emulator float math rounded late. | Determinism requires integer authoritative values (CLAUDE.md rule 3); per-stage floor diverges from the oracle by ≤1 damage per stage. Spec M-002/M-003/M-004. The differential harness compares distributions, not per-swing values, so this is tolerable until proven otherwise. |
| D-005 | open | Attack-table roll granularity is per-myriad (10000 buckets); cmangos rolls float percentages (effectively per-million via `irand(0,10000)`/float chances). | Pinned scenario chances are exact per-myriad values, so no rate error for M0 scenarios; sub-0.01% chances would quantize. Revisit if a scenario ever needs finer resolution. Spec M-009. |
| D-006 | open | Parry-haste (defender's next swing accelerated after parrying) not implemented in M0. | Idle defender never swings, so it is unobservable in M0 scenarios; must be implemented before any scenario where both units attack. Spec M-007. |
| D-007 | open | Frontal-arc test uses integer Bhaskara-I sine (≤0.2% rel. error) and counts dot==0 as front; oracle uses float trig (`HasInArc(M_PI)`). | Only the sign of the dot product feeds outcomes; divergence is confined to placements within ~0.2% of the exact shoulder line. M0 scenarios pin positions far from the boundary. Spec M-008. |
| D-008 | open | No rage generated on miss/dodge/parry for the attacker; rage uses post-mitigation damage; `c(70)` stored as 2747 (×10). | Best community-documented TBC behavior, all `TODO(verify)` against the oracle (the dodge/parry refund is believed WotLK-era). Spec M-006. |
| D-009 | open | Weapon skill pinned 350 vs defense 350 in both M0 scenarios, so the ±0.04%/point skill-delta code paths are implemented but unexercised by goldens. | Equal-skill PvP is the M0 fixture; unit tests cover the delta math synthetically, but oracle comparison of nonzero deltas is deferred. Spec M-001. |
| D-010 | open | No "blocked crit" outcome: block and crit are mutually exclusive single-roll table rows. | Whether TBC blocked attacks can crit is disputed; single-roll exclusivity matches the cmangos table layout. `TODO(verify)` against oracle. Spec M-001/M-005. |
