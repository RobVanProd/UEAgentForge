// Copyright UEAgentForge Project. All Rights Reserved.
// LevelPipelineModule.cpp — v0.4.0 Five-Phase Professional Level Generation Pipeline.

#include "LevelPipelineModule.h"
#include "LevelPresetSystem.h"
#include "SemanticCommandModule.h"    // PlaceAssetThematically

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Math/RandomStream.h"
#include "EngineUtils.h"

#if WITH_EDITOR
#include "Editor.h"
#include "ScopedTransaction.h"
#include "FileHelpers.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Engine/SpotLight.h"
#include "Components/SpotLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/PostProcessVolume.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/SkyLight.h"
#include "AI/Navigation/NavMeshBoundsVolume.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UnrealType.h"
#endif

// ─────────────────────────────────────────────────────────────────────────────
//  File-scope helpers
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	static FString ToJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
		return Out;
	}

	static TSharedPtr<FJsonObject> ErrObj(const FString& Msg)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("error"), Msg);
		return O;
	}

	static TSharedPtr<FJsonObject> VecToObj(const FVector& V)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X);
		O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z);
		return O;
	}

#if WITH_EDITOR
	// Spawn a unit-cube StaticMeshActor scaled to the given extents.
	static AActor* SpawnCubeAt(UWorld* World, const FVector& Center,
	                            const FVector& Scale, const FString& Label)
	{
		UStaticMesh* CubeMesh = Cast<UStaticMesh>(
			StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
			                 TEXT("/Engine/BasicShapes/Cube.Cube")));

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FTransform(FRotator::ZeroRotator, Center), P);
		if (!SMA) { return nullptr; }
		if (CubeMesh && SMA->GetStaticMeshComponent())
		{
			SMA->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
		}
		SMA->SetActorScale3D(Scale);
		SMA->SetActorLabel(Label);
		return SMA;
	}
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase I — GenerateRoomLayout (bubble diagram)
// ─────────────────────────────────────────────────────────────────────────────
TArray<FVector> FLevelPipelineModule::GenerateRoomLayout(int32 RoomCount,
                                                          float GridSize,
                                                          const FLevelPreset& /*Preset*/)
{
	TArray<FVector> Positions;
	if (RoomCount <= 0) { return Positions; }

	// Roles: Entry (0), Exploration (1..N-2), Climax (N-2 if >2), Exit (N-1).
	// Layout: linear chain with alternating lateral offsets to avoid overlap.
	const float SpacingX  = GridSize * 3.f;
	const float OffsetY   = GridSize * 2.f;
	for (int32 i = 0; i < RoomCount; ++i)
	{
		const float X = static_cast<float>(i) * SpacingX;
		const float Y = (i % 2 == 0) ? 0.f : OffsetY;
		Positions.Add(FVector(X, Y, 0.f));
	}
	return Positions;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase I — PlaceBlockoutRoom
// ─────────────────────────────────────────────────────────────────────────────
bool FLevelPipelineModule::PlaceBlockoutRoom(UWorld* World, const FVector& Center,
                                              float Width, float Depth, float Height,
                                              const FString& Label)
{
#if WITH_EDITOR
	// UE unit cube is 100x100x100 cm — scale accordingly.
	const FVector Scale(Width / 100.f, Depth / 100.f, Height / 100.f);
	AActor* A = SpawnCubeAt(World, Center, Scale, Label);
	if (!A) { return false; }
	// Raise so the floor sits at Z=0.
	A->SetActorLocation(Center + FVector(0.f, 0.f, Height * 0.5f));
	return true;
#else
	return false;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase I — ConnectRoomsWithCorridors
// ─────────────────────────────────────────────────────────────────────────────
void FLevelPipelineModule::ConnectRoomsWithCorridors(UWorld* World,
                                                      const TArray<FVector>& RoomCenters,
                                                      float CorridorWidth,
                                                      float CeilingHeight)
{
#if WITH_EDITOR
	for (int32 i = 0; i < RoomCenters.Num() - 1; ++i)
	{
		const FVector A   = RoomCenters[i];
		const FVector B   = RoomCenters[i + 1];
		const FVector Mid = (A + B) * 0.5f;
		const float   Len = FVector::Dist(A, B);

		// Corridor is a thin box from A to B.
		const FVector Dir2D = (FVector(B.X, B.Y, 0.f) - FVector(A.X, A.Y, 0.f)).GetSafeNormal();
		const float Yaw     = FMath::Atan2(Dir2D.Y, Dir2D.X) * (180.f / PI);
		const FVector Scale(Len / 100.f, CorridorWidth / 100.f, CeilingHeight / 100.f);

		UStaticMesh* CubeMesh = Cast<UStaticMesh>(
			StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
			                 TEXT("/Engine/BasicShapes/Cube.Cube")));

		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		FTransform T(FRotator(0.f, Yaw, 0.f), Mid + FVector(0.f, 0.f, CeilingHeight * 0.5f));
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), T, P);
		if (SMA)
		{
			if (CubeMesh && SMA->GetStaticMeshComponent())
				SMA->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
			SMA->SetActorScale3D(Scale);
			SMA->SetActorLabel(FString::Printf(TEXT("Blockout_Corridor_%02d"), i + 1));
		}
	}
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase I — CreateBlockoutLevel
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::CreateBlockoutLevel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor)
	{
		return ToJson(ErrObj(TEXT("GEditor not available.")));
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return ToJson(ErrObj(TEXT("No editor world.")));
	}

	// ── Parse arguments ──────────────────────────────────────────────────────
	FString Mission  = TEXT("Create a level");
	FString PresetNm = TEXT("Default");
	double  RoomCountD = 3.0;
	double  GridSizeD  = 400.0;

	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("mission"),    Mission);
		Args->TryGetStringField(TEXT("preset"),     PresetNm);
		Args->TryGetNumberField(TEXT("room_count"), RoomCountD);
		Args->TryGetNumberField(TEXT("grid_size"),  GridSizeD);
	}

	if (!FLevelPresetSystem::LoadedPresets.Contains(PresetNm))
		FLevelPresetSystem::RegisterBuiltinPresets();

	FLevelPresetSystem::SetCurrentPreset(PresetNm);
	const FLevelPreset& Preset = FLevelPresetSystem::GetCurrentPresetData();

	const int32 RoomCount = FMath::Clamp(static_cast<int32>(RoomCountD), 1, 20);
	const float GridSize  = FMath::Clamp(static_cast<float>(GridSizeD), 100.f, 5000.f);

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "CreateBlockout", "AgentForge: Create Blockout Level"));

	// ── Generate room positions ───────────────────────────────────────────────
	TArray<FVector> RoomCenters = GenerateRoomLayout(RoomCount, GridSize, Preset);

	const float RoomW = GridSize * 2.5f;
	const float RoomD = GridSize * 2.0f;
	const float RoomH = Preset.StandardCeilingHeightCm;

	int32 RoomsPlaced    = 0;
	int32 CorridorsPlaced = 0;

	TArray<TSharedPtr<FJsonValue>> RoomPosArr;

	for (int32 i = 0; i < RoomCenters.Num(); ++i)
	{
		const FString RoleLabel = (i == 0)                    ? TEXT("Entry")
		                        : (i == RoomCenters.Num() - 1) ? TEXT("Exit")
		                        : (i == RoomCenters.Num() - 2  && RoomCenters.Num() > 2) ? TEXT("Climax")
		                        : TEXT("Exploration");
		const FString Label = FString::Printf(TEXT("Blockout_Room_%02d_%s"), i + 1, *RoleLabel);
		if (PlaceBlockoutRoom(World, RoomCenters[i], RoomW, RoomD, RoomH, Label))
		{
			++RoomsPlaced;
			TSharedPtr<FJsonObject> RoomJ = MakeShared<FJsonObject>();
			RoomJ->SetStringField(TEXT("label"), Label);
			RoomJ->SetNumberField(TEXT("x"),     RoomCenters[i].X);
			RoomJ->SetNumberField(TEXT("y"),     RoomCenters[i].Y);
			RoomJ->SetNumberField(TEXT("z"),     RoomCenters[i].Z);
			RoomJ->SetNumberField(TEXT("width"),  RoomW);
			RoomJ->SetNumberField(TEXT("depth"),  RoomD);
			RoomJ->SetNumberField(TEXT("height"), RoomH);
			RoomJ->SetStringField(TEXT("role"),  RoleLabel);
			RoomPosArr.Add(MakeShared<FJsonValueObject>(RoomJ));
		}
	}

	// ── Corridors ─────────────────────────────────────────────────────────────
	ConnectRoomsWithCorridors(World, RoomCenters, Preset.MinCorridorWidthCm, RoomH);
	CorridorsPlaced = FMath::Max(0, RoomCenters.Num() - 1);

	// ── PlayerStart at entry room ─────────────────────────────────────────────
	bool bPlayerStartPlaced = false;
	if (!RoomCenters.IsEmpty())
	{
		const FVector PSLoc = RoomCenters[0] + FVector(0.f, 0.f, Preset.PlayerEyeHeightCm);
		FActorSpawnParameters PSP;
		PSP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* PS = World->SpawnActor<APlayerStart>(APlayerStart::StaticClass(),
			FTransform(FRotator::ZeroRotator, PSLoc), PSP);
		if (PS) { PS->SetActorLabel(TEXT("PlayerStart")); bPlayerStartPlaced = true; }
	}

	// ── NavMeshBoundsVolume covering the whole blockout ───────────────────────
	bool bNavMeshPlaced = false;
	if (!RoomCenters.IsEmpty())
	{
		FVector BoundsCenter = FVector::ZeroVector;
		for (const FVector& C : RoomCenters) { BoundsCenter += C; }
		BoundsCenter /= static_cast<float>(RoomCenters.Num());

		const float NavExtent = GridSize * static_cast<float>(RoomCount) * 1.5f;
		FActorSpawnParameters NavP;
		NavP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ANavMeshBoundsVolume* Nav = World->SpawnActor<ANavMeshBoundsVolume>(
			ANavMeshBoundsVolume::StaticClass(),
			FTransform(FRotator::ZeroRotator, BoundsCenter + FVector(0.f, 0.f, RoomH * 0.5f)), NavP);
		if (Nav)
		{
			Nav->SetActorScale3D(FVector(NavExtent / 100.f, NavExtent / 100.f, RoomH / 50.f));
			Nav->SetActorLabel(TEXT("NavMeshBoundsVolume_Pipeline"));
			bNavMeshPlaced = true;
		}
	}

	// ── Total area ────────────────────────────────────────────────────────────
	const float TotalAreaSqM = (RoomW * RoomD * static_cast<float>(RoomsPlaced)) / (100.f * 100.f);

	// ── Build response ────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),                  true);
	Resp->SetStringField(TEXT("mission"),             Mission);
	Resp->SetStringField(TEXT("preset"),              PresetNm);
	Resp->SetNumberField(TEXT("rooms_placed"),        static_cast<double>(RoomsPlaced));
	Resp->SetNumberField(TEXT("corridors_placed"),    static_cast<double>(CorridorsPlaced));
	Resp->SetNumberField(TEXT("total_area_sqm"),      TotalAreaSqM);
	Resp->SetArrayField (TEXT("room_positions"),      RoomPosArr);
	Resp->SetBoolField  (TEXT("navmesh_placed"),      bNavMeshPlaced);
	Resp->SetBoolField  (TEXT("player_start_placed"), bPlayerStartPlaced);
	Resp->SetNumberField(TEXT("grid_size"),           GridSize);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase II — FindModularKitMeshes
