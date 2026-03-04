// Copyright UEAgentForge Project. All Rights Reserved.
// DensityField - deterministic density gradients for natural scatter variation.

#pragma once

#include "CoreMinimal.h"

struct UEAGENTFORGE_API FDensityFieldConfig
{
	float BaseDensity = 1.0f;
	float Sigma = 3000.0f;
	FVector Center = FVector::ZeroVector;
	float NoiseBlend = 0.15f;
	int32 Seed = 1337;
};

class UEAGENTFORGE_API FDensityField
{
public:
	static TArray<float> GenerateDensityField(
		const FBox& Bounds,
		int32 Width,
		int32 Height,
		const FDensityFieldConfig& Config);

	static float SampleDensity(
		const TArray<float>& Field,
		int32 Width,
		int32 Height,
		const FBox& Bounds,
		const FVector& Location,
		float FallbackDensity = 1.0f);

	static TArray<FVector> ApplyDensityGradient(
		const TArray<FVector>& Points,
		const TArray<float>& Field,
		int32 Width,
		int32 Height,
		const FBox& Bounds,
		int32 Seed,
		float MinKeepProbability = 0.05f);
};
