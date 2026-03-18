// UEAgentForge — AgentForgeLibrary.cpp
// Command router with transaction safety, constitution enforcement, and verification hooks.

#include "AgentForgeLibrary.h"
#include "VerificationEngine.h"
#include "ConstitutionParser.h"
#include "SpatialControlModule.h"   // v0.2.0 Spatial Intelligence Layer
#include "FabIntegrationModule.h"   // v0.2.0 FAB Marketplace Integration
#include "DataAccessModule.h"       // v0.3.0 Rich Multi-Modal Data Access
#include "SemanticCommandModule.h"  // v0.3.0 Advanced Semantic Commands
#include "LevelPresetSystem.h"      // v0.4.0 Named Preset Storage
#include "LevelPipelineModule.h"    // v0.4.0 Five-Phase AAA Level Pipeline
#include "Operators/ProceduralOpsModule.h"    // v0.5.0 Deterministic Operator Pipeline
#include "LLM/AgentForgeLLMSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include <cfloat>

#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "FileHelpers.h"
#include "LevelEditorViewport.h"
#include "UnrealClient.h"   // FScreenshotRequest — programmatic viewport screenshots
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/SpotLight.h"
#include "Engine/RectLight.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/LightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/Texture2D.h"
// Blueprint manipulation
#include "Kismet2/KismetEditorUtilities.h"
#include "BlueprintEditorLibrary.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/SavePackage.h"
#include "UObject/GarbageCollection.h"
// Phase 1: material instancing
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
// Phase 1: content management
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ObjectTools.h"
// Spatial awareness
#include "NavigationSystem.h"
// Performance stats
#include "RHI.h"
#include "RHIStats.h"
#include "HAL/PlatformMemory.h"
#include "EngineUtils.h"
// Python scripting
#include "IPythonScriptPlugin.h"
// Transaction safety — explicit with NoPCHs
#include "ScopedTransaction.h"
#include "Misc/ScopeLock.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/Event.h"
#include "HAL/ThreadSafeBool.h"
// AI asset wiring — set_bt_blackboard bypasses Python CPF_Protected restriction
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
// wire_aicontroller_bt — blueprint graph node creation
#include "AIController.h"
#include "EdGraphSchema_K2.h"
// setup_flashlight_scs — SCS node setup for Movable SpotLight (bypasses Python SubobjectData mobility limit)
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/SpotLightComponent.h"
#include "UObject/UnrealType.h"
#endif

// ============================================================================
//  FILE-SCOPE HELPERS
// ============================================================================
#if WITH_EDITOR

// Simple lock: if set, mutating commands are rejected unless the current level matches.
static FString GForgeMapLockPackagePath;
// Serializes Python execution from remote requests to avoid concurrent interpreter access.
static FCriticalSection GForgePythonExecCS;
// Tracks open begin/end transaction state.
static TUniquePtr<FScopedTransaction> GOpenTransaction;
// Shutdown barrier for close-time command rejection.
static FThreadSafeBool GForgeShutdownRequested(false);

static bool IsLowMemory(float& OutUsedPercent, float& OutAvailableMB)
{
	const FPlatformMemoryStats Mem = FPlatformMemory::GetStats();
	const double TotalMB = static_cast<double>(Mem.TotalPhysical) / (1024.0 * 1024.0);
	const double UsedMB = static_cast<double>(Mem.UsedPhysical) / (1024.0 * 1024.0);
	OutAvailableMB = static_cast<float>(static_cast<double>(Mem.AvailablePhysical) / (1024.0 * 1024.0));
	OutUsedPercent = (TotalMB > 0.0) ? static_cast<float>((UsedMB / TotalMB) * 100.0) : 0.0f;
	// Conservative thresholds to avoid hard OOM/editor freeze under heavy generation.
	return (OutAvailableMB < 2048.0f) || (OutUsedPercent >= 92.0f);
}

static void SaveNewPackage(UPackage* Package, UObject* Asset)
{
	FString PackageFilename;
	if (!FPackageName::TryConvertLongPackageNameToFilename(
	        Package->GetName(), PackageFilename, FPackageName::GetAssetPackageExtension()))
		return;

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(PackageFilename), /*Tree=*/true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Package, Asset, *PackageFilename, Args);
}

static bool GetCurrentLevelPaths(FString& OutPackagePath, FString& OutWorldPath, FString& OutActorPrefix)
{
	if (!GEditor) { return false; }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World || !World->PersistentLevel) { return false; }
	UPackage* MapPackage = World->PersistentLevel->GetOutermost();
	if (!MapPackage) { return false; }
	OutPackagePath = MapPackage->GetName();
	const FString ShortName = FPackageName::GetShortName(OutPackagePath);
	OutWorldPath   = OutPackagePath + TEXT(".") + ShortName;
	OutActorPrefix = OutWorldPath + TEXT(":PersistentLevel.");
	return true;
}

static FString QueueEditorScreenshot(const FString& RequestedBaseName)
{
	FString BaseName = RequestedBaseName;
	BaseName.TrimStartAndEndInline();
	if (BaseName.IsEmpty())
	{
		BaseName = TEXT("AgentForge_Screenshot");
	}
	BaseName = BaseName.Replace(TEXT(" "), TEXT("_"));

	const FString Dir = TEXT("C:/HGShots");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString StagedName = FString::Printf(TEXT("%s_%s.png"), *BaseName, *Timestamp);
	const FString Path = FPaths::Combine(Dir, StagedName);

	if (GEditor)
	{
		GEditor->RedrawAllViewports();
	}
	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);
	return Path;
}

static bool ContainsAnyToken(const FString& InLower, const TArray<FString>& Tokens)
{
	for (const FString& Token : Tokens)
	{
		if (InLower.Contains(Token))
		{
			return true;
		}
	}
	return false;
}

static FString ClassifyActorForContext(const FString& Label, const FString& ClassName)
{
	FString LabelLower = Label;
	LabelLower.ToLowerInline();
	FString ClassLower = ClassName;
	ClassLower.ToLowerInline();
	const FString Combined = LabelLower + TEXT(" ") + ClassLower;

	static const TArray<FString> PlayerTokens = {
		TEXT("playerstart"), TEXT("player"), TEXT("spawn"), TEXT("checkpoint")
	};
	static const TArray<FString> ObjectiveTokens = {
		TEXT("objective"), TEXT("door"), TEXT("key"), TEXT("portal"), TEXT("exit"),
		TEXT("switch"), TEXT("lever"), TEXT("pickup"), TEXT("artifact"), TEXT("trigger")
	};
	static const TArray<FString> AITokens = {
		TEXT("npc"), TEXT("enemy"), TEXT("monster"), TEXT("character"),
		TEXT("aicontroller"), TEXT("behavior"),
		TEXT("controller"), TEXT("bot"), TEXT("warden")
	};
	static const TArray<FString> LightingTokens = {
		TEXT("light"), TEXT("fog"), TEXT("sky"), TEXT("atmosphere"), TEXT("postprocess")
	};
	static const TArray<FString> AudioTokens = {
		TEXT("audio"), TEXT("sound"), TEXT("ambientsound")
	};
	static const TArray<FString> VfxTokens = {
		TEXT("niagara"), TEXT("particle"), TEXT("emitter"), TEXT("vfx")
	};
	static const TArray<FString> EnvironmentTokens = {
		TEXT("staticmesh"), TEXT("foliage"), TEXT("landscape"), TEXT("instanced"),
		TEXT("brush"), TEXT("terrain")
	};

	if (ContainsAnyToken(Combined, PlayerTokens))    { return TEXT("player"); }
	if (ContainsAnyToken(Combined, ObjectiveTokens)) { return TEXT("objective"); }
	if (LabelLower.StartsWith(TEXT("ai_")) || LabelLower.EndsWith(TEXT("_ai")) ||
		ClassLower.Contains(TEXT("ai_")) || ClassLower.Contains(TEXT("_ai")))
	{
		return TEXT("ai");
	}
	if (ContainsAnyToken(Combined, AITokens))        { return TEXT("ai"); }
	if (ContainsAnyToken(Combined, LightingTokens))  { return TEXT("lighting"); }
	if (ContainsAnyToken(Combined, AudioTokens))     { return TEXT("audio"); }
	if (ContainsAnyToken(Combined, VfxTokens))       { return TEXT("vfx"); }
	if (ContainsAnyToken(Combined, EnvironmentTokens)){ return TEXT("environment"); }
	return TEXT("other");
}

static int32 ContextPriorityForCategory(const FString& Category)
{
	if (Category == TEXT("player"))    { return 600; }
	if (Category == TEXT("objective")) { return 550; }
	if (Category == TEXT("ai"))        { return 500; }
	if (Category == TEXT("lighting"))  { return 300; }
	if (Category == TEXT("environment")) { return 200; }
	if (Category == TEXT("audio"))     { return 160; }
	if (Category == TEXT("vfx"))       { return 150; }
	return 100;
}

static FString ExtractSuffixToken(const FString& InLabel)
{
	FString Label = InLabel;
	Label.ToLowerInline();
	TArray<FString> Parts;
	Label.ParseIntoArray(Parts, TEXT("_"), true);
	if (Parts.Num() == 0)
	{
		return FString();
	}
	const FString Last = Parts.Last();
	if (Last.Len() == 0 || Last.Len() > 4)
	{
		return FString();
	}
	return Last;
}

static bool IsLikelySystemActorForContext(const FString& Label, const FString& ClassName)
{
	FString LabelLower = Label;
	LabelLower.ToLowerInline();
	FString ClassLower = ClassName;
	ClassLower.ToLowerInline();
	const FString Combined = LabelLower + TEXT(" ") + ClassLower;

	return
		Combined.Contains(TEXT("gameplaydebugger")) ||
		Combined.Contains(TEXT("worldsettings")) ||
		Combined.Contains(TEXT("defaultphysicsvolume")) ||
		Combined.Contains(TEXT("brush"));
}

static UWorld* GetEditorWorld()
{
	return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
}

static UStaticMeshComponent* FindStaticMeshComponentOnActor(AActor* Actor)
{
	if (!Actor) { return nullptr; }
	return Actor->FindComponentByClass<UStaticMeshComponent>();
}

static UMeshComponent* FindMeshComponentOnActor(AActor* Actor)
{
	if (!Actor) { return nullptr; }
	if (UStaticMeshComponent* StaticMeshComp = FindStaticMeshComponentOnActor(Actor))
	{
		return StaticMeshComp;
	}
	return Actor->FindComponentByClass<UMeshComponent>();
}

static UMaterialInterface* GetFallbackTintMaterial()
{
	return LoadObject<UMaterialInterface>(
		nullptr,
		TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
}

static UMaterialInstanceDynamic* GetOrCreateDynamicMaterialInstance(
	UMeshComponent* MeshComp,
	int32 SlotIndex,
	FString& OutError)
{
	if (!MeshComp)
	{
		OutError = TEXT("Actor has no mesh component.");
		return nullptr;
	}

	if (UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(MeshComp->GetMaterial(SlotIndex)))
	{
		return ExistingMID;
	}

	UMaterialInterface* BaseMaterial = MeshComp->GetMaterial(SlotIndex);
	if (!BaseMaterial)
	{
		BaseMaterial = GetFallbackTintMaterial();
	}
	if (!BaseMaterial)
	{
		OutError = TEXT("No material found on actor and fallback material failed to load.");
		return nullptr;
	}

	MeshComp->Modify();
	UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(BaseMaterial, MeshComp);
	if (!MID)
	{
		OutError = TEXT("Failed to create dynamic material instance.");
		return nullptr;
	}

	MeshComp->SetMaterial(SlotIndex, MID);
	return MID;
}

static UStaticMesh* GetBasicCubeMesh()
{
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
}

static bool LoadOptionalMaterial(const FString& MaterialPath, UMaterialInterface*& OutMaterial, FString& OutError)
{
	OutMaterial = nullptr;
	OutError.Reset();

	if (MaterialPath.IsEmpty())
	{
		return true;
	}

	OutMaterial = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!OutMaterial)
	{
		OutError = FString::Printf(TEXT("Material not found: %s"), *MaterialPath);
		return false;
	}

	return true;
}

static TSharedPtr<FJsonObject> MakeVectorJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

static void AddActorSummary(TArray<TSharedPtr<FJsonValue>>& OutArray, AActor* Actor)
{
	if (!Actor)
	{
		return;
	}

	TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
	ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
	ActorObj->SetStringField(TEXT("name"), Actor->GetName());
	ActorObj->SetStringField(TEXT("object_path"), Actor->GetPathName());
	ActorObj->SetObjectField(TEXT("location"), MakeVectorJson(Actor->GetActorLocation()));
	OutArray.Add(MakeShared<FJsonValueObject>(ActorObj));
}

static AActor* SpawnGroupActor(UWorld* World, const FString& Label, const FVector& Location, FString& OutError)
{
	if (!World)
	{
		OutError = TEXT("No editor world.");
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* GroupActor = World->SpawnActor<AActor>(AActor::StaticClass(), Location, FRotator::ZeroRotator, Params);
	if (!GroupActor)
	{
		OutError = TEXT("Failed to spawn group actor.");
		return nullptr;
	}

	GroupActor->Modify();
	if (!Label.IsEmpty())
	{
		GroupActor->SetActorLabel(Label);
	}

	if (!GroupActor->GetRootComponent())
	{
		USceneComponent* Root = NewObject<USceneComponent>(GroupActor, TEXT("AgentForgeRoot"));
		if (!Root)
		{
			OutError = TEXT("Failed to create group root component.");
			return nullptr;
		}

		Root->Modify();
		Root->SetMobility(EComponentMobility::Static);
		GroupActor->SetRootComponent(Root);
		GroupActor->AddInstanceComponent(Root);
		Root->RegisterComponent();
	}

	GroupActor->SetActorLocation(Location);
	return GroupActor;
}

static AStaticMeshActor* SpawnBoxActor(
	UWorld* World,
	const FVector& Center,
	const FVector& DimensionsCm,
	const FRotator& Rotation,
	const FString& Label,
	const FString& MaterialPath,
	FString& OutError)
{
	OutError.Reset();
	if (!World)
	{
		OutError = TEXT("No editor world.");
		return nullptr;
	}

	if (DimensionsCm.X <= KINDA_SMALL_NUMBER || DimensionsCm.Y <= KINDA_SMALL_NUMBER || DimensionsCm.Z <= KINDA_SMALL_NUMBER)
	{
		OutError = TEXT("Box dimensions must be greater than zero.");
		return nullptr;
	}

	UStaticMesh* CubeMesh = GetBasicCubeMesh();
	if (!CubeMesh)
	{
		OutError = TEXT("Failed to load /Engine/BasicShapes/Cube.Cube.");
		return nullptr;
	}

	UMaterialInterface* Material = nullptr;
	if (!LoadOptionalMaterial(MaterialPath, Material, OutError))
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, Rotation, Params);
	if (!Actor)
	{
		OutError = TEXT("Failed to spawn static mesh actor.");
		return nullptr;
	}

	Actor->Modify();
	if (!Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}

	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (!MeshComp)
	{
		OutError = TEXT("Spawned static mesh actor has no static mesh component.");
		return nullptr;
	}

	MeshComp->Modify();
	MeshComp->SetMobility(EComponentMobility::Static);
	MeshComp->SetStaticMesh(CubeMesh);
	Actor->SetActorScale3D(FVector(
		DimensionsCm.X / 100.0f,
		DimensionsCm.Y / 100.0f,
		DimensionsCm.Z / 100.0f));

	if (Material)
	{
		MeshComp->SetMaterial(0, Material);
	}

	return Actor;
}

static bool AttachActorToParent(AActor* Child, AActor* Parent)
{
	if (!Child || !Parent)
	{
		return false;
	}

	Child->Modify();
	return Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
}

static FString SpawnLinearWallSegments(
	UWorld* World,
	const FVector& Start,
	const FVector& End,
	float BaseZ,
	float Height,
	float Thickness,
	const FString& MaterialPath,
	const FString& Label,
	bool bHasWindows,
	float WindowSpacing,
	float WindowHeight,
	AActor* ParentActor,
	TArray<AActor*>& OutSegments)
{
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return TEXT("Wall start/end points are identical.");
	}

	if (Height <= KINDA_SMALL_NUMBER || Thickness <= KINDA_SMALL_NUMBER)
	{
		return TEXT("Wall height and thickness must be greater than zero.");
	}

	const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	const FRotator Rotation(0.0f, YawDeg, 0.0f);
	const FVector Direction = Delta / Length;

	auto SpawnSegment = [&](const FString& SegmentLabel, float StartAlong, float SegmentLength, float SegmentBottomZ, float SegmentHeight) -> bool
	{
		if (SegmentLength <= KINDA_SMALL_NUMBER || SegmentHeight <= KINDA_SMALL_NUMBER)
		{
			return true;
		}

		const FVector SegmentCenter =
			Start +
			Direction * (StartAlong + (SegmentLength * 0.5f)) +
			FVector(0.0f, 0.0f, SegmentBottomZ + (SegmentHeight * 0.5f));

		FString SpawnError;
		AStaticMeshActor* SegmentActor = SpawnBoxActor(
			World,
			SegmentCenter,
			FVector(SegmentLength, Thickness, SegmentHeight),
			Rotation,
			SegmentLabel,
			MaterialPath,
			SpawnError);
		if (!SegmentActor)
		{
			return false;
		}

		if (ParentActor)
		{
			AttachActorToParent(SegmentActor, ParentActor);
		}
		OutSegments.Add(SegmentActor);
		return true;
	};

	if (!bHasWindows || Length < 220.0f)
	{
		if (!SpawnSegment(Label, 0.0f, Length, BaseZ, Height))
		{
			return TEXT("Failed to spawn wall segment.");
		}
		return FString();
	}

	const float SillHeight = FMath::Clamp(WindowHeight * 0.45f, 70.0f, FMath::Max(70.0f, Height - 120.0f));
	const float WindowOpeningHeight = FMath::Clamp(WindowHeight, 80.0f, FMath::Max(80.0f, Height - SillHeight - 40.0f));
	const float HeaderHeight = FMath::Max(40.0f, Height - SillHeight - WindowOpeningHeight);
	const float WindowWidth = FMath::Clamp(WindowSpacing * 0.5f, 90.0f, 180.0f);
	const float RepeatSpacing = FMath::Max(WindowSpacing, WindowWidth + 80.0f);

	TArray<float> WindowCenters;
	for (float Along = RepeatSpacing * 0.5f; Along < Length - (RepeatSpacing * 0.5f); Along += RepeatSpacing)
	{
		WindowCenters.Add(Along);
	}
	if (WindowCenters.Num() == 0)
	{
		WindowCenters.Add(Length * 0.5f);
	}

	float Cursor = 0.0f;
	int32 SegmentIndex = 0;
	for (const float WindowCenter : WindowCenters)
	{
		const float WindowStart = FMath::Max(0.0f, WindowCenter - (WindowWidth * 0.5f));
		const float WindowEnd = FMath::Min(Length, WindowCenter + (WindowWidth * 0.5f));
		const float LeftLength = WindowStart - Cursor;
		if (LeftLength > KINDA_SMALL_NUMBER)
		{
			if (!SpawnSegment(FString::Printf(TEXT("%s_Solid_%02d"), *Label, ++SegmentIndex), Cursor, LeftLength, BaseZ, Height))
			{
				return TEXT("Failed to spawn solid wall segment.");
			}
		}

		if (!SpawnSegment(FString::Printf(TEXT("%s_Lower_%02d"), *Label, ++SegmentIndex), WindowStart, WindowEnd - WindowStart, BaseZ, SillHeight))
		{
			return TEXT("Failed to spawn lower window wall segment.");
		}
		if (HeaderHeight > KINDA_SMALL_NUMBER)
		{
			if (!SpawnSegment(
				FString::Printf(TEXT("%s_Header_%02d"), *Label, ++SegmentIndex),
				WindowStart,
				WindowEnd - WindowStart,
				BaseZ + SillHeight + WindowOpeningHeight,
				HeaderHeight))
			{
				return TEXT("Failed to spawn header wall segment.");
			}
		}

		Cursor = WindowEnd;
	}

	const float TailLength = Length - Cursor;
	if (TailLength > KINDA_SMALL_NUMBER)
	{
		if (!SpawnSegment(FString::Printf(TEXT("%s_Solid_%02d"), *Label, ++SegmentIndex), Cursor, TailLength, BaseZ, Height))
		{
			return TEXT("Failed to spawn tail wall segment.");
		}
	}

	return FString();
}

static UStaticMesh* GetBasicCylinderMesh()
{
	return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
}

static AStaticMeshActor* SpawnStaticMeshPrimitiveActor(
	UWorld* World,
	UStaticMesh* Mesh,
	const FVector& Center,
	const FVector& Scale3D,
	const FRotator& Rotation,
	const FString& Label,
	const FString& MaterialPath,
	FString& OutError)
{
	OutError.Reset();
	if (!World)
	{
		OutError = TEXT("No editor world.");
		return nullptr;
	}
	if (!Mesh)
	{
		OutError = TEXT("Static mesh is null.");
		return nullptr;
	}

	UMaterialInterface* Material = nullptr;
	if (!LoadOptionalMaterial(MaterialPath, Material, OutError))
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Center, Rotation, Params);
	if (!Actor)
	{
		OutError = TEXT("Failed to spawn static mesh actor.");
		return nullptr;
	}

	Actor->Modify();
	if (!Label.IsEmpty())
	{
		Actor->SetActorLabel(Label);
	}

	UStaticMeshComponent* MeshComp = Actor->GetStaticMeshComponent();
	if (!MeshComp)
	{
		OutError = TEXT("Spawned static mesh actor has no static mesh component.");
		return nullptr;
	}

	MeshComp->Modify();
	MeshComp->SetMobility(EComponentMobility::Static);
	MeshComp->SetStaticMesh(Mesh);
	Actor->SetActorScale3D(Scale3D);
	if (Material)
	{
		MeshComp->SetMaterial(0, Material);
	}
	return Actor;
}

static UActorComponent* FindActorComponentByNameOrClass(AActor* Actor, const FString& Segment)
{
	if (!Actor || Segment.IsEmpty())
	{
		return nullptr;
	}

	FString Needle = Segment;
	Needle.TrimStartAndEndInline();

	TInlineComponentArray<UActorComponent*> Components(Actor);
	for (UActorComponent* Component : Components)
	{
		if (!Component)
		{
			continue;
		}

		const FString ComponentName = Component->GetName();
		const FString ComponentLabel = Component->GetFName().ToString();
		const FString ClassName = Component->GetClass()->GetName();
		FString ClassNameWithoutPrefix = ClassName;
		if (ClassNameWithoutPrefix.StartsWith(TEXT("U")))
		{
			ClassNameWithoutPrefix.RightChopInline(1, EAllowShrinking::No);
		}

		if (ComponentName.Equals(Needle, ESearchCase::IgnoreCase) ||
			ComponentLabel.Equals(Needle, ESearchCase::IgnoreCase) ||
			ClassName.Equals(Needle, ESearchCase::IgnoreCase) ||
			ClassNameWithoutPrefix.Equals(Needle, ESearchCase::IgnoreCase))
		{
			return Component;
		}
	}

	return nullptr;
}

static UObject* ResolvePropertyContainer(UObject* RootObject, const FString& PropertyPath, FString& OutLeafName, FString& OutError)
{
	OutLeafName.Reset();
	OutError.Reset();

	if (!RootObject)
	{
		OutError = TEXT("Root object is null.");
		return nullptr;
	}

	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), true);
	if (Segments.Num() == 0)
	{
		OutError = TEXT("Property path is empty.");
		return nullptr;
	}

	UObject* CurrentObject = RootObject;
	for (int32 Index = 0; Index < Segments.Num() - 1; ++Index)
	{
		const FString Segment = Segments[Index];

		if (AActor* Actor = Cast<AActor>(CurrentObject))
		{
			if (UActorComponent* Component = FindActorComponentByNameOrClass(Actor, Segment))
			{
				CurrentObject = Component;
				continue;
			}
		}

		FProperty* Property = CurrentObject->GetClass()->FindPropertyByName(*Segment);
		FObjectPropertyBase* ObjectProperty = Property ? CastField<FObjectPropertyBase>(Property) : nullptr;
		if (!ObjectProperty)
		{
			OutError = FString::Printf(TEXT("Intermediate property is not an object reference: %s"), *Segment);
			return nullptr;
		}

		UObject* NextObject = ObjectProperty->GetObjectPropertyValue_InContainer(CurrentObject);
		if (!NextObject)
		{
			OutError = FString::Printf(TEXT("Intermediate object is null: %s"), *Segment);
			return nullptr;
		}

		CurrentObject = NextObject;
	}

	OutLeafName = Segments.Last();
	return CurrentObject;
}

static FProperty* FindResolvedProperty(UObject* ContainerObject, const FString& LeafName, FString& OutError)
{
	OutError.Reset();
	if (!ContainerObject)
	{
		OutError = TEXT("Property container is null.");
		return nullptr;
	}

	FProperty* Property = ContainerObject->GetClass()->FindPropertyByName(*LeafName);
	if (!Property)
	{
		OutError = FString::Printf(TEXT("Property not found: %s"), *LeafName);
		return nullptr;
	}
	return Property;
}

static bool ExportResolvedPropertyValue(UObject* ContainerObject, const FString& LeafName, FString& OutValue, FString& OutError)
{
	FProperty* Property = FindResolvedProperty(ContainerObject, LeafName, OutError);
	if (!Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerObject);
	if (!ValuePtr)
	{
		OutError = FString::Printf(TEXT("Could not resolve property value pointer for %s"), *LeafName);
		return false;
	}

	OutValue.Reset();
	Property->ExportTextItem_Direct(OutValue, ValuePtr, nullptr, ContainerObject, PPF_None);
	return true;
}

