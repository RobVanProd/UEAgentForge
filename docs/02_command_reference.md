# Command Reference

All commands are sent as JSON to the Remote Control API endpoint:

```
PUT http://127.0.0.1:30010/remote/object/call
Content-Type: application/json

{
  "objectPath":   "/Script/UEAgentForge.Default__AgentForgeLibrary",
  "functionName": "ExecuteCommandJson",
  "parameters":   { "RequestJson": "{\"cmd\":\"ping\"}" }
}
```

The `RequestJson` value is a JSON string with a `cmd` field and optional `args` object:

```json
{ "cmd": "spawn_actor", "args": { "class_path": "/Script/Engine.StaticMeshActor", "x": 0, "y": 0, "z": 200 } }
```

---

## Forge Meta Commands

### `ping`
Health check. Returns plugin version and constitution status.

**Args:** none

**Response:**
```json
{
  "pong": "UEAgentForge v0.1.0",
  "version": "0.1.0",
  "constitution_loaded": true,
  "constitution_rules": 12
}
```

---

### `get_forge_status`
Full status of the plugin runtime — version, constitution, last verification.

**Args:** none

**Response:**
```json
{
  "version": "0.1.0",
  "constitution_loaded": true,
  "constitution_rules_loaded": 12,
  "constitution_path": "C:/Users/.../ue_dev_constitution.md",
  "last_verification": ""
}
```

---

### `run_verification`
Execute the 4-phase verification protocol. See [Verification Protocol](03_verification_protocol.md).

**Args:**

| Field | Type | Default | Description |
|---|---|---|---|
| `phase_mask` | int | `15` | Bitmask: 1=PreFlight, 2=Snapshot, 4=PostVerify, 8=BuildCheck |

**Response:**
```json
{
  "all_passed": true,
  "phases_run": 4,
  "details": [
    { "phase": "PreFlight",         "passed": true,  "detail": "...", "duration_ms": 2.1 },
    { "phase": "Snapshot+Rollback", "passed": true,  "detail": "...", "duration_ms": 45.3 },
    { "phase": "PostVerify",        "passed": true,  "detail": "...", "duration_ms": 1.8 },
    { "phase": "BuildCheck",        "passed": true,  "detail": "...", "duration_ms": 120.0 }
  ]
}
```

---

### `enforce_constitution`
Check whether a proposed action is allowed by the loaded constitution rules.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `action_description` | string | yes | Natural language description of the intended action |

**Response:**
```json
{ "allowed": false, "violations": ["[RULE_001] No plugin source edits unless explicitly approved."] }
```

---

## Observation Commands

### `get_all_level_actors`
Returns every actor currently in the open level.

**Args:** none

**Response:**
```json
{
  "actors": [
    {
      "name": "StaticMeshActor_0",
      "label": "MyFloor",
      "class": "StaticMeshActor",
      "object_path": "/Game/Maps/M_Underwater.M_Underwater:PersistentLevel.StaticMeshActor_0",
      "location": { "x": 0, "y": 0, "z": -5 },
      "rotation": { "pitch": 0, "yaw": 0, "roll": 0 },
      "scale":    { "x": 100, "y": 100, "z": 0.1 }
    }
  ]
}
```

---

### `get_actor_components`
List all components attached to an actor.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Actor label or name |

**Response:**
```json
{
  "components": [
    { "name": "StaticMeshComponent0", "class": "StaticMeshComponent", "object_path": "..." }
  ]
}
```

---

### `get_current_level`
Get the package path and metadata of the currently open level.

**Args:** none

**Response:**
```json
{
  "package_path":  "/Game/Maps/M_Underwater",
  "world_path":    "/Game/Maps/M_Underwater.M_Underwater",
  "actor_prefix":  "/Game/Maps/M_Underwater.M_Underwater:PersistentLevel.",
  "map_lock":      ""
}
```

---

### `assert_current_level`
Verify that the currently open level matches an expected path.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `expected_level` | string | yes | Expected level path (partial match accepted) |

**Response:**
```json
{ "ok": true, "expected_level": "/Game/Maps/M_Underwater", "current_package_path": "/Game/Maps/M_Underwater" }
```

---

### `get_actor_bounds`
Get the axis-aligned bounding box of an actor.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Actor label or name |

**Response:**
```json
{
  "origin":  { "x": 0, "y": 0, "z": 50 },
  "extent":  { "x": 100, "y": 100, "z": 50 },
  "box_min": { "x": -100, "y": -100, "z": 0 },
  "box_max": { "x": 100, "y": 100, "z": 100 }
}
```

---

## Actor Control Commands

