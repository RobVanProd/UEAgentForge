// Copyright UEAgentForge Project. All Rights Reserved.

#include "SemanticCommandModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/LightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#endif


// ─── Shared JSON helpers ──────────────────────────────────────────────────────

static FString ToJson(const TSharedPtr<FJsonObject>& O)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return Out;
}

static FString ErrResp(const FString& Msg)
{
	auto O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), false);
	O->SetStringField(TEXT("error"), Msg);
	return ToJson(O);
}

static TSharedPtr<FJsonObject> VecObj(const FVector& V)
{
	auto O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("x"), V.X);
	O->SetNumberField(TEXT("y"), V.Y);
	O->SetNumberField(TEXT("z"), V.Z);
	return O;
}


// ─── PlaceAssetThematically ───────────────────────────────────────────────────

FString FSemanticCommandModule::PlaceAssetThematically(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args || !Args->HasField(TEXT("class_path")))
	{
		return ErrResp(TEXT("args.class_path required"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	const FString ClassPath = Args->GetStringField(TEXT("class_path"));
	const int32   Count     = Args->HasField(TEXT("count"))
	                        ? (int32)Args->GetNumberField(TEXT("count")) : 3;
	const FString LabelPfx  = Args->HasField(TEXT("label_prefix"))
	                        ? Args->GetStringField(TEXT("label_prefix")) : TEXT("Themed");

	// Parse theme rules.
	bool  bPreferDark     = true;
	bool  bPreferCorners  = true;
	bool  bPreferOccluded = false;
	float MinSpacing      = 300.f;

	if (Args->HasField(TEXT("theme_rules")))
	{
		TSharedPtr<FJsonObject> TR = Args->GetObjectField(TEXT("theme_rules"));
		if (TR->HasField(TEXT("prefer_dark")))     { bPreferDark     = TR->GetBoolField(TEXT("prefer_dark")); }
		if (TR->HasField(TEXT("prefer_corners")))  { bPreferCorners  = TR->GetBoolField(TEXT("prefer_corners")); }
		if (TR->HasField(TEXT("prefer_occluded"))) { bPreferOccluded = TR->GetBoolField(TEXT("prefer_occluded")); }
		if (TR->HasField(TEXT("min_spacing")))     { MinSpacing      = (float)TR->GetNumberField(TEXT("min_spacing")); }
	}

	// Reference area.
	FVector AreaCentre = FVector::ZeroVector;
	float   AreaRadius = 5000.f;
	if (Args->HasField(TEXT("reference_area")))
	{
		TSharedPtr<FJsonObject> RA = Args->GetObjectField(TEXT("reference_area"));
		AreaCentre.X = RA->HasField(TEXT("x")) ? (float)RA->GetNumberField(TEXT("x")) : 0.f;
		AreaCentre.Y = RA->HasField(TEXT("y")) ? (float)RA->GetNumberField(TEXT("y")) : 0.f;
		AreaCentre.Z = RA->HasField(TEXT("z")) ? (float)RA->GetNumberField(TEXT("z")) : 0.f;
		AreaRadius   = RA->HasField(TEXT("radius")) ? (float)RA->GetNumberField(TEXT("radius")) : 5000.f;
	}
	else
	{
		// Default: use level bounding box centre.
		FBox LevelBox(EForceInit::ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!(*It)->IsA<AWorldSettings>())
			{
				FVector O, E; (*It)->GetActorBounds(false, O, E);
				LevelBox += FBox(O - E, O + E);
			}
		}
		if (LevelBox.IsValid) { AreaCentre = LevelBox.GetCenter(); }
	}

	// Load spawn class.
	UClass* SpawnClass = LoadObject<UClass>(nullptr, *ClassPath);
	if (!SpawnClass) { SpawnClass = AStaticMeshActor::StaticClass(); }

	// Generate candidate positions.
	TArray<FVector> Candidates = FindDarkCorners(World, AreaCentre, AreaRadius, Count * 10);

	// Score each candidate.
	TArray<TPair<float, FVector>> Scored;
	for (const FVector& Pos : Candidates)
	{
		float Score = 0.f;
		if (bPreferDark)     { Score += EstimateDarknessAt(World, Pos, 1000.f); }
		if (bPreferOccluded) { Score += IsOccluded(World, Pos, AreaCentre) ? 30.f : 0.f; }
		Scored.Add({ Score, Pos });
	}
	Scored.Sort([](const TPair<float,FVector>& A, const TPair<float,FVector>& B)
		{ return A.Key > B.Key; });

	// Spawn at top-scoring positions, enforcing min spacing.
	FScopedTransaction Transaction(FText::FromString(TEXT("PlaceAssetThematically")));

	TArray<TSharedPtr<FJsonValue>> SpawnedArr;
	TArray<FVector> PlacedLocations;
	FString Reasoning;
	int32 PlacedCount = 0;

	for (const TPair<float,FVector>& Candidate : Scored)
	{
		if (PlacedCount >= Count) { break; }

		const FVector& CandPos = Candidate.Value;

		// Enforce spacing.
		bool bTooClose = false;
		for (const FVector& Placed : PlacedLocations)
		{
			if (FVector::Dist(Placed, CandPos) < MinSpacing) { bTooClose = true; break; }
		}
		if (bTooClose) { continue; }

		// Snap to surface via downward trace.
		FHitResult Hit;
		FCollisionQueryParams Params;
		FVector TraceStart = CandPos + FVector(0, 0, 200.f);
		FVector TraceEnd   = CandPos - FVector(0, 0, 2000.f);
		FVector SpawnLoc   = CandPos;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
		{
			SpawnLoc = Hit.ImpactPoint;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AActor* Spawned = World->SpawnActor<AActor>(SpawnClass,
			FTransform(FQuat::Identity, SpawnLoc), SpawnParams);
		if (!Spawned) { continue; }

		const FString Label = FString::Printf(TEXT("%s_%02d"), *LabelPfx, PlacedCount + 1);
		Spawned->SetActorLabel(Label);
		PlacedLocations.Add(SpawnLoc);
		++PlacedCount;

		auto AO = MakeShared<FJsonObject>();
		AO->SetStringField(TEXT("label"),    Label);
		AO->SetObjectField(TEXT("location"), VecObj(SpawnLoc));
		AO->SetNumberField(TEXT("score"),    Candidate.Key);
		SpawnedArr.Add(MakeShared<FJsonValueObject>(AO));
	}

	// Build reasoning string.
	TArray<FString> ReasonParts;
	if (bPreferDark)     { ReasonParts.Add(TEXT("preferred dark areas")); }
	if (bPreferCorners)  { ReasonParts.Add(TEXT("preferred wall-adjacent positions")); }
	if (bPreferOccluded) { ReasonParts.Add(TEXT("preferred occluded spots")); }
	Reasoning = FString::Join(ReasonParts, TEXT(", "));

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetNumberField(TEXT("placed_count"), PlacedCount);
	Root->SetArrayField(TEXT("actors"), SpawnedArr);
	Root->SetStringField(TEXT("placement_reasoning"), Reasoning);
	return ToJson(Root);
#else
	return ErrResp(TEXT("PlaceAssetThematically requires WITH_EDITOR"));
#endif
}


// ─── RefineLevelSection ───────────────────────────────────────────────────────

FString FSemanticCommandModule::RefineLevelSection(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	const FString Desc       = Args && Args->HasField(TEXT("description"))
	                         ? Args->GetStringField(TEXT("description")) : TEXT("improve atmosphere");
	const int32   MaxIter    = Args && Args->HasField(TEXT("max_iterations"))
	                         ? (int32)Args->GetNumberField(TEXT("max_iterations")) : 3;
	const FString ClassPath  = Args && Args->HasField(TEXT("class_path"))
	                         ? Args->GetStringField(TEXT("class_path"))
	                         : TEXT("/Script/Engine.StaticMeshActor");

	FVector AreaCentre = FVector::ZeroVector;
	float   AreaRadius = 3000.f;
	if (Args && Args->HasField(TEXT("target_area")))
	{
		TSharedPtr<FJsonObject> TA = Args->GetObjectField(TEXT("target_area"));
		AreaCentre.X = (float)TA->GetNumberField(TEXT("x"));
		AreaCentre.Y = (float)TA->GetNumberField(TEXT("y"));
		AreaCentre.Z = (float)TA->GetNumberField(TEXT("z"));
		AreaRadius   = TA->HasField(TEXT("radius"))
		             ? (float)TA->GetNumberField(TEXT("radius")) : 3000.f;
	}

	TArray<TSharedPtr<FJsonValue>> ActionLog;
	int32  IterationsRun    = 0;
	float  FinalDensity     = 0.f;

	// Quality target: 2 static mesh props per 100 m² in the target area.
	const float TargetDensity = 2.f;

	for (int32 Iter = 0; Iter < MaxIter; ++Iter)
	{
		++IterationsRun;

		// Count static mesh actors in area.
		int32 InAreaCount = 0;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			if (FVector::Dist((*It)->GetActorLocation(), AreaCentre) < AreaRadius)
			{
				++InAreaCount;
			}
		}
		const float AreaM2  = PI * (AreaRadius / 100.f) * (AreaRadius / 100.f);
		FinalDensity        = InAreaCount / FMath::Max(AreaM2, 1.f);

		if (FinalDensity >= TargetDensity)
		{
			ActionLog.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("Iter %d: Target density %.2f met — done"), Iter+1, TargetDensity)));
			break;
		}

		// Add props via PlaceAssetThematically.
		auto SubArgs = MakeShared<FJsonObject>();
		SubArgs->SetStringField(TEXT("class_path"), ClassPath);
		SubArgs->SetNumberField(TEXT("count"), 3);
		auto RefArea = MakeShared<FJsonObject>();
		RefArea->SetNumberField(TEXT("x"), AreaCentre.X);
		RefArea->SetNumberField(TEXT("y"), AreaCentre.Y);
		RefArea->SetNumberField(TEXT("z"), AreaCentre.Z);
		RefArea->SetNumberField(TEXT("radius"), AreaRadius);
		SubArgs->SetObjectField(TEXT("reference_area"), RefArea);
		auto ThemeRules = MakeShared<FJsonObject>();
		ThemeRules->SetBoolField(TEXT("prefer_dark"),    true);
		ThemeRules->SetBoolField(TEXT("prefer_corners"), true);
		SubArgs->SetObjectField(TEXT("theme_rules"), ThemeRules);
		SubArgs->SetStringField(TEXT("label_prefix"), FString::Printf(TEXT("Refined_Iter%d"), Iter+1));

		FString PlaceResult = PlaceAssetThematically(SubArgs);
		ActionLog.Add(MakeShared<FJsonValueString>(
			FString::Printf(TEXT("Iter %d: Placed props. Density=%.3f/m2 (target %.2f)"),
			                Iter + 1, FinalDensity, TargetDensity)));
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("description"), Desc);
	Root->SetNumberField(TEXT("iterations_run"), IterationsRun);
	Root->SetNumberField(TEXT("final_density_score"), FinalDensity);
	Root->SetArrayField(TEXT("actions_taken"), ActionLog);
	Root->SetStringField(TEXT("detail"),
		FString::Printf(TEXT("Final density: %.3f props/m2 in r=%.0f cm area"), FinalDensity, AreaRadius));
	return ToJson(Root);
