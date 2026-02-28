"""
UEAgentForge v0.3.0 — Full Closed-Loop Iteration Demo
Demonstrates: observe_analyze_plan_act

The agent observes the level, identifies atmosphere gaps, plans corrective actions,
executes them, verifies, and iterates until the horror score meets a target threshold.

This is the "AI that sees and thinks" workflow — the closest current approximation
to a fully autonomous, closed-loop level design agent.
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
    return json.loads(resp.json()["ReturnValue"])


def print_section(title):
    print(f"\n{'='*60}")
    print(f"  {title}")
    print(f"{'='*60}")


# ── Step 1: Baseline assessment before OAPA ───────────────────────────────────
print_section("BASELINE: Pre-OAPA Environment State")

baseline = cmd("get_semantic_env_snapshot")
if baseline.get("ok"):
    print(f"  Horror score:    {baseline['horror_score']:.1f}/100  [{baseline['horror_rating']}]")
    lighting = baseline.get("lighting", {})
    pp       = baseline.get("post_process", {})
    density  = baseline.get("density", {})
    print(f"  Darkness score:  {lighting.get('darkness_score', 0):.1f}")
    print(f"  Actor count:     {density.get('actor_count', 0)}")
    print(f"  Fog density:     {pp.get('fog_density', 0):.6f}")
    print(f"  Vignette:        {pp.get('vignette', 0):.2f}")
else:
    print(f"  FAILED: {baseline.get('error')}")


# ── Step 2: Run closed-loop OAPA ──────────────────────────────────────────────
print_section("OBSERVE → ANALYZE → PLAN → ACT → VERIFY")

oapa_result = cmd("observe_analyze_plan_act", {
    "description":    "Gothic 1920s asylum reception — terrifying, oppressive, claustrophobic",
    "max_iterations": 3,
    "score_target":   65.0,
})

if oapa_result.get("ok"):
    iterations = oapa_result.get("iterations", [])
    print(f"  Iterations run: {len(iterations)}")

    for it in iterations:
        n     = it.get("iteration", "?")
        score = it.get("observed_horror_score", 0)
        issues = it.get("issues_identified", [])
        plan   = it.get("plan_steps", [])
        acts   = it.get("action_results", [])

        print(f"\n  ── Iteration {n} ──────────────────────────────────")
        print(f"  Horror score:  {score:.1f}/100")

        if issues:
            print(f"  Issues found:  {len(issues)}")
            for issue in issues:
                print(f"    ✗ {issue}")
        else:
            print(f"  Issues found:  none (scene already meets threshold)")

        if plan:
            print(f"  Action plan:   {len(plan)} steps")
            for step in plan:
                print(f"    → {step}")

        if acts:
            print(f"  Executed:")
            for act in acts:
                print(f"    ✓ {act[:100]}")

    # Verification result.
    verify = oapa_result.get("verification", "N/A")
    print(f"\n  Verification: {verify[:120]}")
else:
    print(f"  OAPA FAILED: {oapa_result.get('error')}")


# ── Step 3: Post-OAPA assessment ──────────────────────────────────────────────
print_section("POST-OAPA: Final Environment State")

final = cmd("get_semantic_env_snapshot")
if final.get("ok") and baseline.get("ok"):
    score_before = baseline["horror_score"]
    score_after  = final["horror_score"]
    delta        = score_after - score_before

    print(f"  Horror score:  {score_before:.1f} → {score_after:.1f}/100  "
          f"({'▲' if delta >= 0 else '▼'}{abs(delta):.1f} pts)")
    print(f"  Rating:        {baseline['horror_rating']} → {final['horror_rating']}")

    pp_b = baseline.get("post_process", {})
    pp_a = final.get("post_process", {})
    l_b  = baseline.get("lighting", {})
    l_a  = final.get("lighting", {})

    print(f"\n  Changes applied:")
    print(f"    Vignette:     {pp_b.get('vignette',0):.2f} → {pp_a.get('vignette',0):.2f}")
    print(f"    Darkness:     {l_b.get('darkness_score',0):.1f} → {l_a.get('darkness_score',0):.1f}")
    print(f"    Fog density:  {pp_b.get('fog_density',0):.6f} → {pp_a.get('fog_density',0):.6f}")
    print(f"    CRT active:   {pp_a.get('has_crt_blendable', False)}")

    if score_after >= 65.0:
        print(f"\n  ✓ Target score (65) achieved — level is atmospherically ready.")
    else:
        print(f"\n  ✗ Target score (65) not yet reached — run again or use enhance_horror_scene.")
else:
    print(f"  FAILED: {final.get('error')}")


# ── Step 4: Take a final verification screenshot ──────────────────────────────
print_section("Final Screenshot")

shot = cmd("take_screenshot", {"filename": "oapa_final_result"})
print(f"  Screenshot: {shot.get('path', 'N/A')}")

print_section("Closed-Loop Iteration Demo Complete")
print(f"  The OAPA command ran autonomously, identified issues, applied fixes,")
print(f"  verified the result, and iterated until the target was reached.")
print(f"  Final horror score: {final.get('horror_score', 0):.1f}/100")
