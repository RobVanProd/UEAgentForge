// Copyright UEAgentForge Project. All Rights Reserved.
// SemanticCommandModule — v0.3.0 High-Level Semantic Command Set.
//
// Lifts the agent above raw actor manipulation into genre-aware, intent-driven operations:
//   PlaceAssetThematically — spawn at locations chosen by horror/genre heuristics
//   RefineLevelSection     — iterative analyze → place → verify loop
//   ApplyGenreRules        — apply genre-specific atmosphere presets (lighting, PP, fog)
//   CreateInEditorAsset    — stub guidance for Geometry Script / modeling tools

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"


class UEAGENTFORGE_API FSemanticCommandModule
{
public:
	/**
	 * Spawns an asset at locations selected by thematic heuristics rather than
	 * explicit coordinates. The agent describes what it wants; this function finds
	 * appropriate world positions automatically.
	 *
	 * Args:
	 *   class_path   — asset class or SM path to spawn
	 *   count        — number of instances to place (default 3)
	 *   theme_rules  — JSON object:
	 *                    prefer_dark:bool      — favour low-light areas
	 *                    prefer_corners:bool   — favour areas with nearby walls (tight spaces)
	 *                    prefer_occluded:bool  — favour spots not visible from level centre
	 *                    min_spacing:float     — minimum cm between placed actors (default 300)
	 *   reference_area — {x,y,z,radius} world-space search volume (optional; defaults to full level)
	 *   label_prefix   — base name for spawned actors (default "Themed")
	 *
	 * Returns: {ok, placed_count, actors:[{label, location:{x,y,z}}], placement_reasoning}
	 */
	static FString PlaceAssetThematically(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Iteratively refines a level section until it meets a quality threshold.
	 * Each iteration: analyze composition → decide action → spawn/delete → verify.
	 *
	 * Args:
	 *   description    — natural-language goal (e.g. "denser gothic atmosphere")
	 *   target_area    — {x,y,z,radius} optional search volume
	 *   max_iterations — cap on refinement cycles (default 3)
	 *   class_path     — class to use when adding props (default StaticMeshActor)
	 *
	 * Returns: {ok, iterations_run, actions_taken[], final_density_score, detail}
	 */
	static FString RefineLevelSection(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Applies an atmosphere preset for the specified genre.
	 * Mutates lights, post-process volumes, and fog in the current level.
	 *
	 * Args:
	 *   genre     — "horror" | "dark" | "thriller" | "neutral"
	 *   intensity — 0.0–1.0 blend towards the preset (default 1.0)
	 *
	 * Presets:
	 *   horror  — dim point lights to 40%, heavy vignette (0.85), high grain (0.45),
	 *             cool desaturation, fog +50%, CRT blendable weight 0.4
	 *   dark    — dim point lights to 60%, vignette 0.65, fog +30%, neutral color
	 *   thriller— dim point lights to 70%, sharp contrast, no fog change
	 *   neutral — restore point lights to 100%, vignette 0.4, reset grain/fog
	 *
	 * Returns: {ok, genre, intensity, changes_applied[], lights_modified, pp_modified}
	 */
	static FString ApplyGenreRules(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Stub — Geometry Script / in-editor modeling asset creation.
	 * Returns guidance on how to accomplish the request manually or via external tools.
	 *
	 * Args: type ("StaticMesh"|"Material"|"Blueprint"), description
	 * Returns: {ok:false, message, workaround, recommended_approach}
	 */
	static FString CreateInEditorAsset(const TSharedPtr<FJsonObject>& Args);

private:
	/** Returns a list of "dark corner" candidate positions in the world.
	 *  Positions are far from light sources and near walls (evaluated via line traces). */
	static TArray<FVector> FindDarkCorners(UWorld* World, const FVector& AreaCentre,
	                                        float AreaRadius, int32 MaxCandidates);

	/** Returns true if a world position is not in direct line of sight from LevelCentre. */
	static bool IsOccluded(UWorld* World, const FVector& Position, const FVector& LevelCentre);

	/** Estimates darkness at a world position by querying nearby light intensities. */
	static float EstimateDarknessAt(UWorld* World, const FVector& Position, float SearchRadius);
};
