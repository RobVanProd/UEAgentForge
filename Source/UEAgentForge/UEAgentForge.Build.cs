using UnrealBuildTool;

public class UEAgentForge : ModuleRules
{
	public UEAgentForge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

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
			"ObjectTools",            // delete_asset (force delete)

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
		});
	}
}
