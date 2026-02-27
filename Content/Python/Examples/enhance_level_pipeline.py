"""
UEAgentForge v0.2.0 — Full Level Enhancement Pipeline
The "game-changer" command: natural language → complete, verified level enhancement.

This script demonstrates the unified orchestration approach:
  1. enhance_current_level (single command)
  2. Detailed multi-step workflow using all v0.2.0 systems

Usage:
  python enhance_level_pipeline.py
  # or run via UEAgentForge execute_python command
"""
import json, requests, time

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
    result = json.loads(resp.json()["ReturnValue"])
    return result


def print_section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


# ═══════════════════════════════════════════════════════════════════════════════
#  APPROACH A: Single-command enhancement (simplest)
# ═══════════════════════════════════════════════════════════════════════════════
print_section("APPROACH A: enhance_current_level (Single Command)")

result = cmd("enhance_current_level", {
    "description": "Add gothic atmosphere to the reception area — "
                   "check composition, snapshot, and verify all blueprints compile."
})

if result.get("ok"):
    print("✓ Enhancement complete!")
    print("Actions taken:")
    for action in result.get("actions_taken", []):
        print(f"  • {action}")
    print(f"\nComposition snapshot: {result.get('snapshot_path', 'N/A')}")
    print(f"Screenshot: {result.get('screenshot_path', 'N/A')}")
else:
    print(f"✗ Enhancement failed: {result.get('error')}")


# ═══════════════════════════════════════════════════════════════════════════════
#  APPROACH B: Multi-step pipeline (full control)
# ═══════════════════════════════════════════════════════════════════════════════
print_section("APPROACH B: Full Pipeline (Multi-Step)")

# Step 1: Create snapshot before any changes
print("\n[1/6] Creating pre-enhancement snapshot...")
snap_result = cmd("create_snapshot", {"snapshot_name": "enhance_pipeline_pre"})
print(f"  → Snapshot: {snap_result.get('path', 'N/A')} ({snap_result.get('actor_count', 0)} actors)")

# Step 2: Analyze level composition
print("\n[2/6] Analyzing level composition...")
comp = cmd("analyze_level_composition")
print(f"  → {comp['actor_count']} actors | Density: {comp['density_score']:.2f}")
for rec in comp.get("recommendations", [])[:3]:
    print(f"  Recommendation: {rec}")

# Step 3: Find available surfaces for placement
print("\n[3/6] Sampling surface normals at key locations...")
sample_points = [
    (0, 0), (500, 0), (-500, 0), (0, 500), (0, -500),
]
valid_surfaces = []
for (x, y) in sample_points:
    normal = cmd("get_surface_normal_at", {"x": x, "y": y, "z": 1000})
    if normal.get("ok"):
        valid_surfaces.append((x, y, normal["location"]["z"]))
        nrm = normal["normal"]
        print(f"  ({x:+5.0f}, {y:+5.0f}) → Z={normal['location']['z']:.0f} cm  normal=({nrm['x']:.2f},{nrm['y']:.2f},{nrm['z']:.2f})")

# Step 4: Spawn props at valid surface points
print(f"\n[4/6] Spawning props at {len(valid_surfaces)} surface points...")
spawned = []
for i, (x, y, z) in enumerate(valid_surfaces[:3]):  # limit to 3 for demo
    result = cmd("spawn_actor_at_surface", {
        "class_path":      "/Script/Engine.StaticMeshActor",
        "origin":          {"x": x, "y": y, "z": z + 200},
        "direction":       {"x": 0, "y": 0, "z": -1},
        "max_distance":    500,
        "align_to_normal": True,
        "label":           f"EnhanceProp_{i+1:02d}",
    })
    if result.get("ok"):
        spawned.append(result["actor_label"])
        loc = result["location"]
        print(f"  ✓ Spawned {result['actor_label']} at ({loc['x']:.0f}, {loc['y']:.0f}, {loc['z']:.0f})")
    else:
        print(f"  ✗ Spawn failed: {result.get('error')}")

# Step 5: Align all spawned props to surface
if spawned:
    print(f"\n[5/6] Aligning {len(spawned)} props to surface...")
    align = cmd("align_actors_to_surface", {
        "actor_labels":      spawned,
        "down_trace_extent": 1000,
    })
    print(f"  → Aligned {align['aligned_count']}/{len(spawned)} actors")

# Step 6: Run verification
print("\n[6/6] Running verification protocol...")
verify = cmd("run_verification", {"phase_mask": 13})  # PreFlight + PostVerify + BuildCheck
print(f"  Phases passed: {verify.get('phases_passed', '?')}/{verify.get('phases_run', '?')}")
for detail in verify.get("details", []):
    status = "✓" if detail.get("passed") else "✗"
    print(f"  {status} {detail.get('phase')}: {detail.get('detail', '')[:80]}")

# Final: Take screenshot
print("\n[Final] Requesting screenshot...")
screenshot = cmd("take_screenshot", {"filename": "enhance_pipeline_result"})
print(f"  → {screenshot.get('path', 'N/A')}")

print_section("Enhancement Pipeline Complete")
print(f"Spawned {len(spawned)} props | All surfaces aligned | Verification done")