// ─────────────────────────────────────────────────────────────────────────────
TArray<FString> FLevelPipelineModule::FindModularKitMeshes(const FString& KitPath)
{
	TArray<FString> Results;
#if WITH_EDITOR
	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	FARFilter Filter;
	Filter.PackagePaths.Add(*KitPath);
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	ARM.Get().GetAssets(Filter, Assets);
	for (const FAssetData& AD : Assets)
	{
		Results.Add(AD.GetObjectPathString());
	}
#endif
	return Results;
}

float FLevelPipelineModule::SnapToGrid(float Value, float GridSize)
{
	if (GridSize <= 0.f) { return Value; }
	return FMath::RoundToFloat(Value / GridSize) * GridSize;
}

int32 FLevelPipelineModule::ReplaceBlockoutWithModular(UWorld* World,
                                                        const FString& KitPath,
                                                        float SnapGrid)
{
#if WITH_EDITOR
	TArray<FString> Meshes = FindModularKitMeshes(KitPath);
	int32 PiecesPlaced = 0;

	// Collect all Blockout_Room_* actors.
	TArray<AActor*> BlockoutActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (IsValid(A) && A->GetActorLabel().StartsWith(TEXT("Blockout_Room_")))
			BlockoutActors.Add(A);
	}

	for (int32 RoomIdx = 0; RoomIdx < BlockoutActors.Num(); ++RoomIdx)
	{
		AActor* BlkActor = BlockoutActors[RoomIdx];
		FVector Origin, Extent;
		BlkActor->GetActorBounds(false, Origin, Extent);

		// Snap dimensions.
		const float SnapW = SnapToGrid(Extent.X * 2.f, SnapGrid);
		const float SnapD = SnapToGrid(Extent.Y * 2.f, SnapGrid);
		const float SnapH = SnapToGrid(Extent.Z * 2.f, SnapGrid);
		const FVector SnapOrigin(
			SnapToGrid(Origin.X, SnapGrid),
			SnapToGrid(Origin.Y, SnapGrid),
			SnapToGrid(Origin.Z, SnapGrid));

		// Load a mesh from the kit (cycle through available meshes).
		UStaticMesh* Mesh = nullptr;
		if (!Meshes.IsEmpty())
		{
			const FString& MeshPath = Meshes[RoomIdx % Meshes.Num()];
			Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr, *MeshPath));
		}
		// Fallback to engine cube.
		if (!Mesh)
		{
			Mesh = Cast<UStaticMesh>(StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
			                                           TEXT("/Engine/BasicShapes/Cube.Cube")));
		}

		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, SnapOrigin), SP);
		if (SMA)
		{
			if (Mesh && SMA->GetStaticMeshComponent())
			{
				SMA->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				SMA->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			}
			SMA->SetActorScale3D(FVector(SnapW / 100.f, SnapD / 100.f, SnapH / 100.f));
			SMA->SetActorLabel(FString::Printf(TEXT("Arch_Room_%02d_Modular"), RoomIdx + 1));
			++PiecesPlaced;
		}

		// Destroy original blockout actor.
		BlkActor->Destroy();
	}
	return PiecesPlaced;
