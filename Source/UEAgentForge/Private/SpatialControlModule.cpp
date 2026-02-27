// UEAgentForge — SpatialControlModule.cpp
// Precise surface-aware 3D placement and spatial analysis for AI agents.

#include "SpatialControlModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EngineUtils.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "DrawDebugHelpers.h"
#endif

// ============================================================================
//  FILE-SCOPE HELPERS
// ============================================================================
#if WITH_EDITOR

static TSharedPtr<FJsonObject> VecToJsonSC(const FVector& V)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetNumberField(TEXT("x"), V.X);
	O->SetNumberField(TEXT("y"), V.Y);
	O->SetNumberField(TEXT("z"), V.Z);
	return O;
}

static FString SpatialError(const FString& Msg)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("error"), Msg);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return Out;
}

static FString SpatialJson(const TSharedPtr<FJsonObject>& O)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return Out;
}

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

/** Line-trace downward from Loc+Extent, return true + HitResult on hit. */
static bool TraceDown(UWorld* World, FVector Loc, float DownExtent, FHitResult& OutHit)
{
	FCollisionQueryParams Params(NAME_None, /*bTraceComplex=*/true);
	return World->LineTraceSingleByChannel(
		OutHit,
		Loc + FVector(0.f, 0.f, DownExtent * 0.5f),
		Loc - FVector(0.f, 0.f, DownExtent),
		ECC_WorldStatic,
		Params);
}

#endif // WITH_EDITOR

// ============================================================================
//  SPAWN_ACTOR_AT_SURFACE
// ============================================================================
FString FSpatialControlModule::SpawnActorAtSurface(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return SpatialError(TEXT("spawn_actor_at_surface: invalid args."));

	// ── Parse args ────────────────────────────────────────────────────────────
	FString ClassPath, Label;
	Args->TryGetStringField(TEXT("class_path"), ClassPath);
	Args->TryGetStringField(TEXT("label"), Label);

	const TSharedPtr<FJsonObject>* OriginObj = nullptr;
	const TSharedPtr<FJsonObject>* DirObj    = nullptr;
	FVector Origin    = FVector::ZeroVector;
	FVector Direction = FVector(0.f, 0.f, -1.f);

	if (Args->TryGetObjectField(TEXT("origin"), OriginObj))
	{
		Origin.X = (*OriginObj)->GetNumberField(TEXT("x"));
		Origin.Y = (*OriginObj)->GetNumberField(TEXT("y"));
		Origin.Z = (*OriginObj)->GetNumberField(TEXT("z"));
	}
	if (Args->TryGetObjectField(TEXT("direction"), DirObj))
	{
		Direction.X = (*DirObj)->GetNumberField(TEXT("x"));
		Direction.Y = (*DirObj)->GetNumberField(TEXT("y"));
		Direction.Z = (*DirObj)->GetNumberField(TEXT("z"));
		Direction.Normalize();
	}

	const float MaxDistance    = Args->HasField(TEXT("max_distance"))
		? (float)Args->GetNumberField(TEXT("max_distance")) : 5000.f;
	const bool  bAlignToNormal = Args->HasField(TEXT("align_to_normal"))
		? Args->GetBoolField(TEXT("align_to_normal")) : true;

	// ── Load spawnable class ─────────────────────────────────────────────────
	UClass* SpawnClass = nullptr;
	if (!ClassPath.IsEmpty())
	{
		SpawnClass = LoadObject<UClass>(nullptr, *ClassPath);
		if (!SpawnClass)
		{
			// Try generated_class suffix
			const FString GenPath = ClassPath + TEXT("_C");
			SpawnClass = LoadObject<UClass>(nullptr, *GenPath);
		}
	}
	if (!SpawnClass)
	{
		SpawnClass = AStaticMeshActor::StaticClass();
	}

	// ── Raycast to surface ────────────────────────────────────────────────────
	UWorld* World = GetEditorWorld();
	if (!World) return SpatialError(TEXT("No editor world."));

	FCollisionQueryParams Params(NAME_None, true);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		Origin,
		Origin + Direction * MaxDistance,
		ECC_WorldStatic,
		Params);

	if (!bHit)
		return SpatialError(TEXT("No surface hit along trace direction. Check origin/direction/max_distance."));

	// ── Compute spawn transform ───────────────────────────────────────────────
	FVector    SpawnLoc = Hit.ImpactPoint;
	FRotator   SpawnRot = FRotator::ZeroRotator;

	if (bAlignToNormal)
	{
		// Build a rotation where +Z aligns with the surface normal
		const FVector Normal = Hit.ImpactNormal;
		const FVector Right  = FVector::CrossProduct(Normal, FVector::ForwardVector).GetSafeNormal();
		const FVector Fwd    = FVector::CrossProduct(Right, Normal).GetSafeNormal();
		FMatrix RotMat       = FMatrix(Fwd, Right, Normal, FVector::ZeroVector);
		SpawnRot             = RotMat.Rotator();
	}

	// ── Spawn actor ───────────────────────────────────────────────────────────
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* NewActor = World->SpawnActor<AActor>(SpawnClass, SpawnLoc, SpawnRot, SpawnParams);
	if (!NewActor)
		return SpatialError(TEXT("SpawnActor failed. Check class_path is valid."));

	if (!Label.IsEmpty()) { NewActor->SetActorLabel(Label); }

	// ── Build response ────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),           true);
	Resp->SetStringField(TEXT("actor_label"),  NewActor->GetActorLabel());
	Resp->SetStringField(TEXT("actor_path"),   NewActor->GetPathName());
	Resp->SetObjectField(TEXT("location"),     VecToJsonSC(SpawnLoc));
	Resp->SetObjectField(TEXT("normal"),       VecToJsonSC(Hit.ImpactNormal));
	Resp->SetStringField(TEXT("surface_actor"),
		Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT("none"));
	return SpatialJson(Resp);
