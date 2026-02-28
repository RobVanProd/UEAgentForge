// Copyright UEAgentForge Project. All Rights Reserved.
// LevelPresetSystem.cpp — v0.4.0 preset registry implementation.

#include "LevelPresetSystem.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  Static storage
// ─────────────────────────────────────────────────────────────────────────────
TMap<FString, FLevelPreset> FLevelPresetSystem::LoadedPresets;
FString                     FLevelPresetSystem::CurrentPresetName = TEXT("Default");

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPresetSystem::PresetDir()
{
	return FPaths::ProjectContentDir() / TEXT("AgentForge/Presets/");
}

static FString ToJson(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Serialisation
// ─────────────────────────────────────────────────────────────────────────────
TSharedPtr<FJsonObject> FLevelPresetSystem::PresetToJson(const FLevelPreset& P)
{
	TSharedPtr<FJsonObject> J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("preset_name"),             P.PresetName);
	J->SetStringField(TEXT("description"),             P.Description);
	// Phase I
	J->SetNumberField(TEXT("standard_door_width_cm"),     P.StandardDoorWidthCm);
	J->SetNumberField(TEXT("standard_ceiling_height_cm"), P.StandardCeilingHeightCm);
	J->SetNumberField(TEXT("player_eye_height_cm"),       P.PlayerEyeHeightCm);
	J->SetNumberField(TEXT("max_jump_height_cm"),         P.MaxJumpHeightCm);
	J->SetNumberField(TEXT("min_corridor_width_cm"),      P.MinCorridorWidthCm);
	// Phase II
	TArray<TSharedPtr<FJsonValue>> KitArr, MatArr;
	for (const FString& P2 : P.PreferredModularKitPaths)
		KitArr.Add(MakeShared<FJsonValueString>(P2));
	for (const FString& M : P.PreferredMaterialPaths)
		MatArr.Add(MakeShared<FJsonValueString>(M));
	J->SetArrayField(TEXT("preferred_modular_kit_paths"), KitArr);
	J->SetArrayField(TEXT("preferred_material_paths"),    MatArr);
	// Phase III
	J->SetNumberField(TEXT("set_dressing_density"),           P.SetDressingDensity);
	J->SetBoolField  (TEXT("enable_vertex_paint_weathering"), P.bEnableVertexPaintWeathering);
	// Phase IV
	TSharedPtr<FJsonObject> AmbCol = MakeShared<FJsonObject>();
	AmbCol->SetNumberField(TEXT("r"), P.AmbientLightColor.R);
	AmbCol->SetNumberField(TEXT("g"), P.AmbientLightColor.G);
	AmbCol->SetNumberField(TEXT("b"), P.AmbientLightColor.B);
	AmbCol->SetNumberField(TEXT("a"), P.AmbientLightColor.A);
	J->SetObjectField(TEXT("ambient_light_color"),          AmbCol);
	J->SetNumberField(TEXT("ambient_intensity_multiplier"), P.AmbientIntensityMultiplier);
	J->SetBoolField  (TEXT("enable_god_rays"),              P.bEnableGodRays);
	// Phase V
	J->SetBoolField  (TEXT("enable_ambient_particles"), P.bEnableAmbientParticles);
	J->SetNumberField(TEXT("particle_density"),         P.ParticleDensity);
	J->SetBoolField  (TEXT("enable_ambient_sound"),     P.bEnableAmbientSound);
	// Quality
	J->SetNumberField(TEXT("min_horror_score"),         P.MinHorrorScore);
	J->SetNumberField(TEXT("target_lighting_coverage"), P.TargetLightingCoverage);
	J->SetNumberField(TEXT("min_actor_count"),          static_cast<double>(P.MinActorCount));
	J->SetNumberField(TEXT("max_actor_count"),          static_cast<double>(P.MaxActorCount));
	return J;
}