#else
	return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase II — ConvertToWhiteboxModular
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::ConvertToWhiteboxModular(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ToJson(ErrObj(TEXT("GEditor not available."))); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)   { return ToJson(ErrObj(TEXT("No editor world."))); }

	FString KitPath  = TEXT("/Game/");
	double  SnapGridD = 50.0;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("kit_path"),   KitPath);
		Args->TryGetNumberField(TEXT("snap_grid"),  SnapGridD);
	}
	const float SnapGrid = FMath::Clamp(static_cast<float>(SnapGridD), 1.f, 2000.f);

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "WhiteboxModular", "AgentForge: Whitebox Modular Pass"));

	const int32 Pieces = ReplaceBlockoutWithModular(World, KitPath, SnapGrid);

	// Count remaining Arch_ actors.
	TArray<FString> ArchLabels;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsValid(*It) && (*It)->GetActorLabel().StartsWith(TEXT("Arch_")))
			ArchLabels.Add((*It)->GetActorLabel());
	}
	TArray<TSharedPtr<FJsonValue>> LabelArr;
	for (const FString& L : ArchLabels)
		LabelArr.Add(MakeShared<FJsonValueString>(L));

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),               true);
	Resp->SetNumberField(TEXT("pieces_placed"),     static_cast<double>(Pieces));
	Resp->SetNumberField(TEXT("blockout_replaced"), static_cast<double>(Pieces));
	Resp->SetNumberField(TEXT("snap_grid"),         SnapGrid);
	Resp->SetStringField(TEXT("kit_path"),          KitPath);
	Resp->SetArrayField (TEXT("arch_labels"),       LabelArr);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase III helpers
// ─────────────────────────────────────────────────────────────────────────────
FVector FLevelPipelineModule::FindPropPlacementPoint(UWorld* World,
                                                      const FVector& Center,
                                                      float Radius)
{
#if WITH_EDITOR
	FRandomStream RS(FMath::Rand());
	for (int32 Try = 0; Try < 8; ++Try)
	{
		const float Angle = RS.FRandRange(0.f, 360.f) * PI / 180.f;
		const float Dist  = RS.FRandRange(Radius * 0.2f, Radius * 0.85f);
		const FVector Candidate(
			Center.X + FMath::Cos(Angle) * Dist,
			Center.Y + FMath::Sin(Angle) * Dist,
			Center.Z + 2000.f);

		FHitResult Hit;
		FCollisionQueryParams QP(NAME_None, true);
		if (World->LineTraceSingleByChannel(Hit, Candidate,
		        Candidate - FVector(0.f, 0.f, 4000.f), ECC_WorldStatic, QP))
		{
			return Hit.Location + FVector(0.f, 0.f, 5.f);
		}
	}
	return Center + FVector(RS.FRandRange(-Radius * 0.5f, Radius * 0.5f),
	                         RS.FRandRange(-Radius * 0.5f, Radius * 0.5f), 0.f);
#else
	return Center;
#endif
}

