# UEAgentForge Program

This file adapts Karpathy's `autoresearch` pattern to shipping software in UEAgentForge. The human edits the mission and constraints here. The coding agent executes the loop.

## Setup

To start a fresh overnight run, work through this sequence:

1. Read `AGENTS.md`, `UEAGENTFORGE_V050_ADDENDUM_A.md`, `UEAGENTFORGE_V050_GAMEPLAN.md`, `PythonClient/mcp_server/knowledge_base/building_guide.md`, `docs/07_architecture.md`, `README.md`, `PythonClient/ueagentforge_client.py`, and `agent/review_inbox.md`.
2. Create or continue a dedicated branch using the exact date in the name. Recommended pattern: `autoresearch/ueagentforge-v050-YYYY-MM-DD`.
3. Create these local runtime artifacts if missing:
   - `agent/results.tsv`
   - `agent/run.log`
   - `agent/logs/`
4. Initialize `agent/results.tsv` with this header if the file is new:

```tsv
commit	workstream	verification	status	description
```

5. Confirm what can be validated on the current machine:
   - Python is available for `py_compile` and MCP/client checks
   - Unreal build tooling is available if compilation is expected
   - Remote Control API smoke tests are only possible if the editor is running
6. Start work. Do not wait for the human once the loop has begun.

## Mission

Deliver the addendum and v0.5.0 scope in this order:

- `UEAGENTFORGE_V050_ADDENDUM_A.md` priority order first,
- then `UEAGENTFORGE_V050_GAMEPLAN.md`.

That means the overnight path starts with granular scene commands, asset discovery, material application, lighting, compound building tools, and MCP guidance before the LLM subsystem phases.

The output must feel like a production-quality extension of the current plugin, not a sidecar prototype.

## Files That Matter

Fixed context:

- `UEAGENTFORGE_V050_ADDENDUM_A.md`
- `UEAGENTFORGE_V050_GAMEPLAN.md`
- `docs/07_architecture.md`
- `docs/10_roadmap.md`
- `README.md`
- `PythonClient/mcp_server/knowledge_base/building_guide.md`

Primary implementation frontier:

- `Source/UEAgentForge/Public/LLM/`
- `Source/UEAgentForge/Private/LLM/`
- `PythonClient/mcp_server/`
- `Content/AgentForge/Schemas/`
- `PythonClient/examples/`
- the existing bridge files named in the game plan

## Hard Boundaries

- Do not bypass `ExecuteCommandJson`.
- Do not weaken verification, rollback, or constitution behavior.
- Do not add new external dependencies beyond what the game plan requires.
- Do not persist secrets to disk.
- Do not rework unrelated subsystems just because they could be cleaner.
- Do not stop and wait for routine approval once the overnight run is active.
- You may compile the plugin and launch Unreal Editor when the current slice needs real validation.
- If a tool or command is too vague to work reliably, make it more explicit in code, docs, and MCP descriptions.

## Delivery Loop

LOOP UNTIL MANUALLY STOPPED:

1. Inspect git state and the remaining items in `UEAGENTFORGE_V050_GAMEPLAN.md`.
2. Inspect the remaining items in `UEAGENTFORGE_V050_ADDENDUM_A.md`.
3. Choose the smallest critical-path slice that creates forward progress. Start with Addendum A priority-1 commands before MCP, then continue to the main v0.5.0 phases.
4. Implement the slice directly in the repo.
5. Run the narrowest useful validation for the edited files. Redirect long output to `agent/run.log` or `agent/logs/<timestamp>-<topic>.log`.
6. When the slice touches editor behavior, commands, screenshots, or viewport workflows, compile and launch Unreal if that is the shortest credible proof.
7. If the slice is good, commit it and append one line to `agent/results.tsv`.
8. If the slice fails, either fix it immediately or revert to the last good commit and log the attempt as `discard` or `blocked`.
9. If human judgment is required, write a short entry in `agent/review_inbox.md` and continue with other unblocked work.
10. Repeat.

## Keep Or Discard Rule

Keep a change only if it clearly advances the shipped product:

- a planned file exists and is wired correctly,
- validation coverage improves,
- a real blocker is removed,
- or complexity drops without losing capability.

If two solutions both work, prefer the simpler one that fits the existing architecture.

## Verification Priorities

Verify in this order:

1. Build and syntax correctness
2. Addendum A priority-1 command coverage and behavior
3. New command dispatch coverage for `llm_chat`, `llm_structured`, `llm_stream`, `llm_set_key`, `llm_get_models`, `vision_analyze`, and `vision_quality_score`
4. Python MCP server import/startup sanity
5. Python client method coverage for the new commands
6. No obvious regressions to existing v0.4.0 commands
7. Documentation alignment with the actual code

## When To Stop

Stop only when:

- the user interrupts,
- the repo reaches the best coherent completion point available on the current machine,
- or a hard external dependency prevents safe progress.

In every case, leave a complete handoff in `agent/review_inbox.md`.
