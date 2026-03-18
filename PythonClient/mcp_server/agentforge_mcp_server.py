"""
UEAgentForge MCP Server.

Exposes curated UEAgentForge tools over the standard Model Context Protocol so
Claude Desktop, Cursor, Windsurf, and other MCP clients can drive the Unreal
Editor through the existing Remote Control HTTP bridge.
"""

from __future__ import annotations

import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

from mcp.server.fastmcp import FastMCP


THIS_FILE = Path(__file__).resolve()
PYTHON_CLIENT_DIR = THIS_FILE.parents[1]
REPO_ROOT = THIS_FILE.parents[2]
KNOWLEDGE_BASE_DIR = THIS_FILE.parent / "knowledge_base"
BUILDING_GUIDE_PATH = KNOWLEDGE_BASE_DIR / "building_guide.md"

if str(PYTHON_CLIENT_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_CLIENT_DIR))

from ueagentforge_client import AgentForgeClient, ForgeResult  # noqa: E402


SERVER_NAME = "UEAgentForge"
SERVER_VERSION = "0.5.0"
DEFAULT_HOST = os.environ.get("UEAGENTFORGE_HOST", "127.0.0.1")
DEFAULT_PORT = int(os.environ.get("UEAGENTFORGE_PORT", "30010"))
DEFAULT_TIMEOUT = float(os.environ.get("UEAGENTFORGE_TIMEOUT", "60"))

mcp = FastMCP("UEAgentForge MCP Server", json_response=True)
_client: Optional[AgentForgeClient] = None


def get_client() -> AgentForgeClient:
    global _client
    if _client is None:
        _client = AgentForgeClient(
            host=DEFAULT_HOST,
            port=DEFAULT_PORT,
            timeout=DEFAULT_TIMEOUT,
            verify=True,
            max_retries=6,
            retry_backoff_sec=1.0,
        )
    return _client


def _payload(result: Any) -> Dict[str, Any]:
    if isinstance(result, ForgeResult):
        return result.raw
    if isinstance(result, dict):
        return result
    return {"raw": str(result)}


def _ensure_ok(result: Any) -> Dict[str, Any]:
    payload = _payload(result)
    if isinstance(result, ForgeResult):
        if not result.ok:
            raise RuntimeError(result.error or json.dumps(payload))
        return payload

    if "error" in payload:
        raise RuntimeError(str(payload["error"]))
    return payload


def _stringify_result(result: Dict[str, Any]) -> Dict[str, Any]:
    return json.loads(json.dumps(result))


@mcp.resource("guide://building")
def building_guide() -> str:
    """Scene-building playbook for architectural layout, materials, and lighting."""
    return BUILDING_GUIDE_PATH.read_text(encoding="utf-8")


@mcp.prompt()
def build_room_prompt(theme: str = "horror", scale: str = "medium") -> str:
    """Prompt template that teaches an MCP client how to assemble a room step by step."""
    return (
        f"Build a {scale} {theme} room in Unreal using UEAgentForge. "
        "Start by discovering available meshes and materials, then create a floor, walls, "
        "and a complete room shell. Add one or two lights only after the structure exists. "
        "Prefer project materials over graybox defaults when suitable. End with a screenshot "
        "or world-context check so the result can be reviewed."
    )


@mcp.tool()
def ping() -> Dict[str, Any]:
    """Check that UEAgentForge is reachable before issuing any scene-editing tools."""
    return _stringify_result(get_client().ping())


@mcp.tool()
def get_current_level() -> Dict[str, Any]:
    """Return the currently loaded Unreal level package path and actor prefix for object lookups."""
    return _stringify_result(get_client().get_current_level())


@mcp.tool()
def get_all_level_actors() -> Dict[str, Any]:
    """List current level actors with labels, classes, paths, transforms, and bounds for scene inspection."""
    return {"actors": get_client().get_all_level_actors()}


@mcp.tool()
def get_available_meshes(
    search_filter: str = "",
    path_filter: str = "",
    max_results: int = 50,
) -> Dict[str, Any]:
    """Search the Content Browser for static meshes. Call this before placing geometry so you use real project assets instead of guessing mesh paths."""
    return _stringify_result(get_client().get_available_meshes(
        search_filter=search_filter,
        path_filter=path_filter,
        max_results=max_results,
    ))