int32 FLevelPipelineModule::ScatterPropsInRoom(UWorld* World,
                                                const FVector& RoomCenter,
                                                float Radius, float Density,
                                                const FString& StoryTheme,
                                                int32 RoomIndex)
{
#if WITH_EDITOR
	const int32 PropCount = FMath::Clamp(FMath::RoundToInt(Density * 8.f), 1, 12);
	UStaticMesh* CubeMesh = Cast<UStaticMesh>(
		StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
		                 TEXT("/Engine/BasicShapes/Cube.Cube")));

	int32 Placed = 0;
	for (int32 i = 0; i < PropCount; ++i)
	{
		FVector Loc = FindPropPlacementPoint(World, RoomCenter, Radius);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(),
			FTransform(FRotator::ZeroRotator, Loc), SP);
		if (SMA)
		{
			if (CubeMesh && SMA->GetStaticMeshComponent())
				SMA->GetStaticMeshComponent()->SetStaticMesh(CubeMesh);
			// Props are small (50x50x50 cm).
			SMA->SetActorScale3D(FVector(0.5f, 0.5f, 0.5f));
			SMA->SetActorLabel(
				FString::Printf(TEXT("Prop_Room%02d_%s_%02d"), RoomIndex + 1, *StoryTheme.Left(8), i + 1));
			++Placed;
		}
	}
	return Placed;
#else
	return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase III — ApplySetDressingAndStorytelling
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::ApplySetDressingAndStorytelling(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ToJson(ErrObj(TEXT("GEditor not available."))); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)   { return ToJson(ErrObj(TEXT("No editor world."))); }

	FString StoryTheme = TEXT("generic");
	double  PropDensity = 0.5;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("story_theme"),  StoryTheme);
		Args->TryGetNumberField(TEXT("prop_density"), PropDensity);
	}
	const float Density = FMath::Clamp(static_cast<float>(PropDensity), 0.f, 1.f);

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "SetDressing", "AgentForge: Set Dressing Pass"));

	// Find all room-type actors (Blockout_Room_* or Arch_Room_*).
	TArray<AActor*> RoomActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		const FString Lbl = A->GetActorLabel();
		if (Lbl.StartsWith(TEXT("Blockout_Room_")) || Lbl.StartsWith(TEXT("Arch_Room_")))
			RoomActors.Add(A);
	}

	int32 TotalProps     = 0;
	int32 MicroStories   = 0;
	int32 RoomsDressed   = 0;

	for (int32 i = 0; i < RoomActors.Num(); ++i)
	{
		FVector Origin, Extent;
		RoomActors[i]->GetActorBounds(false, Origin, Extent);
		const float Radius = FMath::Max(Extent.X, Extent.Y);

		const int32 PropsInRoom = ScatterPropsInRoom(World, Origin, Radius, Density, StoryTheme, i);
		TotalProps += PropsInRoom;
		if (PropsInRoom >= 3) { ++MicroStories; }
		++RoomsDressed;
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),           true);
	Resp->SetStringField(TEXT("story_theme"),  StoryTheme);
	Resp->SetNumberField(TEXT("props_placed"), static_cast<double>(TotalProps));
	Resp->SetNumberField(TEXT("micro_stories"), static_cast<double>(MicroStories));
	Resp->SetNumberField(TEXT("rooms_dressed"), static_cast<double>(RoomsDressed));
	Resp->SetNumberField(TEXT("prop_density"), PropDensity);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase IV helpers
// ─────────────────────────────────────────────────────────────────────────────
int32 FLevelPipelineModule::SetupKeyLighting(UWorld* World, const FString& TimeOfDay,
                                              const FString& Mood,
                                              const FLevelPreset& Preset)
{
#if WITH_EDITOR
	int32 LightsPlaced = 0;
	const bool bNight = TimeOfDay.Contains(TEXT("night")) || TimeOfDay.Contains(TEXT("midnight"));
	const bool bFearful = Mood.Contains(TEXT("fear")) || Mood.Contains(TEXT("horror")) || Mood.Contains(TEXT("dark"));

	// Key directional / sky light.
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ADirectionalLight* DL = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			FTransform(FRotator(bNight ? -20.f : -45.f, 45.f, 0.f), FVector::ZeroVector), SP);
		if (DL)
		{
			if (UDirectionalLightComponent* DLC = DL->GetComponent())
			{
				// Night: very dim cool; day: bright warm.
				DLC->Intensity   = bNight ? (bFearful ? 0.05f : 0.3f) : 5.f;
				DLC->LightColor  = bNight
				                   ? FColor(180, 180, 220)   // moonlight blue
				                   : FColor(255, 240, 200);  // warm sun
				DLC->bCastShadows = true;
			}
			DL->SetActorLabel(TEXT("Pipeline_KeyLight"));
			++LightsPlaced;
		}
	}

	// Fill point lights — one per room.
	TArray<AActor*> RoomActors;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsValid(*It) &&
		    ((*It)->GetActorLabel().StartsWith(TEXT("Blockout_Room_")) ||
		     (*It)->GetActorLabel().StartsWith(TEXT("Arch_Room_"))))
		{
			RoomActors.Add(*It);
		}
	}

	for (int32 i = 0; i < RoomActors.Num(); ++i)
	{
		FVector Origin, Extent;
		RoomActors[i]->GetActorBounds(false, Origin, Extent);
		const FVector LightPos = Origin + FVector(0.f, 0.f, Extent.Z * 0.6f);

		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APointLight* PL = World->SpawnActor<APointLight>(
			APointLight::StaticClass(),
			FTransform(FRotator::ZeroRotator, LightPos), SP);
		if (PL)
		{
			if (UPointLightComponent* PLC = PL->GetLightComponent())
			{
				const float BaseIntensity = bNight ? 800.f : 2000.f;
				PLC->Intensity   = BaseIntensity * Preset.AmbientIntensityMultiplier;
				PLC->AttenuationRadius = FMath::Max(Extent.X, Extent.Y) * 1.5f;
				// Apply ambient tint from preset.
				const FLinearColor& AC = Preset.AmbientLightColor;
				PLC->LightColor = FColor(
					FMath::Clamp(static_cast<int32>(AC.R * 255.f + (bFearful ? 0 : 50)), 0, 255),
					FMath::Clamp(static_cast<int32>(AC.G * 255.f + (bFearful ? 0 : 50)), 0, 255),
					FMath::Clamp(static_cast<int32>(AC.B * 255.f + (bFearful ? 30 : 50)), 0, 255));
				PLC->bCastShadows = true;
			}
			PL->SetActorLabel(FString::Printf(TEXT("Pipeline_FillLight_%02d"), i + 1));
			++LightsPlaced;
		}
	}

	// God-ray spot if requested.
	if (Preset.bEnableGodRays)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		FVector GodRayPos = RoomActors.IsEmpty()
		                    ? FVector(0.f, 0.f, 800.f)
		                    : RoomActors[0]->GetActorLocation() + FVector(0.f, 0.f, 700.f);
		ASpotLight* SL = World->SpawnActor<ASpotLight>(
			ASpotLight::StaticClass(),
			FTransform(FRotator(-90.f, 0.f, 0.f), GodRayPos), SP);
		if (SL)
		{
			if (USpotLightComponent* SLC = Cast<USpotLightComponent>(SL->GetLightComponent()))
			{
				SLC->Intensity          = 3000.f * Preset.AmbientIntensityMultiplier;
				SLC->InnerConeAngle     = 5.f;
				SLC->OuterConeAngle     = 20.f;
				SLC->bUseInverseSquaredFalloff = true;
				SLC->LightColor         = FColor(220, 220, 255);
			}
			SL->SetActorLabel(TEXT("Pipeline_GodRay"));
			++LightsPlaced;
		}
	}
	return LightsPlaced;