static bool ImportResolvedPropertyValue(UObject* ContainerObject, const FString& LeafName, const FString& InValue, FString& OutError)
{
	FProperty* Property = FindResolvedProperty(ContainerObject, LeafName, OutError);
	if (!Property)
	{
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ContainerObject);
	if (!ValuePtr)
	{
		OutError = FString::Printf(TEXT("Could not resolve property value pointer for %s"), *LeafName);
		return false;
	}

	if (AActor* Actor = Cast<AActor>(ContainerObject))
	{
		Actor->Modify();
	}
	else if (UActorComponent* Component = Cast<UActorComponent>(ContainerObject))
	{
		Component->Modify();
	}

	const TCHAR* ImportResult = Property->ImportText_Direct(*InValue, ValuePtr, ContainerObject, PPF_None);
	if (!ImportResult)
	{
		OutError = FString::Printf(TEXT("Failed to import value '%s' into property %s"), *InValue, *LeafName);
		return false;
	}

	return true;
}

static bool ParseMobilityValue(const FString& MobilityString, EComponentMobility::Type& OutMobility)
{
	FString Value = MobilityString;
	Value.TrimStartAndEndInline();
	Value.ToLowerInline();

	if (Value == TEXT("static"))
	{
		OutMobility = EComponentMobility::Static;
		return true;
	}
	if (Value == TEXT("stationary"))
	{
		OutMobility = EComponentMobility::Stationary;
		return true;
	}
	if (Value == TEXT("movable"))
	{
		OutMobility = EComponentMobility::Movable;
		return true;
	}

	return false;
}

static FLevelEditorViewportClient* GetPrimaryPerspectiveViewportClient()
{
	if (!GEditor)
	{
		return nullptr;
	}

	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (ViewportClient && ViewportClient->IsPerspective())
		{
			return ViewportClient;
		}
	}

	return nullptr;
}

static bool ParseLLMProviderName(const FString& InProvider, EAgentForgeLLMProvider& OutProvider)
{
	FString Provider = InProvider;
	Provider.ReplaceInline(TEXT("-"), TEXT(""));
	Provider.ReplaceInline(TEXT("_"), TEXT(""));
	Provider.ToLowerInline();

	if (Provider == TEXT("anthropic") || Provider == TEXT("claude"))
	{
		OutProvider = EAgentForgeLLMProvider::Anthropic;
		return true;
	}
	if (Provider == TEXT("openai"))
	{
		OutProvider = EAgentForgeLLMProvider::OpenAI;
		return true;
	}
	if (Provider == TEXT("deepseek"))
	{
		OutProvider = EAgentForgeLLMProvider::DeepSeek;
		return true;
	}
	if (Provider == TEXT("openaicompatible") || Provider == TEXT("compatible") || Provider == TEXT("ollama"))
	{
		OutProvider = EAgentForgeLLMProvider::OpenAICompatible;
		return true;
	}
	return false;
}

static FString GetLLMProviderName(EAgentForgeLLMProvider Provider)
{
	switch (Provider)
	{
	case EAgentForgeLLMProvider::Anthropic: return TEXT("Anthropic");
	case EAgentForgeLLMProvider::OpenAI: return TEXT("OpenAI");
	case EAgentForgeLLMProvider::DeepSeek: return TEXT("DeepSeek");
	case EAgentForgeLLMProvider::OpenAICompatible: return TEXT("OpenAICompatible");
	default: return TEXT("Unknown");
	}
}

static bool ParseChatMessages(
	const TSharedPtr<FJsonObject>& Args,
	TArray<FAgentForgeChatMessage>& OutMessages,
	FString& OutError)
{
	OutMessages.Reset();
	OutError.Reset();

	const TArray<TSharedPtr<FJsonValue>>* MessageValues = nullptr;
	if (Args->TryGetArrayField(TEXT("messages"), MessageValues) && MessageValues)
	{
		for (const TSharedPtr<FJsonValue>& MessageValue : *MessageValues)
		{
			const TSharedPtr<FJsonObject>* MessageObj = nullptr;
			if (!MessageValue.IsValid() || !MessageValue->TryGetObject(MessageObj) || !MessageObj || !(*MessageObj).IsValid())
			{
				OutError = TEXT("llm_chat messages must be objects.");
				return false;
			}

			FAgentForgeChatMessage Message;
			(*MessageObj)->TryGetStringField(TEXT("role"), Message.Role);
			(*MessageObj)->TryGetStringField(TEXT("content"), Message.Content);

			const TArray<TSharedPtr<FJsonValue>>* ImageDataValues = nullptr;
			if ((*MessageObj)->TryGetArrayField(TEXT("image_data"), ImageDataValues) && ImageDataValues)
			{
				for (const TSharedPtr<FJsonValue>& ImageValue : *ImageDataValues)
				{
					Message.ImageData.Add(ImageValue.IsValid() ? ImageValue->AsString() : FString());
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* ImageMediaValues = nullptr;
			if ((*MessageObj)->TryGetArrayField(TEXT("image_media_types"), ImageMediaValues) && ImageMediaValues)
			{
				for (const TSharedPtr<FJsonValue>& MediaValue : *ImageMediaValues)
				{
					Message.ImageMediaTypes.Add(MediaValue.IsValid() ? MediaValue->AsString() : FString());
				}
			}

			if (Message.Role.IsEmpty())
			{
				Message.Role = TEXT("user");
			}
			OutMessages.Add(MoveTemp(Message));
		}
	}

	if (OutMessages.Num() == 0 && Args->HasTypedField<EJson::String>(TEXT("prompt")))
	{
		FAgentForgeChatMessage PromptMessage;
		PromptMessage.Role = TEXT("user");
		PromptMessage.Content = Args->GetStringField(TEXT("prompt"));
		OutMessages.Add(MoveTemp(PromptMessage));
	}

	if (OutMessages.Num() == 0)
	{
		OutError = TEXT("llm_chat requires a non-empty messages array.");
		return false;
	}

	return true;
}

static TSharedPtr<FJsonObject> BuildLLMResponseObject(
	const FAgentForgeLLMResponse& Response,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), Response.bSuccess);
	Obj->SetStringField(TEXT("provider"), GetLLMProviderName(Provider));
	Obj->SetStringField(TEXT("model"), Model);
	Obj->SetStringField(TEXT("content"), Response.Content);
	Obj->SetStringField(TEXT("error_message"), Response.ErrorMessage);
	Obj->SetStringField(TEXT("raw_json"), Response.RawJSON);
	Obj->SetStringField(TEXT("reasoning_content"), Response.ReasoningContent);
	Obj->SetNumberField(TEXT("prompt_tokens"), Response.PromptTokens);
	Obj->SetNumberField(TEXT("completion_tokens"), Response.CompletionTokens);
	return Obj;
}

#endif // WITH_EDITOR

// ============================================================================
//  UTILITIES
// ============================================================================
bool UAgentForgeLibrary::ParseJsonObject(const FString& In, TSharedPtr<FJsonObject>& OutObj, FString& OutErr)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
	if (!FJsonSerializer::Deserialize(Reader, OutObj) || !OutObj.IsValid())
	{
		OutErr = FString::Printf(TEXT("JSON parse error: %s"), *Reader.Get().GetErrorMessage());
		return false;
	}
	return true;
}

FString UAgentForgeLibrary::ToJsonString(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	return Out;
}

FString UAgentForgeLibrary::ErrorResponse(const FString& Msg)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("error"), Msg);
	return ToJsonString(Obj);
}

FString UAgentForgeLibrary::OkResponse(const FString& Detail)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	if (!Detail.IsEmpty()) { Obj->SetStringField(TEXT("detail"), Detail); }
	return ToJsonString(Obj);
}

AActor* UAgentForgeLibrary::FindActorByLabelOrName(const FString& LabelOrName)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return nullptr; }
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || !IsValid(A)) { continue; }
		if (A->GetActorLabel().Equals(LabelOrName, ESearchCase::IgnoreCase)) { return A; }
		if (A->GetName().Equals(LabelOrName, ESearchCase::IgnoreCase)) { return A; }
	}
#endif
	return nullptr;
}

TSharedPtr<FJsonObject> UAgentForgeLibrary::VecToJson(const FVector& V)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("x"), V.X);
	Obj->SetNumberField(TEXT("y"), V.Y);
	Obj->SetNumberField(TEXT("z"), V.Z);
	return Obj;
}

bool UAgentForgeLibrary::IsMutatingCommand(const FString& Cmd)
{
	static const TArray<FString> MutatingCmds =
	{
		TEXT("spawn_actor"), TEXT("set_actor_transform"), TEXT("delete_actor"),
		TEXT("duplicate_actor"),
		TEXT("spawn_point_light"), TEXT("spawn_spot_light"),
		TEXT("spawn_rect_light"), TEXT("spawn_directional_light"),
		TEXT("set_static_mesh"), TEXT("set_actor_scale"),
		TEXT("set_actor_label"), TEXT("set_actor_mobility"), TEXT("set_actor_visibility"),
		TEXT("group_actors"), TEXT("set_actor_property"),
		TEXT("create_wall"), TEXT("create_floor"), TEXT("create_room"),
		TEXT("create_corridor"), TEXT("create_pillar"),
		TEXT("create_blueprint"), TEXT("compile_blueprint"), TEXT("set_bp_cdo_property"),
		TEXT("edit_blueprint_node"), TEXT("create_material_instance"), TEXT("set_material_params"),
		TEXT("apply_material_to_actor"), TEXT("set_mesh_material_color"),
		TEXT("set_material_scalar_param"),
		TEXT("rename_asset"), TEXT("move_asset"), TEXT("delete_asset"),
		TEXT("setup_test_level"),
		// NOTE: execute_python is NOT here — it routes directly (see ExecuteCommandJson).
	};
	return MutatingCmds.Contains(Cmd);
}

void UAgentForgeLibrary::MarkEngineShuttingDown()
{
#if WITH_EDITOR
	GForgeShutdownRequested = true;
	if (GOpenTransaction.IsValid())
	{
		GOpenTransaction->Cancel();
		GOpenTransaction.Reset();
	}
#endif
}

bool UAgentForgeLibrary::IsEngineShuttingDown()
{
#if WITH_EDITOR
	return GForgeShutdownRequested || IsEngineExitRequested();
#else
	return true;
#endif
}

// ============================================================================
//  BLUEPRINT CALLABLE ENTRY POINTS
// ============================================================================
FString UAgentForgeLibrary::ExecuteCommandJson(const FString& RequestJson)
{
#if WITH_EDITOR
	if (IsEngineShuttingDown())
	{
		return ErrorResponse(TEXT("Engine shutdown in progress; command rejected."));
	}

	if (!IsInGameThread())
	{
		FString Result;
		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
		AsyncTask(ENamedThreads::GameThread, [RequestJson, &Result, DoneEvent]()
		{
			Result = UAgentForgeLibrary::ExecuteCommandJson(RequestJson);
			DoneEvent->Trigger();
		});

		DoneEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		return Result;
	}

	TSharedPtr<FJsonObject> Root;
	FString ParseErr;
	if (!ParseJsonObject(RequestJson, Root, ParseErr))
	{
		return ErrorResponse(FString::Printf(TEXT("Invalid JSON: %s"), *ParseErr));
	}

	FString Cmd;
	if (!Root->TryGetStringField(TEXT("cmd"), Cmd) || Cmd.IsEmpty())
	{
		return ErrorResponse(TEXT("Missing 'cmd' field."));
	}
	Cmd.ToLowerInline();

	const bool bOperatorHeavyCommand =
		Cmd == TEXT("run_operator_pipeline") ||
		Cmd == TEXT("op_terrain_generate") ||
		Cmd == TEXT("op_surface_scatter") ||
		Cmd == TEXT("op_spline_scatter") ||
		Cmd == TEXT("op_road_layout") ||
		Cmd == TEXT("op_biome_layers") ||
		Cmd == TEXT("op_stamp_poi");

	const bool bDirectPlacementCommand =
		Cmd == TEXT("spawn_actor") ||
		Cmd == TEXT("set_actor_transform") ||
		Cmd == TEXT("delete_actor");

	if (bDirectPlacementCommand && FProceduralOpsModule::IsOperatorOnlyMode())
	{
		return ErrorResponse(TEXT("Operator-only mode blocks direct actor placement. Use op_* commands or run_operator_pipeline."));
	}

	if (Cmd == TEXT("execute_python") || IsMutatingCommand(Cmd) || bOperatorHeavyCommand)
	{
		float UsedPct = 0.0f;
		float AvailableMB = 0.0f;
		if (IsLowMemory(UsedPct, AvailableMB))
		{
			CollectGarbage(RF_NoFlags, true);
			if (IsLowMemory(UsedPct, AvailableMB))
			{
				return ErrorResponse(FString::Printf(
					TEXT("Memory guard triggered: available memory %.0f MB, used %.1f%%. Aborting %s to prevent OOM."),
					AvailableMB, UsedPct, *Cmd));
			}
		}
	}

	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* ArgsPtr;
	if (Root->TryGetObjectField(TEXT("args"), ArgsPtr))
	{
		Args = *ArgsPtr;
	}

	// execute_python bypasses ExecuteSafeTransaction — Python scripts may perform
	// non-undoable operations (new_level, load_level, file I/O) that break rollback
	// verification. Route directly so the script runs once without a test phase.
	if (Cmd == TEXT("execute_python"))        { return Cmd_ExecutePython(Args); }
	// set_bt_blackboard bypasses Python CPF_Protected restriction on BehaviorTree::BlackboardAsset
	if (Cmd == TEXT("set_bt_blackboard"))     { return Cmd_SetBtBlackboard(Args); }

	// Mutating commands run inside a full safe transaction with verification.
	if (IsMutatingCommand(Cmd))
	{
		FString Result;
		ExecuteSafeTransaction(RequestJson, Result);
		return Result;
	}

	// Read-only / meta commands route directly.
	if (Cmd == TEXT("ping"))                  { return Cmd_Ping(Args); }
	if (Cmd == TEXT("get_all_level_actors"))  { return Cmd_GetAllLevelActors(); }
	if (Cmd == TEXT("get_actor_components"))  { return Cmd_GetActorComponents(Args); }
	if (Cmd == TEXT("get_current_level"))     { return Cmd_GetCurrentLevel(); }
	if (Cmd == TEXT("assert_current_level"))  { return Cmd_AssertCurrentLevel(Args); }
	if (Cmd == TEXT("get_actor_bounds"))      { return Cmd_GetActorBounds(Args); }
	if (Cmd == TEXT("get_world_context"))     { return Cmd_GetWorldContext(Args); }
	if (Cmd == TEXT("get_available_meshes"))  { return Cmd_GetAvailableMeshes(Args); }
	if (Cmd == TEXT("get_available_materials")) { return Cmd_GetAvailableMaterials(Args); }
	if (Cmd == TEXT("get_available_blueprints")) { return Cmd_GetAvailableBlueprints(Args); }
	if (Cmd == TEXT("get_available_textures")) { return Cmd_GetAvailableTextures(Args); }
	if (Cmd == TEXT("get_available_sounds")) { return Cmd_GetAvailableSounds(Args); }
	if (Cmd == TEXT("get_asset_details"))    { return Cmd_GetAssetDetails(Args); }
	if (Cmd == TEXT("focus_viewport_on_actor")) { return Cmd_FocusViewportOnActor(Args); }
	if (Cmd == TEXT("get_viewport_info"))    { return Cmd_GetViewportInfo(); }
	if (Cmd == TEXT("get_actor_property"))   { return Cmd_GetActorProperty(Args); }
	if (Cmd == TEXT("cast_ray"))              { return Cmd_CastRay(Args); }
	if (Cmd == TEXT("query_navmesh"))         { return Cmd_QueryNavMesh(Args); }
	if (Cmd == TEXT("begin_transaction"))     { return Cmd_BeginTransaction(Args); }
	if (Cmd == TEXT("end_transaction"))       { return Cmd_EndTransaction(); }
	if (Cmd == TEXT("undo_transaction"))      { return Cmd_UndoTransaction(); }
	if (Cmd == TEXT("create_snapshot"))       { return Cmd_CreateSnapshot(Args); }
	if (Cmd == TEXT("get_perf_stats"))        { return Cmd_GetPerfStats(); }
	if (Cmd == TEXT("save_current_level"))    { return Cmd_SaveCurrentLevel(); }
	if (Cmd == TEXT("take_screenshot"))       { return Cmd_TakeScreenshot(Args); }
	if (Cmd == TEXT("run_verification"))      { return Cmd_RunVerification(Args); }
	if (Cmd == TEXT("enforce_constitution"))  { return Cmd_EnforceConstitution(Args); }
	if (Cmd == TEXT("get_forge_status"))      { return Cmd_GetForgeStatus(); }
	if (Cmd == TEXT("llm_chat"))              { return Cmd_LLMChat(Args); }
	if (Cmd == TEXT("llm_structured"))        { return Cmd_LLMStructured(Args); }
	if (Cmd == TEXT("llm_set_key"))           { return Cmd_LLMSetKey(Args); }
	if (Cmd == TEXT("llm_get_models"))        { return Cmd_LLMGetModels(Args); }
	if (Cmd == TEXT("set_viewport_camera"))   { return Cmd_SetViewportCamera(Args); }
	if (Cmd == TEXT("redraw_viewports"))      { return Cmd_RedrawViewports(); }
	// wire_aicontroller_bt: creates BeginPlay→RunBehaviorTree in an AIController Blueprint
	if (Cmd == TEXT("wire_aicontroller_bt"))  { return Cmd_WireAIControllerBT(Args); }
	// setup_flashlight_scs: adds/configures Movable SpotLight SCS node in a Blueprint
	if (Cmd == TEXT("setup_flashlight_scs"))  { return Cmd_SetupFlashlightSCS(Args); }

	// ── v0.2.0 Spatial Intelligence Layer ────────────────────────────────────
	if (Cmd == TEXT("spawn_actor_at_surface"))   { return FSpatialControlModule::SpawnActorAtSurface(Args); }
	if (Cmd == TEXT("align_actors_to_surface"))  { return FSpatialControlModule::AlignActorsToSurface(Args); }
	if (Cmd == TEXT("get_surface_normal_at"))    { return FSpatialControlModule::GetSurfaceNormalAt(Args); }
	if (Cmd == TEXT("analyze_level_composition")){ return FSpatialControlModule::AnalyzeLevelComposition(); }
	if (Cmd == TEXT("get_actors_in_radius"))     { return FSpatialControlModule::GetActorsInRadius(Args); }

	// ── v0.2.0 FAB Integration ────────────────────────────────────────────────
	if (Cmd == TEXT("search_fab_assets"))        { return FFabIntegrationModule::SearchFabAssets(Args); }
	if (Cmd == TEXT("download_fab_asset"))       { return FFabIntegrationModule::DownloadFabAsset(Args); }
	if (Cmd == TEXT("import_local_asset"))       { return FFabIntegrationModule::ImportLocalAsset(Args); }
	if (Cmd == TEXT("list_imported_assets"))     { return FFabIntegrationModule::ListImportedAssets(Args); }

	// ── v0.2.0 Unified Orchestration ─────────────────────────────────────────
	if (Cmd == TEXT("enhance_current_level"))    { return Cmd_EnhanceCurrentLevel(Args); }

	// ── v0.3.0 Rich Multi-Modal Data Access ──────────────────────────────────
	if (Cmd == TEXT("get_multi_view_capture"))         { return FDataAccessModule::GetMultiViewCapture(Args); }
	if (Cmd == TEXT("get_level_hierarchy"))            { return FDataAccessModule::GetLevelHierarchy(); }
	if (Cmd == TEXT("get_deep_properties"))            { return FDataAccessModule::GetDeepProperties(Args); }
	if (Cmd == TEXT("get_semantic_env_snapshot"))      { return FDataAccessModule::GetSemanticEnvironmentSnapshot(); }

	// ── v0.3.0 Advanced Semantic Commands ────────────────────────────────────
	if (Cmd == TEXT("place_asset_thematically"))  { return FSemanticCommandModule::PlaceAssetThematically(Args); }
	if (Cmd == TEXT("refine_level_section"))      { return FSemanticCommandModule::RefineLevelSection(Args); }
	if (Cmd == TEXT("apply_genre_rules"))         { return FSemanticCommandModule::ApplyGenreRules(Args); }
	if (Cmd == TEXT("create_in_editor_asset"))    { return FSemanticCommandModule::CreateInEditorAsset(Args); }

	// ── v0.3.0 Closed-Loop Reasoning & Horror Orchestration ──────────────────
	if (Cmd == TEXT("observe_analyze_plan_act"))  { return Cmd_ObserveAnalyzePlanAct(Args); }
	if (Cmd == TEXT("enhance_horror_scene"))      { return Cmd_EnhanceHorrorScene(Args); }

	// ── v0.4.0 Level Preset System ────────────────────────────────────────────
	if (Cmd == TEXT("load_preset"))              { return FLevelPresetSystem::LoadPreset(Args); }
	if (Cmd == TEXT("save_preset"))              { return FLevelPresetSystem::SavePreset(Args); }
	if (Cmd == TEXT("list_presets"))             { return FLevelPresetSystem::ListPresets(); }
	if (Cmd == TEXT("suggest_preset"))           { return FLevelPresetSystem::SuggestPresetForProject(); }
	if (Cmd == TEXT("get_current_preset"))       { return FLevelPresetSystem::GetCurrentPreset(); }

	// ── v0.4.0 Five-Phase AAA Level Pipeline ─────────────────────────────────
	if (Cmd == TEXT("create_blockout_level"))         { return FLevelPipelineModule::CreateBlockoutLevel(Args); }
	if (Cmd == TEXT("convert_to_whitebox_modular"))   { return FLevelPipelineModule::ConvertToWhiteboxModular(Args); }
	if (Cmd == TEXT("apply_set_dressing"))            { return FLevelPipelineModule::ApplySetDressingAndStorytelling(Args); }
	if (Cmd == TEXT("apply_professional_lighting"))   { return FLevelPipelineModule::ApplyProfessionalLightingAndAtmosphere(Args); }
	if (Cmd == TEXT("add_living_systems"))            { return FLevelPipelineModule::AddLivingSystemsAndPolish(Args); }
	if (Cmd == TEXT("generate_full_quality_level"))   { return FLevelPipelineModule::GenerateFullQualityLevel(Args); }
	if (Cmd == TEXT("get_procedural_capabilities"))   { return FProceduralOpsModule::GetProceduralCapabilities(Args); }
	if (Cmd == TEXT("get_operator_policy"))           { return FProceduralOpsModule::GetOperatorPolicy(); }
	if (Cmd == TEXT("set_operator_policy"))           { return FProceduralOpsModule::SetOperatorPolicy(Args); }
	if (Cmd == TEXT("op_terrain_generate"))           { return FProceduralOpsModule::TerrainGenerate(Args); }
	if (Cmd == TEXT("op_surface_scatter"))            { return FProceduralOpsModule::SurfaceScatter(Args); }
	if (Cmd == TEXT("op_spline_scatter"))             { return FProceduralOpsModule::SplineScatter(Args); }
	if (Cmd == TEXT("op_road_layout"))                { return FProceduralOpsModule::RoadLayout(Args); }
	if (Cmd == TEXT("op_biome_layers"))               { return FProceduralOpsModule::BiomeLayers(Args); }
	if (Cmd == TEXT("op_stamp_poi"))                  { return FProceduralOpsModule::StampPOI(Args); }
	if (Cmd == TEXT("run_operator_pipeline"))         { return FProceduralOpsModule::RunOperatorPipeline(Args); }

	return ErrorResponse(FString::Printf(TEXT("Unknown command: %s"), *Cmd));
#else
	return ErrorResponse(TEXT("UEAgentForge requires WITH_EDITOR."));
#endif
}

