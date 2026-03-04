// Copyright UEAgentForge Project. All Rights Reserved.
// DistributionEngine.cpp - spatial point generation and filtering.

#include "Distribution/DistributionEngine.h"

#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Math/RandomStream.h"

namespace
{
	static FVector SamplePointInBounds(const FBox& Bounds, FRandomStream& Rng)
	{
		return FVector(
			Rng.FRandRange(Bounds.Min.X, Bounds.Max.X),
			Rng.FRandRange(Bounds.Min.Y, Bounds.Max.Y),
			Rng.FRandRange(Bounds.Min.Z, Bounds.Max.Z));
	}

	static bool IsPointWithinBounds2D(const FVector& Point, const FBox& Bounds)
	{
		return Point.X >= Bounds.Min.X && Point.X <= Bounds.Max.X &&
			   Point.Y >= Bounds.Min.Y && Point.Y <= Bounds.Max.Y;
	}
}

TArray<FVector> FDistributionEngine::GenerateBlueNoisePoints(
	const FBox& Bounds,
	int32 TargetCount,
	int32 Seed,
	float MinSpacing)
{
	return GeneratePoissonDiskPoints(Bounds, TargetCount, MinSpacing, Seed);
}

TArray<FVector> FDistributionEngine::GenerateClusterPoints(
	const FBox& Bounds,
	int32 TargetCount,
	int32 ClusterCount,
	float ClusterRadius,
	int32 Seed)
{
	TArray<FVector> Points;
	TargetCount = FMath::Clamp(TargetCount, 0, 50000);
	ClusterCount = FMath::Clamp(ClusterCount, 1, 1024);
	if (!Bounds.IsValid || TargetCount <= 0)
	{
		return Points;
	}

	FRandomStream Rng(Seed);
	TArray<FVector> ClusterCenters;
	ClusterCenters.Reserve(ClusterCount);
	for (int32 Index = 0; Index < ClusterCount; ++Index)
	{
		ClusterCenters.Add(SamplePointInBounds(Bounds, Rng));
	}

	const float Radius = FMath::Max(1.0f, ClusterRadius);
	for (int32 Index = 0; Index < TargetCount; ++Index)
	{
		const FVector& Center = ClusterCenters[Rng.RandRange(0, ClusterCenters.Num() - 1)];
		const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
		const float Distance = Radius * FMath::Sqrt(Rng.FRand());

		FVector Candidate = Center;
		Candidate.X += FMath::Cos(Angle) * Distance;
		Candidate.Y += FMath::Sin(Angle) * Distance;
		Candidate.Z = Rng.FRandRange(Bounds.Min.Z, Bounds.Max.Z);

		if (!IsPointWithinBounds2D(Candidate, Bounds))
		{
			Candidate.X = FMath::Clamp(Candidate.X, Bounds.Min.X, Bounds.Max.X);
			Candidate.Y = FMath::Clamp(Candidate.Y, Bounds.Min.Y, Bounds.Max.Y);
		}
		Points.Add(Candidate);
	}

	return Points;
}

