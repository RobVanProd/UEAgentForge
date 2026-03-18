# ADDENDUM A: Granular Scene Commands & Rich MCP Tool Descriptions

## Priority: EXECUTE BEFORE Phase 1 of the main game plan

**Why this matters:** The quality difference you saw between your plugin and theirs is NOT the LLM — it's tool granularity and tool descriptions. When Claude Desktop sees a tool called `spawn_actor` with a bare description, it guesses. When it sees `create_wall` with parameters for `length, height, thickness, material, has_windows, window_spacing` and a description that says "Creates a modular wall segment. Use multiple calls to form rooms. Walls snap to grid at 100-unit increments. Place walls in clockwise order to form enclosed spaces." — Claude builds rooms correctly on the first try.

The flopperam/unreal-engine-mcp project (452 stars, builds entire towns from prompts) succeeds because it has **high-level compound commands** (`create_town`, `construct_house`, `create_castle_fortress`, `create_maze`) alongside **low-level granular commands** (`set_static_mesh_properties`, `set_mesh_material_color`, `apply_material_to_actor`). The AI picks the right abstraction level for the task.

---

## SECTION 1: New C++ Commands to Add to AgentForgeLibrary

Add ALL of the following commands to `ExecuteCommandJson` dispatch. Each command below includes the exact JSON args format and the implementation approach.

### 1.1 — Actor Manipulation (Granular)

**These are the commands that let Claude build things piece by piece.**

```
Command: "duplicate_actor"
Args: {"actor_name": "<string>", "offset_x": <float>, "offset_y": <float>, "offset_z": <float>}
Description: "Duplicate an existing actor with an offset. Essential for creating rows of objects, repeating structures, or array-based layouts. Returns the new actor's name."
Implementation: Find actor by label → GEditor->GetEditorSubsystem<UEditorActorSubsystem>()->DuplicateActors({actor}) → offset the duplicate's transform
```

```
Command: "set_static_mesh"
Args: {"actor_name": "<string>", "mesh_path": "<string>"}
Description: "Change which static mesh an actor displays. Use get_available_meshes to find mesh paths. Common engine meshes: /Engine/BasicShapes/Cube, /Engine/BasicShapes/Sphere, /Engine/BasicShapes/Cylinder, /Engine/BasicShapes/Cone, /Engine/BasicShapes/Plane."
Implementation: Find actor → GetComponentByClass<UStaticMeshComponent>() → SetStaticMesh(LoadObject<UStaticMesh>(mesh_path))
```

```
Command: "set_actor_scale"
Args: {"actor_name": "<string>", "sx": <float>, "sy": <float>, "sz": <float>}
Description: "Set absolute scale of an actor. Scale 1.0 = default size. A Cube with scale (10, 0.1, 3) makes a wall 1000cm long, 10cm thick, 300cm tall. Use this to create architectural elements from basic shapes."
Implementation: Find actor → SetActorScale3D(FVector(sx, sy, sz))
```

```
Command: "set_actor_label"
Args: {"actor_name": "<string>", "new_label": "<string>"}
Description: "Rename an actor's editor label. Use descriptive names like 'NorthWall_01', 'FloorTile_MainHall', 'PointLight_Hallway' for organized scene hierarchy."
Implementation: Find actor → SetActorLabel(new_label)
```

```
Command: "set_actor_mobility"
Args: {"actor_name": "<string>", "mobility": "<string>"}
Description: "Set mobility: 'Static' for non-moving geometry (best performance, baked lighting), 'Stationary' for lights that don't move but cast dynamic shadows, 'Movable' for anything that moves at runtime."
Implementation: Find actor → GetRootComponent()->SetMobility(EComponentMobility from string)
```

```
Command: "set_actor_visibility"
Args: {"actor_name": "<string>", "visible": <bool>}
Description: "Show or hide an actor. Hidden actors still exist and can be unhidden later. Useful for creating variants or toggling detail levels."
Implementation: Find actor → SetActorHiddenInGame(!) → GetRootComponent()->SetVisibility(visible)
```

