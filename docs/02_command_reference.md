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

### `get_world_context`
Build an LLM-oriented world-state packet from multiple subsystems in one call.
This is the recommended context hub before planning or mutating actions.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `max_actors` | int | no | `120` | Max actors included in the packet (priority-trimmed) |
| `max_relationships` | int | no | `48` | Max inferred relationships to include |
| `include_components` | bool | no | `false` | Include component counts per actor |
| `include_screenshot` | bool | no | `true` | Queue a fresh viewport screenshot and include path in response |
| `screenshot_label` | string | no | `world_context` | Prefix used for the queued screenshot filename |

**Response (shape):**
```json
{
  "ok": true,
  "schema": "world_context_v1",
  "generated_at_utc": "2026-03-03T23:12:10Z",
  "budget": {
    "max_actors": 120,
    "selected_actors": 120,
    "source_actors": 403,
    "truncated": true,
    "max_relationships": 48
  },
  "level": { "package_path": "/Game/...", "world_path": "/Game/..."},
  "semantic": { "horror_score": 68.4, "horror_rating": "High", "...": "..." },
  "composition": { "actor_count": 403, "density_score": 4.2, "...": "..." },
  "category_counts": { "objective": 14, "player": 1, "ai": 7, "environment": 310, "...": 0 },
  "actors": [{ "label": "HE_Door_A", "category": "objective", "location": {"x":0,"y":0,"z":0} }],
  "gameplay_anchors": [{ "label": "HE_Key_A", "category": "objective", "location": {"x":0,"y":0,"z":0} }],
  "relationships": [{ "from":"HE_Key_A", "to":"HE_Door_A", "type":"matching_suffix", "confidence":0.95 }],
  "spatial_hotspots": [{ "cell": 6, "actor_count": 89, "dominant_category": "environment", "center": {"x":0,"y":0,"z":0} }],
  "screenshot": { "requested": true, "queued": true, "path": "C:/HGShots/world_context_20260303_190233.png" },
  "llm_brief": [
    "Map: /Game/HorrorEngine/Maps/HE_OpenWorld_AAA_Step1",
    "Actors: 403 total, 120 included in context packet",
    "Screenshot queued: C:/HGShots/world_context_20260303_190233.png"
  ],
  "warnings": ["Context truncated: 283 actors omitted by max_actors budget."],
  "suggested_next_cmds": ["get_deep_properties", "get_actors_in_radius", "observe_analyze_plan_act"]
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

### `get_available_meshes`
Search the Content Browser for static meshes by keyword and/or folder path.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `search_filter` | string | no | `""` | Case-insensitive substring match against asset name |
| `path_filter` | string | no | `""` | Package path filter, e.g. `"/Game/Environment"` |
| `max_results` | int | no | `50` | Maximum number of matches returned |

**Response:**
```json
{
  "ok": true,
  "search_filter": "wall",
  "path_filter": "/Game/Environment",
  "count": 2,
  "assets": [
    {
      "asset_name": "SM_Wall_01",
      "asset_path": "/Game/Environment/SM_Wall_01.SM_Wall_01",
      "package_path": "/Game/Environment",
      "class": "StaticMesh"
    }
  ]
}
```

Use this before `set_static_mesh` so agents prefer project assets over fallback engine primitives.

---

### `get_available_materials`
Search the Content Browser for materials and material instances by keyword and/or folder path.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `search_filter` | string | no | `""` | Case-insensitive substring match against asset name |
| `path_filter` | string | no | `""` | Package path filter, e.g. `"/Game/Materials"` |
| `max_results` | int | no | `50` | Maximum number of matches returned |

**Response:**
```json
{
  "ok": true,
  "search_filter": "concrete",
  "path_filter": "/Game/Materials",
  "count": 1,
  "assets": [
    {
      "asset_name": "MI_Concrete_Wet",
      "asset_path": "/Game/Materials/MI_Concrete_Wet.MI_Concrete_Wet",
      "package_path": "/Game/Materials",
      "class": "MaterialInstanceConstant"
    }
  ]
}
```

Use this before `apply_material_to_actor` so agents select real project materials before falling back to flat tinting.

---

### `get_available_blueprints`
Search Blueprint assets by keyword, path, and optional parent class filter.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `search_filter` | string | no | `""` | Case-insensitive substring match against asset name |
| `path_filter` | string | no | `""` | Package path filter |
| `parent_class` | string | no | `""` | Parent class name hint, e.g. `Actor` |
| `max_results` | int | no | `50` | Maximum number of matches returned |

**Response:**
```json
{
  "ok": true,
  "count": 1,
  "assets": [
    {
      "asset_name": "BP_Sky_Sphere",
      "asset_path": "/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere",
      "generated_class_path": "/Script/Engine.BlueprintGeneratedClass'/Engine/EngineSky/BP_Sky_Sphere.BP_Sky_Sphere_C'",
      "parent_class_path": "/Script/CoreUObject.Class'/Script/Engine.Actor'"
    }
  ]
}
```

---

### `get_available_textures`
Search `Texture2D` assets by keyword and/or folder path.

**Args:** same shape as `get_available_meshes`

**Response:**
```json
{
  "ok": true,
  "count": 10,
  "assets": [
    {
      "asset_name": "DefaultTexture",
      "asset_path": "/Engine/EngineResources/DefaultTexture.DefaultTexture",
      "package_path": "/Engine/EngineResources",
      "class": "Texture2D"
    }
  ]
}
```

---

### `get_available_sounds`
Search `SoundWave` and `SoundCue` assets by keyword and/or folder path.

**Args:** same shape as `get_available_meshes`

**Response:**
```json
{
  "ok": true,
  "count": 2,
  "assets": [
    {
      "asset_name": "1kSineTonePing",
      "asset_path": "/Engine/EngineSounds/1kSineTonePing.1kSineTonePing",
      "package_path": "/Engine/EngineSounds",
      "class": "SoundWave"
    }
  ]
}
```

---

### `get_asset_details`
Inspect supported asset metadata for meshes, materials, textures, sounds, and blueprints.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `asset_path` | string | yes | Full object path to the asset |

**Response (mesh example):**
```json
{
  "ok": true,
  "asset_name": "Cube",
  "asset_path": "/Engine/BasicShapes/Cube.Cube",
  "class": "StaticMesh",
  "package": "/Engine/BasicShapes/Cube",
  "lod_count": 1,
  "bounds_origin": { "x": 0, "y": 0, "z": 0 },
  "bounds_extent": { "x": 50, "y": 50, "z": 50 }
}
```

---

### `get_actor_property`
Read an actor or component property using dot-path notation.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `property_name` | string | yes | Property path, e.g. `LightComponent.Intensity` |

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallFill",
  "property_name": "LightComponent.Intensity",
  "value": "2500.000000"
}
```