FLevelPreset FLevelPresetSystem::JsonToPreset(const TSharedPtr<FJsonObject>& J)
{
	FLevelPreset P;
	if (!J.IsValid()) { return P; }

	J->TryGetStringField(TEXT("preset_name"),             P.PresetName);
	J->TryGetStringField(TEXT("description"),             P.Description);
	// Phase I
	J->TryGetNumberField(TEXT("standard_door_width_cm"),     P.StandardDoorWidthCm);
	J->TryGetNumberField(TEXT("standard_ceiling_height_cm"), P.StandardCeilingHeightCm);
	J->TryGetNumberField(TEXT("player_eye_height_cm"),       P.PlayerEyeHeightCm);
	J->TryGetNumberField(TEXT("max_jump_height_cm"),         P.MaxJumpHeightCm);
	J->TryGetNumberField(TEXT("min_corridor_width_cm"),      P.MinCorridorWidthCm);
	// Phase II arrays
	const TArray<TSharedPtr<FJsonValue>>* KitArr;
	if (J->TryGetArrayField(TEXT("preferred_modular_kit_paths"), KitArr))
		for (const auto& V : *KitArr) { FString S; V->TryGetString(S); P.PreferredModularKitPaths.Add(S); }
	const TArray<TSharedPtr<FJsonValue>>* MatArr;
	if (J->TryGetArrayField(TEXT("preferred_material_paths"), MatArr))
		for (const auto& V : *MatArr) { FString S; V->TryGetString(S); P.PreferredMaterialPaths.Add(S); }
	// Phase III
	J->TryGetNumberField(TEXT("set_dressing_density"),           P.SetDressingDensity);
	J->TryGetBoolField  (TEXT("enable_vertex_paint_weathering"), P.bEnableVertexPaintWeathering);
	// Phase IV
	const TSharedPtr<FJsonObject>* AmbPtr;
	if (J->TryGetObjectField(TEXT("ambient_light_color"), AmbPtr))
	{
		(*AmbPtr)->TryGetNumberField(TEXT("r"), P.AmbientLightColor.R);
		(*AmbPtr)->TryGetNumberField(TEXT("g"), P.AmbientLightColor.G);
		(*AmbPtr)->TryGetNumberField(TEXT("b"), P.AmbientLightColor.B);
		(*AmbPtr)->TryGetNumberField(TEXT("a"), P.AmbientLightColor.A);
	}
	J->TryGetNumberField(TEXT("ambient_intensity_multiplier"), P.AmbientIntensityMultiplier);
	J->TryGetBoolField  (TEXT("enable_god_rays"),              P.bEnableGodRays);
	// Phase V
	J->TryGetBoolField  (TEXT("enable_ambient_particles"), P.bEnableAmbientParticles);
	J->TryGetNumberField(TEXT("particle_density"),         P.ParticleDensity);
	J->TryGetBoolField  (TEXT("enable_ambient_sound"),     P.bEnableAmbientSound);
	// Quality
	J->TryGetNumberField(TEXT("min_horror_score"),         P.MinHorrorScore);
	J->TryGetNumberField(TEXT("target_lighting_coverage"), P.TargetLightingCoverage);
	double MinA = P.MinActorCount, MaxA = P.MaxActorCount;
	J->TryGetNumberField(TEXT("min_actor_count"), MinA);
	J->TryGetNumberField(TEXT("max_actor_count"), MaxA);
	P.MinActorCount = static_cast<int32>(MinA);
	P.MaxActorCount = static_cast<int32>(MaxA);
	return P;
}

