# Structural Status

_Last updated: 2026-03-18_

## Executive Summary

UEAgentForge has matured from an Unreal automation prototype into a credible, bounded-trust control substrate for Unreal Engine workflows.

The key improvement is not raw pass count. It is that the repo now distinguishes between:

- `verified_success` - command claims were checked and confirmed
- `partial_verified_success` - command succeeded, but trust is bounded by recovery or rollback limits
- `unverified_success` - command succeeded without sufficient verification guarantees
- `blocked` - command was intentionally prevented for structural or environment reasons
- `fail` - command behavior failed outright
- `no_op` - command did not execute meaningful work

This classification model replaced earlier green-by-default behavior and gives the repository a more truthful operational surface.

## Current Validation Snapshot

Latest practical validation counts:

- `47 verified_success`
- `30 partial_verified_success`
- `4 unverified_success`
- `7 blocked`
- `0 fail`
- `0 no_op`

The repository is now in refinement and trusted-surface expansion mode, not rescue mode.

## What Is Now Proven

1. A real command substrate for Unreal mutation and inspection.
2. Truthful verification reporting for manual and transaction-bound verification paths.
3. Bounded recovery semantics for rollback-problem commands.
4. A meaningful verified layer above atomic commands.
5. A more honest blocked surface for destructive or externally constrained operations.

## Current Trust Model

### Verified Success

Commands in this bucket have concrete post-state or state-contract verification. This is the most trustworthy current surface.

Examples:

- common actor and property mutations with command-aware postconditions
- state-verified semantic scene commands
- state-verified preset, genre, and set-dressing flows

### Partial Verified Success

These commands are operationally useful, but their guarantees are bounded.

Common reasons:

- true rollback is not available
- snapshot or undo is skipped for structural reasons
- compensating cleanup is used instead of rollback
- post-state verifies, but recovery semantics are weaker than desired

Important current examples:

- `create_floor`
- `create_room`
- `create_corridor`
- `spawn_actor_at_surface`

These are not rollback-safe. They are classified with explicit compensating-cleanup semantics.

### Unverified Success

These commands work but still sit outside the stronger trust envelope.

Remaining high-value examples:

- `generate_full_quality_level`
- `op_terrain_generate`
- `run_operator_pipeline`
- `execute_python_make_validation_dirs`

### Blocked

Blocked commands are intentionally prevented because that is the most honest current behavior.

Examples:

- `delete_asset` - blocked until real reference analysis exists
- provider-dependent LLM and vision commands - blocked by missing credentials or provider state
- `download_fab_asset` - blocked because the external dependency is unsupported

## Recovery Semantics

### True Rollback vs Compensating Cleanup

Several builder and spawn commands could not be made truthfully rollback-safe under the current transaction and undo model.

For those commands, the repo now uses compensating cleanup:

1. probe execution runs first
2. spawned actor paths are extracted
3. those actors are explicitly destroyed
4. cleanup is verified against world state
5. real execution only proceeds after cleanup validation succeeds

This is not rollback.

The distinction is surfaced directly in response metadata:

- `verification_mode`
- `recovery_probe_used`
- `recovery_mode=compensating_cleanup`
- `recovery_detail`
- `recovery_cleanup_actor_count`
- `recovery_post_cleanup_actor_count`

## Remaining Structural Gaps

### 1. No true rollback for key builder and spawn commands

Still unresolved for:

- `create_floor`
- `create_room`
- `create_corridor`
- `spawn_actor_at_surface`

These are recoverable, but still not undo-safe in the strict sense.

### 2. Destructive safety is not complete

`delete_asset` remains blocked for a good reason.

Before it can be unblocked safely, the system still needs:

- explicit reference and dependency analysis
- surfaced findings in the response
- clear sandbox or override behavior
- honest destructive-operation guarantees

### 3. A few high-value surfaces remain outside stronger trust accounting

Still to migrate or bound more tightly:

- `generate_full_quality_level`
- `op_terrain_generate`
- `run_operator_pipeline`

### 4. Visual correctness is still weaker than execution truth

The repository is better at answering:

`Did the command run, and how trustworthy was the execution?`

It is still weaker at answering:

`Did the scene come out visually stable and production-usable?`

## Future Workstream: Visual Stability and Artifact Mitigation

Recent viewport output showed issues such as:

- competing directional lights
- floor flicker or rendering artifacts
- scene-state conflicts that are visually detectable even when command execution succeeds

Recommended scope for the next quality frontier:

- structural scene checks for overlapping geometry and duplicate generated actors
- lighting sanity checks for conflicting dominant lights and fog or post-process interactions
- visual artifact heuristics for flicker, z-fighting, and unstable shadow regions
- output-quality classification for structurally valid, visually stable, degraded, or unacceptable scenes

## Recommended Next Phase

Recommended priority order:

1. Reduce one more high-value unverified pipeline or orchestration surface.
2. Preserve and tighten truthful recovery metadata.
3. Leave `delete_asset` blocked until reference analysis is real.
4. Begin planning the visual stability validation layer.

## Recommended Positioning

UEAgentForge should currently be described as:

> UEAgentForge is a bounded-trust Unreal Engine control substrate with growing verified coverage, explicit recovery semantics, and a structured path toward safer high-level automation.

Avoid describing it as fully safe or fully transactional across all mutation classes.

## Final Assessment

UEAgentForge is no longer broadly structurally suspect.

It is structurally maturing, increasingly trustworthy in bounded domains, and worth continuing with rigor.

Its biggest strength is no longer just capability.

Its biggest strength is that it increasingly tells the truth about its own limits.
