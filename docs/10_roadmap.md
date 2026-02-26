# Roadmap

## v0.1.0 — Foundation (current)

**Status:** Complete

- [x] Core plugin structure (`UAgentForgeLibrary`, `UVerificationEngine`, `UConstitutionParser`)
- [x] 30+ commands: observation, actor control, spatial queries, Blueprint manipulation, material instancing, content management, transaction safety, Python scripting, performance profiling, scene setup
- [x] 4-phase verification protocol (PreFlight, Snapshot+Rollback, PostVerify, BuildCheck)
- [x] Constitution system (markdown rule parsing, runtime enforcement)
- [x] Python client with transaction context manager
- [x] 3 example scripts
- [x] Full documentation (9 docs + README)
- [x] MIT license

## v0.2.0 — Blueprint Graph Control

**Target:** Q2 2026

The biggest gap in current UE5 AI bridges is node-level Blueprint graph editing. v0.2.0 focuses here.

- [ ] `create_blueprint_node` — spawn any UK2Node type at a given position in the graph
- [ ] `connect_blueprint_pins` — wire two node pins together
- [ ] `get_blueprint_graph` — return full node/pin graph as JSON (for agent context)
- [ ] `delete_blueprint_node` — remove a node and clean up connections
- [ ] `set_actor_property` — set any UPROPERTY on any actor by name + type
- [ ] `get_actor_property` — read any UPROPERTY value by name
- [ ] ConstitutionParser severity levels: `[WARN]` prefix = non-blocking
- [ ] Automated integration test suite (pytest against a running editor instance)

## v0.3.0 — Content Pipeline

**Target:** Q3 2026

- [ ] `import_asset` — import a file (FBX, PNG, WAV) from disk into the Content Browser
- [ ] `export_asset` — export a content browser asset to disk
- [ ] `create_texture` — create a UTexture2D from raw pixel data or a file
- [ ] `create_static_mesh` — procedurally create a `UStaticMesh` via GeometryScripting
- [ ] `set_actor_material` — assign a material to an actor's mesh component by slot index
- [ ] `create_sound_cue` — create a basic USoundCue asset
- [ ] Niagara: `spawn_niagara_system`, `set_niagara_parameter`
- [ ] Bulk content operations: `rename_assets_bulk`, `delete_assets_bulk`

## v0.4.0 — Level Streaming & World Partition

**Target:** Q4 2026

- [ ] World Partition support: `load_cell`, `unload_cell`, `get_loaded_cells`
- [ ] Level streaming: `load_sublevel`, `unload_sublevel`, `get_sublevels`
- [ ] `get_world_bounds` — AABB of the entire loaded world
- [ ] `teleport_player` — move the player pawn (runtime use case)
- [ ] Landscape: `get_landscape_heightmap`, `paint_landscape_layer`

## v0.5.0 — Multi-Agent & Collaboration

**Target:** Q1 2027

- [ ] Agent identity — tag commands with an `agent_id` for multi-agent tracing
- [ ] Command queue — submit multiple commands in one HTTP call with ordered execution
- [ ] WebSocket transport — lower latency than polling for event-driven agents
- [ ] Constitution remote URL — load rules from a team-shared endpoint
- [ ] Audit log — structured log of all commands with agent ID, timestamp, result
- [ ] `lock_actor` / `unlock_actor` — prevent concurrent agent edits on the same object

## v1.0.0 — Production Release

**Target:** Q2 2027

- [ ] Full automated test coverage (all commands tested)
- [ ] GitHub Actions CI (builds against UE 5.5, 5.6, 5.7)
- [ ] Packaged plugin (`.uplugin` zip for direct installation)
- [ ] GitHub Pages documentation site
- [ ] Video tutorials
- [ ] Example project (standalone UE5 project demonstrating all features)
- [ ] Stable API guarantee (no breaking changes without major version bump)

## Feature requests

Submit feature requests as GitHub Issues with the label `enhancement`:
https://github.com/RobVanProd/UEAgentForge/issues