// ─────────────────────────────────────────────────────────────────────────────
//  RegisterBuiltinPresets — called once at module startup
// ─────────────────────────────────────────────────────────────────────────────
void FLevelPresetSystem::RegisterBuiltinPresets()
{
	// ── Default ──────────────────────────────────────────────────────────────
	{
		FLevelPreset P;
		P.PresetName                  = TEXT("Default");
		P.Description                 = TEXT("Neutral starting point for any genre.");
		P.AmbientLightColor           = FLinearColor(0.15f, 0.15f, 0.15f, 1.f);
		P.AmbientIntensityMultiplier  = 1.0f;
		P.SetDressingDensity          = 0.4f;
		P.ParticleDensity             = 0.2f;
		P.MinHorrorScore              = 0.f;
		P.TargetLightingCoverage      = 0.7f;
		P.MinActorCount               = 10;
		P.MaxActorCount               = 500;
		LoadedPresets.Add(TEXT("Default"), P);
	}

	// ── Horror ───────────────────────────────────────────────────────────────
	{
		FLevelPreset P;
		P.PresetName                  = TEXT("Horror");
		P.Description                 = TEXT("Dark survival horror — oppressive lighting, high particle density, optional god rays.");
		P.StandardCeilingHeightCm     = 280.f;
		P.MinCorridorWidthCm          = 130.f;
		P.AmbientLightColor           = FLinearColor(0.04f, 0.04f, 0.08f, 1.f);
		P.AmbientIntensityMultiplier  = 0.6f;
		P.bEnableGodRays              = true;
		P.SetDressingDensity          = 0.75f;
		P.bEnableVertexPaintWeathering = true;
		P.bEnableAmbientParticles     = true;
		P.ParticleDensity             = 0.6f;
		P.bEnableAmbientSound         = true;
		P.MinHorrorScore              = 55.f;
		P.TargetLightingCoverage      = 0.5f;
		P.MinActorCount               = 20;
		P.MaxActorCount               = 400;
		P.PreferredModularKitPaths.Add(TEXT("/Game/Gothic_Cathedral/Meshes/"));
		P.PreferredMaterialPaths.Add(TEXT("/Game/Gothic_Cathedral/Materials/"));
		LoadedPresets.Add(TEXT("Horror"), P);
	}

	// ── SciFi ─────────────────────────────────────────────────────────────────
	{
		FLevelPreset P;
		P.PresetName                  = TEXT("SciFi");
		P.Description                 = TEXT("Clean technological spaces — cool blue ambient, glow particles, minimal weathering.");
		P.StandardCeilingHeightCm     = 350.f;
		P.StandardDoorWidthCm         = 220.f;
		P.AmbientLightColor           = FLinearColor(0.05f, 0.1f, 0.25f, 1.f);
		P.AmbientIntensityMultiplier  = 1.2f;
		P.bEnableGodRays              = false;
		P.SetDressingDensity          = 0.35f;
		P.bEnableVertexPaintWeathering = false;
		P.bEnableAmbientParticles     = true;
		P.ParticleDensity             = 0.25f;
		P.bEnableAmbientSound         = true;
		P.MinHorrorScore              = 0.f;
		P.TargetLightingCoverage      = 0.8f;
		P.MinActorCount               = 15;
		P.MaxActorCount               = 500;
		P.PreferredModularKitPaths.Add(TEXT("/Game/SciFi/Meshes/"));
		LoadedPresets.Add(TEXT("SciFi"), P);
	}

	// ── Fantasy ───────────────────────────────────────────────────────────────
	{
		FLevelPreset P;
		P.PresetName                  = TEXT("Fantasy");
		P.Description                 = TEXT("Warm golden atmosphere — high ambient, rich set dressing, nature particles.");
		P.StandardCeilingHeightCm     = 400.f;
		P.AmbientLightColor           = FLinearColor(0.25f, 0.18f, 0.07f, 1.f);
		P.AmbientIntensityMultiplier  = 1.4f;
		P.bEnableGodRays              = true;
		P.SetDressingDensity          = 0.8f;
		P.bEnableVertexPaintWeathering = true;
		P.bEnableAmbientParticles     = true;
		P.ParticleDensity             = 0.5f;
		P.bEnableAmbientSound         = true;
		P.MinHorrorScore              = 0.f;
		P.TargetLightingCoverage      = 0.85f;
		P.MinActorCount               = 25;
		P.MaxActorCount               = 600;
		P.PreferredModularKitPaths.Add(TEXT("/Game/Fantasy/Meshes/"));
		LoadedPresets.Add(TEXT("Fantasy"), P);
	}

	// ── Military ──────────────────────────────────────────────────────────────
	{
		FLevelPreset P;
		P.PresetName                  = TEXT("Military");
		P.Description                 = TEXT("WW2 / modern military — olive/grey ambient, sparse prop density, functional corridors.");
		P.StandardCeilingHeightCm     = 250.f;
		P.StandardDoorWidthCm         = 180.f;
		P.MinCorridorWidthCm          = 160.f;
		P.AmbientLightColor           = FLinearColor(0.07f, 0.09f, 0.06f, 1.f);
		P.AmbientIntensityMultiplier  = 0.85f;
		P.bEnableGodRays              = false;
		P.SetDressingDensity          = 0.3f;
		P.bEnableVertexPaintWeathering = true;
		P.bEnableAmbientParticles     = true;
		P.ParticleDensity             = 0.35f;
		P.bEnableAmbientSound         = true;
		P.MinHorrorScore              = 0.f;
		P.TargetLightingCoverage      = 0.6f;
		P.MinActorCount               = 15;
		P.MaxActorCount               = 450;
		P.PreferredModularKitPaths.Add(TEXT("/Game/Military/Meshes/"));
		LoadedPresets.Add(TEXT("Military"), P);
	}

	// Overlay any JSON presets found on disk.
	ScanPresetDir();
}