---

## Viewport & Capture Commands

### `set_viewport_camera`
Move the first perspective editor viewport camera to a specific world transform.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `x` | float | no | `0` | Camera world X |
| `y` | float | no | `0` | Camera world Y |
| `z` | float | no | `170` | Camera world Z |
| `pitch` | float | no | `0` | Camera pitch |
| `yaw` | float | no | `0` | Camera yaw |
| `roll` | float | no | `0` | Camera roll |

**Response:**
```json
{
  "ok": true,
  "x": 0,
  "y": -600,
  "z": 300,
  "pitch": -15,
  "yaw": 90
}
```

This changes the editor viewport only. It does not move any actors.

---

### `redraw_viewports`
Force all editor viewports to render a fresh frame.

**Args:** none

**Response:**
```json
{ "ok": true, "detail": "All viewports redrawn." }
```

Call this immediately before `take_screenshot` when the editor may be idle.

---

### `focus_viewport_on_actor`
Frame the active perspective editor viewport on a target actor.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Batch2_Corridor",
  "actor_object_path": "/Temp/Untitled_1.Untitled_1:PersistentLevel.Actor_0"
}
```

---

### `get_viewport_info`
Read transform and render dimensions from the active perspective editor viewport.

**Args:** none

**Response:**
```json
{
  "ok": true,
  "location": { "x": 75201.9, "y": 85553.0, "z": 44621.0 },
  "pitch": -24.2,
  "yaw": -854.4,
  "roll": 0.0,
  "fov": 90.0,
  "width": 1315,
  "height": 379
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

### `duplicate_actor`
Duplicate an existing actor and optionally offset the duplicate in world space.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `actor_name` | string | yes | — | Source actor label or name |
| `offset_x` | float | no | `0` | World-space X offset |
| `offset_y` | float | no | `0` | World-space Y offset |
| `offset_z` | float | no | `0` | World-space Z offset |

**Response:**
```json
{
  "ok": true,
  "source_actor": "Wall_A",
  "actor_name": "Wall_A2",
  "actor_object_path": "/Game/Maps/Test.Test:PersistentLevel.StaticMeshActor_12",
  "location": { "x": 200, "y": 0, "z": 0 }
}
```

---

### `spawn_point_light`
Spawn a movable `PointLight` actor with intensity, color, and attenuation radius.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `x` | float | yes | — | World X position |
| `y` | float | yes | — | World Y position |
| `z` | float | yes | — | World Z position |
| `intensity` | float | no | `5000` | Point light intensity |
| `color_r`, `color_g`, `color_b` | float | no | `1/1/1` | RGB light color |
| `attenuation_radius` | float | no | `1200` | Light attenuation radius in cm |
| `label` | string | no | `""` | Optional actor label |

**Response:**
```json
{
  "ok": true,
  "spawned_name": "PointLight_2",
  "spawned_object_path": "...",
  "label": "HallFill"
}
```

---

### `spawn_spot_light`
Spawn a movable `SpotLight` actor with transform, color, and cone angles.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `x` | float | yes | — | World X position |
| `y` | float | yes | — | World Y position |
| `z` | float | yes | — | World Z position |
| `rx`, `ry`, `rz` | float | no | `0/0/0` | Spot light rotation |
| `intensity` | float | no | `5000` | Spot light intensity |
| `color_r`, `color_g`, `color_b` | float | no | `1/1/1` | RGB light color |
| `inner_cone_angle` | float | no | `15` | Inner cone angle in degrees |
| `outer_cone_angle` | float | no | `30` | Outer cone angle in degrees |
| `label` | string | no | `""` | Optional actor label |

**Response:**
```json
{
  "ok": true,
  "spawned_name": "SpotLight_1",
  "spawned_object_path": "...",
  "label": "DoorAccent"
}
```

---

### `spawn_rect_light`
Spawn a movable `RectLight` actor with source dimensions and intensity.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `x`, `y`, `z` | float | no | `0/0/0` | World position |
| `rx`, `ry`, `rz` | float | no | `0/0/0` | Rotation |
| `width` | float | no | `100` | Source width in cm |
| `height` | float | no | `100` | Source height in cm |
| `intensity` | float | no | `5000` | Light intensity |
| `color_r`, `color_g`, `color_b` | float | no | `1/1/1` | RGB light color |
| `label` | string | no | `""` | Optional actor label |

**Response:**
```json
{
  "ok": true,
  "spawned_name": "RectLight_1",
  "spawned_object_path": "...",
  "label": "WindowFill"
}
```

---

### `spawn_directional_light`
Spawn a movable `DirectionalLight` actor with rotation and intensity.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `rx`, `ry`, `rz` | float | no | `-45/0/0` | Rotation |
| `intensity` | float | no | `10` | Light intensity |
| `color_r`, `color_g`, `color_b` | float | no | `1/1/1` | RGB light color |
| `label` | string | no | `""` | Optional actor label |

**Response:**
```json
{
  "ok": true,
  "spawned_name": "DirectionalLight_0",
  "spawned_object_path": "...",
  "label": "MainSun"
}
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

### `set_static_mesh`
Swap the `UStaticMesh` used by an actor's static mesh component.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `mesh_path` | string | yes | Full static mesh object path |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Wall_A",
  "mesh_path": "/Game/Environment/SM_Wall_01.SM_Wall_01"
}
```

Use `get_available_meshes` first when the correct project mesh is unknown.

---

### `set_actor_scale`
Set an actor's world scale. Missing axis fields keep the current value.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `sx`, `sy`, `sz` | float | no | New scale values for X/Y/Z |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Wall_A",
  "scale": { "x": 4.0, "y": 0.25, "z": 3.0 }
}
```

---

### `set_actor_label`
Rename an actor label in the editor.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `new_label` | string | yes | New editor label |

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallWall_A",
  "actor_object_path": "/Game/Maps/Test.Test:PersistentLevel.StaticMeshActor_4"
}
```

