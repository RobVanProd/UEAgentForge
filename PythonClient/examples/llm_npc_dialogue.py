"""
Example: generate an NPC personality payload, then use it to create dialogue.

Set one provider key in the environment before running, for example:
    set AGENTFORGE_OPENAI_KEY=...
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
    provider = os.environ.get("UEAGENTFORGE_PROVIDER", "OpenAI")
    model = os.environ.get("UEAGENTFORGE_MODEL", "gpt-4o")

    client = AgentForgeClient(timeout=60.0)
    if not configure_runtime_key(client, provider):
        return 1

    personality = client.generate_npc_personality(
        provider=provider,
        model=model,
        prompt=(
            "Create an anxious but capable maintenance engineer who has survived three nights "
            "inside a flooded industrial research wing."
        ),
        temperature=0.5,
    )
    if not personality.ok:
        print(f"Failed to generate personality: {personality.error}")
        return 2

    profile = personality.raw.get("structured")
    if profile is None and personality.raw.get("content"):
        profile = json.loads(personality.raw["content"])

    dialogue = client.llm_chat(
        provider=provider,
        model=model,
        system=(
            "You are writing game-ready NPC dialogue. Keep each line punchy, characterful, "
            "and grounded in environmental storytelling."
        ),
        messages=[
            {
                "role": "user",
                "content": (
                    "Use this NPC profile to write 5 spoken lines and 2 optional bark lines:\n"
                    f"{json.dumps(profile, indent=2)}"
                ),
            }
        ],
        temperature=0.7,
        max_tokens=300,
    )
    if not dialogue.ok:
        print(f"Dialogue request failed: {dialogue.error}")
        return 3

    print("NPC profile:")
    print(json.dumps(profile, indent=2))
    print("\nDialogue:")
    print(dialogue.raw.get("content", ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
