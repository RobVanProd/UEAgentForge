// Copyright UEAgentForge Project. All Rights Reserved.
// ErosionSim.cpp - simple thermal erosion for terrain realism.

#include "Terrain/ErosionSim.h"

namespace
{
	static FORCEINLINE int32 CellIndex(const int32 X, const int32 Y, const int32 Width)
	{
		return (Y * Width) + X;
	}
}

void FErosionSim::ApplyThermalErosion(
	TArray<float>& Heightmap,
	int32 Width,
	int32 Height,
	int32 Iterations,
	float TalusThreshold,
	float SedimentStrength)
{
	if (Heightmap.Num() != Width * Height || Width < 3 || Height < 3)
	{
		return;
	}

	const int32 StepCount = FMath::Clamp(Iterations, 0, 512);
	const float Talus = FMath::Max(0.0001f, TalusThreshold);
	const float Strength = FMath::Clamp(SedimentStrength, 0.0f, 1.0f);
	if (StepCount == 0 || Strength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	TArray<float> Delta;
	Delta.SetNumZeroed(Heightmap.Num());

	const int32 NX[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
	const int32 NY[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

	for (int32 Iteration = 0; Iteration < StepCount; ++Iteration)
	{
		FMemory::Memzero(Delta.GetData(), sizeof(float) * Delta.Num());

		for (int32 Y = 1; Y < Height - 1; ++Y)
		{
			for (int32 X = 1; X < Width - 1; ++X)
			{
				const int32 CenterIndex = CellIndex(X, Y, Width);
				const float CenterHeight = Heightmap[CenterIndex];

				float MaxDrop = 0.0f;
				int32 TargetX = X;
				int32 TargetY = Y;

				for (int32 N = 0; N < 8; ++N)
				{
					const int32 SX = X + NX[N];
					const int32 SY = Y + NY[N];
					const float NeighborHeight = Heightmap[CellIndex(SX, SY, Width)];
					const float Drop = CenterHeight - NeighborHeight;
					if (Drop > MaxDrop)
					{
						MaxDrop = Drop;
						TargetX = SX;
						TargetY = SY;
					}
				}

				if (MaxDrop <= Talus)
				{
					continue;
				}

				const float Sediment = (MaxDrop - Talus) * 0.5f * Strength;
				const int32 TargetIndex = CellIndex(TargetX, TargetY, Width);
				Delta[CenterIndex] -= Sediment;
				Delta[TargetIndex] += Sediment;
			}
		}

		for (int32 Index = 0; Index < Heightmap.Num(); ++Index)
		{
			Heightmap[Index] += Delta[Index];
		}
	}
}