---

### `set_actor_mobility`
Set every scene component on an actor to `Static`, `Stationary`, or `Movable`.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `mobility` | string | yes | `Static`, `Stationary`, or `Movable` |

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallWall_A",
  "mobility": "Movable"
}
```

---

### `set_actor_visibility`
Toggle whether an actor is visible in the editor and game.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `visible` | bool | yes | Desired visibility |

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallWall_A",
  "visible": false
}
```

---

### `group_actors`
Parent a set of existing actors under a named root actor for compound layout control.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_names` | array<string> | yes | Labels or names of the actors to group |
| `group_name` | string | yes | Label for the spawned root/group actor |

**Response:**
```json
{
  "ok": true,
  "group_name": "HallSegment_A",
  "group_object_path": "/Game/Maps/Test.Test:PersistentLevel.Actor_2",
  "children": [
    { "label": "HallWall_A", "object_path": "..." }
  ]
}
```

---

### `set_actor_property`
Write an actor or component property using dot-path notation.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `actor_name` | string | yes | Actor label or name |
| `property_name` | string | yes | Property path, e.g. `LightComponent.Intensity` |
| `value` | string | yes | Unreal text-import value |

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallFill",
  "property_name": "LightComponent.Intensity",
  "value": "2500.000000"
}
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

## Scene Construction Commands

These compound tools build scaled primitive geometry using engine fallback meshes and optional material assignment.

### `create_wall`
Spawn a wall segment between two world points.

**Key args:** `start_x`, `start_y`, `end_x`, `end_y`, `z`, `height`, `thickness`, `material_path`, `label`

**Response:**
```json
{
  "ok": true,
  "actor_name": "HallWall_A",
  "actor_object_path": "...",
  "length": 600.0,
  "height": 300.0,
  "thickness": 20.0
}
```

---

### `create_floor`
Spawn a floor slab centered on a rectangle.

**Key args:** `center_x`, `center_y`, `z`, `width`, `depth`, `thickness`, `material_path`, `label`

---

### `create_room`
Spawn a grouped room from a floor and four walls.

**Key args:** `center_x`, `center_y`, `z`, `width`, `depth`, `height`, `wall_thickness`, `floor_material`, `wall_material`, `label`

**Response shape:** returns `group_name`, `group_object_path`, `child_count`, and `children[]`.

---

### `create_corridor`
Spawn a grouped corridor from floor, two walls, and optional ceiling.

**Key args:** `start_x`, `start_y`, `end_x`, `end_y`, `z`, `width`, `height`, `wall_thickness`, `include_ceiling`, `floor_material`, `wall_material`, `label`

**Response shape:** returns `group_name`, `group_object_path`, `child_count`, and `children[]`.

---

### `create_pillar`
Spawn a pillar primitive. Box mode is used for low side counts, cylinder mode for round pillars.

**Key args:** `x`, `y`, `z`, `radius`, `height`, `sides`, `material_path`, `label`

**Response:**
```json
{
  "ok": true,
  "actor_name": "Pillar_A",
  "actor_object_path": "...",
  "radius": 25.0,
  "height": 300.0,
  "sides": 16
}
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
{ "ok": true, "path": "C:/HGShots/AgentForge_Screenshot_20260318_021530.png" }
```

Saved to: `C:/HGShots/`

The screenshot is written on the next rendered frame. Use `redraw_viewports` first when capturing from an unattended editor session.

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

### `apply_material_to_actor`
Apply a material to a mesh component slot on an actor.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `actor_name` | string | yes | — | Actor label or name |
| `material_path` | string | yes | — | Full material or MIC object path |
| `slot_index` | int | no | `0` | Mesh material slot to update |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Wall_A",
  "material_path": "/Game/Materials/MI_Concrete_Wet.MI_Concrete_Wet",
  "slot_index": 0
}
```

