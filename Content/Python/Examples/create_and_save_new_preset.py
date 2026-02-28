"""
UEAgentForge v0.4.0 — Example: Create and Save a Custom Preset
==============================================================
Demonstrates the Level Preset System:
  - List all available presets
  - Load a built-in preset as a starting point
  - Modify values to create a custom genre preset
  - Save it to Content/AgentForge/Presets/<name>.json
  - Verify it persists by listing presets again

Custom presets persist across editor sessions and can be shared
across projects by copying the JSON file.
"""
import json, requests

BASE = "http://127.0.0.1:30010/remote/object/call"
CDO  = "/Script/UEAgentForge.Default__AgentForgeLibrary"


def call(cmd, args=None):
    body = {"cmd": cmd}
    if args:
        body["args"] = args
    payload = {
        "objectPath": CDO,
        "functionName": "ExecuteCommandJson",
        "parameters": {"RequestJson": json.dumps(body)}
    }
    r = requests.put(BASE, json=payload, timeout=30)
    raw = r.json().get("ReturnValue", "{}")
    return json.loads(raw) if isinstance(raw, str) else raw


def main():
    # 1. List all available presets
    print("Available presets:")
    presets = call("list_presets")
    if isinstance(presets, dict):
        for p in presets.get("presets", []):
            print(f"  - {p}")

    # 2. Have the engine suggest the best preset for the current project
    print("\nSuggesting preset for current project...")
    suggestion = call("suggest_preset")
    if isinstance(suggestion, dict):
        print(f"  Suggested  : {suggestion.get('suggested_preset', '?')}")
        print(f"  Confidence : {suggestion.get('confidence', 0):.0%}")
        print(f"  Reasoning  : {suggestion.get('reasoning', '')}")

    # 3. Load the Horror built-in preset as a base
    print("\nLoading Horror preset as base...")
    horror = call("load_preset", {"preset_name": "Horror"})
    print(json.dumps(horror, indent=2) if isinstance(horror, dict) else horror)

    # 4. Create and save a custom "AbandonedAsylum" preset
    print("\nSaving custom AbandonedAsylum preset...")
    custom = call("save_preset", {
        "preset_name":                  "AbandonedAsylum",
        "description":                  "1920s Gothic asylum — claustrophobic corridors, high ceilings, decayed grandeur",
        # Phase I — tighter scale than standard Horror
        "standard_door_width_cm":       160.0,
        "standard_ceiling_height_cm":   450.0,   # high nave ceilings
        "player_eye_height_cm":         170.0,
        "max_jump_height_cm":           100.0,
        "min_corridor_width_cm":        120.0,   # claustrophobic corridors
        # Phase II kit
        "preferred_modular_kit_paths":  ["/Game/Gothic_Cathedral/Meshes"],
        "preferred_material_paths":     ["/Game/Gothic_Cathedral/Materials"],
        # Phase III set dressing
        "set_dressing_density":         0.65,    # more props than default Horror
        "enable_vertex_paint_weathering": True,
        # Phase IV lighting
        "ambient_light_color":          {"r": 0.06, "g": 0.05, "b": 0.10, "a": 1.0},
        "ambient_intensity_multiplier": 0.4,
        "enable_god_rays":              True,
        # Phase V living systems
        "enable_ambient_particles":     True,
        "particle_density":             0.5,     # dust + embers
        "enable_ambient_sound":         True,
        # Quality thresholds
        "min_horror_score":             60.0,
        "target_lighting_coverage":     0.55,    # intentionally dim
        "min_actor_count":              30,
        "max_actor_count":              400,
    })
    print(json.dumps(custom, indent=2) if isinstance(custom, dict) else custom)

    # 5. Confirm it now appears in the preset list
    print("\nVerifying preset was saved...")
    updated = call("list_presets")
    if isinstance(updated, dict):
        found = "AbandonedAsylum" in updated.get("presets", [])
        print(f"  AbandonedAsylum in list: {found}")
        print(f"  All presets: {updated.get('presets', [])}")


if __name__ == "__main__":
    main()
