// UEAgentForge — AgentForgeLibrary.cpp
// Command router with transaction safety, constitution enforcement, and verification hooks.

#include "AgentForgeLibrary.h"
#include "VerificationEngine.h"
#include "ConstitutionParser.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

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
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
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
// Phase 1: material instancing
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
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
// AI asset wiring — set_bt_blackboard bypasses Python CPF_Protected restriction
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
// wire_aicontroller_bt — blueprint graph node creation
#include "AIController.h"
#include "EdGraphSchema_K2.h"
#endif

// ============================================================================
//  FILE-SCOPE HELPERS
// ============================================================================
#if WITH_EDITOR

// Simple lock: if set, mutating commands are rejected unless the current level matches.
static FString GForgeMapLockPackagePath;

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
		TEXT("create_blueprint"), TEXT("compile_blueprint"), TEXT("set_bp_cdo_property"),
		TEXT("edit_blueprint_node"), TEXT("create_material_instance"), TEXT("set_material_params"),
		TEXT("rename_asset"), TEXT("move_asset"), TEXT("delete_asset"),
		TEXT("setup_test_level"),
		// NOTE: execute_python is NOT here — it routes directly (see ExecuteCommandJson).
	};
	return MutatingCmds.Contains(Cmd);
}

// ============================================================================
//  BLUEPRINT CALLABLE ENTRY POINTS
// ============================================================================
FString UAgentForgeLibrary::ExecuteCommandJson(const FString& RequestJson)
{
#if WITH_EDITOR
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
	if (Cmd == TEXT("set_viewport_camera"))   { return Cmd_SetViewportCamera(Args); }
	if (Cmd == TEXT("redraw_viewports"))      { return Cmd_RedrawViewports(); }
	// wire_aicontroller_bt: creates BeginPlay→RunBehaviorTree in an AIController Blueprint
	if (Cmd == TEXT("wire_aicontroller_bt"))  { return Cmd_WireAIControllerBT(Args); }

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

	// Phase 2: Snapshot + Rollback test — intentionally runs BEFORE opening the real
	// transaction. The rollback test opens and cancels its own inner FScopedTransaction
	// to confirm that undo works for this command type. Only on success do we open
	// the permanent transaction below.
	if (VE)
	{
		FVerificationPhaseResult SnapResult = VE->RunSnapshotRollback(
			[&]() -> bool
			{
				// Executes inside a temporary cancelled sub-transaction (rollback test).
				// Changes are intentionally undone — this is the safety proof.
				FString Dummy;
				if (Cmd == TEXT("spawn_actor"))                  { Dummy = Cmd_SpawnActor(Args); }
				else if (Cmd == TEXT("set_actor_transform"))     { Dummy = Cmd_SetActorTransform(Args); }
				else if (Cmd == TEXT("delete_actor"))            { Dummy = Cmd_DeleteActor(Args); }
				else if (Cmd == TEXT("create_blueprint"))        { Dummy = Cmd_CreateBlueprint(Args); }
				else if (Cmd == TEXT("compile_blueprint"))       { Dummy = Cmd_CompileBlueprint(Args); }
				else if (Cmd == TEXT("set_bp_cdo_property"))     { Dummy = Cmd_SetBlueprintCDOProperty(Args); }
				else if (Cmd == TEXT("edit_blueprint_node"))     { Dummy = Cmd_EditBlueprintNode(Args); }
				else if (Cmd == TEXT("create_material_instance")){ Dummy = Cmd_CreateMaterialInstance(Args); }
				else if (Cmd == TEXT("set_material_params"))     { Dummy = Cmd_SetMaterialParams(Args); }
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

	// Open the REAL transaction — only reached after Phase 2 confirmed rollback works.
	// All operations below are permanently recorded in the undo history.
	FScopedTransaction Transaction(
		FText::FromString(FString::Printf(TEXT("AgentForge: %s"), *Cmd)));

	bool bCommandSuccess = false;

	// Execute for real (the snapshot rollback lambda already ran it inside a cancelled tx;
	// now we execute again inside the real open transaction).
	FString CommandResult;
	if (Cmd == TEXT("spawn_actor"))              { CommandResult = Cmd_SpawnActor(Args); }
	else if (Cmd == TEXT("set_actor_transform")) { CommandResult = Cmd_SetActorTransform(Args); }
	else if (Cmd == TEXT("delete_actor"))        { CommandResult = Cmd_DeleteActor(Args); }
	else if (Cmd == TEXT("create_blueprint"))    { CommandResult = Cmd_CreateBlueprint(Args); }
	else if (Cmd == TEXT("compile_blueprint"))   { CommandResult = Cmd_CompileBlueprint(Args); }
	else if (Cmd == TEXT("set_bp_cdo_property")) { CommandResult = Cmd_SetBlueprintCDOProperty(Args); }
	else if (Cmd == TEXT("edit_blueprint_node")) { CommandResult = Cmd_EditBlueprintNode(Args); }
	else if (Cmd == TEXT("create_material_instance")){ CommandResult = Cmd_CreateMaterialInstance(Args); }
	else if (Cmd == TEXT("set_material_params")) { CommandResult = Cmd_SetMaterialParams(Args); }
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
			Cmd == TEXT("spawn_actor")   ? 1  :
			Cmd == TEXT("delete_actor")  ? -1 : 0;

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

	// FScreenshotRequest::RequestScreenshot — correct programmatic editor screenshot API.
	// Saves on the next rendered frame to the exact path specified (no path-space issues).
	// bAddFilenameSuffix=false so we control the exact filename; bShowUI=false for silent capture.
	// Use C:/HGShots staging dir (no spaces in path — HighResShot historically breaks on spaces).
	const FString Dir = TEXT("C:/HGShots");
	IFileManager::Get().MakeDirectory(*Dir, true);
	const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));
	const FString StagedName = FString::Printf(TEXT("%s_%s.png"), *Filename, *Timestamp);
	const FString Path = FPaths::Combine(Dir, StagedName);

	FScreenshotRequest::RequestScreenshot(Path, /*bShowUI=*/false, /*bAddFilenameSuffix=*/false);

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

	UClass* Parent = LoadObject<UClass>(nullptr, *ParentClass);
	if (!Parent) { return ErrorResponse(FString::Printf(TEXT("Parent class not found: %s"), *ParentClass)); }

	const FString PackageName = FString::Printf(TEXT("%s/%s"), *OutputPath, *Name);
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) { return ErrorResponse(TEXT("Failed to create package.")); }

	UBlueprint* BP = FKismetEditorUtilities::CreateBlueprint(
		Parent, Package, *Name, BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	if (!BP) { return ErrorResponse(TEXT("CreateBlueprint returned null.")); }

	BP->MarkPackageDirty();
	SaveNewPackage(Package, BP);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),                   true);
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
	const FString NewPath = FPaths::GetPath(AssetPath) + TEXT("/") + NewName;
	RenameData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), FPaths::GetPath(AssetPath), NewName));

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
	MoveData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), DestinationPath, AssetName));

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