#else
	return ErrResp(TEXT("RefineLevelSection requires WITH_EDITOR"));
#endif
}


// ─── ApplyGenreRules ─────────────────────────────────────────────────────────

FString FSemanticCommandModule::ApplyGenreRules(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args || !Args->HasField(TEXT("genre")))
	{
		return ErrResp(TEXT("args.genre required (horror|dark|thriller|neutral)"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	const FString Genre   = Args->GetStringField(TEXT("genre")).ToLower();
	const float Intensity = Args->HasField(TEXT("intensity"))
	                      ? FMath::Clamp((float)Args->GetNumberField(TEXT("intensity")), 0.f, 1.f)
	                      : 1.f;

	// Genre presets.
	struct FGenrePreset
	{
		float LightMultiplier;   // multiply existing point light intensity
		float Vignette;
		float Grain;
		float ExposureBias;
		float FogDensityMult;    // multiply existing fog density
		float CRTWeight;         // post-process blendable weight
	};

	FGenrePreset Preset;
	if      (Genre == TEXT("horror"))   { Preset = { 0.40f, 0.85f, 0.45f, -0.8f, 1.50f, 0.40f }; }
	else if (Genre == TEXT("dark"))     { Preset = { 0.60f, 0.65f, 0.25f, -0.5f, 1.30f, 0.15f }; }
	else if (Genre == TEXT("thriller")) { Preset = { 0.70f, 0.55f, 0.15f, -0.3f, 1.00f, 0.00f }; }
	else if (Genre == TEXT("neutral"))  { Preset = { 1.00f, 0.40f, 0.05f,  0.0f, 1.00f, 0.00f }; }
	else { return ErrResp(FString::Printf(TEXT("Unknown genre '%s'"), *Genre)); }

	// Blend with intensity.
	FGenrePreset Current = { 1.f, 0.4f, 0.05f, 0.f, 1.f, 0.f };  // neutral baseline
	auto Blend = [Intensity](float Base, float Target) -> float
		{ return FMath::Lerp(Base, Target, Intensity); };

	const float LightMult    = Blend(Current.LightMultiplier, Preset.LightMultiplier);
	const float VignetteVal  = Blend(Current.Vignette,        Preset.Vignette);
	const float GrainVal     = Blend(Current.Grain,           Preset.Grain);
	const float ExposureVal  = Blend(Current.ExposureBias,    Preset.ExposureBias);
	const float FogMult      = Blend(Current.FogDensityMult,  Preset.FogDensityMult);
	const float CRTWeightVal = Blend(Current.CRTWeight,       Preset.CRTWeight);

	FScopedTransaction Transaction(FText::FromString(TEXT("ApplyGenreRules")));
	TArray<TSharedPtr<FJsonValue>> Changes;
	int32 LightsModified = 0;
	bool  bPPModified    = false;

	// Modify point/spot lights.
	for (TActorIterator<ALight> It(World); It; ++It)
	{
		if ((*It)->IsA<ADirectionalLight>() || (*It)->IsA<ASkyLight>()) { continue; }
		if (ULightComponent* LC = (*It)->GetLightComponent())
		{
			(*It)->Modify();
			LC->SetIntensity(LC->Intensity * LightMult);
			++LightsModified;
		}
	}
	Changes.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Modified %d point/spot lights (x%.2f intensity)"),
		                LightsModified, LightMult)));

	// Modify first PostProcessVolume.
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		(*It)->Modify();
		FPostProcessSettings& S = (*It)->Settings;

		S.bOverride_VignetteIntensity  = true;  S.VignetteIntensity  = VignetteVal;
		S.bOverride_FilmGrainIntensity = true;  S.FilmGrainIntensity = GrainVal;
		S.bOverride_AutoExposureBias   = true;  S.AutoExposureBias   = ExposureVal;

		// Set CRT blendable weight if a blendable exists.
		for (FWeightedBlendable& WB : S.WeightedBlendables.Array)
		{
			if (WB.Object) { WB.Weight = CRTWeightVal; }
		}

		(*It)->GetOutermost()->MarkPackageDirty();
		bPPModified = true;
		Changes.Add(MakeShared<FJsonValueString>(
			FString::Printf(TEXT("PP: vignette=%.2f grain=%.2f exposure=%.2f crt=%.2f"),
			                VignetteVal, GrainVal, ExposureVal, CRTWeightVal)));
		break;
	}

	// Modify fog density.
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* FogComp = (*It)->GetComponent())
		{
			(*It)->Modify();
			FogComp->FogDensity = FogComp->FogDensity * FogMult;
			(*It)->GetOutermost()->MarkPackageDirty();
			Changes.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("Fog density x%.2f"), FogMult)));
		}
		break;
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("genre"), Genre);
	Root->SetNumberField(TEXT("intensity"), Intensity);
	Root->SetNumberField(TEXT("lights_modified"), LightsModified);
	Root->SetBoolField(TEXT("pp_modified"), bPPModified);
	Root->SetArrayField(TEXT("changes_applied"), Changes);
	return ToJson(Root);
