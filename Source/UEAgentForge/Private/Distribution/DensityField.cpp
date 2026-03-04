// Copyright UEAgentForge Project. All Rights Reserved.
// DensityField.cpp - deterministic radial density gradients.

#include "Distribution/DensityField.h"

#include "Math/RandomStream.h"

namespace
{
	static FORCEINLINE int32 FieldIndex(const int32 X, const int32 Y, const int32 Width)
	{
		return (Y * Width) + X;
	}

	static FORCEINLINE float SafeSaturate(const float Value)
	{
		return FMath::Clamp(Value, 0.0f, 1.0f);
	}
}

TArray<float> FDensityField::GenerateDensityField(
	const FBox& Bounds,
	int32 Width,
	int32 Height,
	const FDensityFieldConfig& Config)
{
	Width = FMath::Clamp(Width, 2, 1024);
	Height = FMath::Clamp(Height, 2, 1024);

	TArray<float> Field;
	Field.SetNumZeroed(Width * Height);

	const FVector2D Extent2D(FMath::Max(1.0f, Bounds.Max.X - Bounds.Min.X), FMath::Max(1.0f, Bounds.Max.Y - Bounds.Min.Y));
	const float Sigma = FMath::Max(1.0f, Config.Sigma);
	const float Sigma2 = Sigma * Sigma;
	const float BaseDensity = FMath::Max(0.0f, Config.BaseDensity);
	const float NoiseBlend = SafeSaturate(Config.NoiseBlend);

	FRandomStream Rng(Config.Seed);
	const float NoiseOffsetX = Rng.FRandRange(-100000.0f, 100000.0f);
	const float NoiseOffsetY = Rng.FRandRange(-100000.0f, 100000.0f);
	const float NoiseScale = 0.0015f;

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const float V = (float)Y / (float)(Height - 1);
		const float WY = FMath::Lerp(Bounds.Min.Y, Bounds.Max.Y, V);

		for (int32 X = 0; X < Width; ++X)
		{
			const float U = (float)X / (float)(Width - 1);
			const float WX = FMath::Lerp(Bounds.Min.X, Bounds.Max.X, U);
			const FVector2D P(WX, WY);

			const FVector2D Center(Config.Center.X, Config.Center.Y);
			const float DistSq = FVector2D::DistSquared(P, Center);
			const float Falloff = FMath::Exp(-DistSq / (2.0f * Sigma2));
			float Density = BaseDensity * Falloff;

			const float Noise = FMath::PerlinNoise2D(FVector2D((WX * NoiseScale) + NoiseOffsetX, (WY * NoiseScale) + NoiseOffsetY));
			const float NoiseFactor = FMath::Lerp(1.0f, 0.75f + (Noise * 0.25f), NoiseBlend);
			Density *= NoiseFactor;

			// Keep slight support outside the high-density center for natural fringe growth.
			const float Fringe = FMath::Clamp(1.0f - (DistSq / FMath::Square(FMath::Max(Extent2D.X, Extent2D.Y))), 0.0f, 1.0f) * 0.15f;
			Field[FieldIndex(X, Y, Width)] = SafeSaturate(Density + Fringe);
		}
	}

	return Field;
}

float FDensityField::SampleDensity(
	const TArray<float>& Field,
	int32 Width,
	int32 Height,
	const FBox& Bounds,
	const FVector& Location,
	float FallbackDensity)
{
	if (Field.Num() != Width * Height || Width < 2 || Height < 2 || !Bounds.IsValid)
	{
		return SafeSaturate(FallbackDensity);
	}

	const float U = FMath::Clamp((Location.X - Bounds.Min.X) / FMath::Max(1.0f, Bounds.Max.X - Bounds.Min.X), 0.0f, 1.0f);
	const float V = FMath::Clamp((Location.Y - Bounds.Min.Y) / FMath::Max(1.0f, Bounds.Max.Y - Bounds.Min.Y), 0.0f, 1.0f);

	const float FX = U * (float)(Width - 1);
	const float FY = V * (float)(Height - 1);
	const int32 X0 = FMath::Clamp(FMath::FloorToInt(FX), 0, Width - 1);
	const int32 Y0 = FMath::Clamp(FMath::FloorToInt(FY), 0, Height - 1);
	const int32 X1 = FMath::Clamp(X0 + 1, 0, Width - 1);
	const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Height - 1);

	const float Tx = FMath::Frac(FX);
	const float Ty = FMath::Frac(FY);
	const float A = FMath::Lerp(Field[FieldIndex(X0, Y0, Width)], Field[FieldIndex(X1, Y0, Width)], Tx);
	const float B = FMath::Lerp(Field[FieldIndex(X0, Y1, Width)], Field[FieldIndex(X1, Y1, Width)], Tx);
	return SafeSaturate(FMath::Lerp(A, B, Ty));
}

TArray<FVector> FDensityField::ApplyDensityGradient(
	const TArray<FVector>& Points,
	const TArray<float>& Field,
	int32 Width,
	int32 Height,
	const FBox& Bounds,
	int32 Seed,
	float MinKeepProbability)
{
	if (Points.Num() == 0)
	{
		return Points;
	}

	FRandomStream Rng(Seed ^ 0x2F1A6D93);
	const float MinKeep = SafeSaturate(MinKeepProbability);

	TArray<FVector> Filtered;
	Filtered.Reserve(Points.Num());
	for (const FVector& Point : Points)
	{
		const float Density = SampleDensity(Field, Width, Height, Bounds, Point, 1.0f);
		const float KeepProb = FMath::Max(MinKeep, Density);
		if (Rng.FRand() <= KeepProb)
		{
			Filtered.Add(Point);
		}
	}
	return Filtered;
}