---

### `set_mesh_material_color`
Create or reuse a dynamic material instance on an actor mesh and attempt a common tint parameter update.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `actor_name` | string | yes | — | Actor label or name |
| `r`, `g`, `b` | float | yes | — | RGB tint values |
| `a` | float | no | `1.0` | Alpha value |
| `slot_index` | int | no | `0` | Mesh material slot to update |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Wall_A",
  "slot_index": 0,
  "attempted_parameters": ["BaseColor", "Color", "Tint", "Base_Color"],
  "color": { "x": 0.18, "y": 0.2, "z": 0.22 },
  "alpha": 1.0
}
```

This is the fallback when project-specific materials are unavailable or too expensive to search for in the current step.

---

### `set_material_scalar_param`
Set a scalar parameter on a dynamic material instance assigned to an actor mesh slot.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `actor_name` | string | yes | — | Actor label or name |
| `param_name` | string | yes | — | Scalar parameter name |
| `value` | float | yes | — | Scalar value to assign |
| `slot_index` | int | no | `0` | Mesh material slot to update |

**Response:**
```json
{
  "ok": true,
  "actor_name": "Wall_A",
  "param_name": "Roughness",
  "slot_index": 0,
  "value": 0.8
}
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

## Spatial Intelligence Commands (v0.2.0)

### `spawn_actor_at_surface`
Raycast downward and spawn an actor flush to the nearest surface, aligned to surface normal.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `class_path` | string | yes | — | Actor class path |
| `x`, `y`, `z` | float | yes | — | World origin for downward raycast |
| `offset_z` | float | no | `0` | Additional Z offset after surface snap |

**Response:** `{ "ok": true, "spawned_name": "...", "surface_z": 0.0, "normal": {...} }`

---

### `align_actors_to_surface`
Drop a list of actors to the nearest surface beneath each one.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `labels` | array | yes | Actor labels to align |

**Response:** `{ "ok": true, "aligned": 4, "skipped": 0 }`

---

### `get_surface_normal_at`
Return the surface normal at a given world point (useful for AI placement decisions).

**Args:** `x`, `y`, `z` (float, required)

**Response:** `{ "hit": true, "normal": {"x": 0, "y": 0, "z": 1}, "location": {...} }`

---

### `analyze_level_composition`
Return an AI-readable scene analysis: actor density, bounding box, coverage gaps, and placement suggestions.

**Args:** none

**Response:** `{ "actor_count": 64, "bounds": {...}, "density": "medium", "suggestions": ["..."] }`

---

### `get_actors_in_radius`
Find all actors within a sphere, sorted by distance.

**Args:** `x`, `y`, `z`, `radius` (float, required)

**Response:** `{ "actors": [{"label": "...", "distance": 150.0}, ...] }`

---

## FAB Marketplace Commands (v0.2.0)

### `search_fab_assets`
Search Fab.com for assets matching a query. Uses Fab's internal JSON API (undocumented, may change).

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `query` | string | yes | — | Search terms |
| `max_results` | int | no | `20` | Max results (capped at 50) |
| `free_only` | bool | no | `true` | Filter to free assets only |

**Response:** `{ "ok": true, "count": 5, "results": [{"title": "...", "id": "...", "price": 0, "url": "..."}] }`

---

### `download_fab_asset`
**Stub** — Fab.com has no public download API. Returns workaround instructions for using the EGL or the in-editor Fab plugin.

---

### `import_local_asset`
Import a file from disk into the Content Browser. Supports FBX, OBJ, PNG, JPG, TGA, BMP, EXR, WAV.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `file_path` | string | yes | — | Absolute path to the file |
| `destination_path` | string | no | `/Game/FabImports` | Target content folder |

**Response:** `{ "ok": true, "asset_path": "/Game/FabImports/MyMesh", "type": "StaticMesh", "imported_count": 1 }`

---

### `list_imported_assets`
List all assets in a content folder (recursive).

**Args:** `content_path` (string, default `/Game/FabImports`)

**Response:** `{ "ok": true, "count": 3, "assets": [{"asset_name": "...", "asset_path": "...", "type": "..."}] }`

---

### `enhance_current_level` (v0.2.0)
Natural language → composition analysis + snapshot + constitution verify + screenshot. Orchestrates multiple commands in sequence.

**Args:** `instruction` (string, required) — e.g. `"Make this level feel more like a horror dungeon"`

---

## Advanced Intelligence Commands (v0.3.0)

### `get_multi_view_capture`
Capture the scene from multiple camera angles simultaneously and return metadata for each view.

**Args:** `angles` (array of `{pitch, yaw}`, optional), `resolution` (object, optional)

---

### `get_level_hierarchy`
Return the full actor hierarchy of the current level, including parent-child relationships and component trees.

