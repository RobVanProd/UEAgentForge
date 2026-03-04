// Copyright UEAgentForge Project. All Rights Reserved.
// ProceduralOpsModule - deterministic operator pipeline for world generation.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Operator-centric procedural command surface.
 *
 * Design goal:
 * - AI chooses seeds/palettes/parameters.
 * - Unreal procedural systems (PCG/splines/road/biome tools) execute placement.
 * - Avoid direct atomic object salad placement for world content.
 */
class UEAGENTFORGE_API FProceduralOpsModule
{
public:
	// Capability and policy
	static FString GetProceduralCapabilities(const TSharedPtr<FJsonObject>& Args);
	static FString GetOperatorPolicy();
	static FString SetOperatorPolicy(const TSharedPtr<FJsonObject>& Args);
	static bool IsOperatorOnlyMode();

	// Constrained operators
	static FString TerrainGenerate(const TSharedPtr<FJsonObject>& Args);
	static FString SurfaceScatter(const TSharedPtr<FJsonObject>& Args);
	static FString SplineScatter(const TSharedPtr<FJsonObject>& Args);
	static FString RoadLayout(const TSharedPtr<FJsonObject>& Args);
	static FString BiomeLayers(const TSharedPtr<FJsonObject>& Args);
	static FString StampPOI(const TSharedPtr<FJsonObject>& Args);

	// Deterministic orchestration
	static FString RunOperatorPipeline(const TSharedPtr<FJsonObject>& Args);
};

