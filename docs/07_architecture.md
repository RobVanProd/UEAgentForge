# Architecture

## Overview

UEAgentForge is structured as a single Editor-only UE5 module with three core classes and a transport layer.

The current v0.5.0 foundation includes these Addendum A and subsystem slices:

- read-only asset discovery commands (`get_available_meshes`, `get_available_materials`)
- viewport helpers used for deterministic screenshot capture (`set_viewport_camera`, `redraw_viewports`)
- new mutating scene-dressing commands (`set_static_mesh`, `set_actor_scale`, `apply_material_to_actor`, `set_mesh_material_color`, `set_material_scalar_param`)
- light spawning commands (`spawn_point_light`, `spawn_spot_light`)
- expanded discovery and inspection commands (`get_available_blueprints`, `get_available_textures`, `get_available_sounds`, `get_asset_details`)
- actor metadata/property controls (`duplicate_actor`, `set_actor_label`, `set_actor_mobility`, `set_actor_visibility`, `get_actor_property`, `set_actor_property`, `group_actors`)
- compound building primitives (`create_wall`, `create_floor`, `create_room`, `create_corridor`, `create_pillar`)
- a Python MCP server under `PythonClient/mcp_server/` that exposes the same command surface to external agent hosts
- a UE-side LLM subsystem under `Source/UEAgentForge/Public/LLM/` and `Source/UEAgentForge/Private/LLM/`

```
┌─────────────────────────────────────────────────────────────────────┐
│                         UEAgentForge Plugin                         │
│                                                                     │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                     AgentForgeLibrary                         │  │
│  │                  (UBlueprintFunctionLibrary)                  │  │
│  │                                                              │  │
│  │  ExecuteCommandJson()  ──→  IsMutatingCommand?               │  │
│  │       │                         │                            │  │
│  │       │                    YES  ▼                            │  │
│  │       │              ExecuteSafeTransaction()                 │  │
│  │       │                    │                                  │  │
│  │       │               Phase 1: PreFlight                      │  │
│  │       │               Phase 2: Snapshot+Rollback              │  │
│  │       │               → Execute for real                      │  │
│  │       │               Phase 3: PostVerify                     │  │
│  │       │               Phase 4: BuildCheck                     │  │
│  │       │                                                      │  │
│  │       │ NO ──→  Direct route to read-only handler            │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                                                                     │
│  ┌────────────────────┐    ┌──────────────────────────────────┐   │
│  │  VerificationEngine │    │       ConstitutionParser         │   │
│  │  (UObject, root)   │    │       (UObject, root)            │   │
│  │                    │    │                                  │   │
│  │  RunPhases()       │    │  AutoLoadConstitution()          │   │
│  │  RunPreFlight()    │    │  LoadConstitution(path)          │   │
│  │  RunSnapshotRollback│   │  ValidateAction()                │   │
│  │  RunPostVerify()   │    │  GetRules()                      │   │
│  │  RunBuildCheck()   │    │                                  │   │
│  │  CreateSnapshot()  │    │  Rules: [FConstitutionRule]      │   │
│  │  DiffSnapshots()   │    │  TriggerKeywords per rule        │   │
│  └────────────────────┘    └──────────────────────────────────┘   │
│                                                                     │
│  Module startup: FUEAgentForgeModule::StartupModule()               │
│     → UConstitutionParser::Get()->AutoLoadConstitution()            │
└─────────────────────────────────────────────────────────────────────┘
              ▲
              │  HTTP PUT /remote/object/call
              │  (Unreal Remote Control API)
              │
         AI Agent / Python Client / curl
```

## Class responsibilities

### `UAgentForgeLibrary`

The public-facing command surface. Inherits from `UBlueprintFunctionLibrary` so its static functions appear as nodes in Blueprint and are discoverable by the Remote Control API via CDO path.

Key design decisions:
- **All static** — BlueprintFunctionLibrary pattern; no instance state
- **Routing table** — `ExecuteCommandJson` dispatches `cmd` strings to private handlers
- **Mutating/read split** — `IsMutatingCommand()` determines whether the command goes through `ExecuteSafeTransaction` or routes directly
- **Asset discovery stays read-only** — Content Browser search commands use direct routing because they cannot mutate editor state
- **Scene dressing stays inside safety rails** — mesh swaps, scale changes, material application, and light spawning all use the existing rollback-capable transaction path
- **Shared utilities** — `ParseJsonObject`, `ToJsonString`, `ErrorResponse`, `OkResponse`, `FindActorByLabelOrName`, `VecToJson` used by all handlers

### `UVerificationEngine`

Singleton (`AddToRoot()` prevents GC) that owns the pre-state capture and 4-phase protocol state.

State:
- `PreStateActorLabels` — labels captured in Phase 1, compared in Phase 3
- `PreStateActorCount` — count compared in Phase 3 and Phase 2 rollback test
- `LastVerificationResult` — JSON string of the last explicit `run_verification` call
- `Singleton` — static pointer, initialized on first `Get()` call

Phase 2 (`RunSnapshotRollback`) takes a `TFunction<bool()>` lambda so the command implementation doesn't need to know about the verification system — the library passes it in.

### `UConstitutionParser`

Singleton that parses the constitution markdown at startup. Stores `TArray<FConstitutionRule>` where each rule has its trigger keywords pre-extracted for O(rules × keywords) validation time.

