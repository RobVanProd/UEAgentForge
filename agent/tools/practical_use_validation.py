from __future__ import annotations

import json
import sys
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, List, Optional


REPO_ROOT = Path(__file__).resolve().parents[2]
PYTHON_CLIENT_DIR = REPO_ROOT / "PythonClient"
if str(PYTHON_CLIENT_DIR) not in sys.path:
    sys.path.insert(0, str(PYTHON_CLIENT_DIR))

from ueagentforge_client import AgentForgeClient, ForgeResult  # noqa: E402


LOG_DIR = REPO_ROOT / "agent" / "logs"
LOG_DIR.mkdir(parents=True, exist_ok=True)
STAMP = datetime.now().strftime("%Y%m%d_%H%M%S")
JSON_PATH = LOG_DIR / f"practical_use_validation_{STAMP}.json"
TSV_PATH = LOG_DIR / "practical_use_validation_latest.tsv"

LIMITATION_TOKENS = (
    "No API key configured for the selected provider.",
    "request failed before receiving a response.",
    "received an error response.",
    "Unable to resolve a valid vision provider/model.",
    "invalid_api_key",
    "Incorrect API key provided",
    "Fab.com has no public download API",
    "No imported assets found",
    "Failed to find matching node",
    "No navigation system available",
    "No valid NavData",
    "PreFlight FAILED: Constitution violations:",
    "Fab.com does not provide a public download API",
)


def is_success(result: Any) -> bool:
    if isinstance(result, ForgeResult):
        return result.ok
    if isinstance(result, dict):
        return not result.get("error") and not result.get("error_message") and result.get("ok", True)
    return False


def payload_for(result: Any) -> Dict[str, Any]:
    if isinstance(result, ForgeResult):
        return result.raw
    if isinstance(result, dict):
        return result
    return {"raw": str(result)}


def error_for(result: Any) -> str:
    if isinstance(result, ForgeResult):
        return result.error or ""
    if isinstance(result, dict):
        return str(result.get("error") or result.get("error_message") or "")
    return str(result)


def classify_status(result: Any) -> str:
    payload = payload_for(result)
    if is_success(result):
        if payload.get("error") and "No runnable manual verification phases were selected." in str(payload.get("error", "")):
            return "no_op"
        verification_mode = str(payload.get("verification_mode", ""))
        if payload.get("unverified") or verification_mode == "bypass_unverified":
            return "unverified_success"
        if verification_mode.startswith("main_path_partial") or verification_mode in {"bypass_manual_verification", "manual_observational"}:
            return "partial_verified_success"
        return "verified_success"
    detail = error_for(result)
    if payload.get("unsupported_external_dependency") or payload.get("availability") == "unsupported_external_dependency":
        return "blocked"
    if "No runnable manual verification phases were selected." in detail or "No verification phases executed." in detail:
        return "no_op"
    if any(token in detail for token in LIMITATION_TOKENS):
        return "blocked"
    return "fail"


def first_nonempty(*values: str) -> str:
    for value in values:
        if value:
            return value
    return ""


