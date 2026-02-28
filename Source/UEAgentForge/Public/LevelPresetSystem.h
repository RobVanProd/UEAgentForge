// Copyright UEAgentForge Project. All Rights Reserved.
// LevelPresetSystem — v0.4.0 Named preset storage for the 5-phase level pipeline.
//
// A preset bundles all tunable parameters for one genre of level so that
// PhaseI-V pipeline calls can reason about the project's art direction without
// magic numbers scattered across the codebase.
//
// Built-in presets: Default, Horror, SciFi, Fantasy, Military.
// Custom presets are persisted as JSON in Content/AgentForge/Presets/.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"   // explicit with NoPCHs
#include "Math/Color.h"

// ─────────────────────────────────────────────────────────────────────────────
//  FLevelPreset — one complete art/gameplay configuration bundle
// ─────────────────────────────────────────────────────────────────────────────

struct UEAGENTFORGE_API FLevelPreset
{
	FString PresetName;
	FString Description;

	// ── Phase I metrics (gameplay / blockout scale) ───────────────────────────
	float StandardDoorWidthCm     = 200.f;
	float StandardCeilingHeightCm = 300.f;
	float PlayerEyeHeightCm       = 170.f;
	float MaxJumpHeightCm         = 120.f;
	float MinCorridorWidthCm      = 150.f;

	// ── Phase II kit preferences ──────────────────────────────────────────────
	TArray<FString> PreferredModularKitPaths;
	TArray<FString> PreferredMaterialPaths;

	// ── Phase III set dressing ───────────────────────────────────────────────
	float SetDressingDensity          = 0.5f;   // 0 = sparse, 1 = dense
	bool  bEnableVertexPaintWeathering = true;

	// ── Phase IV lighting ─────────────────────────────────────────────────────
	FLinearColor AmbientLightColor          = FLinearColor(0.1f, 0.1f, 0.2f, 1.f);
	float        AmbientIntensityMultiplier = 1.0f;
	bool         bEnableGodRays             = false;

	// ── Phase V living systems ────────────────────────────────────────────────
	bool  bEnableAmbientParticles = true;
	float ParticleDensity         = 0.3f;
	bool  bEnableAmbientSound     = true;

	// ── Quality thresholds ────────────────────────────────────────────────────
	float   MinHorrorScore          = 0.f;    // 0 = no genre requirement
	float   TargetLightingCoverage  = 0.7f;
	int32   MinActorCount           = 10;
	int32   MaxActorCount           = 500;
};

// ─────────────────────────────────────────────────────────────────────────────
//  FLevelPresetSystem — static registry + JSON I/O
// ─────────────────────────────────────────────────────────────────────────────

class UEAGENTFORGE_API FLevelPresetSystem
{
public:
	// ── Commands exposed through AgentForgeLibrary ────────────────────────────

	/** Load a named preset (built-in or from Content/AgentForge/Presets/{name}.json).
	 *  args: { "preset_name": "Horror" }
	 *  Returns: full preset JSON on success, or error. */
	static FString LoadPreset(const TSharedPtr<FJsonObject>& Args);

	/** Serialize the current preset to Content/AgentForge/Presets/{preset_name}.json.
	 *  args: preset fields (preset_name is required).
	 *  Returns: { ok, saved_path } */
	static FString SavePreset(const TSharedPtr<FJsonObject>& Args);

	/** Return all known preset names (built-in + discovered JSON files).
	 *  Returns: { presets: ["Default","Horror","SciFi","Fantasy","Military",...] } */
	static FString ListPresets();

	/** Analyse the current level + project content and recommend the best preset.
	 *  Heuristics: mesh names (Gothic_Cathedral → Horror), existing PP settings.
	 *  Returns: { suggested_preset, confidence, reasoning } */
	static FString SuggestPresetForProject();

	/** Return the active FLevelPreset serialised as JSON.
	 *  Returns: full preset JSON */
	static FString GetCurrentPreset();

	// ── Internal helpers ──────────────────────────────────────────────────────

	/** Set the active preset by name.  Returns false if not found. */
	static bool SetCurrentPreset(const FString& Name);

	/** Register all built-in presets.  Called once during module startup. */
	static void RegisterBuiltinPresets();

	/** Return the active FLevelPreset (read-only ref for pipeline phases). */
	static const FLevelPreset& GetCurrentPresetData();

	// ── Static storage (initialised by RegisterBuiltinPresets) ───────────────
	static TMap<FString, FLevelPreset> LoadedPresets;
	static FString                     CurrentPresetName;

private:
	// Serialisation helpers
	static TSharedPtr<FJsonObject> PresetToJson(const FLevelPreset& P);
	static FLevelPreset            JsonToPreset(const TSharedPtr<FJsonObject>& Json);
	static FString                 PresetDir();           // Content/AgentForge/Presets/
	static void                    ScanPresetDir();       // load JSON files from disk
};
