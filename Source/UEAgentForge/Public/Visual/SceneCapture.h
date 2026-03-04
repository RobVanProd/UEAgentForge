// Copyright UEAgentForge Project. All Rights Reserved.
// SceneCapture - visual scoring and capture scaffolding.

#pragma once

#include "CoreMinimal.h"

class UWorld;

class UEAGENTFORGE_API FSceneCapture
{
public:
	static bool CaptureSceneImages(
		UWorld* World,
		const TArray<FTransform>& CameraTransforms,
		const FString& OutputDirectory,
		TArray<FString>& OutImagePaths,
		FString& OutError);

	static float ComputeCompositionScore(const TArray<float>& Metrics);
	static float ComputeDensityScore(int32 ActorCount, float BoundsAreaM2);
	static float ComputeSceneComplexity(int32 ActorCount, int32 ComponentCount, float DrawCalls);
	static float ComputeImageSimilarity(const TArray<float>& FeaturesA, const TArray<float>& FeaturesB);
};

