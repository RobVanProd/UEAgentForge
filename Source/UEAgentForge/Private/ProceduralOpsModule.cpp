
// Copyright UEAgentForge Project. All Rights Reserved.
// ProceduralOpsModule.cpp - deterministic operator-centric procedural workflow.

#include "ProceduralOpsModule.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "Math/RandomStream.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

#if WITH_EDITOR
#include "Editor.h"
#include "EngineUtils.h"
#include "Components/ActorComponent.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Interfaces/IPluginManager.h"
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"
#endif

namespace
{
	struct FOperatorPolicyState
	{
		bool bOperatorOnly = true;
		bool bAllowAtomicPlacement = false;
		int32 MaxPoiPerCall = 48;
		int32 MaxActorDeltaPerPipeline = 1200;
		float MaxMemoryUsedMB = 24576.0f;
	};

	static FOperatorPolicyState GOperatorPolicy;

	static FString ToJson(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	static TSharedPtr<FJsonObject> ParseJsonObjectOrNull(const FString& In)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(In);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			return Obj;
		}
		return nullptr;
	}

	static FString ErrorJson(const FString& Msg)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("ok"), false);
		Obj->SetStringField(TEXT("error"), Msg);
		return ToJson(Obj);
	}

	static TSharedPtr<FJsonObject> VecToObj(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}
	static bool JsonValueToNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Number)
		{
			Out = Value->AsNumber();
			return true;
		}
		if (Value->Type == EJson::Boolean)
		{
			Out = Value->AsBool() ? 1.0 : 0.0;
			return true;
		}
		if (Value->Type == EJson::String)
		{
			return LexTryParseString(Out, *Value->AsString());
		}
		return false;
	}

	static bool JsonValueToBool(const TSharedPtr<FJsonValue>& Value, bool& Out)
	{
		if (!Value.IsValid())
		{
			return false;
		}
		if (Value->Type == EJson::Boolean)
		{
			Out = Value->AsBool();
			return true;
		}
		if (Value->Type == EJson::Number)
		{
			Out = (Value->AsNumber() != 0.0);
			return true;
		}
		if (Value->Type == EJson::String)
		{
			const FString S = Value->AsString().ToLower();
			if (S == TEXT("true") || S == TEXT("1") || S == TEXT("yes"))
			{
				Out = true;
				return true;
			}
			if (S == TEXT("false") || S == TEXT("0") || S == TEXT("no"))
			{
				Out = false;
				return true;
			}
		}
		return false;
	}

	static bool JsonValueToVector(const TSharedPtr<FJsonValue>& Value, FVector& Out)
	{
		if (!Value.IsValid() || Value->Type != EJson::Object)
		{
			return false;
		}
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid() || !Obj->HasField(TEXT("x")) || !Obj->HasField(TEXT("y")) || !Obj->HasField(TEXT("z")))
		{
			return false;
		}
		Out.X = (float)Obj->GetNumberField(TEXT("x"));
		Out.Y = (float)Obj->GetNumberField(TEXT("y"));
		Out.Z = (float)Obj->GetNumberField(TEXT("z"));
		return true;
	}

