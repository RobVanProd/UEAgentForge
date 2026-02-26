# UEAgentForge

**The first free, open-source, enterprise-grade AI agent control bridge for Unreal Engine 5.**

UEAgentForge evolves the pattern of basic Remote Control API wrappers into a production-standard framework with unbreakable transaction safety, 4-phase verification, and constitution-enforced project governance.

## Why UEAgentForge?

| Feature | Basic RC wrappers (UnrealMCP, etc.) | Commercial tools (Neo AI, etc.) | **UEAgentForge** |
|---|---|---|---|
| Scene manipulation | ✓ | ✓ | ✓ |
| Blueprint node editing | ✗ | ✓ | ✓ |
| Transaction safety + undo | ✗ | Partial | ✓ Full FScopedTransaction |
| Snapshot + rollback test | ✗ | ✗ | ✓ Error-injection verified |
| Constitution enforcement | ✗ | ✗ | ✓ Runtime markdown rules |
| Python scripting bridge | Partial | ✗ | ✓ |
| Open source | ✓ | ✗ | ✓ MIT |
| Free | ✓ | ✗ | ✓ |

## Architecture

```
AI Agent (Claude, GPT, etc.)
        │
        │ HTTP PUT /remote/object/call
        ▼
┌─────────────────────────────────────────────────────┐
│               UEAgentForge Plugin                   │
│                                                     │
│  AgentForgeLibrary  ←→  VerificationEngine          │
│       │                    │                        │
│  ConstitutionParser    4-Phase Protocol             │
│  (runtime rules)       Phase 1: PreFlight           │
│                        Phase 2: Snapshot+Rollback   │
│                        Phase 3: PostVerify          │
│                        Phase 4: BuildCheck          │
│                                                     │
│  Command Surface (30+ commands)                     │
│  ─ Observation  ─ Actor Control  ─ Blueprints       │
│  ─ Materials    ─ Content Mgmt   ─ Spatial Queries  │
│  ─ Transactions ─ Python Bridge  ─ Perf Profiling   │
└─────────────────────────────────────────────────────┘
        │
        ▼
   Unreal Editor (UE 5.5+)
```

## Installation

1. Copy the `UEAgentForge/` folder into your project's `Plugins/` directory.
2. Re-generate project files (right-click `.uproject` → Generate Visual Studio files).
3. Build the editor: `AquaEchosEditor Win64 Development` (or your project target).
4. Enable **Remote Control API** in `Edit → Plugins → Remote Control`.
5. The plugin auto-loads. Check the Output Log for:
   ```
   [UEAgentForge] Constitution loaded: ... (N rules)
   ```

### Constitution setup (optional but recommended)

Copy `Constitution/ue_dev_constitution_template.md` to your project root as
`ue_dev_constitution.md` and customize the rules for your project.
UEAgentForge will auto-discover and enforce it at startup.

### Python client

```bash
pip install requests
python PythonClient/ueagentforge_client.py
```

## Quick Start

### Via Python client

```python
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()  # verify=True by default

# Check connection
print(client.ping())

# Check forge status (constitution, verification)
print(client.get_forge_status())

# List actors
actors = client.get_all_level_actors()
print(f"{len(actors)} actors in level")

# Spawn with automatic safety pipeline
result = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=200)
print(result)

# Full verified workflow
with client.transaction("My Safe Workflow"):
    client.spawn_actor("/Script/Engine.StaticMeshActor", x=100, y=100, z=0)
    client.set_actor_transform("MyActor", x=200, y=200, z=100)
```

### Via HTTP (curl / any agent)

```bash
curl -X PUT http://127.0.0.1:30010/remote/object/call \
  -H "Content-Type: application/json" \
  -d '{
    "objectPath": "/Script/UEAgentForge.Default__AgentForgeLibrary",
    "functionName": "ExecuteCommandJson",
    "parameters": {"RequestJson": "{\"cmd\":\"ping\"}"}
  }'
```

### Via Claude Code (AI agent)

```json
{
  "cmd": "run_verification",
  "args": {"phase_mask": 15}
}
```

## Command Reference

### Forge Meta
| Command | Description |
|---|---|
| `ping` | Health check, returns version and constitution status |
| `get_forge_status` | Plugin version, constitution rules loaded, last verification |
| `run_verification` | Run 4-phase verification protocol (`phase_mask` = bitmask 1-15) |
| `enforce_constitution` | Check an action against loaded constitution rules |

### Observation
| Command | Description |
|---|---|
| `get_all_level_actors` | All actors with transforms, class, path |
| `get_actor_components` | Components of a named actor |
| `get_current_level` | Current level package path, map lock status |
| `assert_current_level` | Verify expected level matches current |
| `get_actor_bounds` | AABB origin, extent, min, max |