// Track manually-opened transactions for the begin/end/undo command pair
static TUniquePtr<FScopedTransaction> GOpenTransaction;

FString UAgentForgeLibrary::Cmd_BeginTransaction(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString Label = TEXT("AgentForge");
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("label"), Label); }
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
	const int32 OpsCount = GEditor ? GEditor->Trans->GetQueueLength() : 0;
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
	FString ScriptCode;
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("script"), ScriptCode); }
	if (ScriptCode.IsEmpty()) { return ErrorResponse(TEXT("execute_python requires 'script' field.")); }

	IPythonScriptPlugin* Python = IPythonScriptPlugin::Get();
	if (!Python || !Python->IsPythonAvailable())
	{
		return ErrorResponse(TEXT("PythonScriptPlugin not available. Enable it in your .uproject plugins list."));
	}

	// ExecuteStatement runs a code string directly (not a file path).
	// For multi-line scripts, write to a .py file and use ExecuteFile mode instead.
	FPythonCommandEx PyCmd;
	PyCmd.Command       = ScriptCode;
	PyCmd.ExecutionMode = EPythonCommandExecutionMode::ExecuteStatement;

	const bool bOk = Python->ExecPythonCommandEx(PyCmd);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField  (TEXT("ok"),     bOk);
	Obj->SetStringField(TEXT("output"), bOk ? PyCmd.CommandResult : TEXT(""));
	Obj->SetStringField(TEXT("errors"), bOk ? TEXT("") : PyCmd.CommandResult);
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
