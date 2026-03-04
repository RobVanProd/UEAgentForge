// Copyright UEAgentForge Project. All Rights Reserved.
// Clearings.cpp - open-space exclusion masks.

#include "Distribution/Clearings.h"

#include "Distribution/DistributionEngine.h"
#include "Math/RandomStream.h"

TArray<FClearingRegion> FClearings::GenerateClearings(
	const FBox& Bounds,
	int32 ClearingCount,
	float MinRadius,
	float MaxRadius,
	int32 Seed)
{
	TArray<FClearingRegion> Regions;
	if (!Bounds.IsValid)
	{
		return Regions;
	}

	const int32 Count = FMath::Clamp(ClearingCount, 0, 2048);
	if (Count <= 0)
	{
		return Regions;
	}

	const float RadiusLow = FMath::Max(50.0f, FMath::Min(MinRadius, MaxRadius));
	const float RadiusHigh = FMath::Max(RadiusLow, FMath::Max(MinRadius, MaxRadius));
	const float MeanRadius = 0.5f * (RadiusLow + RadiusHigh);

	const TArray<FVector> Centers = FDistributionEngine::GenerateBlueNoisePoints(
		Bounds,
		Count,
		Seed ^ 0x3C6EF35F,
		FMath::Max(80.0f, MeanRadius * 1.5f));

	FRandomStream Rng(Seed ^ 0x7F4A7C15);
	Regions.Reserve(Centers.Num());
	for (const FVector& Center : Centers)
	{
		FClearingRegion Region;
		Region.Center = Center;
		Region.Radius = Rng.FRandRange(RadiusLow, RadiusHigh);
		Regions.Add(Region);
	}

	return Regions;
}

TArray<FVector> FClearings::ApplyClearingMask(
	const TArray<FVector>& Points,
	const TArray<FClearingRegion>& Clearings)
{
	if (Points.Num() == 0 || Clearings.Num() == 0)
	{
		return Points;
	}

	TArray<FVector> Filtered;
	Filtered.Reserve(Points.Num());

	for (const FVector& Point : Points)
	{
		bool bInsideClearing = false;
		for (const FClearingRegion& Region : Clearings)
		{
			if (FVector::DistSquared2D(Point, Region.Center) <= FMath::Square(FMath::Max(1.0f, Region.Radius)))
			{
				bInsideClearing = true;
				break;
			}
		}

		if (!bInsideClearing)
		{
			Filtered.Add(Point);
		}
	}

	return Filtered;
}