```
Command: "group_actors"
Args: {"actor_names": ["<string>", ...], "group_name": "<string>"}
Description: "Attach multiple actors to a new empty parent actor for organization. Moving the parent moves all children. Essential for treating a collection of actors (walls + floor + ceiling) as a single 'room'."
Implementation: Spawn empty actor as parent → for each child: child->AttachToActor(parent, FAttachmentTransformRules::KeepWorldTransform)
```

```
Command: "get_actor_property"
Args: {"actor_name": "<string>", "property_name": "<string>"}
Description: "Read any UPROPERTY value on an actor. Common properties: 'StaticMeshComponent.StaticMesh', 'LightComponent.Intensity', 'LightComponent.LightColor'. Returns the value as a string."
Implementation: Find actor → use FProperty reflection to read value
```

```
Command: "set_actor_property"
Args: {"actor_name": "<string>", "property_name": "<string>", "value": "<string>"}
Description: "Set any UPROPERTY value on an actor. Accepts dot-notation for component properties. Examples: set LightComponent.Intensity to '5000', set LightComponent.LightColor to '(R=1.0,G=0.5,B=0.2,A=1.0)'."
Implementation: Find actor → FProperty reflection → SetValue_InContainer
```

### 1.2 — Content Browser / Asset Discovery

**These are CRITICAL. Without these, Claude doesn't know what assets are available in the project and has to guess mesh/material paths.**

```
Command: "get_available_meshes"
Args: {"search_filter": "<string>", "path_filter": "<string>", "max_results": <int>}
Description: "Search the Content Browser for static meshes. Returns asset paths that can be used with set_static_mesh or spawn_actor. Search examples: 'wall' finds SM_Wall_01, SM_Wall_Corner, etc. 'rock' finds all rock meshes. Leave search_filter empty and set path_filter to '/Game/Environment/' to browse a folder. Always call this before placing meshes to use project assets instead of basic shapes."
Implementation: FAssetRegistryModule → GetAssets(FARFilter with ClassPath = StaticMesh, PackagePath optional, text filter on asset name)
```

```
Command: "get_available_materials"
Args: {"search_filter": "<string>", "path_filter": "<string>", "max_results": <int>}
Description: "Search the Content Browser for materials and material instances. Returns asset paths for use with apply_material_to_actor or create_material_instance. Search 'wood' to find all wood materials, 'metal' for metals, etc."
Implementation: Same as meshes but ClassPath = Material | MaterialInstance
```

```
Command: "get_available_blueprints"
Args: {"search_filter": "<string>", "parent_class": "<string>", "max_results": <int>}
Description: "Search for Blueprint assets. Optionally filter by parent class (e.g., 'Actor', 'Character', 'AIController'). Returns asset paths that can be spawned."
Implementation: FAssetRegistryModule → filter by Blueprint class
```

```
Command: "get_available_textures"
Args: {"search_filter": "<string>", "max_results": <int>}
Description: "Search for texture assets. Useful for finding textures to apply to material instances."
Implementation: FAssetRegistryModule → filter by Texture2D
```

```
Command: "get_available_sounds"
Args: {"search_filter": "<string>", "max_results": <int>}
Description: "Search for sound assets (SoundWave, SoundCue). Returns paths for use with audio components."
Implementation: FAssetRegistryModule → filter by SoundWave | SoundCue
```

```
Command: "get_asset_details"
Args: {"asset_path": "<string>"}
Description: "Get detailed info about any asset: type, size, references, thumbnail path. For meshes: poly count, bounds, LOD count. For materials: parameter list. Helps the AI make informed decisions about which assets to use."
Implementation: Load asset metadata, type-specific info extraction
```

### 1.3 — Material Application (the #1 thing that makes scenes look good vs. gray boxes)