**Args:** none

---

### `get_deep_properties`
Fetch detailed property values from an actor, including nested component properties.

**Args:** `label` (string, required)

---

### `get_semantic_env_snapshot`
Return a rich semantic description of the current level — actor roles, spatial layout, lighting mood, AI nav coverage — formatted for LLM context.

**Args:** none

---

### `place_asset_thematically`
Place an asset class using genre-aware rules (horror, sci-fi, fantasy etc.) — chooses location, scale, and rotation based on existing level context.

**Args:** `class_path` (string), `theme` (string), optional position hints

---

### `refine_level_section`
Analyze a spatial region and improve it — adds props, adjusts lighting, removes clutter — based on genre rules.

**Args:** `center` (`{x,y,z}`), `radius` (float), `goal` (string)

---

### `apply_genre_rules`
Apply a set of named genre rules (e.g. `"horror"`) to the entire level — adjusts lighting color, fog density, prop density, and atmosphere.

**Args:** `genre` (string: `horror`, `sci_fi`, `fantasy`, `military`, `default`)

---

### `create_in_editor_asset`
Create a new content browser asset of a given class type using editor factories.

**Args:** `class_path` (string), `asset_name` (string), `output_path` (string)

---

### `observe_analyze_plan_act`
Full Observe-Analyze-Plan-Act loop. Takes a high-level goal, analyzes the scene, plans a sequence of commands, and executes them with verification.

**Args:** `goal` (string, required) — e.g. `"Add atmospheric candles along the left wall"`

---

### `enhance_horror_scene`
Specialized closed-loop pipeline for horror game levels: analyzes tension, shadow coverage, escape routes, and applies targeted improvements.

**Args:** `target_area` (string, optional), `intensity` (float 0..1, optional)

---

### `set_bt_blackboard`
Link a BlackboardData asset to a BehaviorTree asset. Bypasses the Python `CPF_Protected` restriction on `BehaviorTree.BlackboardAsset`.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `bt_path` | string | yes | BehaviorTree asset path |
| `bb_path` | string | yes | BlackboardData asset path |

---

### `wire_aicontroller_bt`
Wire `BeginPlay → RunBehaviorTree(BT)` in an AIController Blueprint event graph. Bypasses the Python `CPF_Protected` restriction on `UbergraphPages`.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `controller_path` | string | yes | AIController Blueprint path |
| `bt_path` | string | yes | BehaviorTree asset path |

---

## Level Preset System (v0.4.0)

### `list_presets`
List all available named level presets (built-in + user-saved).

**Args:** none

**Response:**
```json
{
  "ok": true,
  "count": 5,
  "presets": [
    { "name": "Default",  "style": "default",  "description": "..." },
    { "name": "Horror",   "style": "horror",   "description": "..." },
    { "name": "SciFi",    "style": "sci_fi",   "description": "..." },
    { "name": "Fantasy",  "style": "fantasy",  "description": "..." },
    { "name": "Military", "style": "military", "description": "..." }
  ]
}
```

---

### `load_preset`
Apply a named preset's settings to the current level (lighting defaults, actor density targets, atmosphere parameters).

**Args:** `name` (string, required) — e.g. `"Horror"`

---

### `save_preset`
Save the current level's configuration as a named preset for future use.

**Args:** `name` (string, required), `description` (string, optional)

---

### `suggest_preset`
Analyze the current project's content and suggest the best matching preset.

**Args:** none

**Response:** `{ "suggested": "Horror", "confidence": 0.87, "reason": "..." }`

---

### `get_current_preset`
Return the name and settings of the currently active preset.

**Args:** none

---

## Five-Phase AAA Level Pipeline (v0.4.0)

### `generate_full_quality_level`
**Master command.** Runs all 5 phases in sequence with a quality-scoring loop — blockout → whitebox → set dressing → lighting → living systems. Returns a full report.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `preset` | string | no | `"Default"` | Preset to use (`Horror`, `SciFi`, `Fantasy`, etc.) |
| `goal` | string | no | `""` | Natural language level goal |
| `quality_target` | float | no | `0.75` | Target quality score 0..1 (loops phases until reached or max iterations hit) |

**Response:**
```json
{
  "ok": true,
  "phases_completed": 5,
  "quality_score": 0.82,
  "actors_spawned": 47,
  "report": "Phase I: Blockout — 12 volumes placed. Phase II: ..."
}
```

---

### `create_blockout_level`
**Phase I.** Generate rough blockout geometry — floor, ceiling, walls, and major volume shapes — using the active preset's spatial parameters.

**Args:** `preset` (string, optional), `bounds` (`{x,y,z}` extents, optional)

---

### `convert_to_whitebox_modular`
**Phase II.** Replace blockout geometry with modular static mesh pieces from the project's content library, matching surface types and junctions.

**Args:** `mesh_library_path` (string, optional)

---

### `apply_set_dressing`
**Phase III.** Add storytelling props, environmental details, and interactive objects based on genre rules and spatial analysis.

**Args:** `theme` (string, optional), `density` (float 0..1, optional)

---

### `apply_professional_lighting`
**Phase IV.** Set up atmosphere: directional light, sky light, fog, post-process volume, and accent lights appropriate for the active preset.

**Args:** `preset` (string, optional), `time_of_day` (string: `day`, `dusk`, `night`)

