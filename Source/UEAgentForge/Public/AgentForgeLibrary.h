#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Dom/JsonObject.h"           // FJsonObject, TSharedPtr — explicit with NoPCHs
#include "AgentForgeLibrary.generated.h"

/**
 * UEAgentForge v0.1.0 — Enterprise-grade AI agent command surface.
 *
 * All commands are available via the Unreal Remote Control API:
 *   PUT http://127.0.0.1:30010/remote/object/call
 *   objectPath:   "/Script/UEAgentForge.Default__AgentForgeLibrary"
 *   functionName: "ExecuteCommandJson"
 *   parameters:   { "RequestJson": "{\"cmd\":\"...\",\"args\":{...}}" }
 *
 * ─── VERIFICATION SYSTEM ────────────────────────────────────────────────────
 *
 *   run_verification      → {phases_run, phases_passed, details[]}
 *                           args: [phase_mask=15]   (bits: 1=PreFlight, 2=Snapshot, 4=PostVerify, 8=BuildCheck)
 *   enforce_constitution  → {allowed, violations[]}
 *                           args: action_description
 *   get_forge_status      → {version, constitution_rules_loaded, constitution_path, last_verification}
 *
 * ─── OBSERVATION ────────────────────────────────────────────────────────────
 *
 *   ping                  → {pong, version}
 *   get_current_level     → {package_path, world_path, actor_prefix, map_lock}
 *   assert_current_level  → {ok, expected_level, current_package_path}
 *   get_all_level_actors  → [{name,label,class,object_path,location,rotation,scale,bounds}]
 *   get_actor_components  → [{name,class,object_path}]       args: label
 *   get_actor_bounds      → {origin,extent,box_min,box_max}  args: label
 *   set_viewport_camera   → {ok, x,y,z, pitch,yaw,roll}
 *                           args: x,y,z, [pitch=0,yaw=0,roll=0]
 *   redraw_viewports      → {ok, detail}   (forces a render tick — useful before take_screenshot)
 *
 * ─── ACTOR CONTROL ──────────────────────────────────────────────────────────
 *
 *   spawn_actor           → {spawned_name, spawned_object_path}
 *                           args: class_path, x,y,z, pitch,yaw,roll
 *   set_actor_transform   → {ok, actor_object_path}
 *                           args: object_path, x,y,z, pitch,yaw,roll
 *   delete_actor          → {ok, deleted}       args: label
 *   save_current_level    → {ok}
 *   take_screenshot       → {ok, path, w, h}    args: [filename]
 *
 * ─── SPATIAL QUERIES ────────────────────────────────────────────────────────
 *
 *   cast_ray              → {hit, hit_location, hit_normal, hit_actor, distance}
 *                           args: start{x,y,z}, end{x,y,z}, [trace_complex=true]
 *   query_navmesh         → {on_navmesh, projected_location}
 *                           args: x,y,z, [extent_x=100,extent_y=100,extent_z=200]
 *
 * ─── BLUEPRINT MANIPULATION ─────────────────────────────────────────────────
 *
 *   create_blueprint      → {ok, package, generated_class_path}
 *                           args: name, parent_class, output_path
 *   compile_blueprint     → {ok, errors}         args: blueprint_path
 *   set_bp_cdo_property   → {ok, property, type, value_set}
 *                           args: blueprint_path, property_name,
 *                                 type(float|int|bool|string|name|vector), value
 *   edit_blueprint_node   → {ok, node_guid, action}
 *                           args: blueprint_path, node_spec{type,title,pins[{name,value}]}
 *
 * ─── MATERIAL INSTANCING ────────────────────────────────────────────────────
 *
 *   create_material_instance → {ok, package}
 *                              args: parent_material, instance_name, output_path
 *   set_material_params      → {ok, scalars_set, vectors_set}
 *                              args: instance_path,
 *                                    [scalar_params:{name:float,...}]
 *                                    [vector_params:{name:{r,g,b,a},...}]
 *
 * ─── CONTENT MANAGEMENT ─────────────────────────────────────────────────────
 *
 *   rename_asset          → {ok, new_path}  args: asset_path, new_name
 *   move_asset            → {ok, new_path}  args: asset_path, destination_path
 *   delete_asset          → {ok, deleted}   args: asset_path
 *
 * ─── TRANSACTION SAFETY ─────────────────────────────────────────────────────
 *
 *   begin_transaction     → {ok, label}     args: [label="AgentForge"]
 *   end_transaction       → {ok, ops_count}
 *   undo_transaction      → {ok}
 *   create_snapshot       → {ok, path, actor_count}  args: [snapshot_name]
 *
 * ─── PYTHON SCRIPTING ───────────────────────────────────────────────────────
 *
 *   execute_python        → {ok, output, errors}   args: script (Python code string)
 *
 * ─── PERFORMANCE PROFILING ──────────────────────────────────────────────────
 *
 *   get_perf_stats        → {frame_ms, target_fps, actor_count, component_count,
 *                            draw_calls, primitives, memory_used_mb, memory_total_mb,
 *                            game_thread_ms, render_thread_ms, gpu_ms}
 *
 * ─── SCENE SETUP ────────────────────────────────────────────────────────────
 *
 *   setup_test_level      → {ok, log[], test_actors[]}  args: [floor_size=10000]
 *
 * ─── AI ASSET WIRING ────────────────────────────────────────────────────────
 *
 *   set_bt_blackboard     → {ok, bt_path, bb_path}
 *                           args: bt_path, bb_path
 *                           Links a BlackboardData asset to a BehaviorTree via C++,
 *                           bypassing Python's CPF_Protected restriction on
 *                           BehaviorTree::BlackboardAsset.
 */