```
Command: "apply_material_to_actor"
Args: {"actor_name": "<string>", "material_path": "<string>", "slot_index": <int>}
Description: "Apply a material to an actor's mesh. slot_index 0 is the primary material. Use get_available_materials to find material paths. If you don't know the path, try common engine materials: '/Engine/EngineMaterials/DefaultMaterial', '/Engine/MapTemplates/Materials/BasicAsset03'."
Implementation: Find actor → GetStaticMeshComponent() → SetMaterial(slot_index, LoadObject<UMaterialInterface>(path))
```

```
Command: "set_mesh_material_color"
Args: {"actor_name": "<string>", "r": <float>, "g": <float>, "b": <float>, "a": <float>, "slot_index": <int>}
Description: "Create a dynamic material instance on the actor and set its base color. RGB values 0.0-1.0. Quick way to colorize objects without creating persistent material assets. Good for prototyping. Example: (0.8, 0.2, 0.1) = brick red, (0.3, 0.3, 0.3) = dark concrete, (0.9, 0.85, 0.6) = sandstone."
Implementation: Create UMaterialInstanceDynamic from actor's current material → SetVectorParameterValue("BaseColor", FLinearColor(r,g,b,a)) → SetMaterial
```

```
Command: "set_material_scalar_param"
Args: {"actor_name": "<string>", "param_name": "<string>", "value": <float>}
Description: "Set a scalar parameter on an actor's dynamic material. Common parameters: 'Roughness' (0=mirror, 1=matte), 'Metallic' (0=nonmetal, 1=full metal), 'Opacity' (for translucent materials), 'EmissiveStrength'."
Implementation: GetOrCreateDynamicMaterialInstance → SetScalarParameterValue
```

### 1.4 — Light Placement (essential for scene quality)

```
Command: "spawn_point_light"
Args: {"x": <float>, "y": <float>, "z": <float>, "intensity": <float>, "color_r": <float>, "color_g": <float>, "color_b": <float>, "attenuation_radius": <float>, "label": "<string>"}
Description: "Spawn a point light. Intensity in lumens — 800 = standard room bulb, 5000 = bright spotlight, 50000 = outdoor. Color: warm interior (1.0, 0.85, 0.7), cool moonlight (0.7, 0.8, 1.0), eerie green (0.3, 1.0, 0.4). Attenuation radius controls how far the light reaches in cm."
Implementation: SpawnActor<APointLight> → set properties on UPointLightComponent
```

```
Command: "spawn_spot_light"
Args: {"x": <float>, "y": <float>, "z": <float>, "rx": <float>, "ry": <float>, "rz": <float>, "intensity": <float>, "color_r": <float>, "color_g": <float>, "color_b": <float>, "inner_cone_angle": <float>, "outer_cone_angle": <float>, "label": "<string>"}
Description: "Spawn a spotlight. Rotation determines aim direction. Inner cone = full brightness zone, outer cone = falloff edge. For a ceiling downlight: rotation (0,-90,0), inner 15, outer 30. For a dramatic wall wash: rotation (0,-45,0), inner 25, outer 60."
Implementation: SpawnActor<ASpotLight> → set properties
```

```
Command: "spawn_rect_light"
Args: {"x": <float>, "y": <float>, "z": <float>, "rx": <float>, "ry": <float>, "rz": <float>, "intensity": <float>, "width": <float>, "height": <float>, "color_r": <float>, "color_g": <float>, "color_b": <float>, "label": "<string>"}
Description: "Spawn a rectangular area light. Produces soft, natural-looking illumination. Great for simulating windows (set width/height to match window size), fluorescent panels, or TV screens. Width and height in cm."
Implementation: SpawnActor<ARectLight> → set properties
```

```
Command: "spawn_directional_light"
Args: {"rx": <float>, "ry": <float>, "rz": <float>, "intensity": <float>, "color_r": <float>, "color_g": <float>, "color_b": <float>, "label": "<string>"}
Description: "Spawn a directional light (sun/moon). Only rotation matters, not position. For a typical sun: rotation (-45, 30, 0). For dramatic sunset: rotation (-15, -60, 0). Only use one per level unless doing multi-sun sci-fi."
Implementation: SpawnActor<ADirectionalLight> → set properties
```

