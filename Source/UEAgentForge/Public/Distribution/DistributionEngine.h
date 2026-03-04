// Copyright UEAgentForge Project. All Rights Reserved.
// DistributionEngine - shared spatial sampling and filtering utilities.

#pragma once

#include "CoreMinimal.h"

class UWorld;

class UEAGENTFORGE_API FDistributionEngine
{
public:
	static TArray<FVector> GenerateBlueNoisePoints(
		const FBox& Bounds,
		int32 TargetCount,
		int32 Seed,
		float MinSpacing);

	static TArray<FVector> GenerateClusterPoints(
		const FBox& Bounds,
		int32 TargetCount,
		int32 ClusterCount,
		float ClusterRadius,
		int32 Seed);

	static TArray<FVector> GeneratePoissonDiskPoints(
		const FBox& Bounds,
		int32 TargetCount,
		float MinSpacing,
		int32 Seed);

	// Backward-compatible alias.
	static TArray<FVector> GeneratePoissonPoints(
		const FBox& Bounds,
		int32 TargetCount,
		float MinSpacing,
		int32 Seed);

	static TArray<FVector> ApplySlopeFilter(
		const TArray<FVector>& Points,
		UWorld* World,
		float MinSlopeDegrees,
		float MaxSlopeDegrees);

	static TArray<FVector> ApplyHeightFilter(
		const TArray<FVector>& Points,
		float MinHeight,
		float MaxHeight);

	static TArray<FVector> ApplyDistanceMask(
		const TArray<FVector>& Points,
		const FVector& Origin,
		float MinDistance,
		float MaxDistance);
};