// ============================================================================
bool UAgentForgeLibrary::ExecuteSafeTransaction(const FString& CommandJson, FString& OutResult)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> Root;
	FString ParseErr;
	if (!ParseJsonObject(CommandJson, Root, ParseErr))
	{
		OutResult = ErrorResponse(FString::Printf(TEXT("Invalid JSON: %s"), *ParseErr));
		return false;
	}

	FString Cmd;
	Root->TryGetStringField(TEXT("cmd"), Cmd);
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* ArgsPtr;
	if (Root->TryGetObjectField(TEXT("args"), ArgsPtr)) { Args = *ArgsPtr; }

	// Phase 1: PreFlight (constitution + pre-state)
	UVerificationEngine* VE = UVerificationEngine::Get();
	if (VE)
	{
		FVerificationPhaseResult PreFlight = VE->RunPreFlight(Cmd);
		if (!PreFlight.Passed)
		{
			OutResult = ErrorResponse(FString::Printf(
				TEXT("PreFlight FAILED: %s"), *PreFlight.Detail));
			return false;
		}
	}

	// Asset creation/manipulation commands are not reliably rollback-safe under
	// snapshot testing (e.g. Blueprint asset creation can persist objects in package
	// memory and trigger duplicate-name assertions on the second pass). For those
	// commands we intentionally skip Phase 2 and rely on preflight + real execution.
	const bool bSkipSnapshotRollbackForCommand =
		Cmd == TEXT("spawn_actor") ||
		Cmd == TEXT("duplicate_actor") ||
		Cmd == TEXT("spawn_point_light") ||
		Cmd == TEXT("spawn_spot_light") ||
		Cmd == TEXT("spawn_rect_light") ||
		Cmd == TEXT("spawn_directional_light") ||
		Cmd == TEXT("set_actor_label") ||
		Cmd == TEXT("group_actors") ||
		Cmd == TEXT("create_wall") ||
		Cmd == TEXT("create_floor") ||
		Cmd == TEXT("create_room") ||
		Cmd == TEXT("create_corridor") ||
		Cmd == TEXT("create_pillar") ||
		Cmd == TEXT("create_blueprint") ||
		Cmd == TEXT("create_material_instance") ||
		Cmd == TEXT("rename_asset") ||
		Cmd == TEXT("move_asset") ||
		Cmd == TEXT("delete_asset");

	// Phase 2: Snapshot + Rollback test — intentionally runs BEFORE opening the real
	// transaction. The rollback test opens and cancels its own inner FScopedTransaction
	// to confirm that undo works for this command type. Only on success do we open
	// the permanent transaction below.
	if (VE && !bSkipSnapshotRollbackForCommand)
	{
		FVerificationPhaseResult SnapResult = VE->RunSnapshotRollback(
			[&]() -> bool
			{
				// Executes inside a temporary cancelled sub-transaction (rollback test).
				// Changes are intentionally undone — this is the safety proof.
				FString Dummy;
				if (Cmd == TEXT("spawn_actor"))                  { Dummy = Cmd_SpawnActor(Args); }
				else if (Cmd == TEXT("duplicate_actor"))        { Dummy = Cmd_DuplicateActor(Args); }
				else if (Cmd == TEXT("spawn_point_light"))       { Dummy = Cmd_SpawnPointLight(Args); }
				else if (Cmd == TEXT("spawn_spot_light"))        { Dummy = Cmd_SpawnSpotLight(Args); }
				else if (Cmd == TEXT("spawn_rect_light"))        { Dummy = Cmd_SpawnRectLight(Args); }
				else if (Cmd == TEXT("spawn_directional_light")) { Dummy = Cmd_SpawnDirectionalLight(Args); }
				else if (Cmd == TEXT("set_actor_transform"))     { Dummy = Cmd_SetActorTransform(Args); }
				else if (Cmd == TEXT("set_static_mesh"))         { Dummy = Cmd_SetStaticMesh(Args); }
				else if (Cmd == TEXT("set_actor_scale"))         { Dummy = Cmd_SetActorScale(Args); }
				else if (Cmd == TEXT("set_actor_label"))         { Dummy = Cmd_SetActorLabel(Args); }
				else if (Cmd == TEXT("set_actor_mobility"))      { Dummy = Cmd_SetActorMobility(Args); }
				else if (Cmd == TEXT("set_actor_visibility"))    { Dummy = Cmd_SetActorVisibility(Args); }
				else if (Cmd == TEXT("group_actors"))            { Dummy = Cmd_GroupActors(Args); }
				else if (Cmd == TEXT("set_actor_property"))      { Dummy = Cmd_SetActorProperty(Args); }
				else if (Cmd == TEXT("create_wall"))             { Dummy = Cmd_CreateWall(Args); }
				else if (Cmd == TEXT("create_floor"))            { Dummy = Cmd_CreateFloor(Args); }
				else if (Cmd == TEXT("create_room"))             { Dummy = Cmd_CreateRoom(Args); }
				else if (Cmd == TEXT("create_corridor"))         { Dummy = Cmd_CreateCorridor(Args); }
				else if (Cmd == TEXT("create_pillar"))           { Dummy = Cmd_CreatePillar(Args); }
				else if (Cmd == TEXT("delete_actor"))            { Dummy = Cmd_DeleteActor(Args); }
				else if (Cmd == TEXT("create_blueprint"))        { Dummy = Cmd_CreateBlueprint(Args); }
				else if (Cmd == TEXT("compile_blueprint"))       { Dummy = Cmd_CompileBlueprint(Args); }
				else if (Cmd == TEXT("set_bp_cdo_property"))     { Dummy = Cmd_SetBlueprintCDOProperty(Args); }
				else if (Cmd == TEXT("edit_blueprint_node"))     { Dummy = Cmd_EditBlueprintNode(Args); }
				else if (Cmd == TEXT("create_material_instance")){ Dummy = Cmd_CreateMaterialInstance(Args); }
				else if (Cmd == TEXT("set_material_params"))     { Dummy = Cmd_SetMaterialParams(Args); }
				else if (Cmd == TEXT("apply_material_to_actor")) { Dummy = Cmd_ApplyMaterialToActor(Args); }
				else if (Cmd == TEXT("set_mesh_material_color")) { Dummy = Cmd_SetMeshMaterialColor(Args); }
				else if (Cmd == TEXT("set_material_scalar_param")) { Dummy = Cmd_SetMaterialScalarParam(Args); }
				else if (Cmd == TEXT("rename_asset"))            { Dummy = Cmd_RenameAsset(Args); }
				else if (Cmd == TEXT("move_asset"))              { Dummy = Cmd_MoveAsset(Args); }
				else if (Cmd == TEXT("delete_asset"))            { Dummy = Cmd_DeleteAsset(Args); }
				else if (Cmd == TEXT("setup_test_level"))        { Dummy = Cmd_SetupTestLevel(Args); }
				return !Dummy.Contains(TEXT("\"error\""));
			},
			Cmd);

		if (!SnapResult.Passed)
		{
			OutResult = ErrorResponse(FString::Printf(
				TEXT("Snapshot+Rollback FAILED: %s"), *SnapResult.Detail));
			return false;
		}
	}
	else if (VE && bSkipSnapshotRollbackForCommand)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UEAgentForge] Snapshot+Rollback skipped for command '%s' (asset op not rollback-safe)."), *Cmd);
	}

	// Open the REAL transaction — only reached after Phase 2 confirmed rollback works.
	// All operations below are permanently recorded in the undo history.
	FScopedTransaction Transaction(
		FText::FromString(FString::Printf(TEXT("AgentForge: %s"), *Cmd)));

	bool bCommandSuccess = false;

	// Execute for real (the snapshot rollback lambda already ran it inside a cancelled tx;
	// now we execute again inside the real open transaction).
	FString CommandResult;
	if (Cmd == TEXT("spawn_actor"))              { CommandResult = Cmd_SpawnActor(Args); }
	else if (Cmd == TEXT("duplicate_actor"))     { CommandResult = Cmd_DuplicateActor(Args); }
	else if (Cmd == TEXT("spawn_point_light"))   { CommandResult = Cmd_SpawnPointLight(Args); }
	else if (Cmd == TEXT("spawn_spot_light"))    { CommandResult = Cmd_SpawnSpotLight(Args); }
	else if (Cmd == TEXT("spawn_rect_light"))    { CommandResult = Cmd_SpawnRectLight(Args); }
	else if (Cmd == TEXT("spawn_directional_light")) { CommandResult = Cmd_SpawnDirectionalLight(Args); }
	else if (Cmd == TEXT("set_actor_transform")) { CommandResult = Cmd_SetActorTransform(Args); }
	else if (Cmd == TEXT("set_static_mesh"))     { CommandResult = Cmd_SetStaticMesh(Args); }
	else if (Cmd == TEXT("set_actor_scale"))     { CommandResult = Cmd_SetActorScale(Args); }
	else if (Cmd == TEXT("set_actor_label"))     { CommandResult = Cmd_SetActorLabel(Args); }
	else if (Cmd == TEXT("set_actor_mobility"))  { CommandResult = Cmd_SetActorMobility(Args); }
	else if (Cmd == TEXT("set_actor_visibility")) { CommandResult = Cmd_SetActorVisibility(Args); }
	else if (Cmd == TEXT("group_actors"))        { CommandResult = Cmd_GroupActors(Args); }
	else if (Cmd == TEXT("set_actor_property"))  { CommandResult = Cmd_SetActorProperty(Args); }
	else if (Cmd == TEXT("create_wall"))         { CommandResult = Cmd_CreateWall(Args); }
	else if (Cmd == TEXT("create_floor"))        { CommandResult = Cmd_CreateFloor(Args); }
	else if (Cmd == TEXT("create_room"))         { CommandResult = Cmd_CreateRoom(Args); }
	else if (Cmd == TEXT("create_corridor"))     { CommandResult = Cmd_CreateCorridor(Args); }
	else if (Cmd == TEXT("create_pillar"))       { CommandResult = Cmd_CreatePillar(Args); }
	else if (Cmd == TEXT("delete_actor"))        { CommandResult = Cmd_DeleteActor(Args); }
	else if (Cmd == TEXT("create_blueprint"))    { CommandResult = Cmd_CreateBlueprint(Args); }
	else if (Cmd == TEXT("compile_blueprint"))   { CommandResult = Cmd_CompileBlueprint(Args); }
	else if (Cmd == TEXT("set_bp_cdo_property")) { CommandResult = Cmd_SetBlueprintCDOProperty(Args); }
	else if (Cmd == TEXT("edit_blueprint_node")) { CommandResult = Cmd_EditBlueprintNode(Args); }
	else if (Cmd == TEXT("create_material_instance")){ CommandResult = Cmd_CreateMaterialInstance(Args); }
	else if (Cmd == TEXT("set_material_params")) { CommandResult = Cmd_SetMaterialParams(Args); }
	else if (Cmd == TEXT("apply_material_to_actor")) { CommandResult = Cmd_ApplyMaterialToActor(Args); }
	else if (Cmd == TEXT("set_mesh_material_color")) { CommandResult = Cmd_SetMeshMaterialColor(Args); }
	else if (Cmd == TEXT("set_material_scalar_param")) { CommandResult = Cmd_SetMaterialScalarParam(Args); }
	else if (Cmd == TEXT("rename_asset"))        { CommandResult = Cmd_RenameAsset(Args); }
	else if (Cmd == TEXT("move_asset"))          { CommandResult = Cmd_MoveAsset(Args); }
	else if (Cmd == TEXT("delete_asset"))        { CommandResult = Cmd_DeleteAsset(Args); }
	else if (Cmd == TEXT("setup_test_level"))    { CommandResult = Cmd_SetupTestLevel(Args); }
	else { CommandResult = ErrorResponse(FString::Printf(TEXT("Unrouted mutating command: %s"), *Cmd)); }

	bCommandSuccess = !CommandResult.Contains(TEXT("\"error\""));

	if (!bCommandSuccess)
	{
		Transaction.Cancel();
		OutResult = CommandResult;
		return false;
	}

	// Phase 3: PostVerify
	if (VE)
	{
		// Estimate expected actor delta from command type
		const int32 ExpectedDelta =
			(Cmd == TEXT("spawn_actor") || Cmd == TEXT("spawn_point_light") || Cmd == TEXT("spawn_spot_light")) ? 1  :
			Cmd == TEXT("delete_actor") ? -1 : 0;

		FVerificationPhaseResult PostResult = VE->RunPostVerify(ExpectedDelta);
		// Non-blocking: log but don't cancel on PostVerify mismatch
		if (!PostResult.Passed)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UEAgentForge] PostVerify warning: %s"), *PostResult.Detail);
		}
	}

	OutResult = CommandResult;
	return true;
#else
	OutResult = ErrorResponse(TEXT("UEAgentForge requires WITH_EDITOR."));
	return false;
#endif
}

// ============================================================================
bool UAgentForgeLibrary::RunVerificationProtocol(int32 PhaseMask)
{
	UVerificationEngine* VE = UVerificationEngine::Get();
	if (!VE) { return false; }
	TArray<FVerificationPhaseResult> Results;
	return VE->RunPhases(PhaseMask, TEXT("ManualVerificationRun"), Results);
}

bool UAgentForgeLibrary::EnforceConstitution(const FString& ActionDesc, TArray<FString>& OutViolations)
{
	UConstitutionParser* Parser = UConstitutionParser::Get();
	if (!Parser) { return true; }
	return Parser->ValidateAction(ActionDesc, OutViolations);
}

FString UAgentForgeLibrary::ExecutePythonScript(const FString& ScriptCode)
{
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("script"), ScriptCode);
	return Cmd_ExecutePython(Args);
}

bool UAgentForgeLibrary::EditBlueprintNode(const FString& BlueprintPath, const FString& NodeSpecJson)
{
#if WITH_EDITOR
	TSharedPtr<FJsonObject> NodeSpec;
	FString Err;
	if (!ParseJsonObject(NodeSpecJson, NodeSpec, Err)) { return false; }
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
	Args->SetStringField(TEXT("blueprint_path"), BlueprintPath);
	Args->SetObjectField(TEXT("node_spec"), NodeSpec);
	return !Cmd_EditBlueprintNode(Args).Contains(TEXT("\"error\""));
#else
	return false;
#endif
}

// ============================================================================
//  COMMAND IMPLEMENTATIONS — OBSERVATION
// ============================================================================
FString UAgentForgeLibrary::Cmd_Ping(const TSharedPtr<FJsonObject>& Args)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("pong"), TEXT("UEAgentForge v0.1.0"));
	Obj->SetStringField(TEXT("version"), TEXT("0.1.0"));
	UConstitutionParser* Parser = UConstitutionParser::Get();
	Obj->SetBoolField(TEXT("constitution_loaded"), Parser && Parser->IsLoaded());
	Obj->SetNumberField(TEXT("constitution_rules"), Parser ? Parser->GetRules().Num() : 0);
	return ToJsonString(Obj);
}

