// Copyright UEAgentForge Project. All Rights Reserved.
// ForgeToolsPanel.cpp - UI lifetime scaffold.

#include "UI/ForgeToolsPanel.h"
#include "Operators/ProceduralOpsModule.h"

bool FForgeToolsPanel::bInitialized = false;
FForgeToolsSettings FForgeToolsPanel::Settings;

void FForgeToolsPanel::Initialize()
{
	bInitialized = true;
}

void FForgeToolsPanel::Shutdown()
{
	bInitialized = false;
}

bool FForgeToolsPanel::IsInitialized()
{
	return bInitialized;
}

void FForgeToolsPanel::SetSettings(const FForgeToolsSettings& InSettings)
{
	Settings = InSettings;
}

FForgeToolsSettings FForgeToolsPanel::GetSettings()
{
	return Settings;
}

TSharedPtr<FJsonObject> FForgeToolsPanel::BuildWorldSpec(const FForgeToolsSettings& InSettings)
{
	TSharedPtr<FJsonObject> Spec = MakeShared<FJsonObject>();
	Spec->SetNumberField(TEXT("terrain_seed"), InSettings.TerrainSeed);
	Spec->SetNumberField(TEXT("biome_count"), InSettings.BiomeCount);
	Spec->SetNumberField(TEXT("tree_density"), InSettings.TreeDensity);
	Spec->SetNumberField(TEXT("cluster_radius"), InSettings.ClusterRadius);
	Spec->SetNumberField(TEXT("clearing_density"), InSettings.ClearingDensity);
	Spec->SetNumberField(TEXT("road_count"), InSettings.RoadCount);
	Spec->SetNumberField(TEXT("poi_density"), InSettings.PoiDensity);
	Spec->SetStringField(TEXT("palette_id"), InSettings.PaletteId);
	return Spec;
}

TSharedPtr<FJsonObject> FForgeToolsPanel::BuildGenerateWorldArgs(const FForgeToolsSettings& InSettings)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("seed"), InSettings.TerrainSeed);
	Root->SetStringField(TEXT("palette_id"), InSettings.PaletteId);
	Root->SetNumberField(TEXT("biome_count"), InSettings.BiomeCount);
	Root->SetNumberField(TEXT("density"), InSettings.TreeDensity);
	Root->SetNumberField(TEXT("cluster_radius"), InSettings.ClusterRadius);
	Root->SetNumberField(TEXT("clearing_density"), InSettings.ClearingDensity);
	Root->SetNumberField(TEXT("max_generation_time_ms"), 30000.0);
	Root->SetNumberField(TEXT("max_spawn_points"), 50000);
	Root->SetNumberField(TEXT("max_cluster_count"), 1024);

	TSharedPtr<FJsonObject> Terrain = MakeShared<FJsonObject>();
	Terrain->SetNumberField(TEXT("seed"), InSettings.TerrainSeed);
	Terrain->SetNumberField(TEXT("erosion_iterations"), 24);
	Terrain->SetNumberField(TEXT("sediment_strength"), 0.4f);
	Root->SetObjectField(TEXT("terrain_generate"), Terrain);

	return Root;
}

FString FForgeToolsPanel::GenerateWorld()
{
	return FProceduralOpsModule::RunOperatorPipeline(BuildGenerateWorldArgs(Settings));
}
