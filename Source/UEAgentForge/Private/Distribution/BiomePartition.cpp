// Copyright UEAgentForge Project. All Rights Reserved.
// BiomePartition.cpp - Voronoi partitioning for biome-aware placement.

#include "Distribution/BiomePartition.h"

#include "Math/RandomStream.h"

namespace
{
	static TArray<FString> BuildBiomeList(const TArray<FString>& InBiomes)
	{
		TArray<FString> Result;
		for (const FString& Name : InBiomes)
		{
			if (!Name.IsEmpty())
			{
				Result.Add(Name.ToLower());
			}
		}
		if (Result.Num() == 0)
		{
			Result = { TEXT("forest"), TEXT("meadow"), TEXT("rock_field"), TEXT("wetland") };
		}
		return Result;
	}

	static void FindClosestSeeds(
		const FBiomePartitionData& Partition,
		const FVector& Location,
		int32& OutNearest,
		int32& OutSecondNearest,
		float& OutNearestDistSq,
		float& OutSecondNearestDistSq)
	{
		OutNearest = INDEX_NONE;
		OutSecondNearest = INDEX_NONE;
		OutNearestDistSq = TNumericLimits<float>::Max();
		OutSecondNearestDistSq = TNumericLimits<float>::Max();

		for (int32 Index = 0; Index < Partition.Seeds.Num(); ++Index)
		{
			const float DistSq = FVector::DistSquared2D(Partition.Seeds[Index].Position, Location);
			if (DistSq < OutNearestDistSq)
			{
				OutSecondNearestDistSq = OutNearestDistSq;
				OutSecondNearest = OutNearest;
				OutNearestDistSq = DistSq;
				OutNearest = Index;
			}
			else if (DistSq < OutSecondNearestDistSq)
			{
				OutSecondNearestDistSq = DistSq;
				OutSecondNearest = Index;
			}
		}
	}
}

FBiomePartitionData FBiomePartition::GenerateVoronoiBiomes(
	const FBox& Bounds,
	int32 BiomeCount,
	const TArray<FString>& BiomeTypes,
	int32 Seed,
	float BlendDistance)
{
	FBiomePartitionData Partition;
	Partition.Bounds = Bounds;
	Partition.BlendDistance = FMath::Max(0.0f, BlendDistance);
	Partition.Seed = Seed;

	if (!Bounds.IsValid)
	{
		return Partition;
	}

	const TArray<FString> ResolvedBiomes = BuildBiomeList(BiomeTypes);
	const int32 Count = FMath::Clamp(BiomeCount, 1, 256);

	FRandomStream Rng(Seed);
	Partition.Seeds.Reserve(Count);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FBiomeSeedPoint Cell;
		Cell.Position = FVector(
			Rng.FRandRange(Bounds.Min.X, Bounds.Max.X),
			Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y),
			Bounds.Min.Z);
		Cell.BiomeType = ResolvedBiomes[Index % ResolvedBiomes.Num()];
		Partition.Seeds.Add(Cell);
	}

	return Partition;
}

FString FBiomePartition::SampleBiomeAtLocation(
	const FBiomePartitionData& Partition,
	const FVector& Location)
{
	if (Partition.Seeds.Num() == 0)
	{
		return TEXT("forest");
	}

	int32 Nearest = INDEX_NONE;
	int32 SecondNearest = INDEX_NONE;
	float NearestDistSq = 0.0f;
	float SecondNearestDistSq = 0.0f;
	FindClosestSeeds(Partition, Location, Nearest, SecondNearest, NearestDistSq, SecondNearestDistSq);

	return Partition.Seeds.IsValidIndex(Nearest)
		? Partition.Seeds[Nearest].BiomeType
		: Partition.Seeds[0].BiomeType;
}

FBiomeBlendSample FBiomePartition::BlendBiomeEdges(
	const FBiomePartitionData& Partition,
	const FVector& Location)
{
	FBiomeBlendSample Sample;
	if (Partition.Seeds.Num() == 0)
	{
		return Sample;
	}

	int32 Nearest = INDEX_NONE;
	int32 SecondNearest = INDEX_NONE;
	float NearestDistSq = 0.0f;
	float SecondNearestDistSq = 0.0f;
	FindClosestSeeds(Partition, Location, Nearest, SecondNearest, NearestDistSq, SecondNearestDistSq);

	if (Partition.Seeds.IsValidIndex(Nearest))
	{
		Sample.PrimaryBiome = Partition.Seeds[Nearest].BiomeType;
	}
	if (Partition.Seeds.IsValidIndex(SecondNearest))
	{
		Sample.SecondaryBiome = Partition.Seeds[SecondNearest].BiomeType;
	}

	if (!Sample.SecondaryBiome.IsEmpty() && Partition.BlendDistance > KINDA_SMALL_NUMBER)
	{
		const float D0 = FMath::Sqrt(FMath::Max(0.0f, NearestDistSq));
		const float D1 = FMath::Sqrt(FMath::Max(0.0f, SecondNearestDistSq));
		const float EdgeDistance = FMath::Abs(D1 - D0);
		Sample.BlendAlpha = FMath::Clamp((Partition.BlendDistance - EdgeDistance) / Partition.BlendDistance, 0.0f, 1.0f);
	}
	else
	{
		Sample.BlendAlpha = 0.0f;
	}

	return Sample;
}
