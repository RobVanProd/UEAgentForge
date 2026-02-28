// Copyright UEAgentForge Project. All Rights Reserved.
// DataAccessModule — v0.3.0 Rich Multi-Modal Data Access Layer.
//
// Gives the AI agent deep, structured visibility into the Unreal scene:
//   GetMultiViewCapture     — viewport screenshots from preset horror-optimised angles
//   GetLevelHierarchy       — full Outliner tree with components, tags, bounds, NavMesh status
//   GetDeepProperties       — all exposed UPROPERTY values on any actor
//   GetSemanticEnvironmentSnapshot — lighting analysis, darkness score, post-process state, perf

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"


class UEAGENTFORGE_API FDataAccessModule
{
public:
	/**
	 * Moves the editor viewport to a preset camera angle, takes a screenshot,
	 * and returns the file path + camera transform.
	 *
	 * Args: [angle="top"]     — "top" | "front" | "side" | "tension" (horror low-angle)
	 *       [center_x/y/z=0] — world centre to orbit around (defaults to level bounds centre)
	 *       [orbit_radius=3000]
	 * Returns: {ok, angle, path, camera:{x,y,z,pitch,yaw}, preset_angles:[...]}
	 *
	 * Note: screenshot is async (written at next frame-end). Allow ~0.5 s before reading.
	 * Call four times (one per angle) for a complete multi-view set.
	 */
	static FString GetMultiViewCapture(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Returns the complete level Outliner hierarchy as structured JSON.
	 *
	 * Returns: {ok, actor_count, actors:[{label, class, parent, tags[], is_visible,
	 *           has_navmesh_coverage, components:[{name,class}],
	 *           location:{x,y,z}, bounds:{min,max,extent_cm}}]}
	 */
	static FString GetLevelHierarchy();

	/**
	 * Dumps all exposed UPROPERTY values on the named actor.
	 *
	 * Args: label — actor label or name
	 * Returns: {ok, label, class, property_count, properties:{name:value,...}}
	 *
	 * Useful for: verifying tuning values, detecting misconfigured assets,
	 * comparing before/after changes.
	 */
	static FString GetDeepProperties(const TSharedPtr<FJsonObject>& Args);

	/**
	 * Captures a semantic snapshot of the environment — intended for one-shot
	 * "how does this scene feel right now?" analysis.
	 *
	 * Returns: {ok,
	 *   lighting:{point_light_count, avg_intensity, max_intensity, darkness_score,
	 *             has_directional, has_sky_light, dominant_color:{r,g,b}},
	 *   post_process:{vignette, bloom, grain, exposure_compensation,
	 *                 has_crt_blendable, fog_density},
	 *   density:{actor_count, static_count, light_count, ai_count, density_per_m2},
	 *   level_bounds:{center:{x,y,z}, extent:{x,y,z}, area_m2},
	 *   performance:{frame_ms, draw_calls, primitives},
	 *   horror_score  — 0-100, higher = more atmospheric horror
	 * }
	 */
	static FString GetSemanticEnvironmentSnapshot();

private:
	/** Computes the bounding box centre of all actors in the world. */
	static FVector ComputeLevelCenter(UWorld* World);

	/** Reads lit intensity of all point/spot lights and returns aggregate stats. */
	static void GatherLightingStats(UWorld* World,
	                                int32& OutCount, float& OutAvg, float& OutMax,
	                                FLinearColor& OutDominantColor, bool& bHasDir, bool& bHasSky);
};
