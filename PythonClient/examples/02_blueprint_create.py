"""
Example 02: Create a Blueprint, set a CDO property, compile, and verify.
"""
import sys; sys.path.insert(0, "..")
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient(verify=True)

# 1. Run verification first
report = client.run_verification(phase_mask=1)  # PreFlight only
print(report.summary())
if not report.all_passed:
    print("Verification failed — aborting.")
    raise SystemExit(1)

# 2. Create Blueprint
bp = client.create_blueprint(
    name="BP_AgentForge_Test",
    parent_class="/Script/Engine.Actor",
    output_path="/Game/AgentForgeTest",
)
print("create_blueprint:", bp)
if not bp.ok:
    raise SystemExit(f"Failed: {bp.error}")

bp_path = bp.raw.get("package") + ".BP_AgentForge_Test"

# 3. Compile
compile_result = client.compile_blueprint(bp_path)
print("compile:", compile_result)

# 4. Snapshot
snap = client.create_snapshot("after_bp_create")
print("snapshot:", snap)