#else
	return 0;
#endif
}

void FLevelPipelineModule::ApplyAtmosphericScattering(UWorld* World,
                                                       float FogDensity,
                                                       FLinearColor FogColor)
{
#if WITH_EDITOR
	// Reuse existing fog or spawn a new one.
	AExponentialHeightFog* Fog = nullptr;
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		Fog = *It; break;
	}
	if (!Fog)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Fog = World->SpawnActor<AExponentialHeightFog>(
			AExponentialHeightFog::StaticClass(),
			FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, -500.f)), SP);
		if (Fog) { Fog->SetActorLabel(TEXT("Pipeline_HeightFog")); }
	}
	if (Fog && Fog->GetComponent())
	{
		UExponentialHeightFogComponent* FogComp = Fog->GetComponent();
		FogComp->SetFogDensity(FogDensity);
		FogComp->SetFogInscatteringColor(FogColor);
	}
#endif
}

float FLevelPipelineModule::ComputeHorrorScore(UWorld* World)
{
#if WITH_EDITOR
	float Score = 0.f;

	// Count lights and sum their intensity.
	float TotalIntensity = 0.f;
	int32 LightCount     = 0;
	for (TActorIterator<APointLight> It(World); It; ++It)
	{
		if (UPointLightComponent* PLC = (*It)->GetLightComponent())
		{
			TotalIntensity += PLC->Intensity;
			++LightCount;
		}
	}

	// Darker levels → higher horror score (up to 40 points).
	const float AvgIntensity = LightCount > 0 ? TotalIntensity / static_cast<float>(LightCount) : 2000.f;
	Score += FMath::Clamp((2000.f - AvgIntensity) / 2000.f * 40.f, 0.f, 40.f);

	// Fog presence (up to 30 points).
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		if (UExponentialHeightFogComponent* FC = (*It)->GetComponent())
		{
			Score += FMath::Clamp(FC->FogDensity / 0.05f * 30.f, 0.f, 30.f);
		}
		break;
	}

	// Props in level (up to 30 points).
	int32 PropCount = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if ((*It)->GetActorLabel().StartsWith(TEXT("Prop_"))) { ++PropCount; }
	}
	Score += FMath::Clamp(static_cast<float>(PropCount) / 30.f * 30.f, 0.f, 30.f);

	return FMath::Clamp(Score, 0.f, 100.f);
#else
	return 0.f;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase IV — ApplyProfessionalLightingAndAtmosphere
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::ApplyProfessionalLightingAndAtmosphere(
	const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ToJson(ErrObj(TEXT("GEditor not available."))); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)   { return ToJson(ErrObj(TEXT("No editor world."))); }

	FString TimeOfDay    = TEXT("midnight");
	FString Mood         = TEXT("fearful");
	bool    bGodRays     = false;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("time_of_day"),      TimeOfDay);
		Args->TryGetStringField(TEXT("mood"),             Mood);
		Args->TryGetBoolField  (TEXT("enable_god_rays"),  bGodRays);
	}

	const FLevelPreset& Preset = FLevelPresetSystem::GetCurrentPresetData();

	// Override god-ray setting from args.
	FLevelPreset PPreset = Preset;
	if (Args.IsValid()) { PPreset.bEnableGodRays = bGodRays; }

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "LightingAtmos", "AgentForge: Lighting & Atmosphere Pass"));

	const int32 LightsPlaced = SetupKeyLighting(World, TimeOfDay, Mood, PPreset);

	// Fog density: night fearful = heavy.
	const bool bNight   = TimeOfDay.Contains(TEXT("night")) || TimeOfDay.Contains(TEXT("midnight"));
	const bool bFearful = Mood.Contains(TEXT("fear")) || Mood.Contains(TEXT("horror")) || Mood.Contains(TEXT("dark"));
	const float FogDensity = bNight && bFearful ? 0.04f : (bNight ? 0.02f : 0.005f);
	const FLinearColor FogColor = Preset.AmbientLightColor;
	ApplyAtmosphericScattering(World, FogDensity, FogColor);

	// Determine atmosphere string.
	FString AtmosphereStr;
	if (bNight && bFearful)   { AtmosphereStr = TEXT("dark_fearful_midnight"); }
	else if (bNight)          { AtmosphereStr = TEXT("calm_night"); }
	else if (bFearful)        { AtmosphereStr = TEXT("tense_daylight"); }
	else                      { AtmosphereStr = TEXT("neutral"); }

	const float HorrorScore = ComputeHorrorScore(World);

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),            true);
	Resp->SetStringField(TEXT("time_of_day"),   TimeOfDay);
	Resp->SetStringField(TEXT("mood"),          Mood);
	Resp->SetNumberField(TEXT("lights_placed"), static_cast<double>(LightsPlaced));
	Resp->SetNumberField(TEXT("horror_score"),  HorrorScore);
	Resp->SetStringField(TEXT("atmosphere"),    AtmosphereStr);
	Resp->SetNumberField(TEXT("fog_density"),   FogDensity);
	Resp->SetBoolField  (TEXT("god_rays"),      PPreset.bEnableGodRays);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase V helpers
