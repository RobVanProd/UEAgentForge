# Morning Review Inbox

Use this file instead of stopping for non-urgent questions. Keep entries short, dated, and tied to specific files or decisions.

## How To Use

- Add one item per blocker, missing credential, or design decision that should be reviewed by the human.
- State the safest default the agent used, or mark the item as blocked.
- Update resolved items instead of duplicating them.

## Open Items

- None yet.

## Decision Log

- 2026-03-18: Initialized the autonomous agent workflow for UEAgentForge v0.5.0. Future agents should append review items here instead of pausing overnight progress.
- 2026-03-18: Integrated Addendum A into the run order before the main v0.5.0 game plan. Added explicit Unreal compile and launch actions, fixed UE 5.7 UHT/build hygiene issues blocking validation, and created `agent/tmp/RuntimeHostProject/RuntimeHostProject.uproject` as a safe smoke-test host project.
- 2026-03-18: Synced the public docs and root workflow docs with the implemented Addendum A command layer, viewport screenshot flow, MCP building guidance, and the UE 5.7 validation harness.
- 2026-03-18: Added the second Addendum A command batch (expanded asset discovery, viewport framing, actor property/label/group controls, rect/directional lights, corridor/pillar construction), fixed `set_actor_label` transaction validation by skip-listing it from snapshot rollback, and live-validated the batch in `RuntimeHostProject`.
- 2026-03-18: Added `agent/tools/launch_runtime_host.ps1` after discovering Unreal can block unattended runs behind a `Restore Packages` modal sourced from stale autosave recovery data.