TArray<FVector> FDistributionEngine::GeneratePoissonDiskPoints(
	const FBox& Bounds,
	int32 TargetCount,
	float MinSpacing,
	int32 Seed)
{
	TArray<FVector> Points;
	TargetCount = FMath::Clamp(TargetCount, 0, 50000);
	if (!Bounds.IsValid || TargetCount <= 0)
	{
		return Points;
	}

	const float Radius = FMath::Max(1.0f, MinSpacing);
	const float CellSize = Radius / FMath::Sqrt(2.0f);
	const int32 GridWidth = FMath::Max(1, FMath::CeilToInt((Bounds.Max.X - Bounds.Min.X) / CellSize));
	const int32 GridHeight = FMath::Max(1, FMath::CeilToInt((Bounds.Max.Y - Bounds.Min.Y) / CellSize));

	FRandomStream Rng(Seed);
	TMap<int64, int32> Grid;
	TArray<int32> ActiveList;

	auto GridKey = [](int32 X, int32 Y) -> int64
	{
		return (((int64)X) << 32) | (uint32)Y;
	};

	auto ToGridCoord = [&](const FVector2D& P) -> FIntPoint
	{
		const int32 GX = FMath::Clamp(FMath::FloorToInt((P.X - Bounds.Min.X) / CellSize), 0, GridWidth - 1);
		const int32 GY = FMath::Clamp(FMath::FloorToInt((P.Y - Bounds.Min.Y) / CellSize), 0, GridHeight - 1);
		return FIntPoint(GX, GY);
	};

	auto IsFarEnough = [&](const FVector2D& Candidate2D) -> bool
	{
		const FIntPoint Cell = ToGridCoord(Candidate2D);
		const int32 SearchRadius = 2;
		for (int32 DY = -SearchRadius; DY <= SearchRadius; ++DY)
		{
			for (int32 DX = -SearchRadius; DX <= SearchRadius; ++DX)
			{
				const int32 NX = Cell.X + DX;
				const int32 NY = Cell.Y + DY;
				if (NX < 0 || NX >= GridWidth || NY < 0 || NY >= GridHeight)
				{
					continue;
				}

				const int32* ExistingIndex = Grid.Find(GridKey(NX, NY));
				if (!ExistingIndex || !Points.IsValidIndex(*ExistingIndex))
				{
					continue;
				}

				const FVector& Existing = Points[*ExistingIndex];
				if (FVector2D::Distance(Candidate2D, FVector2D(Existing.X, Existing.Y)) < Radius)
				{
					return false;
				}
			}
		}
		return true;
	};

	const FVector SeedPoint = SamplePointInBounds(Bounds, Rng);
	Points.Add(SeedPoint);
	ActiveList.Add(0);
	const FIntPoint SeedCell = ToGridCoord(FVector2D(SeedPoint.X, SeedPoint.Y));
	Grid.Add(GridKey(SeedCell.X, SeedCell.Y), 0);

	const int32 CandidatesPerActivePoint = 30;
	while (ActiveList.Num() > 0 && Points.Num() < TargetCount)
	{
		const int32 ActiveIndex = Rng.RandRange(0, ActiveList.Num() - 1);
		const int32 PointIndex = ActiveList[ActiveIndex];
		const FVector BasePoint = Points[PointIndex];
		const FVector2D Base2D(BasePoint.X, BasePoint.Y);

		bool bAccepted = false;
		for (int32 Attempt = 0; Attempt < CandidatesPerActivePoint; ++Attempt)
		{
			const float Angle = Rng.FRandRange(0.0f, 2.0f * PI);
			const float Distance = Rng.FRandRange(Radius, 2.0f * Radius);
			const FVector2D Candidate2D = Base2D + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Distance;

			if (Candidate2D.X < Bounds.Min.X || Candidate2D.X > Bounds.Max.X ||
				Candidate2D.Y < Bounds.Min.Y || Candidate2D.Y > Bounds.Max.Y)
			{
				continue;
			}

			if (!IsFarEnough(Candidate2D))
			{
				continue;
			}

			const FVector Candidate(
				Candidate2D.X,
				Candidate2D.Y,
				Rng.FRandRange(Bounds.Min.Z, Bounds.Max.Z));

			const int32 NewIndex = Points.Add(Candidate);
			ActiveList.Add(NewIndex);
			const FIntPoint Cell = ToGridCoord(Candidate2D);
			Grid.Add(GridKey(Cell.X, Cell.Y), NewIndex);
			bAccepted = true;
			break;
		}

		if (!bAccepted)
		{
			ActiveList.RemoveAtSwap(ActiveIndex);
		}
	}

	return Points;
}

TArray<FVector> FDistributionEngine::GeneratePoissonPoints(
	const FBox& Bounds,
	int32 TargetCount,
	float MinSpacing,
	int32 Seed)
{
	return GeneratePoissonDiskPoints(Bounds, TargetCount, MinSpacing, Seed);
}

TArray<FVector> FDistributionEngine::ApplySlopeFilter(
	const TArray<FVector>& Points,
	UWorld* World,
	float MinSlopeDegrees,
	float MaxSlopeDegrees)
{
	if (!World || Points.Num() == 0)
	{
		return Points;
	}

	const float MinSlope = FMath::Clamp(MinSlopeDegrees, 0.0f, 89.9f);
	const float MaxSlope = FMath::Clamp(MaxSlopeDegrees, MinSlope, 89.9f);
	TArray<FVector> Filtered;
	Filtered.Reserve(Points.Num());

	FCollisionQueryParams Params(SCENE_QUERY_STAT(UEAgentForgeSlopeFilter), false);
	for (const FVector& Point : Points)
	{
		const FVector Start(Point.X, Point.Y, Point.Z + 10000.0f);
		const FVector End(Point.X, Point.Y, Point.Z - 10000.0f);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			continue;
		}

		const float Dot = FMath::Clamp(FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), FVector::UpVector), -1.0f, 1.0f);
		const float SlopeDegrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
		if (SlopeDegrees >= MinSlope && SlopeDegrees <= MaxSlope)
		{
			Filtered.Add(Hit.ImpactPoint);
		}
	}

	return Filtered;
}

TArray<FVector> FDistributionEngine::ApplyHeightFilter(
	const TArray<FVector>& Points,
	float MinHeight,
	float MaxHeight)
{
	const float Low = FMath::Min(MinHeight, MaxHeight);
	const float High = FMath::Max(MinHeight, MaxHeight);

	TArray<FVector> Filtered;
	Filtered.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		if (Point.Z >= Low && Point.Z <= High)
		{
			Filtered.Add(Point);
		}
	}
	return Filtered;
}

TArray<FVector> FDistributionEngine::ApplyDistanceMask(
	const TArray<FVector>& Points,
	const FVector& Origin,
	float MinDistance,
	float MaxDistance)
{
	const float Low = FMath::Max(0.0f, FMath::Min(MinDistance, MaxDistance));
	const float High = FMath::Max(Low, FMath::Max(MinDistance, MaxDistance));

	TArray<FVector> Filtered;
	Filtered.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		const float Distance = FVector::Dist2D(Point, Origin);
		if (Distance >= Low && Distance <= High)
		{
			Filtered.Add(Point);
		}
	}
	return Filtered;
}