#else
	return SpatialError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  ALIGN_ACTORS_TO_SURFACE
// ============================================================================
FString FSpatialControlModule::AlignActorsToSurface(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return SpatialError(TEXT("align_actors_to_surface: invalid args."));

	const TArray<TSharedPtr<FJsonValue>>* LabelsArr;
	if (!Args->TryGetArrayField(TEXT("actor_labels"), LabelsArr))
		return SpatialError(TEXT("align_actors_to_surface requires 'actor_labels' array."));

	const float DownExtent = Args->HasField(TEXT("down_trace_extent"))
		? (float)Args->GetNumberField(TEXT("down_trace_extent")) : 2000.f;

	UWorld* World = GetEditorWorld();
	if (!World) return SpatialError(TEXT("No editor world."));

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 AlignedCount = 0;

	for (const TSharedPtr<FJsonValue>& LabelVal : *LabelsArr)
	{
		FString Label = LabelVal->AsString();
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), Label);

		// Find actor by label
		AActor* Found = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if ((*It)->GetActorLabel() == Label) { Found = *It; break; }
		}

		if (!Found)
		{
			Entry->SetBoolField  (TEXT("ok"),    false);
			Entry->SetStringField(TEXT("error"), TEXT("Actor not found"));
			Results.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}

		const float OldZ = Found->GetActorLocation().Z;
		FHitResult Hit;
		if (TraceDown(World, Found->GetActorLocation(), DownExtent, Hit))
		{
			FVector NewLoc = Found->GetActorLocation();
			NewLoc.Z = Hit.ImpactPoint.Z;
			Found->SetActorLocation(NewLoc);
			++AlignedCount;

			Entry->SetBoolField  (TEXT("ok"),    true);
			Entry->SetNumberField(TEXT("old_z"), OldZ);
			Entry->SetNumberField(TEXT("new_z"), NewLoc.Z);
		}
		else
		{
			Entry->SetBoolField  (TEXT("ok"),    false);
			Entry->SetStringField(TEXT("error"), TEXT("No surface found below actor"));
		}
		Results.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),            true);
	Resp->SetNumberField(TEXT("aligned_count"), AlignedCount);
	Resp->SetArrayField (TEXT("results"),       Results);
	return SpatialJson(Resp);
#else
	return SpatialError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  GET_SURFACE_NORMAL_AT
// ============================================================================
FString FSpatialControlModule::GetSurfaceNormalAt(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return SpatialError(TEXT("get_surface_normal_at: invalid args."));

	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;

	UWorld* World = GetEditorWorld();
	if (!World) return SpatialError(TEXT("No editor world."));

	FHitResult Hit;
	// Trace down 5000 cm from the given point
	FCollisionQueryParams Params(NAME_None, true);
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		FVector(X, Y, Z + 200.f),
		FVector(X, Y, Z - 5000.f),
		ECC_WorldStatic,
		Params);

	if (!bHit)
		return SpatialError(TEXT("No surface found below the given point."));

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),         true);
	Resp->SetObjectField(TEXT("location"),   VecToJsonSC(Hit.ImpactPoint));
	Resp->SetObjectField(TEXT("normal"),     VecToJsonSC(Hit.ImpactNormal));
	Resp->SetStringField(TEXT("hit_actor"),
		Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT("none"));
	return SpatialJson(Resp);
#else
	return SpatialError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  ANALYZE_LEVEL_COMPOSITION