---

### `add_living_systems`
**Phase V.** Add dynamic elements: NavMesh, AI spawn points, ambient particle systems, and audio volumes to make the level feel alive.

**Args:** `preset` (string, optional)

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

---

## Deterministic Procedural Operators

These commands enforce an operator-oriented workflow: the agent controls seeds, palettes,
and high-level parameters while PCG/spline/road systems perform placement.

### `get_procedural_capabilities`
Scan installed/enabled procedural plugins and report operator support tiers.

**Args:**

| Field | Type | Required | Default | Description |
|---|---|---|---|---|
| `include_repo_urls` | bool | no | `true` | Include source URLs in response |

**Response (example):**
```json
{
  "ok": true,
  "plugins": { "pcg_builtin": { "installed": true, "enabled": true } },
  "operator_support": { "surface_scatter": "native", "road_layout": "spline_fallback" },
  "policy": { "operator_only": true }
}
```

---

### `get_operator_policy`
Return current runtime constraints for operator-mode generation.

**Args:** none

**Response:**
```json
{
  "ok": true,
  "operator_only": true,
  "allow_atomic_placement": false,
  "max_poi_per_call": 48,
  "max_actor_delta_per_pipeline": 1200,
  "max_memory_used_mb": 24576.0,
  "max_spawn_points": 50000,
  "max_cluster_count": 1024,
  "max_generation_time_ms": 30000.0
}
```

---

### `set_operator_policy`
Update runtime constraints for operator-mode generation.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `operator_only` | bool | no | Block direct atomic placement when true |
| `allow_atomic_placement` | bool | no | Explicit override for direct placement |
| `max_poi_per_call` | int | no | POI spawn cap per `op_stamp_poi` call |
| `max_actor_delta_per_pipeline` | int | no | Rollback threshold for `run_operator_pipeline` |
| `max_memory_used_mb` | float | no | Rollback threshold for `run_operator_pipeline` |
| `max_spawn_points` | int | no | Upper bound for generated distribution points per operator call |
| `max_cluster_count` | int | no | Upper bound for generated clusters in cluster distribution mode |
| `max_generation_time_ms` | float | no | Hard generation-time budget for procedural stages |

---

### `op_surface_scatter`
Apply scatter parameters to a target PCG actor/component and optionally trigger generation.
This operator now routes placement intent through `DistributionEngine` before generation.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `target_label` or `pcg_volume_label` | string | yes | Target actor label/name/path |
| `parameters` | object | no | Property map applied to actor/components |
| `distribution_mode` | string | no | `blue_noise`, `poisson`, or `cluster` |
| `density` | float | no | Sampling density used to estimate candidate count |
| `cluster_radius` | float | no | Cluster spread (cluster mode) |
| `min_spacing` | float | no | Minimum spacing radius for blue-noise/poisson |
| `height_range` | array<[min,max]> | no | Height filter |
| `slope_range` | array<[min,max]> | no | Slope filter in degrees |
| `distance_mask` | object | no | `{origin:{x,y,z}, min, max}` distance mask |
| `cluster_count` | int | no | Explicit cluster count override |
| `max_spawn_points` | int | no | Point cap for safety/performance |
| `max_cluster_count` | int | no | Cluster cap for safety/performance |
| `max_generation_time_ms` | float | no | Time budget for distribution build |
| `use_density_gradient` | bool | no | Enable radial density falloff field |
| `density_sigma` | float | no | Radial falloff sigma (Gaussian) |
| `density_noise` | float | no | Perlin blend amount for density variation |
| `density_field_resolution` | int | no | Density field grid resolution |
| `clearings` | object | no | `{density,count,radius_min,radius_max}` clearing controls |
| `clearing_density` | float | no | Scalar clearing density (alt to `clearings`) |
| `clearing_count` | int | no | Explicit clearing count |
| `clearing_radius_min` | float | no | Clearing min radius (uu) |
| `clearing_radius_max` | float | no | Clearing max radius (uu) |
| `biome_count` | int | no | Voronoi biome seed count |
| `biome_types` | array<string> | no | Biome labels (`forest`,`meadow`,`rock_field`,`wetland`, etc.) |
| `allowed_biomes` | array<string> | no | Keep points only in listed biome labels |
| `biome_blend_distance` | float | no | Edge blending distance for biome transitions |
| `avoid_points` | array<{x,y,z}> | no | Blocking points for overlap avoidance |
| `avoid_radius` | float | no | Blocking radius for `avoid_points` |
| `prefer_near_points` | array<{x,y,z}> | no | Attraction anchors for biased clustering |
| `prefer_radius` | float | no | Attraction radius |
| `prefer_strength` | float | no | Attraction strength 0..1 |
| `interaction_rules` | object | no | Grouped interaction settings object |
| `seed` | int | no | Deterministic sampling seed |
| `palette_id` | string | no | Curated palette identifier resolved by `PaletteManager` |
| `generate` | bool | no | Trigger PCG component generation (default true) |

**Response additions:**
- `distribution_diagnostics` (point counts after each filter, clearing/biome stats, generation time)
- `scene_score` (combined score from `SceneEvaluator`)
- `generation_time_exceeded` (true when local build exceeded `max_generation_time_ms`)

---

