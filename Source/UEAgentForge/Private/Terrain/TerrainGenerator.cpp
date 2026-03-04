// Copyright UEAgentForge Project. All Rights Reserved.
// TerrainGenerator.cpp - baseline terrain utility implementations.

#include "Terrain/TerrainGenerator.h"
#include "Terrain/ErosionSim.h"

#include "Engine/World.h"
#include "Math/RandomStream.h"

namespace
{
	static bool IsValidHeightmapShape(const TArray<float>& Heightmap, int32 Width, int32 Height)
	{
		return Width > 1 && Height > 1 && Heightmap.Num() == (Width * Height);
	}

	static FORCEINLINE int32 HeightmapIndex(int32 X, int32 Y, int32 Width)
	{
		return Y * Width + X;
	}
}

TArray<float> FTerrainGenerator::GenerateHeightmap(
	int32 Width,
	int32 Height,
	int32 Seed,
	float Frequency,
	float Amplitude)
{
	Width = FMath::Clamp(Width, 2, 4096);
	Height = FMath::Clamp(Height, 2, 4096);
	const float Freq = FMath::Max(0.0001f, Frequency);
	const float Amp = FMath::Max(0.0f, Amplitude);

	TArray<float> Heightmap;
	Heightmap.SetNumZeroed(Width * Height);

	FRandomStream Rng(Seed);
	const float OffsetX = Rng.FRandRange(-100000.0f, 100000.0f);
	const float OffsetY = Rng.FRandRange(-100000.0f, 100000.0f);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const float NX = (float)X * Freq + OffsetX;
			const float NY = (float)Y * Freq + OffsetY;
			const float Noise = FMath::PerlinNoise2D(FVector2D(NX, NY));
			Heightmap[HeightmapIndex(X, Y, Width)] = Noise * Amp;
		}
	}
	return Heightmap;
}

void FTerrainGenerator::ApplyRidgedNoise(
	TArray<float>& Heightmap,
	int32 Width,
	int32 Height,
	int32 Seed,
	float Strength)
{
	if (!IsValidHeightmapShape(Heightmap, Width, Height))
	{
		return;
	}

	const float BlendStrength = FMath::Max(0.0f, Strength);
	if (BlendStrength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FRandomStream Rng(Seed ^ 0x6B8B4567);
	const float OffsetX = Rng.FRandRange(-100000.0f, 100000.0f);
	const float OffsetY = Rng.FRandRange(-100000.0f, 100000.0f);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		for (int32 X = 0; X < Width; ++X)
		{
			const int32 Index = HeightmapIndex(X, Y, Width);
			const float Base = FMath::PerlinNoise2D(FVector2D((float)X * 0.015f + OffsetX, (float)Y * 0.015f + OffsetY));
			const float Ridged = 1.0f - FMath::Abs(Base);
			Heightmap[Index] += Ridged * BlendStrength;
		}
	}
}

void FTerrainGenerator::ApplyErosion(
	TArray<float>& Heightmap,
	int32 Width,
	int32 Height,
	int32 Iterations,
	float Strength)
{
	if (!IsValidHeightmapShape(Heightmap, Width, Height))
	{
		return;
	}

	const int32 Steps = FMath::Clamp(Iterations, 0, 256);
	const float ErodeStrength = FMath::Clamp(Strength, 0.0f, 1.0f);
	if (Steps == 0 || ErodeStrength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	// Thermal erosion creates more natural ridges/valleys than pure blur smoothing.
	const float TalusThreshold = FMath::Lerp(0.06f, 0.01f, ErodeStrength);
	const float SedimentStrength = ErodeStrength;
	FErosionSim::ApplyThermalErosion(Heightmap, Width, Height, Steps, TalusThreshold, SedimentStrength);
}

void FTerrainGenerator::NormalizeHeightmap(TArray<float>& Heightmap, float MinOut, float MaxOut)
{
	if (Heightmap.Num() == 0)
	{
		return;
	}

	float MinValue = TNumericLimits<float>::Max();
	float MaxValue = TNumericLimits<float>::Lowest();
	for (const float Value : Heightmap)
	{
		MinValue = FMath::Min(MinValue, Value);
		MaxValue = FMath::Max(MaxValue, Value);
	}

	if (FMath::IsNearlyEqual(MinValue, MaxValue))
	{
		for (float& Value : Heightmap)
		{
			Value = MinOut;
		}
		return;
	}

	const float OutLow = FMath::Min(MinOut, MaxOut);
	const float OutHigh = FMath::Max(MinOut, MaxOut);
	for (float& Value : Heightmap)
	{
		const float Alpha = (Value - MinValue) / (MaxValue - MinValue);
		Value = FMath::Lerp(OutLow, OutHigh, Alpha);
	}
}

bool FTerrainGenerator::SpawnLandscape(
	UWorld* World,
	const TArray<float>& Heightmap,
	int32 Width,
	int32 Height,
	const FVector& Origin,
	const FVector& Scale,
	FString& OutMessage)
{
	if (!World)
	{
		OutMessage = TEXT("SpawnLandscape failed: invalid world.");
		return false;
	}
	if (!IsValidHeightmapShape(Heightmap, Width, Height))
	{
		OutMessage = TEXT("SpawnLandscape failed: invalid heightmap dimensions.");
		return false;
	}

	// Phase 2 skeleton only: landscape authoring will be implemented by TerrainEngine.
	OutMessage = FString::Printf(
		TEXT("SpawnLandscape not implemented yet (width=%d height=%d origin=(%.1f,%.1f,%.1f) scale=(%.1f,%.1f,%.1f))."),
		Width,
		Height,
		Origin.X, Origin.Y, Origin.Z,
		Scale.X, Scale.Y, Scale.Z);
	return false;
}