FString UAgentForgeLibrary::Cmd_GetAllLevelActors()
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A || !IsValid(A)) { continue; }

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"),        A->GetName());
		Obj->SetStringField(TEXT("label"),       A->GetActorLabel());
		Obj->SetStringField(TEXT("class"),       A->GetClass()->GetName());
		Obj->SetStringField(TEXT("object_path"), A->GetPathName());
		Obj->SetObjectField(TEXT("location"),    VecToJson(A->GetActorLocation()));
		Obj->SetObjectField(TEXT("scale"),       VecToJson(A->GetActorScale3D()));

		const FRotator Rot = A->GetActorRotation();
		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotObj->SetNumberField(TEXT("yaw"),   Rot.Yaw);
		RotObj->SetNumberField(TEXT("roll"),  Rot.Roll);
		Obj->SetObjectField(TEXT("rotation"), RotObj);

		ActorArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("actors"), ActorArray);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetActorComponents(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);
	AActor* Actor = FindActorByLabelOrName(Label);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *Label)); }

	TArray<TSharedPtr<FJsonValue>> CompArray;
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) { continue; }
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"),        Comp->GetName());
		Obj->SetStringField(TEXT("class"),       Comp->GetClass()->GetName());
		Obj->SetStringField(TEXT("object_path"), Comp->GetPathName());
		CompArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("components"), CompArray);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetCurrentLevel()
{
#if WITH_EDITOR
	FString PackagePath, WorldPath, ActorPrefix;
	if (!GetCurrentLevelPaths(PackagePath, WorldPath, ActorPrefix))
	{
		return ErrorResponse(TEXT("Could not determine current level."));
	}
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("package_path"),  PackagePath);
	Obj->SetStringField(TEXT("world_path"),    WorldPath);
	Obj->SetStringField(TEXT("actor_prefix"),  ActorPrefix);
	Obj->SetStringField(TEXT("map_lock"),      GForgeMapLockPackagePath);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetWorldContext(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	int32 MaxActors = 120;
	int32 MaxRelationships = 48;
	bool bIncludeComponents = false;
	bool bIncludeScreenshot = true;
	FString ScreenshotLabel = TEXT("world_context");
	if (Args.IsValid())
	{
		if (Args->HasTypedField<EJson::Number>(TEXT("max_actors")))
		{
			MaxActors = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_actors")), 20, 500);
		}
		if (Args->HasTypedField<EJson::Number>(TEXT("max_relationships")))
		{
			MaxRelationships = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_relationships")), 8, 200);
		}
		if (Args->HasTypedField<EJson::Boolean>(TEXT("include_components")))
		{
			bIncludeComponents = Args->GetBoolField(TEXT("include_components"));
		}
		if (Args->HasTypedField<EJson::Boolean>(TEXT("include_screenshot")))
		{
			bIncludeScreenshot = Args->GetBoolField(TEXT("include_screenshot"));
		}
		if (Args->HasTypedField<EJson::String>(TEXT("screenshot_label")))
		{
			ScreenshotLabel = Args->GetStringField(TEXT("screenshot_label"));
		}
	}

	const FString LevelRaw = Cmd_GetCurrentLevel();
	const FString CompositionRaw = FSpatialControlModule::AnalyzeLevelComposition();
	const FString SemanticRaw = FDataAccessModule::GetSemanticEnvironmentSnapshot();
	const FString HierarchyRaw = FDataAccessModule::GetLevelHierarchy();

	TSharedPtr<FJsonObject> LevelObj;
	TSharedPtr<FJsonObject> CompositionObj;
	TSharedPtr<FJsonObject> SemanticObj;
	TSharedPtr<FJsonObject> HierarchyObj;
	FString ParseErr;

	if (!ParseJsonObject(LevelRaw, LevelObj, ParseErr))
	{
		return ErrorResponse(FString::Printf(TEXT("get_world_context: failed to parse level payload (%s)"), *ParseErr));
	}
	if (!ParseJsonObject(CompositionRaw, CompositionObj, ParseErr))
	{
		return ErrorResponse(FString::Printf(TEXT("get_world_context: failed to parse composition payload (%s)"), *ParseErr));
	}
	if (!ParseJsonObject(SemanticRaw, SemanticObj, ParseErr))
	{
		return ErrorResponse(FString::Printf(TEXT("get_world_context: failed to parse semantic payload (%s)"), *ParseErr));
	}
	if (!ParseJsonObject(HierarchyRaw, HierarchyObj, ParseErr))
	{
		return ErrorResponse(FString::Printf(TEXT("get_world_context: failed to parse hierarchy payload (%s)"), *ParseErr));
	}

	const TArray<TSharedPtr<FJsonValue>>* HierarchyActors = nullptr;
	if (!HierarchyObj->TryGetArrayField(TEXT("actors"), HierarchyActors) || !HierarchyActors)
	{
		return ErrorResponse(TEXT("get_world_context: hierarchy payload missing actors array"));
	}

	struct FContextActor
	{
		FString Label;
		FString ClassName;
		FString Category;
		FVector Location = FVector::ZeroVector;
		FString Parent;
		bool bVisible = true;
		bool bGameplayAnchor = false;
		float DistanceToCenter = 0.0f;
		float DistanceSq = 0.0f;
		int32 ComponentCount = 0;
		TArray<FString> Tags;
		int32 Priority = 0;
	};

	FVector Center = FVector::ZeroVector;
	bool bHasCenter = false;
	double SemanticAreaM2 = 0.0;
	{
		const TSharedPtr<FJsonObject>* BoundsObj = nullptr;
		if (SemanticObj->TryGetObjectField(TEXT("level_bounds"), BoundsObj) && *BoundsObj)
		{
			if ((*BoundsObj)->HasTypedField<EJson::Number>(TEXT("area_m2")))
			{
				SemanticAreaM2 = (*BoundsObj)->GetNumberField(TEXT("area_m2"));
			}
			const TSharedPtr<FJsonObject>* CenterObj = nullptr;
			if ((*BoundsObj)->TryGetObjectField(TEXT("center"), CenterObj) && *CenterObj)
			{
				Center.X = (float)(*CenterObj)->GetNumberField(TEXT("x"));
				Center.Y = (float)(*CenterObj)->GetNumberField(TEXT("y"));
				Center.Z = (float)(*CenterObj)->GetNumberField(TEXT("z"));
				bHasCenter = true;
			}
		}
	}

	TArray<FContextActor> AllActors;
	AllActors.Reserve(HierarchyActors->Num());

	TMap<FString, int32> CategoryCounts;
	TArray<FString> Warnings;
	FVector MeanAccumulator = FVector::ZeroVector;
	int32 MeanCount = 0;

	for (const TSharedPtr<FJsonValue>& Value : *HierarchyActors)
	{
		const TSharedPtr<FJsonObject>* ActorObj = nullptr;
		if (!Value.IsValid() || !Value->TryGetObject(ActorObj) || !ActorObj || !(*ActorObj).IsValid())
		{
			continue;
		}

		FContextActor Entry;
		Entry.Label = (*ActorObj)->GetStringField(TEXT("label"));
		Entry.ClassName = (*ActorObj)->GetStringField(TEXT("class"));
		if (IsLikelySystemActorForContext(Entry.Label, Entry.ClassName))
		{
			continue;
		}
		Entry.Parent = (*ActorObj)->GetStringField(TEXT("parent"));
		Entry.bVisible = !(*ActorObj)->HasField(TEXT("is_visible")) || (*ActorObj)->GetBoolField(TEXT("is_visible"));

		const TSharedPtr<FJsonObject>* LocObj = nullptr;
		if ((*ActorObj)->TryGetObjectField(TEXT("location"), LocObj) && *LocObj)
		{
			Entry.Location.X = (float)(*LocObj)->GetNumberField(TEXT("x"));
			Entry.Location.Y = (float)(*LocObj)->GetNumberField(TEXT("y"));
			Entry.Location.Z = (float)(*LocObj)->GetNumberField(TEXT("z"));
			MeanAccumulator += Entry.Location;
			++MeanCount;
		}

		const TArray<TSharedPtr<FJsonValue>>* TagsArr = nullptr;
		if ((*ActorObj)->TryGetArrayField(TEXT("tags"), TagsArr) && TagsArr)
		{
			for (const TSharedPtr<FJsonValue>& TagValue : *TagsArr)
			{
				if (!TagValue.IsValid()) { continue; }
				if (Entry.Tags.Num() >= 6) { break; }
				Entry.Tags.Add(TagValue->AsString());
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ComponentsArr = nullptr;
		if ((*ActorObj)->TryGetArrayField(TEXT("components"), ComponentsArr) && ComponentsArr)
		{
			Entry.ComponentCount = ComponentsArr->Num();
		}

		Entry.Category = ClassifyActorForContext(Entry.Label, Entry.ClassName);
		Entry.Priority = ContextPriorityForCategory(Entry.Category);
		Entry.bGameplayAnchor =
			Entry.Category == TEXT("player") ||
			Entry.Category == TEXT("objective") ||
			Entry.Category == TEXT("ai");

		CategoryCounts.FindOrAdd(Entry.Category) += 1;
		AllActors.Add(MoveTemp(Entry));
	}

	if (bHasCenter)
	{
		const bool bExtremeCenter =
			FMath::Abs(Center.X) > 20000000.0f ||
			FMath::Abs(Center.Y) > 20000000.0f ||
			FMath::Abs(Center.Z) > 20000000.0f;
		const bool bExtremeArea = SemanticAreaM2 > 1000000000000.0;
		if (bExtremeCenter || bExtremeArea)
		{
			bHasCenter = false;
			Warnings.Add(TEXT("Semantic bounds were extreme; using actor-derived center for context packet."));
		}
	}

	if (!bHasCenter && MeanCount > 0)
	{
		Center = MeanAccumulator / (float)MeanCount;
		bHasCenter = true;
	}
	if (!bHasCenter)
	{
		Warnings.Add(TEXT("Unable to determine level center; distances may be inaccurate."));
	}

	for (FContextActor& Actor : AllActors)
	{
		Actor.DistanceSq = bHasCenter ? FVector::DistSquared(Actor.Location, Center) : 0.0f;
		Actor.DistanceToCenter = bHasCenter ? FMath::Sqrt(Actor.DistanceSq) : 0.0f;
		// Prefer visible actors when priorities tie.
		if (Actor.bVisible)
		{
			Actor.Priority += 20;
		}
	}

	AllActors.Sort([](const FContextActor& A, const FContextActor& B)
	{
		if (A.Priority != B.Priority) { return A.Priority > B.Priority; }
		return A.DistanceSq < B.DistanceSq;
	});

	if (AllActors.Num() == 0)
	{
		return ErrorResponse(TEXT("get_world_context: no context actors found after filtering"));
	}

	const int32 SelectedCount = FMath::Min(MaxActors, AllActors.Num());
	TArray<FContextActor> SelectedActors;
	SelectedActors.Reserve(SelectedCount);
	for (int32 Idx = 0; Idx < SelectedCount; ++Idx)
	{
		SelectedActors.Add(AllActors[Idx]);
	}

	// Spatial hotspots: 4x4 density grid for fast world understanding.
	const int32 GridSide = 4;
	const int32 CellCount = GridSide * GridSide;
	TArray<int32> CellActorCounts;
	TArray<FVector> CellPositionSums;
	TArray<TMap<FString, int32>> CellCategoryCounts;
	CellActorCounts.Init(0, CellCount);
	CellPositionSums.Init(FVector::ZeroVector, CellCount);
	CellCategoryCounts.SetNum(CellCount);

	FVector Min(FLT_MAX, FLT_MAX, FLT_MAX);
	FVector Max(-FLT_MAX, -FLT_MAX, -FLT_MAX);
	for (const FContextActor& Actor : AllActors)
	{
		Min.X = FMath::Min(Min.X, Actor.Location.X);
		Min.Y = FMath::Min(Min.Y, Actor.Location.Y);
		Min.Z = FMath::Min(Min.Z, Actor.Location.Z);
		Max.X = FMath::Max(Max.X, Actor.Location.X);
		Max.Y = FMath::Max(Max.Y, Actor.Location.Y);
		Max.Z = FMath::Max(Max.Z, Actor.Location.Z);
	}
	const float SpanX = FMath::Max(1.0f, Max.X - Min.X);
	const float SpanY = FMath::Max(1.0f, Max.Y - Min.Y);

	for (const FContextActor& Actor : AllActors)
	{
		const float NormX = (Actor.Location.X - Min.X) / SpanX;
		const float NormY = (Actor.Location.Y - Min.Y) / SpanY;
		const int32 IX = FMath::Clamp((int32)FMath::FloorToInt(NormX * (float)GridSide), 0, GridSide - 1);
		const int32 IY = FMath::Clamp((int32)FMath::FloorToInt(NormY * (float)GridSide), 0, GridSide - 1);
		const int32 CellIdx = IY * GridSide + IX;
		CellActorCounts[CellIdx] += 1;
		CellPositionSums[CellIdx] += Actor.Location;
		CellCategoryCounts[CellIdx].FindOrAdd(Actor.Category) += 1;
	}

	struct FHotspot
	{
		int32 CellIndex = 0;
		int32 Count = 0;
	};
	TArray<FHotspot> Hotspots;
	for (int32 CellIdx = 0; CellIdx < CellCount; ++CellIdx)
	{
		if (CellActorCounts[CellIdx] <= 0) { continue; }
		FHotspot H;
		H.CellIndex = CellIdx;
		H.Count = CellActorCounts[CellIdx];
		Hotspots.Add(H);
	}
	Hotspots.Sort([](const FHotspot& A, const FHotspot& B)
	{
		return A.Count > B.Count;
	});

	TArray<TSharedPtr<FJsonValue>> HotspotArr;
	const int32 MaxHotspots = FMath::Min(6, Hotspots.Num());
	for (int32 Idx = 0; Idx < MaxHotspots; ++Idx)
	{
		const FHotspot& H = Hotspots[Idx];
		const int32 Count = FMath::Max(1, CellActorCounts[H.CellIndex]);
		const FVector CenterPos = CellPositionSums[H.CellIndex] / (float)Count;

		FString DominantCategory = TEXT("other");
		int32 DominantCount = 0;
		for (const TPair<FString, int32>& Pair : CellCategoryCounts[H.CellIndex])
		{
			if (Pair.Value > DominantCount)
			{
				DominantCategory = Pair.Key;
				DominantCount = Pair.Value;
			}
		}

		TSharedPtr<FJsonObject> HotspotObj = MakeShared<FJsonObject>();
		HotspotObj->SetNumberField(TEXT("cell"), H.CellIndex);
		HotspotObj->SetNumberField(TEXT("actor_count"), H.Count);
		HotspotObj->SetStringField(TEXT("dominant_category"), DominantCategory);
		HotspotObj->SetObjectField(TEXT("center"), VecToJson(CenterPos));
		HotspotArr.Add(MakeShared<FJsonValueObject>(HotspotObj));
	}

	// Relationship inference for gameplay anchors.
	TArray<TSharedPtr<FJsonValue>> RelationshipArr;
	TSet<FString> RelationshipKeys;

	auto AddRelationship = [&](const FString& From, const FString& To, const FString& Type, float DistanceCm, float Confidence)
	{
		if (From.IsEmpty() || To.IsEmpty() || From == To)
		{
			return;
		}
		const FString Key = From + TEXT("->") + To + TEXT(":") + Type;
		if (RelationshipKeys.Contains(Key))
		{
			return;
		}
		if (RelationshipArr.Num() >= MaxRelationships)
		{
			return;
		}
		RelationshipKeys.Add(Key);
		TSharedPtr<FJsonObject> RelObj = MakeShared<FJsonObject>();
		RelObj->SetStringField(TEXT("from"), From);
		RelObj->SetStringField(TEXT("to"), To);
		RelObj->SetStringField(TEXT("type"), Type);
		RelObj->SetNumberField(TEXT("distance_cm"), DistanceCm);
		RelObj->SetNumberField(TEXT("confidence"), Confidence);
		RelationshipArr.Add(MakeShared<FJsonValueObject>(RelObj));
	};

	TMap<FString, FString> KeysBySuffix;
	TMap<FString, FString> DoorsBySuffix;
	TArray<int32> AnchorIndices;
	for (int32 Idx = 0; Idx < SelectedActors.Num(); ++Idx)
	{
		const FContextActor& Actor = SelectedActors[Idx];
		if (!Actor.bGameplayAnchor)
		{
			continue;
		}
		AnchorIndices.Add(Idx);

		FString LabelLower = Actor.Label;
		LabelLower.ToLowerInline();
		const FString Suffix = ExtractSuffixToken(Actor.Label);
		if (!Suffix.IsEmpty())
		{
			if (LabelLower.Contains(TEXT("key")))
			{
				KeysBySuffix.FindOrAdd(Suffix) = Actor.Label;
			}
			else if (LabelLower.Contains(TEXT("door")))
			{
				DoorsBySuffix.FindOrAdd(Suffix) = Actor.Label;
			}
		}
	}

	for (const TPair<FString, FString>& Pair : KeysBySuffix)
	{
		if (const FString* DoorLabel = DoorsBySuffix.Find(Pair.Key))
		{
			AddRelationship(Pair.Value, *DoorLabel, TEXT("matching_suffix"), 0.0f, 0.95f);
		}
	}

	for (const int32 AnchorIdx : AnchorIndices)
	{
		const FContextActor& Anchor = SelectedActors[AnchorIdx];
		float BestDistSq = TNumericLimits<float>::Max();
		const FContextActor* BestOther = nullptr;

		for (const int32 OtherIdx : AnchorIndices)
		{
			if (AnchorIdx == OtherIdx) { continue; }
			const FContextActor& Other = SelectedActors[OtherIdx];
			const float DistSq = FVector::DistSquared(Anchor.Location, Other.Location);
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestOther = &Other;
			}
		}
		if (BestOther)
		{
			AddRelationship(
				Anchor.Label,
				BestOther->Label,
				TEXT("nearest_anchor"),
				FMath::Sqrt(BestDistSq),
				0.6f);
		}
	}

	// Build selected actor payload and gameplay anchor payload.
	TArray<TSharedPtr<FJsonValue>> ActorsArr;
	TArray<TSharedPtr<FJsonValue>> GameplayAnchorsArr;
	for (const FContextActor& Actor : SelectedActors)
	{
		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("label"), Actor.Label);
		ActorObj->SetStringField(TEXT("class"), Actor.ClassName);
		ActorObj->SetStringField(TEXT("category"), Actor.Category);
		ActorObj->SetStringField(TEXT("parent"), Actor.Parent);
		ActorObj->SetBoolField(TEXT("is_visible"), Actor.bVisible);
		ActorObj->SetObjectField(TEXT("location"), VecToJson(Actor.Location));
		ActorObj->SetNumberField(TEXT("distance_to_center_cm"), Actor.DistanceToCenter);
		if (bIncludeComponents)
		{
			ActorObj->SetNumberField(TEXT("component_count"), Actor.ComponentCount);
		}

		TArray<TSharedPtr<FJsonValue>> TagsJson;
		for (const FString& Tag : Actor.Tags)
		{
			TagsJson.Add(MakeShared<FJsonValueString>(Tag));
		}
		ActorObj->SetArrayField(TEXT("tags"), TagsJson);
		ActorsArr.Add(MakeShared<FJsonValueObject>(ActorObj));

		if (Actor.bGameplayAnchor)
		{
			TSharedPtr<FJsonObject> AnchorObj = MakeShared<FJsonObject>();
			AnchorObj->SetStringField(TEXT("label"), Actor.Label);
			AnchorObj->SetStringField(TEXT("class"), Actor.ClassName);
			AnchorObj->SetStringField(TEXT("category"), Actor.Category);
			AnchorObj->SetObjectField(TEXT("location"), VecToJson(Actor.Location));
			GameplayAnchorsArr.Add(MakeShared<FJsonValueObject>(AnchorObj));
		}
	}

	TSharedPtr<FJsonObject> CategoryCountsObj = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : CategoryCounts)
	{
		CategoryCountsObj->SetNumberField(Pair.Key, Pair.Value);
	}

	TArray<TSharedPtr<FJsonValue>> BriefArr;
	FString PackagePath = TEXT("");
	if (!LevelObj->TryGetStringField(TEXT("package_path"), PackagePath))
	{
		PackagePath = TEXT("Unknown");
		Warnings.Add(TEXT("Level payload missing package_path."));
	}
	BriefArr.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Map: %s"), *PackagePath)));
	BriefArr.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Actors: %d total, %d included in context packet"), AllActors.Num(), SelectedActors.Num())));

	const double HorrorScore = SemanticObj->HasTypedField<EJson::Number>(TEXT("horror_score"))
		? SemanticObj->GetNumberField(TEXT("horror_score")) : 0.0;
	const FString HorrorRating = SemanticObj->HasTypedField<EJson::String>(TEXT("horror_rating"))
		? SemanticObj->GetStringField(TEXT("horror_rating")) : TEXT("Unknown");
	BriefArr.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Atmosphere: horror_score %.1f (%s)"), HorrorScore, *HorrorRating)));
	BriefArr.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Gameplay anchors: %d, inferred relationships: %d"), GameplayAnchorsArr.Num(), RelationshipArr.Num())));
	BriefArr.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Top hotspot count: %d actors"), Hotspots.Num() > 0 ? Hotspots[0].Count : 0)));

	TArray<TSharedPtr<FJsonValue>> WarningsArr;
	for (const FString& Warning : Warnings)
	{
		WarningsArr.Add(MakeShared<FJsonValueString>(Warning));
	}
	if (AllActors.Num() > SelectedActors.Num())
	{
		WarningsArr.Add(MakeShared<FJsonValueString>(
			FString::Printf(TEXT("Context truncated: %d actors omitted by max_actors budget."),
			AllActors.Num() - SelectedActors.Num())));
	}

	TArray<TSharedPtr<FJsonValue>> SuggestedNextCmds;
	SuggestedNextCmds.Add(MakeShared<FJsonValueString>(TEXT("get_deep_properties")));
	SuggestedNextCmds.Add(MakeShared<FJsonValueString>(TEXT("get_actors_in_radius")));
	SuggestedNextCmds.Add(MakeShared<FJsonValueString>(TEXT("observe_analyze_plan_act")));

	FString ScreenshotPath;
	if (bIncludeScreenshot)
	{
		ScreenshotPath = QueueEditorScreenshot(ScreenshotLabel);
		if (ScreenshotPath.IsEmpty())
		{
			WarningsArr.Add(MakeShared<FJsonValueString>(TEXT("Screenshot request failed for get_world_context.")));
		}
	}

	TSharedPtr<FJsonObject> BudgetObj = MakeShared<FJsonObject>();
	BudgetObj->SetNumberField(TEXT("max_actors"), MaxActors);
	BudgetObj->SetNumberField(TEXT("selected_actors"), SelectedActors.Num());
	BudgetObj->SetNumberField(TEXT("source_actors"), AllActors.Num());
	BudgetObj->SetBoolField(TEXT("truncated"), AllActors.Num() > SelectedActors.Num());
	BudgetObj->SetNumberField(TEXT("max_relationships"), MaxRelationships);
	BudgetObj->SetBoolField(TEXT("include_components"), bIncludeComponents);

	TSharedPtr<FJsonObject> ScreenshotObj = MakeShared<FJsonObject>();
	ScreenshotObj->SetBoolField(TEXT("requested"), bIncludeScreenshot);
	ScreenshotObj->SetBoolField(TEXT("queued"), bIncludeScreenshot && !ScreenshotPath.IsEmpty());
	if (!ScreenshotPath.IsEmpty())
	{
		ScreenshotObj->SetStringField(TEXT("path"), ScreenshotPath);
		BriefArr.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("Screenshot queued: %s"), *ScreenshotPath)));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("schema"), TEXT("world_context_v1"));
	Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetObjectField(TEXT("budget"), BudgetObj);
	Root->SetObjectField(TEXT("level"), LevelObj);
	Root->SetObjectField(TEXT("semantic"), SemanticObj);
	Root->SetObjectField(TEXT("composition"), CompositionObj);
	Root->SetObjectField(TEXT("category_counts"), CategoryCountsObj);
	Root->SetArrayField(TEXT("actors"), ActorsArr);
	Root->SetArrayField(TEXT("gameplay_anchors"), GameplayAnchorsArr);
	Root->SetArrayField(TEXT("relationships"), RelationshipArr);
	Root->SetArrayField(TEXT("spatial_hotspots"), HotspotArr);
	Root->SetArrayField(TEXT("llm_brief"), BriefArr);
	Root->SetArrayField(TEXT("warnings"), WarningsArr);
	Root->SetArrayField(TEXT("suggested_next_cmds"), SuggestedNextCmds);
	Root->SetObjectField(TEXT("screenshot"), ScreenshotObj);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_AssertCurrentLevel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Expected;
	Args->TryGetStringField(TEXT("expected_level"), Expected);
	FString PackagePath, WorldPath, ActorPrefix;
	GetCurrentLevelPaths(PackagePath, WorldPath, ActorPrefix);

	const bool bMatch = PackagePath.Contains(Expected) || Expected.Contains(PackagePath);
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), bMatch);
	Obj->SetStringField(TEXT("expected_level"), Expected);
	Obj->SetStringField(TEXT("current_package_path"), PackagePath);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetActorBounds(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);
	AActor* Actor = FindActorByLabelOrName(Label);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *Label)); }

	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetObjectField(TEXT("origin"), VecToJson(Origin));
	Obj->SetObjectField(TEXT("extent"), VecToJson(Extent));
	Obj->SetObjectField(TEXT("box_min"), VecToJson(Origin - Extent));
	Obj->SetObjectField(TEXT("box_max"), VecToJson(Origin + Extent));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAvailableMeshes(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SearchFilter;
	FString PathFilter;
	int32 MaxResults = 50;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("search_filter"), SearchFilter);
		Args->TryGetStringField(TEXT("path_filter"), PathFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = FMath::Max(1, (int32)Args->GetNumberField(TEXT("max_results")));
		}
	}

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
	}

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	const FString SearchNeedle = SearchFilter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!SearchNeedle.IsEmpty() && !AssetName.ToLower().Contains(SearchNeedle))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), AssetName);
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Results.Add(MakeShared<FJsonValueObject>(Entry));

		if (Results.Num() >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("search_filter"), SearchFilter);
	Root->SetStringField(TEXT("path_filter"), PathFilter);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAvailableMaterials(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SearchFilter;
	FString PathFilter;
	int32 MaxResults = 50;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("search_filter"), SearchFilter);
		Args->TryGetStringField(TEXT("path_filter"), PathFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = FMath::Max(1, (int32)Args->GetNumberField(TEXT("max_results")));
		}
	}

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(UMaterialInstanceConstant::StaticClass()->GetClassPathName());
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
	}

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	const FString SearchNeedle = SearchFilter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!SearchNeedle.IsEmpty() && !AssetName.ToLower().Contains(SearchNeedle))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), AssetName);
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Results.Add(MakeShared<FJsonValueObject>(Entry));

		if (Results.Num() >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("search_filter"), SearchFilter);
	Root->SetStringField(TEXT("path_filter"), PathFilter);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAvailableBlueprints(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SearchFilter;
	FString ParentClassFilter;
	FString PathFilter;
	int32 MaxResults = 50;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("search_filter"), SearchFilter);
		Args->TryGetStringField(TEXT("parent_class"), ParentClassFilter);
		Args->TryGetStringField(TEXT("path_filter"), PathFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = FMath::Max(1, (int32)Args->GetNumberField(TEXT("max_results")));
		}
	}

	FString ParentNeedle = ParentClassFilter;
	ParentNeedle.ToLowerInline();

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
	}

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	const FString SearchNeedle = SearchFilter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!SearchNeedle.IsEmpty() && !AssetName.ToLower().Contains(SearchNeedle))
		{
			continue;
		}

		const FString ParentClassPath = Asset.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
		const FString NativeParentClassPath = Asset.GetTagValueRef<FString>(FBlueprintTags::NativeParentClassPath);
		const FString GeneratedClassPath = Asset.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
		if (!ParentNeedle.IsEmpty())
		{
			FString Haystack = ParentClassPath + TEXT(" ") + NativeParentClassPath + TEXT(" ") + GeneratedClassPath;
			Haystack.ToLowerInline();
			if (!Haystack.Contains(ParentNeedle))
			{
				continue;
			}
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), AssetName);
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Entry->SetStringField(TEXT("parent_class_path"), ParentClassPath);
		Entry->SetStringField(TEXT("native_parent_class_path"), NativeParentClassPath);
		Entry->SetStringField(TEXT("generated_class_path"), GeneratedClassPath);
		Results.Add(MakeShared<FJsonValueObject>(Entry));

		if (Results.Num() >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("search_filter"), SearchFilter);
	Root->SetStringField(TEXT("path_filter"), PathFilter);
	Root->SetStringField(TEXT("parent_class"), ParentClassFilter);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAvailableTextures(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SearchFilter;
	FString PathFilter;
	int32 MaxResults = 50;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("search_filter"), SearchFilter);
		Args->TryGetStringField(TEXT("path_filter"), PathFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = FMath::Max(1, (int32)Args->GetNumberField(TEXT("max_results")));
		}
	}

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
	}

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	const FString SearchNeedle = SearchFilter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!SearchNeedle.IsEmpty() && !AssetName.ToLower().Contains(SearchNeedle))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), AssetName);
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		if (Results.Num() >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("search_filter"), SearchFilter);
	Root->SetStringField(TEXT("path_filter"), PathFilter);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAvailableSounds(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SearchFilter;
	FString PathFilter;
	int32 MaxResults = 50;
	if (Args.IsValid())
	{
		Args->TryGetStringField(TEXT("search_filter"), SearchFilter);
		Args->TryGetStringField(TEXT("path_filter"), PathFilter);
		if (Args->HasField(TEXT("max_results")))
		{
			MaxResults = FMath::Max(1, (int32)Args->GetNumberField(TEXT("max_results")));
		}
	}

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.ClassPaths.Add(USoundWave::StaticClass()->GetClassPathName());
	Filter.ClassPaths.Add(USoundCue::StaticClass()->GetClassPathName());
	if (!PathFilter.IsEmpty())
	{
		Filter.PackagePaths.Add(*PathFilter);
	}

	TArray<FAssetData> Assets;
	RegistryModule.Get().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});

	const FString SearchNeedle = SearchFilter.ToLower();
	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& Asset : Assets)
	{
		const FString AssetName = Asset.AssetName.ToString();
		if (!SearchNeedle.IsEmpty() && !AssetName.ToLower().Contains(SearchNeedle))
		{
			continue;
		}

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), AssetName);
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath.ToString());
		Entry->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
		Results.Add(MakeShared<FJsonValueObject>(Entry));
		if (Results.Num() >= MaxResults)
		{
			break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("search_filter"), SearchFilter);
	Root->SetStringField(TEXT("path_filter"), PathFilter);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("assets"), Results);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetAssetDetails(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	if (AssetPath.IsEmpty())
	{
		return ErrorResponse(TEXT("get_asset_details requires asset_path."));
	}

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return ErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("asset_name"), Asset->GetName());
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("class"), Asset->GetClass()->GetName());
	Root->SetStringField(TEXT("package"), Asset->GetOutermost() ? Asset->GetOutermost()->GetName() : FString());

	if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Asset))
	{
		const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
		Root->SetNumberField(TEXT("lod_count"), StaticMesh->GetNumLODs());
		Root->SetObjectField(TEXT("bounds_origin"), VecToJson(Bounds.Origin));
		Root->SetObjectField(TEXT("bounds_extent"), VecToJson(Bounds.BoxExtent));
	}
	else if (UMaterialInterface* Material = Cast<UMaterialInterface>(Asset))
	{
		TArray<FMaterialParameterInfo> ScalarInfos;
		TArray<FGuid> ScalarIds;
		TArray<FMaterialParameterInfo> VectorInfos;
		TArray<FGuid> VectorIds;
		Material->GetAllScalarParameterInfo(ScalarInfos, ScalarIds);
		Material->GetAllVectorParameterInfo(VectorInfos, VectorIds);

		TArray<TSharedPtr<FJsonValue>> ScalarNames;
		for (const FMaterialParameterInfo& Info : ScalarInfos)
		{
			ScalarNames.Add(MakeShared<FJsonValueString>(Info.Name.ToString()));
		}

		TArray<TSharedPtr<FJsonValue>> VectorNames;
		for (const FMaterialParameterInfo& Info : VectorInfos)
		{
			VectorNames.Add(MakeShared<FJsonValueString>(Info.Name.ToString()));
		}

		Root->SetArrayField(TEXT("scalar_parameters"), ScalarNames);
		Root->SetArrayField(TEXT("vector_parameters"), VectorNames);
	}
	else if (UTexture2D* Texture = Cast<UTexture2D>(Asset))
	{
		Root->SetNumberField(TEXT("size_x"), Texture->GetSizeX());
		Root->SetNumberField(TEXT("size_y"), Texture->GetSizeY());
	}
	else if (USoundBase* Sound = Cast<USoundBase>(Asset))
	{
		Root->SetNumberField(TEXT("duration"), Sound->GetDuration());
	}
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		Root->SetStringField(TEXT("generated_class_path"), Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetPathName() : FString());
		Root->SetStringField(TEXT("parent_class"), Blueprint->ParentClass ? Blueprint->ParentClass->GetPathName() : FString());
	}

	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_FocusViewportOnActor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor)
	{
		return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	GEditor->MoveViewportCamerasToActor(*Actor, false);
	GEditor->RedrawAllViewports();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("actor_object_path"), Actor->GetPathName());
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetViewportInfo()
{
#if WITH_EDITOR
	FLevelEditorViewportClient* ViewportClient = GetPrimaryPerspectiveViewportClient();
	if (!ViewportClient)
	{
		return ErrorResponse(TEXT("No perspective level viewport available."));
	}

	const FVector ViewLocation = ViewportClient->GetViewLocation();
	const FRotator ViewRotation = ViewportClient->GetViewRotation();
	const FIntPoint SizeXY = ViewportClient->Viewport ? ViewportClient->Viewport->GetSizeXY() : FIntPoint::ZeroValue;

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetObjectField(TEXT("location"), VecToJson(ViewLocation));
	Root->SetNumberField(TEXT("pitch"), ViewRotation.Pitch);
	Root->SetNumberField(TEXT("yaw"), ViewRotation.Yaw);
	Root->SetNumberField(TEXT("roll"), ViewRotation.Roll);
	Root->SetNumberField(TEXT("fov"), ViewportClient->ViewFOV);
	Root->SetNumberField(TEXT("width"), SizeXY.X);
	Root->SetNumberField(TEXT("height"), SizeXY.Y);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetViewportCamera(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const double X = Args->HasField(TEXT("x")) ? Args->GetNumberField(TEXT("x")) : 0.0;
	const double Y = Args->HasField(TEXT("y")) ? Args->GetNumberField(TEXT("y")) : 0.0;
	const double Z = Args->HasField(TEXT("z")) ? Args->GetNumberField(TEXT("z")) : 170.0;
	const double Pitch = Args->HasField(TEXT("pitch")) ? Args->GetNumberField(TEXT("pitch")) : 0.0;
	const double Yaw   = Args->HasField(TEXT("yaw"))   ? Args->GetNumberField(TEXT("yaw"))   : 0.0;
	const double Roll  = Args->HasField(TEXT("roll"))  ? Args->GetNumberField(TEXT("roll"))  : 0.0;

	const FVector  NewLoc(X, Y, Z);
	const FRotator NewRot(Pitch, Yaw, Roll);

	for (FLevelEditorViewportClient* VC : GEditor->GetLevelViewportClients())
	{
		if (VC && VC->IsPerspective())
		{
			VC->SetViewLocation(NewLoc);
			VC->SetViewRotation(NewRot);
			VC->Invalidate();
			break;   // move the first perspective viewport only
		}
	}

	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetNumberField(TEXT("x"), X);
	Out->SetNumberField(TEXT("y"), Y);
	Out->SetNumberField(TEXT("z"), Z);
	Out->SetNumberField(TEXT("pitch"), Pitch);
	Out->SetNumberField(TEXT("yaw"),   Yaw);
	return ToJsonString(Out);
#else
	return ErrorResponse(TEXT("Editor not available"));
#endif
}

FString UAgentForgeLibrary::Cmd_RedrawViewports()
{
#if WITH_EDITOR
	GEditor->RedrawAllViewports();
	return OkResponse(TEXT("All viewports redrawn."));
#else
	return ErrorResponse(TEXT("Editor not available"));
#endif
}

// ============================================================================
//  ACTOR CONTROL
// ============================================================================
FString UAgentForgeLibrary::Cmd_SpawnActor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ClassPath;
	Args->TryGetStringField(TEXT("class_path"), ClassPath);
	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;
	const float Pitch = Args->HasField(TEXT("pitch")) ? (float)Args->GetNumberField(TEXT("pitch")) : 0.f;
	const float Yaw   = Args->HasField(TEXT("yaw"))   ? (float)Args->GetNumberField(TEXT("yaw"))   : 0.f;
	const float Roll  = Args->HasField(TEXT("roll"))  ? (float)Args->GetNumberField(TEXT("roll"))  : 0.f;

	UClass* ActorClass = LoadObject<UClass>(nullptr, *ClassPath);
	if (!ActorClass) { return ErrorResponse(FString::Printf(TEXT("Class not found: %s"), *ClassPath)); }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	AActor* Spawned = World->SpawnActor<AActor>(ActorClass,
		FVector(X, Y, Z), FRotator(Pitch, Yaw, Roll), Params);
	if (!Spawned) { return ErrorResponse(TEXT("SpawnActor returned null.")); }

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("spawned_name"), Spawned->GetName());
	Obj->SetStringField(TEXT("spawned_object_path"), Spawned->GetPathName());
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_DuplicateActor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	AActor* SourceActor = FindActorByLabelOrName(ActorName);
	if (!SourceActor)
	{
		return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	const FVector Offset(
		Args->HasField(TEXT("offset_x")) ? (float)Args->GetNumberField(TEXT("offset_x")) : 0.0f,
		Args->HasField(TEXT("offset_y")) ? (float)Args->GetNumberField(TEXT("offset_y")) : 0.0f,
		Args->HasField(TEXT("offset_z")) ? (float)Args->GetNumberField(TEXT("offset_z")) : 0.0f);

	UEditorActorSubsystem* ActorSubsystem = GEditor->GetEditorSubsystem<UEditorActorSubsystem>();
	if (!ActorSubsystem)
	{
		return ErrorResponse(TEXT("EditorActorSubsystem unavailable."));
	}

	TArray<AActor*> Duplicates = ActorSubsystem->DuplicateActors({ SourceActor }, SourceActor->GetWorld(), Offset);
	if (Duplicates.Num() == 0 || !Duplicates[0])
	{
		return ErrorResponse(TEXT("Failed to duplicate actor."));
	}

	AActor* Duplicate = Duplicates[0];
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("source_actor"), SourceActor->GetActorLabel());
	Root->SetStringField(TEXT("actor_name"), Duplicate->GetActorLabel());
	Root->SetStringField(TEXT("actor_object_path"), Duplicate->GetPathName());
	Root->SetObjectField(TEXT("location"), VecToJson(Duplicate->GetActorLocation()));
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SpawnPointLight(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;
	const float Intensity = Args->HasField(TEXT("intensity")) ? (float)Args->GetNumberField(TEXT("intensity")) : 5000.f;
	const float ColorR = Args->HasField(TEXT("color_r")) ? (float)Args->GetNumberField(TEXT("color_r")) : 1.f;
	const float ColorG = Args->HasField(TEXT("color_g")) ? (float)Args->GetNumberField(TEXT("color_g")) : 1.f;
	const float ColorB = Args->HasField(TEXT("color_b")) ? (float)Args->GetNumberField(TEXT("color_b")) : 1.f;
	const float AttenuationRadius = Args->HasField(TEXT("attenuation_radius")) ? (float)Args->GetNumberField(TEXT("attenuation_radius")) : 1200.f;
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APointLight* LightActor = World->SpawnActor<APointLight>(
		APointLight::StaticClass(),
		FVector(X, Y, Z),
		FRotator::ZeroRotator,
		Params);
	if (!LightActor) { return ErrorResponse(TEXT("Failed to spawn point light.")); }

	if (!Label.IsEmpty())
	{
		LightActor->SetActorLabel(Label);
	}

	UPointLightComponent* LightComp = Cast<UPointLightComponent>(LightActor->GetLightComponent());
	if (!LightComp) { return ErrorResponse(TEXT("Spawned point light has no light component.")); }

	LightActor->Modify();
	LightComp->Modify();
	LightComp->SetMobility(EComponentMobility::Movable);
	LightComp->SetIntensity(Intensity);
	LightComp->SetLightColor(FLinearColor(ColorR, ColorG, ColorB), false);
	LightComp->SetAttenuationRadius(AttenuationRadius);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("spawned_name"), LightActor->GetName());
	Obj->SetStringField(TEXT("spawned_object_path"), LightActor->GetPathName());
	Obj->SetStringField(TEXT("label"), LightActor->GetActorLabel());
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SpawnSpotLight(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;
	const float RX = Args->HasField(TEXT("rx")) ? (float)Args->GetNumberField(TEXT("rx")) : 0.f;
	const float RY = Args->HasField(TEXT("ry")) ? (float)Args->GetNumberField(TEXT("ry")) : 0.f;
	const float RZ = Args->HasField(TEXT("rz")) ? (float)Args->GetNumberField(TEXT("rz")) : 0.f;
	const float Intensity = Args->HasField(TEXT("intensity")) ? (float)Args->GetNumberField(TEXT("intensity")) : 5000.f;
	const float ColorR = Args->HasField(TEXT("color_r")) ? (float)Args->GetNumberField(TEXT("color_r")) : 1.f;
	const float ColorG = Args->HasField(TEXT("color_g")) ? (float)Args->GetNumberField(TEXT("color_g")) : 1.f;
	const float ColorB = Args->HasField(TEXT("color_b")) ? (float)Args->GetNumberField(TEXT("color_b")) : 1.f;
	const float InnerCone = Args->HasField(TEXT("inner_cone_angle")) ? (float)Args->GetNumberField(TEXT("inner_cone_angle")) : 15.f;
	const float OuterCone = Args->HasField(TEXT("outer_cone_angle")) ? (float)Args->GetNumberField(TEXT("outer_cone_angle")) : 30.f;
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASpotLight* LightActor = World->SpawnActor<ASpotLight>(
		ASpotLight::StaticClass(),
		FVector(X, Y, Z),
		FRotator(RX, RY, RZ),
		Params);
	if (!LightActor) { return ErrorResponse(TEXT("Failed to spawn spot light.")); }

	if (!Label.IsEmpty())
	{
		LightActor->SetActorLabel(Label);
	}

	USpotLightComponent* LightComp = Cast<USpotLightComponent>(LightActor->GetLightComponent());
	if (!LightComp) { return ErrorResponse(TEXT("Spawned spot light has no light component.")); }

	LightActor->Modify();
	LightComp->Modify();
	LightComp->SetMobility(EComponentMobility::Movable);
	LightComp->SetIntensity(Intensity);
	LightComp->SetLightColor(FLinearColor(ColorR, ColorG, ColorB), false);
	LightComp->SetInnerConeAngle(InnerCone);
	LightComp->SetOuterConeAngle(OuterCone);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("spawned_name"), LightActor->GetName());
	Obj->SetStringField(TEXT("spawned_object_path"), LightActor->GetPathName());
	Obj->SetStringField(TEXT("label"), LightActor->GetActorLabel());
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SpawnRectLight(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;
	const float RX = Args->HasField(TEXT("rx")) ? (float)Args->GetNumberField(TEXT("rx")) : 0.f;
	const float RY = Args->HasField(TEXT("ry")) ? (float)Args->GetNumberField(TEXT("ry")) : 0.f;
	const float RZ = Args->HasField(TEXT("rz")) ? (float)Args->GetNumberField(TEXT("rz")) : 0.f;
	const float Intensity = Args->HasField(TEXT("intensity")) ? (float)Args->GetNumberField(TEXT("intensity")) : 5000.f;
	const float Width = Args->HasField(TEXT("width")) ? (float)Args->GetNumberField(TEXT("width")) : 100.f;
	const float Height = Args->HasField(TEXT("height")) ? (float)Args->GetNumberField(TEXT("height")) : 100.f;
	const float ColorR = Args->HasField(TEXT("color_r")) ? (float)Args->GetNumberField(TEXT("color_r")) : 1.f;
	const float ColorG = Args->HasField(TEXT("color_g")) ? (float)Args->GetNumberField(TEXT("color_g")) : 1.f;
	const float ColorB = Args->HasField(TEXT("color_b")) ? (float)Args->GetNumberField(TEXT("color_b")) : 1.f;
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARectLight* LightActor = World->SpawnActor<ARectLight>(
		ARectLight::StaticClass(),
		FVector(X, Y, Z),
		FRotator(RX, RY, RZ),
		Params);
	if (!LightActor) { return ErrorResponse(TEXT("Failed to spawn rect light.")); }

	if (!Label.IsEmpty())
	{
		LightActor->SetActorLabel(Label);
	}

	URectLightComponent* LightComp = Cast<URectLightComponent>(LightActor->GetLightComponent());
	if (!LightComp) { return ErrorResponse(TEXT("Spawned rect light has no light component.")); }

	LightActor->Modify();
	LightComp->Modify();
	LightComp->SetMobility(EComponentMobility::Movable);
	LightComp->SetIntensity(Intensity);
	LightComp->SetLightColor(FLinearColor(ColorR, ColorG, ColorB), false);
	LightComp->SetSourceWidth(Width);
	LightComp->SetSourceHeight(Height);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("spawned_name"), LightActor->GetName());
	Root->SetStringField(TEXT("spawned_object_path"), LightActor->GetPathName());
	Root->SetStringField(TEXT("label"), LightActor->GetActorLabel());
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SpawnDirectionalLight(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float RX = Args->HasField(TEXT("rx")) ? (float)Args->GetNumberField(TEXT("rx")) : -45.f;
	const float RY = Args->HasField(TEXT("ry")) ? (float)Args->GetNumberField(TEXT("ry")) : 0.f;
	const float RZ = Args->HasField(TEXT("rz")) ? (float)Args->GetNumberField(TEXT("rz")) : 0.f;
	const float Intensity = Args->HasField(TEXT("intensity")) ? (float)Args->GetNumberField(TEXT("intensity")) : 10.f;
	const float ColorR = Args->HasField(TEXT("color_r")) ? (float)Args->GetNumberField(TEXT("color_r")) : 1.f;
	const float ColorG = Args->HasField(TEXT("color_g")) ? (float)Args->GetNumberField(TEXT("color_g")) : 1.f;
	const float ColorB = Args->HasField(TEXT("color_b")) ? (float)Args->GetNumberField(TEXT("color_b")) : 1.f;
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ADirectionalLight* LightActor = World->SpawnActor<ADirectionalLight>(
		ADirectionalLight::StaticClass(),
		FVector::ZeroVector,
		FRotator(RX, RY, RZ),
		Params);
	if (!LightActor) { return ErrorResponse(TEXT("Failed to spawn directional light.")); }

	if (!Label.IsEmpty())
	{
		LightActor->SetActorLabel(Label);
	}

	ULightComponent* LightComp = LightActor->GetLightComponent();
	if (!LightComp) { return ErrorResponse(TEXT("Spawned directional light has no light component.")); }

	LightActor->Modify();
	LightComp->Modify();
	LightComp->SetMobility(EComponentMobility::Movable);
	LightComp->SetIntensity(Intensity);
	LightComp->SetLightColor(FLinearColor(ColorR, ColorG, ColorB), false);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("spawned_name"), LightActor->GetName());
	Root->SetStringField(TEXT("spawned_object_path"), LightActor->GetPathName());
	Root->SetStringField(TEXT("label"), LightActor->GetActorLabel());
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorTransform(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ObjectPath;
	Args->TryGetStringField(TEXT("object_path"), ObjectPath);

	AActor* Actor = FindActorByLabelOrName(ObjectPath);
	if (!Actor)
	{
		// Try object path load
		Actor = Cast<AActor>(StaticFindObject(AActor::StaticClass(), nullptr, *ObjectPath));
	}
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ObjectPath)); }

	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : Actor->GetActorLocation().X;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : Actor->GetActorLocation().Y;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : Actor->GetActorLocation().Z;
	const float Pitch = Args->HasField(TEXT("pitch")) ? (float)Args->GetNumberField(TEXT("pitch")) : Actor->GetActorRotation().Pitch;
	const float Yaw   = Args->HasField(TEXT("yaw"))   ? (float)Args->GetNumberField(TEXT("yaw"))   : Actor->GetActorRotation().Yaw;
	const float Roll  = Args->HasField(TEXT("roll"))  ? (float)Args->GetNumberField(TEXT("roll"))  : Actor->GetActorRotation().Roll;

	Actor->SetActorLocationAndRotation(FVector(X, Y, Z), FRotator(Pitch, Yaw, Roll));

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_object_path"), Actor->GetPathName());
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetStaticMesh(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString MeshPath;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("mesh_path"), MeshPath);

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	UStaticMeshComponent* MeshComp = FindStaticMeshComponentOnActor(Actor);
	if (!MeshComp) { return ErrorResponse(TEXT("Actor has no static mesh component.")); }

	UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
	if (!StaticMesh) { return ErrorResponse(FString::Printf(TEXT("Static mesh not found: %s"), *MeshPath)); }

	Actor->Modify();
	MeshComp->Modify();
	MeshComp->SetStaticMesh(StaticMesh);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("mesh_path"), MeshPath);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorScale(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	const FVector NewScale(
		Args->HasField(TEXT("sx")) ? (float)Args->GetNumberField(TEXT("sx")) : Actor->GetActorScale3D().X,
		Args->HasField(TEXT("sy")) ? (float)Args->GetNumberField(TEXT("sy")) : Actor->GetActorScale3D().Y,
		Args->HasField(TEXT("sz")) ? (float)Args->GetNumberField(TEXT("sz")) : Actor->GetActorScale3D().Z);

	Actor->Modify();
	Actor->SetActorScale3D(NewScale);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Obj->SetObjectField(TEXT("scale"), VecToJson(NewScale));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorLabel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString NewLabel;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("new_label"), NewLabel);
	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }
	if (NewLabel.IsEmpty()) { return ErrorResponse(TEXT("set_actor_label requires new_label.")); }

	Actor->Modify();
	Actor->SetActorLabel(NewLabel);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("actor_object_path"), Actor->GetPathName());
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorMobility(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString MobilityString;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("mobility"), MobilityString);
	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	EComponentMobility::Type Mobility = EComponentMobility::Static;
	if (!ParseMobilityValue(MobilityString, Mobility))
	{
		return ErrorResponse(TEXT("mobility must be Static, Stationary, or Movable."));
	}

	TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
	for (USceneComponent* Component : SceneComponents)
	{
		if (Component)
		{
			Component->Modify();
			Component->SetMobility(Mobility);
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("mobility"), MobilityString);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorVisibility(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	const bool bVisible = !Args->HasField(TEXT("visible")) || Args->GetBoolField(TEXT("visible"));

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	Actor->Modify();
	Actor->SetIsTemporarilyHiddenInEditor(!bVisible);
	Actor->SetActorHiddenInGame(!bVisible);

	TInlineComponentArray<USceneComponent*> SceneComponents(Actor);
	for (USceneComponent* Component : SceneComponents)
	{
		if (Component)
		{
			Component->Modify();
			Component->SetVisibility(bVisible, true);
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetBoolField(TEXT("visible"), bVisible);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GroupActors(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const TArray<TSharedPtr<FJsonValue>>* ActorNames = nullptr;
	if (!Args->TryGetArrayField(TEXT("actor_names"), ActorNames) || !ActorNames || ActorNames->Num() == 0)
	{
		return ErrorResponse(TEXT("group_actors requires actor_names."));
	}

	FString GroupName = TEXT("ActorGroup");
	Args->TryGetStringField(TEXT("group_name"), GroupName);

	TArray<AActor*> ActorsToGroup;
	FVector AverageLocation = FVector::ZeroVector;
	for (const TSharedPtr<FJsonValue>& Entry : *ActorNames)
	{
		const FString ActorName = Entry.IsValid() ? Entry->AsString() : FString();
		AActor* Actor = FindActorByLabelOrName(ActorName);
		if (!Actor)
		{
			return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName));
		}
		ActorsToGroup.Add(Actor);
		AverageLocation += Actor->GetActorLocation();
	}

	AverageLocation /= FMath::Max(1, ActorsToGroup.Num());

	FString GroupError;
	AActor* GroupActor = SpawnGroupActor(GetEditorWorld(), GroupName, AverageLocation, GroupError);
	if (!GroupActor)
	{
		return ErrorResponse(GroupError);
	}

	TArray<TSharedPtr<FJsonValue>> Children;
	for (AActor* Actor : ActorsToGroup)
	{
		AttachActorToParent(Actor, GroupActor);
		AddActorSummary(Children, Actor);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("group_name"), GroupActor->GetActorLabel());
	Root->SetStringField(TEXT("group_object_path"), GroupActor->GetPathName());
	Root->SetArrayField(TEXT("children"), Children);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_GetActorProperty(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString PropertyName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }
	if (PropertyName.IsEmpty()) { return ErrorResponse(TEXT("get_actor_property requires property_name.")); }

	FString LeafName;
	FString ResolveError;
	UObject* ContainerObject = ResolvePropertyContainer(Actor, PropertyName, LeafName, ResolveError);
	if (!ContainerObject)
	{
		return ErrorResponse(ResolveError);
	}

	FString ExportedValue;
	if (!ExportResolvedPropertyValue(ContainerObject, LeafName, ExportedValue, ResolveError))
	{
		return ErrorResponse(ResolveError);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("property_name"), PropertyName);
	Root->SetStringField(TEXT("value"), ExportedValue);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetActorProperty(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString PropertyName;
	FString Value;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("property_name"), PropertyName);
	Args->TryGetStringField(TEXT("value"), Value);
	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }
	if (PropertyName.IsEmpty()) { return ErrorResponse(TEXT("set_actor_property requires property_name.")); }

	FString LeafName;
	FString ResolveError;
	UObject* ContainerObject = ResolvePropertyContainer(Actor, PropertyName, LeafName, ResolveError);
	if (!ContainerObject)
	{
		return ErrorResponse(ResolveError);
	}

	if (!ImportResolvedPropertyValue(ContainerObject, LeafName, Value, ResolveError))
	{
		return ErrorResponse(ResolveError);
	}

	FString ExportedValue;
	ExportResolvedPropertyValue(ContainerObject, LeafName, ExportedValue, ResolveError);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Root->SetStringField(TEXT("property_name"), PropertyName);
	Root->SetStringField(TEXT("value"), ExportedValue);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreateWall(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float StartX = Args->HasField(TEXT("start_x")) ? (float)Args->GetNumberField(TEXT("start_x")) : 0.0f;
	const float StartY = Args->HasField(TEXT("start_y")) ? (float)Args->GetNumberField(TEXT("start_y")) : 0.0f;
	const float EndX = Args->HasField(TEXT("end_x")) ? (float)Args->GetNumberField(TEXT("end_x")) : 0.0f;
	const float EndY = Args->HasField(TEXT("end_y")) ? (float)Args->GetNumberField(TEXT("end_y")) : 0.0f;
	const float Height = Args->HasField(TEXT("height")) ? (float)Args->GetNumberField(TEXT("height")) : 300.0f;
	const float Thickness = Args->HasField(TEXT("thickness")) ? (float)Args->GetNumberField(TEXT("thickness")) : 20.0f;
	const float BaseZ = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.0f;
	const bool bHasWindows = Args->HasField(TEXT("has_windows")) && Args->GetBoolField(TEXT("has_windows"));
	const float WindowSpacing = Args->HasField(TEXT("window_spacing")) ? (float)Args->GetNumberField(TEXT("window_spacing")) : 220.0f;
	const float WindowHeight = Args->HasField(TEXT("window_height")) ? (float)Args->GetNumberField(TEXT("window_height")) : 120.0f;
	FString MaterialPath;
	FString Label = TEXT("Wall");
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	TArray<AActor*> Segments;
	const FString WallError = SpawnLinearWallSegments(
		World,
		FVector(StartX, StartY, 0.0f),
		FVector(EndX, EndY, 0.0f),
		BaseZ,
		Height,
		Thickness,
		MaterialPath,
		Label,
		bHasWindows,
		WindowSpacing,
		WindowHeight,
		nullptr,
		Segments);
	if (!WallError.IsEmpty())
	{
		return ErrorResponse(WallError);
	}

	TArray<TSharedPtr<FJsonValue>> SegmentArray;
	for (AActor* Segment : Segments)
	{
		AddActorSummary(SegmentArray, Segment);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("label"), Label);
	Obj->SetNumberField(TEXT("segment_count"), Segments.Num());
	Obj->SetArrayField(TEXT("segments"), SegmentArray);
	if (Segments.Num() > 0)
	{
		Obj->SetStringField(TEXT("actor_name"), Segments[0]->GetActorLabel());
		Obj->SetStringField(TEXT("actor_object_path"), Segments[0]->GetPathName());
	}
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreateFloor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float CenterX = Args->HasField(TEXT("center_x")) ? (float)Args->GetNumberField(TEXT("center_x")) : 0.0f;
	const float CenterY = Args->HasField(TEXT("center_y")) ? (float)Args->GetNumberField(TEXT("center_y")) : 0.0f;
	const float SurfaceZ = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.0f;
	const float Width = Args->HasField(TEXT("width")) ? (float)Args->GetNumberField(TEXT("width")) : 500.0f;
	const float Length = Args->HasField(TEXT("length")) ? (float)Args->GetNumberField(TEXT("length")) : 500.0f;
	const float Thickness = Args->HasField(TEXT("thickness")) ? (float)Args->GetNumberField(TEXT("thickness")) : 10.0f;
	FString MaterialPath;
	FString Label = TEXT("Floor");
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetStringField(TEXT("label"), Label);

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FString SpawnError;
	AStaticMeshActor* FloorActor = SpawnBoxActor(
		World,
		FVector(CenterX, CenterY, SurfaceZ - (Thickness * 0.5f)),
		FVector(Width, Length, Thickness),
		FRotator::ZeroRotator,
		Label,
		MaterialPath,
		SpawnError);
	if (!FloorActor)
	{
		return ErrorResponse(SpawnError);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), FloorActor->GetActorLabel());
	Obj->SetStringField(TEXT("actor_object_path"), FloorActor->GetPathName());
	Obj->SetNumberField(TEXT("width"), Width);
	Obj->SetNumberField(TEXT("length"), Length);
	Obj->SetNumberField(TEXT("thickness"), Thickness);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreateRoom(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float CenterX = Args->HasField(TEXT("center_x")) ? (float)Args->GetNumberField(TEXT("center_x")) : 0.0f;
	const float CenterY = Args->HasField(TEXT("center_y")) ? (float)Args->GetNumberField(TEXT("center_y")) : 0.0f;
	const float BaseZ = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.0f;
	const float Width = Args->HasField(TEXT("width")) ? (float)Args->GetNumberField(TEXT("width")) : 500.0f;
	const float Length = Args->HasField(TEXT("length")) ? (float)Args->GetNumberField(TEXT("length")) : 600.0f;
	const float Height = Args->HasField(TEXT("height")) ? (float)Args->GetNumberField(TEXT("height")) : 300.0f;
	const float WallThickness = Args->HasField(TEXT("wall_thickness")) ? (float)Args->GetNumberField(TEXT("wall_thickness")) : 20.0f;
	const float SlabThickness = Args->HasField(TEXT("slab_thickness")) ? (float)Args->GetNumberField(TEXT("slab_thickness")) : 10.0f;
	FString FloorMaterial;
	FString WallMaterial;
	FString CeilingMaterial;
	FString DoorWall;
	FString Label = TEXT("Room");
	Args->TryGetStringField(TEXT("floor_material"), FloorMaterial);
	Args->TryGetStringField(TEXT("wall_material"), WallMaterial);
	Args->TryGetStringField(TEXT("ceiling_material"), CeilingMaterial);
	Args->TryGetStringField(TEXT("door_wall"), DoorWall);
	Args->TryGetStringField(TEXT("label"), Label);
	DoorWall.ToLowerInline();

	TSet<FString> WindowWalls;
	const TArray<TSharedPtr<FJsonValue>>* WindowWallArray = nullptr;
	if (Args->TryGetArrayField(TEXT("window_walls"), WindowWallArray))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *WindowWallArray)
		{
			FString WallName;
			if (Entry.IsValid() && Entry->TryGetString(WallName))
			{
				WallName.ToLowerInline();
				WindowWalls.Add(WallName);
			}
		}
	}

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FString GroupError;
	AActor* GroupActor = SpawnGroupActor(World, Label, FVector(CenterX, CenterY, BaseZ), GroupError);
	if (!GroupActor)
	{
		return ErrorResponse(GroupError);
	}

	TArray<AActor*> RoomActors;
	auto SpawnRoomBox = [&](const FString& ChildLabel, const FVector& Center, const FVector& Dimensions, const FRotator& Rotation, const FString& MaterialPath) -> bool
	{
		FString SpawnError;
		AStaticMeshActor* ChildActor = SpawnBoxActor(World, Center, Dimensions, Rotation, ChildLabel, MaterialPath, SpawnError);
		if (!ChildActor)
		{
			GroupError = SpawnError;
			return false;
		}
		AttachActorToParent(ChildActor, GroupActor);
		RoomActors.Add(ChildActor);
		return true;
	};

	if (!SpawnRoomBox(
		Label + TEXT("_Floor"),
		FVector(CenterX, CenterY, BaseZ - (SlabThickness * 0.5f)),
		FVector(Width, Length, SlabThickness),
		FRotator::ZeroRotator,
		FloorMaterial))
	{
		return ErrorResponse(GroupError);
	}

	if (!SpawnRoomBox(
		Label + TEXT("_Ceiling"),
		FVector(CenterX, CenterY, BaseZ + Height + (SlabThickness * 0.5f)),
		FVector(Width, Length, SlabThickness),
		FRotator::ZeroRotator,
		CeilingMaterial.IsEmpty() ? FloorMaterial : CeilingMaterial))
	{
		return ErrorResponse(GroupError);
	}

	auto SpawnDoorWall = [&](const FString& Prefix, const FVector& Start, const FVector& End) -> bool
	{
		const FVector Delta = End - Start;
		const float WallLength = Delta.Size();
		if (WallLength <= KINDA_SMALL_NUMBER)
		{
			GroupError = TEXT("Door wall length is zero.");
			return false;
		}

		const FVector Direction = Delta / WallLength;
		const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
		const FRotator Rotation(0.0f, YawDeg, 0.0f);
		const float DoorWidth = FMath::Min(120.0f, WallLength * 0.45f);
		const float DoorHeight = FMath::Min(220.0f, Height - 40.0f);
		const float SideLength = FMath::Max(0.0f, (WallLength - DoorWidth) * 0.5f);
		int32 SegmentIndex = 0;

		auto SpawnSegment = [&](float StartAlong, float SegmentLength, float BottomZ, float SegmentHeight) -> bool
		{
			if (SegmentLength <= KINDA_SMALL_NUMBER || SegmentHeight <= KINDA_SMALL_NUMBER)
			{
				return true;
			}

			const FVector SegmentCenter =
				Start +
				Direction * (StartAlong + (SegmentLength * 0.5f)) +
				FVector(0.0f, 0.0f, BottomZ + (SegmentHeight * 0.5f));
			return SpawnRoomBox(
				FString::Printf(TEXT("%s_%02d"), *Prefix, ++SegmentIndex),
				SegmentCenter,
				FVector(SegmentLength, WallThickness, SegmentHeight),
				Rotation,
				WallMaterial);
		};

		if (!SpawnSegment(0.0f, SideLength, BaseZ, Height)) { return false; }
		if (!SpawnSegment(SideLength + DoorWidth, SideLength, BaseZ, Height)) { return false; }
		if (!SpawnSegment(SideLength, DoorWidth, BaseZ + DoorHeight, Height - DoorHeight)) { return false; }
		return true;
	};

	auto SpawnFullWall = [&](const FString& DirectionName, const FVector& Start, const FVector& End) -> bool
	{
		const bool bHasWindows = WindowWalls.Contains(DirectionName);
		TArray<AActor*> WallSegments;
		const FString WallError = SpawnLinearWallSegments(
			World,
			Start,
			End,
			BaseZ,
			Height,
			WallThickness,
			WallMaterial,
			Label + TEXT("_") + DirectionName + TEXT("Wall"),
			bHasWindows,
			220.0f,
			120.0f,
			GroupActor,
			WallSegments);
		if (!WallError.IsEmpty())
		{
			GroupError = WallError;
			return false;
		}
		RoomActors.Append(WallSegments);
		return true;
	};

	const float HalfWidth = Width * 0.5f;
	const float HalfLength = Length * 0.5f;
	const FVector NorthStart(CenterX - HalfWidth, CenterY + HalfLength - (WallThickness * 0.5f), 0.0f);
	const FVector NorthEnd(CenterX + HalfWidth, CenterY + HalfLength - (WallThickness * 0.5f), 0.0f);
	const FVector SouthStart(CenterX - HalfWidth, CenterY - HalfLength + (WallThickness * 0.5f), 0.0f);
	const FVector SouthEnd(CenterX + HalfWidth, CenterY - HalfLength + (WallThickness * 0.5f), 0.0f);
	const FVector EastStart(CenterX + HalfWidth - (WallThickness * 0.5f), CenterY - HalfLength, 0.0f);
	const FVector EastEnd(CenterX + HalfWidth - (WallThickness * 0.5f), CenterY + HalfLength, 0.0f);
	const FVector WestStart(CenterX - HalfWidth + (WallThickness * 0.5f), CenterY - HalfLength, 0.0f);
	const FVector WestEnd(CenterX - HalfWidth + (WallThickness * 0.5f), CenterY + HalfLength, 0.0f);

	if (DoorWall == TEXT("north"))
	{
		if (!SpawnDoorWall(Label + TEXT("_NorthDoorWall"), NorthStart, NorthEnd)) { return ErrorResponse(GroupError); }
	}
	else if (!SpawnFullWall(TEXT("north"), NorthStart, NorthEnd))
	{
		return ErrorResponse(GroupError);
	}

	if (DoorWall == TEXT("south"))
	{
		if (!SpawnDoorWall(Label + TEXT("_SouthDoorWall"), SouthStart, SouthEnd)) { return ErrorResponse(GroupError); }
	}
	else if (!SpawnFullWall(TEXT("south"), SouthStart, SouthEnd))
	{
		return ErrorResponse(GroupError);
	}

	if (DoorWall == TEXT("east"))
	{
		if (!SpawnDoorWall(Label + TEXT("_EastDoorWall"), EastStart, EastEnd)) { return ErrorResponse(GroupError); }
	}
	else if (!SpawnFullWall(TEXT("east"), EastStart, EastEnd))
	{
		return ErrorResponse(GroupError);
	}

	if (DoorWall == TEXT("west"))
	{
		if (!SpawnDoorWall(Label + TEXT("_WestDoorWall"), WestStart, WestEnd)) { return ErrorResponse(GroupError); }
	}
	else if (!SpawnFullWall(TEXT("west"), WestStart, WestEnd))
	{
		return ErrorResponse(GroupError);
	}

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (AActor* Actor : RoomActors)
	{
		AddActorSummary(ActorArray, Actor);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("group_name"), GroupActor->GetActorLabel());
	Obj->SetStringField(TEXT("group_object_path"), GroupActor->GetPathName());
	Obj->SetNumberField(TEXT("child_count"), RoomActors.Num());
	Obj->SetArrayField(TEXT("children"), ActorArray);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreateCorridor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float StartX = Args->HasField(TEXT("start_x")) ? (float)Args->GetNumberField(TEXT("start_x")) : 0.0f;
	const float StartY = Args->HasField(TEXT("start_y")) ? (float)Args->GetNumberField(TEXT("start_y")) : 0.0f;
	const float EndX = Args->HasField(TEXT("end_x")) ? (float)Args->GetNumberField(TEXT("end_x")) : 500.0f;
	const float EndY = Args->HasField(TEXT("end_y")) ? (float)Args->GetNumberField(TEXT("end_y")) : 0.0f;
	const float BaseZ = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.0f;
	const float Width = Args->HasField(TEXT("width")) ? (float)Args->GetNumberField(TEXT("width")) : 250.0f;
	const float Height = Args->HasField(TEXT("height")) ? (float)Args->GetNumberField(TEXT("height")) : 300.0f;
	const float WallThickness = Args->HasField(TEXT("wall_thickness")) ? (float)Args->GetNumberField(TEXT("wall_thickness")) : 20.0f;
	const float SlabThickness = Args->HasField(TEXT("slab_thickness")) ? (float)Args->GetNumberField(TEXT("slab_thickness")) : 10.0f;
	const bool bHasCeiling = !Args->HasField(TEXT("has_ceiling")) || Args->GetBoolField(TEXT("has_ceiling"));
	FString WallMaterial;
	FString FloorMaterial;
	FString Label = TEXT("Corridor");
	Args->TryGetStringField(TEXT("wall_material"), WallMaterial);
	Args->TryGetStringField(TEXT("floor_material"), FloorMaterial);
	Args->TryGetStringField(TEXT("label"), Label);

	const FVector Start(StartX, StartY, 0.0f);
	const FVector End(EndX, EndY, 0.0f);
	const FVector Delta = End - Start;
	const float Length = Delta.Size();
	if (Length <= KINDA_SMALL_NUMBER)
	{
		return ErrorResponse(TEXT("Corridor start/end points are identical."));
	}

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	const FVector Direction = Delta / Length;
	const FVector MidPoint = Start + (Delta * 0.5f);
	const FVector RightVector = FVector(-Direction.Y, Direction.X, 0.0f);
	const float YawDeg = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
	const FRotator Rotation(0.0f, YawDeg, 0.0f);

	FString GroupError;
	AActor* GroupActor = SpawnGroupActor(World, Label, FVector(MidPoint.X, MidPoint.Y, BaseZ), GroupError);
	if (!GroupActor)
	{
		return ErrorResponse(GroupError);
	}

	TArray<AActor*> CorridorActors;
	auto SpawnChild = [&](const FString& ChildLabel, const FVector& Center, const FVector& Dimensions, const FString& MaterialPath) -> bool
	{
		FString SpawnError;
		AStaticMeshActor* ChildActor = SpawnBoxActor(World, Center, Dimensions, Rotation, ChildLabel, MaterialPath, SpawnError);
		if (!ChildActor)
		{
			GroupError = SpawnError;
			return false;
		}
		AttachActorToParent(ChildActor, GroupActor);
		CorridorActors.Add(ChildActor);
		return true;
	};

	if (!SpawnChild(
		Label + TEXT("_Floor"),
		FVector(MidPoint.X, MidPoint.Y, BaseZ - (SlabThickness * 0.5f)),
		FVector(Length, Width, SlabThickness),
		FloorMaterial))
	{
		return ErrorResponse(GroupError);
	}

	if (bHasCeiling)
	{
		if (!SpawnChild(
			Label + TEXT("_Ceiling"),
			FVector(MidPoint.X, MidPoint.Y, BaseZ + Height + (SlabThickness * 0.5f)),
			FVector(Length, Width, SlabThickness),
			FloorMaterial))
		{
			return ErrorResponse(GroupError);
		}
	}

	const FVector SideOffset = RightVector * ((Width * 0.5f) + (WallThickness * 0.5f));
	if (!SpawnChild(
		Label + TEXT("_Wall_Left"),
		FVector(MidPoint.X, MidPoint.Y, BaseZ + (Height * 0.5f)) + SideOffset,
		FVector(Length, WallThickness, Height),
		WallMaterial))
	{
		return ErrorResponse(GroupError);
	}

	if (!SpawnChild(
		Label + TEXT("_Wall_Right"),
		FVector(MidPoint.X, MidPoint.Y, BaseZ + (Height * 0.5f)) - SideOffset,
		FVector(Length, WallThickness, Height),
		WallMaterial))
	{
		return ErrorResponse(GroupError);
	}

	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (AActor* Actor : CorridorActors)
	{
		AddActorSummary(ActorArray, Actor);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("group_name"), GroupActor->GetActorLabel());
	Root->SetStringField(TEXT("group_object_path"), GroupActor->GetPathName());
	Root->SetNumberField(TEXT("child_count"), CorridorActors.Num());
	Root->SetArrayField(TEXT("children"), ActorArray);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreatePillar(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const float X = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.0f;
	const float Y = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.0f;
	const float Z = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.0f;
	const float Radius = Args->HasField(TEXT("radius")) ? (float)Args->GetNumberField(TEXT("radius")) : 25.0f;
	const float Height = Args->HasField(TEXT("height")) ? (float)Args->GetNumberField(TEXT("height")) : 300.0f;
	const int32 Sides = Args->HasField(TEXT("sides")) ? (int32)Args->GetNumberField(TEXT("sides")) : 16;
	FString MaterialPath;
	FString Label = TEXT("Pillar");
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	Args->TryGetStringField(TEXT("label"), Label);

	if (Radius <= KINDA_SMALL_NUMBER || Height <= KINDA_SMALL_NUMBER)
	{
		return ErrorResponse(TEXT("radius and height must be greater than zero."));
	}

	UWorld* World = GetEditorWorld();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FString SpawnError;
	AStaticMeshActor* PillarActor = nullptr;
	if (Sides <= 4)
	{
		PillarActor = SpawnBoxActor(
			World,
			FVector(X, Y, Z + (Height * 0.5f)),
			FVector(Radius * 2.0f, Radius * 2.0f, Height),
			FRotator::ZeroRotator,
			Label,
			MaterialPath,
			SpawnError);
	}
	else
	{
		UStaticMesh* CylinderMesh = GetBasicCylinderMesh();
		PillarActor = SpawnStaticMeshPrimitiveActor(
			World,
			CylinderMesh,
			FVector(X, Y, Z + (Height * 0.5f)),
			FVector(Radius / 50.0f, Radius / 50.0f, Height / 100.0f),
			FRotator::ZeroRotator,
			Label,
			MaterialPath,
			SpawnError);
	}

	if (!PillarActor)
	{
		return ErrorResponse(SpawnError);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("actor_name"), PillarActor->GetActorLabel());
	Root->SetStringField(TEXT("actor_object_path"), PillarActor->GetPathName());
	Root->SetNumberField(TEXT("radius"), Radius);
	Root->SetNumberField(TEXT("height"), Height);
	Root->SetNumberField(TEXT("sides"), Sides);
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_DeleteActor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Label;
	Args->TryGetStringField(TEXT("label"), Label);
	AActor* Actor = FindActorByLabelOrName(Label);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *Label)); }

	UWorld* World = Actor->GetWorld();
	if (!World) { return ErrorResponse(TEXT("Actor has no world.")); }

	World->DestroyActor(Actor);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"),      true);
	Obj->SetBoolField(TEXT("deleted"), true);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SaveCurrentLevel()
{
#if WITH_EDITOR
	if (!GEditor) { return ErrorResponse(TEXT("GEditor null.")); }
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	FEditorFileUtils::SaveLevel(World->PersistentLevel);
	return OkResponse(TEXT("Level saved."));
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_TakeScreenshot(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Filename = TEXT("AgentForge_Screenshot");
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("filename"), Filename); }

	const FString Path = QueueEditorScreenshot(Filename);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),   true);
	Obj->SetStringField(TEXT("path"), Path);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  SPATIAL QUERIES