All actor control commands are **mutating** and run through the full verification + transaction pipeline.

### `spawn_actor`
Spawn a new actor of a given class at a world transform.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `class_path` | string | yes | — | Full class path, e.g. `/Script/Engine.StaticMeshActor` |
| `x` | float | no | 0 | World X position (cm) |
| `y` | float | no | 0 | World Y position (cm) |
| `z` | float | no | 0 | World Z position (cm) |
| `pitch` | float | no | 0 | Pitch rotation (degrees) |
| `yaw` | float | no | 0 | Yaw rotation (degrees) |
| `roll` | float | no | 0 | Roll rotation (degrees) |

**Response:**
```json
{ "spawned_name": "StaticMeshActor_3", "spawned_object_path": "..." }
```

---

### `set_actor_transform`
Move and/or rotate an existing actor. Only fields provided are changed.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `object_path` | string | yes | Actor object path, label, or name |
| `x`, `y`, `z` | float | no | New world position (cm) |
| `pitch`, `yaw`, `roll` | float | no | New rotation (degrees) |

**Response:**
```json
{ "ok": true, "actor_object_path": "..." }
```

---

### `delete_actor`
Delete an actor from the level by label.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `label` | string | yes | Actor label or name |

**Response:**
```json
{ "ok": true, "deleted": true }
```

---

### `save_current_level`
Save the currently open level to disk.

**Args:** none

**Response:**
```json
{ "ok": true, "detail": "Level saved." }
```

---

### `take_screenshot`
Capture the editor viewport as a PNG file.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `filename` | string | no | `"AgentForge_Screenshot"` | Base filename (timestamp appended) |

**Response:**
```json
{ "ok": true, "path": "C:/...Saved/AgentForgeScreenshots/AgentForge_Screenshot_20260226_143022.png" }
```

Saved to: `{ProjectDir}/Saved/AgentForgeScreenshots/`

---

## Spatial Query Commands

### `cast_ray`
Perform a line trace in the editor world.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `start` | `{x,y,z}` | yes | — | Ray start (cm) |
| `end` | `{x,y,z}` | yes | — | Ray end (cm) |
| `trace_complex` | bool | no | `true` | Use complex collision |

**Response:**
```json
{
  "hit": true,
  "hit_location": { "x": 150.0, "y": 0, "z": 0 },
  "hit_normal":   { "x": 0, "y": 0, "z": 1 },
  "hit_actor":    "MyFloor",
  "distance":     250.0
}
```

---

### `query_navmesh`
Project a point onto the navigation mesh and test reachability.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `x`, `y`, `z` | float | yes | — | Point to query (cm) |
| `extent_x`, `extent_y`, `extent_z` | float | no | 100/100/200 | Search extent (cm) |

**Response:**
```json
{ "on_navmesh": true, "projected_location": { "x": 0, "y": 0, "z": 0 } }
```

---

## Blueprint Manipulation Commands

### `create_blueprint`
Create a new Blueprint asset.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `name` | string | yes | Asset name, e.g. `"BP_MyActor"` |
| `parent_class` | string | yes | Parent class path, e.g. `"/Script/Engine.Actor"` |
| `output_path` | string | yes | Content path, e.g. `"/Game/MyBPs"` |

**Response:**
```json
{ "ok": true, "package": "/Game/MyBPs/BP_MyActor", "generated_class_path": "..." }
```

---

### `compile_blueprint`
Compile an existing Blueprint and check for errors.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `blueprint_path` | string | yes | Full package path to the Blueprint |

**Response:**
```json
{ "ok": true, "errors": "" }
```

---

### `set_bp_cdo_property`
Set a property value on a Blueprint's Class Default Object (CDO).

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `blueprint_path` | string | yes | Blueprint package path |
| `property_name` | string | yes | UPROPERTY name |
| `type` | string | yes | `float`, `int`, `bool`, `string`, `name` |
| `value` | string | yes | String representation of the value |

**Response:**
```json
{ "ok": true, "property": "CruiseSpeed", "type": "float", "value_set": "250.0" }
```

---

### `edit_blueprint_node`
Edit pin values on an existing node in a Blueprint event graph.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `blueprint_path` | string | yes | Blueprint package path |
| `node_spec` | object | yes | Node specification (see below) |

`node_spec` object:

| Field | Type | Description |
|---|---|---|
| `type` | string | Node type hint (e.g. `"CallFunction"`) |
| `title` | string | Node title to match (partial match) |
| `pins` | array | `[{"name": "PinName", "value": "NewDefault"}]` |

**Response:**
```json
{ "ok": true, "node_guid": "XXXXXXXX-...", "action": "pins_updated" }
```

