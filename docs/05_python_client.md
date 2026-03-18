# Python Client

The Python client (`PythonClient/ueagentforge_client.py`) provides a thin, ergonomic wrapper over the Remote Control API HTTP interface. It handles JSON serialization, connection errors, and response parsing.

## Installation

```bash
pip install requests
```

No other dependencies. Python 3.9+ required.

## Quick start

```python
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()          # verify=True by default
print(client.ping())                 # {'pong': 'UEAgentForge v0.1.0', ...}
print(client.get_forge_status())     # constitution status, version, etc.
```

## Constructor options

```python
client = AgentForgeClient(
    host    = "127.0.0.1",  # editor host
    port    = 30010,         # Remote Control API port
    timeout = 30.0,          # request timeout in seconds
    verify  = True,          # run constitution check before mutating commands
    verbose = False,         # enable DEBUG logging
)
```

| Parameter | Type | Default | Description |
|---|---|---|---|
| `host` | str | `"127.0.0.1"` | Unreal Editor host |
| `port` | int | `30010` | Remote Control API port |
| `timeout` | float | `30.0` | HTTP request timeout (seconds) |
| `verify` | bool | `True` | Constitution-check mutating commands before sending |
| `verbose` | bool | `False` | Log all requests/responses to DEBUG |

## Return types

Most methods return either:
- A `ForgeResult` object (for mutating commands)
- A raw `dict` (for observation commands)

### `ForgeResult`

```python
result = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=100)

result.ok      # bool — True if no error field
result.error   # str or None — error message if any
result.raw     # dict — full parsed JSON response
```

### `VerificationReport`

```python
report = client.run_verification(phase_mask=15)

report.all_passed   # bool
report.phases_run   # int
report.details      # list of phase dicts

print(report.summary())
# Verification: PASSED (4 phases)
#   ✓ [PreFlight] Pre-state captured: 47 actors. (2.1ms)
#   ✓ [Snapshot+Rollback] Rollback verified OK. (45.3ms)
#   ✓ [PostVerify] Actor delta: expected 0, actual 0. (1.8ms)
#   ✓ [BuildCheck] 0 blueprints checked. All clean. (120.0ms)
```

## Transaction context manager

The `transaction()` context manager wraps operations in a `begin_transaction` / `end_transaction` pair. On exception, it automatically calls `undo_transaction`.

```python
with client.transaction("Spawn Platform"):
    r1 = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=0)
    r2 = client.spawn_actor("/Script/Engine.StaticMeshActor", x=200, y=0, z=0)
    if not r1.ok or not r2.ok:
        raise RuntimeError("Spawn failed")
# Both actors land atomically, or both are undone on exception
```

## Rapid scene-dressing example

```python
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()

meshes = client.get_available_meshes(search_filter="wall", path_filter="/Game/Environment")
materials = client.get_available_materials(search_filter="concrete", path_filter="/Game/Materials")

wall = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=0)
wall_name = wall.raw["spawned_name"]

client.set_static_mesh(wall_name, meshes["assets"][0]["asset_path"])
client.set_actor_scale(wall_name, 4.0, 0.25, 3.0)

if materials["assets"]:
    client.apply_material_to_actor(wall_name, materials["assets"][0]["asset_path"])
else:
    client.set_mesh_material_color(wall_name, 0.18, 0.20, 0.22)

client.spawn_point_light(180, 0, 220, intensity=3200, label="WallFill")
```

## All available methods

### Forge meta
```python
client.ping()                              # dict
client.get_forge_status()                  # dict
client.run_verification(phase_mask=15)     # VerificationReport
client.enforce_constitution(action_desc)   # dict {"allowed": bool, "violations": [...]}
```

### Observation
```python
client.get_all_level_actors()              # list[dict]
client.get_actor_components(label)         # list[dict]
client.get_current_level()                 # dict
client.assert_current_level(expected)      # dict
client.get_actor_bounds(label)             # dict
client.get_available_meshes(search_filter="", path_filter="", max_results=50)      # dict
client.get_available_materials(search_filter="", path_filter="", max_results=50)   # dict
```

### Viewport & capture
```python
client.set_viewport_camera(x=0, y=-600, z=300, pitch=-15, yaw=90, roll=0)  # ForgeResult
client.redraw_viewports()                # ForgeResult
client.take_screenshot(filename)         # ForgeResult
client.take_focused_screenshot(filename, x=None, y=None, z=None, pitch=None, yaw=None, roll=None)  # ForgeResult
```

