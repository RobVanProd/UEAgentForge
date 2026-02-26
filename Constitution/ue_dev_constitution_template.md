# UE Development Constitution Template

This file is the default template for UEAgentForge's constitution system.
Copy this to your project root as `ue_dev_constitution.md` and customize it.

UEAgentForge auto-discovers and loads this file at editor startup from these locations
(searched in order):
  1. `{ProjectDir}/../ue_dev_constitution.md`
  2. `{ProjectDir}/Constitution/ue_dev_constitution.md`
  3. `{ProjectDir}/ue_dev_constitution.md`
  4. `{PluginDir}/Constitution/ue_dev_constitution_template.md` (fallback)

---

## What Is a Constitution?

A constitution is a project-level governance document that defines the rules,
constraints, and conventions that ALL changes to the project must respect — whether
made by a human developer or an AI agent.

UEAgentForge's ConstitutionParser reads this file at startup and enforces these rules
against every action description passed through `enforce_constitution` or
`ExecuteSafeTransaction`. Violations are logged and can block execution.

---

## Non-negotiable constraints

The following rules are enforced by the UEAgentForge verification pipeline.
Customize these for your project:

- One change per iteration. Never batch multiple unrelated changes in a single command.
- Components over bloated actor classes. Add capabilities as components, not by extending actors.
- No plugin source edits unless explicitly requested and approved by a human reviewer.
- No magic numbers for gameplay tuning. All tunable values must be exposed as UPROPERTY.
- Keep Tick minimal and explicit. Disable Tick by default; enable only when necessary.
- Prefer additive, reversible edits with a clear audit trail.
- Verify behavior (build, logs, or agent response) after each change.
- No direct coupling between systems. Use delegates or interfaces.
- Never modify third-party marketplace plugin source files.
- Safety check: confirm target map, asset, or path is inside the project before mutating.

---

## Rules for Blueprint editing

- Always compile after modifying a Blueprint graph.
- Do not add nodes that reference engine-private classes.
- Prefer Blueprint Function Libraries over duplicating logic in multiple event graphs.
- Test Blueprint changes in the M_AgentVerification map before applying to production levels.

---

## Rules for C++ changes

- One C++ source file change per build-test cycle.
- Include UHT-generated headers (.generated.h) in the correct order.
- Never include platform-specific headers inside UObject subclasses.
- Always wrap editor-only code in `#if WITH_EDITOR`.
- Use `UPROPERTY(EditAnywhere, Category="...")` for all designer-tunable values.

---

## Rules for content management

- Asset names must follow the project naming convention: `{Prefix}_{DescriptiveName}`.
- Never delete assets without first checking for references (`delete_asset` performs a ref-check).
- Moved assets must be re-referenced in all dependent Blueprints and levels.
- Production content lives under `/Game/` — test content lives under `/Game/AgentForgeTest/`.

---

## Project-specific additions

Add your project's specific rules below this line:

<!-- Example:
- No edits to Oceanology_Plugin source (use facade pattern only).
- All ocean creature classes must inherit from UOceanCreatureComponent.
- Main gameplay map is /Game/Maps/M_Underwater — do not delete or rename it.
-->