@mcp.tool()
def get_available_materials(
    search_filter: str = "",
    path_filter: str = "",
    max_results: int = 50,
) -> Dict[str, Any]:
    """Search the Content Browser for materials and material instances. Use this before applying materials so the tool call references valid assets."""
    return _stringify_result(get_client().get_available_materials(
        search_filter=search_filter,
        path_filter=path_filter,
        max_results=max_results,
    ))


@mcp.tool()
def get_available_blueprints(
    search_filter: str = "",
    parent_class: str = "",
    path_filter: str = "",
    max_results: int = 50,
) -> Dict[str, Any]:
    """Search Blueprint assets, optionally filtered by parent class like Actor, Character, or AIController. Use this before trying to spawn project Blueprints so you reference real generated classes instead of guessing paths."""
    return _stringify_result(get_client().get_available_blueprints(
        search_filter=search_filter,
        parent_class=parent_class,
        path_filter=path_filter,
        max_results=max_results,
    ))


@mcp.tool()
def get_available_textures(
    search_filter: str = "",
    path_filter: str = "",
    max_results: int = 50,
) -> Dict[str, Any]:
    """Search texture assets for material authoring and surface replacement workflows."""
    return _stringify_result(get_client().get_available_textures(
        search_filter=search_filter,
        path_filter=path_filter,
        max_results=max_results,
    ))


@mcp.tool()
def get_available_sounds(
    search_filter: str = "",
    path_filter: str = "",
    max_results: int = 50,
) -> Dict[str, Any]:
    """Search SoundWave and SoundCue assets so ambience and audio placement use valid project content."""
    return _stringify_result(get_client().get_available_sounds(
        search_filter=search_filter,
        path_filter=path_filter,
        max_results=max_results,
    ))


@mcp.tool()
def get_asset_details(asset_path: str) -> Dict[str, Any]:
    """Inspect one asset in detail before using it. Meshes report bounds and LOD count, materials report parameter names, textures report size, and sounds report duration when available."""
    return _stringify_result(get_client().get_asset_details(asset_path))


@mcp.tool()
def set_operator_policy(
    operator_only: Optional[bool] = None,
    allow_atomic_placement: Optional[bool] = None,
) -> Dict[str, Any]:
    """Adjust placement policy for the current editor session. Use this if direct actor placement is blocked by operator-only mode."""
    return _stringify_result(get_client().set_operator_policy(
        operator_only=operator_only,
        allow_atomic_placement=allow_atomic_placement,
    ))


@mcp.tool()
def spawn_point_light(
    x: float,
    y: float,
    z: float,
    intensity: float = 5000.0,
    color_r: float = 1.0,
    color_g: float = 1.0,
    color_b: float = 1.0,
    attenuation_radius: float = 1200.0,
    label: str = "",
) -> Dict[str, Any]:
    """Spawn a point light. Use warm values like (1.0, 0.85, 0.7) for interior bulbs, cooler values like (0.7, 0.8, 1.0) for moonlight, and set attenuation in centimeters."""
    return _ensure_ok(get_client().spawn_point_light(
        x=x,
        y=y,
        z=z,
        intensity=intensity,
        color_r=color_r,
        color_g=color_g,
        color_b=color_b,
        attenuation_radius=attenuation_radius,
        label=label,
    ))


@mcp.tool()
def spawn_rect_light(
    x: float,
    y: float,
    z: float,
    rx: float = 0.0,
    ry: float = 0.0,
    rz: float = 0.0,
    intensity: float = 5000.0,
    width: float = 100.0,
    height: float = 100.0,
    color_r: float = 1.0,
    color_g: float = 1.0,
    color_b: float = 1.0,
    label: str = "",
) -> Dict[str, Any]:
    """Spawn a rectangular area light for softer, more natural illumination. Use this for windows, ceiling panels, monitor glow, or fluorescent fixtures."""
    return _ensure_ok(get_client().spawn_rect_light(
        x=x,
        y=y,
        z=z,
        rx=rx,
        ry=ry,
        rz=rz,
        intensity=intensity,
        width=width,
        height=height,
        color_r=color_r,
        color_g=color_g,
        color_b=color_b,
        label=label,
    ))