### 1.5 — High-Level Compound Commands (Architectural Primitives)

**These are what make the flopperam plugin build entire towns. Each one encapsulates 10-50 low-level operations.**

```
Command: "create_wall"
Args: {"start_x": <float>, "start_y": <float>, "end_x": <float>, "end_y": <float>, "height": <float>, "thickness": <float>, "material_path": "<string>", "has_windows": <bool>, "window_spacing": <float>, "window_height": <float>, "label": "<string>"}
Description: "Create a wall segment between two points. Automatically calculates rotation and length. If has_windows is true, cuts window holes at regular intervals. Returns the wall actor name. Chain multiple create_wall calls to form rooms — place walls in clockwise order."
Implementation: Calculate length from start/end → spawn scaled cube → if windows: spawn window cutout actors or use boolean subtraction → apply material
```

```
Command: "create_floor"
Args: {"center_x": <float>, "center_y": <float>, "z": <float>, "width": <float>, "length": <float>, "material_path": "<string>", "label": "<string>"}
Description: "Create a floor/ceiling plane. Width along X axis, Length along Y axis, both in cm. A standard room floor: width=500, length=600. Use at z=0 for ground floor, z=300 for a ceiling or second story floor."
Implementation: Spawn plane/cube mesh scaled to dimensions → apply material
```

```
Command: "create_room"
Args: {"center_x": <float>, "center_y": <float>, "z": <float>, "width": <float>, "length": <float>, "height": <float>, "wall_thickness": <float>, "floor_material": "<string>", "wall_material": "<string>", "ceiling_material": "<string>", "door_wall": "<string>", "window_walls": ["<string>"], "label": "<string>"}
Description: "Create a complete room with 4 walls, floor, and ceiling. door_wall: 'north'|'south'|'east'|'west' — which wall gets a door opening. window_walls: list of wall directions that get windows. All actors are grouped under a parent named <label>. A standard room: width=500, length=600, height=300."
Implementation: Create floor → create 4 walls (with door/window cutouts as specified) → create ceiling → group under parent actor
```

```
Command: "create_corridor"
Args: {"start_x": <float>, "start_y": <float>, "end_x": <float>, "end_y": <float>, "width": <float>, "height": <float>, "wall_material": "<string>", "floor_material": "<string>", "has_ceiling": <bool>, "label": "<string>"}
Description: "Create a corridor/hallway between two points. Automatically generates floor, two side walls, and optional ceiling. Width is the interior walkable space. Connect rooms by aligning corridor endpoints with room door openings."
Implementation: Calculate direction vector → create floor plane → two walls → optional ceiling → group
```

```
Command: "create_staircase"
Args: {"base_x": <float>, "base_y": <float>, "base_z": <float>, "direction": "<string>", "step_count": <int>, "step_width": <float>, "step_depth": <float>, "step_height": <float>, "material_path": "<string>", "label": "<string>"}
Description: "Create a staircase. Direction: 'north'|'south'|'east'|'west'. Standard step dimensions: width=150, depth=30, height=18. For a full story (300cm): 17 steps at height 18cm each. Each step is a separate actor grouped under the label."
Implementation: Loop step_count times → spawn cube scaled to step dimensions → offset each step by depth and height → group all
```

```
Command: "create_pillar"
Args: {"x": <float>, "y": <float>, "z": <float>, "radius": <float>, "height": <float>, "sides": <int>, "material_path": "<string>", "label": "<string>"}
Description: "Create a pillar/column. Sides: 4=square pillar, 8=octagonal, 32+=round column. Standard decorative column: radius=20, height=300, sides=16. Structural pillar: radius=30, height=300, sides=4."
Implementation: Spawn cylinder mesh → scale to radius/height → apply material
```

