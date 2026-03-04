// Copyright UEAgentForge Project. All Rights Reserved.
// SceneEvaluator.cpp - structural metrics used by the visual intelligence loop.

#include "Visual/SceneEvaluator.h"

namespace
{
	static FORCEINLINE int32 CellIndex(const int32 X, const int32 Y, const int32 Width)
	{
		return (Y * Width) + X;
	}

	static TArray<int32> BuildOccupancyGrid(
		const TArray<FVector>& Points,
		const FBox& Bounds,
		const int32 GridResolution)
{
		const int32 Grid = FMath::Max(2, GridResolution);
		TArray<int32> Cells;
		Cells.SetNumZeroed(Grid * Grid);

		const FVector2D Size2D(
			FMath::Max(1.0f, Bounds.Max.X - Bounds.Min.X),
			FMath::Max(1.0f, Bounds.Max.Y - Bounds.Min.Y));

		for (const FVector& Point : Points)
		{
			const float U = FMath::Clamp((Point.X - Bounds.Min.X) / Size2D.X, 0.0f, 0.99999f);
			const float V = FMath::Clamp((Point.Y - Bounds.Min.Y) / Size2D.Y, 0.0f, 0.99999f);
			const int32 X = FMath::Clamp(FMath::FloorToInt(U * Grid), 0, Grid - 1);
			const int32 Y = FMath::Clamp(FMath::FloorToInt(V * Grid), 0, Grid - 1);
			Cells[CellIndex(X, Y, Grid)] += 1;
		}

		return Cells;
	}
}

float FSceneEvaluator::ComputeObjectDensityVariance(
	const TArray<FVector>& Points,
	const FBox& Bounds,
	int32 GridResolution)
{
	if (!Bounds.IsValid || Points.Num() == 0)
	{
		return 0.0f;
	}

	const TArray<int32> Cells = BuildOccupancyGrid(Points, Bounds, GridResolution);
	if (Cells.Num() == 0)
	{
		return 0.0f;
	}

	float Mean = 0.0f;
	for (const int32 Count : Cells)
	{
		Mean += (float)Count;
	}
	Mean /= (float)Cells.Num();

	if (Mean <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	float Variance = 0.0f;
	for (const int32 Count : Cells)
	{
		const float Delta = (float)Count - Mean;
		Variance += Delta * Delta;
	}
	Variance /= (float)Cells.Num();

	const float StdDev = FMath::Sqrt(FMath::Max(0.0f, Variance));
	const float CoeffVar = StdDev / Mean;
	const float TargetCoeffVar = 0.60f;
	return FMath::Clamp(1.0f - FMath::Abs(CoeffVar - TargetCoeffVar), 0.0f, 1.0f);
}

float FSceneEvaluator::ComputeClusterScore(
	const TArray<FVector>& Points,
	float ClusterRadius)
{
	if (Points.Num() < 2)
	{
		return 0.0f;
	}

	const float TargetDist = FMath::Max(50.0f, ClusterRadius * 0.35f);
	float MeanNearest = 0.0f;

	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		float BestDistSq = TNumericLimits<float>::Max();
		for (int32 Other = 0; Other < Points.Num(); ++Other)
		{
			if (Other == Index)
			{
				continue;
			}
			BestDistSq = FMath::Min(BestDistSq, FVector::DistSquared2D(Points[Index], Points[Other]));
		}
		MeanNearest += FMath::Sqrt(FMath::Max(0.0f, BestDistSq));
	}

	MeanNearest /= (float)Points.Num();
	const float RelativeError = FMath::Abs(MeanNearest - TargetDist) / FMath::Max(1.0f, TargetDist);
	return FMath::Clamp(1.0f - RelativeError, 0.0f, 1.0f);
}

float FSceneEvaluator::ComputeEmptySpaceScore(
	const TArray<FVector>& Points,
	const FBox& Bounds,
	float TargetEmptyRatio,
	int32 GridResolution)
{
	if (!Bounds.IsValid)
	{
		return 0.0f;
	}

	const TArray<int32> Cells = BuildOccupancyGrid(Points, Bounds, GridResolution);
	if (Cells.Num() == 0)
	{
		return 0.0f;
	}

	int32 Empty = 0;
	for (const int32 Count : Cells)
	{
		if (Count == 0)
		{
			++Empty;
		}
	}

	const float EmptyRatio = (float)Empty / (float)Cells.Num();
	const float Target = FMath::Clamp(TargetEmptyRatio, 0.0f, 1.0f);
	const float Error = FMath::Abs(EmptyRatio - Target);
	return FMath::Clamp(1.0f - (Error * 2.0f), 0.0f, 1.0f);
}

float FSceneEvaluator::ComputeVisualBalance(
	const TArray<FVector>& Points,
	const FBox& Bounds)
{
	if (!Bounds.IsValid || Points.Num() == 0)
	{
		return 0.0f;
	}

	const FVector2D Center((Bounds.Min.X + Bounds.Max.X) * 0.5f, (Bounds.Min.Y + Bounds.Max.Y) * 0.5f);
	int32 Quadrants[4] = { 0, 0, 0, 0 };

	for (const FVector& Point : Points)
	{
		const bool bRight = Point.X >= Center.X;
		const bool bTop = Point.Y >= Center.Y;
		const int32 Index = (bTop ? 2 : 0) + (bRight ? 1 : 0);
		Quadrants[Index] += 1;
	}

	const float Mean = (float)Points.Num() / 4.0f;
	float Deviation = 0.0f;
	for (const int32 Count : Quadrants)
	{
		Deviation += FMath::Abs((float)Count - Mean) / FMath::Max(1.0f, Mean);
	}
	Deviation /= 4.0f;

	return FMath::Clamp(1.0f - Deviation, 0.0f, 1.0f);
}

FSceneEvaluationMetrics FSceneEvaluator::EvaluateScene(
	const TArray<FVector>& Points,
	const FBox& Bounds,
	float ClusterRadius)
{
	FSceneEvaluationMetrics Metrics;
	Metrics.DensityVarianceScore = ComputeObjectDensityVariance(Points, Bounds, 10);
	Metrics.ClusterScore = ComputeClusterScore(Points, ClusterRadius);
	Metrics.EmptySpaceScore = ComputeEmptySpaceScore(Points, Bounds, 0.28f, 12);
	Metrics.VisualBalanceScore = ComputeVisualBalance(Points, Bounds);

	Metrics.CombinedScore =
		(0.40f * Metrics.DensityVarianceScore) +
		(0.30f * Metrics.ClusterScore) +
		(0.20f * Metrics.EmptySpaceScore) +
		(0.10f * Metrics.VisualBalanceScore);

	return Metrics;
}