#else
	return ErrResp(TEXT("ApplyGenreRules requires WITH_EDITOR"));
#endif
}


// ─── CreateInEditorAsset (stub) ───────────────────────────────────────────────

FString FSemanticCommandModule::CreateInEditorAsset(const TSharedPtr<FJsonObject>& Args)
{
	const FString Type = Args && Args->HasField(TEXT("type"))
	                   ? Args->GetStringField(TEXT("type")) : TEXT("StaticMesh");
	const FString Desc = Args && Args->HasField(TEXT("description"))
	                   ? Args->GetStringField(TEXT("description")) : TEXT("");

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), false);
	Root->SetStringField(TEXT("message"),
		TEXT("In-editor asset creation requires Geometry Script (UE 5.0+) or Modeling Tools. ")
		TEXT("This command is a stub — the full implementation is roadmapped for v0.4.0."));
	Root->SetStringField(TEXT("requested_type"), Type);
	Root->SetStringField(TEXT("requested_description"), Desc);
	Root->SetStringField(TEXT("workaround"),
		TEXT("1) Use Modeling Tools in editor (Shift+5). "
		     "2) Import FBX via import_local_asset. "
		     "3) Use Geometry Script in a Blueprint construction script."));
	Root->SetStringField(TEXT("recommended_approach"),
		TEXT("For horror game props: import_local_asset + spawn_actor_at_surface "
		     "is the current recommended path for custom geometry."));
	return ToJson(Root);
}