#if WITH_EDITOR
	static UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static int32 CountWorldActors(UWorld* World)
	{
		if (!World)
		{
			return 0;
		}
		int32 Count = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && IsValid(*It))
			{
				++Count;
			}
		}
		return Count;
	}

	static FString FirstNonEmptyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Keys)
	{
		if (!Args.IsValid())
		{
			return FString();
		}
		for (const FString& Key : Keys)
		{
			FString Value;
			if (Args->TryGetStringField(Key, Value) && !Value.IsEmpty())
			{
				return Value;
			}
		}
		return FString();
	}
	static AActor* FindActorByAnyId(UWorld* World, const FString& Id)
	{
		if (!World || Id.IsEmpty())
		{
			return nullptr;
		}

		if (UObject* Obj = StaticFindObject(UObject::StaticClass(), nullptr, *Id))
		{
			if (AActor* AsActor = Cast<AActor>(Obj))
			{
				return AsActor;
			}
		}

		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || !IsValid(Actor))
			{
				continue;
			}
			if (Actor->GetActorLabel().Equals(Id, ESearchCase::IgnoreCase) ||
			    Actor->GetName().Equals(Id, ESearchCase::IgnoreCase) ||
			    Actor->GetPathName().Equals(Id, ESearchCase::IgnoreCase))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	static FProperty* FindPropertyIgnoreCase(UClass* Class, const FString& PropertyName)
	{
		if (!Class)
		{
			return nullptr;
		}
		for (TFieldIterator<FProperty> It(Class); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop)
			{
				continue;
			}
			if (Prop->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				return Prop;
			}
		}
		return nullptr;
	}

	static bool SetPropertyOnObject(UObject* Target, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutPropertyApplied, FString& OutError)
	{
		if (!Target || !Value.IsValid())
		{
			OutError = TEXT("invalid_target_or_value");
			return false;
		}

		FProperty* Prop = FindPropertyIgnoreCase(Target->GetClass(), PropertyName);
		if (!Prop)
		{
			OutError = TEXT("property_not_found");
			return false;
		}

		Target->Modify();

		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			double D = 0.0;
			if (!JsonValueToNumber(Value, D))
			{
				OutError = TEXT("expected_number");
				return false;
			}
			FloatProp->SetPropertyValue_InContainer(Target, (float)D);
			OutPropertyApplied = FloatProp->GetName();
			return true;
		}

		if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			double D = 0.0;
			if (!JsonValueToNumber(Value, D))
			{
				OutError = TEXT("expected_number");
				return false;
			}
			DoubleProp->SetPropertyValue_InContainer(Target, D);
			OutPropertyApplied = DoubleProp->GetName();
			return true;
		}

		if (FIntProperty* IntProp = CastField<FIntProperty>(Prop))
		{
			double D = 0.0;
			if (!JsonValueToNumber(Value, D))
			{
				OutError = TEXT("expected_number");
				return false;
			}
			IntProp->SetPropertyValue_InContainer(Target, (int32)FMath::RoundToInt(D));
			OutPropertyApplied = IntProp->GetName();
			return true;
		}
		if (FInt64Property* Int64Prop = CastField<FInt64Property>(Prop))
		{
			double D = 0.0;
			if (!JsonValueToNumber(Value, D))
			{
				OutError = TEXT("expected_number");
				return false;
			}
			Int64Prop->SetPropertyValue_InContainer(Target, (int64)D);
			OutPropertyApplied = Int64Prop->GetName();
			return true;
		}

		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			bool B = false;
			if (!JsonValueToBool(Value, B))
			{
				OutError = TEXT("expected_bool");
				return false;
			}
			BoolProp->SetPropertyValue_InContainer(Target, B);
			OutPropertyApplied = BoolProp->GetName();
			return true;
		}

		if (FStrProperty* StrProp = CastField<FStrProperty>(Prop))
		{
			const FString S = Value->AsString();
			StrProp->SetPropertyValue_InContainer(Target, S);
			OutPropertyApplied = StrProp->GetName();
			return true;
		}

		if (FNameProperty* NameProp = CastField<FNameProperty>(Prop))
		{
			const FString S = Value->AsString();
			NameProp->SetPropertyValue_InContainer(Target, FName(*S));
			OutPropertyApplied = NameProp->GetName();
			return true;
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct == TBaseStructure<FVector>::Get())
			{
				FVector V = FVector::ZeroVector;
				if (!JsonValueToVector(Value, V))
				{
					OutError = TEXT("expected_vector_object");
					return false;
				}
				if (FVector* Dest = StructProp->ContainerPtrToValuePtr<FVector>(Target))
				{
					*Dest = V;
					OutPropertyApplied = StructProp->GetName();
					return true;
				}
			}
		}

		OutError = TEXT("unsupported_property_type");
		return false;
	}

	static bool ApplySingleParam(AActor* Actor, const FString& Key, const TSharedPtr<FJsonValue>& Value, FString& OutTarget, FString& OutProperty, FString& OutError)
	{
		if (!Actor)
		{
			OutError = TEXT("no_actor");
			return false;
		}

		FString ComponentHint;
		FString PropertyName = Key;
		if (Key.Contains(TEXT(".")))
		{
			if (!Key.Split(TEXT("."), &ComponentHint, &PropertyName))
			{
				ComponentHint.Empty();
				PropertyName = Key;
			}
		}

		if (ComponentHint.IsEmpty() || ComponentHint.Equals(TEXT("Actor"), ESearchCase::IgnoreCase))
		{
			FString AppliedName;
			FString Err;
			if (SetPropertyOnObject(Actor, PropertyName, Value, AppliedName, Err))
			{
				OutTarget = FString::Printf(TEXT("actor:%s"), *Actor->GetActorLabel());
				OutProperty = AppliedName;
				return true;
			}
		}
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (!ComponentHint.IsEmpty())
			{
				const bool bHintMatch =
					Component->GetName().Contains(ComponentHint, ESearchCase::IgnoreCase) ||
					Component->GetClass()->GetName().Contains(ComponentHint, ESearchCase::IgnoreCase);
				if (!bHintMatch)
				{
					continue;
				}
			}

			FString AppliedName;
			FString Err;
			if (SetPropertyOnObject(Component, PropertyName, Value, AppliedName, Err))
			{
				OutTarget = FString::Printf(TEXT("component:%s"), *Component->GetName());
				OutProperty = AppliedName;
				return true;
			}
		}

		OutError = TEXT("property_not_found_on_actor_or_components");
		return false;
	}

	static int32 TriggerProceduralGenerate(AActor* Actor)
	{
		if (!Actor)
		{
			return 0;
		}

		int32 Triggered = 0;
		TArray<UActorComponent*> Components;
		Actor->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (!Component)
			{
				continue;
			}

			if (!Component->GetClass()->GetName().Contains(TEXT("PCG"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			UFunction* Fn = Component->FindFunction(TEXT("Generate"));
			if (!Fn)
			{
				Fn = Component->FindFunction(TEXT("GenerateLocal"));
			}
			if (!Fn)
			{
				Fn = Component->FindFunction(TEXT("Refresh"));
			}

			if (Fn && Fn->NumParms == 0)
			{
				Component->ProcessEvent(Fn, nullptr);
				++Triggered;
			}
		}

		return Triggered;
	}

	static bool ProjectPointToSurface(UWorld* World, FVector& InOutLocation, FVector* OutNormal = nullptr)
	{
		if (!World)
		{
			return false;
		}

		FCollisionQueryParams Params(NAME_None, true);
		FHitResult Hit;
		const FVector Start = InOutLocation + FVector(0.0f, 0.0f, 500.0f);
		const FVector End = InOutLocation - FVector(0.0f, 0.0f, 5000.0f);
		const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params);
		if (!bHit)
		{
			return false;
		}

		InOutLocation = Hit.ImpactPoint;
		if (OutNormal)
		{
			*OutNormal = Hit.ImpactNormal;
		}
		return true;
	}

	static int32 ApplySplinePoints(AActor* Actor, const TArray<TSharedPtr<FJsonValue>>& Points, bool bClosedLoop)
	{
		if (!Actor)
		{
			return 0;
		}
		USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
		if (!Spline)
		{
			return 0;
		}
		Spline->Modify();
		Spline->ClearSplinePoints(false);

		int32 Added = 0;
		for (const TSharedPtr<FJsonValue>& PointValue : Points)
		{
			FVector Location = FVector::ZeroVector;
			if (!JsonValueToVector(PointValue, Location))
			{
				continue;
			}
			Spline->AddSplinePoint(Location, ESplineCoordinateSpace::World, false);
			++Added;
		}

		Spline->SetClosedLoop(bClosedLoop, false);
		Spline->UpdateSpline();
		return Added;
	}

	static UClass* LoadActorClass(const FString& ClassPath)
	{
		if (ClassPath.IsEmpty())
		{
			return nullptr;
		}

		UClass* ClassObj = LoadObject<UClass>(nullptr, *ClassPath);
		if (!ClassObj)
		{
			ClassObj = LoadObject<UClass>(nullptr, *(ClassPath + TEXT("_C")));
		}
		return ClassObj;
	}

	static void GatherParameterObject(const TSharedPtr<FJsonObject>& Args, TMap<FString, TSharedPtr<FJsonValue>>& OutParams, const TSet<FString>& ReservedKeys)
	{
		OutParams.Empty();
		if (!Args.IsValid())
		{
			return;
		}

		const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
		if (Args->TryGetObjectField(TEXT("parameters"), ParamsObj) && ParamsObj && ParamsObj->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ParamsObj)->Values)
			{
				OutParams.Add(Pair.Key, Pair.Value);
			}
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Args->Values)
		{
			const FString LowerKey = Pair.Key.ToLower();
			if (ReservedKeys.Contains(LowerKey))
			{
				continue;
			}
			if (!Pair.Value.IsValid())
			{
				continue;
			}
			if (Pair.Value->Type == EJson::Number || Pair.Value->Type == EJson::Boolean || Pair.Value->Type == EJson::String)
			{
				OutParams.Add(Pair.Key, Pair.Value);
			}
		}
	}

	static void AddOperatorTag(AActor* Actor, const FName& Tag)
	{
		if (!Actor)
		{
			return;
		}
		if (!Actor->Tags.Contains(Tag))
		{
			Actor->Modify();
			Actor->Tags.Add(Tag);
		}
	}