// ─────────────────────────────────────────────────────────────────────────────
void FLevelPresetSystem::ScanPresetDir()
{
	const FString Dir = PresetDir();
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Dir + TEXT("*.json")), true, false);
	for (const FString& File : Files)
	{
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *(Dir + File))) { continue; }
		TSharedPtr<FJsonObject> J;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(R, J) || !J.IsValid()) { continue; }
		FLevelPreset P = JsonToPreset(J);
		if (!P.PresetName.IsEmpty())
		{
			LoadedPresets.Add(P.PresetName, P);
		}
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  SetCurrentPreset
// ─────────────────────────────────────────────────────────────────────────────
bool FLevelPresetSystem::SetCurrentPreset(const FString& Name)
{
	if (LoadedPresets.Contains(Name))
	{
		CurrentPresetName = Name;
		return true;
	}
	return false;
}

const FLevelPreset& FLevelPresetSystem::GetCurrentPresetData()
{
	static FLevelPreset FallbackDefault;
	const FLevelPreset* Found = LoadedPresets.Find(CurrentPresetName);
	return Found ? *Found : FallbackDefault;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public command implementations
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPresetSystem::LoadPreset(const TSharedPtr<FJsonObject>& Args)
{
	FString Name;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("preset_name"), Name) || Name.IsEmpty())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("preset_name argument is required."));
		return ToJson(Err);
	}

	// Ensure built-ins are available (idempotent if already registered).
	if (LoadedPresets.Num() == 0) { RegisterBuiltinPresets(); }

	// Try JSON file first.
	const FString FilePath = PresetDir() + Name + TEXT(".json");
	if (IFileManager::Get().FileExists(*FilePath))
	{
		FString JsonStr;
		if (FFileHelper::LoadFileToString(JsonStr, *FilePath))
		{
			TSharedPtr<FJsonObject> J;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(JsonStr);
			if (FJsonSerializer::Deserialize(R, J) && J.IsValid())
			{
				FLevelPreset P = JsonToPreset(J);
				LoadedPresets.Add(P.PresetName, P);
				CurrentPresetName = P.PresetName;
				return ToJson(PresetToJson(P));
			}
		}
	}

	// Check in-memory registry.
	if (LoadedPresets.Contains(Name))
	{
		CurrentPresetName = Name;
		return ToJson(PresetToJson(LoadedPresets[Name]));
	}

	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Preset '%s' not found."), *Name));
	return ToJson(Err);
}

