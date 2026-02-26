# Verification Protocol

The 4-phase verification protocol is UEAgentForge's core differentiator. Every mutating command runs through this pipeline automatically. No existing open-source UE bridge provides this safety guarantee.

## Why it matters

Standard Remote Control API scripts have no safety net:
- A failed spawn leaves the level in a dirty, partially-modified state
- There is no guarantee that `Ctrl+Z` will restore the exact previous state
- No way to confirm a change didn't break Blueprint compilation
- No enforcement of project governance rules

UEAgentForge's verification pipeline solves all four problems.

## The Pipeline

```
AI Agent sends command
        │
        ▼
┌───────────────────────────────────┐
│  Phase 1: PreFlight               │
│  • Constitution rules checked     │
│  • Pre-state captured             │
│  • Fail-fast if violation found   │
└──────────────┬────────────────────┘
               │ PASS
               ▼
┌───────────────────────────────────┐
│  Phase 2: Snapshot + Rollback     │
│  • JSON snapshot created          │
│  • Command executed in temp tx    │
│  • Tx intentionally CANCELLED     │
│  • State verified to be restored  │
│  • Only then: re-execute for real │
└──────────────┬────────────────────┘
               │ PASS
               ▼
  (Real FScopedTransaction opened)
  Command executes permanently
               │
               ▼
┌───────────────────────────────────┐
│  Phase 3: PostVerify              │
│  • Actor count delta checked      │
│  • Unexpected side effects logged │
│  (non-blocking — warns not fails) │
└──────────────┬────────────────────┘
               │
               ▼
┌───────────────────────────────────┐
│  Phase 4: BuildCheck              │
│  • Dirty BPs recompiled           │
│  • BS_Error → cancel real tx      │
└───────────────────────────────────┘
```

## Phase 1: PreFlight (bitmask: `0x01`)

**Purpose:** Catch governance violations and capture pre-state before any change.

**Steps:**
1. Load the constitution (if not already loaded)
2. Run `ConstitutionParser::ValidateAction(CommandDescription)` — checks every rule's trigger keywords against the action description
3. If any blocking rule is violated: return error immediately, no state change
4. Iterate all `AActor` instances in the level and record their labels and count as `PreStateActorLabels` / `PreStateActorCount`

**Example PreFlight response:**
```json
{
  "phase": "PreFlight",
  "passed": true,
  "detail": "Pre-state captured: 47 actors. Constitution: 12 rules checked, 0 violations.",
  "duration_ms": 2.1
}
```

**Example PreFlight failure:**
```json
{
  "phase": "PreFlight",
  "passed": false,
  "detail": "Constitution violations: [RULE_002] No plugin source edits unless explicitly approved.",
  "duration_ms": 0.8
}
```

## Phase 2: Snapshot + Rollback Test (bitmask: `0x02`)

This is the most important phase. It answers the question: **"If this change fails halfway through, can we get back to exactly where we started?"**

**Steps:**
1. Call `VerificationEngine::CreateSnapshot(CommandName + "_pre")` — saves JSON of all actors
2. Open a temporary `FScopedTransaction`
3. Execute the command inside the temp transaction
4. **Intentionally call `Transaction.Cancel()`** — this simulates a failure and rolls back
5. Re-iterate all actors and verify the count matches `PreStateActorCount`
6. If counts don't match: return `Snapshot+Rollback FAILED` — the engine cannot reliably undo this type of operation
7. If counts match: the rollback guarantee is confirmed
8. Open the **real** `FScopedTransaction` and execute the command again for real

**What "error injection" means:**
The temporary transaction is cancelled on purpose — this is not an error state, it's a deliberate test. The goal is to prove that the undo system works for this specific operation before committing. If the rollback test fails, the command is blocked entirely.

**Example Snapshot+Rollback response:**
```json
{
  "phase": "Snapshot+Rollback",
  "passed": true,
  "detail": "Rollback verified OK (47 actors restored). Snapshot: spawn_actor_pre_20260226_143022.json",
  "duration_ms": 45.3
}
```

**Snapshot file location:** `{ProjectDir}/Saved/AgentForgeSnapshots/`

