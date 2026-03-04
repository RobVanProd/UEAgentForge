// Copyright UEAgentForge Project. All Rights Reserved.
// ErosionSim - deterministic thermal erosion simulation.

#pragma once

#include "CoreMinimal.h"

class UEAGENTFORGE_API FErosionSim
{
public:
	static void ApplyThermalErosion(
		TArray<float>& Heightmap,
		int32 Width,
		int32 Height,
		int32 Iterations,
		float TalusThreshold,
		float SedimentStrength);
};