```
Command: "scatter_props"
Args: {"mesh_path": "<string>", "center_x": <float>, "center_y": <float>, "radius": <float>, "count": <int>, "min_scale": <float>, "max_scale": <float>, "random_rotation": <bool>, "snap_to_surface": <bool>, "material_path": "<string>", "label_prefix": "<string>"}
Description: "Scatter multiple instances of a prop within a radius. Randomizes position within the circle, scale between min/max, and optionally rotation. snap_to_surface raycasts each prop down to the ground. Great for: rocks, debris, grass clumps, furniture, crates."
Implementation: Loop count times → random position in circle → optional raycast for Z → random scale/rotation → spawn → group all under parent
```

### 1.6 — Viewport & Camera Control

```
Command: "focus_viewport_on_actor"
Args: {"actor_name": "<string>"}
Description: "Move the editor viewport camera to focus on a specific actor. Essential for inspecting your work after spawning objects."
Implementation: GEditor->MoveViewportCamerasToActor(actor, false)
```

```
Command: "set_viewport_camera"
Args: {"x": <float>, "y": <float>, "z": <float>, "pitch": <float>, "yaw": <float>, "roll": <float>}
Description: "Set the editor viewport camera to an exact position and rotation. Pitch: -90=looking straight down, 0=horizontal, 90=looking up. Yaw: 0=north, 90=east, 180=south, 270=west."
Implementation: FLevelEditorViewportClient → SetViewLocation/SetViewRotation
```

```
Command: "get_viewport_info"
Args: {}
Description: "Get the current viewport camera position, rotation, FOV, and viewport dimensions. Useful for understanding what the user is looking at before making modifications."
Implementation: Read from FLevelEditorViewportClient
```

### 1.7 — Environment & Atmosphere

```
Command: "set_fog"
Args: {"density": <float>, "height_falloff": <float>, "start_distance": <float>, "color_r": <float>, "color_g": <float>, "color_b": <float>}
Description: "Configure exponential height fog. Density: 0.02=light haze, 0.05=moderate fog, 0.2=thick fog. Height falloff controls how quickly fog thins with altitude. Start distance pushes fog away from camera. Creates atmosphere and depth."
Implementation: Find or spawn AExponentialHeightFog → set component properties
```

```
Command: "set_sky_atmosphere"
Args: {"preset": "<string>"}
Description: "Apply a sky atmosphere preset. Options: 'default_day', 'golden_hour', 'overcast', 'night_clear', 'night_cloudy', 'stormy', 'alien_red', 'alien_green'. Creates or modifies SkyAtmosphere + SkyLight + DirectionalLight."
Implementation: Find/spawn atmosphere actors → apply preset values for sun angle, rayleigh scattering, mie scattering, sky light intensity
```

```
Command: "set_post_process"
Args: {"bloom_intensity": <float>, "exposure_compensation": <float>, "ambient_occlusion_intensity": <float>, "vignette_intensity": <float>, "saturation": <float>, "contrast": <float>, "color_temp": <float>}
Description: "Configure the global post-process volume. bloom_intensity: 0=none, 0.5=subtle, 1.5=dreamy. exposure_compensation: negative=darker, positive=brighter. ambient_occlusion: 0-1, adds depth to corners. color_temp: 6500=neutral, lower=warm, higher=cool."
Implementation: Find or create PostProcessVolume with Infinite Extent → set settings struct values
```

---

## SECTION 2: Enhanced MCP Tool Descriptions

**This is the second half of the equation. Every MCP tool needs a description that teaches the AI how to use it effectively.**

### Key Principles for MCP Tool Descriptions:

1. **Say what it does AND when to use it** — "Create a wall segment" is bare. "Create a wall segment between two points. Chain multiple calls in clockwise order to form rooms." teaches usage.
2. **Include typical parameter values** — "intensity: 5000" means nothing. "intensity in lumens: 800=room bulb, 5000=bright spot, 50000=outdoor sun" gives Claude calibration.
3. **Mention common pitfalls** — "Walls at Z=0 sit on the ground plane. Add half the wall height to Z to make the wall bottom rest on the floor."
4. **Cross-reference related tools** — "Use get_available_meshes first to find project-specific meshes before falling back to basic shapes."
5. **Show composition patterns** — "To build a house: create_floor → create_wall ×4 → create_floor (ceiling) → group_actors. Place door opening by leaving a gap between wall segments."