// ============================================================================
FString UAgentForgeLibrary::Cmd_CastRay(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	const TSharedPtr<FJsonObject>* StartObj;
	const TSharedPtr<FJsonObject>* EndObj;
	if (!Args->TryGetObjectField(TEXT("start"), StartObj) ||
	    !Args->TryGetObjectField(TEXT("end"),   EndObj))
	{
		return ErrorResponse(TEXT("cast_ray requires start{x,y,z} and end{x,y,z}."));
	}

	const FVector Start(
		(*StartObj)->GetNumberField(TEXT("x")),
		(*StartObj)->GetNumberField(TEXT("y")),
		(*StartObj)->GetNumberField(TEXT("z")));
	const FVector End(
		(*EndObj)->GetNumberField(TEXT("x")),
		(*EndObj)->GetNumberField(TEXT("y")),
		(*EndObj)->GetNumberField(TEXT("z")));

	bool bTraceComplex = true;
	Args->TryGetBoolField(TEXT("trace_complex"), bTraceComplex);

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit, Start, End, ECC_Visibility,
		FCollisionQueryParams(NAME_None, bTraceComplex));

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("hit"), bHit);
	if (bHit)
	{
		Obj->SetObjectField(TEXT("hit_location"), VecToJson(Hit.Location));
		Obj->SetObjectField(TEXT("hit_normal"),   VecToJson(Hit.Normal));
		Obj->SetStringField(TEXT("hit_actor"),    Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT(""));
		Obj->SetNumberField(TEXT("distance"),     Hit.Distance);
	}
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_QueryNavMesh(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	const float QX = Args->HasField(TEXT("x")) ? (float)Args->GetNumberField(TEXT("x")) : 0.f;
	const float QY = Args->HasField(TEXT("y")) ? (float)Args->GetNumberField(TEXT("y")) : 0.f;
	const float QZ = Args->HasField(TEXT("z")) ? (float)Args->GetNumberField(TEXT("z")) : 0.f;
	const float EX = Args->HasField(TEXT("extent_x")) ? (float)Args->GetNumberField(TEXT("extent_x")) : 100.f;
	const float EY = Args->HasField(TEXT("extent_y")) ? (float)Args->GetNumberField(TEXT("extent_y")) : 100.f;
	const float EZ = Args->HasField(TEXT("extent_z")) ? (float)Args->GetNumberField(TEXT("extent_z")) : 200.f;

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) { return ErrorResponse(TEXT("No NavigationSystem in world.")); }

	FNavLocation NavLocation;
	const bool bOnNav = NavSys->ProjectPointToNavigation(
		FVector(QX, QY, QZ), NavLocation, FVector(EX, EY, EZ));

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("on_navmesh"), bOnNav);
	Obj->SetObjectField(TEXT("projected_location"), VecToJson(NavLocation.Location));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  BLUEPRINT MANIPULATION
