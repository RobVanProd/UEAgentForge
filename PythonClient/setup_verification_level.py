"""
M_AgentVerification Level Setup + 4-Phase Demo
===============================================
Creates the standard AgentForge verification test level and runs the full
4-phase verification protocol as the first public demo.

This script:
1. Asserts the expected level is open (or reports current level)
2. Runs Phase 1 verification only (PreFlight — fast constitution check)
3. Sets up standard test geometry via setup_test_level
4. Takes a baseline snapshot
5. Runs the full 4-phase verification protocol
6. Spawns + deletes a probe actor to exercise the complete transaction pipeline
7. Takes a screenshot
8. Prints a full report

Usage:
    python setup_verification_level.py [--host 127.0.0.1] [--port 30010]

Requirements:
    - Unreal Editor open with Remote Control API enabled
    - UEAgentForge plugin loaded
    - A level open (any level; M_AgentVerification recommended)
    - pip install requests
"""

import sys
import json
import argparse
import time

sys.path.insert(0, ".")  # run from PythonClient/ directory

from ueagentforge_client import AgentForgeClient, VerificationReport

# ─── CLI args ──────────────────────────────────────────────────────────────
parser = argparse.ArgumentParser(description="UEAgentForge verification level setup")
parser.add_argument("--host", default="127.0.0.1")
parser.add_argument("--port", default=30010, type=int)
args = parser.parse_args()

# ─── Connect ───────────────────────────────────────────────────────────────
print("=" * 60)
print("  UEAgentForge v0.1.0 — M_AgentVerification Level Demo")
print("=" * 60)

client = AgentForgeClient(host=args.host, port=args.port, verify=True, verbose=False)

try:
    pong = client.ping()
except RuntimeError as e:
    print(f"\n[FAIL] Cannot connect: {e}")
    sys.exit(1)

print(f"\n[OK] Connected to UEAgentForge {pong.get('version', '?')}")
print(f"     Constitution loaded: {pong.get('constitution_loaded', False)}")
print(f"     Constitution rules:  {pong.get('constitution_rules', 0)}")

# ─── Step 1: Current level info ────────────────────────────────────────────
print("\n[Step 1] Checking current level...")
level_info = client.get_current_level()
print(f"         Level: {level_info.get('package_path', 'unknown')}")
actors = client.get_all_level_actors()
print(f"         Actors: {len(actors)}")

# ─── Step 2: Constitution check ────────────────────────────────────────────
print("\n[Step 2] Constitution enforcement test...")
test_cases = [
    ("spawn a static mesh actor at origin",         True),
    ("edit Oceanology plugin source files",          False),
    ("modify third-party marketplace plugin source", False),
    ("create a blueprint for the test level",        True),
]

all_constitution_passed = True
for action, expect_allowed in test_cases:
    result = client.enforce_constitution(action)
    allowed = result.get("allowed", True)
    status = "[OK]" if allowed == expect_allowed else "[FAIL]"
    if allowed != expect_allowed:
        all_constitution_passed = False
    violations = result.get("violations", [])
    print(f"         {status} '{action[:45]}...' → allowed={allowed}"
          if len(action) > 45 else
          f"         {status} '{action}' → allowed={allowed}")
    if violations:
        print(f"              Violations: {violations[0][:80]}")

if all_constitution_passed:
    print("         Constitution enforcement: ALL TESTS PASSED")
else:
    print("         Constitution enforcement: SOME TESTS FAILED (check rules)")

# ─── Step 3: Setup test geometry ───────────────────────────────────────────
print("\n[Step 3] Setting up verification geometry...")
setup_result = client.setup_test_level(floor_size=10000.0)
if setup_result.ok:
    test_actors = setup_result.raw.get("test_actors", [])
    print(f"         Spawned {len(test_actors)} test actors: {', '.join(test_actors)}")
else:
    print(f"         [WARN] setup_test_level: {setup_result.error}")

