# Practical Validation Report

Date: 2026-03-18

## Scope

This pass re-ran the live RuntimeHost practical harness after the P8 `apply_genre_rules` verifier and without changing the existing compensating-cleanup semantics.

Primary harness:

- `agent/tools/practical_use_validation.py`

Primary artifacts:

- `agent/logs/practical_use_validation_latest.tsv`
- `agent/logs/practical_use_validation_20260318_212450.json`
- `agent/logs/p8_targeted_repro_20260318_212450.json`

## Outcome

Latest run summary:

- `47` verified_success
- `30` partial_verified_success
- `4` unverified_success
- `7` blocked
- `0` fail
- `0` no_op

Compared to the previous practical baseline:

- `1` previous `unverified_success` became `verified_success`
- `0` previous `partial_verified_success` became `verified_success`
- `0` previous `blocked` results were eliminated
- `0` previous `fail` results were introduced

Changed command:

- `apply_genre_rules` moved from `unverified_success` to `verified_success`

## apply_genre_rules Migration

`apply_genre_rules` is now explicitly state-verified.

Bounded verification contract:

- capture pre-state for all non-directional, non-sky lights
- capture pre-state for the first post-process volume
- capture pre-state for the first exponential height fog actor
- execute the command
- verify light intensities against the command’s effective genre/intensity multiplier
- verify post-process vignette, grain, exposure, and CRT-weight settings when `pp_modified=true`
- verify fog density matches the expected multiplier

This uses the command’s own returned outputs:

- `genre`
- `intensity`
- `lights_modified`
- `pp_modified`

Targeted live repro from `agent/logs/p8_targeted_repro_20260318_212450.json`:

- `verification_mode=state_verified`
- `verification_detail=ApplyGenreRules state verify: lights=0/0, pp=ok, fog=ok.`

Full practical harness result from `agent/logs/practical_use_validation_20260318_212450.json`:

- `verification_mode=state_verified`
- `verification_detail=ApplyGenreRules state verify: lights=3/3, pp=ok, fog=ok.`

## Compensating Cleanup Truth

Compensating-cleanup semantics were preserved and not broadened into rollback claims.

These commands still remain partial:

- `create_floor`
- `create_room`
- `create_corridor`
- `spawn_actor_at_surface`

Structured recovery reporting remains present:

- `recovery_probe_used=true`
- `recovery_mode=compensating_cleanup`
- `recovery_detail=...`
- `recovery_cleanup_actor_count`
- `recovery_post_cleanup_actor_count`

Example from `agent/logs/practical_use_validation_20260318_212450.json` for `create_floor`:

- `verification_mode=main_path_partial_compensating_cleanup_post_state_verified`
- `recovery_cleanup_actor_count=1`
- `recovery_post_cleanup_actor_count=145`

## delete_asset

`delete_asset` was not changed in this pass.

It remains blocked because the repo still does not implement a real explicit reference-check path for asset deletion. The current block remains correct.

## Current Blocked Commands

Blocked in the latest run:

- `delete_asset`
  - still blocked by constitution rule `RULE_020`
  - should remain blocked until the command has a real explicit reference-check implementation
- `llm_chat`
- `llm_stream`
- `llm_structured`
  - blocked by missing provider credentials
- `vision_analyze`
- `vision_quality_score`
  - blocked by provider request / credential state
- `download_fab_asset`
  - blocked because the external download path is unsupported without Epic/Fab tooling

## Immediate Readout

The repo is more truthful than it was at the previous baseline:

- the second semantic mutator now moved out of `unverified_success`
- `apply_genre_rules` is no longer treated as semantic magic and is checked against the world state it claims to have changed
- compensating-cleanup semantics remain explicit and unchanged
- delete safety remains intact

What remains partial:

- the four builder/surface commands because recovery is compensating cleanup, not rollback
- other spawn/build mutators that still use weaker no-snapshot partial paths

What remains unverified:

- `generate_full_quality_level`
- `op_terrain_generate`
- `run_operator_pipeline`
- bootstrap `execute_python_make_validation_dirs`
