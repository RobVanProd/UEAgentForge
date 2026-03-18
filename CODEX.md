# Codex Entry Point

Read these files in order:

1. `AGENTS.md`
2. `program.md`
3. `UEAGENTFORGE_V050_ADDENDUM_A.md`
4. `UEAGENTFORGE_V050_GAMEPLAN.md`
5. `PythonClient/mcp_server/knowledge_base/building_guide.md`
6. `docs/07_architecture.md`
7. `agent/review_inbox.md`

Mission:

Autonomously deliver Addendum A first and then the UEAgentForge v0.5.0 plan on top of the existing v0.4.0 safety architecture.

Operating rules:

- Follow the implementation order already defined in `UEAGENTFORGE_V050_GAMEPLAN.md`.
- Execute `UEAGENTFORGE_V050_ADDENDUM_A.md` before Phase 1 of the main game plan.
- Keep working until manually stopped or a real external blocker is hit.
- Put unresolved questions in `agent/review_inbox.md` instead of waiting in chat.
- Use `agent/results.tsv` and `agent/logs/` for runtime artifacts, not git history.
- Protect existing command behavior, transaction safety, rollback verification, and constitution enforcement.
- You are allowed to compile the plugin and launch Unreal Editor for validation using the paths documented in `AGENTS.md`.
- If a command or tool is underspecified, improve the implementation and the documentation until usage is explicit.

Starter prompt:

`Read CODEX.md, AGENTS.md, program.md, UEAGENTFORGE_V050_ADDENDUM_A.md, and UEAGENTFORGE_V050_GAMEPLAN.md. Continue from the next uncompleted Addendum A or v0.5.0 critical-path item. Compile and launch Unreal when the current slice requires proof. Log blockers in agent/review_inbox.md and do not stop unless you hit a hard external dependency.`
