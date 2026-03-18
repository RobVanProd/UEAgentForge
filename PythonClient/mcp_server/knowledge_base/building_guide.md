# UEAgentForge Building Guide for AI Agents

## Coordinate System

- X = forward
- Y = right
- Z = up
- 1 unit = 1 centimeter
- Ground plane is `Z = 0`

## Standard Dimensions (cm)

- Door: `100w x 200h`
- Window: `120w x 100h`, sill at `90`
- Wall thickness: `15-20`
- Room height: `250-300` residential, `350-500` commercial or horror
- Corridor width: `150-250`
- Stair step: `30` depth x `18` height
- Standard room: `400-600` per side
- Large hall: `800-1500` per side

## Building A Room

1. Create a floor at the desired position.
2. Create four walls in clockwise order.
3. Leave a gap of about `100` cm for a door.
4. Create a ceiling at `z = room_height`.
5. Group the pieces under one parent actor.
6. Apply materials to all visible surfaces.
7. Add at least one light.

## Reliable Mesh Selection Order

1. Call `get_available_meshes` first with both `search_filter` and `path_filter` when possible.
2. If a project mesh is found, use `set_static_mesh` with the returned `asset_path`.
3. If no project mesh is found, fall back to engine primitives such as:
   - `/Engine/BasicShapes/Cube.Cube`
   - `/Engine/BasicShapes/Plane.Plane`
   - `/Engine/BasicShapes/Cylinder.Cylinder`
   - `/Engine/BasicShapes/Cone.Cone`
4. After changing the mesh, immediately call `set_actor_scale` so the primitive or mesh lands at real-world size.

## Reliable Wall/Floor Command Sequence

1. `spawn_actor("/Script/Engine.StaticMeshActor", ...)`
2. `set_static_mesh(actor_name, mesh_path)`
3. `set_actor_scale(actor_name, sx, sy, sz)`
4. `apply_material_to_actor(...)` if a project material exists
5. otherwise `set_mesh_material_color(...)`
6. `get_actor_bounds(actor_name)` to confirm dimensions

This sequence is more reliable than trying to guess the correct mesh, scale, and material in one step.

## Building A Multi-Room Layout

1. Plan room centers on a grid.
2. Create each room with doors facing the connection path.
3. Connect rooms with corridors aligned to door openings.
4. Add a `PlayerStart` near the main entry room.
5. Add one or two lights per room.

## Lighting Best Practices

- Every room needs at least one light source.
- Place point lights around `70%` of room height, not dead center.
- Use warm light for inhabited spaces: `(1.0, 0.85, 0.7)`.
- Use cool light for exterior or moonlight: `(0.7, 0.8, 1.0)`.
- Use rect lights near windows when available.
- Keep a subtle ambient fill so shadows are readable.

## Material Application Order

1. Call `get_available_materials` first.
2. If project materials exist, use `apply_material_to_actor`.
3. If not, use `set_mesh_material_color`.
4. Add small color variation to repeated elements.
5. Use `set_material_scalar_param` only when the material is known to expose a named scalar parameter.

## Horror Scene Tips

- Lower light intensity by `60-70%`.
- Use color temperatures below `4000K` or above `8000K`.
- Add fog with density around `0.03-0.08`.
- Leave some areas completely dark.
- Place some lights low to the ground for upward shadows.
- Use red accent lighting sparingly.

## Quality Checklist

- Every room has at least one light.
- All visible surfaces have materials.
- Doors are at least `100` cm wide and `200` cm tall.
- Props rest on valid surfaces.
- Objects do not overlap badly.
- The scene has a `PlayerStart`.

## Minimal Validation Loop

After a building batch:

1. Call `get_all_level_actors` or `get_actor_bounds` on the edited actors.
2. If camera framing matters, call `set_viewport_camera`.
3. Call `redraw_viewports`.
4. Call `take_screenshot`.
5. If the scene is still underspecified, refine the command description instead of repeating the same failing action.
