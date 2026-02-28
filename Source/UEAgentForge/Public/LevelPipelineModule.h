// Copyright UEAgentForge Project. All Rights Reserved.
// LevelPipelineModule — v0.4.0 Five-Phase Professional Level Generation Pipeline.
//
// Implements a top-down, closed-loop AAA level construction pipeline:
//
//   Phase I   — RLD / Blockout          : primitive-based spatial layout
//   Phase II  — Architectural Whitebox  : replace blockout with modular kit pieces
//   Phase III — Beauty Pass / Set Dressing : props, micro-stories, storytelling
//   Phase IV  — Lighting & Atmosphere   : key lights, PP, fog, god-rays
//   Phase V   — Living Systems          : particles, audio emitters, polish
//
// GenerateFullQualityLevel() orchestrates all 5 phases with an OAPA quality loop.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"   // explicit with NoPCHs
#include "LevelPresetSystem.h"

class UEAGENTFORGE_API FLevelPipelineModule
{
public:
	// ──────────────────────────────────────────────────────────────────────────
	//  Phase entry points (each returns a JSON result string)
	// ──────────────────────────────────────────────────────────────────────────

	/** Phase I — RLD & Blockout.
	 *  Parses the mission description for room intent, generates a bubble-diagram
	 *  layout, and places BSP-style box actors as blockout rooms + corridors.
	 *
	 *  args: { "mission": "...", "preset": "Horror", "room_count": 3, "grid_size": 400 }
	 *  returns: { ok, rooms_placed, corridors_placed, total_area_sqm,
	 *             room_positions:[{label,x,y,z,width,depth,height}],
	 *             navmesh_placed, player_start_placed } */
	static FString CreateBlockoutLevel(const TSharedPtr<FJsonObject>& Args);

	/** Phase II — Architectural Whitebox.
	 *  Locates Blockout_* actors, lists kit meshes from kit_path, snaps to grid,
	 *  replaces primitives with modular wall/floor/ceiling pieces.
	 *
	 *  args: { "kit_path": "/Game/Gothic_Cathedral/Meshes", "snap_grid": 50 }
	 *  returns: { ok, pieces_placed, blockout_replaced, snap_grid,
	 *             arch_labels:[] } */
	static FString ConvertToWhiteboxModular(const TSharedPtr<FJsonObject>& Args);

	/** Phase III — Beauty Pass & Set Dressing.
	 *  For each room, determines story context from story_theme, scatters props
	 *  at thematically appropriate positions, builds micro-story arrangements.
	 *
	 *  args: { "story_theme": "abandoned asylum", "prop_density": 0.5 }
	 *  returns: { ok, props_placed, micro_stories, rooms_dressed } */
	static FString ApplySetDressingAndStorytelling(const TSharedPtr<FJsonObject>& Args);

	/** Phase IV — Lighting & Atmosphere.
	 *  Places key/fill/rim lights by time_of_day and mood, applies PP from preset,
	 *  adds ExponentialHeightFog, optionally places god-ray SpotLight.
	 *
	 *  args: { "time_of_day": "midnight", "mood": "fearful", "enable_god_rays": false }
	 *  returns: { ok, lights_placed, horror_score, atmosphere, fog_density } */
	static FString ApplyProfessionalLightingAndAtmosphere(const TSharedPtr<FJsonObject>& Args);

	/** Phase V — Living Systems.
	 *  Spawns Niagara ambient particle emitters (dust, embers, steam, leaves),
	 *  places AudioVolume actors for localised soundscapes.
	 *
	 *  args: { "ambient_vfx": ["dust","embers"], "soundscape": "asylum_ambience" }
	 *  returns: { ok, vfx_placed, audio_placed, vfx_names:[] } */
	static FString AddLivingSystemsAndPolish(const TSharedPtr<FJsonObject>& Args);

	/** Master orchestrator — runs all 5 phases in sequence with closed-loop quality.
	 *  If EvaluateLevelQuality() < quality_threshold and iteration < max_iterations,
	 *  the loop re-runs Phase IV + Phase V to improve atmospheric quality.
	 *
	 *  args: { "mission": "...", "preset": "Horror", "max_iterations": 3,
	 *          "quality_threshold": 0.75 }
	 *  returns: { ok, phase1:{}, phase2:{}, phase3:{}, phase4:{}, phase5:{},
	 *             final_quality_score, iterations, screenshot_path, level_saved } */
	static FString GenerateFullQualityLevel(const TSharedPtr<FJsonObject>& Args);

private:
	// ──────────────────────────────────────────────────────────────────────────
	//  Phase I helpers
	// ──────────────────────────────────────────────────────────────────────────

