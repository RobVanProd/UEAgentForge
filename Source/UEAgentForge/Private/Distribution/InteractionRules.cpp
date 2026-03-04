// Copyright UEAgentForge Project. All Rights Reserved.
// InteractionRules.cpp - overlap avoidance and attraction shaping.

#include "Distribution/InteractionRules.h"

#include "Math/RandomStream.h"

TArray<FVector> FInteractionRules::ApplyAvoidance(
	const TArray<FVector>& CandidatePoints,
	const TArray<FVector>& BlockingPoints,
	float MinDistance)
{
	if (CandidatePoints.Num() == 0 || BlockingPoints.Num() == 0)
	{
		return CandidatePoints;
	}

	const float RadiusSq = FMath::Square(FMath::Max(1.0f, MinDistance));
	TArray<FVector> Filtered;
	Filtered.Reserve(CandidatePoints.Num());

	for (const FVector& Candidate : CandidatePoints)
	{
		bool bBlocked = false;
		for (const FVector& Blocking : BlockingPoints)
		{
			if (FVector::DistSquared2D(Candidate, Blocking) < RadiusSq)
			{
				bBlocked = true;
				break;
			}
		}

		if (!bBlocked)
		{
			Filtered.Add(Candidate);
		}
	}

	return Filtered;
}

TArray<FVector> FInteractionRules::ApplyAttractorBias(
	const TArray<FVector>& CandidatePoints,
	const TArray<FVector>& AttractorPoints,
	float AttractionRadius,
	float AttractionStrength,
	int32 Seed)
{
	if (CandidatePoints.Num() == 0 || AttractorPoints.Num() == 0)
	{
		return CandidatePoints;
	}

	const float Radius = FMath::Max(1.0f, AttractionRadius);
	const float RadiusSq = Radius * Radius;
	const float Strength = FMath::Clamp(AttractionStrength, 0.0f, 1.0f);
	FRandomStream Rng(Seed ^ 0x51F15E3D);

	TArray<FVector> Filtered;
	Filtered.Reserve(CandidatePoints.Num());

	for (const FVector& Candidate : CandidatePoints)
	{
		float BestDistSq = TNumericLimits<float>::Max();
		for (const FVector& Attractor : AttractorPoints)
		{
			BestDistSq = FMath::Min(BestDistSq, FVector::DistSquared2D(Candidate, Attractor));
		}

		if (BestDistSq <= RadiusSq)
		{
			Filtered.Add(Candidate);
			continue;
		}

		const float Dist = FMath::Sqrt(FMath::Max(0.0f, BestDistSq));
		const float Falloff = FMath::Clamp(1.0f - ((Dist - Radius) / FMath::Max(1.0f, Radius)), 0.0f, 1.0f);
		const float KeepProbability = FMath::Lerp(1.0f - Strength, 1.0f, Falloff);
		if (Rng.FRand() <= KeepProbability)
		{
			Filtered.Add(Candidate);
		}
	}

	return Filtered;
}

TArray<FVector> FInteractionRules::ApplySelfSpacing(
	const TArray<FVector>& CandidatePoints,
	float MinDistance)
{
	if (CandidatePoints.Num() == 0)
	{
		return CandidatePoints;
	}

	const float RadiusSq = FMath::Square(FMath::Max(1.0f, MinDistance));
	TArray<FVector> Filtered;
	Filtered.Reserve(CandidatePoints.Num());

	for (const FVector& Candidate : CandidatePoints)
	{
		bool bTooClose = false;
		for (const FVector& Existing : Filtered)
		{
			if (FVector::DistSquared2D(Candidate, Existing) < RadiusSq)
			{
				bTooClose = true;
				break;
			}
		}
		if (!bTooClose)
		{
			Filtered.Add(Candidate);
		}
	}

	return Filtered;
}
