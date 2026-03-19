# Roadmap

## v0.1.0 - Foundation Complete

- [x] Core plugin structure (`UAgentForgeLibrary`, `UVerificationEngine`, `UConstitutionParser`)
- [x] 30+ commands across observation, actor control, spatial queries, Blueprint manipulation, material instancing, content management, transaction safety, Python scripting, performance profiling, and scene setup
- [x] 4-phase verification protocol (PreFlight, Snapshot+Rollback, PostVerify, BuildCheck)
- [x] Constitution system (markdown rule parsing, runtime enforcement)
- [x] Python client with transaction context manager
- [x] Example scripts
- [x] Full documentation and README
- [x] MIT license

## v0.2.0 - Spatial Intelligence and Fab Integration Complete

- [x] `spawn_actor_at_surface`
- [x] `align_actors_to_surface`
- [x] `get_surface_normal_at`
- [x] `analyze_level_composition`
- [x] `get_actors_in_radius`
- [x] `search_fab_assets`
- [x] `download_fab_asset` stub with workaround guidance
- [x] `import_local_asset`
- [x] `list_imported_assets`
- [x] `enhance_current_level`
- [x] HTTP and AudioEditor module dependencies
- [x] Example scripts

## v0.3.0 - Advanced Intelligence and Horror Optimization Complete

- [x] `get_multi_view_capture`
- [x] `get_level_hierarchy`
- [x] `get_deep_properties`
- [x] `get_semantic_env_snapshot`
- [x] `place_asset_thematically`
- [x] `refine_level_section`
- [x] `apply_genre_rules`
- [x] `create_in_editor_asset`
- [x] `observe_analyze_plan_act`
- [x] `enhance_horror_scene`
- [x] `set_bt_blackboard`
- [x] `wire_aicontroller_bt`

## v0.4.0 - Professional Quality Level Generation Pipeline Complete

Released: February 2026

- [x] `list_presets`
- [x] `load_preset`
- [x] `save_preset`
- [x] `suggest_preset`
- [x] `get_current_preset`
- [x] `create_blockout_level`
- [x] `convert_to_whitebox_modular`
- [x] `apply_set_dressing`
- [x] `apply_professional_lighting`
- [x] `add_living_systems`
- [x] `generate_full_quality_level`
- [x] JSON preset persistence
- [x] Example scripts

## v0.5.0 - Multi-Agent and Collaboration

Target: Q3 2026

- [ ] Agent identity for multi-agent tracing
- [ ] Command queue with ordered execution
- [ ] WebSocket transport
- [ ] Constitution remote URL support
- [ ] Audit log with agent ID, timestamp, and result
- [ ] `lock_actor` and `unlock_actor`
- [ ] World Partition support
- [ ] Level streaming support

## Current structural next phase

The current repo state is in trusted-surface expansion mode. The next structural work should stay focused:

1. Reduce one more high-value unverified pipeline or orchestration surface.
   Best current candidates: `op_terrain_generate` or a bounded slice of `generate_full_quality_level`.
2. Preserve and tighten truthful recovery metadata.
   Keep compensating-cleanup paths inspectable and do not upgrade them into rollback claims without proof.
3. Leave `delete_asset` blocked until reference analysis is real.
4. Begin the visual stability validation layer.
   Start with overlapping geometry, duplicate generated actors, competing directional lights, and flicker or z-fighting heuristics.

## v1.0.0 - Production Release

Target: Q1 2027

- [ ] Full automated test coverage
- [ ] GitHub Actions CI for UE 5.5, 5.6, and 5.7
- [ ] Packaged plugin distribution
- [ ] GitHub Pages documentation site
- [ ] Video tutorials
- [ ] Example project
- [ ] Stable API guarantee

## Feature requests

Submit feature requests as GitHub Issues with the label `enhancement`:
https://github.com/RobVanProd/UEAgentForge/issues