### `op_spline_scatter`
Update spline control points and scatter parameters on a spline-based procedural actor.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `spline_actor_label` or `target_label` | string | yes | Target spline actor |
| `control_points` | array<{x,y,z}> | no | World-space spline points |
| `closed_loop` | bool | no | Whether spline is closed |
| `parameters` | object | no | Property map applied to actor/components |
| `distribution_mode` | string | no | `blue_noise`, `poisson`, or `cluster` |
| `density` | float | no | Sampling density used when control points are omitted |
| `cluster_radius` | float | no | Cluster spread (cluster mode) |
| `min_spacing` | float | no | Minimum spacing radius |
| `height_range` | array<[min,max]> | no | Height filter |
| `slope_range` | array<[min,max]> | no | Slope filter in degrees |
| `distance_mask` | object | no | `{origin:{x,y,z}, min, max}` distance mask |
| `cluster_count` | int | no | Explicit cluster count override |
| `max_spawn_points` | int | no | Point cap for safety/performance |
| `max_cluster_count` | int | no | Cluster cap for safety/performance |
| `max_generation_time_ms` | float | no | Time budget for distribution build |
| `use_density_gradient` | bool | no | Enable radial density falloff field |
| `density_sigma` | float | no | Radial falloff sigma (Gaussian) |
| `density_noise` | float | no | Perlin blend amount for density variation |
| `density_field_resolution` | int | no | Density field grid resolution |
| `clearings` | object | no | `{density,count,radius_min,radius_max}` clearing controls |
| `clearing_density` | float | no | Scalar clearing density (alt to `clearings`) |
| `clearing_count` | int | no | Explicit clearing count |
| `clearing_radius_min` | float | no | Clearing min radius (uu) |
| `clearing_radius_max` | float | no | Clearing max radius (uu) |
| `biome_count` | int | no | Voronoi biome seed count |
| `biome_types` | array<string> | no | Biome labels |
| `allowed_biomes` | array<string> | no | Keep points only in listed biome labels |
| `biome_blend_distance` | float | no | Edge blending distance for biome transitions |
| `avoid_points` | array<{x,y,z}> | no | Blocking points for overlap avoidance |
| `avoid_radius` | float | no | Blocking radius for `avoid_points` |
| `prefer_near_points` | array<{x,y,z}> | no | Attraction anchors for biased clustering |
| `prefer_radius` | float | no | Attraction radius |
| `prefer_strength` | float | no | Attraction strength 0..1 |
| `interaction_rules` | object | no | Grouped interaction settings object |
| `seed` | int | no | Deterministic sampling seed |
| `palette_id` | string | no | Curated palette identifier |
| `generate` | bool | no | Trigger PCG generation |

---

### `op_road_layout`
Configure road actor splines/parameters (RoadBuilder plugin or spline fallback actor).

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `road_actor_label` | string | no | Existing road actor |
| `road_class_path` | string | no | Class path to spawn if actor is missing |
| `centerline_points` | array<{x,y,z}> | no | Road spline points |
| `parameters` | object | no | Road-related property map |
| `generate` | bool | no | Trigger procedural generation |

---

### `op_biome_layers`
Apply layered biome controls (groundcover/shrub/tree/rock density, path width, scale, etc.).

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `target_label` | string | yes | Biome/PCG actor target |
| `layers` | object | no | Layer parameter map |
| `parameters` | object | no | Additional property map |
| `distribution_mode` | string | no | `blue_noise`, `poisson`, or `cluster` |
| `density` | float | no | Sampling density for distribution planning |
| `cluster_radius` | float | no | Cluster spread (cluster mode) |
| `min_spacing` | float | no | Minimum spacing radius |
| `height_range` | array<[min,max]> | no | Height filter |
| `slope_range` | array<[min,max]> | no | Slope filter in degrees |
| `distance_mask` | object | no | `{origin:{x,y,z}, min, max}` distance mask |
| `cluster_count` | int | no | Explicit cluster count override |
| `max_spawn_points` | int | no | Point cap for safety/performance |
| `max_cluster_count` | int | no | Cluster cap for safety/performance |
| `max_generation_time_ms` | float | no | Time budget for distribution build |
| `use_density_gradient` | bool | no | Enable radial density falloff field |
| `density_sigma` | float | no | Radial falloff sigma (Gaussian) |
| `density_noise` | float | no | Perlin blend amount for density variation |
| `density_field_resolution` | int | no | Density field grid resolution |
| `clearings` | object | no | `{density,count,radius_min,radius_max}` clearing controls |
| `clearing_density` | float | no | Scalar clearing density (alt to `clearings`) |
| `clearing_count` | int | no | Explicit clearing count |
| `clearing_radius_min` | float | no | Clearing min radius (uu) |
| `clearing_radius_max` | float | no | Clearing max radius (uu) |
| `biome_count` | int | no | Voronoi biome seed count |
| `biome_types` | array<string> | no | Biome labels |
| `allowed_biomes` | array<string> | no | Keep points only in listed biome labels |
| `biome_blend_distance` | float | no | Edge blending distance for biome transitions |
| `avoid_points` | array<{x,y,z}> | no | Blocking points for overlap avoidance |
| `avoid_radius` | float | no | Blocking radius for `avoid_points` |
| `prefer_near_points` | array<{x,y,z}> | no | Attraction anchors for biased clustering |
| `prefer_radius` | float | no | Attraction radius |
| `prefer_strength` | float | no | Attraction strength 0..1 |
| `interaction_rules` | object | no | Grouped interaction settings object |
| `seed` | int | no | Deterministic sampling seed |
| `palette_id` | string | no | Curated palette identifier |
| `generate` | bool | no | Trigger procedural generation |