FString FLevelPresetSystem::SavePreset(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("No arguments provided."));
		return ToJson(Err);
	}

	// Build preset from args.
	FLevelPreset P;
	Args->TryGetStringField(TEXT("preset_name"), P.PresetName);
	Args->TryGetStringField(TEXT("description"), P.Description);
	if (P.PresetName.IsEmpty())
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("preset_name is required."));
		return ToJson(Err);
	}

	// Allow partial overrides on top of an existing preset.
	if (LoadedPresets.Contains(P.PresetName)) { P = LoadedPresets[P.PresetName]; }
	// Re-read name/desc after potential clobber.
	Args->TryGetStringField(TEXT("preset_name"), P.PresetName);
	Args->TryGetStringField(TEXT("description"), P.Description);
	// Numeric fields
	Args->TryGetNumberField(TEXT("standard_ceiling_height_cm"), P.StandardCeilingHeightCm);
	Args->TryGetNumberField(TEXT("standard_door_width_cm"),     P.StandardDoorWidthCm);
	Args->TryGetNumberField(TEXT("player_eye_height_cm"),       P.PlayerEyeHeightCm);
	Args->TryGetNumberField(TEXT("max_jump_height_cm"),         P.MaxJumpHeightCm);
	Args->TryGetNumberField(TEXT("min_corridor_width_cm"),      P.MinCorridorWidthCm);
	Args->TryGetNumberField(TEXT("set_dressing_density"),       P.SetDressingDensity);
	Args->TryGetNumberField(TEXT("ambient_intensity_multiplier"), P.AmbientIntensityMultiplier);
	Args->TryGetNumberField(TEXT("particle_density"),           P.ParticleDensity);
	Args->TryGetNumberField(TEXT("min_horror_score"),           P.MinHorrorScore);
	Args->TryGetNumberField(TEXT("target_lighting_coverage"),   P.TargetLightingCoverage);
	double MinA = P.MinActorCount, MaxA = P.MaxActorCount;
	Args->TryGetNumberField(TEXT("min_actor_count"), MinA);
	Args->TryGetNumberField(TEXT("max_actor_count"), MaxA);
	P.MinActorCount = static_cast<int32>(MinA);
	P.MaxActorCount = static_cast<int32>(MaxA);
	// Bool fields
	Args->TryGetBoolField(TEXT("enable_god_rays"),                P.bEnableGodRays);
	Args->TryGetBoolField(TEXT("enable_ambient_particles"),       P.bEnableAmbientParticles);
	Args->TryGetBoolField(TEXT("enable_ambient_sound"),           P.bEnableAmbientSound);
	Args->TryGetBoolField(TEXT("enable_vertex_paint_weathering"), P.bEnableVertexPaintWeathering);
	// Ambient color
	const TSharedPtr<FJsonObject>* AmbPtr;
	if (Args->TryGetObjectField(TEXT("ambient_light_color"), AmbPtr))
	{
		(*AmbPtr)->TryGetNumberField(TEXT("r"), P.AmbientLightColor.R);
		(*AmbPtr)->TryGetNumberField(TEXT("g"), P.AmbientLightColor.G);
		(*AmbPtr)->TryGetNumberField(TEXT("b"), P.AmbientLightColor.B);
		(*AmbPtr)->TryGetNumberField(TEXT("a"), P.AmbientLightColor.A);
	}
	// Kit paths
	const TArray<TSharedPtr<FJsonValue>>* KitArr;
	if (Args->TryGetArrayField(TEXT("preferred_modular_kit_paths"), KitArr))
	{
		P.PreferredModularKitPaths.Empty();
		for (const auto& V : *KitArr) { FString S; V->TryGetString(S); P.PreferredModularKitPaths.Add(S); }
	}

	// Persist to disk.
	const FString Dir = PresetDir();
	IFileManager::Get().MakeDirectory(*Dir, /*Tree=*/true);
	const FString FilePath = Dir + P.PresetName + TEXT(".json");
	const FString JsonStr  = ToJson(PresetToJson(P));
	FFileHelper::SaveStringToFile(JsonStr, *FilePath);

	LoadedPresets.Add(P.PresetName, P);
	CurrentPresetName = P.PresetName;

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),         true);
	Resp->SetStringField(TEXT("preset_name"), P.PresetName);
	Resp->SetStringField(TEXT("saved_path"),  FilePath);
	return ToJson(Resp);
}

FString FLevelPresetSystem::ListPresets()
{
	if (LoadedPresets.Num() == 0) { RegisterBuiltinPresets(); }
	ScanPresetDir();

	TArray<TSharedPtr<FJsonValue>> Names;
	for (const auto& KV : LoadedPresets)
		Names.Add(MakeShared<FJsonValueString>(KV.Key));

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetArrayField(TEXT("presets"), Names);
	Resp->SetNumberField(TEXT("count"), static_cast<double>(Names.Num()));
	return ToJson(Resp);
}

