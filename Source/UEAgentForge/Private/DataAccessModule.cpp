// Copyright UEAgentForge Project. All Rights Reserved.

#include "DataAccessModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Actor.h"
#include "AI/Navigation/NavigationTypes.h"
#include "NavMesh/NavMeshRenderingComponent.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Engine/SkyLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "UnrealClient.h"
#include "Misc/ScreenshotRequest.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#endif

// ─── Shared JSON helpers (mirror AgentForgeLibrary) ──────────────────────────

static FString ToJsonStr(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

static TSharedPtr<FJsonObject> VecToObj(const FVector& V)
{
	auto O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("x"), V.X);
	O->SetNumberField(TEXT("y"), V.Y);
	O->SetNumberField(TEXT("z"), V.Z);
	return O;
}

static FString ErrResp(const FString& Msg)
{
	auto O = MakeShared<FJsonObject>();
	O->SetBoolField(TEXT("ok"), false);
	O->SetStringField(TEXT("error"), Msg);
	return ToJsonStr(O);
}


// ─── GetMultiViewCapture ──────────────────────────────────────────────────────

FString FDataAccessModule::GetMultiViewCapture(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	// Determine orbit centre: explicit args or level bounding box centre.
	FVector Centre = ComputeLevelCenter(World);
	if (Args)
	{
		if (Args->HasField(TEXT("center_x"))) { Centre.X = Args->GetNumberField(TEXT("center_x")); }
		if (Args->HasField(TEXT("center_y"))) { Centre.Y = Args->GetNumberField(TEXT("center_y")); }
		if (Args->HasField(TEXT("center_z"))) { Centre.Z = Args->GetNumberField(TEXT("center_z")); }
	}
	const float Radius  = Args && Args->HasField(TEXT("orbit_radius"))
	                    ? (float)Args->GetNumberField(TEXT("orbit_radius")) : 3000.f;

	// Preset angles: Name, camera offset from centre, rotation (Pitch,Yaw,Roll).
	struct FPreset { FString Name; FVector Offset; FRotator Rot; };
	const TArray<FPreset> Presets = {
		{ TEXT("top"),     FVector(   0,     0, Radius),  FRotator(-89.f, 0.f, 0.f) },
		{ TEXT("front"),   FVector(-Radius,  0, Radius * 0.3f), FRotator(-15.f,   0.f, 0.f) },
		{ TEXT("side"),    FVector(   0, -Radius, Radius * 0.3f), FRotator(-15.f,  90.f, 0.f) },
		{ TEXT("tension"), FVector(-Radius * 0.5f, 0, Radius * 0.07f), FRotator(-5.f, 0.f, 0.f) },
	};

	// Choose requested angle (or all presets info if no angle specified).
	FString AngleName = Args && Args->HasField(TEXT("angle"))
	                  ? Args->GetStringField(TEXT("angle")) : TEXT("top");

	const FPreset* Chosen = Presets.FindByPredicate(
		[&AngleName](const FPreset& P){ return P.Name == AngleName; });
	if (!Chosen) { return ErrResp(FString::Printf(TEXT("Unknown angle '%s'"), *AngleName)); }

	// Move viewport camera.
	FLevelEditorViewportClient* VC = nullptr;
	for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
	{
		if (C && C->IsPerspective()) { VC = C; break; }
	}
	if (!VC) { return ErrResp(TEXT("No perspective viewport")); }

	VC->SetViewLocation(Centre + Chosen->Offset);
	VC->SetViewRotation(Chosen->Rot);
	VC->Invalidate();
	GEditor->RedrawAllViewports(true);

	// Request screenshot (async — ready after next frame render).
	static int32 CaptureIdx = 0;
	const FString Filename = FString::Printf(TEXT("multiview_%s_%d"),
	                                          *AngleName, CaptureIdx++);
	FScreenshotRequest::RequestScreenshot(Filename, false, false);

	// Build response.
	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("angle"), AngleName);
	Root->SetStringField(TEXT("note"),
		TEXT("Screenshot queued — allow ~0.5s for file write. "
		     "Default path: Saved/Screenshots/WindowsEditor/"));

	auto Cam = MakeShared<FJsonObject>();
	Cam->SetNumberField(TEXT("x"),     (Centre + Chosen->Offset).X);
	Cam->SetNumberField(TEXT("y"),     (Centre + Chosen->Offset).Y);
	Cam->SetNumberField(TEXT("z"),     (Centre + Chosen->Offset).Z);
	Cam->SetNumberField(TEXT("pitch"), Chosen->Rot.Pitch);
	Cam->SetNumberField(TEXT("yaw"),   Chosen->Rot.Yaw);
	Root->SetObjectField(TEXT("camera"), Cam);

	// Include the full preset table so the agent knows all available angles.
	TArray<TSharedPtr<FJsonValue>> PresetArr;
	for (const FPreset& P : Presets)
	{
		auto PO = MakeShared<FJsonObject>();
		PO->SetStringField(TEXT("angle"), P.Name);
		PO->SetObjectField(TEXT("camera_offset"), VecToObj(P.Offset));
		PO->SetNumberField(TEXT("pitch"), P.Rot.Pitch);
		PO->SetNumberField(TEXT("yaw"),   P.Rot.Yaw);
		PresetArr.Add(MakeShared<FJsonValueObject>(PO));
	}
	Root->SetArrayField(TEXT("preset_angles"), PresetArr);
	return ToJsonStr(Root);