---

### `op_terrain_generate`
Generate deterministic terrain data (heightmap + ridged-noise + erosion baseline).

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `seed` | int | no | Deterministic terrain seed |
| `width` | int | no | Heightmap width |
| `height` | int | no | Heightmap height |
| `frequency` | float | no | Base noise frequency |
| `amplitude` | float | no | Base noise amplitude |
| `ridge_strength` | float | no | Ridged-noise blend strength |
| `erosion_iterations` | int | no | Thermal erosion iterations |
| `erosion_strength` | float | no | Thermal erosion blend strength |
| `sediment_strength` | float | no | Alias for erosion strength |
| `spawn_landscape` | bool | no | Attempt direct landscape spawn (stub-safe) |

---

### `op_stamp_poi`
Deterministically stamp hero POI actors onto anchor points.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `poi_class_path` or `poi_class_paths` | string/array | yes | POI actor class path(s) |
| `anchors` | array<{x,y,z,...}> | yes | Anchor transforms |
| `seed` | int | no | Deterministic class selection seed |
| `max_count` | int | no | Max anchors to process |
| `align_to_surface` | bool | no | Drop POIs to nearest surface |
| `align_to_normal` | bool | no | Rotate with surface normal |
| `label_prefix` | string | no | Spawned actor label prefix |

---

### `run_operator_pipeline`
Execute the constrained stack in order:
`terrain_generate -> road_layout -> biome_layers -> surface_scatter -> spline_scatter -> stamp_poi`.

Each stage is optional and provided via nested args objects.

**Args:**

| Field | Type | Required | Description |
|---|---|---|---|
| `terrain_generate` or `terrain` | object | no | Args for `op_terrain_generate` |
| `surface_scatter` or `surface` | object | no | Args for `op_surface_scatter` |
| `spline_scatter` or `spline` | object | no | Args for `op_spline_scatter` |
| `road_layout` or `roads` | object | no | Args for `op_road_layout` |
| `biome_layers` or `biomes` | object | no | Args for `op_biome_layers` |
| `stamp_poi` or `poi` | object | no | Args for `op_stamp_poi` |
| `seed` | int | no | Shared seed injected into stage args if missing |
| `palette_id` | string | no | Shared palette injected into stage args if missing |
| `distribution_mode` | string | no | Shared distribution mode injected when missing |
| `density` | float | no | Shared density injected when missing |
| `cluster_radius` | float | no | Shared cluster radius injected when missing |
| `min_spacing` | float | no | Shared minimum spacing injected when missing |
| `height_range` | array | no | Shared height filter injected when missing |
| `slope_range` | array | no | Shared slope filter injected when missing |
| `distance_mask` | object | no | Shared distance mask injected when missing |
| `cluster_count` | int | no | Shared cluster count injected when missing |
| `max_spawn_points` | int | no | Shared spawn-point cap injected when missing |
| `max_cluster_count` | int | no | Shared cluster cap injected when missing |
| `max_generation_time_ms` | float | no | Shared stage/pipeline generation budget |
| `use_density_gradient` | bool | no | Shared density-gradient toggle |
| `density_sigma` | float | no | Shared Gaussian sigma |
| `density_noise` | float | no | Shared density noise blend |
| `density_field_resolution` | int | no | Shared density-field resolution |
| `clearings` | object | no | Shared clearing settings object |
| `clearing_density` | float | no | Shared clearing density |
| `clearing_count` | int | no | Shared clearing count |
| `clearing_radius_min` | float | no | Shared min clearing radius |
| `clearing_radius_max` | float | no | Shared max clearing radius |
| `biome_count` | int | no | Shared Voronoi biome count |
| `biome_types` | array<string> | no | Shared biome labels |
| `allowed_biomes` | array<string> | no | Shared allowed biome set |
| `biome_blend_distance` | float | no | Shared biome edge blend distance |
| `avoid_points` | array<{x,y,z}> | no | Shared interaction blocking points |
| `avoid_radius` | float | no | Shared interaction blocking radius |
| `prefer_near_points` | array<{x,y,z}> | no | Shared interaction attraction anchors |
| `prefer_radius` | float | no | Shared interaction attraction radius |
| `prefer_strength` | float | no | Shared interaction attraction strength |
| `interaction_rules` | object | no | Shared grouped interaction settings |
| `stop_on_error` | bool | no | Cancel transaction on first stage failure |
| `max_actor_delta` | int | no | Rollback if exceeded |
| `max_memory_used_mb` | float | no | Rollback if exceeded |
| `allow_menu_level` | bool | no | Override MenuLevel guard (default false) |

**Response includes:**
- `stages[]` per-stage structured results
- `rolled_back` when budgets or stage-failure policy triggered
- actor/memory before/after metrics

**Policy note:**
- When `operator_only` is enabled (default), direct atomic placement commands (`spawn_actor`, `set_actor_transform`, `delete_actor`) are rejected.
- `run_operator_pipeline` is blocked on maps with `MenuLevel` in package name unless `allow_menu_level=true`.

