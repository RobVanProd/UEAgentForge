# Roadmap

## v0.1.0 — Foundation ✅ Complete

- [x] Core plugin structure (`UAgentForgeLibrary`, `UVerificationEngine`, `UConstitutionParser`)
- [x] 30+ commands: observation, actor control, spatial queries, Blueprint manipulation, material instancing, content management, transaction safety, Python scripting, performance profiling, scene setup
- [x] 4-phase verification protocol (PreFlight, Snapshot+Rollback, PostVerify, BuildCheck)
- [x] Constitution system (markdown rule parsing, runtime enforcement)
- [x] Python client with transaction context manager
- [x] 3 example scripts
- [x] Full documentation (9 docs + README)
- [x] MIT license

## v0.2.0 — Spatial Intelligence + FAB Integration ✅ Complete

- [x] `spawn_actor_at_surface` — raycast + surface-normal-aligned spawn
- [x] `align_actors_to_surface` — drop actors to nearest surface
- [x] `get_surface_normal_at` — surface normal query for AI placement
- [x] `analyze_level_composition` — actor density, bounding box, AI recommendations
- [x] `get_actors_in_radius` — sphere search sorted by distance
- [x] `search_fab_assets` — Fab.com web search (free-only default, synchronous HTTP)
- [x] `download_fab_asset` — stub with workaround docs (no public Fab API)
- [x] `import_local_asset` — import FBX/OBJ/PNG/WAV from disk via AssetTools
- [x] `list_imported_assets` — list assets in any Content Browser folder
- [x] `enhance_current_level` — natural language → orchestrated multi-command improvement loop
- [x] HTTP + AudioEditor module dependencies added
- [x] 3 Python example scripts

## v0.3.0 — Advanced Intelligence & Horror Optimization ✅ Complete

- [x] `get_multi_view_capture` — capture scene from multiple angles
- [x] `get_level_hierarchy` — full actor hierarchy with parent-child trees
- [x] `get_deep_properties` — nested component property inspection
- [x] `get_semantic_env_snapshot` — rich LLM-ready scene description
- [x] `place_asset_thematically` — genre-aware placement (horror/sci-fi/fantasy)
- [x] `refine_level_section` — spatial region improvement loop
- [x] `apply_genre_rules` — full-level genre rule application
- [x] `create_in_editor_asset` — factory-based in-editor asset creation
- [x] `observe_analyze_plan_act` — full Observe-Analyze-Plan-Act loop
- [x] `enhance_horror_scene` — closed-loop horror tension pipeline
- [x] `set_bt_blackboard` — links BlackboardData to BehaviorTree (bypasses Python CPF_Protected)
- [x] `wire_aicontroller_bt` — wires BeginPlay→RunBehaviorTree in AIController (bypasses UbergraphPages)

## v0.4.0 — Professional Quality Level Generation Pipeline ✅ Complete

Released: February 2026

- [x] `list_presets` — list all named level presets (5 built-in: Default/Horror/SciFi/Fantasy/Military)
- [x] `load_preset` — apply preset settings to current level
- [x] `save_preset` — save current configuration as a named preset (JSON persistence)
- [x] `suggest_preset` — AI-driven preset recommendation based on project content
- [x] `get_current_preset` — retrieve active preset metadata
- [x] `create_blockout_level` — Phase I: blockout geometry generation
- [x] `convert_to_whitebox_modular` — Phase II: modular static mesh replacement
- [x] `apply_set_dressing` — Phase III: storytelling props and environmental detail
- [x] `apply_professional_lighting` — Phase IV: atmosphere, lighting, post-process
- [x] `add_living_systems` — Phase V: NavMesh, AI spawns, particles, audio volumes
- [x] `generate_full_quality_level` — master command: all 5 phases + quality-scoring loop
- [x] JSON preset persistence in `Content/AgentForge/Presets/`
- [x] 3 Python example scripts: blockout-only, full pipeline, custom preset authoring

## v0.5.0 — Multi-Agent & Collaboration

**Target:** Q3 2026

- [ ] Agent identity — tag commands with an `agent_id` for multi-agent tracing
- [ ] Command queue — submit multiple commands in one HTTP call with ordered execution
- [ ] WebSocket transport — lower latency than polling for event-driven agents
- [ ] Constitution remote URL — load rules from a team-shared endpoint
- [ ] Audit log — structured log of all commands with agent ID, timestamp, result
- [ ] `lock_actor` / `unlock_actor` — prevent concurrent agent edits on the same object
- [ ] World Partition support: `load_cell`, `unload_cell`, `get_loaded_cells`
- [ ] Level streaming: `load_sublevel`, `unload_sublevel`, `get_sublevels`

## v1.0.0 — Production Release

**Target:** Q1 2027

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