### Actor control
```python
client.spawn_actor(class_path, x, y, z, pitch, yaw, roll)     # ForgeResult
client.spawn_point_light(x, y, z, intensity=5000.0, color_r=1.0, color_g=1.0, color_b=1.0, attenuation_radius=1200.0, label="")  # ForgeResult
client.spawn_spot_light(x, y, z, rx=0.0, ry=0.0, rz=0.0, intensity=5000.0, color_r=1.0, color_g=1.0, color_b=1.0, inner_cone_angle=15.0, outer_cone_angle=30.0, label="")  # ForgeResult
client.set_actor_transform(object_path, x, y, z, pitch, yaw, roll)  # ForgeResult
client.set_static_mesh(actor_name, mesh_path)  # ForgeResult
client.set_actor_scale(actor_name, sx, sy, sz)  # ForgeResult
client.delete_actor(label)                 # ForgeResult
client.save_current_level()                # ForgeResult
```

### Spatial queries
```python
client.cast_ray(start, end, trace_complex) # dict
client.query_navmesh(x, y, z, extent_x, extent_y, extent_z)  # dict
```

### Blueprint manipulation
```python
client.create_blueprint(name, parent_class, output_path)  # ForgeResult
client.compile_blueprint(blueprint_path)                  # ForgeResult
client.set_blueprint_cdo_property(bp_path, prop, type, value)  # ForgeResult
client.edit_blueprint_node(bp_path, node_type, node_title, pins)  # ForgeResult
```

### Material & content
```python
client.create_material_instance(parent, name, output_path)  # ForgeResult
client.set_material_params(instance_path, scalar_params, vector_params)  # ForgeResult
client.apply_material_to_actor(actor_name, material_path, slot_index=0)  # ForgeResult
client.set_mesh_material_color(actor_name, r, g, b, a=1.0, slot_index=0)  # ForgeResult
client.set_material_scalar_param(actor_name, param_name, value, slot_index=0)  # ForgeResult
client.rename_asset(asset_path, new_name)   # ForgeResult
client.move_asset(asset_path, dest_path)    # ForgeResult
client.delete_asset(asset_path)             # ForgeResult
```

### Transaction safety
```python
client.begin_transaction(label)             # ForgeResult
client.end_transaction()                    # ForgeResult
client.undo_transaction()                   # ForgeResult
client.create_snapshot(snapshot_name)       # ForgeResult
```

### Python scripting
```python
client.execute_python(script)               # ForgeResult
```

### Performance
```python
client.get_perf_stats()                     # dict
```

### Scene setup
```python
client.setup_test_level(floor_size)         # ForgeResult
```

### Generic
```python
client.execute(cmd, args)   # ForgeResult — send any command
client._send(cmd, args)     # dict — raw HTTP call, no ForgeResult wrapping
```

## Connecting from a remote machine

If the Unreal Editor runs on a different machine (or VM), change the host:

```python
client = AgentForgeClient(host="192.168.1.100", port=30010)
```

Make sure the Remote Control API is bound to `0.0.0.0` (not just localhost) in your `DefaultEngine.ini`:

```ini
[/Script/RemoteControl.RemoteControlSettings]
RemoteControlHttpServerPort=30010
bRestrictServerToLocalHost=False
```

## Error handling

```python
result = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=0)
if not result.ok:
    print(f"Failed: {result.error}")
    # Optionally undo
    client.undo_transaction()
```

Connection errors raise `RuntimeError` with a clear message:
```
RuntimeError: Cannot connect to Unreal Editor at http://127.0.0.1:30010/...
Ensure the editor is running with Remote Control API enabled.
```

## Using the low-level `execute()` method

For commands not yet wrapped as named methods:

```python
result = client.execute("get_perf_stats")
print(result.raw)

result = client.execute("cast_ray", {
    "start": {"x": 0, "y": 0, "z": 500},
    "end":   {"x": 0, "y": 0, "z": -500},
})
print(result.raw.get("hit_actor"))
```

## Running the examples

```bash
cd PythonClient/examples

# Basic spawn test
python 01_basic_spawn.py

# Blueprint creation
python 02_blueprint_create.py

# Full verified workflow
python 03_verified_workflow.py
```

All examples connect to `127.0.0.1:30010` by default. Make sure the Unreal Editor is open with a level loaded.

Screenshot helper methods stage captures to `C:/HGShots/`. `take_focused_screenshot()` forces a viewport redraw before capture so unattended editor sessions still produce a rendered frame.
