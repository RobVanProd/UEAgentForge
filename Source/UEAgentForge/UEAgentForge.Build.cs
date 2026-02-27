using UnrealBuildTool;

public class UEAgentForge : ModuleRules
{
	public UEAgentForge(ReadOnlyTargetRules Target) : base(Target)
	{
		// NoPCHs: prevents LNK2011 "precompiled object not linked" during Live Coding patches.
		// The module is small enough that per-file compilation is fast.
		PCHUsage = PCHUsageMode.NoPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			// Core
			"Core",
			"CoreUObject",
			"Engine",
			"UnrealEd",
			"EditorSubsystem",

			// JSON transport (Remote Control API payload format)
			"Json",
			"JsonUtilities",

			// Asset management
			"AssetRegistry",
			"AssetTools",             // rename_asset, move_asset, delete_asset
			// ObjectTools is part of UnrealEd — no separate module in UE 5.7

			// Screenshot capture
			"ImageWrapper",
			"RenderCore",
			"Renderer",

			// Blueprint graph manipulation
			"Kismet",                 // FKismetEditorUtilities::CompileBlueprint
			"KismetCompiler",         // blueprint compilation pipeline
			"BlueprintGraph",         // UEdGraph, UK2Node types
			"BlueprintEditorLibrary", // UBlueprintEditorLibrary::ReparentBlueprint
			"GraphEditor",            // SGraphEditor (optional UI hooks)

			// Material instancing
			// (covered by Engine + UnrealEd)

			// Spatial awareness
			"NavigationSystem",       // query_navmesh

			// Performance profiling
			"RHI",                    // GPU frame time, draw call counters

			// Python scripting bridge
			"PythonScriptPlugin",     // execute_python

			// AI asset wiring — set_bt_blackboard (BehaviorTree/BlackboardData access)
			"AIModule",

			// Spatial Intelligence Layer — SpatialControlModule
			// (Engine + NavigationSystem already listed above)

			// FAB Integration — FabIntegrationModule
			"HTTP",                   // search_fab_assets (web API)
		});
	}
}