If the node is not found, the response lists all available nodes:
```json
{ "error": "Node 'MyNode' not found. Available nodes: [BeginPlay, Tick, ...]" }
```

---

## Material Instancing Commands

### `create_material_instance`
Create a Material Instance Constant from a parent material.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `parent_material` | string | yes | Parent material package path |
| `instance_name` | string | yes | New asset name |
| `output_path` | string | yes | Output content path |

**Response:**
```json
{ "ok": true, "package": "/Game/Materials/MI_MyInstance" }
```

---

### `set_material_params`
Set scalar and/or vector parameters on a Material Instance Constant.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `instance_path` | string | yes | MIC package path |
| `scalar_params` | object | no | `{"ParamName": 1.5, ...}` |
| `vector_params` | object | no | `{"ParamName": {"r":1,"g":0,"b":0,"a":1}, ...}` |

**Response:**
```json
{ "ok": true, "scalars_set": 2, "vectors_set": 1 }
```

---

## Content Management Commands

### `rename_asset`
Rename an asset in the Content Browser.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Current full asset path |
| `new_name` | string | yes | New asset name (no path) |

**Response:**
```json
{ "ok": true, "new_path": "/Game/MyContent/NewName" }
```

---

### `move_asset`
Move an asset to a different content folder.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Current full asset path |
| `destination_path` | string | yes | Target folder path |

**Response:**
```json
{ "ok": true, "new_path": "/Game/NewFolder/AssetName" }
```

---

### `delete_asset`
Delete a content browser asset. The engine performs reference checking.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Full asset path |

**Response:**
```json
{ "ok": true, "deleted": true }
```

---

## Transaction Safety Commands

### `begin_transaction`
Open a named undo transaction. All editor operations until `end_transaction` are grouped.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `label` | string | no | `"AgentForge"` | Display name for the undo history entry |

**Response:**
```json
{ "ok": true, "label": "My Change" }
```

---

### `end_transaction`
Commit and close the open transaction.

**Args:** none

**Response:**
```json
{ "ok": true, "ops_count": 3 }
```

---

### `undo_transaction`
Undo the last transaction (equivalent to Ctrl+Z).

**Args:** none

**Response:**
```json
{ "ok": true, "detail": "Undo executed." }
```

---

### `create_snapshot`
Save a JSON snapshot of all current actors to disk.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `snapshot_name` | string | no | `"snapshot"` | Base name for the file |

**Response:**
```json
{ "ok": true, "path": "C:/...Saved/AgentForgeSnapshots/snapshot_20260226_143022.json", "actor_count": 42 }
```

Snapshots are stored in: `{ProjectDir}/Saved/AgentForgeSnapshots/`

---

## Python Scripting

### `execute_python`
Execute arbitrary Python code inside the Unreal Editor process.

Requires the **Python Script Plugin** to be enabled in your project.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `script` | string | yes | Python code to execute |

**Response:**
```json
{ "ok": true, "output": "...", "errors": "" }
```

**Example:**
```json
{
  "cmd": "execute_python",
  "args": { "script": "import unreal; print(unreal.EditorLevelLibrary.get_all_level_actors())" }
}
```

---

## Performance Profiling

### `get_perf_stats`
Capture current editor performance metrics.

**Args:** none

**Response:**
```json
{
  "actor_count":      47,
  "component_count":  183,
  "draw_calls":       1240,
  "primitives":       8500,
  "memory_used_mb":   4200.5,
  "memory_total_mb":  32768.0,
  "gpu_ms":           8.3
}
```

---

## Scene Setup

### `setup_test_level`
Spawn a standard set of test geometry actors (ground plane + 4 cubes) for agent verification testing.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `floor_size` | float | no | 10000.0 | Floor plane size in cm |

**Response:**
```json
{
  "ok": true,
  "log": ["Spawned AgentForge_Ground at (0,0,-5)", "..."],
  "test_actors": ["AgentForge_Ground", "AgentForge_CubeA", "AgentForge_CubeB", "AgentForge_CubeC", "AgentForge_CubeD"]
}
```

---

## Error Response Format

Any command that fails returns:
```json
{ "error": "Description of what went wrong." }
```

Common errors:
- `"Actor not found: MyLabel"` — label or name does not exist in the level
- `"Blueprint not found: /Game/..."` — asset path is wrong or asset not loaded
- `"PreFlight FAILED: Constitution violations: ..."` — constitution blocked the action
- `"No editor world."` — editor is in a state with no open level
- `"PythonScriptPlugin not available."` — Python plugin is not enabled