#else
	return ErrResp(TEXT("GetMultiViewCapture requires WITH_EDITOR"));
#endif
}


// ─── GetLevelHierarchy ────────────────────────────────────────────────────────

FString FDataAccessModule::GetLevelHierarchy()
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	int32 ActorCount = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || Actor->IsA<AWorldSettings>()) { continue; }
		++ActorCount;

		auto AO = MakeShared<FJsonObject>();
		AO->SetStringField(TEXT("label"), Actor->GetActorLabel());
		AO->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		AO->SetBoolField(TEXT("is_visible"), !Actor->IsHidden());

		// Parent actor label (folders not tracked here — only attach parent).
		AActor* ParentActor = Actor->GetAttachParentActor();
		AO->SetStringField(TEXT("parent"), ParentActor ? ParentActor->GetActorLabel() : TEXT(""));

		// Tags.
		TArray<TSharedPtr<FJsonValue>> TagArr;
		for (const FName& Tag : Actor->Tags)
		{
			TagArr.Add(MakeShared<FJsonValueString>(Tag.ToString()));
		}
		AO->SetArrayField(TEXT("tags"), TagArr);

		// Location.
		AO->SetObjectField(TEXT("location"), VecToObj(Actor->GetActorLocation()));

		// Bounds (world-space box).
		FVector BoundsOrigin, BoundsExtent;
		Actor->GetActorBounds(false, BoundsOrigin, BoundsExtent);
		auto BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetObjectField(TEXT("center"), VecToObj(BoundsOrigin));
		BoundsObj->SetObjectField(TEXT("extent"), VecToObj(BoundsExtent));
		BoundsObj->SetObjectField(TEXT("min"), VecToObj(BoundsOrigin - BoundsExtent));
		BoundsObj->SetObjectField(TEXT("max"), VecToObj(BoundsOrigin + BoundsExtent));
		AO->SetObjectField(TEXT("bounds"), BoundsObj);

		// Components.
		TArray<TSharedPtr<FJsonValue>> CompArr;
		TArray<UActorComponent*> Comps;
		Actor->GetComponents(Comps);
		for (UActorComponent* Comp : Comps)
		{
			if (!Comp) { continue; }
			auto CO = MakeShared<FJsonObject>();
			CO->SetStringField(TEXT("name"), Comp->GetName());
			CO->SetStringField(TEXT("class"), Comp->GetClass()->GetName());
			CompArr.Add(MakeShared<FJsonValueObject>(CO));
		}
		AO->SetArrayField(TEXT("components"), CompArr);

		ActorArray.Add(MakeShared<FJsonValueObject>(AO));
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetNumberField(TEXT("actor_count"), ActorCount);
	Root->SetArrayField(TEXT("actors"), ActorArray);
	return ToJsonStr(Root);
#else
	return ErrResp(TEXT("GetLevelHierarchy requires WITH_EDITOR"));
#endif
}


// ─── GetDeepProperties ───────────────────────────────────────────────────────

FString FDataAccessModule::GetDeepProperties(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args || !Args->HasField(TEXT("label")))
	{
		return ErrResp(TEXT("args.label required"));
	}
	const FString Label = Args->GetStringField(TEXT("label"));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	// Find the actor.
	AActor* FoundActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel() == Label || (*It)->GetName() == Label)
		{
			FoundActor = *It;
			break;
		}
	}
	if (!FoundActor)
	{
		return ErrResp(FString::Printf(TEXT("Actor '%s' not found"), *Label));
	}

	auto PropsObj = MakeShared<FJsonObject>();
	int32 PropCount = 0;

	// Iterate all properties on the class hierarchy.
	for (TFieldIterator<FProperty> PropIt(FoundActor->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Prop = *PropIt;

		// Skip protected/private/transient/deprecated unless explicitly labelled editable.
		const bool bEditable = Prop->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
		if (!bEditable) { continue; }

		const FString PropName = Prop->GetName();
		void* PropAddr = Prop->ContainerPtrToValuePtr<void>(FoundActor);

		// Export to string — works for all property types.
		FString ValueStr;
		Prop->ExportTextItem_Direct(ValueStr, PropAddr, nullptr, FoundActor, PPF_None);

		PropsObj->SetStringField(PropName, ValueStr);
		++PropCount;
	}

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("label"), Label);
	Root->SetStringField(TEXT("class"), FoundActor->GetClass()->GetName());
	Root->SetNumberField(TEXT("property_count"), PropCount);
	Root->SetObjectField(TEXT("properties"), PropsObj);
	return ToJsonStr(Root);