// ============================================================================
FString FSpatialControlModule::AnalyzeLevelComposition()
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World) return SpatialError(TEXT("No editor world."));

	int32 TotalCount = 0, StaticCount = 0, LightCount = 0, AICount = 0, OtherCount = 0;
	FBox  WorldBounds(ForceInit);

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !IsValid(Actor)) { continue; }

		++TotalCount;

		const FString ClassName = Actor->GetClass()->GetName();
		if (ClassName.Contains(TEXT("StaticMesh"))    ||
		    ClassName.Contains(TEXT("Brush"))          ||
		    ClassName.Contains(TEXT("Landscape")))     { ++StaticCount; }
		else if (ClassName.Contains(TEXT("Light"))    ||
		         ClassName.Contains(TEXT("Sky")))      { ++LightCount; }
		else if (ClassName.Contains(TEXT("Character"))||
		         ClassName.Contains(TEXT("AI"))        ||
		         ClassName.Contains(TEXT("Warden")))   { ++AICount; }
		else                                           { ++OtherCount; }

		// Expand world bounds
		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);
		if (!Extent.IsNearlyZero())
		{
			WorldBounds += FBox::BuildAABB(Origin, Extent);
		}
	}

	// ── Density score (actors per 10,000 m² horizontal) ──────────────────────
	const FVector BoundsSize = WorldBounds.IsValid ? WorldBounds.GetSize() : FVector::ZeroVector;
	const float HArea        = FMath::Max(1.f, BoundsSize.X * BoundsSize.Y / 1000000.f); // cm² → m²
	const float DensityScore = FMath::Clamp((float)TotalCount / HArea, 0.f, 100.f);

	// ── Recommendations ───────────────────────────────────────────────────────
	TArray<FString> Recs;
	if (LightCount == 0)                { Recs.Add(TEXT("No lights found — add ambient lighting.")); }
	if (StaticCount < 5)                { Recs.Add(TEXT("Very few static mesh actors — level may appear empty.")); }
	if (AICount == 0)                   { Recs.Add(TEXT("No AI/character actors placed.")); }
	if (DensityScore < 0.5f)            { Recs.Add(TEXT("Low actor density — level may feel sparse.")); }
	if (DensityScore > 20.f)            { Recs.Add(TEXT("High actor density — consider performance profiling.")); }
	if (Recs.IsEmpty())                 { Recs.Add(TEXT("Level composition looks healthy.")); }

	// ── Build response ────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> BoundsObj = MakeShared<FJsonObject>();
	BoundsObj->SetObjectField(TEXT("min"),  VecToJsonSC(WorldBounds.IsValid ? WorldBounds.Min : FVector::ZeroVector));
	BoundsObj->SetObjectField(TEXT("max"),  VecToJsonSC(WorldBounds.IsValid ? WorldBounds.Max : FVector::ZeroVector));
	BoundsObj->SetObjectField(TEXT("size"), VecToJsonSC(BoundsSize));

	TArray<TSharedPtr<FJsonValue>> RecArr;
	for (const FString& R : Recs) { RecArr.Add(MakeShared<FJsonValueString>(R)); }

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),            true);
	Resp->SetNumberField(TEXT("actor_count"),    TotalCount);
	Resp->SetNumberField(TEXT("static_count"),   StaticCount);
	Resp->SetNumberField(TEXT("light_count"),    LightCount);
	Resp->SetNumberField(TEXT("ai_count"),       AICount);
	Resp->SetNumberField(TEXT("other_count"),    OtherCount);
	Resp->SetObjectField(TEXT("bounds"),         BoundsObj);
	Resp->SetNumberField(TEXT("density_score"),  DensityScore);
	Resp->SetArrayField (TEXT("recommendations"), RecArr);
	return SpatialJson(Resp);
#else
	return SpatialError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  GET_ACTORS_IN_RADIUS
// ============================================================================
FString FSpatialControlModule::GetActorsInRadius(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return SpatialError(TEXT("get_actors_in_radius: invalid args."));

	const float X      = Args->HasField(TEXT("x"))      ? (float)Args->GetNumberField(TEXT("x"))      : 0.f;
	const float Y      = Args->HasField(TEXT("y"))      ? (float)Args->GetNumberField(TEXT("y"))      : 0.f;
	const float Z      = Args->HasField(TEXT("z"))      ? (float)Args->GetNumberField(TEXT("z"))      : 0.f;
	const float Radius = Args->HasField(TEXT("radius")) ? (float)Args->GetNumberField(TEXT("radius")) : 1000.f;

	const FVector Center(X, Y, Z);
	UWorld* World = GetEditorWorld();
	if (!World) return SpatialError(TEXT("No editor world."));

	// Collect actors within radius, sort by distance
	TArray<TPair<float, AActor*>> Found;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !IsValid(Actor)) { continue; }
		const float Dist = FVector::Dist(Center, Actor->GetActorLocation());
		if (Dist <= Radius)
		{
			Found.Emplace(Dist, Actor);
		}
	}
	Found.Sort([](const TPair<float,AActor*>& A, const TPair<float,AActor*>& B){
		return A.Key < B.Key;
	});

	TArray<TSharedPtr<FJsonValue>> ResultArr;
	for (const TPair<float, AActor*>& Pair : Found)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"),    Pair.Value->GetActorLabel());
		Entry->SetStringField(TEXT("class"),    Pair.Value->GetClass()->GetName());
		Entry->SetNumberField(TEXT("distance"), Pair.Key);
		Entry->SetObjectField(TEXT("location"), VecToJsonSC(Pair.Value->GetActorLocation()));
		ResultArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),      true);
	Resp->SetNumberField(TEXT("count"),   Found.Num());
	Resp->SetArrayField (TEXT("actors"),  ResultArr);
	return SpatialJson(Resp);
#else
	return SpatialError(TEXT("Editor only."));
#endif
}