#endif // WITH_EDITOR
}

FString FProceduralOpsModule::GetOperatorPolicy()
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetBoolField(TEXT("operator_only"), GOperatorPolicy.bOperatorOnly);
	Root->SetBoolField(TEXT("allow_atomic_placement"), GOperatorPolicy.bAllowAtomicPlacement);
	Root->SetNumberField(TEXT("max_poi_per_call"), GOperatorPolicy.MaxPoiPerCall);
	Root->SetNumberField(TEXT("max_actor_delta_per_pipeline"), GOperatorPolicy.MaxActorDeltaPerPipeline);
	Root->SetNumberField(TEXT("max_memory_used_mb"), GOperatorPolicy.MaxMemoryUsedMB);
	return ToJson(Root);
}

FString FProceduralOpsModule::SetOperatorPolicy(const TSharedPtr<FJsonObject>& Args)
{
	if (Args.IsValid())
	{
		if (Args->HasField(TEXT("operator_only")))
		{
			GOperatorPolicy.bOperatorOnly = Args->GetBoolField(TEXT("operator_only"));
		}
		if (Args->HasField(TEXT("allow_atomic_placement")))
		{
			GOperatorPolicy.bAllowAtomicPlacement = Args->GetBoolField(TEXT("allow_atomic_placement"));
		}
		if (Args->HasField(TEXT("max_poi_per_call")))
		{
			GOperatorPolicy.MaxPoiPerCall = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_poi_per_call")), 1, 2048);
		}
		if (Args->HasField(TEXT("max_actor_delta_per_pipeline")))
		{
			GOperatorPolicy.MaxActorDeltaPerPipeline = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_actor_delta_per_pipeline")), 10, 200000);
		}
		if (Args->HasField(TEXT("max_memory_used_mb")))
		{
			GOperatorPolicy.MaxMemoryUsedMB = FMath::Max(1024.0f, (float)Args->GetNumberField(TEXT("max_memory_used_mb")));
		}
	}
	return GetOperatorPolicy();
}

FString FProceduralOpsModule::GetProceduralCapabilities(const TSharedPtr<FJsonObject>& Args)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);

	const bool bIncludeUrls = !Args.IsValid() || !Args->HasField(TEXT("include_repo_urls")) || Args->GetBoolField(TEXT("include_repo_urls"));

