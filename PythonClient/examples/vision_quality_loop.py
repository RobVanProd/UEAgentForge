"""
Example: score the current editor scene through the multimodal pipeline and,
if requested, run one OAPA refinement step.

Set one provider key in the environment before running, for example:
    set AGENTFORGE_ANTHROPIC_KEY=...
"""

from __future__ import annotations

import json
import os
import sys

sys.path.insert(0, "..")

from ueagentforge_client import AgentForgeClient  # noqa: E402


KEY_ENV_BY_PROVIDER = {
    "Anthropic": "AGENTFORGE_ANTHROPIC_KEY",
    "OpenAI": "AGENTFORGE_OPENAI_KEY",
    "DeepSeek": "AGENTFORGE_DEEPSEEK_KEY",
    "OpenAICompatible": "AGENTFORGE_CUSTOM_KEY",
}


def configure_runtime_key(client: AgentForgeClient, provider: str) -> bool:
    env_name = KEY_ENV_BY_PROVIDER.get(provider, "")
    key = os.environ.get(env_name, "") if env_name else ""
    if not key:
        print(f"Missing {env_name}. Set it before running this example.")
        return False
    return client.llm_set_key(provider, key).ok


def main() -> int:
    provider = os.environ.get("UEAGENTFORGE_PROVIDER", "Anthropic")
    model = os.environ.get("UEAGENTFORGE_MODEL", "claude-sonnet-4-20250514")
    auto_refine = os.environ.get("UEAGENTFORGE_AUTO_REFINE", "0") == "1"

    client = AgentForgeClient(timeout=90.0, max_retries=6, retry_backoff_sec=1.0)
    if not configure_runtime_key(client, provider):
        return 1

    quality = client.vision_quality_score(provider=provider, model=model, multi_view=True)
    if not quality.ok:
        print(f"Vision score request failed: {quality.error}")
        return 2

    payload = quality.raw
    print("Initial score:")
    print(json.dumps(payload, indent=2))

    if auto_refine and float(payload.get("score", 0.0)) < 75.0:
        refinement = client.observe_analyze_plan_act(
            "Improve the scene toward a polished horror environment using the current level state.",
            max_iterations=1,
            score_target=75.0,
        )
        print("\nRefinement result:")
        print(json.dumps(refinement, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
