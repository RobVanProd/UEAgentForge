# C++ Integration

You can call `UAgentForgeLibrary` directly from C++ inside your project, or extend it with project-specific commands.

## Build setup

Add `UEAgentForge` to your project's `Build.cs`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "UEAgentForge",
});
```

Include the public header:

```cpp
#include "AgentForgeLibrary.h"
```

## Executing commands from C++

```cpp
#include "AgentForgeLibrary.h"

// Simple ping
FString PingResult = UAgentForgeLibrary::ExecuteCommandJson(TEXT("{\"cmd\":\"ping\"}"));

// Spawn actor with args
FString SpawnResult = UAgentForgeLibrary::ExecuteCommandJson(
    TEXT("{\"cmd\":\"spawn_actor\",\"args\":{\"class_path\":\"/Script/Engine.StaticMeshActor\",\"x\":0,\"y\":0,\"z\":200}}"));
```

## Safe transaction wrapper

Use `ExecuteSafeTransaction` when you want the full 4-phase pipeline from C++:

```cpp
FString OutResult;
const bool bSuccess = UAgentForgeLibrary::ExecuteSafeTransaction(
    TEXT("{\"cmd\":\"spawn_actor\",\"args\":{\"class_path\":\"/Script/Engine.StaticMeshActor\"}}"),
    OutResult);

if (!bSuccess)
{
    UE_LOG(LogTemp, Error, TEXT("Safe transaction failed: %s"), *OutResult);
}
```

## Blueprint callable functions

All `UFUNCTION(BlueprintCallable)` methods are available in Blueprint:

| Function | Category |
|---|---|
| `ExecuteCommandJson` | `AgentForge\|Core` |
| `ExecuteSafeTransaction` | `AgentForge\|Core` |
| `RunVerificationProtocol` | `AgentForge\|Verification` |
| `EnforceConstitution` | `AgentForge\|Constitution` |
| `ExecutePythonScript` | `AgentForge\|Scripting` |
| `EditBlueprintNode` | `AgentForge\|Blueprint` |

## Using VerificationEngine directly

```cpp
#include "VerificationEngine.h"

UVerificationEngine* VE = UVerificationEngine::Get();

// Run all phases
TArray<FVerificationPhaseResult> Results;
const bool bAllPassed = VE->RunPhases(
    static_cast<int32>(EVerificationPhase::All),
    TEXT("MyAction"),
    Results);

for (const FVerificationPhaseResult& R : Results)
{
    UE_LOG(LogTemp, Log, TEXT("[%s] Passed=%d: %s (%.1fms)"),
        *R.PhaseName, R.Passed, *R.Detail, R.DurationMs);
}

// Create a snapshot
FString SnapPath = VE->CreateSnapshot(TEXT("before_big_change"));

// Run individual phases
FVerificationPhaseResult Pre = VE->RunPreFlight(TEXT("delete_actor"));
FVerificationPhaseResult Post = VE->RunPostVerify(/*ExpectedActorDelta=*/-1);
FVerificationPhaseResult Build = VE->RunBuildCheck();
```

## Using ConstitutionParser directly

```cpp
#include "ConstitutionParser.h"

UConstitutionParser* Parser = UConstitutionParser::Get();

// Check if constitution is loaded
if (!Parser->IsLoaded())
{
    Parser->AutoLoadConstitution();
}

// Validate an action
TArray<FString> Violations;
const bool bAllowed = Parser->ValidateAction(
    TEXT("edit the Oceanology plugin source"), Violations);

if (!bAllowed)
{
    for (const FString& V : Violations)
    {
        UE_LOG(LogTemp, Warning, TEXT("Constitution violation: %s"), *V);
    }
}

// Load a custom constitution file
const int32 RuleCount = Parser->LoadConstitution(
    TEXT("C:/MyProject/my_constitution.md"));
UE_LOG(LogTemp, Log, TEXT("Loaded %d rules"), RuleCount);
```

## Adding project-specific commands

To add custom commands without modifying the plugin source, create a subclass in your project:

**`Source/MyProject/Public/MyForgeExtensions.h`**
```cpp
#pragma once

#include "AgentForgeLibrary.h"
#include "MyForgeExtensions.generated.h"

UCLASS()
class MYPROJECT_API UMyForgeExtensions : public UAgentForgeLibrary
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Custom")
    static FString ExecuteCommandJson(const FString& RequestJson);
};
```

**`Source/MyProject/Private/MyForgeExtensions.cpp`**
```cpp
#include "MyForgeExtensions.h"

FString UMyForgeExtensions::ExecuteCommandJson(const FString& RequestJson)
{
    // Parse the command
    TSharedPtr<FJsonObject> Root;
    // ... parse RequestJson ...

    FString Cmd;
    Root->TryGetStringField(TEXT("cmd"), Cmd);

    // Handle project-specific commands
    if (Cmd == TEXT("my_custom_command"))
    {
        // Your implementation
        return TEXT("{\"ok\":true}");
    }

    // Fall back to parent for all standard commands
    return Super::ExecuteCommandJson(RequestJson);
}
```

Then update the `objectPath` in your client to point to your subclass:
```
/Script/MyProject.Default__MyForgeExtensions
```

## Snapshot format

Snapshots are plain JSON and can be parsed in C++:

```cpp
#include "Misc/FileHelper.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FString SnapJson;
FFileHelper::LoadFileToString(SnapJson, TEXT("C:/...Saved/AgentForgeSnapshots/my_snap.json"));

TSharedPtr<FJsonObject> Snap;
TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SnapJson);
FJsonSerializer::Deserialize(Reader, Snap);

const int32 ActorCount = (int32)Snap->GetNumberField(TEXT("actor_count"));
```

## Threading notes

All commands in `AgentForgeLibrary.cpp` use `GEditor` and `UWorld` — these must be called on the **game thread**. The Remote Control API already marshals incoming HTTP requests to the game thread before invoking your function, so this is handled automatically when using the HTTP interface.

If calling from C++ directly (e.g. from an editor button click), you are already on the game thread. If calling from a background thread, use `AsyncTask(ENamedThreads::GameThread, [...])`.