// ─────────────────────────────────────────────────────────────────────────────
int32 FLevelPipelineModule::SpawnAmbientParticles(UWorld* World,
                                                   const TArray<FString>& VfxNames,
                                                   float Density)
{
#if WITH_EDITOR
	// Collect room centres.
	TArray<FVector> RoomCenters;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsValid(*It) && ((*It)->GetActorLabel().StartsWith(TEXT("Blockout_Room_")) ||
		                     (*It)->GetActorLabel().StartsWith(TEXT("Arch_Room_"))))
		{
			RoomCenters.Add((*It)->GetActorLocation());
		}
	}

	const int32 PerRoom  = FMath::Max(1, FMath::RoundToInt(Density * 3.f));
	int32 TotalSpawned   = 0;
	FRandomStream RS(12345);

	for (const FVector& Center : RoomCenters)
	{
		for (int32 p = 0; p < PerRoom; ++p)
		{
			const int32  VfxIdx  = (VfxNames.IsEmpty()) ? 0 : (TotalSpawned % VfxNames.Num());
			const FString VfxName = VfxNames.IsEmpty() ? TEXT("dust") : VfxNames[VfxIdx];
			const FVector Loc(
				Center.X + RS.FRandRange(-300.f, 300.f),
				Center.Y + RS.FRandRange(-300.f, 300.f),
				Center.Z + RS.FRandRange(50.f, 200.f));

			// Spawn as a StaticMeshActor stand-in (sphere) labeled VFX_name_NN.
			// A real project would use ANiagaraActor — that requires Niagara includes
			// and a specific Niagara system asset.  We label it clearly so the pipeline
			// report is accurate and users can swap to a real NS asset.
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			UStaticMesh* SphereMesh = Cast<UStaticMesh>(
				StaticLoadObject(UStaticMesh::StaticClass(), nullptr,
				                 TEXT("/Engine/BasicShapes/Sphere.Sphere")));
			AStaticMeshActor* VfxActor = World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(),
				FTransform(FRotator::ZeroRotator, Loc), SP);
			if (VfxActor)
			{
				if (SphereMesh && VfxActor->GetStaticMeshComponent())
					VfxActor->GetStaticMeshComponent()->SetStaticMesh(SphereMesh);
				VfxActor->SetActorScale3D(FVector(0.1f));
				VfxActor->SetActorLabel(
					FString::Printf(TEXT("VFX_%s_%02d"), *VfxName, TotalSpawned + 1));
				++TotalSpawned;
			}
		}
	}
	return TotalSpawned;
#else
	return 0;
#endif
}

int32 FLevelPipelineModule::PlaceAmbientAudioEmitters(UWorld* World,
                                                       const FString& Soundscape)
{
#if WITH_EDITOR
	int32 Placed = 0;
	// Place one AudioVolume-proxy (StaticMeshActor, cube) per room.
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!IsValid(A)) { continue; }
		if (A->GetActorLabel().StartsWith(TEXT("Blockout_Room_")) ||
		    A->GetActorLabel().StartsWith(TEXT("Arch_Room_")))
		{
			FVector Origin, Extent;
			A->GetActorBounds(false, Origin, Extent);

			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AActor* AudioProxy = World->SpawnActor<AStaticMeshActor>(
				AStaticMeshActor::StaticClass(),
				FTransform(FRotator::ZeroRotator, Origin), SP);
			if (AudioProxy)
			{
				AudioProxy->SetActorScale3D(FVector(Extent.X / 50.f, Extent.Y / 50.f, 1.f));
				AudioProxy->SetActorLabel(
					FString::Printf(TEXT("Audio_%s_%02d"), *Soundscape.Left(12), Placed + 1));
				++Placed;
			}
		}
	}
	return Placed;
#else
	return 0;
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase V — AddLivingSystemsAndPolish
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::AddLivingSystemsAndPolish(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ToJson(ErrObj(TEXT("GEditor not available."))); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)   { return ToJson(ErrObj(TEXT("No editor world."))); }

	TArray<FString> VfxNames = { TEXT("dust") };
	FString Soundscape = TEXT("ambient");
	if (Args.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* VfxArr;
		if (Args->TryGetArrayField(TEXT("ambient_vfx"), VfxArr))
		{
			VfxNames.Empty();
			for (const auto& V : *VfxArr) { FString S; V->TryGetString(S); VfxNames.Add(S); }
		}
		Args->TryGetStringField(TEXT("soundscape"), Soundscape);
	}

	const FLevelPreset& Preset = FLevelPresetSystem::GetCurrentPresetData();

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "LivingSystems", "AgentForge: Living Systems Pass"));

	int32 VfxPlaced   = 0;
	int32 AudioPlaced = 0;

	if (Preset.bEnableAmbientParticles)
		VfxPlaced = SpawnAmbientParticles(World, VfxNames, Preset.ParticleDensity);

	if (Preset.bEnableAmbientSound)
		AudioPlaced = PlaceAmbientAudioEmitters(World, Soundscape);

	TArray<TSharedPtr<FJsonValue>> VfxArr2;
	for (const FString& V : VfxNames) VfxArr2.Add(MakeShared<FJsonValueString>(V));

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),           true);
	Resp->SetNumberField(TEXT("vfx_placed"),   static_cast<double>(VfxPlaced));
	Resp->SetNumberField(TEXT("audio_placed"), static_cast<double>(AudioPlaced));
	Resp->SetArrayField (TEXT("vfx_names"),    VfxArr2);
	Resp->SetStringField(TEXT("soundscape"),   Soundscape);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
