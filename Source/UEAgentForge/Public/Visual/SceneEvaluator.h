// Copyright UEAgentForge Project. All Rights Reserved.
// SceneEvaluator - structural visual-quality metrics for generated scenes.

#pragma once

#include "CoreMinimal.h"

struct UEAGENTFORGE_API FSceneEvaluationMetrics
{
	float DensityVarianceScore = 0.0f;
	float ClusterScore = 0.0f;
	float EmptySpaceScore = 0.0f;
	float VisualBalanceScore = 0.0f;
	float CombinedScore = 0.0f;
};

class UEAGENTFORGE_API FSceneEvaluator
{
public:
	static float ComputeObjectDensityVariance(
		const TArray<FVector>& Points,
		const FBox& Bounds,
		int32 GridResolution = 8);

	static float ComputeClusterScore(
		const TArray<FVector>& Points,
		float ClusterRadius);

	static float ComputeEmptySpaceScore(
		const TArray<FVector>& Points,
		const FBox& Bounds,
		float TargetEmptyRatio = 0.25f,
		int32 GridResolution = 10);

	static float ComputeVisualBalance(
		const TArray<FVector>& Points,
		const FBox& Bounds);

	static FSceneEvaluationMetrics EvaluateScene(
		const TArray<FVector>& Points,
		const FBox& Bounds,
		float ClusterRadius);
};
