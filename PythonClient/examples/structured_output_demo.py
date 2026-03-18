"""
Example: request schema-validated JSON through the UEAgentForge LLM bridge.

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
    result = client.llm_set_key(provider, key)
    if not result.ok:
        print(f"Failed to set runtime key: {result.error}")
        return False
    return True


def main() -> int:
    provider = os.environ.get("UEAGENTFORGE_PROVIDER", "OpenAI")
    model = os.environ.get("UEAGENTFORGE_MODEL", "gpt-4o")

    client = AgentForgeClient(timeout=60.0)
    if not configure_runtime_key(client, provider):
        return 1

    result = client.generate_level_layout(
        provider=provider,
        model=model,
        prompt=(
            "Design a compact survival-horror mansion wing with 4 rooms, a locked shortcut, "
            "one strong visual reveal, and lighting notes that support player guidance."
        ),
        temperature=0.3,
    )
    if not result.ok:
        print(f"Structured request failed: {result.error}")
        return 2

    structured = result.raw.get("structured")
    if structured is None and result.raw.get("content"):
        structured = json.loads(result.raw["content"])

    print(json.dumps(structured, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
