"""
UEAgentForge v0.4.0 — Example: Phase I Blockout Only
=====================================================
Creates a quick spatial blockout from a mission description.
Use this to rapidly prototype level layouts before committing
to modular kit pieces or set dressing.
"""
import json, requests

BASE = "http://127.0.0.1:30010/remote/object/call"
CDO  = "/Script/UEAgentForge.Default__AgentForgeLibrary"

def call(cmd, **kwargs):
    payload = {
        "objectPath": CDO,
        "functionName": "ExecuteCommandJson",
        "parameters": {"RequestJson": json.dumps({"cmd": cmd, "args": kwargs} if kwargs else {"cmd": cmd})}
    }
    r = requests.put(BASE, json=payload, timeout=60)
    return json.loads(json.loads(r.json()["ReturnValue"]) if isinstance(r.json().get("ReturnValue"), str)
                      else r.text).get("ReturnValue", r.json().get("ReturnValue", {}))

def main():
    # 1. Load the Horror preset so Phase I uses correct scale metrics
    print("Loading Horror preset...")
    preset = call("load_preset", preset_name="Horror")
    print(json.dumps(json.loads(preset), indent=2) if isinstance(preset, str) else preset)

    # 2. Run Phase I blockout — generates bubble-diagram rooms + corridors
    print("\nCreating blockout level...")
    result = call(
        "create_blockout_level",
        mission=(
            "Three-room asylum floor: a narrow intake corridor leads into "
            "a vaulted patient ward, then a locked examination room at the far end."
        ),
        preset="Horror",
        room_count=3,
        grid_size=400,
    )
    data = json.loads(result) if isinstance(result, str) else result
    print(json.dumps(data, indent=2))

    if data.get("ok"):
        print(f"\nBlockout complete!")
        print(f"  Rooms placed    : {data.get('rooms_placed', 0)}")
        print(f"  Corridors placed: {data.get('corridors_placed', 0)}")
        print(f"  Total area      : {data.get('total_area_sqm', 0):.0f} sqm")
        print(f"  NavMesh placed  : {data.get('navmesh_placed', False)}")
        print(f"  PlayerStart     : {data.get('player_start_placed', False)}")
        print("\nNext step: run convert_to_whitebox_modular to replace blockout with real meshes.")
    else:
        print(f"ERROR: {data.get('error', 'unknown')}")

if __name__ == "__main__":
    main()
