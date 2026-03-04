
// Copyright UEAgentForge Project. All Rights Reserved.
// ProceduralOpsModule.cpp - deterministic operator-centric procedural workflow.

#include "Operators/ProceduralOpsModule.h"
#include "Distribution/BiomePartition.h"
#include "Distribution/Clearings.h"
#include "Distribution/DensityField.h"
#include "Distribution/DistributionEngine.h"
#include "Distribution/InteractionRules.h"
#include "Palette/PaletteManager.h"
#include "Terrain/TerrainGenerator.h"
#include "Visual/SceneEvaluator.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
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
#include <limits>

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
		int32 MaxSpawnPoints = 50000;
		int32 MaxClusterCount = 1024;
		float MaxGenerationTimeMs = 30000.0f;
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

	struct FDistributionRequest
	{
		FString Mode = TEXT("blue_noise");
		float Density = 0.25f;
		float ClusterRadius = 400.0f;
		float MinSpacing = 220.0f;
		int32 Seed = 1337;
		int32 ExplicitPointCount = 0;
		int32 ExplicitClusterCount = 0;
		int32 MaxSpawnPoints = 50000;
		int32 MaxClusterCount = 1024;
		float MaxGenerationTimeMs = 30000.0f;

		bool bUseHeightRange = false;
		float MinHeight = -1000000.0f;
		float MaxHeight = 1000000.0f;

		bool bUseSlopeRange = false;
		float MinSlope = 0.0f;
		float MaxSlope = 90.0f;

		bool bUseDistanceMask = false;
		FVector DistanceOrigin = FVector::ZeroVector;
		float MinDistance = 0.0f;
		float MaxDistance = 1000000.0f;

		bool bUseDensityGradient = true;
		float DensitySigma = 3000.0f;
		float DensityNoise = 0.15f;
		int32 DensityFieldResolution = 64;

		bool bUseClearings = false;
		float ClearingDensity = 0.0f;
		int32 ExplicitClearingCount = 0;
		float ClearingRadiusMin = 200.0f;
		float ClearingRadiusMax = 800.0f;

		bool bUseBiomePartition = false;
		int32 BiomeCount = 0;
		float BiomeBlendDistance = 300.0f;
		TArray<FString> BiomeTypes;
		TSet<FString> AllowedBiomes;

		bool bUseInteractionRules = false;
		TArray<FVector> AvoidPoints;
		float AvoidRadius = 200.0f;
		TArray<FVector> PreferNearPoints;
		float PreferRadius = 500.0f;
		float PreferStrength = 0.5f;
	};

	struct FDistributionDiagnostics
	{
		int32 RequestedPoints = 0;
		int32 BaseGeneratedPoints = 0;
		int32 AfterHeightFilter = 0;
		int32 AfterSlopeFilter = 0;
		int32 AfterDistanceMask = 0;
		int32 AfterDensityGradient = 0;
		int32 AfterClearings = 0;
		int32 AfterBiomeFilter = 0;
		int32 AfterInteractionRules = 0;
		int32 FinalPoints = 0;
		int32 ClearingCount = 0;
		int32 BiomeSeedCount = 0;
		float DensityFieldAverage = 0.0f;
		double GenerationTimeMs = 0.0;
		bool bGenerationTimeExceeded = false;
		TMap<FString, int32> BiomeHistogram;
		FSceneEvaluationMetrics SceneMetrics;
	};

	static bool ParseNumericArrayRange(const TSharedPtr<FJsonObject>& Args, const FString& Field, float& OutMin, float& OutMax)
	{
		const TArray<TSharedPtr<FJsonValue>>* RangeArr = nullptr;
		if (!Args.IsValid() || !Args->TryGetArrayField(Field, RangeArr) || !RangeArr || RangeArr->Num() < 2)
		{
			return false;
		}
		if (!(*RangeArr)[0].IsValid() || !(*RangeArr)[1].IsValid())
		{
			return false;
		}
		const double MinVal = (*RangeArr)[0]->AsNumber();
		const double MaxVal = (*RangeArr)[1]->AsNumber();
		OutMin = (float)FMath::Min(MinVal, MaxVal);
		OutMax = (float)FMath::Max(MinVal, MaxVal);
		return true;
	}
	static bool ParseStringArrayField(const TSharedPtr<FJsonObject>& Args, const FString& Field, TArray<FString>& OutValues)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args.IsValid() || !Args->TryGetArrayField(Field, Arr) || !Arr)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Entry : *Arr)
		{
			if (Entry.IsValid() && Entry->Type == EJson::String)
			{
				const FString Value = Entry->AsString().TrimStartAndEnd().ToLower();
				if (!Value.IsEmpty())
				{
					OutValues.Add(Value);
				}
			}
		}
		return OutValues.Num() > 0;
	}

	static bool ParseVectorArrayField(const TSharedPtr<FJsonObject>& Args, const FString& Field, TArray<FVector>& OutValues)
	{
		OutValues.Reset();
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Args.IsValid() || !Args->TryGetArrayField(Field, Arr) || !Arr)
		{
			return false;
		}

		for (const TSharedPtr<FJsonValue>& Entry : *Arr)
		{
			FVector Value = FVector::ZeroVector;
			if (JsonValueToVector(Entry, Value))
			{
				OutValues.Add(Value);
			}
		}
		return OutValues.Num() > 0;
	}

	static FDistributionRequest ParseDistributionRequest(const TSharedPtr<FJsonObject>& Args, AActor* TargetActor)
	{
		FDistributionRequest Request;
		Request.MaxSpawnPoints = GOperatorPolicy.MaxSpawnPoints;
		Request.MaxClusterCount = GOperatorPolicy.MaxClusterCount;
		Request.MaxGenerationTimeMs = GOperatorPolicy.MaxGenerationTimeMs;

		if (!Args.IsValid())
		{
			if (TargetActor)
			{
				Request.DistanceOrigin = TargetActor->GetActorLocation();
			}
			return Request;
		}

		Args->TryGetStringField(TEXT("distribution_mode"), Request.Mode);
		if (Args->HasField(TEXT("density")))
		{
			Request.Density = FMath::Clamp((float)Args->GetNumberField(TEXT("density")), 0.0f, 1000.0f);
		}
		if (Args->HasField(TEXT("cluster_radius")))
		{
			Request.ClusterRadius = FMath::Max(1.0f, (float)Args->GetNumberField(TEXT("cluster_radius")));
		}
		if (Args->HasField(TEXT("min_spacing")))
		{
			Request.MinSpacing = FMath::Max(1.0f, (float)Args->GetNumberField(TEXT("min_spacing")));
		}
		if (Args->HasField(TEXT("seed")))
		{
			Request.Seed = (int32)Args->GetNumberField(TEXT("seed"));
		}
		if (Args->HasField(TEXT("point_count")))
		{
			Request.ExplicitPointCount = FMath::Max(0, (int32)Args->GetNumberField(TEXT("point_count")));
		}
		else if (Args->HasField(TEXT("max_points")))
		{
			Request.ExplicitPointCount = FMath::Max(0, (int32)Args->GetNumberField(TEXT("max_points")));
		}
		if (Args->HasField(TEXT("cluster_count")))
		{
			Request.ExplicitClusterCount = FMath::Max(0, (int32)Args->GetNumberField(TEXT("cluster_count")));
		}
		if (Args->HasField(TEXT("max_spawn_points")))
		{
			Request.MaxSpawnPoints = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_spawn_points")), 1, 500000);
		}
		if (Args->HasField(TEXT("max_cluster_count")))
		{
			Request.MaxClusterCount = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_cluster_count")), 1, 16384);
		}
		if (Args->HasField(TEXT("max_generation_time_ms")))
		{
			Request.MaxGenerationTimeMs = FMath::Clamp((float)Args->GetNumberField(TEXT("max_generation_time_ms")), 10.0f, 600000.0f);
		}

		if (Args->HasField(TEXT("use_density_gradient")))
		{
			Request.bUseDensityGradient = Args->GetBoolField(TEXT("use_density_gradient"));
		}
		if (Args->HasField(TEXT("density_sigma")))
		{
			Request.DensitySigma = FMath::Max(1.0f, (float)Args->GetNumberField(TEXT("density_sigma")));
		}
		if (Args->HasField(TEXT("density_noise")))
		{
			Request.DensityNoise = FMath::Clamp((float)Args->GetNumberField(TEXT("density_noise")), 0.0f, 1.0f);
		}
		if (Args->HasField(TEXT("density_field_resolution")))
		{
			Request.DensityFieldResolution = FMath::Clamp((int32)Args->GetNumberField(TEXT("density_field_resolution")), 8, 512);
		}

		if (ParseNumericArrayRange(Args, TEXT("height_range"), Request.MinHeight, Request.MaxHeight))
		{
			Request.bUseHeightRange = true;
		}

		if (ParseNumericArrayRange(Args, TEXT("slope_range"), Request.MinSlope, Request.MaxSlope))
		{
			Request.bUseSlopeRange = true;
		}

		if (Args->HasField(TEXT("distance_mask")))
		{
			const TSharedPtr<FJsonObject>* DistObj = nullptr;
			if (Args->TryGetObjectField(TEXT("distance_mask"), DistObj) && DistObj && DistObj->IsValid())
			{
				Request.bUseDistanceMask = true;
				if ((*DistObj)->HasField(TEXT("min")))
				{
					Request.MinDistance = FMath::Max(0.0f, (float)(*DistObj)->GetNumberField(TEXT("min")));
				}
				if ((*DistObj)->HasField(TEXT("max")))
				{
					Request.MaxDistance = FMath::Max(Request.MinDistance, (float)(*DistObj)->GetNumberField(TEXT("max")));
				}

				FVector ParsedOrigin = FVector::ZeroVector;
				if ((*DistObj)->HasField(TEXT("origin")) && JsonValueToVector((*DistObj)->TryGetField(TEXT("origin")), ParsedOrigin))
				{
					Request.DistanceOrigin = ParsedOrigin;
				}
			}
		}

		if (Args->HasField(TEXT("clearing_density")))
		{
			Request.ClearingDensity = FMath::Max(0.0f, (float)Args->GetNumberField(TEXT("clearing_density")));
			Request.bUseClearings = Request.ClearingDensity > KINDA_SMALL_NUMBER;
		}
		if (Args->HasField(TEXT("clearing_count")))
		{
			Request.ExplicitClearingCount = FMath::Max(0, (int32)Args->GetNumberField(TEXT("clearing_count")));
			Request.bUseClearings = Request.ExplicitClearingCount > 0 || Request.bUseClearings;
		}
		if (Args->HasField(TEXT("clearing_radius_min")))
		{
			Request.ClearingRadiusMin = FMath::Max(50.0f, (float)Args->GetNumberField(TEXT("clearing_radius_min")));
		}
		if (Args->HasField(TEXT("clearing_radius_max")))
		{
			Request.ClearingRadiusMax = FMath::Max(Request.ClearingRadiusMin, (float)Args->GetNumberField(TEXT("clearing_radius_max")));
		}
		const TSharedPtr<FJsonObject>* ClearingsObj = nullptr;
		if (Args->TryGetObjectField(TEXT("clearings"), ClearingsObj) && ClearingsObj && ClearingsObj->IsValid())
		{
			if ((*ClearingsObj)->HasField(TEXT("density")))
			{
				Request.ClearingDensity = FMath::Max(0.0f, (float)(*ClearingsObj)->GetNumberField(TEXT("density")));
			}
			if ((*ClearingsObj)->HasField(TEXT("count")))
			{
				Request.ExplicitClearingCount = FMath::Max(0, (int32)(*ClearingsObj)->GetNumberField(TEXT("count")));
			}
			if ((*ClearingsObj)->HasField(TEXT("radius_min")))
			{
				Request.ClearingRadiusMin = FMath::Max(50.0f, (float)(*ClearingsObj)->GetNumberField(TEXT("radius_min")));
			}
			if ((*ClearingsObj)->HasField(TEXT("radius_max")))
			{
				Request.ClearingRadiusMax = FMath::Max(Request.ClearingRadiusMin, (float)(*ClearingsObj)->GetNumberField(TEXT("radius_max")));
			}
			Request.bUseClearings = Request.ClearingDensity > KINDA_SMALL_NUMBER || Request.ExplicitClearingCount > 0;
		}

		if (Args->HasField(TEXT("biome_count")))
		{
			Request.BiomeCount = FMath::Clamp((int32)Args->GetNumberField(TEXT("biome_count")), 1, 256);
			Request.bUseBiomePartition = true;
		}
		if (Args->HasField(TEXT("biome_blend_distance")))
		{
			Request.BiomeBlendDistance = FMath::Max(0.0f, (float)Args->GetNumberField(TEXT("biome_blend_distance")));
		}
		ParseStringArrayField(Args, TEXT("biome_types"), Request.BiomeTypes);
		if (Request.BiomeTypes.Num() > 0)
		{
			Request.bUseBiomePartition = true;
		}

		TArray<FString> AllowedBiomes;
		if (ParseStringArrayField(Args, TEXT("allowed_biomes"), AllowedBiomes))
		{
			Request.bUseBiomePartition = true;
			for (const FString& Biome : AllowedBiomes)
			{
				Request.AllowedBiomes.Add(Biome);
			}
		}

		ParseVectorArrayField(Args, TEXT("avoid_points"), Request.AvoidPoints);
		if (Args->HasField(TEXT("avoid_radius")))
		{
			Request.AvoidRadius = FMath::Max(1.0f, (float)Args->GetNumberField(TEXT("avoid_radius")));
		}
		ParseVectorArrayField(Args, TEXT("prefer_near_points"), Request.PreferNearPoints);
		if (Args->HasField(TEXT("prefer_radius")))
		{
			Request.PreferRadius = FMath::Max(1.0f, (float)Args->GetNumberField(TEXT("prefer_radius")));
		}
		if (Args->HasField(TEXT("prefer_strength")))
		{
			Request.PreferStrength = FMath::Clamp((float)Args->GetNumberField(TEXT("prefer_strength")), 0.0f, 1.0f);
		}

		const TSharedPtr<FJsonObject>* InteractionObj = nullptr;
		if (Args->TryGetObjectField(TEXT("interaction_rules"), InteractionObj) && InteractionObj && InteractionObj->IsValid())
		{
			TArray<FVector> ParsedPoints;
			if (ParseVectorArrayField(*InteractionObj, TEXT("avoid_points"), ParsedPoints))
			{
				Request.AvoidPoints = ParsedPoints;
			}
			if ((*InteractionObj)->HasField(TEXT("avoid_radius")))
			{
				Request.AvoidRadius = FMath::Max(1.0f, (float)(*InteractionObj)->GetNumberField(TEXT("avoid_radius")));
			}

			ParsedPoints.Reset();
			if (ParseVectorArrayField(*InteractionObj, TEXT("prefer_near_points"), ParsedPoints))
			{
				Request.PreferNearPoints = ParsedPoints;
			}
			if ((*InteractionObj)->HasField(TEXT("prefer_radius")))
			{
				Request.PreferRadius = FMath::Max(1.0f, (float)(*InteractionObj)->GetNumberField(TEXT("prefer_radius")));
			}
			if ((*InteractionObj)->HasField(TEXT("prefer_strength")))
			{
				Request.PreferStrength = FMath::Clamp((float)(*InteractionObj)->GetNumberField(TEXT("prefer_strength")), 0.0f, 1.0f);
			}
		}
		Request.bUseInteractionRules = Request.AvoidPoints.Num() > 0 || Request.PreferNearPoints.Num() > 0;

		if (TargetActor && !Request.bUseDistanceMask)
		{
			Request.DistanceOrigin = TargetActor->GetActorLocation();
		}

		return Request;
	}

	static TArray<FVector> GenerateDistributionPoints(
		UWorld* World,
		AActor* TargetActor,
		const FDistributionRequest& Request,
		FDistributionDiagnostics* OutDiagnostics = nullptr)
	{
		TArray<FVector> Points;
		if (!TargetActor)
		{
			return Points;
		}

		FDistributionDiagnostics Diagnostics;
		const double StartSeconds = FPlatformTime::Seconds();
		auto IsTimeExceeded = [&]() -> bool
		{
			if (Request.MaxGenerationTimeMs <= 0.0f)
			{
				return false;
			}
			const double ElapsedMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			return ElapsedMs > (double)Request.MaxGenerationTimeMs;
		};

		FVector Origin = FVector::ZeroVector;
		FVector Extent = FVector(2000.0f, 2000.0f, 500.0f);
		TargetActor->GetActorBounds(true, Origin, Extent);
		Extent.X = FMath::Max(Extent.X, 200.0f);
		Extent.Y = FMath::Max(Extent.Y, 200.0f);
		Extent.Z = FMath::Max(Extent.Z, 200.0f);

		FBox Bounds(Origin - Extent, Origin + Extent);
		const float AreaM2 = (Extent.X * 2.0f * Extent.Y * 2.0f) / 10000.0f;
		const int32 EstimatedByDensity = FMath::Clamp(FMath::RoundToInt(AreaM2 * Request.Density), 1, Request.MaxSpawnPoints);
		const int32 TargetCount = FMath::Clamp(
			Request.ExplicitPointCount > 0 ? Request.ExplicitPointCount : EstimatedByDensity,
			1,
			Request.MaxSpawnPoints);
		Diagnostics.RequestedPoints = TargetCount;

		const FString ModeLower = Request.Mode.ToLower();
		if (ModeLower == TEXT("cluster") || ModeLower == TEXT("clustered"))
		{
			const int32 ClusterCount = FMath::Clamp(
				Request.ExplicitClusterCount > 0 ? Request.ExplicitClusterCount : FMath::RoundToInt(FMath::Sqrt((float)TargetCount) * 0.35f),
				1,
				Request.MaxClusterCount);
			const TArray<FVector> Clustered = FDistributionEngine::GenerateClusterPoints(Bounds, TargetCount, ClusterCount, Request.ClusterRadius, Request.Seed);
			const float MinDistSq = FMath::Square(FMath::Max(1.0f, Request.MinSpacing));
			for (const FVector& Candidate : Clustered)
			{
				bool bAccept = true;
				for (const FVector& Existing : Points)
				{
					if (FVector::DistSquared2D(Candidate, Existing) < MinDistSq)
					{
						bAccept = false;
						break;
					}
				}
				if (bAccept)
				{
					Points.Add(Candidate);
				}
			}
		}
		else if (ModeLower == TEXT("poisson") || ModeLower == TEXT("poisson_disk") || ModeLower == TEXT("poisson_disk_sampling"))
		{
			Points = FDistributionEngine::GeneratePoissonDiskPoints(Bounds, TargetCount, Request.MinSpacing, Request.Seed);
		}
		else
		{
			Points = FDistributionEngine::GenerateBlueNoisePoints(Bounds, TargetCount, Request.Seed, Request.MinSpacing);
		}
		Diagnostics.BaseGeneratedPoints = Points.Num();

		if (Request.Mode.ToLower() == TEXT("cluster") && Request.MinSpacing > 1.0f)
		{
			Points = FInteractionRules::ApplySelfSpacing(Points, Request.MinSpacing);
		}

		if (Request.bUseHeightRange)
		{
			Points = FDistributionEngine::ApplyHeightFilter(Points, Request.MinHeight, Request.MaxHeight);
		}
		Diagnostics.AfterHeightFilter = Points.Num();
		if (Request.bUseSlopeRange)
		{
			Points = FDistributionEngine::ApplySlopeFilter(Points, World, Request.MinSlope, Request.MaxSlope);
		}
		Diagnostics.AfterSlopeFilter = Points.Num();
		if (Request.bUseDistanceMask)
		{
			Points = FDistributionEngine::ApplyDistanceMask(Points, Request.DistanceOrigin, Request.MinDistance, Request.MaxDistance);
		}
		Diagnostics.AfterDistanceMask = Points.Num();

		if (!IsTimeExceeded() && Request.bUseDensityGradient && Points.Num() > 0)
		{
			FDensityFieldConfig DensityConfig;
			DensityConfig.BaseDensity = 1.0f;
			DensityConfig.Sigma = Request.DensitySigma;
			DensityConfig.Center = Origin;
			DensityConfig.NoiseBlend = Request.DensityNoise;
			DensityConfig.Seed = Request.Seed;

			const int32 DensityRes = FMath::Clamp(Request.DensityFieldResolution, 8, 512);
			const TArray<float> DensityField = FDensityField::GenerateDensityField(Bounds, DensityRes, DensityRes, DensityConfig);
			float SumDensity = 0.0f;
			for (const float DensityValue : DensityField)
			{
				SumDensity += DensityValue;
			}
			Diagnostics.DensityFieldAverage = DensityField.Num() > 0 ? (SumDensity / (float)DensityField.Num()) : 0.0f;

			Points = FDensityField::ApplyDensityGradient(Points, DensityField, DensityRes, DensityRes, Bounds, Request.Seed ^ 0x5B8D3D6A, 0.03f);
		}
		Diagnostics.AfterDensityGradient = Points.Num();

		if (!IsTimeExceeded() && Request.bUseClearings && Points.Num() > 0)
		{
			const int32 DerivedCount = FMath::Clamp(FMath::RoundToInt((AreaM2 / 2500.0f) * Request.ClearingDensity), 0, 2048);
			const int32 ClearingCount = Request.ExplicitClearingCount > 0 ? Request.ExplicitClearingCount : DerivedCount;
			const TArray<FClearingRegion> ClearingRegions = FClearings::GenerateClearings(
				Bounds,
				ClearingCount,
				Request.ClearingRadiusMin,
				Request.ClearingRadiusMax,
				Request.Seed ^ 0x1E35A7BD);
			Diagnostics.ClearingCount = ClearingRegions.Num();
			Points = FClearings::ApplyClearingMask(Points, ClearingRegions);
		}
		Diagnostics.AfterClearings = Points.Num();

		if (!IsTimeExceeded() && Request.bUseBiomePartition && Points.Num() > 0)
		{
			const int32 BiomeCount = FMath::Clamp(Request.BiomeCount > 0 ? Request.BiomeCount : FMath::Max(2, Request.BiomeTypes.Num()), 1, 256);
			const FBiomePartitionData Partition = FBiomePartition::GenerateVoronoiBiomes(
				Bounds,
				BiomeCount,
				Request.BiomeTypes,
				Request.Seed ^ 0x9E3779B9,
				Request.BiomeBlendDistance);
			Diagnostics.BiomeSeedCount = Partition.Seeds.Num();

			TArray<FVector> BiomeFiltered;
			BiomeFiltered.Reserve(Points.Num());
			for (const FVector& Point : Points)
			{
				const FBiomeBlendSample Blend = FBiomePartition::BlendBiomeEdges(Partition, Point);
				if (!Blend.PrimaryBiome.IsEmpty())
				{
					Diagnostics.BiomeHistogram.FindOrAdd(Blend.PrimaryBiome) += 1;
				}

				if (Request.AllowedBiomes.Num() == 0)
				{
					BiomeFiltered.Add(Point);
					continue;
				}

				if (Request.AllowedBiomes.Contains(Blend.PrimaryBiome))
				{
					BiomeFiltered.Add(Point);
					continue;
				}

				if (!Blend.SecondaryBiome.IsEmpty() &&
					Request.AllowedBiomes.Contains(Blend.SecondaryBiome) &&
					Blend.BlendAlpha >= 0.5f)
				{
					BiomeFiltered.Add(Point);
				}
			}

			Points = MoveTemp(BiomeFiltered);
		}
		Diagnostics.AfterBiomeFilter = Points.Num();

		if (!IsTimeExceeded() && Request.bUseInteractionRules && Points.Num() > 0)
		{
			if (Request.AvoidPoints.Num() > 0)
			{
				Points = FInteractionRules::ApplyAvoidance(Points, Request.AvoidPoints, Request.AvoidRadius);
			}
			if (Request.PreferNearPoints.Num() > 0)
			{
				Points = FInteractionRules::ApplyAttractorBias(Points, Request.PreferNearPoints, Request.PreferRadius, Request.PreferStrength, Request.Seed ^ 0x7D2B4C91);
			}
		}
		Diagnostics.AfterInteractionRules = Points.Num();

		if (Points.Num() > Request.MaxSpawnPoints)
		{
			Points.SetNum(Request.MaxSpawnPoints);
		}

		Diagnostics.FinalPoints = Points.Num();
		Diagnostics.GenerationTimeMs = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		Diagnostics.bGenerationTimeExceeded = Request.MaxGenerationTimeMs > 0.0f && Diagnostics.GenerationTimeMs > (double)Request.MaxGenerationTimeMs;
		Diagnostics.SceneMetrics = FSceneEvaluator::EvaluateScene(Points, Bounds, Request.ClusterRadius);
		if (OutDiagnostics)
		{
			*OutDiagnostics = Diagnostics;
		}

		return Points;
	}

	static TArray<TSharedPtr<FJsonValue>> BuildPointSampleArray(const TArray<FVector>& Points, int32 MaxPoints = 64)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		const int32 Count = FMath::Min(MaxPoints, Points.Num());
		Arr.Reserve(Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Arr.Add(MakeShared<FJsonValueObject>(VecToObj(Points[Index])));
		}
		return Arr;
	}

	static TSharedPtr<FJsonObject> BuildDistributionDiagnosticsJson(const FDistributionDiagnostics& Diagnostics)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("requested_points"), Diagnostics.RequestedPoints);
		Obj->SetNumberField(TEXT("base_generated_points"), Diagnostics.BaseGeneratedPoints);
		Obj->SetNumberField(TEXT("after_height_filter"), Diagnostics.AfterHeightFilter);
		Obj->SetNumberField(TEXT("after_slope_filter"), Diagnostics.AfterSlopeFilter);
		Obj->SetNumberField(TEXT("after_distance_mask"), Diagnostics.AfterDistanceMask);
		Obj->SetNumberField(TEXT("after_density_gradient"), Diagnostics.AfterDensityGradient);
		Obj->SetNumberField(TEXT("after_clearings"), Diagnostics.AfterClearings);
		Obj->SetNumberField(TEXT("after_biome_filter"), Diagnostics.AfterBiomeFilter);
		Obj->SetNumberField(TEXT("after_interaction_rules"), Diagnostics.AfterInteractionRules);
		Obj->SetNumberField(TEXT("final_points"), Diagnostics.FinalPoints);
		Obj->SetNumberField(TEXT("clearing_count"), Diagnostics.ClearingCount);
		Obj->SetNumberField(TEXT("biome_seed_count"), Diagnostics.BiomeSeedCount);
		Obj->SetNumberField(TEXT("density_field_average"), Diagnostics.DensityFieldAverage);
		Obj->SetNumberField(TEXT("generation_time_ms"), Diagnostics.GenerationTimeMs);
		Obj->SetBoolField(TEXT("generation_time_exceeded"), Diagnostics.bGenerationTimeExceeded);

		TArray<TSharedPtr<FJsonValue>> BiomeCountsArr;
		for (const TPair<FString, int32>& Pair : Diagnostics.BiomeHistogram)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("biome"), Pair.Key);
			Entry->SetNumberField(TEXT("count"), Pair.Value);
			BiomeCountsArr.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Obj->SetArrayField(TEXT("biome_histogram"), BiomeCountsArr);

		TSharedPtr<FJsonObject> SceneObj = MakeShared<FJsonObject>();
		SceneObj->SetNumberField(TEXT("density_variance"), Diagnostics.SceneMetrics.DensityVarianceScore);
		SceneObj->SetNumberField(TEXT("cluster_score"), Diagnostics.SceneMetrics.ClusterScore);
		SceneObj->SetNumberField(TEXT("empty_space_score"), Diagnostics.SceneMetrics.EmptySpaceScore);
		SceneObj->SetNumberField(TEXT("visual_balance"), Diagnostics.SceneMetrics.VisualBalanceScore);
		SceneObj->SetNumberField(TEXT("combined_score"), Diagnostics.SceneMetrics.CombinedScore);
		Obj->SetObjectField(TEXT("scene_evaluation"), SceneObj);

		return Obj;
	}

	static bool ResolvePaletteIfPresent(const TSharedPtr<FJsonObject>& Args, FString& OutPaletteId, TSharedPtr<FJsonObject>& OutPalette, FString& OutPaletteError)
	{
		OutPaletteId = TEXT("");
		OutPalette.Reset();
		OutPaletteError = TEXT("");

		if (!Args.IsValid() || !Args->TryGetStringField(TEXT("palette_id"), OutPaletteId) || OutPaletteId.IsEmpty())
		{
			return true;
		}

		if (!FPaletteManager::LoadPaletteById(OutPaletteId, OutPalette, OutPaletteError))
		{
			return false;
		}
		return true;
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
	Root->SetNumberField(TEXT("max_spawn_points"), GOperatorPolicy.MaxSpawnPoints);
	Root->SetNumberField(TEXT("max_cluster_count"), GOperatorPolicy.MaxClusterCount);
	Root->SetNumberField(TEXT("max_generation_time_ms"), GOperatorPolicy.MaxGenerationTimeMs);
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
		if (Args->HasField(TEXT("max_spawn_points")))
		{
			GOperatorPolicy.MaxSpawnPoints = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_spawn_points")), 1, 500000);
		}
		if (Args->HasField(TEXT("max_cluster_count")))
		{
			GOperatorPolicy.MaxClusterCount = FMath::Clamp((int32)Args->GetNumberField(TEXT("max_cluster_count")), 1, 16384);
		}
		if (Args->HasField(TEXT("max_generation_time_ms")))
		{
			GOperatorPolicy.MaxGenerationTimeMs = FMath::Clamp((float)Args->GetNumberField(TEXT("max_generation_time_ms")), 10.0f, 600000.0f);
		}
	}
	return GetOperatorPolicy();
}

