// UEAgentForge — VerificationEngine.cpp
// 4-phase safety protocol: PreFlight → Snapshot+Rollback → PostVerify → BuildCheck

#include "VerificationEngine.h"
#include "ConstitutionParser.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EngineUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#endif

UVerificationEngine* UVerificationEngine::Singleton = nullptr;

// ============================================================================
UVerificationEngine* UVerificationEngine::Get()
{
	if (!Singleton || !IsValid(Singleton))
	{
		Singleton = NewObject<UVerificationEngine>(GetTransientPackage(), NAME_None, RF_Standalone);
		Singleton->AddToRoot();
	}
	return Singleton;
}

// ============================================================================
bool UVerificationEngine::RunPhases(int32 PhaseMask, const FString& ActionDesc,
                                    TArray<FVerificationPhaseResult>& OutResults)
{
	OutResults.Empty();
	bool bAllPassed = true;

	if (PhaseMask & static_cast<int32>(EVerificationPhase::PreFlight))
	{
		FVerificationPhaseResult Result = RunPreFlight(ActionDesc);
		OutResults.Add(Result);
		if (!Result.Passed) { bAllPassed = false; }
	}

	// Phase 2 is executed inline with the command via RunSnapshotRollback — not here.
	// Phases 3 and 4 are called after execution.

	return bAllPassed;
}

// ============================================================================
FVerificationPhaseResult UVerificationEngine::RunPreFlight(const FString& ActionDesc)
{
	FVerificationPhaseResult Result;
	Result.PhaseName = TEXT("PreFlight");

	const double StartTime = FPlatformTime::Seconds();

#if WITH_EDITOR
	// 1a. Constitution check
	TArray<FString> Violations;
	UConstitutionParser* Parser = UConstitutionParser::Get();
	if (Parser && Parser->IsLoaded())
	{
		const bool bAllowed = Parser->ValidateAction(ActionDesc, Violations);
		if (!bAllowed)
		{
			Result.Passed = false;
			Result.Detail = FString::Printf(TEXT("Constitution violations: %s"),
			                               *FString::Join(Violations, TEXT("; ")));
			Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
			return Result;
		}
	}

	// 1b. Capture pre-state actor list
	PreStateActorLabels.Empty();
	PreStateActorCount = 0;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && IsValid(Actor))
			{
				PreStateActorLabels.Add(Actor->GetActorLabel());
				++PreStateActorCount;
			}
		}
	}

	Result.Passed = true;
	Result.Detail = FString::Printf(
		TEXT("Pre-state captured: %d actors. Constitution: %d rules checked, 0 violations."),
		PreStateActorCount, Parser ? Parser->GetRules().Num() : 0);
#else
	Result.Passed = true;
	Result.Detail = TEXT("Editor not available — skipped.");
#endif

	Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Result;
}

// ============================================================================
FVerificationPhaseResult UVerificationEngine::RunSnapshotRollback(
	TFunction<bool()> ExecuteCmd, const FString& SnapshotLabel)
{
	FVerificationPhaseResult Result;
	Result.PhaseName = TEXT("Snapshot+Rollback");

	const double StartTime = FPlatformTime::Seconds();

#if WITH_EDITOR
	// Step 1: Create pre-execution snapshot
	const FString SnapPath = CreateSnapshot(SnapshotLabel + TEXT("_pre"));

	// Step 2: Execute the command inside a temporary sub-transaction
	bool bExecuteSuccess = false;
	{
		FScopedTransaction RollbackTest(FText::FromString(TEXT("AgentForge RollbackTest")));
		bExecuteSuccess = ExecuteCmd();
		// Step 3: Intentionally cancel — this is the rollback test
		RollbackTest.Cancel();
	}
	// At this point the undo system has rolled back the sub-transaction.

	// Step 4: Verify state matches pre-snapshot
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	int32 PostRollbackCount = 0;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && IsValid(*It)) { ++PostRollbackCount; }
		}
	}

	const bool bRollbackCorrect = (PostRollbackCount == PreStateActorCount);
	if (!bRollbackCorrect)
	{
		Result.Passed = false;
		Result.Detail = FString::Printf(
			TEXT("Rollback verification FAILED: expected %d actors, got %d after undo."),
			PreStateActorCount, PostRollbackCount);
		Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
		return Result;
	}

	// Step 5: Re-execute for real (caller's responsibility to wrap in real transaction)
	Result.Passed    = true;
	Result.Detail    = FString::Printf(
		TEXT("Rollback verified OK (%d actors restored). Snapshot: %s"),
		PostRollbackCount, *FPaths::GetCleanFilename(SnapPath));
#else
	Result.Passed = true;
	Result.Detail = TEXT("Editor not available — skipped.");
#endif

	Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Result;
}

// ============================================================================
FVerificationPhaseResult UVerificationEngine::RunPostVerify(int32 ExpectedActorDelta)
{
	FVerificationPhaseResult Result;
	Result.PhaseName = TEXT("PostVerify");

	const double StartTime = FPlatformTime::Seconds();

#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	int32 PostActorCount = 0;
	if (World)
	{
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && IsValid(*It)) { ++PostActorCount; }
		}
	}

	const int32 ActualDelta = PostActorCount - PreStateActorCount;
	const bool  bDeltaOk    = (ActualDelta == ExpectedActorDelta);

	Result.Passed = bDeltaOk;
	Result.Detail = FString::Printf(
		TEXT("Actor delta: expected %+d, actual %+d. Post-count: %d."),
		ExpectedActorDelta, ActualDelta, PostActorCount);

	if (!bDeltaOk)
	{
		Result.Detail += TEXT(" [MISMATCH]");
	}