// ─── Private helpers ──────────────────────────────────────────────────────────

TArray<FVector> FSemanticCommandModule::FindDarkCorners(UWorld* World, const FVector& Centre,
                                                         float Radius, int32 MaxCandidates)
{
	TArray<FVector> Candidates;
	const int32 GridSteps = FMath::CeilToInt(FMath::Sqrt((float)MaxCandidates));
	const float Step      = (Radius * 2.f) / GridSteps;

	for (int32 IX = 0; IX < GridSteps && Candidates.Num() < MaxCandidates; ++IX)
	{
		for (int32 IY = 0; IY < GridSteps && Candidates.Num() < MaxCandidates; ++IY)
		{
			const float X = Centre.X - Radius + IX * Step + Step * 0.5f;
			const float Y = Centre.Y - Radius + IY * Step + Step * 0.5f;
			FVector Candidate(X, Y, Centre.Z);

			// Only include if within radius.
			if (FVector::Dist2D(Candidate, Centre) > Radius) { continue; }

			// Snap to floor.
			FHitResult Hit;
			FCollisionQueryParams Params;
			if (World->LineTraceSingleByChannel(Hit,
				Candidate + FVector(0, 0, 500.f),
				Candidate - FVector(0, 0, 2000.f),
				ECC_WorldStatic, Params))
			{
				Candidates.Add(Hit.ImpactPoint + FVector(0, 0, 5.f));
			}
		}
	}
	return Candidates;
}

bool FSemanticCommandModule::IsOccluded(UWorld* World, const FVector& Position,
                                         const FVector& LevelCentre)
{
	FHitResult Hit;
	FCollisionQueryParams Params;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, Position + FVector(0, 0, 60.f), LevelCentre, ECC_Visibility, Params);
	return bHit; // hit = occluded (something blocks LoS to centre)
}

float FSemanticCommandModule::EstimateDarknessAt(UWorld* World, const FVector& Position,
                                                   float SearchRadius)
{
	// Sum all nearby point light contributions. Lower total = darker = higher score.
	float TotalInfluence = 0.f;
	for (TActorIterator<ALight> It(World); It; ++It)
	{
		if ((*It)->IsA<ADirectionalLight>() || (*It)->IsA<ASkyLight>()) { continue; }
		if (ULightComponent* LC = (*It)->GetLightComponent())
		{
			const float Dist = FVector::Dist((*It)->GetActorLocation(), Position);
			if (Dist < SearchRadius)
			{
				TotalInfluence += LC->Intensity * (1.f - Dist / SearchRadius);
			}
		}
	}
	// Convert to a 0-100 darkness score (high = dark).
	return FMath::Clamp(100.f - TotalInfluence * 0.005f, 0.f, 100.f);
}
