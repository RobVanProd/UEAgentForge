"""
UEAgentForge v0.3.0 — Multi-View Analysis Demo
Demonstrates: get_multi_view_capture, get_level_hierarchy,
              get_deep_properties, get_semantic_env_snapshot

Captures 4 viewport screenshots and performs a complete environment analysis
in a single agent loop — giving the AI a rich, multi-modal view of the scene.
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
    return json.loads(resp.json()["ReturnValue"])


def print_section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


# ── 1. Multi-View Capture (4 angles) ─────────────────────────────────────────
print_section("1. Multi-View Capture — 4 Horror Angles")

ANGLES = ["top", "front", "side", "tension"]
for angle in ANGLES:
    result = cmd("get_multi_view_capture", {"angle": angle, "orbit_radius": 4000})
    if result.get("ok"):
        cam = result.get("camera", {})
        print(f"  [{angle}] camera=({cam.get('x',0):.0f}, {cam.get('y',0):.0f}, {cam.get('z',0):.0f})"
              f"  pitch={cam.get('pitch',0):.0f}°")
        print(f"         {result.get('note', '')[:80]}")
    else:
        print(f"  [{angle}] FAILED: {result.get('error')}")
    time.sleep(0.6)  # allow each screenshot to write before next camera move

print("\n  All 4 screenshots queued. Check Saved/Screenshots/WindowsEditor/ for output.")


# ── 2. Level Hierarchy ────────────────────────────────────────────────────────
print_section("2. Level Hierarchy — Full Outliner Tree")

hierarchy = cmd("get_level_hierarchy")
if hierarchy.get("ok"):
    actors = hierarchy.get("actors", [])
    print(f"  Total actors: {hierarchy['actor_count']}")

    # Group by class.
    from collections import Counter
    class_counts = Counter(a["class"] for a in actors)
    print("  By class:")
    for cls, count in class_counts.most_common(8):
        print(f"    {cls:40s}  ×{count}")

    # Show actors with most components.
    actors_by_comp = sorted(actors, key=lambda a: len(a.get("components", [])), reverse=True)
    print("\n  Most complex actors (by component count):")
    for a in actors_by_comp[:5]:
        comps = [c["class"] for c in a.get("components", [])]
        print(f"    {a['label']:30s}  [{', '.join(comps[:4])}]")
else:
    print(f"  FAILED: {hierarchy.get('error')}")


# ── 3. Deep Properties on specific actor ──────────────────────────────────────
print_section("3. Deep Properties — First PostProcessVolume")

# Find a PostProcessVolume from the hierarchy.
ppv_label = None
for a in hierarchy.get("actors", []):
    if "PostProcess" in a.get("class", ""):
        ppv_label = a["label"]
        break

if ppv_label:
    props = cmd("get_deep_properties", {"label": ppv_label})
    if props.get("ok"):
        print(f"  {ppv_label} ({props['class']}) — {props['property_count']} properties")
        # Show the most interesting post-process properties.
        interesting = ["bEnabled", "BlendRadius", "BlendWeight", "Priority"]
        for key in interesting:
            val = props["properties"].get(key, "—")
            print(f"    {key}: {val}")
    else:
        print(f"  FAILED: {props.get('error')}")
else:
    print("  No PostProcessVolume found in level.")


# ── 4. Semantic Environment Snapshot ─────────────────────────────────────────
print_section("4. Semantic Environment Snapshot — Full Analysis")

snap = cmd("get_semantic_env_snapshot")
if snap.get("ok"):
    lighting = snap.get("lighting", {})
    pp       = snap.get("post_process", {})
    density  = snap.get("density", {})
    bounds   = snap.get("level_bounds", {})

    print(f"  Horror Score:   {snap['horror_score']:.1f}/100  [{snap['horror_rating']}]")
    print(f"\n  Lighting:")
    print(f"    Point lights:     {lighting.get('point_light_count', 0)}")
    print(f"    Avg intensity:    {lighting.get('avg_intensity', 0):.0f} cd")
    print(f"    Darkness score:   {lighting.get('darkness_score', 0):.1f}/100")
    print(f"    Has directional:  {lighting.get('has_directional_light', False)}")
    print(f"    Has sky light:    {lighting.get('has_sky_light', False)}")
    print(f"\n  Post-Process:")
    print(f"    Vignette:         {pp.get('vignette', 0):.2f}")
    print(f"    Bloom:            {pp.get('bloom', 0):.2f}")
    print(f"    Grain:            {pp.get('grain', 0):.2f}")
    print(f"    Exposure bias:    {pp.get('exposure_compensation', 0):.2f}")
    print(f"    CRT blendable:    {pp.get('has_crt_blendable', False)}")
    print(f"    Fog density:      {pp.get('fog_density', 0):.6f}")
    print(f"\n  Density:")
    print(f"    Actors total:     {density.get('actor_count', 0)}")
    print(f"    Density/m²:       {density.get('density_per_m2', 0):.3f}")
    ext = bounds.get("extent", {})
    area = bounds.get("area_m2", 0)
    print(f"    Level area:       {area:.0f} m²")
else:
    print(f"  FAILED: {snap.get('error')}")


print_section("Multi-View Analysis Demo Complete")
print(f"  Horror score: {snap.get('horror_score', '?'):.1f}/100 — {snap.get('horror_rating', '?')}")
print(f"  Actor count:  {hierarchy.get('actor_count', '?')}")
print(f"  Screenshots:  4 queued in Saved/Screenshots/WindowsEditor/")
