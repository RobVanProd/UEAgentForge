// Copyright UEAgentForge Project. All Rights Reserved.
// BiomePartition - deterministic Voronoi biome partitioning helpers.

#pragma once

#include "CoreMinimal.h"

struct UEAGENTFORGE_API FBiomeSeedPoint
{
	FVector Position = FVector::ZeroVector;
	FString BiomeType = TEXT("forest");
};

struct UEAGENTFORGE_API FBiomeBlendSample
{
	FString PrimaryBiome = TEXT("forest");
	FString SecondaryBiome = TEXT("");
	float BlendAlpha = 0.0f;
};

struct UEAGENTFORGE_API FBiomePartitionData
{
	FBox Bounds;
	TArray<FBiomeSeedPoint> Seeds;
	float BlendDistance = 300.0f;
	int32 Seed = 1337;
};

class UEAGENTFORGE_API FBiomePartition
{
public:
	static FBiomePartitionData GenerateVoronoiBiomes(
		const FBox& Bounds,
		int32 BiomeCount,
		const TArray<FString>& BiomeTypes,
		int32 Seed,
		float BlendDistance);

	static FString SampleBiomeAtLocation(
		const FBiomePartitionData& Partition,
		const FVector& Location);

	static FBiomeBlendSample BlendBiomeEdges(
		const FBiomePartitionData& Partition,
		const FVector& Location);
};
