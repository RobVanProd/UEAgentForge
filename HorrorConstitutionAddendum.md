# UEAgentForge — Horror Genre Constitution Addendum
## Supplement to ue_dev_constitution.md for Survival Horror Projects

This addendum is loaded alongside the base constitution when `genre = horror`.
It extends the core rules with horror-specific atmosphere, pacing, and design constraints.

---

## Non-negotiable Horror Rules

- Darkness score must remain ≥ 50 at all times. Never place lights that eliminate tension zones.
- Point light intensity must not exceed 2000 cd in any area the player can hide in.
- All player-hiding areas must have at least one light occluder (wall, column, furniture).
- The Warden must have at least one active patrol route defined via NavMesh before the level is considered playable.
- No ambient music or sound effect may reveal the Warden's position directly (diegetic only — footsteps, breathing, door creaks).
- Sanity drain events must not stack without a recovery window (minimum 10 seconds safe-zone between consecutive drain triggers).

## Atmosphere Requirements

- Vignette intensity ≥ 0.6 when horror_score < 70.
- Exponential height fog must be present and active. FogDensity ≥ 0.0005.
- Post-process exposure bias ≤ −0.4 in the main horror sections.
- CRT/VHS blendable weight increases linearly from 0 (sanity=100) to 1 (sanity=0).
- GoodSky or equivalent must use a nighttime/storm preset (MIDNIGHT_STORM, MIDNIGHT_MOON, or MIDNIGHT_BLOOD_MOON).

## Spatial Design Rules

- At least 30% of floor area must be in darkness (darkness_score ≥ 70) to maintain tension.
- Hiding spots (HidingSpotComponent) must be placed within 15m of any Warden patrol point.
- No corridor may be longer than 20m without a junction, hiding option, or visual obstruction.
- All interactive objects (InteractableItems) must be placed where the player must briefly enter Warden LoS to collect them — no safe, easy pickups.
- Use `place_asset_thematically` with `prefer_dark=true` and `prefer_occluded=true` for all atmospheric prop placement.

## The Warden AI Rules

- The Warden must transition to Hunt state within 3 seconds of hearing a noise at loudness ≥ 0.6.
- Hunt speed must not exceed 400 cm/s (player base speed is 300 cm/s — maintain tension, not frustration).
- Warden hearing range at loudness 1.0 must be ≥ 1500 cm.
- Vision cone half-angle must not exceed 65° — peripheral blindness is intentional horror design.
- The Warden must have a give-up duration ≥ 8 seconds before transitioning from Hunt → Investigate.

## Pacing Constraints

- Chapter 1 (Reception): maximum 4 Warden encounters in 15 minutes of play.
- First Warden sighting must occur through glass/indirect view — never direct confrontation.
- At least one "safe room" per chapter with no Warden patrol and full sanity recovery.
- Jump scares must be preceded by at least 30 seconds of tension build (audio cues, environmental tells).

## Performance Requirements

- Lvl_Reception target: ≤ 80 actors, ≤ 12ms frame time on UE 5.7 mid-range hardware.
- No dynamic shadows from more than 4 light sources simultaneously in any room.
- NavMesh must cover ≥ 85% of walkable floor area for Warden pathing to function.

## Testing Criteria (before marking a chapter complete)

1. Take screenshot with `get_multi_view_capture` from all 4 angles. Horror atmosphere visible in all views.
2. `get_semantic_env_snapshot` horror_score ≥ 65.
3. NavMesh query confirms all Warden patrol points are reachable.
4. All 4 verification phases pass with `run_verification` (phase_mask=15).
5. PIE test: player can hide successfully and Warden eventually gives up hunt.
6. PIE test: ambient footstep noise triggers Warden Investigate state within 3 steps of a patrol point.

---

*This addendum is automatically applied when `apply_genre_rules(genre="horror")` is called.*
*All rules are enforced by `enhance_horror_scene` and `observe_analyze_plan_act`.*