#if WITH_EDITOR
	struct FCandidatePlugin
	{
		FString Key;
		FString Purpose;
		FString Repo;
		TArray<FString> Aliases;
	};

	const TArray<FCandidatePlugin> Candidates = {
		{ TEXT("pcg_builtin"), TEXT("Core UE PCG framework"), TEXT("https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-framework-in-unreal-engine"), { TEXT("PCG") } },
		{ TEXT("ocg"), TEXT("One-click terrain + biome + river scaffolding"), TEXT("https://github.com/Code1133/ocg"), { TEXT("OCG"), TEXT("OneClickLevelGenerator"), TEXT("OneClickGenerator"), TEXT("OneClickLevelGen") } },
		{ TEXT("pcgex"), TEXT("Graph/connectivity/spatial PCG operators"), TEXT("https://github.com/Nebukam/PCGExtendedToolkit"), { TEXT("PCGExtendedToolkit"), TEXT("PCGEx"), TEXT("PCGExtended") } },
		{ TEXT("layered_biomes"), TEXT("Layered biome rule stack"), TEXT("https://github.com/lazycatsdev/PCGLayeredBiomes"), { TEXT("PCGLayeredBiomes"), TEXT("LayeredBiomes"), TEXT("PCGLayered") } },
		{ TEXT("roadbuilder"), TEXT("Road spline tool + PCG boundaries"), TEXT("https://github.com/fullike/RoadBuilder"), { TEXT("RoadBuilder") } },
		{ TEXT("arteries"), TEXT("Blueprint procedural modeling toolkit"), TEXT("https://github.com/fullike/Arteries"), { TEXT("Arteries") } },
	};

	TSharedPtr<FJsonObject> PluginInfoObj = MakeShared<FJsonObject>();
	TArray<FString> Recommendations;

	auto IsCandidateEnabled = [&](const FString& CandidateKey) -> bool
	{
		bool bInstalled = false;
		bool bEnabled = false;
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (PluginInfoObj->TryGetObjectField(CandidateKey, Obj) && Obj && Obj->IsValid())
		{
			if ((*Obj)->HasField(TEXT("installed"))) { bInstalled = (*Obj)->GetBoolField(TEXT("installed")); }
			if ((*Obj)->HasField(TEXT("enabled")))   { bEnabled = (*Obj)->GetBoolField(TEXT("enabled")); }
		}
		return bInstalled && bEnabled;
	};

	for (const FCandidatePlugin& Candidate : Candidates)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("purpose"), Candidate.Purpose);
		if (bIncludeUrls) { Entry->SetStringField(TEXT("repo"), Candidate.Repo); }

		TSharedPtr<IPlugin> FoundPlugin;
		FString MatchedAlias;
		for (const FString& Alias : Candidate.Aliases)
		{
			TSharedPtr<IPlugin> Maybe = IPluginManager::Get().FindPlugin(Alias);
			if (Maybe.IsValid())
			{
				FoundPlugin = Maybe;
				MatchedAlias = Alias;
				break;
			}
		}

		if (!FoundPlugin.IsValid())
		{
			Entry->SetBoolField(TEXT("installed"), false);
			Entry->SetBoolField(TEXT("enabled"), false);
			Recommendations.Add(FString::Printf(TEXT("Install %s to improve %s."), *Candidate.Key, *Candidate.Purpose));
		}
		else
		{
			const FPluginDescriptor& Desc = FoundPlugin->GetDescriptor();
			Entry->SetBoolField(TEXT("installed"), true);
			Entry->SetBoolField(TEXT("enabled"), FoundPlugin->IsEnabled());
			Entry->SetStringField(TEXT("plugin_name"), FoundPlugin->GetName());
			Entry->SetStringField(TEXT("matched_alias"), MatchedAlias);
			Entry->SetStringField(TEXT("version_name"), Desc.VersionName);
			Entry->SetStringField(TEXT("friendly_name"), Desc.FriendlyName);
			Entry->SetStringField(TEXT("base_dir"), FoundPlugin->GetBaseDir());
			if (!FoundPlugin->IsEnabled())
			{
				Recommendations.Add(FString::Printf(TEXT("Enable plugin %s."), *FoundPlugin->GetName()));
			}
		}

		PluginInfoObj->SetObjectField(Candidate.Key, Entry);
	}
	TSharedPtr<FJsonObject> RecipeObj = MakeShared<FJsonObject>();
	const FString EssentialPCGPath = FPaths::ProjectContentDir() / TEXT("EssentialUE5PCG");
	const bool bEssentialPresent = IFileManager::Get().DirectoryExists(*EssentialPCGPath);
	RecipeObj->SetBoolField(TEXT("essential_ue5_pcg_content_present"), bEssentialPresent);
	RecipeObj->SetStringField(TEXT("essential_ue5_pcg_path_checked"), EssentialPCGPath);
	if (!bEssentialPresent)
	{
		Recommendations.Add(TEXT("Add EssentialUE5PCG examples under Content for reusable graph recipes."));
	}

	Root->SetObjectField(TEXT("plugins"), PluginInfoObj);
	Root->SetObjectField(TEXT("recipe_sources"), RecipeObj);
	Root->SetStringField(TEXT("strategy"), TEXT("operator_orchestration_only"));
	Root->SetStringField(TEXT("direct_object_placement"), GOperatorPolicy.bOperatorOnly ? TEXT("blocked_by_policy") : TEXT("allowed"));

	TSharedPtr<FJsonObject> OperatorSupport = MakeShared<FJsonObject>();
	const bool bPCGReady = IsCandidateEnabled(TEXT("pcg_builtin"));
	const bool bPCGExReady = IsCandidateEnabled(TEXT("pcgex"));
	const bool bRoadReady = IsCandidateEnabled(TEXT("roadbuilder"));
	const bool bLayeredReady = IsCandidateEnabled(TEXT("layered_biomes"));

	OperatorSupport->SetStringField(TEXT("surface_scatter"), bPCGExReady ? TEXT("enhanced") : (bPCGReady ? TEXT("native") : TEXT("unavailable")));
	OperatorSupport->SetStringField(TEXT("spline_scatter"), bPCGReady ? TEXT("native") : TEXT("unavailable"));
	OperatorSupport->SetStringField(TEXT("road_layout"), bRoadReady ? TEXT("plugin") : TEXT("spline_fallback"));
	OperatorSupport->SetStringField(TEXT("biome_layers"), bLayeredReady ? TEXT("plugin") : (bPCGReady ? TEXT("native_fallback") : TEXT("unavailable")));
	OperatorSupport->SetStringField(TEXT("stamp_poi"), TEXT("native"));
	OperatorSupport->SetStringField(TEXT("run_operator_pipeline"), TEXT("native"));
	Root->SetObjectField(TEXT("operator_support"), OperatorSupport);

	TArray<TSharedPtr<FJsonValue>> RecommendationArr;
	for (const FString& Item : Recommendations)
	{
		RecommendationArr.Add(MakeShared<FJsonValueString>(Item));
	}
	Root->SetArrayField(TEXT("recommendations"), RecommendationArr);
#else
	Root->SetStringField(TEXT("note"), TEXT("WITH_EDITOR required for capability scan."));
#endif

	Root->SetObjectField(TEXT("policy"), ParseJsonObjectOrNull(GetOperatorPolicy()));
	return ToJson(Root);
}

