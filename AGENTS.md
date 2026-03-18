# UEAgentForge Agent Operating Manual

This repository uses a Karpathy-style "program the agents" workflow. The active implementation targets are [`UEAGENTFORGE_V050_ADDENDUM_A.md`](UEAGENTFORGE_V050_ADDENDUM_A.md) first, then [`UEAGENTFORGE_V050_GAMEPLAN.md`](UEAGENTFORGE_V050_GAMEPLAN.md).

## Read Order

Read these files before making changes:

1. `program.md`
2. `UEAGENTFORGE_V050_ADDENDUM_A.md`
3. `UEAGENTFORGE_V050_GAMEPLAN.md`
4. `PythonClient/mcp_server/knowledge_base/building_guide.md`
5. `docs/07_architecture.md`
6. `README.md`
7. `PythonClient/ueagentforge_client.py`
8. `docs/10_roadmap.md`
9. `agent/review_inbox.md`

## Mission

Deliver Addendum A and the v0.5.0 plan without regressing the existing safety model.

The invariants are:

- `ExecuteCommandJson` remains the public command surface.
- Mutating commands keep transaction safety and rollback guarantees.
- Constitution enforcement stays in place.
- Existing v0.4.0 command behavior remains stable unless a documented fix is required.
- New LLM, MCP, schema, and vision work must fit the current plugin architecture instead of bypassing it.
- If a tool, command, or workflow is too vague to be reliable, refine it until the behavior and validation steps are crystal clear.

## Project Facts

- Unreal Engine 5.5+ editor-only plugin.
- Single module: `Source/UEAgentForge/`
- Core path: `UAgentForgeLibrary` -> `UVerificationEngine` + `UConstitutionParser`
- Python transport client: `PythonClient/ueagentforge_client.py`
- MCP construction guidance: `PythonClient/mcp_server/knowledge_base/building_guide.md`
- Current public roadmap: `docs/10_roadmap.md`
- Addendum-first scope expansion: `UEAGENTFORGE_V050_ADDENDUM_A.md`
- Detailed v0.5.0 build sheet: `UEAGENTFORGE_V050_GAMEPLAN.md`

## Edit Frontier

Primary new code belongs in:

- `Source/UEAgentForge/Public/LLM/`
- `Source/UEAgentForge/Private/LLM/`
- `PythonClient/mcp_server/`
- `PythonClient/mcp_server/knowledge_base/`
- `Content/AgentForge/Schemas/`
- `PythonClient/examples/`

Expected existing files to modify:

- `Source/UEAgentForge/Public/AgentForgeLibrary.h`
- `Source/UEAgentForge/Private/AgentForgeLibrary.cpp`
- `Source/UEAgentForge/UEAgentForge.Build.cs`
- `UEAgentForge.uplugin`
- `PythonClient/ueagentforge_client.py`
- `README.md`

Treat these as stable unless the current slice truly requires a change:

- `Source/UEAgentForge/Public/VerificationEngine.h`
- `Source/UEAgentForge/Private/VerificationEngine.cpp`
- `Source/UEAgentForge/Public/ConstitutionParser.h`
- `Source/UEAgentForge/Private/ConstitutionParser.cpp`
- Existing example scripts unrelated to the current v0.5.0 work item

## Non-Negotiable Constraints

- Do not add third-party HTTP libraries or marketplace dependencies.
- Do not store API keys on disk.
- Keep the LLM subsystem editor-only.
- Route new capabilities through the existing JSON command interface.
- Do not break existing command names or response shapes unless the game plan explicitly says to.
- Keep async and network work non-blocking.
- Prefer the existing Unreal patterns already used in the plugin over new abstractions.

## Unreal Actions Allowed

The agent is explicitly allowed to use local Unreal tooling for validation and execution.

Detected engine:

- `C:\Program Files\Epic Games\UE_5.7`

Detected host project candidates:

- `C:\Users\Rob\Documents\Unreal Projects\HorrorEngine\HorrorEngine.uproject`
- `C:\Users\Rob\Documents\Unreal Projects\HorrorGame\HorrorGame.uproject`