### Actor Control
| Command | Description |
|---|---|
| `spawn_actor` | Spawn actor by class path at transform |
| `set_actor_transform` | Move/rotate actor by object path |
| `delete_actor` | Delete actor by label |
| `save_current_level` | Save the current map |
| `take_screenshot` | Capture viewport to PNG |

### Spatial Queries
| Command | Description |
|---|---|
| `cast_ray` | Line trace, returns hit location/actor/distance |
| `query_navmesh` | Project point to navigation mesh |

### Blueprint Manipulation
| Command | Description |
|---|---|
| `create_blueprint` | Create new BP asset with parent class |
| `compile_blueprint` | Compile a Blueprint, return errors |
| `set_bp_cdo_property` | Set CDO property value (float/int/bool/string) |
| `edit_blueprint_node` | Edit a graph node's pin values |

### Material & Content
| Command | Description |
|---|---|
| `create_material_instance` | Create MIC from parent material |
| `set_material_params` | Set scalar/vector parameters on MIC |
| `rename_asset` | Rename content browser asset |
| `move_asset` | Move asset to different content path |
| `delete_asset` | Delete asset (with reference check) |

### Transaction Safety
| Command | Description |
|---|---|
| `begin_transaction` | Open a named undo transaction |
| `end_transaction` | Commit the open transaction |
| `undo_transaction` | Undo last transaction |
| `create_snapshot` | JSON snapshot of all actor states |

### Python Scripting
| Command | Description |
|---|---|
| `execute_python` | Run Python code in the editor process |

### Performance Profiling
| Command | Description |
|---|---|
| `get_perf_stats` | Frame time, draw calls, memory, actor count |

## The 4-Phase Verification Protocol

Every mutating command in UEAgentForge runs through this protocol before changes land:

**Phase 1 — PreFlight (bitmask: `0x01`)**
- Validates the proposed action against all constitution rules
- Captures pre-state actor list and count for comparison
- Fails fast if constitution is violated → no changes made

**Phase 2 — Snapshot + Rollback Test (bitmask: `0x02`)**
- Creates a named JSON snapshot of the current level
- Executes the command inside a temporary `FScopedTransaction`
- Cancels (rolls back) the transaction intentionally
- Verifies the actor count and labels exactly match the pre-state
- **Only if rollback succeeds**: re-executes the command for real
- This guarantees Ctrl+Z will always work correctly

**Phase 3 — PostVerify (bitmask: `0x04`)**
- Queries post-execution state
- Compares actor delta against expected (spawn: +1, delete: -1, transform: 0)
- Logs warnings on unexpected side effects (non-blocking)

**Phase 4 — BuildCheck (bitmask: `0x08`)**
- Iterates all dirty Blueprint assets
- Triggers compilation and checks for `BS_Error` status
- Cancels the real transaction if compilation fails
- Prevents breaking the project with every change

Run all 4 phases: `{"cmd": "run_verification", "args": {"phase_mask": 15}}`

## Constitution System

The constitution parser reads a markdown file at startup and enforces its rules at runtime.

Rules are extracted from bullet lists under headings containing:
`Non-negotiable`, `Rules`, `Constraints`, `Requirements`, or `Enforcement`.

Example:
```markdown
## Non-negotiable constraints

- No plugin source edits unless explicitly approved.
- One change per iteration.
- No magic numbers — use UPROPERTY.
```

Check an action at runtime:
```python
chk = client.enforce_constitution("edit Oceanology plugin source file")
# → {"allowed": false, "violations": ["[RULE_001] No plugin source edits..."]}
```

## Snapshots

Snapshots are stored as JSON in `{ProjectDir}/Saved/AgentForgeSnapshots/`.
Each snapshot captures all actor labels, classes, locations, and rotations.

```python
snap = client.create_snapshot("before_big_change")
# ... make changes ...
# Undo if needed:
client.undo_transaction()
```

## Requirements

- Unreal Engine 5.5 or later (5.7 recommended)
- Remote Control API plugin enabled
- PythonScriptPlugin enabled (optional — for `execute_python`)
- Windows / Linux / Mac (editor targets only)

## Contributing

Contributions welcome. Please:
1. Follow the one-change-per-PR rule
2. Test against UE 5.5+ before submitting
3. Update the command reference table if adding new commands
4. Add a Python example in `PythonClient/examples/` for new features

## License

MIT — see [LICENSE](LICENSE).

---

*UEAgentForge was born from [AquaEchos](https://github.com/your-org/AquaEchos),
a dolphin adventure game built entirely with AI-native Unreal Engine development workflows.*
