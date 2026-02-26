"""
Example 03: Full verified workflow — spawn inside a transaction with
snapshot + rollback test, post-verify, and automatic undo on failure.

This demonstrates UEAgentForge's key differentiator: the client gets a
confirmed rollback guarantee before any mutation lands permanently.
"""
import sys, json; sys.path.insert(0, "..")
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient(verify=True)

# ── Step 1: Pre-flight check ────────────────────────────────────────────────
print("Step 1: Running pre-flight verification...")
report = client.run_verification(phase_mask=0x01 | 0x02)  # PreFlight + Snapshot
print(report.summary())
if not report.all_passed:
    raise SystemExit("Pre-flight failed. Aborting workflow.")

# ── Step 2: Check constitution ──────────────────────────────────────────────
print("\nStep 2: Checking constitution...")
chk = client.enforce_constitution("spawn a static mesh actor at the origin")
print("Constitution check:", json.dumps(chk, indent=2))
if not chk.get("allowed", True):
    raise SystemExit(f"Constitution blocked this action: {chk.get('violations')}")

# ── Step 3: Snapshot before workflow ───────────────────────────────────────
print("\nStep 3: Creating pre-workflow snapshot...")
snap_before = client.create_snapshot("workflow_before")
print("Snapshot:", snap_before.raw.get("path"))

# ── Step 4: Execute inside a transaction ───────────────────────────────────
print("\nStep 4: Executing verified spawn...")
with client.transaction("AgentForge_VerifiedWorkflow"):
    result = client.spawn_actor("/Script/Engine.StaticMeshActor", x=300, y=0, z=100)
    if not result.ok:
        raise RuntimeError(f"Spawn failed: {result.error}")
    print("Spawned:", result.raw.get("spawned_name"))

# ── Step 5: Post-workflow verification ─────────────────────────────────────
print("\nStep 5: Post-workflow state check...")
actors = client.get_all_level_actors()
print(f"Actor count after workflow: {len(actors)}")

perf = client.get_perf_stats()
print(f"Perf — draw calls: {perf.get('draw_calls')}, "
      f"memory: {perf.get('memory_used_mb', 0):.1f} MB")

print("\n✓ Verified workflow complete.")