# ─── Step 4: Baseline snapshot ─────────────────────────────────────────────
print("\n[Step 4] Creating baseline snapshot...")
snap = client.create_snapshot("M_AgentVerification_baseline")
if snap.ok:
    print(f"         Snapshot: {snap.raw.get('path', '').split('/')[-1]}")
    print(f"         Actors captured: {snap.raw.get('actor_count', 0)}")
else:
    print(f"         [WARN] Snapshot failed: {snap.error}")

# ─── Step 5: Full 4-phase verification ─────────────────────────────────────
print("\n[Step 5] Running 4-phase verification protocol (phase_mask=15)...")
report = client.run_verification(phase_mask=15)
print()
print(report.summary())

if not report.all_passed:
    print("\n[FAIL] Verification failed. Check Output Log for details.")
    # Don't exit — continue with demo

# ─── Step 6: Live transaction pipeline test ────────────────────────────────
print("\n[Step 6] Live transaction pipeline test (spawn → verify → delete)...")

actors_before = client.get_all_level_actors()
count_before = len(actors_before)

# Spawn a probe actor
print(f"         Actor count before: {count_before}")
spawn_result = client.spawn_actor("/Script/Engine.StaticMeshActor", x=0, y=0, z=500)

if spawn_result.ok:
    spawned_name = spawn_result.raw.get("spawned_name", "")
    actors_after_spawn = client.get_all_level_actors()
    print(f"         Spawned: {spawned_name} (count now: {len(actors_after_spawn)})")

    # Wait a tick
    time.sleep(0.5)

    # Delete it
    if spawned_name:
        del_result = client.delete_actor(spawned_name)
        actors_after_del = client.get_all_level_actors()
        print(f"         Deleted: {spawned_name} (count now: {len(actors_after_del)})")

        if len(actors_after_del) == count_before:
            print("         [OK] Transaction pipeline: spawn + delete verified")
        else:
            print(f"         [WARN] Count mismatch: expected {count_before}, got {len(actors_after_del)}")
    else:
        print("         [WARN] Could not get spawned actor name for cleanup")
else:
    print(f"         [WARN] Spawn failed: {spawn_result.error}")

# ─── Step 7: Performance snapshot ──────────────────────────────────────────
print("\n[Step 7] Performance stats...")
perf = client.get_perf_stats()
print(f"         Actor count:    {perf.get('actor_count', '?')}")
print(f"         Draw calls:     {perf.get('draw_calls', '?')}")
print(f"         Memory used:    {perf.get('memory_used_mb', 0):.1f} MB")
print(f"         GPU frame:      {perf.get('gpu_ms', 0):.1f} ms")

# ─── Step 8: Screenshot ────────────────────────────────────────────────────
print("\n[Step 8] Taking verification screenshot...")
shot = client.take_screenshot("M_AgentVerification_demo")
if shot.ok:
    print(f"         Screenshot queued: {shot.raw.get('path', '').split('/')[-1]}")
    print(f"         Saved to: Saved/AgentForgeScreenshots/")
else:
    print(f"         [WARN] Screenshot: {shot.error}")

# ─── Final report ──────────────────────────────────────────────────────────
print("\n" + "=" * 60)
forge_status = client.get_forge_status()
print("  UEAgentForge Status:")
print(f"    Version:              {forge_status.get('version', '?')}")
print(f"    Constitution loaded:  {forge_status.get('constitution_loaded', False)}")
print(f"    Rules enforced:       {forge_status.get('constitution_rules_loaded', 0)}")
print(f"    Constitution path:    {forge_status.get('constitution_path', 'none')}")
print()
print(f"  4-Phase Verification:  {'PASSED' if report.all_passed else 'CHECK REQUIRED'}")
print(f"  Constitution tests:    {'PASSED' if all_constitution_passed else 'CHECK RULES'}")
print()
print("  M_AgentVerification level setup COMPLETE.")
print("  This is the first fully AI-driven UE5 level verification demo.")
print("=" * 60)