//  Quality Evaluation
// ─────────────────────────────────────────────────────────────────────────────
float FLevelPipelineModule::EvaluateLevelQuality(UWorld* World, const FLevelPreset& Preset)
{
#if WITH_EDITOR
	float Score = 0.f;
	constexpr float TotalWeight = 5.f;

	// 1. Actor count in valid range (1 point).
	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (IsValid(*It)) { ++ActorCount; }
	}
	if (ActorCount >= Preset.MinActorCount && ActorCount <= Preset.MaxActorCount)
		Score += 1.f;

	// 2. Lighting — at least one fill light (1 point).
	int32 LightCount = 0;
	for (TActorIterator<APointLight> It(World); It; ++It)
	{
		if (IsValid(*It)) { ++LightCount; }
	}
	if (LightCount > 0) { Score += 1.f; }

	// 3. PlayerStart present (1 point).
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (IsValid(*It)) { Score += 1.f; break; }
	}

	// 4. NavMesh volume present (1 point).
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{
		if (IsValid(*It)) { Score += 1.f; break; }
	}

	// 5. Horror score meets preset minimum (1 point).
	if (Preset.MinHorrorScore > 0.f)
	{
		if (ComputeHorrorScore(World) >= Preset.MinHorrorScore)
			Score += 1.f;
	}
	else
	{
		Score += 1.f;  // No genre requirement — full point.
	}

	return FMath::Clamp(Score / TotalWeight, 0.f, 1.f);
#else
	return 0.5f;
#endif
}

TSharedPtr<FJsonObject> FLevelPipelineModule::BuildQualityReport(UWorld* World,
                                                                   const FLevelPreset& Preset)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
#if WITH_EDITOR
	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{ if (IsValid(*It)) { ++ActorCount; } }

	int32 LightCount = 0;
	for (TActorIterator<APointLight> It(World); It; ++It)
	{ if (IsValid(*It)) { ++LightCount; } }

	bool bHasPlayerStart = false;
	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{ if (IsValid(*It)) { bHasPlayerStart = true; break; } }

	bool bHasNavMesh = false;
	for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
	{ if (IsValid(*It)) { bHasNavMesh = true; break; } }

	const float HorrorSc = ComputeHorrorScore(World);

	R->SetNumberField(TEXT("actor_count"),       static_cast<double>(ActorCount));
	R->SetNumberField(TEXT("light_count"),       static_cast<double>(LightCount));
	R->SetBoolField  (TEXT("has_player_start"),  bHasPlayerStart);
	R->SetBoolField  (TEXT("has_navmesh"),       bHasNavMesh);
	R->SetNumberField(TEXT("horror_score"),      HorrorSc);
	R->SetNumberField(TEXT("quality_score"),     EvaluateLevelQuality(World, Preset));
#endif
	return R;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Master Orchestrator — GenerateFullQualityLevel
