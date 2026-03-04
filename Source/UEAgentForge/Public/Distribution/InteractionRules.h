// Copyright UEAgentForge Project. All Rights Reserved.
// InteractionRules - deterministic overlap and attraction filters for scatter points.

#pragma once

#include "CoreMinimal.h"

class UEAGENTFORGE_API FInteractionRules
{
public:
	static TArray<FVector> ApplyAvoidance(
		const TArray<FVector>& CandidatePoints,
		const TArray<FVector>& BlockingPoints,
		float MinDistance);

	static TArray<FVector> ApplyAttractorBias(
		const TArray<FVector>& CandidatePoints,
		const TArray<FVector>& AttractorPoints,
		float AttractionRadius,
		float AttractionStrength,
		int32 Seed);

	static TArray<FVector> ApplySelfSpacing(
		const TArray<FVector>& CandidatePoints,
		float MinDistance);
};
