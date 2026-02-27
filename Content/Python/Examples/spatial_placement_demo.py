"""
UEAgentForge v0.2.0 — Spatial Intelligence Layer Demo
Demonstrates: spawn_actor_at_surface, align_actors_to_surface,
              get_surface_normal_at, analyze_level_composition, get_actors_in_radius
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
    result = json.loads(resp.json()["ReturnValue"])
    return result


# ── 1. Analyze current level composition ─────────────────────────────────────
print("=== Level Composition Analysis ===")
comp = cmd("analyze_level_composition")
print(f"Total actors: {comp['actor_count']}")
print(f"  Static meshes: {comp['static_count']}")
print(f"  Lights: {comp['light_count']}")
print(f"  AI/Characters: {comp['ai_count']}")
print(f"Density score: {comp['density_score']:.2f}")
print("Recommendations:")
for rec in comp.get("recommendations", []):
    print(f"  • {rec}")

# ── 2. Get surface normal at center of level ──────────────────────────────────
print("\n=== Surface Normal at Origin ===")
normal = cmd("get_surface_normal_at", {"x": 0, "y": 0, "z": 1000})
if normal.get("ok"):
    loc = normal["location"]
    nrm = normal["normal"]
    print(f"Hit location: ({loc['x']:.0f}, {loc['y']:.0f}, {loc['z']:.0f})")
    print(f"Surface normal: ({nrm['x']:.3f}, {nrm['y']:.3f}, {nrm['z']:.3f})")
    print(f"Hit actor: {normal['hit_actor']}")

# ── 3. Spawn actor at surface ─────────────────────────────────────────────────
print("\n=== Spawn Actor at Surface ===")
spawn_result = cmd("spawn_actor_at_surface", {
    "class_path":      "/Script/Engine.StaticMeshActor",
    "origin":          {"x": 500, "y": 0, "z": 2000},
    "direction":       {"x": 0, "y": 0, "z": -1},
    "max_distance":    5000,
    "align_to_normal": True,
    "label":           "SpatialDemo_Prop",
})
if spawn_result.get("ok"):
    print(f"Spawned: {spawn_result['actor_label']}")
    loc = spawn_result["location"]
    print(f"Location: ({loc['x']:.0f}, {loc['y']:.0f}, {loc['z']:.0f})")
    print(f"Normal: {spawn_result['normal']}")
else:
    print(f"Spawn failed: {spawn_result.get('error')}")

# ── 4. Get actors within 1000 cm of origin ────────────────────────────────────
print("\n=== Actors Within 1000 cm of Origin ===")
nearby = cmd("get_actors_in_radius", {"x": 0, "y": 0, "z": 0, "radius": 1000})
print(f"Found {nearby['count']} actors:")
for actor in nearby.get("actors", [])[:5]:
    print(f"  {actor['label']} ({actor['class']}) — {actor['distance']:.0f} cm")

# ── 5. Align a batch of actors to surface ─────────────────────────────────────
print("\n=== Align Actors to Surface ===")
# (assumes some actors exist in the level)
align_result = cmd("align_actors_to_surface", {
    "actor_labels":      ["SpatialDemo_Prop"],
    "down_trace_extent": 2000,
})
print(f"Aligned {align_result['aligned_count']} actors")
for r in align_result.get("results", []):
    if r["ok"]:
        print(f"  {r['label']}: Z {r['old_z']:.0f} → {r['new_z']:.0f}")
    else:
        print(f"  {r['label']}: {r.get('error')}")

print("\n=== Spatial Demo Complete ===")