## Phase 3: PostVerify (bitmask: `0x04`)

**Purpose:** Confirm the command had the expected effect.

**Steps:**
1. Re-iterate all actors and count them
2. Calculate delta: `PostCount - PreCount`
3. Compare against expected delta:
   - `spawn_actor` → expected `+1`
   - `delete_actor` → expected `-1`
   - all others → expected `0`
4. If delta doesn't match: log a **warning** (non-blocking)

PostVerify is intentionally non-blocking because:
- Some commands legitimately spawn helper actors (NavMesh rebuilds, etc.)
- Discrepancies are useful information but shouldn't stop legitimate work
- The real safety guarantee came from Phase 2

**Example PostVerify response:**
```json
{
  "phase": "PostVerify",
  "passed": true,
  "detail": "Actor delta: expected +1, actual +1. Post-count: 48.",
  "duration_ms": 1.8
}
```

## Phase 4: BuildCheck (bitmask: `0x08`)

**Purpose:** Ensure Blueprint changes don't break the project.

**Steps:**
1. Query the Asset Registry for all loaded `UBlueprint` assets with `bBeingCompiled = true` (dirty)
2. For each dirty Blueprint: call `FKismetEditorUtilities::CompileBlueprint`
3. Check `BP->Status == BS_Error`
4. If any Blueprint has errors: cancel the real transaction and return an error
5. If all clean: pass

**When BuildCheck runs:**
BuildCheck is most valuable after `create_blueprint`, `compile_blueprint`, `edit_blueprint_node`, and `set_bp_cdo_property`. For pure scene manipulation commands like `spawn_actor`, it has no effect (no dirty Blueprints).

**Example BuildCheck response:**
```json
{
  "phase": "BuildCheck",
  "passed": true,
  "detail": "BuildCheck: 2 blueprints checked. All clean.",
  "duration_ms": 120.0
}
```

## Selective phase execution

The `phase_mask` parameter lets you run only the phases you need:

| Mask | Phases |
|---|---|
| `1` (`0x01`) | PreFlight only — fast constitution check |
| `3` (`0x03`) | PreFlight + Snapshot (rollback guarantee, no post-check) |
| `7` (`0x07`) | PreFlight + Snapshot + PostVerify (no build check) |
| `15` (`0x0F`) | All 4 phases (full safety, slowest) |
| `9` (`0x09`) | PreFlight + BuildCheck only (skip rollback for speed) |

**Recommended defaults:**
- Interactive development: `15` (all phases)
- Automated CI/level building: `7` (skip slow BuildCheck)
- Read-only observation commands: `1` (PreFlight only, for constitution logging)

## Calling verification explicitly

```python
from ueagentforge_client import AgentForgeClient
client = AgentForgeClient()

# Run all 4 phases manually
report = client.run_verification(phase_mask=15)
print(report.summary())

# Run only PreFlight (fast, for constitution check)
fast_report = client.run_verification(phase_mask=1)
```

Via HTTP:
```bash
curl -X PUT http://127.0.0.1:30010/remote/object/call \
  -H "Content-Type: application/json" \
  -d '{
    "objectPath": "/Script/UEAgentForge.Default__AgentForgeLibrary",
    "functionName": "ExecuteCommandJson",
    "parameters": {"RequestJson": "{\"cmd\":\"run_verification\",\"args\":{\"phase_mask\":15}}"}
  }'
```

## Snapshots as audit trail

Every Phase 2 run creates a timestamped JSON snapshot. These serve as an audit trail of every AI agent action and can be used for debugging:

```json
{
  "snapshot_name": "spawn_actor_pre",
  "timestamp": "2026-02-26T14:30:22",
  "actor_count": 47,
  "actors": [
    {
      "label": "MyFloor",
      "class": "StaticMeshActor",
      "path": "...",
      "location": { "x": 0, "y": 0, "z": -5 },
      "rotation": { "pitch": 0, "yaw": 0, "roll": 0 }
    }
  ]
}
```

You can diff two snapshots using the Python client:
```python
# Snapshots are in Saved/AgentForgeSnapshots/
# Compare before/after manually or add a diff_snapshots command
```