### Update the MCP Server Tool Registration

In `agentforge_mcp_server.py`, update ALL existing tool descriptions to be this detailed. Here are the rewrites for the most important existing tools:

```python
# REPLACE the existing spawn_actor tool definition with:
Tool(
    name="spawn_actor",
    description="""Spawn an actor by class path at a world position. 

COMMON CLASSES:
- /Script/Engine.StaticMeshActor — generic mesh actor (default cube). Change mesh with set_static_mesh after spawning.
- /Script/Engine.PointLight — point light source
- /Script/Engine.SpotLight — directional spotlight  
- /Script/Engine.DirectionalLight — sun/moon light
- /Script/Engine.CameraActor — viewport camera
- /Script/Engine.PlayerStart — player spawn point
- /Script/Engine.TriggerBox — invisible trigger volume
- /Script/Engine.ExponentialHeightFog — atmospheric fog
- /Script/Engine.SkyLight — ambient sky illumination
- /Script/NavigationSystem.NavMeshBoundsVolume — AI navigation area

COORDINATE SYSTEM: X=forward, Y=right, Z=up. 1 unit = 1 cm. 
A standard door is 200cm tall, 100cm wide.
A room is typically 400-600cm per side, 250-300cm tall.

TIP: After spawning a StaticMeshActor, use set_static_mesh to change from default cube to the desired mesh, set_actor_scale to size it, and apply_material_to_actor or set_mesh_material_color for appearance.""",
    inputSchema={...}  # keep existing schema
)

# REPLACE get_all_level_actors:
Tool(
    name="get_all_level_actors",
    description="""List ALL actors in the current level with their names, classes, positions, rotations, and scales.

ALWAYS call this before making changes to understand the current scene state.
Returns a JSON array. Each entry has: label, class, location{x,y,z}, rotation{pitch,yaw,roll}, scale{x,y,z}.

TIP: Use the actor labels to target specific actors in subsequent commands like set_actor_transform, delete_actor, apply_material_to_actor.""",
    inputSchema={"type": "object", "properties": {}}
)

# REPLACE take_screenshot:
Tool(
    name="take_screenshot",
    description="""Capture the current editor viewport to a PNG file. Returns the file path.

Use this to visually verify your work after making changes. The screenshot shows exactly what the editor camera sees.

WORKFLOW: Make changes → take_screenshot → analyze the image → iterate if needed.
TIP: Use set_viewport_camera or focus_viewport_on_actor first to frame the shot.""",
    inputSchema={"type": "object", "properties": {}}
)
```

### Register ALL New Commands as MCP Tools

Add to the `list_tools()` return array in `agentforge_mcp_server.py`. I won't repeat the full schema for each (the agent should generate these from the command definitions above), but here's the registration pattern for the compound commands:

```python
# --- Compound Building Commands ---
Tool(name="create_wall", description="Create a wall segment between two XY points. Height and thickness in cm. Automatically calculates length and rotation. Chain multiple calls clockwise to form rooms. Set has_windows=true to add window openings at regular intervals. Standard wall: height=300, thickness=15.", inputSchema={...}),

Tool(name="create_floor", description="Create a horizontal floor or ceiling plane. Width along X, Length along Y, both in cm. Place at z=0 for ground level. For a ceiling, place at z=<room_height>. Standard room: width=500, length=600.", inputSchema={...}),

Tool(name="create_room", description="Create a complete enclosed room with 4 walls, floor, and ceiling in a single call. Specify which wall gets a door opening and which walls get windows. All pieces are grouped under a parent actor. Standard room: width=500, length=600, height=300. To connect rooms, use create_corridor between their door openings.", inputSchema={...}),

Tool(name="create_corridor", description="Create a hallway connecting two points. Generates floor, two side walls, and optional ceiling. Align endpoints with room door openings to connect rooms. Standard corridor: width=200, height=280.", inputSchema={...}),

# --- Asset Discovery ---
Tool(name="get_available_meshes", description="IMPORTANT: Call this before placing objects to discover what meshes exist in the project. Search by keyword (e.g., 'wall', 'rock', 'tree', 'furniture'). Returns asset paths you can use with set_static_mesh. If no project meshes match, fall back to /Engine/BasicShapes/ primitives.", inputSchema={...}),

Tool(name="get_available_materials", description="IMPORTANT: Call this before applying materials to discover what materials exist in the project. Search by keyword (e.g., 'wood', 'concrete', 'metal', 'brick'). Returns paths for apply_material_to_actor. If no project materials match, use set_mesh_material_color for solid colors.", inputSchema={...}),
```

---

## SECTION 3: MCP Server System Prompt / Instructions Resource

Add an MCP **resource** that Claude Desktop can read on connection, teaching it architectural patterns. Create this file and register it as an MCP resource:

### `PythonClient/mcp_server/knowledge_base/building_guide.md`

```markdown
# UEAgentForge Building Guide for AI Agents

## Coordinate System
- X = forward (north), Y = right (east), Z = up
- 1 unit = 1 centimeter  
- Ground plane is Z = 0

## Standard Dimensions (cm)
- Door: 100w × 200h
- Window: 120w × 100h, sill at 90cm above floor
- Wall thickness: 15-20
- Room height: 250-300 (residential), 350-500 (commercial/horror)
- Corridor width: 150-250
- Stair step: 30 depth × 18 height
- Standard room: 400-600 per side
- Large hall: 800-1500 per side

## Building a Room (step by step)
1. create_floor at desired position
2. create_wall for each side (clockwise: north, east, south, west)
3. Leave gaps in walls for doors (skip ~100cm segment)
4. create_floor again at z=room_height for ceiling
5. group_actors to combine all pieces
6. Apply materials to all surfaces

## Building a Multi-Room Layout
1. Plan room positions on a grid (rooms separated by wall_thickness)
2. Create each room with create_room (specify door_wall direction)  
3. Connect adjacent rooms with create_corridor
4. Add a PlayerStart actor in the first room
5. Add lighting to each room (1-2 point lights per room)

## Lighting Best Practices
- Every room needs at least one light source
- Place point lights at 70% of room height (not dead center — offset for interest)
- Use warm colors (1.0, 0.85, 0.7) for inhabited spaces
- Use cool colors (0.7, 0.8, 1.0) for exterior/moonlight
- Use rect_lights near windows to simulate daylight
- Add a subtle ambient light (low intensity point light) to prevent pure black shadows

## Material Application Order
1. First call get_available_materials to check project assets
2. If project has materials, use apply_material_to_actor
3. If not, use set_mesh_material_color for solid colors
4. For variety, use slightly different color values on repeated elements

## Horror Scene Tips
- Lower light intensity by 60-70%
- Use color temperature < 4000K (warm, amber) or > 8000K (cold, blue)  
- Add fog with density 0.03-0.08
- Leave some areas completely dark
- Place lights at low positions (floor level) for upward shadows
- Use red accent lights sparingly for alarm/danger areas

## Quality Checklist
- [ ] Every room has at least one light
- [ ] All surfaces have materials (no default grey)
- [ ] Player can navigate through doors (100cm+ wide, 200cm+ tall)
- [ ] No floating objects (all props at correct Z height)
- [ ] Objects don't overlap/intersect
- [ ] Scene has a PlayerStart
```

### Register as MCP Resource

In `agentforge_mcp_server.py`, add:

