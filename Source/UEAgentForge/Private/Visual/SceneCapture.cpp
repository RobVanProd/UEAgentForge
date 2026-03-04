// Copyright UEAgentForge Project. All Rights Reserved.
// SceneCapture.cpp - baseline visual scoring helpers.

#include "Visual/SceneCapture.h"

#include "Engine/World.h"

bool FSceneCapture::CaptureSceneImages(
	UWorld* World,
	const TArray<FTransform>& CameraTransforms,
	const FString& OutputDirectory,
	TArray<FString>& OutImagePaths,
	FString& OutError)
{
	OutImagePaths.Reset();
	OutError.Empty();

	if (!World)
	{
		OutError = TEXT("CaptureSceneImages failed: invalid world.");
		return false;
	}
	if (CameraTransforms.Num() == 0)
	{
		OutError = TEXT("CaptureSceneImages failed: no camera transforms provided.");
		return false;
	}
	if (OutputDirectory.IsEmpty())
	{
		OutError = TEXT("CaptureSceneImages failed: output directory is empty.");
		return false;
	}

	// Phase 2 skeleton: SceneCapture2D export pipeline will be wired here.
	OutError = TEXT("CaptureSceneImages not implemented yet.");
	return false;
}

float FSceneCapture::ComputeCompositionScore(const TArray<float>& Metrics)
{
	if (Metrics.Num() == 0)
	{
		return 0.0f;
	}

	float Sum = 0.0f;
	for (const float Value : Metrics)
	{
		Sum += FMath::Clamp(Value, 0.0f, 1.0f);
	}
	return Sum / (float)Metrics.Num();
}

float FSceneCapture::ComputeDensityScore(int32 ActorCount, float BoundsAreaM2)
{
	const float SafeArea = FMath::Max(1.0f, BoundsAreaM2);
	const float Density = (float)FMath::Max(0, ActorCount) / SafeArea;
	return FMath::Clamp(Density * 100.0f, 0.0f, 100.0f);
}

float FSceneCapture::ComputeSceneComplexity(int32 ActorCount, int32 ComponentCount, float DrawCalls)
{
	const float ActorTerm = (float)FMath::Max(0, ActorCount) * 0.03f;
	const float ComponentTerm = (float)FMath::Max(0, ComponentCount) * 0.01f;
	const float DrawCallTerm = FMath::Max(0.0f, DrawCalls) * 0.02f;
	return FMath::Clamp(ActorTerm + ComponentTerm + DrawCallTerm, 0.0f, 100.0f);
}

float FSceneCapture::ComputeImageSimilarity(const TArray<float>& FeaturesA, const TArray<float>& FeaturesB)
{
	if (FeaturesA.Num() == 0 || FeaturesA.Num() != FeaturesB.Num())
	{
		return 0.0f;
	}

	float Dot = 0.0f;
	float LenA = 0.0f;
	float LenB = 0.0f;
	for (int32 Index = 0; Index < FeaturesA.Num(); ++Index)
	{
		const float A = FeaturesA[Index];
		const float B = FeaturesB[Index];
		Dot += A * B;
		LenA += A * A;
		LenB += B * B;
	}

	const float Denom = FMath::Sqrt(LenA * LenB);
	if (Denom <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}
	return FMath::Clamp(Dot / Denom, 0.0f, 1.0f);
}