FString FLevelPresetSystem::SuggestPresetForProject()
{
#if WITH_EDITOR
	if (LoadedPresets.Num() == 0) { RegisterBuiltinPresets(); }

	FString SuggestedName = TEXT("Default");
	FString Reasoning;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), TEXT("No editor world available."));
		return ToJson(Err);
	}

	// Scan actor labels for genre hints.
	int32 GothicHints   = 0;
	int32 SciFiHints    = 0;
	int32 FantasyHints  = 0;
	int32 MilitaryHints = 0;

	static const TArray<FString> GothicTokens   = { TEXT("gothic"), TEXT("cathedral"), TEXT("church"), TEXT("chapel"), TEXT("crypt"), TEXT("asylum"), TEXT("warden") };
	static const TArray<FString> SciFiTokens    = { TEXT("scifi"), TEXT("sci_fi"), TEXT("station"), TEXT("reactor"), TEXT("corridor"), TEXT("tech"), TEXT("space") };
	static const TArray<FString> FantasyTokens  = { TEXT("fantasy"), TEXT("castle"), TEXT("dungeon"), TEXT("elven"), TEXT("magic"), TEXT("ruin"), TEXT("forest") };
	static const TArray<FString> MilitaryTokens = { TEXT("military"), TEXT("bunker"), TEXT("trench"), TEXT("barracks"), TEXT("ww2"), TEXT("armory") };

	auto CountHints = [](const FString& Label, const TArray<FString>& Tokens) -> int32
	{
		int32 Count = 0;
		const FString LabelLower = Label.ToLower();
		for (const FString& T : Tokens)
			if (LabelLower.Contains(T)) { ++Count; }
		return Count;
	};

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (!IsValid(*It)) { continue; }
		const FString Label = (*It)->GetActorLabel().ToLower();
		GothicHints   += CountHints(Label, GothicTokens);
		SciFiHints    += CountHints(Label, SciFiTokens);
		FantasyHints  += CountHints(Label, FantasyTokens);
		MilitaryHints += CountHints(Label, MilitaryTokens);
	}

	// Scan asset registry for content folder names.
#if WITH_EDITOR
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FString> Paths;
	ARM.Get().GetSubPaths(TEXT("/Game"), Paths, true);
	for (const FString& Path : Paths)
	{
		const FString P = Path.ToLower();
		if (P.Contains(TEXT("gothic")) || P.Contains(TEXT("cathedral")) || P.Contains(TEXT("horror"))) { GothicHints   += 3; }
		if (P.Contains(TEXT("scifi"))  || P.Contains(TEXT("sci_fi"))    || P.Contains(TEXT("station"))) { SciFiHints    += 3; }
		if (P.Contains(TEXT("fantasy")) || P.Contains(TEXT("castle")))                                   { FantasyHints  += 3; }
		if (P.Contains(TEXT("military")) || P.Contains(TEXT("bunker")))                                  { MilitaryHints += 3; }
	}
#endif

	int32 MaxHints = 0;
	if (GothicHints   > MaxHints) { MaxHints = GothicHints;   SuggestedName = TEXT("Horror"); }
	if (SciFiHints    > MaxHints) { MaxHints = SciFiHints;    SuggestedName = TEXT("SciFi"); }
	if (FantasyHints  > MaxHints) { MaxHints = FantasyHints;  SuggestedName = TEXT("Fantasy"); }
	if (MilitaryHints > MaxHints) { MaxHints = MilitaryHints; SuggestedName = TEXT("Military"); }

	Reasoning = FString::Printf(
		TEXT("Gothic/Horror hints=%d, SciFi hints=%d, Fantasy hints=%d, Military hints=%d — highest match: %s"),
		GothicHints, SciFiHints, FantasyHints, MilitaryHints, *SuggestedName);

	float Confidence = MaxHints > 0 ? FMath::Clamp(static_cast<float>(MaxHints) / 10.f, 0.1f, 1.f) : 0.0f;

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetStringField(TEXT("suggested_preset"), SuggestedName);
	Resp->SetNumberField(TEXT("confidence"),       Confidence);
	Resp->SetStringField(TEXT("reasoning"),        Reasoning);
	return ToJson(Resp);
#else
	TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
	Err->SetStringField(TEXT("error"), TEXT("WITH_EDITOR required."));
	return ToJson(Err);
#endif
}

FString FLevelPresetSystem::GetCurrentPreset()
{
	if (LoadedPresets.Num() == 0) { RegisterBuiltinPresets(); }
	const FLevelPreset* Found = LoadedPresets.Find(CurrentPresetName);
	if (!Found)
	{
		TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
		Err->SetStringField(TEXT("error"), FString::Printf(TEXT("No preset loaded. Current name: %s"), *CurrentPresetName));
		return ToJson(Err);
	}
	return ToJson(PresetToJson(*Found));
}
