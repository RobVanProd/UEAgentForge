// Copyright UEAgentForge Project. All Rights Reserved.
// ForgeToolsPanel - editor UI bootstrap scaffolding.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct UEAGENTFORGE_API FForgeToolsSettings
{
	int32 TerrainSeed = 48293;
	int32 BiomeCount = 4;
	float TreeDensity = 0.65f;
	float ClusterRadius = 500.0f;
	float ClearingDensity = 0.15f;
	int32 RoadCount = 2;
	float PoiDensity = 0.03f;
	FString PaletteId = TEXT("forest_temperate");
};

class UEAGENTFORGE_API FForgeToolsPanel
{
public:
	static void Initialize();
	static void Shutdown();
	static bool IsInitialized();

	static void SetSettings(const FForgeToolsSettings& InSettings);
	static FForgeToolsSettings GetSettings();

	static TSharedPtr<FJsonObject> BuildWorldSpec(const FForgeToolsSettings& InSettings);
	static TSharedPtr<FJsonObject> BuildGenerateWorldArgs(const FForgeToolsSettings& InSettings);
	static FString GenerateWorld();

private:
	static bool bInitialized;
	static FForgeToolsSettings Settings;
};
