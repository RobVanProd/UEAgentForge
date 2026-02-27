"""
UEAgentForge v0.2.0 — FAB Integration Demo
Demonstrates: search_fab_assets, download_fab_asset (stub), import_local_asset

IMPORTANT LIMITATION:
  Fab.com does NOT have a public download API. The download_fab_asset command
  returns guidance on how to manually download via the Epic Games Launcher or
  the Fab plugin inside the Unreal Editor. Once downloaded, import_local_asset
  can import the files into your project.

Full workflow:
  1. search_fab_assets → find free gothic ruins assets
  2. Open Fab.com URL manually → download via EGL or UE Fab plugin
  3. import_local_asset → import downloaded files into /Game/FabImports/
  4. spawn_actor_at_surface → place imported mesh in the level
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


# ── 1. Search for free gothic ruins assets on Fab.com ────────────────────────
print("=== Searching Fab.com for Free Gothic Ruins ===")
search_result = cmd("search_fab_assets", {
    "query":       "gothic ruins modular",
    "max_results": 10,
    "free_only":   True,
})

if search_result.get("ok"):
    print(f"Found {search_result['count']} results for '{search_result['query']}':")
    for asset in search_result.get("results", []):
        price = "FREE" if asset["price"] == 0 else f"${asset['price']:.2f}"
        print(f"  [{price}] {asset['title']}")
        print(f"         URL: {asset['url']}")
        print(f"         Type: {asset['type']}")
    if search_result.get("note"):
        print(f"\nNOTE: {search_result['note']}")
else:
    print(f"Search failed: {search_result.get('error')}")

# ── 2. Demonstrate download stub (honest limitation) ──────────────────────────
print("\n=== Download Asset (Stub — No Public API) ===")
download_result = cmd("download_fab_asset", {"asset_id": "example-id-123"})
print("Result:", download_result.get("error"))
print("Workaround:", download_result.get("workaround"))

# ── 3. Import a local asset (e.g., previously downloaded via EGL) ────────────
print("\n=== Import Local Asset ===")
# Example: import an FBX file you've already downloaded
local_fbx = r"C:\Users\Rob\Downloads\GothicPillar.fbx"
import os
if os.path.exists(local_fbx):
    import_result = cmd("import_local_asset", {
        "file_path":        local_fbx,
        "destination_path": "/Game/FabImports/Gothic",
    })
    if import_result.get("ok"):
        print(f"Imported: {import_result['asset_path']}")
        print(f"Type: {import_result['type']}")

        # ── 4. Place the imported mesh at surface ─────────────────────────────
        print("\n=== Placing Imported Asset at Surface ===")
        place_result = cmd("spawn_actor_at_surface", {
            "class_path":      import_result["asset_path"],
            "origin":          {"x": 0, "y": 0, "z": 2000},
            "direction":       {"x": 0, "y": 0, "z": -1},
            "max_distance":    5000,
            "align_to_normal": True,
            "label":           "FabGothicPillar_01",
        })
        if place_result.get("ok"):
            loc = place_result["location"]
            print(f"Placed at ({loc['x']:.0f}, {loc['y']:.0f}, {loc['z']:.0f})")
        else:
            print(f"Placement failed: {place_result.get('error')}")
    else:
        print(f"Import failed: {import_result.get('error')}")
else:
    print(f"(FBX file not found at {local_fbx} — download from Fab.com first)")

# ── 5. List all imported assets ───────────────────────────────────────────────
print("\n=== Imported Assets in /Game/FabImports/ ===")
list_result = cmd("list_imported_assets", {"content_path": "/Game/FabImports"})
if list_result.get("ok"):
    print(f"Found {list_result['count']} assets:")
    for asset in list_result.get("assets", [])[:10]:
        print(f"  {asset['asset_name']} ({asset['type']})")
else:
    print(f"List failed: {list_result.get('error')}")

print("\n=== FAB Demo Complete ===")
