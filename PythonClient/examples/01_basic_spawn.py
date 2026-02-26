"""
Example 01: Basic actor spawn and cleanup.
Run from the PythonClient/ directory with the Unreal Editor open.
"""
import sys; sys.path.insert(0, "..")
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient(verify=True)

# Ping to confirm connection
pong = client.ping()
print("Connected:", pong.get("pong"))

# List current actors
actors = client.get_all_level_actors()
print(f"Level has {len(actors)} actors before spawn.")

# Spawn a static mesh actor
result = client.spawn_actor(
    "/Script/Engine.StaticMeshActor",
    x=0, y=0, z=200,
)
print("Spawn result:", result)

if result.ok:
    actors_after = client.get_all_level_actors()
    print(f"Level has {len(actors_after)} actors after spawn.")

    # Clean up
    spawned_label = result.raw.get("spawned_name", "")
    if spawned_label:
        del_result = client.delete_actor(spawned_label)
        print("Delete result:", del_result)