Safe temporary validation host project:

- `C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\agent\tmp\RuntimeHostProject\RuntimeHostProject.uproject`

Preferred validation actions:

- Compile the plugin directly with `RunUAT BuildPlugin` when a project is not required.
- Compile against a host project target when editor integration must be validated.
- Launch the Unreal Editor with a host project when screenshot, viewport, Remote Control API, or end-to-end command validation is needed.
- Prefer `agent/tools/launch_runtime_host.ps1` for unattended RuntimeHost launches because it disables stale Unreal restore state before waiting for Remote Control.

Preferred command patterns:

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\RunUAT.bat" BuildPlugin -Plugin="C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\UEAgentForge.uplugin" -Package="C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\agent\tmp\BuildPlugin" -Rocket
```

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\Rob\Documents\Unreal Projects\HorrorEngine\HorrorEngine.uproject" -log
```

```powershell
& "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\agent\tmp\RuntimeHostProject\RuntimeHostProject.uproject" -NoSplash -Unattended -NullRHI -ExecCmds="QUIT_EDITOR" -log
```

```powershell
powershell -ExecutionPolicy Bypass -File "C:\Users\Rob\Documents\Unreal Projects\UEAgentForge\agent\tools\launch_runtime_host.ps1" -StopExisting
```

Use the narrowest action that proves the current slice. Do not claim Unreal validation unless the command was actually run successfully.

## Definition Of Done

A work slice is only done when it:

- advances the addendum or main-plan checklist in priority order,
- advances the file checklist in `UEAGENTFORGE_V050_GAMEPLAN.md`,
- leaves the repository in a coherent state,
- includes the narrowest practical verification for the files touched,
- records any unresolved questions in `agent/review_inbox.md`,
- and leaves a clear next step for the following agent cycle.

## Verification Ladder

When C++ changes:

- verify includes, module deps, and command dispatch wiring,
- check that new files are referenced from the build/module setup,
- compile the plugin or editor target if the Unreal toolchain is available,
- launch the editor when the change affects viewport, screenshots, actor spawning, Remote Control API, or end-to-end tool behavior.
- if the editor stalls behind a `Restore Packages` modal, clear the restore state or use `agent/tools/launch_runtime_host.ps1` before retrying.

When Python changes:

- run `python -m py_compile` on touched files,
- smoke check imports where reasonable,
- keep noisy command output in log files instead of flooding the console context.

When docs or setup files change:

- ensure paths and file names match the repo,
- ensure instructions match the current architecture and roadmap,
- ensure command examples are explicit enough that an agent can execute them without guessing,
- avoid promising validation that the repo cannot actually perform.

## Working Loop

- Work from the critical path in `UEAGENTFORGE_V050_ADDENDUM_A.md` first, then `UEAGENTFORGE_V050_GAMEPLAN.md`.
- Prefer vertical slices that stay mergeable.
- Commit one logical change at a time.
- Keep discarded experiments local; do not leave half-integrated code behind.
- If human judgment is useful but not urgent, log it in `agent/review_inbox.md` and keep moving.
- Once the overnight loop begins, do not keep stopping for confirmation.

## Run Artifacts

Use these local runtime artifacts during autonomous runs:

- `agent/results.tsv` - untracked progress log created locally during a run
- `agent/run.log` - untracked capture for a long-running command
- `agent/logs/` - untracked per-step logs
- `agent/review_inbox.md` - tracked handoff file for the human

Recommended `agent/results.tsv` header:

```tsv
commit	workstream	verification	status	description
```

## Branching

Use a dedicated branch for overnight implementation work. Recommended pattern:

`autoresearch/ueagentforge-v050-YYYY-MM-DD`

If continuing an existing overnight run, stay on that branch instead of spawning a second parallel branch in the same workspace.

## Morning Handoff

Before stopping, leave:

- the current branch name,
- the last known good commit,
- remaining checklist items,
- blockers or design questions in `agent/review_inbox.md`,
- and any required credentials or environment gaps stated explicitly.
