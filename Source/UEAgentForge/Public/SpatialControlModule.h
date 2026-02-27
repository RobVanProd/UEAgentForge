#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * UEAgentForge — SpatialControlModule
 *
 * Precise, context-aware 3D placement and spatial analysis commands.
 * All functions are pure static — no UObject lifecycle required.
 *
 * ─── COMMANDS ───────────────────────────────────────────────────────────────
 *
 *   spawn_actor_at_surface   → {ok, actor_label, actor_path, location, normal, surface_actor}
 *                              args: class_path, origin{x,y,z}, direction{x,y,z},
 *                                    [max_distance=5000], [align_to_normal=true],
 *                                    [label]
 *
 *   align_actors_to_surface  → {ok, aligned_count, results[{label,old_z,new_z,ok}]}
 *                              args: actor_labels[], [down_trace_extent=2000]
 *
 *   get_surface_normal_at    → {ok, location{x,y,z}, normal{x,y,z}, hit_actor}
 *                              args: x,y,z
 *
 *   analyze_level_composition → {ok, actor_count, static_count, light_count, ai_count,
 *                                bounds{min,max,size}, density_score, recommendations[]}
 *
 *   get_actors_in_radius     → [{label, class, distance, location}]
 *                              args: x,y,z, radius
 */
class UEAGENTFORGE_API FSpatialControlModule
{
public:
	/** Raycast from origin in direction, spawn actor at hit surface. */
	static FString SpawnActorAtSurface(const TSharedPtr<FJsonObject>& Args);

	/** Drop a list of actors to the nearest surface below them. */
	static FString AlignActorsToSurface(const TSharedPtr<FJsonObject>& Args);

	/** Return the surface normal and exact hit location at a given world point. */
	static FString GetSurfaceNormalAt(const TSharedPtr<FJsonObject>& Args);

	/** Analyze actor distribution, density, and bounding box of the current level. */
	static FString AnalyzeLevelComposition();

	/** Return all actors within a sphere radius, sorted by distance. */
	static FString GetActorsInRadius(const TSharedPtr<FJsonObject>& Args);
};