#else
	Result.Passed = true;
	Result.Detail = TEXT("Editor not available — skipped.");
#endif

	Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Result;
}

// ============================================================================
FVerificationPhaseResult UVerificationEngine::RunBuildCheck()
{
	FVerificationPhaseResult Result;
	Result.PhaseName = TEXT("BuildCheck");

	const double StartTime = FPlatformTime::Seconds();

#if WITH_EDITOR
	TArray<FString> Errors;
	int32 BlueprintsChecked = 0;

	// Iterate all loaded Blueprint assets and recompile dirty ones
	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	TArray<FAssetData> BlueprintAssets;
	Registry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssets);

	for (const FAssetData& AssetData : BlueprintAssets)
	{
		UBlueprint* BP = Cast<UBlueprint>(AssetData.GetAsset());
		if (!BP || !BP->bBeingCompiled) { continue; }

		++BlueprintsChecked;
		TArray<FCompilerResultsLog> Results;
		FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::None);

		if (BP->Status == BS_Error)
		{
			Errors.Add(FString::Printf(TEXT("Blueprint compile error: %s"), *AssetData.AssetName.ToString()));
		}
	}

	Result.Passed = Errors.IsEmpty();
	Result.Detail = FString::Printf(
		TEXT("BuildCheck: %d blueprints checked. %s"),
		BlueprintsChecked,
		Errors.IsEmpty()
		    ? TEXT("All clean.")
		    : *FString::Join(Errors, TEXT("; ")));
#else
	Result.Passed = true;
	Result.Detail = TEXT("Editor not available — skipped.");
#endif

	Result.DurationMs = static_cast<float>((FPlatformTime::Seconds() - StartTime) * 1000.0);
	return Result;
}

// ============================================================================
FString UVerificationEngine::CreateSnapshot(const FString& SnapshotName)
{
#if WITH_EDITOR
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) { return FString(); }

	// Build JSON array of actor states
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || !IsValid(Actor)) { continue; }

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("label"),  Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"),  Actor->GetClass()->GetName());
		ActorObj->SetStringField(TEXT("path"),   Actor->GetPathName());

		const FVector  Loc   = Actor->GetActorLocation();
		const FRotator Rot   = Actor->GetActorRotation();
		const FVector  Scale = Actor->GetActorScale3D();

		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Loc.X);
		LocObj->SetNumberField(TEXT("y"), Loc.Y);
		LocObj->SetNumberField(TEXT("z"), Loc.Z);
		ActorObj->SetObjectField(TEXT("location"), LocObj);

		TSharedPtr<FJsonObject> RotObj = MakeShared<FJsonObject>();
		RotObj->SetNumberField(TEXT("pitch"), Rot.Pitch);
		RotObj->SetNumberField(TEXT("yaw"),   Rot.Yaw);
		RotObj->SetNumberField(TEXT("roll"),  Rot.Roll);
		ActorObj->SetObjectField(TEXT("rotation"), RotObj);

		ActorArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("snapshot_name"),  SnapshotName);
	Root->SetStringField(TEXT("timestamp"),      FDateTime::Now().ToString());
	Root->SetNumberField(TEXT("actor_count"),    static_cast<double>(ActorArray.Num()));
	Root->SetArrayField(TEXT("actors"),          ActorArray);

	FString JsonStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
	FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);

	// Save to ProjectSaved/AgentForgeSnapshots/
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("AgentForgeSnapshots"));
	IFileManager::Get().MakeDirectory(*Dir, true);

	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString SafeName  = SnapshotName.IsEmpty() ? TEXT("snapshot") : SnapshotName;
	const FString FilePath  = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.json"), *SafeName, *Timestamp));

	FFileHelper::SaveStringToFile(JsonStr, *FilePath);
	return FilePath;
#else
	return FString();
#endif
}

// ============================================================================
FString UVerificationEngine::DiffSnapshots(const FString& SnapshotPathA, const FString& SnapshotPathB)
{
	auto LoadActorLabels = [](const FString& Path) -> TSet<FString>
	{
		TSet<FString> Labels;
		FString JsonStr;
		if (!FFileHelper::LoadFileToString(JsonStr, *Path)) { return Labels; }

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (!FJsonSerializer::Deserialize(Reader, Root)) { return Labels; }

		const TArray<TSharedPtr<FJsonValue>>* Actors;
		if (!Root->TryGetArrayField(TEXT("actors"), Actors)) { return Labels; }

		for (const TSharedPtr<FJsonValue>& V : *Actors)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (V->TryGetObject(Obj))
			{
				FString Label;
				if ((*Obj)->TryGetStringField(TEXT("label"), Label))
				{
					Labels.Add(Label);
				}
			}
		}
		return Labels;
	};

	const TSet<FString> A = LoadActorLabels(SnapshotPathA);
	const TSet<FString> B = LoadActorLabels(SnapshotPathB);

	TArray<FString> Added   = B.Difference(A).Array();
	TArray<FString> Removed = A.Difference(B).Array();

	if (Added.IsEmpty() && Removed.IsEmpty())
	{
		return TEXT("Snapshots identical.");
	}

	FString Diff;
	if (!Added.IsEmpty())
	{
		Diff += FString::Printf(TEXT("+ Added (%d): %s\n"), Added.Num(), *FString::Join(Added, TEXT(", ")));
	}
	if (!Removed.IsEmpty())
	{
		Diff += FString::Printf(TEXT("- Removed (%d): %s\n"), Removed.Num(), *FString::Join(Removed, TEXT(", ")));
	}
	return Diff.TrimEnd();
}