bool FProceduralOpsModule::IsOperatorOnlyMode()
{
	return GOperatorPolicy.bOperatorOnly;
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
	OperatorSupport->SetStringField(TEXT("terrain_generate"), TEXT("native_baseline"));
	OperatorSupport->SetStringField(TEXT("visual_intelligence"), TEXT("native_baseline"));
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

FString FProceduralOpsModule::TerrainGenerate(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return ErrorJson(TEXT("No editor world."));
	}

	const int32 Seed = (Args.IsValid() && Args->HasField(TEXT("seed"))) ? (int32)Args->GetNumberField(TEXT("seed")) : 48293;
	const int32 Width = (Args.IsValid() && Args->HasField(TEXT("width"))) ? (int32)Args->GetNumberField(TEXT("width")) : 257;
	const int32 Height = (Args.IsValid() && Args->HasField(TEXT("height"))) ? (int32)Args->GetNumberField(TEXT("height")) : 257;
	const float Frequency = (Args.IsValid() && Args->HasField(TEXT("frequency"))) ? (float)Args->GetNumberField(TEXT("frequency")) : 0.01f;
	const float Amplitude = (Args.IsValid() && Args->HasField(TEXT("amplitude"))) ? (float)Args->GetNumberField(TEXT("amplitude")) : 1.0f;
	const float RidgeStrength = (Args.IsValid() && Args->HasField(TEXT("ridge_strength"))) ? (float)Args->GetNumberField(TEXT("ridge_strength")) : 0.35f;
	const int32 ErosionIterations = (Args.IsValid() && Args->HasField(TEXT("erosion_iterations"))) ? (int32)Args->GetNumberField(TEXT("erosion_iterations")) : 16;
	const float ErosionStrength =
		(Args.IsValid() && Args->HasField(TEXT("erosion_strength"))) ? (float)Args->GetNumberField(TEXT("erosion_strength")) :
		((Args.IsValid() && Args->HasField(TEXT("sediment_strength"))) ? (float)Args->GetNumberField(TEXT("sediment_strength")) : 0.35f);
	const bool bSpawnLandscape = (Args.IsValid() && Args->HasField(TEXT("spawn_landscape"))) ? Args->GetBoolField(TEXT("spawn_landscape")) : false;

	TArray<float> Heightmap = FTerrainGenerator::GenerateHeightmap(Width, Height, Seed, Frequency, Amplitude);
	FTerrainGenerator::ApplyRidgedNoise(Heightmap, Width, Height, Seed ^ 0x18A1F6C3, RidgeStrength);
	FTerrainGenerator::ApplyErosion(Heightmap, Width, Height, ErosionIterations, ErosionStrength);
	FTerrainGenerator::NormalizeHeightmap(Heightmap, 0.0f, 1.0f);

	float MinH = TNumericLimits<float>::Max();
	float MaxH = TNumericLimits<float>::Lowest();
	float AvgH = 0.0f;
	for (const float Value : Heightmap)
	{
		MinH = FMath::Min(MinH, Value);
		MaxH = FMath::Max(MaxH, Value);
		AvgH += Value;
	}
	if (Heightmap.Num() > 0)
	{
		AvgH /= (float)Heightmap.Num();
	}

	bool bLandscapeSpawned = false;
	FString SpawnMessage = TEXT("spawn_landscape=false");
	if (bSpawnLandscape)
	{
		bLandscapeSpawned = FTerrainGenerator::SpawnLandscape(
			World,
			Heightmap,
			Width,
			Height,
			FVector::ZeroVector,
			FVector(100.0f, 100.0f, 100.0f),
			SpawnMessage);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("operator"), TEXT("terrain_generate"));
	Root->SetNumberField(TEXT("seed"), Seed);
	Root->SetNumberField(TEXT("width"), Width);
	Root->SetNumberField(TEXT("height"), Height);
	Root->SetNumberField(TEXT("frequency"), Frequency);
	Root->SetNumberField(TEXT("amplitude"), Amplitude);
	Root->SetNumberField(TEXT("ridge_strength"), RidgeStrength);
	Root->SetNumberField(TEXT("erosion_iterations"), ErosionIterations);
	Root->SetNumberField(TEXT("erosion_strength"), ErosionStrength);
	Root->SetNumberField(TEXT("sediment_strength"), ErosionStrength);
	Root->SetNumberField(TEXT("height_min"), MinH);
	Root->SetNumberField(TEXT("height_max"), MaxH);
	Root->SetNumberField(TEXT("height_avg"), AvgH);
	Root->SetBoolField(TEXT("landscape_spawned"), bLandscapeSpawned);
	Root->SetStringField(TEXT("landscape_message"), SpawnMessage);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
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

	const FDistributionRequest Distribution = ParseDistributionRequest(Args, TargetActor);
	FDistributionDiagnostics DistributionDiagnostics;
	const TArray<FVector> DistributionPoints = GenerateDistributionPoints(World, TargetActor, Distribution, &DistributionDiagnostics);

	FString PaletteId;
	TSharedPtr<FJsonObject> PaletteObj;
	FString PaletteError;
	if (!ResolvePaletteIfPresent(Args, PaletteId, PaletteObj, PaletteError))
	{
		return ErrorJson(PaletteError);
	}

	const TSet<FString> Reserved = {
		TEXT("target_label"), TEXT("pcg_volume_label"), TEXT("actor_label"), TEXT("target_actor"),
		TEXT("parameters"), TEXT("generate"),
		TEXT("distribution_mode"), TEXT("density"), TEXT("cluster_radius"), TEXT("min_spacing"),
		TEXT("height_range"), TEXT("slope_range"), TEXT("distance_mask"), TEXT("seed"),
		TEXT("point_count"), TEXT("max_points"), TEXT("cluster_count"),
		TEXT("max_spawn_points"), TEXT("max_cluster_count"), TEXT("max_generation_time_ms"),
		TEXT("density_sigma"), TEXT("density_noise"), TEXT("density_field_resolution"), TEXT("use_density_gradient"),
		TEXT("clearings"), TEXT("clearing_density"), TEXT("clearing_count"), TEXT("clearing_radius_min"), TEXT("clearing_radius_max"),
		TEXT("biome_count"), TEXT("biome_types"), TEXT("allowed_biomes"), TEXT("biome_blend_distance"),
		TEXT("avoid_points"), TEXT("avoid_radius"), TEXT("prefer_near_points"), TEXT("prefer_radius"), TEXT("prefer_strength"),
		TEXT("interaction_rules"),
		TEXT("palette_id")
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
	Root->SetStringField(TEXT("distribution_mode"), Distribution.Mode);
	Root->SetNumberField(TEXT("distribution_points"), DistributionPoints.Num());
	Root->SetArrayField(TEXT("distribution_point_sample"), BuildPointSampleArray(DistributionPoints));
	Root->SetObjectField(TEXT("distribution_diagnostics"), BuildDistributionDiagnosticsJson(DistributionDiagnostics));
	Root->SetBoolField(TEXT("generation_time_exceeded"), DistributionDiagnostics.bGenerationTimeExceeded);
	Root->SetNumberField(TEXT("scene_score"), DistributionDiagnostics.SceneMetrics.CombinedScore);
	if (!PaletteId.IsEmpty())
	{
		Root->SetStringField(TEXT("palette_id"), PaletteId);
		Root->SetBoolField(TEXT("palette_resolved"), PaletteObj.IsValid());
	}
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

	const FDistributionRequest Distribution = ParseDistributionRequest(Args, TargetActor);
	FDistributionDiagnostics DistributionDiagnostics;
	const TArray<FVector> DistributionPoints = GenerateDistributionPoints(World, TargetActor, Distribution, &DistributionDiagnostics);

	FString PaletteId;
	TSharedPtr<FJsonObject> PaletteObj;
	FString PaletteError;
	if (!ResolvePaletteIfPresent(Args, PaletteId, PaletteObj, PaletteError))
	{
		return ErrorJson(PaletteError);
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
	if (SplinePointCount == 0 && DistributionPoints.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> GeneratedPointValues;
		GeneratedPointValues.Reserve(DistributionPoints.Num());
		for (const FVector& Point : DistributionPoints)
		{
			GeneratedPointValues.Add(MakeShared<FJsonValueObject>(VecToObj(Point)));
		}
		SplinePointCount = ApplySplinePoints(TargetActor, GeneratedPointValues, bClosedLoop);
	}

	const TSet<FString> Reserved = {
		TEXT("spline_actor_label"), TEXT("target_label"), TEXT("actor_label"), TEXT("target_actor"),
		TEXT("control_points"), TEXT("spline_points"), TEXT("closed_loop"),
		TEXT("parameters"), TEXT("generate"),
		TEXT("distribution_mode"), TEXT("density"), TEXT("cluster_radius"), TEXT("min_spacing"),
		TEXT("height_range"), TEXT("slope_range"), TEXT("distance_mask"), TEXT("seed"),
		TEXT("point_count"), TEXT("max_points"), TEXT("cluster_count"),
		TEXT("max_spawn_points"), TEXT("max_cluster_count"), TEXT("max_generation_time_ms"),
		TEXT("density_sigma"), TEXT("density_noise"), TEXT("density_field_resolution"), TEXT("use_density_gradient"),
		TEXT("clearings"), TEXT("clearing_density"), TEXT("clearing_count"), TEXT("clearing_radius_min"), TEXT("clearing_radius_max"),
		TEXT("biome_count"), TEXT("biome_types"), TEXT("allowed_biomes"), TEXT("biome_blend_distance"),
		TEXT("avoid_points"), TEXT("avoid_radius"), TEXT("prefer_near_points"), TEXT("prefer_radius"), TEXT("prefer_strength"),
		TEXT("interaction_rules"),
		TEXT("palette_id")
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
	Root->SetStringField(TEXT("distribution_mode"), Distribution.Mode);
	Root->SetNumberField(TEXT("distribution_points"), DistributionPoints.Num());
	Root->SetArrayField(TEXT("distribution_point_sample"), BuildPointSampleArray(DistributionPoints));
	Root->SetObjectField(TEXT("distribution_diagnostics"), BuildDistributionDiagnosticsJson(DistributionDiagnostics));
	Root->SetBoolField(TEXT("generation_time_exceeded"), DistributionDiagnostics.bGenerationTimeExceeded);
	Root->SetNumberField(TEXT("scene_score"), DistributionDiagnostics.SceneMetrics.CombinedScore);
	if (!PaletteId.IsEmpty())
	{
		Root->SetStringField(TEXT("palette_id"), PaletteId);
		Root->SetBoolField(TEXT("palette_resolved"), PaletteObj.IsValid());
	}
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

	const FDistributionRequest Distribution = ParseDistributionRequest(Args, TargetActor);
	FDistributionDiagnostics DistributionDiagnostics;
	const TArray<FVector> DistributionPoints = GenerateDistributionPoints(World, TargetActor, Distribution, &DistributionDiagnostics);

	FString PaletteId;
	TSharedPtr<FJsonObject> PaletteObj;
	FString PaletteError;
	if (!ResolvePaletteIfPresent(Args, PaletteId, PaletteObj, PaletteError))
	{
		return ErrorJson(PaletteError);
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
		TEXT("layers"), TEXT("parameters"), TEXT("generate"),
		TEXT("distribution_mode"), TEXT("density"), TEXT("cluster_radius"), TEXT("min_spacing"),
		TEXT("height_range"), TEXT("slope_range"), TEXT("distance_mask"), TEXT("seed"),
		TEXT("point_count"), TEXT("max_points"), TEXT("cluster_count"),
		TEXT("max_spawn_points"), TEXT("max_cluster_count"), TEXT("max_generation_time_ms"),
		TEXT("density_sigma"), TEXT("density_noise"), TEXT("density_field_resolution"), TEXT("use_density_gradient"),
		TEXT("clearings"), TEXT("clearing_density"), TEXT("clearing_count"), TEXT("clearing_radius_min"), TEXT("clearing_radius_max"),
		TEXT("biome_count"), TEXT("biome_types"), TEXT("allowed_biomes"), TEXT("biome_blend_distance"),
		TEXT("avoid_points"), TEXT("avoid_radius"), TEXT("prefer_near_points"), TEXT("prefer_radius"), TEXT("prefer_strength"),
		TEXT("interaction_rules"),
		TEXT("palette_id")
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
	Root->SetStringField(TEXT("distribution_mode"), Distribution.Mode);
	Root->SetNumberField(TEXT("distribution_points"), DistributionPoints.Num());
	Root->SetArrayField(TEXT("distribution_point_sample"), BuildPointSampleArray(DistributionPoints));
	Root->SetObjectField(TEXT("distribution_diagnostics"), BuildDistributionDiagnosticsJson(DistributionDiagnostics));
	Root->SetBoolField(TEXT("generation_time_exceeded"), DistributionDiagnostics.bGenerationTimeExceeded);
	Root->SetNumberField(TEXT("scene_score"), DistributionDiagnostics.SceneMetrics.CombinedScore);
	if (!PaletteId.IsEmpty())
	{
		Root->SetStringField(TEXT("palette_id"), PaletteId);
		Root->SetBoolField(TEXT("palette_resolved"), PaletteObj.IsValid());
	}
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

	const bool bAllowMenuLevel = Args.IsValid() && Args->HasField(TEXT("allow_menu_level")) && Args->GetBoolField(TEXT("allow_menu_level"));
	const FString PackagePath = World->GetOutermost() ? World->GetOutermost()->GetName() : FString();
	if (!bAllowMenuLevel && PackagePath.Contains(TEXT("MenuLevel"), ESearchCase::IgnoreCase))
	{
		return ErrorJson(TEXT("run_operator_pipeline blocked on MenuLevel. Load a gameplay/validation level first or pass allow_menu_level=true."));
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
	float MaxGenerationTimeMs = GOperatorPolicy.MaxGenerationTimeMs;
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
		if (Args->HasField(TEXT("max_generation_time_ms")))
		{
			MaxGenerationTimeMs = FMath::Clamp((float)Args->GetNumberField(TEXT("max_generation_time_ms")), 10.0f, 600000.0f);
		}
		if (Args->HasField(TEXT("stop_on_error")))
		{
			bStopOnError = Args->GetBoolField(TEXT("stop_on_error"));
		}
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEAgentForge", "RunOperatorPipeline", "AgentForge: Run Operator Pipeline"));
	TArray<TSharedPtr<FJsonValue>> StageResults;
	bool bAnyFailure = false;
	bool bTimeBudgetExceeded = false;
	FString TimeBudgetFailureReason;
	const double PipelineStartSeconds = FPlatformTime::Seconds();

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
			if (Args->HasField(TEXT("distribution_mode")) && !CopyObj->HasField(TEXT("distribution_mode")))
			{
				CopyObj->SetStringField(TEXT("distribution_mode"), Args->GetStringField(TEXT("distribution_mode")));
			}
			if (Args->HasField(TEXT("density")) && !CopyObj->HasField(TEXT("density")))
			{
				CopyObj->SetNumberField(TEXT("density"), Args->GetNumberField(TEXT("density")));
			}
			if (Args->HasField(TEXT("cluster_radius")) && !CopyObj->HasField(TEXT("cluster_radius")))
			{
				CopyObj->SetNumberField(TEXT("cluster_radius"), Args->GetNumberField(TEXT("cluster_radius")));
			}
			if (Args->HasField(TEXT("min_spacing")) && !CopyObj->HasField(TEXT("min_spacing")))
			{
				CopyObj->SetNumberField(TEXT("min_spacing"), Args->GetNumberField(TEXT("min_spacing")));
			}
			for (const TCHAR* SharedFieldName : {
					TEXT("height_range"), TEXT("slope_range"), TEXT("distance_mask"), TEXT("point_count"), TEXT("max_points"), TEXT("cluster_count"),
					TEXT("max_spawn_points"), TEXT("max_cluster_count"), TEXT("max_generation_time_ms"),
					TEXT("density_sigma"), TEXT("density_noise"), TEXT("density_field_resolution"), TEXT("use_density_gradient"),
					TEXT("clearings"), TEXT("clearing_density"), TEXT("clearing_count"), TEXT("clearing_radius_min"), TEXT("clearing_radius_max"),
					TEXT("biome_count"), TEXT("biome_types"), TEXT("allowed_biomes"), TEXT("biome_blend_distance"),
					TEXT("avoid_points"), TEXT("avoid_radius"), TEXT("prefer_near_points"), TEXT("prefer_radius"), TEXT("prefer_strength"),
					TEXT("interaction_rules") })
			{
				const FString SharedField(SharedFieldName);
				if (!CopyObj->HasField(SharedField))
				{
					const TSharedPtr<FJsonValue>* Value = Args->Values.Find(SharedField);
					if (Value && Value->IsValid())
					{
						CopyObj->SetField(SharedField, *Value);
					}
				}
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
			if (Args->HasField(TEXT("distribution_mode")) && !CopyObj->HasField(TEXT("distribution_mode")))
			{
				CopyObj->SetStringField(TEXT("distribution_mode"), Args->GetStringField(TEXT("distribution_mode")));
			}
			if (Args->HasField(TEXT("density")) && !CopyObj->HasField(TEXT("density")))
			{
				CopyObj->SetNumberField(TEXT("density"), Args->GetNumberField(TEXT("density")));
			}
			if (Args->HasField(TEXT("cluster_radius")) && !CopyObj->HasField(TEXT("cluster_radius")))
			{
				CopyObj->SetNumberField(TEXT("cluster_radius"), Args->GetNumberField(TEXT("cluster_radius")));
			}
			if (Args->HasField(TEXT("min_spacing")) && !CopyObj->HasField(TEXT("min_spacing")))
			{
				CopyObj->SetNumberField(TEXT("min_spacing"), Args->GetNumberField(TEXT("min_spacing")));
			}
			for (const TCHAR* SharedFieldName : {
					TEXT("height_range"), TEXT("slope_range"), TEXT("distance_mask"), TEXT("point_count"), TEXT("max_points"), TEXT("cluster_count"),
					TEXT("max_spawn_points"), TEXT("max_cluster_count"), TEXT("max_generation_time_ms"),
					TEXT("density_sigma"), TEXT("density_noise"), TEXT("density_field_resolution"), TEXT("use_density_gradient"),
					TEXT("clearings"), TEXT("clearing_density"), TEXT("clearing_count"), TEXT("clearing_radius_min"), TEXT("clearing_radius_max"),
					TEXT("biome_count"), TEXT("biome_types"), TEXT("allowed_biomes"), TEXT("biome_blend_distance"),
					TEXT("avoid_points"), TEXT("avoid_radius"), TEXT("prefer_near_points"), TEXT("prefer_radius"), TEXT("prefer_strength"),
					TEXT("interaction_rules") })
			{
				const FString SharedField(SharedFieldName);
				if (!CopyObj->HasField(SharedField))
				{
					const TSharedPtr<FJsonValue>* Value = Args->Values.Find(SharedField);
					if (Value && Value->IsValid())
					{
						CopyObj->SetField(SharedField, *Value);
					}
				}
			}
			return CopyObj;
		}

		return nullptr;
	};

	auto RunStage = [&](const FString& Name, const TSharedPtr<FJsonObject>& StageArgs, TFunctionRef<FString(const TSharedPtr<FJsonObject>&)> Fn)
	{
		if (bTimeBudgetExceeded)
		{
			return;
		}
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

		const double ElapsedMs = (FPlatformTime::Seconds() - PipelineStartSeconds) * 1000.0;
		Parsed->SetNumberField(TEXT("pipeline_elapsed_ms"), ElapsedMs);
		if (MaxGenerationTimeMs > 0.0f && ElapsedMs > (double)MaxGenerationTimeMs)
		{
			bTimeBudgetExceeded = true;
			bAnyFailure = true;
			TimeBudgetFailureReason = FString::Printf(
				TEXT("Generation time %.1f ms exceeded max_generation_time_ms %.1f ms."),
				ElapsedMs,
				MaxGenerationTimeMs);
			Parsed->SetBoolField(TEXT("time_budget_exceeded"), true);
			Parsed->SetStringField(TEXT("error"), TimeBudgetFailureReason);
		}

		StageResults.Add(MakeShared<FJsonValueObject>(Parsed));
	};

	RunStage(TEXT("terrain_generate"), BuildStageArgs(TEXT("terrain_generate"), TEXT("terrain")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::TerrainGenerate(StageArgs); });
	RunStage(TEXT("road_layout"), BuildStageArgs(TEXT("road_layout"), TEXT("roads")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::RoadLayout(StageArgs); });
	RunStage(TEXT("biome_layers"), BuildStageArgs(TEXT("biome_layers"), TEXT("biomes")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::BiomeLayers(StageArgs); });
	RunStage(TEXT("surface_scatter"), BuildStageArgs(TEXT("surface_scatter"), TEXT("surface")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::SurfaceScatter(StageArgs); });
	RunStage(TEXT("spline_scatter"), BuildStageArgs(TEXT("spline_scatter"), TEXT("spline")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::SplineScatter(StageArgs); });
	RunStage(TEXT("stamp_poi"), BuildStageArgs(TEXT("stamp_poi"), TEXT("poi")), [](const TSharedPtr<FJsonObject>& StageArgs) { return FProceduralOpsModule::StampPOI(StageArgs); });

	if (bTimeBudgetExceeded)
	{
		Transaction.Cancel();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), TimeBudgetFailureReason);
		Root->SetArrayField(TEXT("stages"), StageResults);
		Root->SetBoolField(TEXT("rolled_back"), true);
		Root->SetNumberField(TEXT("max_generation_time_ms"), MaxGenerationTimeMs);
		Root->SetNumberField(TEXT("pipeline_elapsed_ms"), (FPlatformTime::Seconds() - PipelineStartSeconds) * 1000.0);
		return ToJson(Root);
	}

	if (bAnyFailure && bStopOnError)
	{
		Transaction.Cancel();
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), TimeBudgetFailureReason.IsEmpty() ? TEXT("Pipeline halted after a stage error.") : TimeBudgetFailureReason);
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
	Root->SetNumberField(TEXT("max_generation_time_ms"), MaxGenerationTimeMs);
	Root->SetNumberField(TEXT("pipeline_elapsed_ms"), (FPlatformTime::Seconds() - PipelineStartSeconds) * 1000.0);
	return ToJson(Root);
#else
	return ErrorJson(TEXT("WITH_EDITOR required."));
#endif
}