@mcp.tool()
def spawn_directional_light(
    rx: float = -45.0,
    ry: float = 0.0,
    rz: float = 0.0,
    intensity: float = 10.0,
    color_r: float = 1.0,
    color_g: float = 1.0,
    color_b: float = 1.0,
    label: str = "",
) -> Dict[str, Any]:
    """Spawn a directional light for sun, moon, or broad stylized key lighting. Rotation controls direction; position is not important."""
    return _ensure_ok(get_client().spawn_directional_light(
        rx=rx,
        ry=ry,
        rz=rz,
        intensity=intensity,
        color_r=color_r,
        color_g=color_g,
        color_b=color_b,
        label=label,
    ))


@mcp.tool()
def set_static_mesh(actor_name: str, mesh_path: str) -> Dict[str, Any]:
    """Change which static mesh an actor displays. Common engine graybox meshes include /Engine/BasicShapes/Cube.Cube and Plane.Plane."""
    return _ensure_ok(get_client().set_static_mesh(actor_name, mesh_path))


@mcp.tool()
def set_actor_scale(actor_name: str, sx: float, sy: float, sz: float) -> Dict[str, Any]:
    """Set absolute actor scale. A Cube scaled to (10, 0.1, 3) forms a 1000cm x 10cm x 300cm wall-like element."""
    return _ensure_ok(get_client().set_actor_scale(actor_name, sx, sy, sz))


@mcp.tool()
def duplicate_actor(
    actor_name: str,
    offset_x: float = 0.0,
    offset_y: float = 0.0,
    offset_z: float = 0.0,
) -> Dict[str, Any]:
    """Duplicate an existing actor with an offset. Use this for repeated props, modular layout patterns, and fast array-based scene construction."""
    return _ensure_ok(get_client().duplicate_actor(
        actor_name=actor_name,
        offset_x=offset_x,
        offset_y=offset_y,
        offset_z=offset_z,
    ))


@mcp.tool()
def set_actor_label(actor_name: str, new_label: str) -> Dict[str, Any]:
    """Rename an actor to something descriptive like NorthWall_01 or PointLight_Hallway for clearer editor organization."""
    return _ensure_ok(get_client().set_actor_label(actor_name, new_label))


@mcp.tool()
def set_actor_mobility(actor_name: str, mobility: str) -> Dict[str, Any]:
    """Set component mobility to Static, Stationary, or Movable. Use Static for architecture, Stationary for fixed lights with dynamic shadowing, and Movable for runtime motion."""
    return _ensure_ok(get_client().set_actor_mobility(actor_name, mobility))


@mcp.tool()
def set_actor_visibility(actor_name: str, visible: bool) -> Dict[str, Any]:
    """Show or hide an actor without deleting it. Useful for scene variants, staging, and temporary iteration states."""
    return _ensure_ok(get_client().set_actor_visibility(actor_name, visible))


@mcp.tool()
def group_actors(actor_names: List[str], group_name: str) -> Dict[str, Any]:
    """Attach multiple actors under a new parent actor so they can be treated as one logical unit, such as a room shell or prop cluster."""
    return _ensure_ok(get_client().group_actors(actor_names, group_name))


@mcp.tool()
def get_actor_property(actor_name: str, property_name: str) -> Dict[str, Any]:
    """Read one reflected actor or component property using dot notation like LightComponent.Intensity or StaticMeshComponent.StaticMesh."""
    return _stringify_result(get_client().get_actor_property(actor_name, property_name))


@mcp.tool()
def set_actor_property(actor_name: str, property_name: str, value: str) -> Dict[str, Any]:
    """Write one reflected actor or component property using dot notation. This is the precise escape hatch for properties that do not have a dedicated tool yet."""
    return _ensure_ok(get_client().set_actor_property(actor_name, property_name, value))


@mcp.tool()
def set_mesh_material_color(
    actor_name: str,
    r: float,
    g: float,
    b: float,
    a: float = 1.0,
    slot_index: int = 0,
) -> Dict[str, Any]:
    """Create or reuse a dynamic material instance and tint an actor quickly for graybox and mood iteration."""
    return _ensure_ok(get_client().set_mesh_material_color(
        actor_name=actor_name,
        r=r,
        g=g,
        b=b,
        a=a,
        slot_index=slot_index,
    ))