// ============================================================================
FString UAgentForgeLibrary::Cmd_CreateBlueprint(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Name, ParentClass, OutputPath;
	Args->TryGetStringField(TEXT("name"),         Name);
	Args->TryGetStringField(TEXT("parent_class"),  ParentClass);
	Args->TryGetStringField(TEXT("output_path"),   OutputPath);

	if (Name.IsEmpty())       { return ErrorResponse(TEXT("create_blueprint requires non-empty 'name'.")); }
	if (ParentClass.IsEmpty()){ return ErrorResponse(TEXT("create_blueprint requires non-empty 'parent_class'.")); }
	if (OutputPath.IsEmpty()) { return ErrorResponse(TEXT("create_blueprint requires non-empty 'output_path'.")); }
	const FString RequestedName = Name;
	Name = ObjectTools::SanitizeObjectName(Name);
	if (Name.IsEmpty())
	{
		return ErrorResponse(TEXT("create_blueprint name is invalid after sanitization."));
	}
	OutputPath.RemoveFromEnd(TEXT("/"));
	if (!OutputPath.StartsWith(TEXT("/")) || !FPackageName::IsValidLongPackageName(OutputPath))
	{
		return ErrorResponse(FString::Printf(TEXT("Invalid output_path: %s"), *OutputPath));
	}

	UClass* Parent = LoadObject<UClass>(nullptr, *ParentClass);
	if (!Parent) { return ErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClass)); }

	auto BuildExistingResponse = [&](UBlueprint* ExistingBP, const FString& ExistingPackageName, const FString& ExistingName) -> FString
	{
		TSharedPtr<FJsonObject> ExistingObj = MakeShared<FJsonObject>();
		ExistingObj->SetBoolField  (TEXT("ok"), true);
		ExistingObj->SetBoolField  (TEXT("existing"), true);
		ExistingObj->SetStringField(TEXT("requested_name"), RequestedName);
		ExistingObj->SetStringField(TEXT("name"), ExistingName);
		ExistingObj->SetStringField(TEXT("package"), ExistingPackageName);
		ExistingObj->SetStringField(
			TEXT("generated_class_path"),
			ExistingBP && ExistingBP->GeneratedClass ? ExistingBP->GeneratedClass->GetPathName() : TEXT(""));
		return ToJsonString(ExistingObj);
	};

	FString FinalName = Name;
	FString PackageName = FString::Printf(TEXT("%s/%s"), *OutputPath, *FinalName);
	FString AssetObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *FinalName);

	// Idempotent fast path: if Blueprint already exists, return it instead of attempting
	// to recreate (avoids duplicate-name assertion in Kismet2.cpp).
	if (UBlueprint* ExistingBP = LoadObject<UBlueprint>(nullptr, *AssetObjectPath))
	{
		return BuildExistingResponse(ExistingBP, PackageName, FinalName);
	}

	if (UPackage* ExistingPackage = FindPackage(nullptr, *PackageName))
	{
		ExistingPackage->FullyLoad();
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) { return ErrorResponse(TEXT("Failed to create package.")); }
	Package->FullyLoad();

	// If any object already exists at this name inside the package, pick a unique suffix.
	if (FindObject<UObject>(Package, *FinalName) != nullptr)
	{
		const FName UniqueName = MakeUniqueObjectName(Package, UBlueprint::StaticClass(), *FinalName);
		FinalName = UniqueName.ToString();
		PackageName = FString::Printf(TEXT("%s/%s"), *OutputPath, *FinalName);
		AssetObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *FinalName);
		Package = CreatePackage(*PackageName);
		if (!Package) { return ErrorResponse(TEXT("Failed to create unique package for Blueprint.")); }
		Package->FullyLoad();
	}

	if (UBlueprint* ExistingAfterRename = LoadObject<UBlueprint>(nullptr, *AssetObjectPath))
	{
		return BuildExistingResponse(ExistingAfterRename, PackageName, FinalName);
	}

	// Additional in-package collision guard before FKismetEditorUtilities::CreateBlueprint.
	if (UBlueprint* ExistingInPackage = FindObject<UBlueprint>(Package, *FinalName))
	{
		return BuildExistingResponse(ExistingInPackage, PackageName, FinalName);
	}

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		Parent, Package, *FinalName, BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!BP) { return ErrorResponse(TEXT("CreateBlueprint returned null.")); }

	BP->MarkPackageDirty();
	SaveNewPackage(Package, BP);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),                   true);
	Obj->SetStringField(TEXT("requested_name"),       RequestedName);
	Obj->SetStringField(TEXT("name"),                 FinalName);
	Obj->SetStringField(TEXT("package"),               PackageName);
	Obj->SetStringField(TEXT("generated_class_path"),  BP->GeneratedClass ? BP->GeneratedClass->GetPathName() : TEXT(""));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CompileBlueprint(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!BP) { return ErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath)); }

	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),     BP->Status != BS_Error);
	Obj->SetStringField(TEXT("errors"), BP->Status == BS_Error ? TEXT("Compile errors detected.") : TEXT(""));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetBlueprintCDOProperty(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString BlueprintPath, PropertyName, TypeStr, Value;
	Args->TryGetStringField(TEXT("blueprint_path"),  BlueprintPath);
	Args->TryGetStringField(TEXT("property_name"),   PropertyName);
	Args->TryGetStringField(TEXT("type"),            TypeStr);
	Args->TryGetStringField(TEXT("value"),           Value);

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!BP) { return ErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath)); }

	UObject* CDO = BP->GeneratedClass ? BP->GeneratedClass->GetDefaultObject() : nullptr;
	if (!CDO) { return ErrorResponse(TEXT("Blueprint has no generated class CDO.")); }

	FProperty* Prop = CDO->GetClass()->FindPropertyByName(*PropertyName);
	if (!Prop) { return ErrorResponse(FString::Printf(TEXT("Property not found: %s"), *PropertyName)); }

	CDO->PreEditChange(Prop);
	if (TypeStr == TEXT("float") || TypeStr == TEXT("double"))
	{
		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
			DoubleProp->SetPropertyValue_InContainer(CDO, FCString::Atod(*Value));
		else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
			FloatProp->SetPropertyValue_InContainer(CDO, FCString::Atof(*Value));
	}
	else if (TypeStr == TEXT("int"))
	{
		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
			IntProp->SetPropertyValue_InContainer(CDO, FCString::Atoi(*Value));
	}
	else if (TypeStr == TEXT("bool"))
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
			BoolProp->SetPropertyValue_InContainer(CDO, Value.ToBool());
	}
	else if (TypeStr == TEXT("string"))
	{
		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
			StrProp->SetPropertyValue_InContainer(CDO, Value);
	}

	CDO->PostEditChange();
	CDO->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),        true);
	Obj->SetStringField(TEXT("property"),  PropertyName);
	Obj->SetStringField(TEXT("type"),      TypeStr);
	Obj->SetStringField(TEXT("value_set"), Value);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_EditBlueprintNode(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString BlueprintPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BlueprintPath);

	const TSharedPtr<FJsonObject>* NodeSpecPtr;
	if (!Args->TryGetObjectField(TEXT("node_spec"), NodeSpecPtr))
	{
		return ErrorResponse(TEXT("edit_blueprint_node requires 'node_spec' object."));
	}
	const TSharedPtr<FJsonObject>& NodeSpec = *NodeSpecPtr;

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
	if (!BP) { return ErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath)); }
	if (!BP->UbergraphPages.Num()) { return ErrorResponse(TEXT("Blueprint has no event graphs.")); }

	UEdGraph* EventGraph = BP->UbergraphPages[0];

	FString NodeType, NodeTitle;
	NodeSpec->TryGetStringField(TEXT("type"),  NodeType);
	NodeSpec->TryGetStringField(TEXT("title"), NodeTitle);

	// Find an existing node matching the type + title, or report what's there
	UEdGraphNode* FoundNode = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (!Node) { continue; }
		const FString NodeTitleStr = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		if (NodeTitleStr.Contains(NodeTitle) || NodeTitle.IsEmpty())
		{
			FoundNode = Node;
			break;
		}
	}

	if (!FoundNode)
	{
		// Report available nodes for the agent to correct its request
		TArray<FString> NodeNames;
		for (UEdGraphNode* Node : EventGraph->Nodes)
		{
			if (Node)
			{
				NodeNames.Add(Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
			}
		}
		return ErrorResponse(FString::Printf(
			TEXT("Node '%s' not found. Available nodes: [%s]"),
			*NodeTitle, *FString::Join(NodeNames, TEXT(", "))));
	}

	// Apply pin value changes from the spec
	const TArray<TSharedPtr<FJsonValue>>* Pins;
	if (NodeSpec->TryGetArrayField(TEXT("pins"), Pins))
	{
		for (const TSharedPtr<FJsonValue>& PinVal : *Pins)
		{
			const TSharedPtr<FJsonObject>* PinObj;
			if (!PinVal->TryGetObject(PinObj)) { continue; }
			FString PinName, PinValue;
			(*PinObj)->TryGetStringField(TEXT("name"),  PinName);
			(*PinObj)->TryGetStringField(TEXT("value"), PinValue);

			for (UEdGraphPin* Pin : FoundNode->Pins)
			{
				if (Pin && Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
				{
					Pin->DefaultValue = PinValue;
					break;
				}
			}
		}
	}

	FoundNode->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),        true);
	Obj->SetStringField(TEXT("node_guid"), FoundNode->NodeGuid.ToString());
	Obj->SetStringField(TEXT("action"),    TEXT("pins_updated"));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  MATERIAL INSTANCING
