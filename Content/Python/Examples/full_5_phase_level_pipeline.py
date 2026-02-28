"""
UEAgentForge v0.4.0 — Example: Full 5-Phase AAA Level Pipeline
==============================================================
Runs all five pipeline phases in sequence and logs quality scores.

Pipeline:
  Phase I   — RLD / Blockout         : primitive spatial layout
  Phase II  — Architectural Whitebox  : modular kit replacement
  Phase III — Beauty / Set Dressing   : props + micro-story arrangement
  Phase IV  — Lighting & Atmosphere   : professional lighting + PP + fog
  Phase V   — Living Systems          : particles + audio emitters
  QA Loop   — iterates IV + V until quality_threshold is met

Usage:
  1. Open your target level in the Unreal Editor
  2. Run this script: python full_5_phase_level_pipeline.py
  3. Inspect the generated level and quality report
"""
import json, requests, time

BASE = "http://127.0.0.1:30010/remote/object/call"
CDO  = "/Script/UEAgentForge.Default__AgentForgeLibrary"


def call(cmd, args=None):
    body = {"cmd": cmd}
    if args:
        body["args"] = args
    payload = {
        "objectPath": CDO,
        "functionName": "ExecuteCommandJson",
        "parameters": {"RequestJson": json.dumps(body)}
    }
    r = requests.put(BASE, json=payload, timeout=120)
    raw = r.json().get("ReturnValue", "{}")
    return json.loads(raw) if isinstance(raw, str) else raw


def phase(name, cmd, args=None):
    print(f"\n{'='*60}")
    print(f"  {name}")
    print(f"{'='*60}")
    t0 = time.time()
    result = call(cmd, args)
    elapsed = time.time() - t0
    print(f"  Completed in {elapsed:.1f}s")
    if isinstance(result, dict):
        for k, v in result.items():
            if k not in ("ok", "error"):
                print(f"    {k}: {v}")
    return result


def main():
    mission = (
        "Blackmoor Asylum reception — 1920s Gothic psychiatric facility entrance. "
        "Contains a front desk area, two confessionals (player hiding spots), "
        "ornate pew rows, and a chandelier-lit nave with stone pillars. "
        "The Warden patrols between the entrance and the far confessional."
    )

    # ── Orchestrate all 5 phases via the master command ──────────────────────
    print("Starting UEAgentForge v0.4.0 — Full 5-Phase Level Pipeline")
    print(f"Mission: {mission[:80]}...")

    result = call("generate_full_quality_level", {
        "mission":            mission,
        "preset":             "Horror",
        "max_iterations":     3,
        "quality_threshold":  0.70,
    })

    # ── Print summary ─────────────────────────────────────────────────────────
    print("\n" + "="*60)
    print("  PIPELINE COMPLETE")
    print("="*60)
    if isinstance(result, dict) and result.get("ok"):
        print(f"  Final quality score : {result.get('final_quality_score', 0):.2f}")
        print(f"  Iterations taken    : {result.get('iterations', 1)}")
        print(f"  Level saved         : {result.get('level_saved', False)}")
        print(f"  Screenshot          : {result.get('screenshot_path', 'none')}")

        for ph in ("phase1", "phase2", "phase3", "phase4", "phase5"):
            d = result.get(ph, {})
            if isinstance(d, dict) and d:
                ok = d.get("ok", "?")
                print(f"  {ph}: ok={ok}")

        qr = result.get("quality_report", {})
        if isinstance(qr, dict):
            print(f"\n  Quality Report:")
            for k, v in qr.items():
                print(f"    {k}: {v}")
    else:
        err = result.get("error", "unknown") if isinstance(result, dict) else str(result)
        print(f"  ERROR: {err}")


if __name__ == "__main__":
    main()