@mcp.tool()
def apply_material_to_actor(actor_name: str, material_path: str, slot_index: int = 0) -> Dict[str, Any]:
    """Apply a specific material or material instance to an actor mesh slot. Use get_available_materials first if you do not know the asset path."""
    return _ensure_ok(get_client().apply_material_to_actor(
        actor_name=actor_name,
        material_path=material_path,
        slot_index=slot_index,
    ))


@mcp.tool()
def create_wall(
    start_x: float,
    start_y: float,
    end_x: float,
    end_y: float,
    height: float = 300.0,
    thickness: float = 20.0,
    material_path: str = "",
    has_windows: bool = False,
    window_spacing: float = 220.0,
    window_height: float = 120.0,
    label: str = "Wall",
    z: float = 0.0,
) -> Dict[str, Any]:
    """Create a wall segment between two points. Use clockwise wall placement to form rooms and set thickness/height in centimeters."""
    return _ensure_ok(get_client().create_wall(
        start_x=start_x,
        start_y=start_y,
        end_x=end_x,
        end_y=end_y,
        height=height,
        thickness=thickness,
        material_path=material_path,
        has_windows=has_windows,
        window_spacing=window_spacing,
        window_height=window_height,
        label=label,
        z=z,
    ))


@mcp.tool()
def create_floor(
    center_x: float,
    center_y: float,
    z: float,
    width: float,
    length: float,
    material_path: str = "",
    label: str = "Floor",
    thickness: float = 10.0,
) -> Dict[str, Any]:
    """Create a floor or ceiling slab. Width maps to X, length maps to Y, and z is the finished surface height."""
    return _ensure_ok(get_client().create_floor(
        center_x=center_x,
        center_y=center_y,
        z=z,
        width=width,
        length=length,
        material_path=material_path,
        label=label,
        thickness=thickness,
    ))


@mcp.tool()
def create_room(
    center_x: float,
    center_y: float,
    z: float,
    width: float,
    length: float,
    height: float = 300.0,
    wall_thickness: float = 20.0,
    floor_material: str = "",
    wall_material: str = "",
    ceiling_material: str = "",
    door_wall: str = "",
    window_walls: Optional[List[str]] = None,
    label: str = "Room",
    slab_thickness: float = 10.0,
) -> Dict[str, Any]:
    """Create a complete room shell with floor, ceiling, walls, optional windows, and an optional door opening. Use this when you need a whole architectural unit instead of hand-placing every segment."""
    return _ensure_ok(get_client().create_room(
        center_x=center_x,
        center_y=center_y,
        z=z,
        width=width,
        length=length,
        height=height,
        wall_thickness=wall_thickness,
        floor_material=floor_material,
        wall_material=wall_material,
        ceiling_material=ceiling_material,
        door_wall=door_wall,
        window_walls=window_walls or [],
        label=label,
        slab_thickness=slab_thickness,
    ))


@mcp.tool()
def create_corridor(
    start_x: float,
    start_y: float,
    end_x: float,
    end_y: float,
    width: float,
    height: float = 300.0,
    wall_material: str = "",
    floor_material: str = "",
    has_ceiling: bool = True,
    label: str = "Corridor",
    z: float = 0.0,
) -> Dict[str, Any]:
    """Create a hallway between two points with floor, side walls, and optional ceiling. Use this to connect room openings cleanly instead of hand-building corridor pieces."""
    return _ensure_ok(get_client().create_corridor(
        start_x=start_x,
        start_y=start_y,
        end_x=end_x,
        end_y=end_y,
        width=width,
        height=height,
        wall_material=wall_material,
        floor_material=floor_material,
        has_ceiling=has_ceiling,
        label=label,
        z=z,
    ))


@mcp.tool()
def create_pillar(
    x: float,
    y: float,
    z: float,
    radius: float,
    height: float,
    sides: int = 16,
    material_path: str = "",
    label: str = "Pillar",
) -> Dict[str, Any]:
    """Create a pillar or column. Use lower side counts for blocky supports and higher counts for rounder columns."""
    return _ensure_ok(get_client().create_pillar(
        x=x,
        y=y,
        z=z,
        radius=radius,
        height=height,
        sides=sides,
        material_path=material_path,
        label=label,
    ))