#else
	return ErrResp(TEXT("GetDeepProperties requires WITH_EDITOR"));
#endif
}


// ─── GetSemanticEnvironmentSnapshot ──────────────────────────────────────────

FString FDataAccessModule::GetSemanticEnvironmentSnapshot()
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrResp(TEXT("No editor world")); }

	// ── Lighting analysis ──
	int32 LightCount = 0;
	float TotalIntensity = 0.f;
	float MaxIntensity   = 0.f;
	FLinearColor DomColor(0, 0, 0);
	bool bHasDir = false, bHasSky = false;
	GatherLightingStats(World, LightCount, TotalIntensity, MaxIntensity, DomColor, bHasDir, bHasSky);

	const float AvgIntensity = LightCount > 0 ? TotalIntensity / LightCount : 0.f;
	// Darkness score: 0=pitch black, 100=very bright. Based on avg candela.
	const float DarknessScore = FMath::Clamp(100.f - AvgIntensity * 0.01f, 0.f, 100.f);

	auto LightingObj = MakeShared<FJsonObject>();
	LightingObj->SetNumberField(TEXT("point_light_count"),   LightCount);
	LightingObj->SetNumberField(TEXT("avg_intensity"),       AvgIntensity);
	LightingObj->SetNumberField(TEXT("max_intensity"),       MaxIntensity);
	LightingObj->SetNumberField(TEXT("darkness_score"),      DarknessScore);
	LightingObj->SetBoolField(TEXT("has_directional_light"), bHasDir);
	LightingObj->SetBoolField(TEXT("has_sky_light"),         bHasSky);
	auto DomColorObj = MakeShared<FJsonObject>();
	DomColorObj->SetNumberField(TEXT("r"), DomColor.R);
	DomColorObj->SetNumberField(TEXT("g"), DomColor.G);
	DomColorObj->SetNumberField(TEXT("b"), DomColor.B);
	LightingObj->SetObjectField(TEXT("dominant_color"), DomColorObj);

	// ── Post-process analysis ──
	auto PPObj = MakeShared<FJsonObject>();
	float Vignette = 0.f, Bloom = 0.f, Grain = 0.f, ExposureComp = 0.f, FogDensity = 0.f;
	bool bHasCRT = false;

	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		const FPostProcessSettings& S = (*It)->Settings;
		if (S.bOverride_VignetteIntensity)   { Vignette     = S.VignetteIntensity; }
		if (S.bOverride_BloomIntensity)      { Bloom        = S.BloomIntensity; }
		if (S.bOverride_FilmGrainIntensity)  { Grain        = S.FilmGrainIntensity; }
		if (S.bOverride_AutoExposureBias)    { ExposureComp = S.AutoExposureBias; }
		// CRT check: any material blendable with non-zero weight.
		if (S.WeightedBlendables.Array.Num() > 0)
		{
			for (const FWeightedBlendable& WB : S.WeightedBlendables.Array)
			{
				if (WB.Weight > 0.01f && WB.Object) { bHasCRT = true; }
			}
		}
		break; // use first PPV only for snapshot
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* Fog =
			(*It)->GetComponent())
		{
			FogDensity = Fog->FogDensity;
		}
		break;
	}

	PPObj->SetNumberField(TEXT("vignette"),              Vignette);
	PPObj->SetNumberField(TEXT("bloom"),                 Bloom);
	PPObj->SetNumberField(TEXT("grain"),                 Grain);
	PPObj->SetNumberField(TEXT("exposure_compensation"), ExposureComp);
	PPObj->SetBoolField(TEXT("has_crt_blendable"),       bHasCRT);
	PPObj->SetNumberField(TEXT("fog_density"),           FogDensity);

	// ── Actor density ──
	int32 TotalActors = 0, StaticCount = 0, LightsInScene = 0, AICount = 0;
	FBox LevelBox(EForceInit::ForceInit);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || A->IsA<AWorldSettings>()) { continue; }
		++TotalActors;
		if (A->IsA<AStaticMeshActor>())   { ++StaticCount; }
		if (A->IsA<ALight>())             { ++LightsInScene; }
		if (A->IsA<APawn>())              { ++AICount; }
		FVector Origin, Extent;
		A->GetActorBounds(false, Origin, Extent);
		LevelBox += FBox(Origin - Extent, Origin + Extent);
	}
	const float LevelAreaM2 = LevelBox.IsValid
	                        ? (LevelBox.GetSize().X * LevelBox.GetSize().Y) / (100.f * 100.f)
	                        : 1.f;
	const float DensityPerM2 = LevelAreaM2 > 0.f ? TotalActors / LevelAreaM2 : 0.f;

	auto DensityObj = MakeShared<FJsonObject>();
	DensityObj->SetNumberField(TEXT("actor_count"),    TotalActors);
	DensityObj->SetNumberField(TEXT("static_count"),   StaticCount);
	DensityObj->SetNumberField(TEXT("light_count"),    LightsInScene);
	DensityObj->SetNumberField(TEXT("ai_count"),       AICount);
	DensityObj->SetNumberField(TEXT("density_per_m2"), DensityPerM2);

	auto BoundsObj = MakeShared<FJsonObject>();
	if (LevelBox.IsValid)
	{
		BoundsObj->SetObjectField(TEXT("center"), VecToObj(LevelBox.GetCenter()));
		BoundsObj->SetObjectField(TEXT("extent"), VecToObj(LevelBox.GetExtent()));
		BoundsObj->SetNumberField(TEXT("area_m2"), LevelAreaM2);
	}

	// ── Horror score (0–100) ──
	// High darkness + fog + CRT + vignette + low actor density → more horror.
	float HorrorScore = 0.f;
	HorrorScore += FMath::Clamp(DarknessScore * 0.4f, 0.f, 40.f);      // darkness
	HorrorScore += FMath::Clamp(FogDensity * 1000.f, 0.f, 15.f);       // fog
	HorrorScore += Vignette > 0.5f ? 10.f : 0.f;                        // vignette
	HorrorScore += bHasCRT ? 10.f : 0.f;                                // CRT overlay
	HorrorScore += ExposureComp < -0.3f ? 10.f : 0.f;                  // dark exposure
	HorrorScore += bHasSky ? 5.f : 0.f;                                 // sky for ambience
	HorrorScore = FMath::Clamp(HorrorScore, 0.f, 100.f);

	// ── Assemble response ──
	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetObjectField(TEXT("lighting"),      LightingObj);
	Root->SetObjectField(TEXT("post_process"),  PPObj);
	Root->SetObjectField(TEXT("density"),       DensityObj);
	Root->SetObjectField(TEXT("level_bounds"),  BoundsObj);
	Root->SetNumberField(TEXT("horror_score"),  HorrorScore);
	Root->SetStringField(TEXT("horror_rating"),
		HorrorScore >= 70.f ? TEXT("High") :
		HorrorScore >= 40.f ? TEXT("Medium") : TEXT("Low"));
	return ToJsonStr(Root);