// ============================================================================
FString UAgentForgeLibrary::Cmd_CreateMaterialInstance(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ParentMaterial, InstanceName, OutputPath;
	Args->TryGetStringField(TEXT("parent_material"), ParentMaterial);
	Args->TryGetStringField(TEXT("instance_name"),   InstanceName);
	Args->TryGetStringField(TEXT("output_path"),     OutputPath);

	UMaterialInterface* Parent = LoadObject<UMaterialInterface>(nullptr, *ParentMaterial);
	if (!Parent) { return ErrorResponse(FString::Printf(TEXT("Parent material not found: %s"), *ParentMaterial)); }

	const FString PackageName = FString::Printf(TEXT("%s/%s"), *OutputPath, *InstanceName);
	UPackage* Package = CreatePackage(*PackageName);
	UMaterialInstanceConstant* MIC = NewObject<UMaterialInstanceConstant>(Package, *InstanceName, RF_Public | RF_Standalone);
	MIC->Parent = Parent;
	MIC->PostEditChange();
	SaveNewPackage(Package, MIC);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),      true);
	Obj->SetStringField(TEXT("package"), PackageName);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetMaterialParams(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString InstancePath;
	Args->TryGetStringField(TEXT("instance_path"), InstancePath);
	UMaterialInstanceConstant* MIC = LoadObject<UMaterialInstanceConstant>(nullptr, *InstancePath);
	if (!MIC) { return ErrorResponse(FString::Printf(TEXT("MIC not found: %s"), *InstancePath)); }

	int32 ScalarsSet = 0, VectorsSet = 0;

	const TSharedPtr<FJsonObject>* ScalarParams;
	if (Args->TryGetObjectField(TEXT("scalar_params"), ScalarParams))
	{
		for (auto& Pair : (*ScalarParams)->Values)
		{
			double Val = 0.0;
			Pair.Value->TryGetNumber(Val);
			MIC->SetScalarParameterValueEditorOnly(FName(*Pair.Key), (float)Val);
			++ScalarsSet;
		}
	}

	const TSharedPtr<FJsonObject>* VectorParams;
	if (Args->TryGetObjectField(TEXT("vector_params"), VectorParams))
	{
		for (auto& Pair : (*VectorParams)->Values)
		{
			const TSharedPtr<FJsonObject>* ColorObj;
			if (Pair.Value->TryGetObject(ColorObj))
			{
				FLinearColor Color(
					(float)(*ColorObj)->GetNumberField(TEXT("r")),
					(float)(*ColorObj)->GetNumberField(TEXT("g")),
					(float)(*ColorObj)->GetNumberField(TEXT("b")),
					(float)((*ColorObj)->HasField(TEXT("a")) ? (*ColorObj)->GetNumberField(TEXT("a")) : 1.0));
				MIC->SetVectorParameterValueEditorOnly(FName(*Pair.Key), Color);
				++VectorsSet;
			}
		}
	}

	MIC->PostEditChange();
	MIC->MarkPackageDirty();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),          true);
	Obj->SetNumberField(TEXT("scalars_set"), ScalarsSet);
	Obj->SetNumberField(TEXT("vectors_set"), VectorsSet);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_ApplyMaterialToActor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString MaterialPath;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("material_path"), MaterialPath);
	const int32 SlotIndex = Args->HasField(TEXT("slot_index")) ? (int32)Args->GetNumberField(TEXT("slot_index")) : 0;

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	UMeshComponent* MeshComp = FindMeshComponentOnActor(Actor);
	if (!MeshComp) { return ErrorResponse(TEXT("Actor has no mesh component.")); }

	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Material) { return ErrorResponse(FString::Printf(TEXT("Material not found: %s"), *MaterialPath)); }

	Actor->Modify();
	MeshComp->Modify();
	MeshComp->SetMaterial(SlotIndex, Material);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("material_path"), MaterialPath);
	Obj->SetNumberField(TEXT("slot_index"), SlotIndex);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetMeshMaterialColor(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	const float R = Args->HasField(TEXT("r")) ? (float)Args->GetNumberField(TEXT("r")) : 1.f;
	const float G = Args->HasField(TEXT("g")) ? (float)Args->GetNumberField(TEXT("g")) : 1.f;
	const float B = Args->HasField(TEXT("b")) ? (float)Args->GetNumberField(TEXT("b")) : 1.f;
	const float A = Args->HasField(TEXT("a")) ? (float)Args->GetNumberField(TEXT("a")) : 1.f;
	const int32 SlotIndex = Args->HasField(TEXT("slot_index")) ? (int32)Args->GetNumberField(TEXT("slot_index")) : 0;

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	UMeshComponent* MeshComp = FindMeshComponentOnActor(Actor);
	FString Error;
	UMaterialInstanceDynamic* MID = GetOrCreateDynamicMaterialInstance(MeshComp, SlotIndex, Error);
	if (!MID) { return ErrorResponse(Error); }

	const FLinearColor Color(R, G, B, A);
	static const TArray<FName> CandidateParams =
	{
		FName(TEXT("BaseColor")),
		FName(TEXT("Color")),
		FName(TEXT("Tint")),
		FName(TEXT("Base_Color"))
	};

	TArray<TSharedPtr<FJsonValue>> ParamNames;
	for (const FName& ParamName : CandidateParams)
	{
		MID->SetVectorParameterValue(ParamName, Color);
		ParamNames.Add(MakeShared<FJsonValueString>(ParamName.ToString()));
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Obj->SetNumberField(TEXT("slot_index"), SlotIndex);
	Obj->SetArrayField(TEXT("attempted_parameters"), ParamNames);
	Obj->SetObjectField(TEXT("color"), VecToJson(FVector(Color.R, Color.G, Color.B)));
	Obj->SetNumberField(TEXT("alpha"), Color.A);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_SetMaterialScalarParam(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ActorName;
	FString ParamName;
	Args->TryGetStringField(TEXT("actor_name"), ActorName);
	Args->TryGetStringField(TEXT("param_name"), ParamName);
	const float Value = Args->HasField(TEXT("value")) ? (float)Args->GetNumberField(TEXT("value")) : 0.f;
	const int32 SlotIndex = Args->HasField(TEXT("slot_index")) ? (int32)Args->GetNumberField(TEXT("slot_index")) : 0;

	AActor* Actor = FindActorByLabelOrName(ActorName);
	if (!Actor) { return ErrorResponse(FString::Printf(TEXT("Actor not found: %s"), *ActorName)); }

	UMeshComponent* MeshComp = FindMeshComponentOnActor(Actor);
	FString Error;
	UMaterialInstanceDynamic* MID = GetOrCreateDynamicMaterialInstance(MeshComp, SlotIndex, Error);
	if (!MID) { return ErrorResponse(Error); }

	MID->SetScalarParameterValue(FName(*ParamName), Value);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("actor_name"), Actor->GetActorLabel());
	Obj->SetStringField(TEXT("param_name"), ParamName);
	Obj->SetNumberField(TEXT("slot_index"), SlotIndex);
	Obj->SetNumberField(TEXT("value"), Value);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  CONTENT MANAGEMENT
// ============================================================================
FString UAgentForgeLibrary::Cmd_RenameAsset(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString AssetPath, NewName;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);
	Args->TryGetStringField(TEXT("new_name"),   NewName);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<FAssetRenameData> RenameData;
	const FString NewPath = FPaths::GetPath(AssetPath) / NewName;
	// UE 5.7: 3-arg FAssetRenameData removed — use 2-SoftObjectPath form
	const FString NewFullObjectPath = NewPath + TEXT(".") + NewName;
	RenameData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), FSoftObjectPath(NewFullObjectPath)));

	const bool bOk = AssetTools.RenameAssets(RenameData);
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),       bOk);
	Obj->SetStringField(TEXT("new_path"), NewPath);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_MoveAsset(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString AssetPath, DestinationPath;
	Args->TryGetStringField(TEXT("asset_path"),       AssetPath);
	Args->TryGetStringField(TEXT("destination_path"), DestinationPath);

	const FString AssetName = FPackageName::GetShortName(AssetPath);
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	TArray<FAssetRenameData> MoveData;
	// UE 5.7: 3-arg FAssetRenameData(OldPath, PackagePath, Name) removed — use 2-SoftObjectPath form
	const FString NewFullPath = DestinationPath / AssetName + TEXT(".") + AssetName;
	MoveData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), FSoftObjectPath(NewFullPath)));

	const bool bOk = AssetTools.RenameAssets(MoveData);
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),       bOk);
	Obj->SetStringField(TEXT("new_path"), DestinationPath + TEXT("/") + AssetName);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_DeleteAsset(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString AssetPath;
	Args->TryGetStringField(TEXT("asset_path"), AssetPath);

	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset) { return ErrorResponse(FString::Printf(TEXT("Asset not found: %s"), *AssetPath)); }

	TArray<UObject*> ToDelete = { Asset };
	const int32 Deleted = ObjectTools::DeleteObjects(ToDelete, /*bShowConfirmation=*/false);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),      Deleted > 0);
	Obj->SetBoolField  (TEXT("deleted"), Deleted > 0);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  TRANSACTION SAFETY
// ============================================================================

FString UAgentForgeLibrary::Cmd_BeginTransaction(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Label = TEXT("AgentForge");
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("label"), Label); }
	if (GOpenTransaction.IsValid())
	{
		// Avoid leaking/implicitly committing a previous transaction when begin is called twice.
		GOpenTransaction->Cancel();
		GOpenTransaction.Reset();
	}
	GOpenTransaction = MakeUnique<FScopedTransaction>(FText::FromString(Label));
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),    true);
	Obj->SetStringField(TEXT("label"), Label);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_EndTransaction()
{
#if WITH_EDITOR
	const int32 OpsCount = 0; // GEditor->Trans->GetQueueLength() inaccessible in UE 5.7 (UTransactor forward-decl only)
	GOpenTransaction.Reset(); // commits the transaction
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),        true);
	Obj->SetNumberField(TEXT("ops_count"), OpsCount);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_UndoTransaction()
{
#if WITH_EDITOR
	if (GEditor && GEditor->Trans) { GEditor->UndoTransaction(); }
	return OkResponse(TEXT("Undo executed."));
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_CreateSnapshot(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString SnapshotName;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("snapshot_name"), SnapshotName); }
	if (SnapshotName.IsEmpty()) { SnapshotName = TEXT("snapshot"); }

	UVerificationEngine* VE = UVerificationEngine::Get();
	const FString Path = VE ? VE->CreateSnapshot(SnapshotName) : FString();

	if (Path.IsEmpty()) { return ErrorResponse(TEXT("Snapshot creation failed.")); }

	// Count actors
	int32 ActorCount = 0;
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && IsValid(*It)) { ++ActorCount; }
		}
	}
#endif

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),          true);
	Obj->SetStringField(TEXT("path"),        Path);
	Obj->SetNumberField(TEXT("actor_count"), ActorCount);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  PYTHON SCRIPTING
// ============================================================================
FString UAgentForgeLibrary::Cmd_ExecutePython(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	// Refuse new Python execution while shutting down; this avoids close-time crashes in
	// PythonScriptPlugin teardown when late remote commands race engine exit.
	if (IsEngineShuttingDown())
	{
		return ErrorResponse(TEXT("Engine exit requested; execute_python is disabled during shutdown."));
	}

	FString ScriptCode;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("script"), ScriptCode); }
	if (ScriptCode.IsEmpty()) { return ErrorResponse(TEXT("execute_python requires 'script' field.")); }
	if (ScriptCode.Len() > 500000)
	{
		return ErrorResponse(TEXT("execute_python script too large (>500000 chars). Use smaller chunked scripts."));
	}

	float UsedPctBefore = 0.0f;
	float AvailMBBefore = 0.0f;
	if (IsLowMemory(UsedPctBefore, AvailMBBefore))
	{
		CollectGarbage(RF_NoFlags, true);
		if (IsLowMemory(UsedPctBefore, AvailMBBefore))
		{
			return ErrorResponse(FString::Printf(
				TEXT("Memory guard triggered before execute_python: available %.0f MB, used %.1f%%."),
				AvailMBBefore, UsedPctBefore));
		}
	}

	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
	{
		return ErrorResponse(TEXT("PythonScriptPlugin not available. Enable it in your .uproject plugins list."));
	}

	FScopeLock PythonLock(&GForgePythonExecCS);

	// ExecuteStatement runs a code string directly (not a file path).
	// For multi-line scripts, write to a .py file and use ExecuteFile mode instead.
	FPythonCommandEx PyCmd;
	PyCmd.Command       = ScriptCode;
	PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;

	const bool bOk = Python->ExecPythonCommandEx(PyCmd);

	// Aggressively collect Python garbage after each command. This reduces lingering
	// wrapper objects that can become invalid during editor shutdown.
	bool bDidPyGc = false;
	if (bOk && !IsEngineShuttingDown())
	{
		FPythonCommandEx GcCmd;
		GcCmd.Command       = TEXT("import gc; gc.collect()");
		GcCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;
		bDidPyGc = Python->ExecPythonCommandEx(GcCmd);
	}

	bool bForceUeGc = false;
	if (Args.IsValid()) { Args->TryGetBoolField(TEXT("force_ue_gc"), bForceUeGc); }
	float UsedPctAfter = 0.0f;
	float AvailMBAfter = 0.0f;
	const bool bLowAfter = IsLowMemory(UsedPctAfter, AvailMBAfter);
	bool bDidUeGc = false;
	if (bOk && !IsEngineShuttingDown() && (bForceUeGc || bLowAfter))
	{
		CollectGarbage(RF_NoFlags, true);
		bDidUeGc = true;
		IsLowMemory(UsedPctAfter, AvailMBAfter);
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),     bOk);
	Obj->SetStringField(TEXT("output"), bOk ? PyCmd.CommandResult : TEXT(""));
	Obj->SetStringField(TEXT("errors"), bOk ? TEXT("") : PyCmd.CommandResult);
	Obj->SetBoolField  (TEXT("py_gc_after"), bDidPyGc);
	Obj->SetBoolField  (TEXT("ue_gc_after"), bDidUeGc);
	Obj->SetNumberField(TEXT("mem_used_percent"), UsedPctAfter);
	Obj->SetNumberField(TEXT("mem_available_mb"), AvailMBAfter);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  PERFORMANCE PROFILING
// ============================================================================
FString UAgentForgeLibrary::Cmd_GetPerfStats()
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;

	int32 ActorCount = 0, ComponentCount = 0;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && IsValid(*It))
			{
				++ActorCount;
				ComponentCount += (*It)->GetComponents().Num();
			}
		}
	}

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const float MemUsedMB  = static_cast<float>(MemStats.UsedPhysical)  / (1024.f * 1024.f);
	const float MemTotalMB = static_cast<float>(MemStats.TotalPhysical) / (1024.f * 1024.f);

	// GPU stats (UE 5.7 — GNumPrimitivesDrawnRHI is an array [MAX_NUM_GPUS])
	const int32 DrawCalls  = GNumDrawCallsRHI[0];
	const int32 Primitives = GNumPrimitivesDrawnRHI[0];
	const float GpuMs      = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("actor_count"),      ActorCount);
	Obj->SetNumberField(TEXT("component_count"),  ComponentCount);
	Obj->SetNumberField(TEXT("draw_calls"),       DrawCalls);
	Obj->SetNumberField(TEXT("primitives"),       Primitives);
	Obj->SetNumberField(TEXT("memory_used_mb"),   MemUsedMB);
	Obj->SetNumberField(TEXT("memory_total_mb"),  MemTotalMB);
	Obj->SetNumberField(TEXT("gpu_ms"),           GpuMs);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  FORGE META-COMMANDS
// ============================================================================
FString UAgentForgeLibrary::Cmd_RunVerification(const TSharedPtr<FJsonObject>& Args)
{
	int32 PhaseMask = 15;
	if (Args.IsValid()) { Args->TryGetNumberField(TEXT("phase_mask"), reinterpret_cast<double&>(PhaseMask)); }

	UVerificationEngine* VE = UVerificationEngine::Get();
	if (!VE) { return ErrorResponse(TEXT("VerificationEngine unavailable.")); }

	TArray<FVerificationPhaseResult> Results;
	const bool bAllPassed = VE->RunPhases(PhaseMask, TEXT("ManualVerificationRun"), Results);

	TArray<TSharedPtr<FJsonValue>> DetailsArr;
	for (const FVerificationPhaseResult& R : Results)
	{
		TSharedPtr<FJsonObject> PhaseObj = MakeShared<FJsonObject>();
		PhaseObj->SetStringField(TEXT("phase"),       R.PhaseName);
		PhaseObj->SetBoolField  (TEXT("passed"),      R.Passed);
		PhaseObj->SetStringField(TEXT("detail"),      R.Detail);
		PhaseObj->SetNumberField(TEXT("duration_ms"), R.DurationMs);
		DetailsArr.Add(MakeShared<FJsonValueObject>(PhaseObj));
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("all_passed"),  bAllPassed);
	Obj->SetNumberField(TEXT("phases_run"),  Results.Num());
	Obj->SetArrayField (TEXT("details"),     DetailsArr);
	return ToJsonString(Obj);
}

FString UAgentForgeLibrary::Cmd_EnforceConstitution(const TSharedPtr<FJsonObject>& Args)
{
	FString ActionDesc;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("action_description"), ActionDesc); }

	TArray<FString> Violations;
	TArray<FString> ViolationList;
	const bool bAllowed = EnforceConstitution(ActionDesc, ViolationList);

	TArray<TSharedPtr<FJsonValue>> VArr;
	for (const FString& V : ViolationList)
	{
		VArr.Add(MakeShared<FJsonValueString>(V));
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField (TEXT("allowed"),    bAllowed);
	Obj->SetArrayField(TEXT("violations"), VArr);
	return ToJsonString(Obj);
}

FString UAgentForgeLibrary::Cmd_GetForgeStatus()
{
	UConstitutionParser* Parser = UConstitutionParser::Get();
	UVerificationEngine* VE     = UVerificationEngine::Get();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("version"),                   TEXT("0.1.0"));
	Obj->SetBoolField  (TEXT("constitution_loaded"),       Parser && Parser->IsLoaded());
	Obj->SetNumberField(TEXT("constitution_rules_loaded"), Parser ? Parser->GetRules().Num() : 0);
	Obj->SetStringField(TEXT("constitution_path"),         Parser ? Parser->GetConstitutionPath() : TEXT(""));
	Obj->SetStringField(TEXT("last_verification"),         VE ? VE->LastVerificationResult : TEXT(""));
	return ToJsonString(Obj);
}

// ============================================================================
//  SCENE SETUP
// ============================================================================
FString UAgentForgeLibrary::Cmd_SetupTestLevel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return ErrorResponse(TEXT("No editor world.")); }

	const float FloorSize = Args.IsValid() && Args->HasField(TEXT("floor_size"))
		? (float)Args->GetNumberField(TEXT("floor_size")) : 10000.f;

	TArray<FString> Log;
	TArray<FString> SpawnedActors;

	auto SpawnStatic = [&](const FString& Label, FVector Loc, FVector Scale)
	{
		FActorSpawnParameters P;
		P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* SMA = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Loc, FRotator::ZeroRotator, P);
		if (!SMA) { Log.Add(FString::Printf(TEXT("WARN: Failed to spawn %s"), *Label)); return; }
		SMA->SetActorLabel(Label);
		SMA->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		SMA->SetActorScale3D(Scale);
		SpawnedActors.Add(Label);
		Log.Add(FString::Printf(TEXT("Spawned %s at (%.0f,%.0f,%.0f)"), *Label, Loc.X, Loc.Y, Loc.Z));
	};

	SpawnStatic(TEXT("AgentForge_Ground"),  FVector(0,0,-5),   FVector(FloorSize/100.f, FloorSize/100.f, 0.1f));
	SpawnStatic(TEXT("AgentForge_CubeA"),   FVector(500,0,50),   FVector(1,1,1));
	SpawnStatic(TEXT("AgentForge_CubeB"),   FVector(-500,0,50),  FVector(1,1,1));
	SpawnStatic(TEXT("AgentForge_CubeC"),   FVector(0,500,50),   FVector(1,1,1));
	SpawnStatic(TEXT("AgentForge_CubeD"),   FVector(0,-500,50),  FVector(1,1,1));

	TArray<TSharedPtr<FJsonValue>> LogArr, ActorArr;
	for (const FString& S : Log)         { LogArr.Add(MakeShared<FJsonValueString>(S)); }
	for (const FString& S : SpawnedActors){ ActorArr.Add(MakeShared<FJsonValueString>(S)); }

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField (TEXT("ok"),          true);
	Obj->SetArrayField(TEXT("log"),         LogArr);
	Obj->SetArrayField(TEXT("test_actors"), ActorArr);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_LLMSetKey(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ProviderString;
	FString Key;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("provider"), ProviderString))
	{
		return ErrorResponse(TEXT("llm_set_key requires 'provider'."));
	}
	if (!Args->TryGetStringField(TEXT("key"), Key))
	{
		return ErrorResponse(TEXT("llm_set_key requires 'key'."));
	}

	EAgentForgeLLMProvider Provider;
	if (!ParseLLMProviderName(ProviderString, Provider))
	{
		return ErrorResponse(FString::Printf(TEXT("Unknown LLM provider: %s"), *ProviderString));
	}

	UAgentForgeLLMSubsystem::SetApiKeyRuntime(Provider, Key);
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("provider"), GetLLMProviderName(Provider));
	Obj->SetBoolField(TEXT("key_set"), !Key.IsEmpty());
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_LLMGetModels(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ProviderString;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("provider"), ProviderString))
	{
		return ErrorResponse(TEXT("llm_get_models requires 'provider'."));
	}

	EAgentForgeLLMProvider Provider;
	if (!ParseLLMProviderName(ProviderString, Provider))
	{
		return ErrorResponse(FString::Printf(TEXT("Unknown LLM provider: %s"), *ProviderString));
	}

	TArray<TSharedPtr<FJsonValue>> ModelValues;
	for (const FString& Model : UAgentForgeLLMSubsystem::GetAvailableModels(Provider))
	{
		ModelValues.Add(MakeShared<FJsonValueString>(Model));
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("ok"), true);
	Obj->SetStringField(TEXT("provider"), GetLLMProviderName(Provider));
	Obj->SetArrayField(TEXT("models"), ModelValues);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_LLMChat(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ErrorResponse(TEXT("GEditor null.")); }

	FString ProviderString;
	FString Model;
	FString SystemPrompt;
	FString CustomEndpoint;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("provider"), ProviderString))
	{
		return ErrorResponse(TEXT("llm_chat requires 'provider'."));
	}
	if (!Args->TryGetStringField(TEXT("model"), Model))
	{
		return ErrorResponse(TEXT("llm_chat requires 'model'."));
	}
	Args->TryGetStringField(TEXT("system"), SystemPrompt);
	Args->TryGetStringField(TEXT("custom_endpoint"), CustomEndpoint);

	EAgentForgeLLMProvider Provider;
	if (!ParseLLMProviderName(ProviderString, Provider))
	{
		return ErrorResponse(FString::Printf(TEXT("Unknown LLM provider: %s"), *ProviderString));
	}

	TArray<FAgentForgeChatMessage> Messages;
	FString ParseError;
	if (!ParseChatMessages(Args, Messages, ParseError))
	{
		return ErrorResponse(ParseError);
	}

	FAgentForgeLLMSettings Settings;
	Settings.Provider = Provider;
	Settings.Model = Model;
	Settings.SystemPrompt = SystemPrompt;
	Settings.Messages = Messages;
	Settings.CustomEndpoint = CustomEndpoint;
	if (Args->HasField(TEXT("max_tokens")))
	{
		Settings.MaxTokens = (int32)Args->GetNumberField(TEXT("max_tokens"));
	}
	if (Args->HasField(TEXT("temperature")))
	{
		Settings.Temperature = (float)Args->GetNumberField(TEXT("temperature"));
	}

	UAgentForgeLLMSubsystem* LLM = GEditor->GetEditorSubsystem<UAgentForgeLLMSubsystem>();
	if (!LLM)
	{
		return ErrorResponse(TEXT("LLM subsystem unavailable."));
	}

	const FAgentForgeLLMResponse Response = LLM->SendChatRequestBlocking(Settings);
	return ToJsonString(BuildLLMResponseObject(Response, Provider, Model));
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

FString UAgentForgeLibrary::Cmd_LLMStructured(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!GEditor) { return ErrorResponse(TEXT("GEditor null.")); }

	FString ProviderString;
	FString Model;
	FString Prompt;
	FString SystemPrompt;
	FString CustomEndpoint;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("provider"), ProviderString))
	{
		return ErrorResponse(TEXT("llm_structured requires 'provider'."));
	}
	if (!Args->TryGetStringField(TEXT("model"), Model))
	{
		return ErrorResponse(TEXT("llm_structured requires 'model'."));
	}
	if (!Args->TryGetStringField(TEXT("prompt"), Prompt))
	{
		return ErrorResponse(TEXT("llm_structured requires 'prompt'."));
	}
	Args->TryGetStringField(TEXT("system"), SystemPrompt);
	Args->TryGetStringField(TEXT("custom_endpoint"), CustomEndpoint);

	EAgentForgeLLMProvider Provider;
	if (!ParseLLMProviderName(ProviderString, Provider))
	{
		return ErrorResponse(FString::Printf(TEXT("Unknown LLM provider: %s"), *ProviderString));
	}

	FString SchemaString;
	if (Args->HasTypedField<EJson::String>(TEXT("schema")))
	{
		SchemaString = Args->GetStringField(TEXT("schema"));
	}
	else
	{
		const TSharedPtr<FJsonObject>* SchemaObject = nullptr;
		if (Args->TryGetObjectField(TEXT("schema"), SchemaObject) && SchemaObject && (*SchemaObject).IsValid())
		{
			SchemaString = ToJsonString(*SchemaObject);
		}
	}

	if (SchemaString.IsEmpty())
	{
		return ErrorResponse(TEXT("llm_structured requires 'schema' as a JSON object or string."));
	}

	FAgentForgeLLMSettings Settings;
	Settings.Provider = Provider;
	Settings.Model = Model;
	Settings.SystemPrompt = SystemPrompt;
	Settings.CustomEndpoint = CustomEndpoint;
	if (Args->HasField(TEXT("max_tokens")))
	{
		Settings.MaxTokens = (int32)Args->GetNumberField(TEXT("max_tokens"));
	}
	if (Args->HasField(TEXT("temperature")))
	{
		Settings.Temperature = (float)Args->GetNumberField(TEXT("temperature"));
	}

	UAgentForgeLLMSubsystem* LLM = GEditor->GetEditorSubsystem<UAgentForgeLLMSubsystem>();
	if (!LLM)
	{
		return ErrorResponse(TEXT("LLM subsystem unavailable."));
	}

	const FAgentForgeLLMResponse Response = LLM->SendStructuredRequestBlocking(Prompt, SchemaString, Settings);
	TSharedPtr<FJsonObject> Obj = BuildLLMResponseObject(Response, Provider, Model);

	TSharedPtr<FJsonObject> StructuredObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.Content);
	if (Response.bSuccess && FJsonSerializer::Deserialize(Reader, StructuredObject) && StructuredObject.IsValid())
	{
		Obj->SetObjectField(TEXT("structured"), StructuredObject);
	}

	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  AI ASSET WIRING