@mcp.tool()
def focus_viewport_on_actor(actor_name: str) -> Dict[str, Any]:
    """Move the editor camera to frame one actor so the next screenshot or inspection step is centered on the thing you just changed."""
    return _stringify_result(get_client().focus_viewport_on_actor(actor_name))


@mcp.tool()
def get_viewport_info() -> Dict[str, Any]:
    """Read the current perspective viewport camera location, rotation, FOV, and dimensions."""
    return _stringify_result(get_client().get_viewport_info())


@mcp.tool()
def create_snapshot(snapshot_name: str = "") -> Dict[str, Any]:
    """Create a level snapshot before larger edits so you can verify or recover from a generation pass."""
    return _ensure_ok(get_client().create_snapshot(snapshot_name=snapshot_name))


@mcp.tool()
def run_verification(phase_mask: int = 15) -> Dict[str, Any]:
    """Run the four-phase verification protocol. Use this before or after large edits when you want explicit safety feedback."""
    report = get_client().run_verification(phase_mask=phase_mask)
    return {
        "all_passed": report.all_passed,
        "phases_run": report.phases_run,
        "details": report.details,
    }


@mcp.tool()
def observe_analyze_plan_act(
    description: str,
    max_iterations: int = 1,
    score_target: float = 60.0,
) -> Dict[str, Any]:
    """Run the OAPA closed-loop scene refinement pipeline for higher-level improvement tasks."""
    return _stringify_result(get_client().observe_analyze_plan_act(
        description=description,
        max_iterations=max_iterations,
        score_target=score_target,
    ))


@mcp.tool()
def llm_set_key(provider: str, key: str) -> Dict[str, Any]:
    """Store an API key in the active Unreal Editor session for one LLM provider. Provider values: Anthropic, OpenAI, DeepSeek, OpenAICompatible."""
    return _ensure_ok(get_client().execute("llm_set_key", {
        "provider": provider,
        "key": key,
    }))


@mcp.tool()
def llm_get_models(provider: str) -> Dict[str, Any]:
    """List the built-in model names known for an LLM provider so you can choose a valid value for llm_chat or llm_structured."""
    return _stringify_result(get_client()._send("llm_get_models", {"provider": provider}))


@mcp.tool()
def llm_chat(
    provider: str,
    model: str,
    messages: List[Dict[str, Any]],
    system: str = "",
    max_tokens: int = 1024,
    temperature: float = 0.7,
    custom_endpoint: str = "",
) -> Dict[str, Any]:
    """Send a multi-provider chat completion request through the Unreal editor. Use this for NPC dialogue, design reasoning, naming, quest beats, and other text generation tasks."""
    return _ensure_ok(get_client().execute("llm_chat", {
        "provider": provider,
        "model": model,
        "messages": messages,
        "system": system,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "custom_endpoint": custom_endpoint,
    }))


@mcp.tool()
def llm_structured(
    provider: str,
    model: str,
    prompt: str,
    schema: Dict[str, Any],
    system: str = "",
    max_tokens: int = 1024,
    temperature: float = 0.2,
    custom_endpoint: str = "",
) -> Dict[str, Any]:
    """Request structured JSON output from a provider. Use this when you need machine-readable plans like NPC specs, quest payloads, room briefs, or content metadata."""
    return _ensure_ok(get_client().execute("llm_structured", {
        "provider": provider,
        "model": model,
        "prompt": prompt,
        "schema": schema,
        "system": system,
        "max_tokens": max_tokens,
        "temperature": temperature,
        "custom_endpoint": custom_endpoint,
    }))


@mcp.tool()
def execute_forge_command(cmd: str, args: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """Low-level escape hatch for any UEAgentForge command not yet promoted to a dedicated MCP tool. Prefer the specific tools above when they exist."""
    return _ensure_ok(get_client().execute(cmd, args or {}))


def main() -> None:
    """Run the MCP server over stdio for desktop MCP clients."""
    mcp.run()


if __name__ == "__main__":
    main()
