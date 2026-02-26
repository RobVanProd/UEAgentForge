#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "VerificationEngine.generated.h"

/**
 * Phase bitmask constants for RunVerificationProtocol.
 */
UENUM(BlueprintType)
enum class EVerificationPhase : uint8
{
	PreFlight   = 0x01,  // Validate constitution, capture pre-state
	Snapshot    = 0x02,  // Auto-snapshot + rollback test (error injection)
	PostVerify  = 0x04,  // Verify expected state changes occurred
	BuildCheck  = 0x08,  // Trigger blueprint compilation, check for errors
	All         = 0x0F,  // All phases
};

/**
 * Result from a single verification phase.
 */
USTRUCT(BlueprintType)
struct UEAGENTFORGE_API FVerificationPhaseResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FString  PhaseName;
	UPROPERTY(BlueprintReadOnly) bool     Passed = false;
	UPROPERTY(BlueprintReadOnly) FString  Detail;
	UPROPERTY(BlueprintReadOnly) float    DurationMs = 0.f;
};

/**
 * UVerificationEngine — 4-phase safety protocol for all AI-driven mutations.
 *
 * Phase 1 (PreFlight):
 *   - Query constitution rules, reject if violation detected
 *   - Serialize pre-state (actor transforms, properties) for later comparison
 *
 * Phase 2 (Snapshot + Rollback Test):
 *   - Create named snapshot of current level state
 *   - Execute the command in a sub-transaction
 *   - Inject a simulated failure → undo the sub-transaction
 *   - Verify level state exactly matches the pre-snapshot (byte-level actor count + labels)
 *   - Re-execute the command for real
 *
 * Phase 3 (PostVerify):
 *   - Re-query state after execution
 *   - Compare with expected deltas (actor count change, property values)
 *   - Flag unexpected side-effects
 *
 * Phase 4 (BuildCheck):
 *   - Iterate all dirty Blueprints and attempt compilation
 *   - Collect compilation errors
 *   - On error: undo the transaction and report
 */
UCLASS(NotBlueprintable)
class UEAGENTFORGE_API UVerificationEngine : public UObject
{
	GENERATED_BODY()

public:
	/** Singleton accessor — one engine per editor session. */
	static UVerificationEngine* Get();

	/** Run selected verification phases. Returns true if all selected phases passed. */
	bool RunPhases(int32 PhaseMask, const FString& ActionDesc, TArray<FVerificationPhaseResult>& OutResults);

	/** Phase 1: Constitution + pre-state capture. */
	FVerificationPhaseResult RunPreFlight(const FString& ActionDesc);

	/**
	 * Phase 2: Auto-snapshot, execute the pending command in a temporary sub-transaction,
	 * inject failure (cancel sub-tx), verify rollback, then execute for real.
	 * ExecuteCmd is the lambda that performs the actual mutation.
	 */
	FVerificationPhaseResult RunSnapshotRollback(TFunction<bool()> ExecuteCmd, const FString& SnapshotLabel);

	/** Phase 3: Post-execution state comparison against pre-state. */
	FVerificationPhaseResult RunPostVerify(int32 ExpectedActorDelta = 0);

	/** Phase 4: Compile all dirty Blueprints and check for errors. */
	FVerificationPhaseResult RunBuildCheck();

	/** Create a JSON snapshot of all actors in the current level. Returns snapshot file path. */
	FString CreateSnapshot(const FString& SnapshotName);

	/** Compare two snapshots and return a human-readable diff summary. */
	FString DiffSnapshots(const FString& SnapshotPathA, const FString& SnapshotPathB);

	/** Record the last verification run result (JSON). */
	FString LastVerificationResult;

private:
	/** Captured actor labels before execution — used by PostVerify. */
	TArray<FString> PreStateActorLabels;
	int32           PreStateActorCount = 0;

	static UVerificationEngine* Singleton;
};