	/** Generate room centre positions using a bubble-diagram flow:
	 *  Entry → Exploration(s) → Climax → Exit.
	 *  Rooms are offset on a grid with spacing proportional to GridSize. */
	static TArray<FVector> GenerateRoomLayout(int32 RoomCount, float GridSize,
	                                           const FLevelPreset& Preset);

	/** Spawn a cube StaticMeshActor (engine unit cube) at Centre scaled to
	 *  Width×Depth×Height.  Returns true on success. */
	static bool PlaceBlockoutRoom(UWorld* World, const FVector& Center,
	                               float Width, float Depth, float Height,
	                               const FString& Label);

	/** Spawn thin box actors between adjacent room centres to act as corridor stand-ins. */
	static void ConnectRoomsWithCorridors(UWorld* World,
	                                       const TArray<FVector>& RoomCenters,
	                                       float CorridorWidth, float CeilingHeight);

	// ──────────────────────────────────────────────────────────────────────────
	//  Phase II helpers
	// ──────────────────────────────────────────────────────────────────────────

	/** Return all asset paths inside KitPath that look like wall/floor/ceiling meshes. */
	static TArray<FString> FindModularKitMeshes(const FString& KitPath);

	/** Snap Value to the nearest multiple of GridSize. */
	static float SnapToGrid(float Value, float GridSize);

	/** Replace Blockout_* actors in World with modular pieces from KitPath.
	 *  New actors are labeled Arch_Room_NN_Wall/Floor/Ceiling_XX. */
	static int32 ReplaceBlockoutWithModular(UWorld* World, const FString& KitPath,
	                                         float SnapGrid);

	// ──────────────────────────────────────────────────────────────────────────
	//  Phase III helpers
	// ──────────────────────────────────────────────────────────────────────────

	/** Scatter generic StaticMeshActors (cubes) as prop stand-ins within a room
	 *  radius, biased towards dark corners / occluded spots via SemanticCommandModule. */
	static int32 ScatterPropsInRoom(UWorld* World, const FVector& RoomCenter,
	                                 float Radius, float Density,
	                                 const FString& StoryTheme, int32 RoomIndex);

	/** Raycast downward from random points inside a disc to find a valid floor hit. */
	static FVector FindPropPlacementPoint(UWorld* World, const FVector& Center,
	                                       float Radius);

	// ──────────────────────────────────────────────────────────────────────────
	//  Phase IV helpers
	// ──────────────────────────────────────────────────────────────────────────

	/** Spawn and configure the primary key light (DirectionalLight or skylight
	 *  proxy) based on time-of-day string. */
	static int32 SetupKeyLighting(UWorld* World, const FString& TimeOfDay,
	                               const FString& Mood, const FLevelPreset& Preset);

	/** Add or update ExponentialHeightFog with preset density and colour. */
	static void ApplyAtmosphericScattering(UWorld* World, float FogDensity,
	                                        FLinearColor FogColor);

	/** Compute a naive horror atmosphere score (0–100) from lights and fog. */
	static float ComputeHorrorScore(UWorld* World);

	// ──────────────────────────────────────────────────────────────────────────
	//  Phase V helpers
	// ──────────────────────────────────────────────────────────────────────────

	/** Spawn NiagaraActor stand-ins (labeled VFX_{name}_NN) at scattered positions.
	 *  Returns count placed. */
	static int32 SpawnAmbientParticles(UWorld* World,
	                                    const TArray<FString>& VfxNames,
	                                    float Density);

	/** Place AudioVolume actors (labeled Audio_{soundscape}_NN) near room centres.
	 *  Returns count placed. */
	static int32 PlaceAmbientAudioEmitters(UWorld* World,
	                                        const FString& Soundscape);

	// ──────────────────────────────────────────────────────────────────────────
	//  Quality evaluation
	// ──────────────────────────────────────────────────────────────────────────

	/** Sample-based quality evaluation.  Returns 0–1 composite score.
	 *  Checks: actor count range, lighting coverage, player start present,
	 *          navmesh volume present, horror score vs preset threshold. */
	static float EvaluateLevelQuality(UWorld* World, const FLevelPreset& Preset);

	/** Return a human-readable quality report JSON object. */
	static TSharedPtr<FJsonObject> BuildQualityReport(UWorld* World,
	                                                   const FLevelPreset& Preset);
};