// ─────────────────────────────────────────────────────────────────────────────
FString FLevelPipelineModule::GenerateFullQualityLevel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ToJson(ErrObj(TEXT("GEditor not available."))); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)   { return ToJson(ErrObj(TEXT("No editor world."))); }

	// ── Parse top-level args ─────────────────────────────────────────────────
	FString Mission           = TEXT("Create a level");
	FString PresetName        = TEXT("Default");
	double  MaxIterationsD    = 2.0;
	double  QualityThresholdD = 0.75;

	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("mission"),           Mission);
		Args->TryGetStringField(TEXT("preset"),            PresetName);
		Args->TryGetNumberField(TEXT("max_iterations"),    MaxIterationsD);
		Args->TryGetNumberField(TEXT("quality_threshold"), QualityThresholdD);
	}

	const int32 MaxIter    = FMath::Clamp(static_cast<int32>(MaxIterationsD), 1, 5);
	const float QualThresh = FMath::Clamp(static_cast<float>(QualityThresholdD), 0.f, 1.f);

	// ── Load preset ───────────────────────────────────────────────────────────
	if (FLevelPresetSystem::LoadedPresets.Num() == 0)
		FLevelPresetSystem::RegisterBuiltinPresets();
	FLevelPresetSystem::SetCurrentPreset(PresetName);
	const FLevelPreset& Preset = FLevelPresetSystem::GetCurrentPresetData();

	// ── Infer phase args from master args ─────────────────────────────────────
	// Phase I.
	TSharedPtr<FJsonObject> P1Args = MakeShared<FJsonObject>();
	P1Args->SetStringField(TEXT("mission"),    Mission);
	P1Args->SetStringField(TEXT("preset"),     PresetName);
	P1Args->SetNumberField(TEXT("room_count"), 3.0);
	P1Args->SetNumberField(TEXT("grid_size"),  400.0);
	if (Args.IsValid())
	{
		double RC = 3.0; Args->TryGetNumberField(TEXT("room_count"), RC);
		P1Args->SetNumberField(TEXT("room_count"), RC);
		double GS = 400.0; Args->TryGetNumberField(TEXT("grid_size"), GS);
		P1Args->SetNumberField(TEXT("grid_size"), GS);
	}

	// Phase III.
	TSharedPtr<FJsonObject> P3Args = MakeShared<FJsonObject>();
	P3Args->SetStringField(TEXT("story_theme"),  Mission.Left(20));
	P3Args->SetNumberField(TEXT("prop_density"),  Preset.SetDressingDensity);

	// Phase IV.
	TSharedPtr<FJsonObject> P4Args = MakeShared<FJsonObject>();
	P4Args->SetStringField(TEXT("time_of_day"),     TEXT("midnight"));
	P4Args->SetStringField(TEXT("mood"),            TEXT("fearful"));
	P4Args->SetBoolField  (TEXT("enable_god_rays"), Preset.bEnableGodRays);
	if (Args.IsValid())
	{
		FString TOD; if (Args->TryGetStringField(TEXT("time_of_day"), TOD)) P4Args->SetStringField(TEXT("time_of_day"), TOD);
		FString Md;  if (Args->TryGetStringField(TEXT("mood"),        Md))  P4Args->SetStringField(TEXT("mood"),        Md);
	}

	// Phase V.
	TSharedPtr<FJsonObject> P5Args = MakeShared<FJsonObject>();
	{
		TArray<TSharedPtr<FJsonValue>> VfxArr;
		VfxArr.Add(MakeShared<FJsonValueString>(TEXT("dust")));
		VfxArr.Add(MakeShared<FJsonValueString>(TEXT("embers")));
		P5Args->SetArrayField(TEXT("ambient_vfx"), VfxArr);
		P5Args->SetStringField(TEXT("soundscape"), TEXT("ambient_atmosphere"));
		if (Args.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrPtr;
			if (Args->TryGetArrayField(TEXT("ambient_vfx"), ArrPtr))
				P5Args->SetArrayField(TEXT("ambient_vfx"), *ArrPtr);
			FString SC; if (Args->TryGetStringField(TEXT("soundscape"), SC)) P5Args->SetStringField(TEXT("soundscape"), SC);
		}
	}

	// ── Phase I ───────────────────────────────────────────────────────────────
	const FString P1Result = CreateBlockoutLevel(P1Args);
	TSharedPtr<FJsonObject> P1Json;
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(P1Result);
		FJsonSerializer::Deserialize(R, P1Json);
	}

	// ── Phase II ──────────────────────────────────────────────────────────────
	// Use kit paths from preset if available.
	TSharedPtr<FJsonObject> P2Args = MakeShared<FJsonObject>();
	FString KitPath = Preset.PreferredModularKitPaths.IsEmpty()
	                  ? TEXT("/Game/")
	                  : Preset.PreferredModularKitPaths[0];
	if (Args.IsValid()) { FString KP; if (Args->TryGetStringField(TEXT("kit_path"), KP)) KitPath = KP; }
	P2Args->SetStringField(TEXT("kit_path"),  KitPath);
	P2Args->SetNumberField(TEXT("snap_grid"), 50.0);

	const FString P2Result = ConvertToWhiteboxModular(P2Args);
	TSharedPtr<FJsonObject> P2Json;
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(P2Result);
		FJsonSerializer::Deserialize(R, P2Json);
	}

	// ── Phase III ─────────────────────────────────────────────────────────────
	const FString P3Result = ApplySetDressingAndStorytelling(P3Args);
	TSharedPtr<FJsonObject> P3Json;
	{
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(P3Result);
		FJsonSerializer::Deserialize(R, P3Json);
	}

	// ── Phase IV + V with closed-loop refinement ─────────────────────────────
	float   QualScore    = 0.f;
	int32   Iteration    = 0;
	TSharedPtr<FJsonObject> P4Json, P5Json;

	do
	{
		++Iteration;
		const FString P4Result = ApplyProfessionalLightingAndAtmosphere(P4Args);
		{ TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(P4Result); FJsonSerializer::Deserialize(R, P4Json); }

		const FString P5Result = AddLivingSystemsAndPolish(P5Args);
		{ TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(P5Result); FJsonSerializer::Deserialize(R, P5Json); }

		QualScore = EvaluateLevelQuality(World, Preset);
	}
	while (QualScore < QualThresh && Iteration < MaxIter);

	// ── Screenshot ────────────────────────────────────────────────────────────
	FString ScreenshotPath;
	{
		const FString SSDir  = FPaths::ProjectSavedDir() / TEXT("Screenshots/WindowsEditor/");
		const FString SSFile = SSDir + FString::Printf(
			TEXT("Pipeline_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		IFileManager::Get().MakeDirectory(*SSDir, true);
		FScreenshotRequest::RequestScreenshot(SSFile, false, false);
		ScreenshotPath = SSFile;
	}

	// ── Save level ────────────────────────────────────────────────────────────
	bool bLevelSaved = false;
	{
		TArray<UPackage*> DirtyPackages;
		if (World->PersistentLevel)
			DirtyPackages.Add(World->PersistentLevel->GetOutermost());
		if (!DirtyPackages.IsEmpty())
			bLevelSaved = FEditorFileUtils::PromptForCheckoutAndSave(DirtyPackages, false, false);
	}

	// ── Quality report ────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> QualReport = BuildQualityReport(World, Preset);

	// ── Compose master response ───────────────────────────────────────────────
	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),                  true);
	Resp->SetStringField(TEXT("mission"),             Mission);
	Resp->SetStringField(TEXT("preset"),              PresetName);
	Resp->SetNumberField(TEXT("final_quality_score"), QualScore);
	Resp->SetNumberField(TEXT("iterations"),          static_cast<double>(Iteration));
	Resp->SetStringField(TEXT("screenshot_path"),     ScreenshotPath);
	Resp->SetBoolField  (TEXT("level_saved"),         bLevelSaved);
	if (P1Json.IsValid()) Resp->SetObjectField(TEXT("phase1"), P1Json);
	if (P2Json.IsValid()) Resp->SetObjectField(TEXT("phase2"), P2Json);
	if (P3Json.IsValid()) Resp->SetObjectField(TEXT("phase3"), P3Json);
	if (P4Json.IsValid()) Resp->SetObjectField(TEXT("phase4"), P4Json);
	if (P5Json.IsValid()) Resp->SetObjectField(TEXT("phase5"), P5Json);
	if (QualReport.IsValid()) Resp->SetObjectField(TEXT("quality_report"), QualReport);
	return ToJson(Resp);
#else
	return ToJson(ErrObj(TEXT("WITH_EDITOR required.")));
#endif
}
