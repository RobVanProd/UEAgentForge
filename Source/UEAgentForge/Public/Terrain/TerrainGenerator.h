// Copyright UEAgentForge Project. All Rights Reserved.
// TerrainGenerator - heightmap-oriented terrain build utilities.

#pragma once

#include "CoreMinimal.h"

class UWorld;

class UEAGENTFORGE_API FTerrainGenerator
{
public:
	static TArray<float> GenerateHeightmap(
		int32 Width,
		int32 Height,
		int32 Seed,
		float Frequency,
		float Amplitude);

	static void ApplyRidgedNoise(
		TArray<float>& Heightmap,
		int32 Width,
		int32 Height,
		int32 Seed,
		float Strength);

	static void ApplyErosion(
		TArray<float>& Heightmap,
		int32 Width,
		int32 Height,
		int32 Iterations,
		float Strength);

	static void NormalizeHeightmap(
		TArray<float>& Heightmap,
		float MinOut = 0.0f,
		float MaxOut = 1.0f);

	static bool SpawnLandscape(
		UWorld* World,
		const TArray<float>& Heightmap,
		int32 Width,
		int32 Height,
		const FVector& Origin,
		const FVector& Scale,
		FString& OutMessage);
};

