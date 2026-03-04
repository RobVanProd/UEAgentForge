// Copyright UEAgentForge Project. All Rights Reserved.
// Clearings - deterministic negative-space generation for natural biomes.

#pragma once

#include "CoreMinimal.h"

struct UEAGENTFORGE_API FClearingRegion
{
	FVector Center = FVector::ZeroVector;
	float Radius = 300.0f;
};

class UEAGENTFORGE_API FClearings
{
public:
	static TArray<FClearingRegion> GenerateClearings(
		const FBox& Bounds,
		int32 ClearingCount,
		float MinRadius,
		float MaxRadius,
		int32 Seed);

	static TArray<FVector> ApplyClearingMask(
		const TArray<FVector>& Points,
		const TArray<FClearingRegion>& Clearings);
};