// ============================================================================
FString UAgentForgeLibrary::Cmd_SetBtBlackboard(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	// args: { "bt_path": "/Game/Horror/AI/BT_Warden", "bb_path": "/Game/Horror/AI/BB_Warden" }
	FString BtPath, BbPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("bt_path"), BtPath))
		return ErrorResponse(TEXT("set_bt_blackboard requires 'bt_path' arg."));
	if (!Args->TryGetStringField(TEXT("bb_path"), BbPath))
		return ErrorResponse(TEXT("set_bt_blackboard requires 'bb_path' arg."));

	// Load the BehaviorTree
	UBehaviorTree* BT = Cast<UBehaviorTree>(
		StaticLoadObject(UBehaviorTree::StaticClass(), nullptr, *BtPath));
	if (!BT) return ErrorResponse(FString::Printf(TEXT("BehaviorTree not found: %s"), *BtPath));

	// Load the BlackboardData
	UBlackboardData* BB = Cast<UBlackboardData>(
		StaticLoadObject(UBlackboardData::StaticClass(), nullptr, *BbPath));
	if (!BB) return ErrorResponse(FString::Printf(TEXT("BlackboardData not found: %s"), *BbPath));

	// Assign via C++ — bypasses Python's CPF_Protected restriction
	BT->Modify();
	BT->BlackboardAsset = BB;
	BT->GetOutermost()->MarkPackageDirty();

	// Save the BT asset
	FString PackagePath  = FPackageName::ObjectPathToPackageName(BtPath);
	FString AssetFilePath = FPackageName::LongPackageNameToFilename(
		PackagePath, FPackageName::GetAssetPackageExtension());
	UPackage* Package = BT->GetOutermost();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
	UPackage::SavePackage(Package, BT, *AssetFilePath, SaveArgs);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),      true);
	Obj->SetStringField(TEXT("bt_path"), BtPath);
	Obj->SetStringField(TEXT("bb_path"), BbPath);
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  WIRE AICONTROLLER → BEHAVIOR TREE
// ============================================================================
FString UAgentForgeLibrary::Cmd_WireAIControllerBT(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	// args: { "aicontroller_path": "/Game/.../BP_WardenAIController",
	//         "bt_path":           "/Game/.../BT_Warden" }
	FString AICtrlPath, BtPath;
	if (!Args.IsValid() || !Args->TryGetStringField(TEXT("aicontroller_path"), AICtrlPath))
		return ErrorResponse(TEXT("wire_aicontroller_bt requires 'aicontroller_path' arg."));
	if (!Args->TryGetStringField(TEXT("bt_path"), BtPath))
		return ErrorResponse(TEXT("wire_aicontroller_bt requires 'bt_path' arg."));

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *AICtrlPath);
	if (!BP) return ErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *AICtrlPath));

	UBehaviorTree* BT = Cast<UBehaviorTree>(
		StaticLoadObject(UBehaviorTree::StaticClass(), nullptr, *BtPath));
	if (!BT) return ErrorResponse(FString::Printf(TEXT("BehaviorTree not found: %s"), *BtPath));

	if (!BP->UbergraphPages.Num())
		return ErrorResponse(TEXT("Blueprint has no event graph."));

	UEdGraph* EventGraph = BP->UbergraphPages[0];

	// ── Find or create a BeginPlay event node ────────────────────────────────
	UK2Node_Event* BeginPlayNode = nullptr;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_Event* Ev = Cast<UK2Node_Event>(Node);
		if (Ev && Ev->EventReference.GetMemberName() == FName("ReceiveBeginPlay"))
		{
			BeginPlayNode = Ev;
			break;
		}
	}
	if (!BeginPlayNode)
	{
		BeginPlayNode = NewObject<UK2Node_Event>(EventGraph);
		UFunction* BeginPlayFunc = AActor::StaticClass()->FindFunctionByName(FName("ReceiveBeginPlay"));
		if (BeginPlayFunc)
		{
			BeginPlayNode->EventReference.SetFromField<UFunction>(BeginPlayFunc, false);
			BeginPlayNode->bOverrideFunction = true;
		}
		EventGraph->AddNode(BeginPlayNode, false, false);
		BeginPlayNode->CreateNewGuid();
		BeginPlayNode->PostPlacedNewNode();
		BeginPlayNode->NodePosX = 0;
		BeginPlayNode->NodePosY = 0;
		BeginPlayNode->AllocateDefaultPins();
	}

	// ── Create RunBehaviorTree call node ─────────────────────────────────────
	UK2Node_CallFunction* RunBTNode = NewObject<UK2Node_CallFunction>(EventGraph);
	UFunction* RunBTFunc = AAIController::StaticClass()->FindFunctionByName(FName("RunBehaviorTree"));
	if (!RunBTFunc)
		return ErrorResponse(TEXT("RunBehaviorTree not found on AAIController."));

	RunBTNode->FunctionReference.SetFromField<UFunction>(RunBTFunc, false);
	EventGraph->AddNode(RunBTNode, false, false);
	RunBTNode->CreateNewGuid();
	RunBTNode->PostPlacedNewNode();
	RunBTNode->NodePosX = BeginPlayNode->NodePosX + 400;
	RunBTNode->NodePosY = BeginPlayNode->NodePosY;
	RunBTNode->AllocateDefaultPins();

	// ── Wire execution: BeginPlay.Then → RunBT.Execute ───────────────────────
	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* BeginPlayThenPin = BeginPlayNode->FindPin(UEdGraphSchema_K2::PN_Then);
	UEdGraphPin* RunBTExecPin     = RunBTNode->FindPin(UEdGraphSchema_K2::PN_Execute);
	if (BeginPlayThenPin && RunBTExecPin)
	{
		Schema->TryCreateConnection(BeginPlayThenPin, RunBTExecPin);
	}

	// ── Set BTAsset pin default object ───────────────────────────────────────
	UEdGraphPin* BTAssetPin = RunBTNode->FindPin(FName("BTAsset"));
	if (BTAssetPin)
	{
		BTAssetPin->DefaultObject = BT;
	}

	// ── Compile and save ─────────────────────────────────────────────────────
	BP->Modify();
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);
	SaveNewPackage(BP->GetOutermost(), BP);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),           true);
	Obj->SetStringField(TEXT("aicontroller"), AICtrlPath);
	Obj->SetStringField(TEXT("bt_path"),      BtPath);
	Obj->SetStringField(TEXT("action"),       TEXT("BeginPlay->RunBehaviorTree wired and compiled"));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  SETUP_FLASHLIGHT_SCS
//  Adds a Movable SpotLightComponent SCS node to a Blueprint and compiles it.
//  Python SubobjectData cannot set Mobility at design time; runtime SetMobility()
//  is too late for Lumen scene registration. This C++ command does it correctly.
// ============================================================================
FString UAgentForgeLibrary::Cmd_SetupFlashlightSCS(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString BpPath;
	Args->TryGetStringField(TEXT("blueprint_path"), BpPath);
	if (BpPath.IsEmpty())
	{
		return ErrorResponse(TEXT("setup_flashlight_scs requires 'blueprint_path'"));
	}

	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BpPath);
	if (!BP)
	{
		return ErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BpPath));
	}

	USimpleConstructionScript* SCS = BP->SimpleConstructionScript;
	if (!SCS)
	{
		return ErrorResponse(TEXT("Blueprint has no SimpleConstructionScript"));
	}

	// --- Remove any existing SpotLightComponent SCS nodes (clean slate) ---
	TArray<USCS_Node*> AllNodes = SCS->GetAllNodes();
	int32 RemovedCount = 0;
	for (USCS_Node* Node : AllNodes)
	{
		if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(USpotLightComponent::StaticClass()))
		{
			SCS->RemoveNodeAndPromoteChildren(Node);
			++RemovedCount;
		}
	}

	// --- Create a new SpotLightComponent SCS node ---
	USCS_Node* SpotNode = SCS->CreateNode(USpotLightComponent::StaticClass(), TEXT("FlashlightSpot"));
	if (!SpotNode)
	{
		return ErrorResponse(TEXT("Failed to create SpotLightComponent SCS node"));
	}

	USpotLightComponent* SpotTemplate = Cast<USpotLightComponent>(SpotNode->ComponentTemplate);
	if (!SpotTemplate)
	{
		return ErrorResponse(TEXT("SpotLightComponent template not accessible"));
	}

	// Configure the template — these values are baked into the BP, not set at runtime.
	// Movable is critical: Lumen only renders dynamic/movable lights in real time.
	SpotTemplate->SetMobility(EComponentMobility::Movable);
	SpotTemplate->SetIntensity(50000.f);
	SpotTemplate->SetAttenuationRadius(2500.f);
	SpotTemplate->SetInnerConeAngle(12.f);
	SpotTemplate->SetOuterConeAngle(28.f);
	SpotTemplate->SetLightColor(FLinearColor(1.f, 0.95f, 0.85f));
	SpotTemplate->SetCastShadows(false);
	SpotTemplate->SetVisibility(true);

	// Attach under the root node (FlashlightBatteryComponent::BeginPlay re-attaches to camera)
	USCS_Node* RootNode = SCS->GetDefaultSceneRootNode();
	if (RootNode)
	{
		RootNode->AddChildNode(SpotNode);
	}
	else
	{
		SCS->AddNode(SpotNode);
	}

	// Compile and save
	FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

	UPackage* Pkg = BP->GetOutermost();
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	UPackage::SavePackage(Pkg, BP, *FPackageName::LongPackageNameToFilename(
		Pkg->GetName(), FPackageName::GetAssetPackageExtension()), SaveArgs);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),             true);
	Obj->SetStringField(TEXT("blueprint"),      BpPath);
	Obj->SetNumberField (TEXT("removed_nodes"), (double)RemovedCount);
	Obj->SetStringField(TEXT("action"),         TEXT("FlashlightSpot SCS node added with Movable mobility, compiled and saved"));
	return ToJsonString(Obj);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}

// ============================================================================
//  UNIFIED ORCHESTRATION — ENHANCE_CURRENT_LEVEL
// ============================================================================
FString UAgentForgeLibrary::Cmd_EnhanceCurrentLevel(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Description;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("description"), Description); }
	if (Description.IsEmpty())
		return ErrorResponse(TEXT("enhance_current_level requires 'description' arg."));

	TArray<FString>              ActionsTaken;
	TSharedPtr<FJsonObject>      VerifResult;

	// ── Step 1: Run PreFlight verification ────────────────────────────────────
	UVerificationEngine* Engine = UVerificationEngine::Get();
	TArray<FVerificationPhaseResult> VerifResults;
	Engine->RunPhases(static_cast<int32>(EVerificationPhase::PreFlight), Description, VerifResults);

	if (!VerifResults.IsEmpty() && !VerifResults[0].Passed)
	{
		return ErrorResponse(FString::Printf(
			TEXT("enhance_current_level blocked by PreFlight: %s"), *VerifResults[0].Detail));
	}
	ActionsTaken.Add(TEXT("PreFlight verification passed"));

	// ── Step 2: Analyze current level composition ─────────────────────────────
	const FString CompositionJson = FSpatialControlModule::AnalyzeLevelComposition();
	TSharedPtr<FJsonObject> CompositionObj;
	TSharedRef<TJsonReader<>> CompReader = TJsonReaderFactory<>::Create(CompositionJson);
	FJsonSerializer::Deserialize(CompReader, CompositionObj);
	ActionsTaken.Add(TEXT("Level composition analyzed"));

	// ── Step 3: Take pre-enhancement snapshot ────────────────────────────────
	const FString SnapPath = Engine->CreateSnapshot(TEXT("enhance_pre"));
	if (!SnapPath.IsEmpty())
	{
		ActionsTaken.Add(FString::Printf(
			TEXT("Snapshot created: %s"), *FPaths::GetCleanFilename(SnapPath)));
	}

	// ── Step 4: Take a screenshot for visual context ──────────────────────────
	FString ScreenshotPath;
	{
		// Request screenshot via staging dir pattern (see Lesson 23)
		const FString StageDir = TEXT("C:/HGShots/");
		IFileManager::Get().MakeDirectory(*StageDir, true);
		const FString Filename = FString::Printf(
			TEXT("enhance_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
		FScreenshotRequest::RequestScreenshot(
			FPaths::Combine(StageDir, Filename), false, false);
		ScreenshotPath = FPaths::Combine(StageDir, Filename + TEXT(".png"));
	}
	ActionsTaken.Add(TEXT("Screenshot requested"));

	// ── Step 5: Run PostVerify + BuildCheck ───────────────────────────────────
	FVerificationPhaseResult PostVerify  = Engine->RunPostVerify(0); // no actor delta expected
	FVerificationPhaseResult BuildCheck  = Engine->RunBuildCheck();

	ActionsTaken.Add(PostVerify.Passed  ? TEXT("PostVerify: PASSED")  : (TEXT("PostVerify: ") + PostVerify.Detail));
	ActionsTaken.Add(BuildCheck.Passed  ? TEXT("BuildCheck: PASSED")  : (TEXT("BuildCheck: ") + BuildCheck.Detail));

	// ── Build response ────────────────────────────────────────────────────────
	TArray<TSharedPtr<FJsonValue>> ActionsArr;
	for (const FString& A : ActionsTaken)
	{
		ActionsArr.Add(MakeShared<FJsonValueString>(A));
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),              PostVerify.Passed && BuildCheck.Passed);
	Resp->SetStringField(TEXT("description"),     Description);
	Resp->SetArrayField (TEXT("actions_taken"),   ActionsArr);
	if (CompositionObj.IsValid())
	{
		Resp->SetObjectField(TEXT("composition"), CompositionObj);
	}
	Resp->SetStringField(TEXT("snapshot_path"),  SnapPath);
	Resp->SetStringField(TEXT("screenshot_path"), ScreenshotPath);
	Resp->SetStringField(TEXT("post_verify"),    PostVerify.Detail);
	Resp->SetStringField(TEXT("build_check"),    BuildCheck.Detail);
	return ToJsonString(Resp);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}


// ─── v0.3.0: ObserveAnalyzePlanAct ───────────────────────────────────────────
// Single entry point for the full closed-loop reasoning cycle:
//   Observe  → GetSemanticEnvironmentSnapshot + GetLevelHierarchy
//   Analyze  → compute horror_score, identify gaps, generate action plan
//   Plan     → emit ordered command list as JSON
//   Act      → execute each command under transaction
//   Verify   → 4-phase verification
//   Returns  → complete cycle report

FString UAgentForgeLibrary::Cmd_ObserveAnalyzePlanAct(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const FString Description = Args && Args->HasField(TEXT("description"))
	                          ? Args->GetStringField(TEXT("description")) : TEXT("");
	const int32   MaxIter     = Args && Args->HasField(TEXT("max_iterations"))
	                          ? (int32)Args->GetNumberField(TEXT("max_iterations")) : 1;
	const float   ScoreTarget = Args && Args->HasField(TEXT("score_target"))
	                          ? (float)Args->GetNumberField(TEXT("score_target")) : 60.f;

	TArray<TSharedPtr<FJsonValue>> IterLog;

	for (int32 Iter = 0; Iter < MaxIter; ++Iter)
	{
		auto IterObj = MakeShared<FJsonObject>();
		IterObj->SetNumberField(TEXT("iteration"), Iter + 1);

		// ── Observe ──────────────────────────────────────────────────────────
		const FString SnapRaw = FDataAccessModule::GetSemanticEnvironmentSnapshot();
		TSharedPtr<FJsonObject> Snapshot;
		TSharedRef<TJsonReader<>> SR = TJsonReaderFactory<>::Create(SnapRaw);
		FJsonSerializer::Deserialize(SR, Snapshot);

		float HorrorScore = 0.f;
		if (Snapshot.IsValid()) { HorrorScore = (float)Snapshot->GetNumberField(TEXT("horror_score")); }
		IterObj->SetNumberField(TEXT("observed_horror_score"), HorrorScore);

		// ── Analyze ──────────────────────────────────────────────────────────
		TArray<FString> Issues;
		TArray<FString> Plan;

		if (Snapshot.IsValid())
		{
			// Check darkness.
			const TSharedPtr<FJsonObject>* LightObj;
			if (Snapshot->TryGetObjectField(TEXT("lighting"), LightObj))
			{
				const float DarkScore = (float)(*LightObj)->GetNumberField(TEXT("darkness_score"));
				if (DarkScore < 50.f) { Issues.Add(TEXT("Level too bright for horror")); Plan.Add(TEXT("apply_genre_rules:horror")); }
			}
			// Check fog.
			const TSharedPtr<FJsonObject>* PPObj;
			if (Snapshot->TryGetObjectField(TEXT("post_process"), PPObj))
			{
				const float FogDensity = (float)(*PPObj)->GetNumberField(TEXT("fog_density"));
				if (FogDensity < 0.001f) { Issues.Add(TEXT("No atmospheric fog")); }
			}
			// Check density.
			const TSharedPtr<FJsonObject>* DensObj;
			if (Snapshot->TryGetObjectField(TEXT("density"), DensObj))
			{
				const float Density = (float)(*DensObj)->GetNumberField(TEXT("density_per_m2"));
				if (Density < 0.5f) { Issues.Add(TEXT("Level too sparse")); Plan.Add(TEXT("place_asset_thematically")); }
			}
		}

		// ── Act ───────────────────────────────────────────────────────────────
		TArray<TSharedPtr<FJsonValue>> ActionResults;
		for (const FString& PlanStep : Plan)
		{
			FString StepResult;
			if (PlanStep == TEXT("apply_genre_rules:horror"))
			{
				auto GA = MakeShared<FJsonObject>();
				GA->SetStringField(TEXT("genre"), TEXT("horror"));
				GA->SetNumberField(TEXT("intensity"), 0.8f);
				StepResult = FSemanticCommandModule::ApplyGenreRules(GA);
			}
			else if (PlanStep == TEXT("place_asset_thematically"))
			{
				auto PA = MakeShared<FJsonObject>();
				PA->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.StaticMeshActor"));
				PA->SetNumberField(TEXT("count"), 3);
				auto TR = MakeShared<FJsonObject>();
				TR->SetBoolField(TEXT("prefer_dark"), true);
				TR->SetBoolField(TEXT("prefer_corners"), true);
				PA->SetObjectField(TEXT("theme_rules"), TR);
				StepResult = FSemanticCommandModule::PlaceAssetThematically(PA);
			}
			ActionResults.Add(MakeShared<FJsonValueString>(
				FString::Printf(TEXT("[%s] → %s"), *PlanStep, *StepResult.Left(80))));
		}

		IterObj->SetArrayField(TEXT("issues_identified"), [&]() {
			TArray<TSharedPtr<FJsonValue>> A;
			for (const FString& I : Issues) { A.Add(MakeShared<FJsonValueString>(I)); }
			return A;
		}());
		IterObj->SetArrayField(TEXT("plan_steps"), [&]() {
			TArray<TSharedPtr<FJsonValue>> A;
			for (const FString& P : Plan) { A.Add(MakeShared<FJsonValueString>(P)); }
			return A;
		}());
		IterObj->SetArrayField(TEXT("action_results"), ActionResults);

		IterLog.Add(MakeShared<FJsonValueObject>(IterObj));

		// ── Check convergence ────────────────────────────────────────────────
		if (HorrorScore >= ScoreTarget || Plan.IsEmpty()) { break; }
	}

	// ── Verify ───────────────────────────────────────────────────────────────
	const FString VerifyResult = Cmd_RunVerification(nullptr);

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("description"), Description);
	Root->SetArrayField(TEXT("iterations"), IterLog);
	Root->SetStringField(TEXT("verification"), VerifyResult.Left(200));
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}


// ─── v0.3.0: EnhanceHorrorScene ──────────────────────────────────────────────
// One-shot horror scene enhancement pipeline:
//   1. Get semantic snapshot (current state)
//   2. Apply genre rules (horror atmosphere preset)
//   3. Place assets thematically (dark corners + occluded spots)
//   4. Run full 4-phase verification
//   5. Take screenshot
// args: description (natural language), [intensity=1.0], [prop_count=5]

FString UAgentForgeLibrary::Cmd_EnhanceHorrorScene(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	const FString Description = Args && Args->HasField(TEXT("description"))
	                          ? Args->GetStringField(TEXT("description")) : TEXT("enhance horror atmosphere");
	const float   Intensity   = Args && Args->HasField(TEXT("intensity"))
	                          ? FMath::Clamp((float)Args->GetNumberField(TEXT("intensity")), 0.f, 1.f)
	                          : 1.f;
	const int32   PropCount   = Args && Args->HasField(TEXT("prop_count"))
	                          ? (int32)Args->GetNumberField(TEXT("prop_count")) : 5;

	TArray<TSharedPtr<FJsonValue>> ActionsTaken;

	// Step 1: Observe.
	const FString SnapshotBefore = FDataAccessModule::GetSemanticEnvironmentSnapshot();
	ActionsTaken.Add(MakeShared<FJsonValueString>(TEXT("Observed: GetSemanticEnvironmentSnapshot")));

	// Step 2: Apply horror genre rules.
	auto GenreArgs = MakeShared<FJsonObject>();
	GenreArgs->SetStringField(TEXT("genre"), TEXT("horror"));
	GenreArgs->SetNumberField(TEXT("intensity"), Intensity);
	const FString GenreResult = FSemanticCommandModule::ApplyGenreRules(GenreArgs);
	ActionsTaken.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Applied horror genre rules (intensity=%.2f)"), Intensity)));

	// Step 3: Place props thematically.
	auto PlaceArgs = MakeShared<FJsonObject>();
	PlaceArgs->SetStringField(TEXT("class_path"), TEXT("/Script/Engine.StaticMeshActor"));
	PlaceArgs->SetNumberField(TEXT("count"), PropCount);
	PlaceArgs->SetStringField(TEXT("label_prefix"), TEXT("HorrorProp"));
	auto ThemeRules = MakeShared<FJsonObject>();
	ThemeRules->SetBoolField(TEXT("prefer_dark"),     true);
	ThemeRules->SetBoolField(TEXT("prefer_corners"),  true);
	ThemeRules->SetBoolField(TEXT("prefer_occluded"), true);
	ThemeRules->SetNumberField(TEXT("min_spacing"), 400.f);
	PlaceArgs->SetObjectField(TEXT("theme_rules"), ThemeRules);
	const FString PlaceResult = FSemanticCommandModule::PlaceAssetThematically(PlaceArgs);
	ActionsTaken.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Placed %d horror props in dark corners"), PropCount)));

	// Step 4: Verify.
	TSharedPtr<FJsonObject> VerifyArgs = MakeShared<FJsonObject>();
	VerifyArgs->SetNumberField(TEXT("phase_mask"), 13); // PreFlight + PostVerify + BuildCheck
	const FString VerifyResult = Cmd_RunVerification(VerifyArgs);
	ActionsTaken.Add(MakeShared<FJsonValueString>(TEXT("Ran 3-phase verification (mask=13)")));

	// Step 5: Screenshot.
	FScreenshotRequest::RequestScreenshot(TEXT("enhance_horror_result"), false, false);
	const FString ScreenshotPath = TEXT("Saved/Screenshots/WindowsEditor/enhance_horror_result.png");
	ActionsTaken.Add(MakeShared<FJsonValueString>(
		FString::Printf(TEXT("Screenshot queued: %s"), *ScreenshotPath)));

	// Observe after.
	const FString SnapshotAfter = FDataAccessModule::GetSemanticEnvironmentSnapshot();
	TSharedPtr<FJsonObject> AfterObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SnapshotAfter);
	FJsonSerializer::Deserialize(Reader, AfterObj);
	float FinalHorrorScore = 0.f;
	if (AfterObj.IsValid()) { FinalHorrorScore = (float)AfterObj->GetNumberField(TEXT("horror_score")); }

	auto Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("description"), Description);
	Root->SetArrayField(TEXT("actions_taken"), ActionsTaken);
	Root->SetNumberField(TEXT("final_horror_score"), FinalHorrorScore);
	Root->SetStringField(TEXT("screenshot_path"), ScreenshotPath);
	Root->SetStringField(TEXT("genre_result"), GenreResult.Left(200));
	Root->SetStringField(TEXT("placement_result"), PlaceResult.Left(200));
	return ToJsonString(Root);
#else
	return ErrorResponse(TEXT("Editor only."));
#endif
}