UCLASS()
class UEAGENTFORGE_API UAgentForgeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Primary entry point. Routes JSON commands through the verification + constitution pipeline.
	 * All mutations are wrapped in FScopedTransaction and pre/post verified.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Core")
	static FString ExecuteCommandJson(const FString& RequestJson);

	/**
	 * Execute a command inside a full safe transaction with auto-snapshot and verification.
	 * Returns false and cancels the transaction if any verification phase fails.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Core")
	static bool ExecuteSafeTransaction(const FString& CommandJson, FString& OutResult);

	/**
	 * Run the 4-phase verification protocol.
	 * PhaseMask bits: 1=PreFlight, 2=Snapshot+Rollback, 4=PostVerify, 8=BuildCheck
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Verification")
	static bool RunVerificationProtocol(int32 PhaseMask = 15);

	/**
	 * Check whether a proposed action is permitted by the loaded constitution.
	 * Returns true if allowed, false if any rule is violated.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Constitution")
	static bool EnforceConstitution(const FString& ActionDesc, TArray<FString>& OutViolations);

	/**
	 * Execute arbitrary Python via the UE PythonScriptPlugin.
	 * Requires the PythonScriptPlugin to be enabled in the project.
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Scripting")
	static FString ExecutePythonScript(const FString& ScriptCode);

	/**
	 * Edit a Blueprint graph node by spec.
	 * NodeSpecJson: {"type":"CallFunction","title":"NodeTitle","pins":[{"name":"Target","value":"..."}]}
	 */
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Blueprint")
	static bool EditBlueprintNode(const FString& BlueprintPath, const FString& NodeSpecJson);

private:
	// ─── Observation ──────────────────────────────────────────────────────────
	static FString Cmd_Ping(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_GetAllLevelActors();
	static FString Cmd_GetActorComponents(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_GetCurrentLevel();
	static FString Cmd_AssertCurrentLevel(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_GetActorBounds(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_SetViewportCamera(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_RedrawViewports();

	// ─── Actor control ────────────────────────────────────────────────────────
	static FString Cmd_SpawnActor(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_SetActorTransform(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_DeleteActor(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_SaveCurrentLevel();
	static FString Cmd_TakeScreenshot(const TSharedPtr<FJsonObject>& Args);

	// ─── Spatial queries ──────────────────────────────────────────────────────
	static FString Cmd_CastRay(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_QueryNavMesh(const TSharedPtr<FJsonObject>& Args);

	// ─── Blueprint manipulation ───────────────────────────────────────────────
	static FString Cmd_CreateBlueprint(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_CompileBlueprint(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_SetBlueprintCDOProperty(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_EditBlueprintNode(const TSharedPtr<FJsonObject>& Args);

	// ─── Material instancing ──────────────────────────────────────────────────
	static FString Cmd_CreateMaterialInstance(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_SetMaterialParams(const TSharedPtr<FJsonObject>& Args);

	// ─── Content management ───────────────────────────────────────────────────
	static FString Cmd_RenameAsset(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_MoveAsset(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_DeleteAsset(const TSharedPtr<FJsonObject>& Args);

	// ─── Transaction safety ───────────────────────────────────────────────────
	static FString Cmd_BeginTransaction(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_EndTransaction();
	static FString Cmd_UndoTransaction();
	static FString Cmd_CreateSnapshot(const TSharedPtr<FJsonObject>& Args);

	// ─── Python scripting ─────────────────────────────────────────────────────
	static FString Cmd_ExecutePython(const TSharedPtr<FJsonObject>& Args);

	// ─── Performance profiling ────────────────────────────────────────────────
	static FString Cmd_GetPerfStats();

	// ─── Forge meta-commands ──────────────────────────────────────────────────
	static FString Cmd_RunVerification(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_EnforceConstitution(const TSharedPtr<FJsonObject>& Args);
	static FString Cmd_GetForgeStatus();

	// ─── Scene setup ──────────────────────────────────────────────────────────
	static FString Cmd_SetupTestLevel(const TSharedPtr<FJsonObject>& Args);

	// ─── AI asset wiring ──────────────────────────────────────────────────────
	// set_bt_blackboard: links a BlackboardData asset to a BehaviorTree via C++
	// (bypasses Python CPF_Protected restriction on BehaviorTree::BlackboardAsset)
	static FString Cmd_SetBtBlackboard(const TSharedPtr<FJsonObject>& Args);

	// ─── Shared utilities ─────────────────────────────────────────────────────
	static bool            ParseJsonObject(const FString& In, TSharedPtr<FJsonObject>& OutObj, FString& OutErr);
	static FString         ToJsonString(const TSharedPtr<FJsonObject>& Obj);
	static FString         ErrorResponse(const FString& Msg);
	static FString         OkResponse(const FString& Detail = FString());
	static AActor*         FindActorByLabelOrName(const FString& LabelOrName);
	static TSharedPtr<FJsonObject> VecToJson(const FVector& V);
	static bool            IsMutatingCommand(const FString& Cmd);
};