#else
	return ErrResp(TEXT("GetSemanticEnvironmentSnapshot requires WITH_EDITOR"));
#endif
}


// ─── Private helpers ──────────────────────────────────────────────────────────

FVector FDataAccessModule::ComputeLevelCenter(UWorld* World)
{
	if (!World) { return FVector::ZeroVector; }
	FBox Box(EForceInit::ForceInit);
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || A->IsA<AWorldSettings>()) { continue; }
		FVector O, E;
		A->GetActorBounds(false, O, E);
		Box += FBox(O - E, O + E);
	}
	return Box.IsValid ? Box.GetCenter() : FVector::ZeroVector;
}

void FDataAccessModule::GatherLightingStats(UWorld* World,
                                             int32& OutCount, float& OutAvg, float& OutMax,
                                             FLinearColor& OutDominantColor,
                                             bool& bHasDir, bool& bHasSky)
{
	OutCount = 0; OutAvg = 0.f; OutMax = 0.f;
	OutDominantColor = FLinearColor::Black;
	bHasDir = false; bHasSky = false;

	float TotalIntensity = 0.f;
	FLinearColor AccColor(0, 0, 0);

	for (TActorIterator<ALight> It(World); It; ++It)
	{
		if (ADirectionalLight* D = Cast<ADirectionalLight>(*It)) { bHasDir = true; continue; }
		if (Cast<ASkyLight>(*It)) { bHasSky = true; continue; }

		if (ULightComponent* LC = (*It)->GetLightComponent())
		{
			const float Intensity = LC->Intensity;
			TotalIntensity += Intensity;
			OutMax = FMath::Max(OutMax, Intensity);
			AccColor += FLinearColor(LC->LightColor) * (Intensity / 10000.f);
			++OutCount;
		}
	}

	OutAvg = OutCount > 0 ? TotalIntensity / OutCount : 0.f;
	if (OutCount > 0)
	{
		AccColor /= (float)OutCount;
		OutDominantColor = AccColor;
	}
}
