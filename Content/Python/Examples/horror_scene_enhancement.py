"""
UEAgentForge v0.3.0 — Horror Scene Enhancement Demo
Demonstrates: apply_genre_rules, place_asset_thematically,
              refine_level_section, enhance_horror_scene

Two approaches:
  A) enhance_horror_scene   — single command, full pipeline
  B) Granular step-by-step  — full control over each enhancement step
"""
import json, requests

ENDPOINT = "http://127.0.0.1:30010/remote/object/call"
CDO      = "/Script/UEAgentForge.Default__AgentForgeLibrary"


def cmd(command_name, args=None):
    payload = {"cmd": command_name}
    if args:
        payload["args"] = args
    resp = requests.put(ENDPOINT, json={
        "objectPath":   CDO,
        "functionName": "ExecuteCommandJson",
        "parameters":   {"RequestJson": json.dumps(payload)},
    })
    return json.loads(resp.json()["ReturnValue"])


def print_section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


# ═══════════════════════════════════════════════════════════════════════════════
#  APPROACH A: One-shot enhance_horror_scene
# ═══════════════════════════════════════════════════════════════════════════════
print_section("APPROACH A: enhance_horror_scene (One Shot)")

snap_before = cmd("get_semantic_env_snapshot")
score_before = snap_before.get("horror_score", 0) if snap_before.get("ok") else 0
print(f"  Horror score BEFORE: {score_before:.1f}/100")

result = cmd("enhance_horror_scene", {
    "description": "Gothic asylum corridor — deep shadow, oppressive atmosphere, unseen threat",
    "intensity":   0.85,
    "prop_count":  5,
})

if result.get("ok"):
    print(f"  Horror score AFTER:  {result.get('final_horror_score', 0):.1f}/100")
    print(f"  Screenshot: {result.get('screenshot_path', 'N/A')}")
    print("  Actions taken:")
    for action in result.get("actions_taken", []):
        print(f"    • {action}")
else:
    print(f"  FAILED: {result.get('error')}")


# ═══════════════════════════════════════════════════════════════════════════════
#  APPROACH B: Step-by-step granular control
# ═══════════════════════════════════════════════════════════════════════════════
print_section("APPROACH B: Granular Step-by-Step Enhancement")

# Step 1: Apply horror genre rules at 70% intensity.
print("\n[1/4] Applying horror genre atmosphere (intensity=0.70)...")
genre_result = cmd("apply_genre_rules", {
    "genre":     "horror",
    "intensity": 0.70,
})
if genre_result.get("ok"):
    print(f"  Lights modified:  {genre_result['lights_modified']}")
    print(f"  PP modified:      {genre_result['pp_modified']}")
    for change in genre_result.get("changes_applied", []):
        print(f"    • {change}")
else:
    print(f"  FAILED: {genre_result.get('error')}")


# Step 2: Place props in the reception area (chairs, debris, etc.).
print("\n[2/4] Placing horror props thematically (dark + occluded corners)...")
place_result = cmd("place_asset_thematically", {
    "class_path": "/Script/Engine.StaticMeshActor",
    "count":       4,
    "label_prefix": "AtmosProp",
    "theme_rules": {
        "prefer_dark":     True,
        "prefer_corners":  True,
        "prefer_occluded": True,
        "min_spacing":     500.0,
    },
    "reference_area": {"x": 0, "y": 0, "z": 0, "radius": 4000},
})
if place_result.get("ok"):
    print(f"  Placed: {place_result['placed_count']} actors")
    print(f"  Reasoning: {place_result['placement_reasoning']}")
    for a in place_result.get("actors", []):
        loc = a["location"]
        print(f"    • {a['label']:25s}  ({loc['x']:.0f}, {loc['y']:.0f}, {loc['z']:.0f})")
else:
    print(f"  FAILED: {place_result.get('error')}")


# Step 3: Refine a specific area (e.g., the far corner of the room).
print("\n[3/4] Refining north wing atmosphere (max 2 iterations)...")
refine_result = cmd("refine_level_section", {
    "description":    "Dense gothic horror props, dark and claustrophobic",
    "target_area":    {"x": 500, "y": 1000, "z": 0, "radius": 1500},
    "max_iterations": 2,
    "class_path":     "/Script/Engine.StaticMeshActor",
})
if refine_result.get("ok"):
    print(f"  Iterations run:      {refine_result['iterations_run']}")
    print(f"  Final density:       {refine_result['final_density_score']:.3f} props/m²")
    for action in refine_result.get("actions_taken", []):
        print(f"    • {action}")
else:
    print(f"  FAILED: {refine_result.get('error')}")


# Step 4: Final snapshot to verify improvement.
print("\n[4/4] Final environment assessment...")
snap_after = cmd("get_semantic_env_snapshot")
if snap_after.get("ok"):
    score_after = snap_after.get("horror_score", 0)
    rating = snap_after.get("horror_rating", "?")
    print(f"  Horror score: {score_before:.1f} → {score_after:.1f}/100  [{rating}]")

    pp = snap_after.get("post_process", {})
    lighting = snap_after.get("lighting", {})
    print(f"  Vignette:     {pp.get('vignette', 0):.2f}")
    print(f"  Darkness:     {lighting.get('darkness_score', 0):.1f}/100")
    print(f"  Fog density:  {pp.get('fog_density', 0):.6f}")
else:
    print(f"  FAILED: {snap_after.get('error')}")


print_section("Horror Scene Enhancement Complete")
print(f"  Score improvement: {score_before:.1f} → {snap_after.get('horror_score', 0):.1f}")