The markdown parser is intentionally simple — it only looks at section headings and bullet lines, ignoring all other markdown syntax. This makes it robust to formatting variations in real constitution files.

## Transport layer

UEAgentForge does not implement its own HTTP server. It relies entirely on the Unreal **Remote Control API** plugin, which:
- Listens on port 30010 by default
- Routes `PUT /remote/object/call` to the specified `objectPath` + `functionName`
- Marshals the call to the game thread
- Returns the function's return value as JSON

The plugin's CDO path is:
```
/Script/UEAgentForge.Default__AgentForgeLibrary
```

This is the path the Python client and AI agents use to target the plugin.

## Transaction model

UEAgentForge uses two layers of transactions:

**Layer 1 — Automatic (per command):**
Every mutating command in `ExecuteSafeTransaction` opens an `FScopedTransaction`. This provides automatic Ctrl+Z support for the change.

**Layer 2 — Manual (multi-command):**
The `begin_transaction` / `end_transaction` commands allow the caller to group multiple commands into one undo entry. This uses a `TUniquePtr<FScopedTransaction>` stored as a file-scope static, which stays open until `end_transaction` or plugin shutdown.

**Phase 2 rollback test transaction:**
A third temporary transaction is opened during Phase 2 specifically to test rollback. This transaction is always cancelled before the real one is opened. The rollback test lambda runs inside this cancelled transaction, and then again inside the real transaction.

Some Unreal editor operations are not rollback-safe in this preflight pattern. Those commands are explicitly skip-listed and rely on Phase 1 plus the real transaction path instead. Actor spawning, duplication, grouping, compound primitive creation, and actor relabeling are examples.

## Singleton lifetime

Both `UVerificationEngine::Get()` and `UConstitutionParser::Get()` use `AddToRoot()` to prevent garbage collection. They are initialized on first access and live for the entire editor session.

This is safe because both classes only hold in-memory data (actor label arrays, parsed rule structs). No resource handles, no threads.

## File layout

```
Source/UEAgentForge/
├── UEAgentForge.Build.cs          Module rules, all deps
├── Public/
│   ├── AgentForgeLibrary.h        Main command surface declaration
│   ├── VerificationEngine.h       4-phase protocol, FVerificationPhaseResult
│   └── ConstitutionParser.h       Rule types, FConstitutionRule
└── Private/
    ├── UEAgentForge.cpp           Module, auto-loads constitution on startup
    ├── AgentForgeLibrary.cpp      Command router + all handler implementations
    ├── VerificationEngine.cpp     Phase implementations, snapshot I/O
    └── ConstitutionParser.cpp     Markdown parser, keyword extraction
```

Additional v0.5.0 frontier paths:

- `Source/UEAgentForge/Public/LLM/` and `Source/UEAgentForge/Private/LLM/` - provider interfaces, providers, and the editor LLM subsystem
- `PythonClient/mcp_server/` - MCP server, host configs, and knowledge-base guidance

## Dependency map

```
UEAgentForge.Build.cs depends on:
  Core, CoreUObject, Engine, UnrealEd, EditorSubsystem   ← basics
  Json, JsonUtilities                                      ← command transport
  AssetRegistry, AssetTools, ObjectTools                   ← content management
  ImageWrapper, RenderCore, Renderer                       ← screenshots
  LevelEditor                                              ← viewport camera control and redraw
  Kismet, KismetCompiler, BlueprintGraph                   ← BP manipulation
  BlueprintEditorLibrary, GraphEditor                      ← BP graph editing
  NavigationSystem                                         ← navmesh queries
  RHI                                                      ← GPU perf stats
  PythonScriptPlugin                                       ← execute_python
```

## Validation harness

The root workflow docs (`AGENTS.md`, `CODEX.md`, `program.md`) now treat Unreal compilation and editor launch as standard validation actions, not optional side notes.

The narrowest compile proof is `RunUAT BuildPlugin` against the detected UE 5.7 install. For editor smoke tests, the repo uses a temporary host project under `agent/tmp/RuntimeHostProject/` so command routing, plugin mounting, and headless editor startup can be proven without touching the user's live Unreal projects.

Use `agent/tools/launch_runtime_host.ps1` for unattended scratch-host startup. It disables stale project and engine autosave restore state before waiting on `http://127.0.0.1:30010/remote/info`, which avoids the blocking Unreal `Restore Packages` modal discovered during live validation.

## Design principles

**1. Command pattern, not method explosion**
All operations funnel through a single `ExecuteCommandJson(FString)` entry point. This is what the Remote Control API calls. The routing table is explicit and exhaustive.

**2. Verification is not optional for mutations**
The `IsMutatingCommand()` check forces all state-changing operations through `ExecuteSafeTransaction`. Read-only commands bypass verification because they cannot cause damage.

**3. Singletons for cross-command state**
The verification pre-state (actor list) must persist between Phase 1 (capture) and Phase 3 (compare). A singleton is the right pattern here — no per-request context object is needed because the Remote Control API is inherently single-threaded on the game thread.

**4. Fail-fast on constitution violations**
Phase 1 returns an error immediately before any state is changed. This is the cheapest possible failure — no transactions opened, no snapshots created.

**5. Non-blocking PostVerify**
Phase 3 warns but doesn't fail because actor count deltas can legitimately differ from expectations (engine background spawns, deferred construction, etc.). The real safety guarantee is Phase 2's rollback test, not Phase 3's count check.