def main() -> int:
    client = AgentForgeClient(timeout=120.0, max_retries=6, retry_backoff_sec=1.0)
    results: List[Dict[str, Any]] = []
    run_suffix = STAMP.replace("_", "")
    base_x = 5000.0
    base_y = 5000.0
    validation_root = "/Game/AgentForgeTest/Validation"
    validation_moved = f"{validation_root}/Moved"

    def record(stage: str, command: str, result: Any, detail: str = "") -> Dict[str, Any]:
        status = classify_status(result)
        payload = payload_for(result)
        entry = {
            "stage": stage,
            "command": command,
            "status": status,
            "detail": detail if detail else error_for(result),
            "payload": payload,
        }
        results.append(entry)
        return entry

    def record_payload(stage: str, command: str, payload: Dict[str, Any], status: Optional[str] = None, detail: str = "") -> Dict[str, Any]:
        entry = {
            "stage": stage,
            "command": command,
            "status": status or classify_status(payload),
            "detail": detail,
            "payload": payload,
        }
        results.append(entry)
        return entry

    def do_execute(stage: str, command: str, args: Optional[Dict[str, Any]] = None, detail: str = "") -> Dict[str, Any]:
        return record(stage, command, client.execute(command, args or {}), detail)

    if not client.wait_for_editor(timeout_sec=240.0, poll_sec=2.0):
        record_payload("bootstrap", "editor_start", {"error": "Editor did not answer ping() within 240 seconds."}, status="fail", detail="Editor did not answer ping() within 240 seconds.")
        JSON_PATH.write_text(json.dumps({"ok": False, "results": results}, indent=2), encoding="utf-8")
        TSV_PATH.write_text("stage\tcommand\tstatus\tdetail\nbootstrap\teditor_start\tfail\tEditor did not answer ping() within 240 seconds.\n", encoding="utf-8")
        return 2

    pong = client.ping()
    record_payload("bootstrap", "ping", pong, status="verified_success" if pong.get("version") == "0.5.0" else "fail", detail=str(pong.get("version", "")))
    record_payload("bootstrap", "get_current_level", client.get_current_level())
    record_payload("bootstrap", "get_forge_status", client.get_forge_status())
    verification = client.run_verification(phase_mask=15)
    record_payload(
        "bootstrap",
        "run_verification",
        {
            "verification_mode": verification.verification_mode,
            "all_passed": verification.all_passed,
            "phases_run": verification.phases_run,
            "requested_phase_mask": verification.requested_phase_mask,
            "runnable_phase_mask": verification.runnable_phase_mask,
            "effective_phase_mask": verification.effective_phase_mask,
            "details": verification.details,
            "requested_but_not_run": verification.requested_but_not_run,
            "out_of_scope_requested_phases": verification.out_of_scope_requested_phases,
            "error": verification.error,
        },
        detail=f"mode={verification.verification_mode}; phases_run={verification.phases_run}; requested_but_not_run={len(verification.requested_but_not_run)}; out_of_scope={len(verification.out_of_scope_requested_phases)}; error={verification.error or ''}",
    )
    record("bootstrap", "set_operator_policy", client.set_operator_policy(operator_only=False, allow_atomic_placement=True))

    ensure_dirs = client.execute_python_multiline(
        f"""
import unreal
unreal.EditorAssetLibrary.make_directory({json.dumps(validation_root)})
unreal.EditorAssetLibrary.make_directory({json.dumps(validation_moved)})
"""
    )
    record("bootstrap", "execute_python_make_validation_dirs", ensure_dirs)

    meshes = client.get_available_meshes(search_filter="Cube", path_filter="/Engine/BasicShapes", max_results=10)
    materials = client.get_available_materials(search_filter="BasicShapeMaterial", path_filter="/Engine/BasicShapes", max_results=10)
    record_payload("bootstrap", "get_available_meshes", meshes, status="verified_success" if int(meshes.get("count", 0)) > 0 else "fail")
    record_payload("bootstrap", "get_available_materials", materials, status="verified_success" if int(materials.get("count", 0)) > 0 else "fail")

    mesh_path = "/Engine/BasicShapes/Cube.Cube"
    for asset in meshes.get("assets", []):
        candidate = str(asset.get("asset_path", "")).strip()
        if candidate:
            mesh_path = candidate
            break

    material_path = "/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"
    for asset in materials.get("assets", []):
        candidate = str(asset.get("asset_path", "")).strip()
        if candidate:
            material_path = candidate
            break

    basic_floor_label = f"PracticalFloor_{run_suffix}"
    room_a_label = f"PracticalRoomA_{run_suffix}"
    room_b_label = f"PracticalRoomB_{run_suffix}"
    corridor_label = f"PracticalCorridor_{run_suffix}"
    staircase_label = f"PracticalStairs_{run_suffix}"
    pillar_label = f"PracticalPillar_{run_suffix}"
    player_label = f"PracticalPlayerStart_{run_suffix}"
    exit_label = f"PracticalExitTrigger_{run_suffix}"

    record("stage_basic", "create_floor", client.create_floor(
        center_x=base_x,
        center_y=base_y,
        z=0.0,
        width=2200.0,
        length=1200.0,
        thickness=12.0,
        material_path=material_path,
        label=basic_floor_label,
    ))
    record("stage_basic", "create_room_a", client.create_room(
        center_x=base_x - 300.0,
        center_y=base_y,
        z=0.0,
        width=900.0,
        length=900.0,
        height=320.0,
        wall_thickness=20.0,
        floor_material=material_path,
        wall_material=material_path,
        label=room_a_label,
    ))
    record("stage_basic", "create_corridor", client.create_corridor(
        start_x=base_x + 150.0,
        start_y=base_y,
        end_x=base_x + 950.0,
        end_y=base_y,
        z=0.0,
        width=260.0,
        height=300.0,
        wall_thickness=20.0,
        slab_thickness=12.0,
        has_ceiling=True,
        floor_material=material_path,
        wall_material=material_path,
        label=corridor_label,
    ))
    record("stage_basic", "create_room_b", client.create_room(
        center_x=base_x + 1350.0,
        center_y=base_y,
        z=0.0,
        width=900.0,
        length=900.0,
        height=320.0,
        wall_thickness=20.0,
        floor_material=material_path,
        wall_material=material_path,
        label=room_b_label,
    ))

    player_spawn = client.spawn_actor("/Script/Engine.PlayerStart", x=base_x - 650.0, y=base_y, z=40.0)
    record("stage_basic", "spawn_player_start", player_spawn)
    player_spawn_name = first_nonempty(payload_for(player_spawn).get("spawned_name", ""), payload_for(player_spawn).get("actor_name", ""))
    if player_spawn_name:
        record("stage_basic", "label_player_start", client.set_actor_label(player_spawn_name, player_label))

    exit_spawn = client.spawn_actor("/Script/Engine.TriggerBox", x=base_x + 1650.0, y=base_y, z=120.0)
    record("stage_basic", "spawn_exit_trigger", exit_spawn)
    exit_spawn_name = first_nonempty(payload_for(exit_spawn).get("spawned_name", ""), payload_for(exit_spawn).get("actor_name", ""))
    if exit_spawn_name:
        record("stage_basic", "label_exit_trigger", client.set_actor_label(exit_spawn_name, exit_label))
        record("stage_basic", "scale_exit_trigger", client.set_actor_scale(exit_label, 1.5, 1.5, 2.0))

    point_light = client.spawn_point_light(
        x=base_x - 250.0,
        y=base_y,
        z=220.0,
        intensity=3500.0,
        color_r=1.0,
        color_g=0.88,
        color_b=0.72,
        label=f"PracticalPoint_{run_suffix}",
    )
    record("stage_basic", "spawn_point_light", point_light)
    record("stage_basic", "spawn_spot_light", client.spawn_spot_light(
        x=base_x + 1150.0,
        y=base_y,
        z=260.0,
        rx=-50.0,
        ry=0.0,
        rz=0.0,
        intensity=4200.0,
        outer_cone_angle=34.0,
        label=f"PracticalSpot_{run_suffix}",
    ))

    surface_actor_label = f"PracticalSurface_{run_suffix}"
    surface_spawn = client.spawn_actor_at_surface(
        class_path="/Script/Engine.StaticMeshActor",
        label=surface_actor_label,
        origin={"x": base_x + 150.0, "y": base_y, "z": 650.0},
        direction={"x": 0.0, "y": 0.0, "z": -1.0},
        max_distance=2000.0,
        align_to_normal=True,
    )
    record_payload("stage_spatial", "spawn_actor_at_surface", surface_spawn)
    surface_payload = payload_for(surface_spawn)
    surface_actor_path = first_nonempty(surface_payload.get("actor_path", ""), surface_payload.get("actor_object_path", ""))
    surface_location = surface_payload.get("location", {}) if isinstance(surface_payload.get("location"), dict) else {}
    if surface_actor_path:
        raised_z = float(surface_location.get("z", 0.0)) + 140.0
        record("stage_spatial", "raise_surface_actor", client.set_actor_transform(
            surface_actor_path,
            x=float(surface_location.get("x", base_x + 150.0)),
            y=float(surface_location.get("y", base_y)),
            z=raised_z,
        ))
        record_payload("stage_spatial", "align_actors_to_surface", client.align_actors_to_surface(
            [surface_actor_label],
            down_trace_extent=500.0,
        ))

    record_payload("stage_basic", "focus_viewport_on_actor", client.focus_viewport_on_actor(room_a_label))
    record("stage_basic", "take_focused_screenshot", client.take_focused_screenshot(
        filename=f"practical_basic_{STAMP}",
        x=base_x + 500.0,
        y=base_y - 1750.0,
        z=700.0,
        pitch=-12.0,
        yaw=0.0,
        roll=0.0,
    ))
    record_payload("stage_basic", "get_world_context", client.get_world_context(
        max_actors=160,
        max_relationships=64,
        include_components=False,
        include_screenshot=True,
        screenshot_label=f"practical_world_{STAMP}",
    ))

    record("stage_dressing", "create_staircase", client.create_staircase(
        base_x=base_x + 950.0,
        base_y=base_y + 180.0,
        base_z=0.0,
        direction="east",
        step_count=7,
        step_width=180.0,
        step_depth=40.0,
        step_height=20.0,
        material_path=material_path,
        label=staircase_label,
    ))
    record("stage_dressing", "create_pillar", client.create_pillar(
        x=base_x - 450.0,
        y=base_y + 250.0,
        z=0.0,
        radius=28.0,
        height=300.0,
        sides=16,
        material_path=material_path,
        label=pillar_label,
    ))
    record("stage_dressing", "scatter_props", client.scatter_props(
        mesh_path=mesh_path,
        center_x=base_x + 1350.0,
        center_y=base_y,
        z=0.0,
        radius=240.0,
        count=8,
        min_scale=0.35,
        max_scale=0.9,
        random_rotation=True,
        snap_to_surface=True,
        material_path=material_path,
        label_prefix=f"PracticalProp_{run_suffix}",
    ))
    record("stage_dressing", "set_fog", client.set_fog(
        density=0.028,
        height_falloff=0.18,
        start_distance=200.0,
        color_r=0.62,
        color_g=0.69,
        color_b=0.82,
    ))
    record("stage_dressing", "set_post_process", client.set_post_process(
        bloom_intensity=0.45,
        exposure_compensation=-0.25,
        ambient_occlusion_intensity=0.65,
        vignette_intensity=0.3,
        saturation=0.92,
        contrast=1.08,
        color_temp=5900.0,
    ))
    record("stage_dressing", "set_sky_atmosphere", client.set_sky_atmosphere("golden_hour"))
    record("stage_dressing", "spawn_rect_light", client.spawn_rect_light(
        x=base_x + 1450.0,
        y=base_y - 180.0,
        z=240.0,
        rx=-45.0,
        intensity=2500.0,
        width=180.0,
        height=90.0,
        label=f"PracticalRect_{run_suffix}",
    ))
    record("stage_dressing", "spawn_directional_light", client.spawn_directional_light(
        rx=-20.0,
        ry=35.0,
        rz=0.0,
        intensity=5.0,
        label=f"PracticalDirectional_{run_suffix}",
    ))
    record_payload("stage_dressing", "get_actor_bounds", client.get_actor_bounds(room_b_label))
    record_payload("stage_dressing", "cast_ray", client.cast_ray(
        {"x": base_x + 1350.0, "y": base_y, "z": 500.0},
        {"x": base_x + 1350.0, "y": base_y, "z": -300.0},
        trace_complex=False,
    ))
    record_payload("stage_dressing", "query_navmesh", client.query_navmesh(
        base_x - 650.0,
        base_y,
        20.0,
        extent_x=120.0,
        extent_y=120.0,
        extent_z=200.0,
    ))
    record_payload("stage_dressing", "get_actors_in_radius", client.get_actors_in_radius(
        base_x,
        base_y,
        100.0,
        radius=1600.0,
    ))
    record_payload("stage_dressing", "analyze_level_composition", client.analyze_level_composition())

    material_details = client.get_asset_details(material_path)
    record_payload("stage_content", "get_asset_details_material", material_details, status="verified_success" if material_details.get("ok", True) else "fail")
    scalar_params = material_details.get("scalar_parameters", []) or material_details.get("scalar_params", []) or []
    vector_params = material_details.get("vector_parameters", []) or material_details.get("vector_params", []) or []

    mi_name = f"MI_PracticalApplied_{run_suffix}"
    mi_result = client.create_material_instance(material_path, mi_name, validation_root)
    record("stage_content", "create_material_instance_apply", mi_result)
    mi_path = f"{validation_root}/{mi_name}.{mi_name}"
    if is_success(mi_result):
        set_params_args: Dict[str, Any] = {}
        if scalar_params:
            set_params_args["scalar_params"] = {str(scalar_params[0]): 0.7}
        if vector_params:
            set_params_args["vector_params"] = {str(vector_params[0]): {"r": 0.2, "g": 0.65, "b": 0.24, "a": 1.0}}
        if set_params_args:
            record("stage_content", "set_material_params", client.set_material_params(mi_path, **set_params_args))
        record("stage_content", "apply_material_to_actor", client.apply_material_to_actor(basic_floor_label, mi_path, slot_index=0))

    record("stage_content", "set_mesh_material_color", client.set_mesh_material_color(basic_floor_label, 0.18, 0.22, 0.26))
    record("stage_content", "set_material_scalar_param", client.set_material_scalar_param(basic_floor_label, "Roughness", 0.85))

    temp_mi_name = f"MI_PracticalTemp_{run_suffix}"
    temp_mi_result = client.create_material_instance(material_path, temp_mi_name, validation_root)
    record("stage_content", "create_material_instance_temp", temp_mi_result)
    renamed_mi_name = f"MI_PracticalRenamed_{run_suffix}"
    renamed_asset_path = f"{validation_root}/{renamed_mi_name}"
    if is_success(temp_mi_result):
        temp_asset_path = f"{validation_root}/{temp_mi_name}"
        rename_result = client.rename_asset(temp_asset_path, renamed_mi_name)
        record("stage_content", "rename_asset", rename_result)
        move_result = client.move_asset(renamed_asset_path, validation_moved)
        record("stage_content", "move_asset", move_result)
        moved_path = f"{validation_moved}/{renamed_mi_name}"
        record("stage_content", "delete_asset", client.delete_asset(moved_path))

    bp_name = f"BP_PracticalMarker_{run_suffix}"
    bp_result = client.create_blueprint(bp_name, "/Script/Engine.StaticMeshActor", validation_root)
    record("stage_content", "create_blueprint", bp_result)
    bp_asset_path = f"{validation_root}/{bp_name}"
    bp_class_path = f"{bp_asset_path}.{bp_name}_C"
    if is_success(bp_result):
        record("stage_content", "compile_blueprint", client.compile_blueprint(bp_asset_path))
        record("stage_content", "set_blueprint_cdo_property", client.set_blueprint_cdo_property(bp_asset_path, "bCanBeDamaged", "bool", False))
        bp_spawn = client.spawn_actor(bp_class_path, x=base_x + 1550.0, y=base_y + 260.0, z=60.0)
        record("stage_content", "spawn_blueprint_actor", bp_spawn)
        bp_spawn_name = first_nonempty(payload_for(bp_spawn).get("spawned_name", ""), payload_for(bp_spawn).get("actor_name", ""))
        if bp_spawn_name:
            record("stage_content", "set_static_mesh_blueprint_actor", client.set_static_mesh(bp_spawn_name, mesh_path))
            record("stage_content", "set_actor_scale_blueprint_actor", client.set_actor_scale(bp_spawn_name, 0.8, 0.8, 1.6))
        record("stage_content", "edit_blueprint_node", client.edit_blueprint_node(
            bp_asset_path,
            node_type="Event",
            node_title="BeginPlay",
            pins=[],
        ))

    record_payload("stage_intelligence", "get_viewport_info", client.get_viewport_info())
    record_payload("stage_intelligence", "get_multi_view_capture", client.get_multi_view_capture(angle="top", orbit_radius=2200.0, center_x=base_x, center_y=base_y, center_z=120.0))
    record_payload("stage_intelligence", "get_semantic_env_snapshot", client.get_semantic_env_snapshot())
    record_payload("stage_intelligence", "get_level_hierarchy", client._send("get_level_hierarchy"))
    record_payload("stage_intelligence", "get_deep_properties", client._send("get_deep_properties", {"label": room_a_label}))
    record_payload("stage_intelligence", "place_asset_thematically", client.place_asset_thematically(
        class_path="/Script/Engine.StaticMeshActor",
        count=2,
        label_prefix=f"PracticalTheme_{run_suffix}",
        reference_area={"x": base_x + 1350.0, "y": base_y, "z": 0.0, "radius": 300.0},
        theme_rules={"genre": "horror"},
    ))
    record_payload("stage_intelligence", "apply_genre_rules", client._send("apply_genre_rules", {"genre": "horror"}))
    record_payload("stage_intelligence", "enhance_current_level", client._send("enhance_current_level", {"instruction": "Improve the validation wing with subtle horror atmosphere and better focal lighting."}))
    record_payload("stage_intelligence", "observe_analyze_plan_act", client.observe_analyze_plan_act(
        "Improve the validation wing toward a polished horror slice with better contrast and prop storytelling.",
        max_iterations=1,
        score_target=70.0,
    ))
    record_payload("stage_intelligence", "enhance_horror_scene", client._send("enhance_horror_scene", {"intensity": 0.65}))

    record_payload("stage_presets", "list_presets", client._send("list_presets"))
    record_payload("stage_presets", "suggest_preset", client._send("suggest_preset"))
    record_payload("stage_presets", "load_preset", client._send("load_preset", {"name": "Horror"}))
    record_payload("stage_presets", "get_current_preset", client._send("get_current_preset"))
    record_payload("stage_presets", "save_preset", client._send("save_preset", {"name": f"Practical_{run_suffix}", "description": "Practical validation preset snapshot"}))

    record_payload("stage_pipeline", "create_blockout_level", client._send("create_blockout_level", {"preset": "Horror"}))
    record_payload("stage_pipeline", "apply_set_dressing", client._send("apply_set_dressing", {"story_theme": "horror", "prop_density": 0.4}))
    record_payload("stage_pipeline", "apply_professional_lighting", client._send("apply_professional_lighting", {"preset": "Horror", "time_of_day": "night"}))
    record_payload("stage_pipeline", "generate_full_quality_level", client._send("generate_full_quality_level", {"preset": "Horror", "goal": "Extend the practical validation wing into a fuller horror encounter space.", "quality_target": 0.55, "save_level": False}))

    record_payload("stage_operator", "get_procedural_capabilities", client.get_procedural_capabilities())
    record_payload("stage_operator", "get_operator_policy", client.get_operator_policy())
    record("stage_operator", "set_operator_policy_locked", client.set_operator_policy(operator_only=False, allow_atomic_placement=True))
    record_payload("stage_operator", "op_terrain_generate", client.execute("op_terrain_generate", {
        "seed": 7,
        "width": 64,
        "height": 64,
        "frequency": 0.015,
        "amplitude": 140.0,
        "ridge_strength": 0.35,
        "erosion_iterations": 3,
        "erosion_strength": 0.08,
        "spawn_landscape": False,
    }).raw)
    record_payload("stage_operator", "run_operator_pipeline", client.execute("run_operator_pipeline", {
        "seed": 11,
        "terrain_generate": {
            "width": 64,
            "height": 64,
            "spawn_landscape": False,
        },
    }).raw)

    record_payload("stage_external", "llm_get_models_openai", client.llm_get_models("OpenAI"))
    record("stage_external", "llm_chat", client.llm_chat(
        provider="OpenAI",
        model="gpt-4o",
        messages=[{"role": "user", "content": "Reply with the word validation."}],
        max_tokens=32,
        temperature=0.0,
    ))
    record("stage_external", "llm_stream", client.llm_stream(
        provider="OpenAI",
        model="gpt-4o",
        messages=[{"role": "user", "content": "Reply with the word stream."}],
        max_tokens=32,
        temperature=0.0,
    ))
    record("stage_external", "llm_structured", client.generate_level_layout(
        provider="OpenAI",
        model="gpt-4o",
        prompt="Design a compact horror encounter wing with three spaces.",
        max_tokens=256,
        temperature=0.2,
    ))
    record("stage_external", "vision_analyze", client.vision_analyze(
        prompt="Analyze the current validation scene for composition, lighting, and readability.",
        multi_view=True,
    ))
    record("stage_external", "vision_quality_score", client.vision_quality_score(multi_view=True))
    record_payload("stage_external", "search_fab_assets", client._send("search_fab_assets", {"query": "horror props", "max_results": 5, "free_only": True}))
    record_payload("stage_external", "download_fab_asset", client._send("download_fab_asset", {"id": "stub"}))
    record_payload("stage_external", "list_imported_assets", client._send("list_imported_assets", {"content_path": "/Game/FabImports"}))

    ok = all(item["status"] not in ("fail", "blocked", "no_op") for item in results)
    summary = {
        "ok": ok,
        "counts": {
            "verified_success": sum(1 for item in results if item["status"] == "verified_success"),
            "partial_verified_success": sum(1 for item in results if item["status"] == "partial_verified_success"),
            "unverified_success": sum(1 for item in results if item["status"] == "unverified_success"),
            "blocked": sum(1 for item in results if item["status"] == "blocked"),
            "fail": sum(1 for item in results if item["status"] == "fail"),
            "no_op": sum(1 for item in results if item["status"] == "no_op"),
        },
        "results": results,
    }
    JSON_PATH.write_text(json.dumps(summary, indent=2), encoding="utf-8")

    lines = ["stage\tcommand\tstatus\tdetail"]
    for item in results:
        detail = str(item.get("detail", "")).replace("\t", " ").replace("\n", " ").strip()
        lines.append(f"{item['stage']}\t{item['command']}\t{item['status']}\t{detail}")
    TSV_PATH.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return 0 if ok else 2


if __name__ == "__main__":
    raise SystemExit(main())