FString FProceduralOpsModule::SurfaceScatter(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	const FString TargetId = FirstNonEmptyField(Args, { TEXT("target_label"), TEXT("pcg_volume_label"), TEXT("actor_label"), TEXT("target_actor") });
	if (TargetId.IsEmpty())
	{
		return ErrorJson(TEXT("op_surface_scatter requires target_label (or pcg_volume_label)."));
	}

	AActor* TargetActor = FindActorByAnyId(World, TargetId);
	if (!TargetActor)
	{
		return ErrorJson(FString::Printf(TEXT("Target actor not found: %s"), *TargetId));
	}

	const TSet<FString> Reserved = {
		TEXT("target_label"), TEXT("pcg_volume_label"), TEXT("actor_label"), TEXT("target_actor"),
		TEXT("parameters"), TEXT("generate")
	};

	TMap<FString, TSharedPtr<FJsonValue>> Params;
	GatherParameterObject(Args, Params, Reserved);

	int32 AppliedCount = 0;
	TArray<TSharedPtr<FJsonValue>> AppliedArr;
	TArray<TSharedPtr<FJsonValue>> MissingArr;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params)
	{
		FString AppliedTarget;
		FString AppliedProperty;
		FString Error;
		if (ApplySingleParam(TargetActor, Pair.Key, Pair.Value, AppliedTarget, AppliedProperty, Error))
		{
			++AppliedCount;
			TSharedPtr<FJsonObject> AppliedObj = MakeShared<FJsonObject>();
			AppliedObj->SetStringField(TEXT("param"), Pair.Key);
			AppliedObj->SetStringField(TEXT("target"), AppliedTarget);
			AppliedObj->SetStringField(TEXT("property"), AppliedProperty);
			AppliedArr.Add(MakeShared<FJsonValueObject>(AppliedObj));
		}
		else
		{
			MissingArr.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}
	const bool bGenerate = !Args.IsValid() || !Args->HasField(TEXT("generate")) || Args->GetBoolField(TEXT("generate"));
	const int32 Triggered = bGenerate ? TriggerProceduralGenerate(TargetActor) : 0;

	AddOperatorTag(TargetActor, TEXT("AF_Operator_SurfaceScatter"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("surface_scatter"));
	Root->SetStringField(TEXT("target"), TargetActor->GetActorLabel());
	Root->SetNumberField(TEXT("applied_params"), AppliedCount);
	Root->SetArrayField(TEXT("applied"), AppliedArr);
	Root->SetArrayField(TEXT("missing"), MissingArr);
	Root->SetBoolField(TEXT("generated"), bGenerate);
	Root->SetNumberField(TEXT("generated_components"), Triggered);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}

FString FProceduralOpsModule::SplineScatter(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	const FString TargetId = FirstNonEmptyField(Args, { TEXT("spline_actor_label"), TEXT("target_label"), TEXT("actor_label"), TEXT("target_actor") });
	if (TargetId.IsEmpty())
	{
		return ErrorJson(TEXT("op_spline_scatter requires spline_actor_label (or target_label)."));
	}

	AActor* TargetActor = FindActorByAnyId(World, TargetId);
	if (!TargetActor)
	{
		return ErrorJson(FString::Printf(TEXT("Spline actor not found: %s"), *TargetId));
	}

	const bool bClosedLoop = Args.IsValid() && Args->HasField(TEXT("closed_loop")) ? Args->GetBoolField(TEXT("closed_loop")) : false;
	int32 SplinePointCount = 0;

	const TArray<TSharedPtr<FJsonValue>>* Points = nullptr;
	if (Args.IsValid())
	{
		if (Args->TryGetArrayField(TEXT("control_points"), Points) || Args->TryGetArrayField(TEXT("spline_points"), Points))
		{
			SplinePointCount = ApplySplinePoints(TargetActor, *Points, bClosedLoop);
		}
	}

	const TSet<FString> Reserved = {
		TEXT("spline_actor_label"), TEXT("target_label"), TEXT("actor_label"), TEXT("target_actor"),
		TEXT("control_points"), TEXT("spline_points"), TEXT("closed_loop"),
		TEXT("parameters"), TEXT("generate")
	};

	TMap<FString, TSharedPtr<FJsonValue>> Params;
	GatherParameterObject(Args, Params, Reserved);

	int32 AppliedCount = 0;
	TArray<TSharedPtr<FJsonValue>> MissingArr;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params)
	{
		FString AppliedTarget;
		FString AppliedProperty;
		FString Error;
		if (ApplySingleParam(TargetActor, Pair.Key, Pair.Value, AppliedTarget, AppliedProperty, Error))
		{
			++AppliedCount;
		}
		else
		{
			MissingArr.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}
	const bool bGenerate = !Args.IsValid() || !Args->HasField(TEXT("generate")) || Args->GetBoolField(TEXT("generate"));
	const int32 Triggered = bGenerate ? TriggerProceduralGenerate(TargetActor) : 0;
	AddOperatorTag(TargetActor, TEXT("AF_Operator_SplineScatter"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("spline_scatter"));
	Root->SetStringField(TEXT("target"), TargetActor->GetActorLabel());
	Root->SetNumberField(TEXT("spline_points_applied"), SplinePointCount);
	Root->SetNumberField(TEXT("applied_params"), AppliedCount);
	Root->SetArrayField(TEXT("missing"), MissingArr);
	Root->SetNumberField(TEXT("generated_components"), Triggered);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}

FString FProceduralOpsModule::RoadLayout(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	AActor* RoadActor = nullptr;
	bool bSpawnedActor = false;

	const FString ExistingRoadId = FirstNonEmptyField(Args, { TEXT("road_actor_label"), TEXT("target_label"), TEXT("actor_label") });
	if (!ExistingRoadId.IsEmpty())
	{
		RoadActor = FindActorByAnyId(World, ExistingRoadId);
	}

	if (!RoadActor)
	{
		FString RoadClassPath;
		if (Args.IsValid())
		{
			Args->TryGetStringField(TEXT("road_class_path"), RoadClassPath);
		}
		if (!RoadClassPath.IsEmpty())
		{
			UClass* RoadClass = LoadActorClass(RoadClassPath);
			if (!RoadClass)
			{
				return ErrorJson(FString::Printf(TEXT("road_class_path could not be loaded: %s"), *RoadClassPath));
			}

			FVector SpawnLocation = FVector::ZeroVector;
			const TArray<TSharedPtr<FJsonValue>>* Centerline = nullptr;
			if (Args.IsValid() && (Args->TryGetArrayField(TEXT("centerline_points"), Centerline) || Args->TryGetArrayField(TEXT("control_points"), Centerline)))
			{
				if (Centerline->Num() > 0)
				{
					JsonValueToVector((*Centerline)[0], SpawnLocation);
				}
			}

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			RoadActor = World->SpawnActor<AActor>(RoadClass, FTransform(FRotator::ZeroRotator, SpawnLocation), SpawnParams);
			if (!RoadActor)
			{
				return ErrorJson(TEXT("Failed to spawn road actor."));
			}

			FString RoadLabel = TEXT("RoadLayoutActor");
			if (Args.IsValid())
			{
				Args->TryGetStringField(TEXT("road_label"), RoadLabel);
			}
			RoadActor->SetActorLabel(RoadLabel);
			bSpawnedActor = true;
		}
	}

	if (!RoadActor)
	{
		return ErrorJson(TEXT("op_road_layout requires road_actor_label or road_class_path."));
	}

	int32 SplinePointCount = 0;
	const TArray<TSharedPtr<FJsonValue>>* Centerline = nullptr;
	if (Args.IsValid() && (Args->TryGetArrayField(TEXT("centerline_points"), Centerline) || Args->TryGetArrayField(TEXT("control_points"), Centerline)))
	{
		const bool bClosedLoop = Args->HasField(TEXT("closed_loop")) ? Args->GetBoolField(TEXT("closed_loop")) : false;
		SplinePointCount = ApplySplinePoints(RoadActor, *Centerline, bClosedLoop);
	}
	const TSet<FString> Reserved = {
		TEXT("road_actor_label"), TEXT("target_label"), TEXT("actor_label"), TEXT("road_class_path"),
		TEXT("road_label"), TEXT("centerline_points"), TEXT("control_points"), TEXT("closed_loop"),
		TEXT("parameters"), TEXT("generate")
	};
	TMap<FString, TSharedPtr<FJsonValue>> Params;
	GatherParameterObject(Args, Params, Reserved);

	int32 AppliedCount = 0;
	TArray<TSharedPtr<FJsonValue>> MissingArr;
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params)
	{
		FString AppliedTarget;
		FString AppliedProperty;
		FString Error;
		if (ApplySingleParam(RoadActor, Pair.Key, Pair.Value, AppliedTarget, AppliedProperty, Error))
		{
			++AppliedCount;
		}
		else
		{
			MissingArr.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}

	const bool bGenerate = !Args.IsValid() || !Args->HasField(TEXT("generate")) || Args->GetBoolField(TEXT("generate"));
	const int32 Triggered = bGenerate ? TriggerProceduralGenerate(RoadActor) : 0;
	AddOperatorTag(RoadActor, TEXT("AF_Operator_RoadLayout"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("road_layout"));
	Root->SetStringField(TEXT("target"), RoadActor->GetActorLabel());
	Root->SetBoolField(TEXT("spawned_actor"), bSpawnedActor);
	Root->SetNumberField(TEXT("spline_points_applied"), SplinePointCount);
	Root->SetNumberField(TEXT("applied_params"), AppliedCount);
	Root->SetArrayField(TEXT("missing"), MissingArr);
	Root->SetNumberField(TEXT("generated_components"), Triggered);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}

FString FProceduralOpsModule::BiomeLayers(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	const FString TargetId = FirstNonEmptyField(Args, { TEXT("target_label"), TEXT("pcg_volume_label"), TEXT("actor_label"), TEXT("target_actor") });
	if (TargetId.IsEmpty())
	{
		return ErrorJson(TEXT("op_biome_layers requires target_label."));
	}

	AActor* TargetActor = FindActorByAnyId(World, TargetId);
	if (!TargetActor)
	{
		return ErrorJson(FString::Printf(TEXT("Target actor not found: %s"), *TargetId));
	}

	TMap<FString, TSharedPtr<FJsonValue>> Params;
	if (Args.IsValid())
	{
		const TSharedPtr<FJsonObject>* LayersObj = nullptr;
		if (Args->TryGetObjectField(TEXT("layers"), LayersObj) && LayersObj && LayersObj->IsValid())
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*LayersObj)->Values)
			{
				Params.Add(Pair.Key, Pair.Value);
			}
		}
	}

	const TSet<FString> Reserved = {
		TEXT("target_label"), TEXT("pcg_volume_label"), TEXT("actor_label"), TEXT("target_actor"),
		TEXT("layers"), TEXT("parameters"), TEXT("generate")
	};
	TMap<FString, TSharedPtr<FJsonValue>> ExtraParams;
	GatherParameterObject(Args, ExtraParams, Reserved);
	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : ExtraParams)
	{
		Params.Add(Pair.Key, Pair.Value);
	}

	auto AddAliasIfPresent = [&](const FString& Src, const FString& Alias)
	{
		if (Params.Contains(Src) && !Params.Contains(Alias))
		{
			Params.Add(Alias, Params[Src]);
		}
	};
	AddAliasIfPresent(TEXT("groundcover_density"), TEXT("GroundcoverDensity"));
	AddAliasIfPresent(TEXT("shrub_density"), TEXT("ShrubDensity"));
	AddAliasIfPresent(TEXT("tree_density"), TEXT("TreeDensity"));
	AddAliasIfPresent(TEXT("rock_density"), TEXT("RockDensity"));
	AddAliasIfPresent(TEXT("path_width"), TEXT("PathWidth"));
	AddAliasIfPresent(TEXT("rock_scale"), TEXT("RockScale"));
	int32 AppliedCount = 0;
	TArray<TSharedPtr<FJsonValue>> MissingArr;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Params)
	{
		FString AppliedTarget;
		FString AppliedProperty;
		FString Error;
		if (ApplySingleParam(TargetActor, Pair.Key, Pair.Value, AppliedTarget, AppliedProperty, Error))
		{
			++AppliedCount;
		}
		else
		{
			MissingArr.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}

	const bool bGenerate = !Args.IsValid() || !Args->HasField(TEXT("generate")) || Args->GetBoolField(TEXT("generate"));
	const int32 Triggered = bGenerate ? TriggerProceduralGenerate(TargetActor) : 0;
	AddOperatorTag(TargetActor, TEXT("AF_Operator_BiomeLayers"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("biome_layers"));
	Root->SetStringField(TEXT("target"), TargetActor->GetActorLabel());
	Root->SetNumberField(TEXT("applied_params"), AppliedCount);
	Root->SetArrayField(TEXT("missing"), MissingArr);
	Root->SetNumberField(TEXT("generated_components"), Triggered);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}

FString FProceduralOpsModule::StampPOI(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	TArray<FString> PoiClassPaths;
	if (Args.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* ClassArray = nullptr;
		if (Args->TryGetArrayField(TEXT("poi_class_paths"), ClassArray))
		{
			for (const TSharedPtr<FJsonValue>& Item : *ClassArray)
			{
				if (Item.IsValid() && Item->Type == EJson::String)
				{
					PoiClassPaths.Add(Item->AsString());
				}
			}
		}
		FString SingleClass;
		if (Args->TryGetStringField(TEXT("poi_class_path"), SingleClass) && !SingleClass.IsEmpty())
		{
			PoiClassPaths.AddUnique(SingleClass);
		}
	}

	if (PoiClassPaths.Num() == 0)
	{
		return ErrorJson(TEXT("op_stamp_poi requires poi_class_path or poi_class_paths[]."));
	}

	TArray<UClass*> PoiClasses;
	for (const FString& ClassPath : PoiClassPaths)
	{
		if (UClass* Loaded = LoadActorClass(ClassPath))
		{
			PoiClasses.Add(Loaded);
		}
	}
	if (PoiClasses.Num() == 0)
	{
		return ErrorJson(TEXT("No valid POI class paths could be loaded."));
	}

	const TArray<TSharedPtr<FJsonValue>>* Anchors = nullptr;
	if (!Args.IsValid() || (!Args->TryGetArrayField(TEXT("anchors"), Anchors) && !Args->TryGetArrayField(TEXT("anchor_points"), Anchors)))
	{
		return ErrorJson(TEXT("op_stamp_poi requires anchors[] with {x,y,z}."));
	}

	if (Anchors->Num() == 0)
	{
		return ErrorJson(TEXT("op_stamp_poi anchors array is empty."));
	}

	int32 Seed = 1337;
	if (Args->HasField(TEXT("seed")))
	{
		Seed = (int32)Args->GetNumberField(TEXT("seed"));
	}

	bool bAlignToSurface = true;
	if (Args->HasField(TEXT("align_to_surface")))
	{
		bAlignToSurface = Args->GetBoolField(TEXT("align_to_surface"));
	}
	bool bAlignToNormal = false;
	if (Args->HasField(TEXT("align_to_normal")))
	{
		bAlignToNormal = Args->GetBoolField(TEXT("align_to_normal"));
	}

	FString LabelPrefix = TEXT("POI");
	Args->TryGetStringField(TEXT("label_prefix"), LabelPrefix);

	int32 MaxCount = Anchors->Num();
	if (Args->HasField(TEXT("max_count")))
	{
		MaxCount = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_count")), 1, Anchors->Num());
	}
	MaxCount = FMath::Min(MaxCount, GOperatorPolicy.MaxPoiPerCall);

	FRandomStream RNG(Seed);
	TArray<TSharedPtr<FJsonValue>> PlacedArr;
	int32 Spawned = 0;

	for (int32 i = 0; i < MaxCount; ++i)
	{
		const TSharedPtr<FJsonValue>& AnchorValue = (*Anchors)[i];
		if (!AnchorValue.IsValid() || AnchorValue->Type != EJson::Object)
		{
			continue;
		}
		const TSharedPtr<FJsonObject> AnchorObj = AnchorValue->AsObject();
		if (!AnchorObj.IsValid())
		{
			continue;
		}

		FVector Location = FVector::ZeroVector;
		if (AnchorObj->HasField(TEXT("location")))
		{
			JsonValueToVector(AnchorObj->TryGetField(TEXT("location")), Location);
		}
		else
		{
			if (!AnchorObj->HasField(TEXT("x")) || !AnchorObj->HasField(TEXT("y")) || !AnchorObj->HasField(TEXT("z")))
			{
				continue;
			}
			Location.X = (float)AnchorObj->GetNumberField(TEXT("x"));
			Location.Y = (float)AnchorObj->GetNumberField(TEXT("y"));
			Location.Z = (float)AnchorObj->GetNumberField(TEXT("z"));
		}

		FRotator Rotation = FRotator::ZeroRotator;
		if (AnchorObj->HasField(TEXT("pitch"))) { Rotation.Pitch = (float)AnchorObj->GetNumberField(TEXT("pitch")); }
		if (AnchorObj->HasField(TEXT("yaw")))   { Rotation.Yaw   = (float)AnchorObj->GetNumberField(TEXT("yaw")); }
		if (AnchorObj->HasField(TEXT("roll")))  { Rotation.Roll  = (float)AnchorObj->GetNumberField(TEXT("roll")); }

		if (bAlignToSurface)
		{
			FVector SurfaceNormal = FVector::UpVector;
			if (ProjectPointToSurface(World, Location, &SurfaceNormal) && bAlignToNormal)
			{
				const FVector Right = FVector::CrossProduct(SurfaceNormal, FVector::ForwardVector).GetSafeNormal();
				const FVector Forward = FVector::CrossProduct(Right, SurfaceNormal).GetSafeNormal();
				const FMatrix Basis(Forward, Right, SurfaceNormal, FVector::ZeroVector);
				Rotation = Basis.Rotator();
			}
		}

		const int32 ClassIdx = RNG.RandRange(0, PoiClasses.Num() - 1);
		UClass* SpawnClass = PoiClasses[ClassIdx];
		if (!SpawnClass)
		{
			continue;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		AActor* SpawnedActor = World->SpawnActor<AActor>(SpawnClass, FTransform(Rotation, Location), SpawnParams);
		if (!SpawnedActor)
		{
			continue;
		}

		SpawnedActor->SetActorLabel(FString::Printf(TEXT("%s_%03d"), *LabelPrefix, Spawned + 1));
		AddOperatorTag(SpawnedActor, TEXT("AF_Operator_POI"));

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("label"), SpawnedActor->GetActorLabel());
		Entry->SetStringField(TEXT("class"), SpawnClass->GetPathName());
		Entry->SetObjectField(TEXT("location"), VecToObj(SpawnedActor->GetActorLocation()));
		PlacedArr.Add(MakeShared<FJsonValueObject>(Entry));
		++Spawned;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("stamp_poi"));
	Root->SetNumberField(TEXT("spawned"), Spawned);
	Root->SetArrayField(TEXT("placed"), PlacedArr);
	Root->SetNumberField(TEXT("seed"), Seed);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}

FString FProceduralOpsModule::RunOperatorPipeline(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	if (GOperatorPolicy.bOperatorOnly && Args.IsValid() && Args->HasField(TEXT("allow_atomic_placement")))
	{
		const bool bRequestAtomic = Args->GetBoolField(TEXT("allow_atomic_placement"));
		if (bRequestAtomic && !GOperatorPolicy.bAllowAtomicPlacement)
		{
			return ErrorJson(TEXT("Policy blocks atomic placement. Use constrained operators only."));
		}
	}

	const int32 ActorCountBefore = CountWorldActors(World);
	const FPlatformMemoryStats MemBefore = FPlatformMemory::GetStats();
	const float UsedBeforeMB = (float)((double)MemBefore.UsedPhysical / (1024.0 * 1024.0));

	int32 MaxActorDelta = GOperatorPolicy.MaxActorDeltaPerPipeline;
	float MaxMemoryMB = GOperatorPolicy.MaxMemoryUsedMB;
	bool bStopOnError = true;

	if (Args.IsValid())
	{
		if (Args->HasField(TEXT("max_actor_delta")))
		{
			MaxActorDelta = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_actor_delta")), 1, 200000);
		}
		if (Args->HasField(TEXT("max_memory_used_mb")))
		{
			MaxMemoryMB = FMath::Max(1024.0f, (float)Args->GetNumberField(TEXT("max_memory_used_mb")));
		}
		if (Args->HasField(TEXT("stop_on_error")))
		{
			bStopOnError = Args->GetBoolField(TEXT("stop_on_error"));
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "RunOperatorPipeline", "AgentForge: Run Operator Pipeline"));
	TArray<TSharedPtr<FJsonValue>> StageResults;
	bool bAnyFailure = false;

	auto BuildStageArgs = [&](const FString& Primary, const FString& Secondary = FString()) -> TSharedPtr<FJsonObject>
	{
		if (!Args.IsValid())
		{
			return nullptr;
		}

		const TSharedPtr<FJsonObject>* StageObj = nullptr;
		if (Args->TryGetObjectField(Primary, StageObj) && StageObj && StageObj->IsValid())
		{
			TSharedPtr<FJsonObject> CopyObj = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StageObj)->Values)
			{
				CopyObj->SetField(Pair.Key, Pair.Value);
			}
			if (Args->HasField(TEXT("seed")) && !CopyObj->HasField(TEXT("seed")))
			{
				CopyObj->SetNumberField(TEXT("seed"), Args->GetNumberField(TEXT("seed")));
			}
			if (Args->HasField(TEXT("palette_id")) && !CopyObj->HasField(TEXT("palette_id")))
			{
				CopyObj->SetStringField(TEXT("palette_id"), Args->GetStringField(TEXT("palette_id")));
			}
			return CopyObj;
		}

		if (!Secondary.IsEmpty() && Args->TryGetObjectField(Secondary, StageObj) && StageObj && StageObj->IsValid())
		{
			TSharedPtr<FJsonObject> CopyObj = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*StageObj)->Values)
			{
				CopyObj->SetField(Pair.Key, Pair.Value);
			}
			if (Args->HasField(TEXT("seed")) && !CopyObj->HasField(TEXT("seed")))
			{
				CopyObj->SetNumberField(TEXT("seed"), Args->GetNumberField(TEXT("seed")));
			}
			if (Args->HasField(TEXT("palette_id")) && !CopyObj->HasField(TEXT("palette_id")))
			{
				CopyObj->SetStringField(TEXT("palette_id"), Args->GetStringField(TEXT("palette_id")));
			}
			return CopyObj;
		}

		return nullptr;
	};

	auto RunStage = [&](const FString& Name, const TSharedPtr<FJsonObject>& StageArgs, TFunctionRef<FString(const TSharedPtr<FJsonObject>&)> Fn)
	{
		if (!StageArgs.IsValid())
		{
			return;
		}

		const FString Raw = Fn(StageArgs);
		TSharedPtr<FJsonObject> Parsed = ParseJsonObjectOrNull(Raw);
		if (!Parsed.IsValid())
		{
			Parsed = MakeShared<FJsonObject>();
			Parsed->SetBoolField(TEXT("ok"), false);
			Parsed->SetStringField(TEXT("error"), TEXT("stage_returned_non_json"));
			Parsed->SetStringField(TEXT("raw"), Raw);
		}
		Parsed->SetStringField(TEXT("stage"), Name);

		if (Parsed->HasField(TEXT("error")))
		{
			bAnyFailure = true;
		}

		StageResults.Add(MakeShared<FJsonValueObject>(Parsed));
	};

	RunStage(TEXT("surface_scatter"), BuildStageArgs(TEXT("surface_scatter"), TEXT("surface")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::SurfaceScatter(StageArgs); });
	RunStage(TEXT("spline_scatter"), BuildStageArgs(TEXT("spline_scatter"), TEXT("spline")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::SplineScatter(StageArgs); });
	RunStage(TEXT("road_layout"), BuildStageArgs(TEXT("road_layout"), TEXT("roads")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::RoadLayout(StageArgs); });
	RunStage(TEXT("biome_layers"), BuildStageArgs(TEXT("biome_layers"), TEXT("biomes")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::BiomeLayers(StageArgs); });
	RunStage(TEXT("stamp_poi"), BuildStageArgs(TEXT("stamp_poi"), TEXT("poi")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::StampPOI(StageArgs); });

	if (bAnyFailure && bStopOnError)
	{
		Transaction.Cancel();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), TEXT("Pipeline halted after a stage error."));
		Root->SetArrayField(TEXT("stages"), StageResults);
		Root->SetBoolField(TEXT("rolled_back"), true);
		return ToJson(Root);
	}

	const int32 ActorCountAfter = CountWorldActors(World);
	const int32 ActorDelta = ActorCountAfter - ActorCountBefore;
	const FPlatformMemoryStats MemAfter = FPlatformMemory::GetStats();
	const float UsedAfterMB = (float)((double)MemAfter.UsedPhysical / (1024.0 * 1024.0));

	bool bBudgetExceeded = false;
	FString BudgetFailureReason;
	if (ActorDelta > MaxActorDelta)
	{
		bBudgetExceeded = true;
		BudgetFailureReason = FString::Printf(TEXT("Actor delta %d exceeded max_actor_delta %d."), ActorDelta, MaxActorDelta);
	}
	else if (UsedAfterMB > MaxMemoryMB)
	{
		bBudgetExceeded = true;
		BudgetFailureReason = FString::Printf(TEXT("Memory %.1f MB exceeded max_memory_used_mb %.1f MB."), UsedAfterMB, MaxMemoryMB);
	}

	if (bBudgetExceeded)
	{
		Transaction.Cancel();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), BudgetFailureReason);
		Root->SetArrayField(TEXT("stages"), StageResults);
		Root->SetBoolField(TEXT("rolled_back"), true);
		Root->SetNumberField(TEXT("actor_delta"), ActorDelta);
		Root->SetNumberField(TEXT("memory_before_mb"), UsedBeforeMB);
		Root->SetNumberField(TEXT("memory_after_mb"), UsedAfterMB);
		return ToJson(Root);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), !bAnyFailure);
	Root->SetStringField(TEXT("operator_mode"), TEXT("deterministic_pipeline"));
	Root->SetArrayField(TEXT("stages"), StageResults);
	Root->SetBoolField(TEXT("rolled_back"), false);
	Root->SetNumberField(TEXT("actor_count_before"), ActorCountBefore);
	Root->SetNumberField(TEXT("actor_count_after"), ActorCountAfter);
	Root->SetNumberField(TEXT("actor_delta"), ActorDelta);
	Root->SetNumberField(TEXT("memory_before_mb"), UsedBeforeMB);
	Root->SetNumberField(TEXT("memory_after_mb"), UsedAfterMB);
	Root->SetNumberField(TEXT("max_actor_delta"), MaxActorDelta);
	Root->SetNumberField(TEXT("max_memory_used_mb"), MaxMemoryMB);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}