```python
@app.list_resources()
async def list_resources():
    return [
        Resource(
            uri="agentforge://building-guide",
            name="UEAgentForge Building Guide",
            description="Architectural standards, dimensions, building patterns, and best practices for AI-driven level building in Unreal Engine.",
            mimeType="text/markdown"
        )
    ]

@app.read_resource()
async def read_resource(uri: str):
    if uri == "agentforge://building-guide":
        guide_path = os.path.join(os.path.dirname(__file__), "knowledge_base", "building_guide.md")
        with open(guide_path) as f:
            return f.read()
    raise ValueError(f"Unknown resource: {uri}")
```

---

## SECTION 4: File Checklist (Addendum Files)

### New C++ Files
1. No new files needed — all commands go into existing `AgentForgeLibrary.h/.cpp` as new cases in `ExecuteCommandJson`

### Modified C++ Files  
2. `Public/AgentForgeLibrary.h` — Add UFUNCTION declarations for all ~25 new commands
3. `Private/AgentForgeLibrary.cpp` — Add dispatch cases + implementations for all ~25 new commands
4. `UEAgentForge.Build.cs` — Ensure `"NavigationSystem"`, `"LevelEditor"`, `"UnrealEd"` are in dependencies (needed for viewport control, actor subsystems)

### New Python/Content Files
5. `PythonClient/mcp_server/knowledge_base/building_guide.md` — The AI instruction document
6. Update `PythonClient/mcp_server/agentforge_mcp_server.py` — Register all ~25 new tools + resource endpoint
7. Update `PythonClient/ueagentforge_client.py` — Add Python methods for all new commands

### New Example Scripts
8. `PythonClient/examples/build_simple_house.py` — Step-by-step house construction demo
9. `PythonClient/examples/build_horror_corridor.py` — Horror corridor with lighting

---

## SECTION 5: Implementation Priority Within This Addendum

```
Priority 1 (DO FIRST — biggest impact on level quality):
  - get_available_meshes          (asset discovery)
  - get_available_materials       (asset discovery)
  - apply_material_to_actor       (visual quality)
  - set_mesh_material_color       (visual quality)
  - set_static_mesh               (mesh swapping)
  - set_actor_scale               (sizing)
  - spawn_point_light             (lighting)
  - spawn_spot_light              (lighting)
  - building_guide.md resource    (AI instruction)

Priority 2 (compound commands — big productivity gain):
  - create_wall
  - create_floor
  - create_room
  - create_corridor
  - scatter_props

Priority 3 (quality of life):
  - duplicate_actor
  - group_actors
  - set_actor_label
  - focus_viewport_on_actor
  - set_viewport_camera
  - get_viewport_info

Priority 4 (atmosphere):
  - spawn_rect_light
  - spawn_directional_light
  - set_fog
  - set_post_process
  - set_sky_atmosphere
  - create_staircase
  - create_pillar

Priority 5 (advanced):
  - set_actor_property
  - get_actor_property
  - set_actor_mobility
  - set_actor_visibility
  - get_available_blueprints
  - get_available_textures
  - get_available_sounds
  - get_asset_details
  - set_material_scalar_param
```

---

## SECTION 6: Execution Order Relative to Main Game Plan

```
OVERNIGHT SEQUENCE:
1. This Addendum Priority 1    (~2 hrs)  — asset discovery + materials + lights
2. This Addendum Priority 2    (~2 hrs)  — compound building commands
3. Main Plan Phase 3 (MCP)     (~1 hr)   — MCP server with ALL tools registered
4. This Addendum Priority 3    (~1 hr)   — quality of life commands
5. Main Plan Phase 1 (LLM)     (~3 hrs)  — multi-provider subsystem
6. This Addendum Priority 4    (~1 hr)   — atmosphere
7. Main Plan Phase 4 (Vision)  (~1 hr)   — vision-in-the-loop
8. Main Plan Phase 2 (Schema)  (~30 min) — structured outputs
9. This Addendum Priority 5    (~1 hr)   — advanced commands
10. Main Plan Phase 5-6         (~1 hr)   — python client + docs

Total: ~14 hrs (aggressive but doable for an overnight agent)
```
