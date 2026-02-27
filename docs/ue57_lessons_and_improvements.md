# UEAgentForge — UE 5.7 Lessons & Improvement Suggestions

Captured during THE WARDEN project Phase 1 onboarding. Apply these to harden the plugin
for future projects before distribution.

---

## 1. Build.cs — Module Name Fixes

### Problem
`"ObjectTools"` is listed as a standalone module dependency but does not exist as a separate
module in UE 5.7. It is part of `UnrealEd`.

### Fix
Remove `"ObjectTools"` from `PrivateDependencyModuleNames`. The `#include "ObjectTools.h"` in
`AgentForgeLibrary.cpp` resolves correctly via `UnrealEd` which is already listed.

---

## 2. uplugin — Missing Plugin Dependency Declaration

### Problem
`UEAgentForge.Build.cs` depends on `"PythonScriptPlugin"` but the `UEAgentForge.uplugin` does
not list `PythonScriptPlugin` as a plugin dependency. UBT emits a warning:
> Plugin 'UEAgentForge' does not list plugin 'PythonScriptPlugin' as a dependency

### Fix
Add to `UEAgentForge.uplugin`:
```json
"Plugins": [
    {
        "Name": "PythonScriptPlugin",
        "Enabled": true
    }
]
```

---

## 3. EVerificationPhase Enum — Missing Zero Entry

### Problem
`EVerificationPhase` starts at `0x01`. UHT requires all `UENUM` types to have a `0` entry
or it fails with:
> 'EVerificationPhase' does not have a 0 entry!

### Fix
Add `None = 0x00` as the first entry.

---

## 4. FAssetRenameData — Constructor Signature Changed in UE 5.7

### Problem
The old `FAssetRenameData(FSoftObjectPath, FString PackagePath, FString NewName)` 3-argument
constructor no longer exists in UE 5.7. UBT error:
> cannot convert argument 1 from 'FSoftObjectPath' to 'const TWeakObjectPtr<UObject>&'

Two valid constructors remain:
- `FAssetRenameData(const TWeakObjectPtr<UObject>&, const FString& PackagePath, const FString& NewName)`
- `FAssetRenameData(const FSoftObjectPath& OldPath, const FSoftObjectPath& NewPath, bool, bool)`

### Fix
Use the `(FSoftObjectPath, FSoftObjectPath)` form — compute the full new path before construction:
```cpp
// rename_asset
const FString NewPath = FPaths::GetPath(AssetPath) + TEXT("/") + NewName;
RenameData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), FSoftObjectPath(NewPath)));

// move_asset
const FString NewMovePath = DestinationPath + TEXT("/") + AssetName;
MoveData.Add(FAssetRenameData(FSoftObjectPath(AssetPath), FSoftObjectPath(NewMovePath)));
```

---

## 5. Trans->GetQueueLength() — UTransactor Forward Declaration

### Problem
`GEditor->Trans->GetQueueLength()` fails to compile because `UTransactor` is only
forward-declared in `EditorEngine.h`. Full type is in `Editor/Transactor.h`.

Error:
> use of undefined type 'UTransactor'

### Fix (Option A — Include the header):
```cpp
#include "Editor/Transactor.h"
```

### Fix (Option B — Remove the call, return 0):
`ops_count` in the `end_transaction` response is informational only. Return `0` until
a clean include path is confirmed.
Currently using Option B.

---

## 6. Blueprint-Only Project Conversion — Missing Target.cs Files

### Problem
When adding a C++ module to an existing Blueprint-only project, UBT fails with:
> Expecting to find a type to be declared in a target rules named 'HorrorGameEditorTarget'

Blueprint-only projects have no `Source/` directory and no `.Target.cs` files.

### Fix
Always create both before building:
```
Source/ProjectName.Target.cs         (TargetType.Game)
Source/ProjectNameEditor.Target.cs   (TargetType.Editor)
```

Template:
```csharp
using UnrealBuildTool;
public class ProjectNameEditorTarget : TargetRules
{
    public ProjectNameEditorTarget(TargetInfo Target) : base(Target)
    {
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        Type = TargetType.Editor;
        ExtraModuleNames.Add("ProjectName");
    }
}
```

### Suggestion for UEAgentForge
Add a `setup_cpp_module` command that:
1. Creates `Source/ProjectName/` with Build.cs, header, and cpp
2. Creates `Source/ProjectName.Target.cs` and `Source/ProjectNameEditor.Target.cs`
3. Patches the `.uproject` with the Modules entry
4. Optionally triggers a build via shell

---

## 7. Correct Plugin Names for Remote Control in UE 5.7

### Problem
The plan called for adding `"RemoteControlAPI"` as a plugin — this plugin does not exist.

### Correct Names
| Purpose | Plugin Name |
|---|---|
| Core Remote Control framework | `RemoteControl` |
| HTTP web server (port 30010) | `RemoteControlWebInterface` |
| DMX protocol bridge | `RemoteControlProtocolDMX` |

### Note on First Launch
`RemoteControlWebInterface` spins up an embedded Node.js web server on first launch.
First run can take 2-5 minutes to unpack web assets. Subsequent launches are fast (~5s).

---

## 8. ConstitutionParser — Auto-Load Only Finds ue_dev_constitution.md

### Problem
`AutoLoadConstitution()` only searches for files named `ue_dev_constitution.md`.
Project-specific constitutions (e.g., `horror_constitution.md`) are never auto-discovered.

### Current Behaviour
The workspace-root `ue_dev_constitution.md` is found first (path priority 1), so all
projects in the same workspace share that constitution unless overridden.

### Suggestion for UEAgentForge
Extend `AutoLoadConstitution()` to also search for:
- `<project_name>_constitution.md` (e.g., `HorrorGame_constitution.md`)
- Any file matching `*_constitution.md` in the project root
- A `[AgentForge]` section in `Config/DefaultGame.ini` that specifies `ConstitutionPath=`

This would allow each project in a multi-project workspace to carry its own rules.

---

## 9. UEAgentForge.uplugin — Consider Adding RemoteControl Dependency

### Suggestion
The plugin depends on the Remote Control HTTP server being available but doesn't declare
`RemoteControl` or `RemoteControlWebInterface` as plugin dependencies. Consider adding:
```json
{
    "Name": "RemoteControl",
    "Enabled": true
},
{
    "Name": "RemoteControlWebInterface",
    "Enabled": true
}
```
This would auto-enable the required infrastructure when UEAgentForge is enabled.

---

## 10. Process: cp vs robocopy on Windows/Bash

### Observation
When running bash on Windows, `robocopy` path arguments with forward slashes cause
`/E` to be interpreted as a drive letter (`E:/`). Use `cp -r` with Unix paths instead:
```bash
mkdir -p "/c/path/to/dest"
cp -r "/c/path/to/source/." "/c/path/to/dest/"
```

---

## 11. Live Coding + Python Reflection — New UCLASSes Not Visible

### Problem
After a Live Coding patch that introduces new `UCLASS` types:
- `unreal.SanityComponent` → `AttributeError: module 'unreal' has no attribute 'SanityComponent'`
- `unreal.load_class(None, '/Script/HorrorGame.SanityComponent')` → `None`
- `unreal.find_object(None, '/Script/HorrorGame.SanityComponent')` → `None`

Python `unreal` module builds its type table at editor startup. Live Coding patches the DLL
but does not re-trigger Python binding generation.

### Impact
Python-based verification of new component types is unreliable during iterative development.

### Current Workaround
Trust the build output: clean compile + successful patch = component is live.
Restart editor after all components in a phase are added to get full Python reflection.

### Suggestion for UEAgentForge
Add a `check_class_registered` command that uses the C++ `FindObject<UClass>` path directly
(not Python) to verify a UCLASS is live. This works immediately after Live Coding.
Example: `{"cmd":"check_class_registered","args":{"class_path":"/Script/HorrorGame.SanityComponent"}}`

## 12. Live Coding — UEAgentForge Plugin Spurious LNK2011

### Problem
When iterating on the HorrorGame module via Live Coding, UEAgentForge also tries to
re-patch its DLL and fails with:
> `LNK2011: precompiled object not linked in`

This is a Live Coding PCH issue — the UEAgentForge module uses `UseExplicitOrSharedPCHs`
and the patch linker can't find the PCH object.

### Impact
Cosmetic — UEAgentForge was already loaded correctly. The failed patch is ignored.
HorrorGame patch still applies successfully.

### Suggestion for UEAgentForge
Set `PCHUsage = PCHUsageMode.NoPCHs` in `UEAgentForge.Build.cs` OR ensure
`bUseUnity = false` so the module compiles cleanly under Live Coding.
Alternatively: exclude UEAgentForge from Live Coding recompilation entirely since
it only changes during plugin updates, not game iteration.

---

## 13. C1076 — MSVC Internal Heap with UBoxComponent Inheritance

### Problem
Inheriting a `UCLASS` directly from `UBoxComponent` in a public header pulls in the full
physics and collision template chain, exhausting MSVC's internal heap during `PCH` builds:
> `c1xx: fatal error C1076: compiler limit: internal heap limit reached`

`AdditionalCompilerArguments` is NOT a valid field on `ModuleRules` in UE 5.7
(only on `TargetRules`), so the `/Zm300` workaround doesn't apply at module level.

### Fix — Use Composition
Keep the public header lightweight: forward-declare `UBoxComponent` and inherit from
`UActorComponent`. Create the `UBoxComponent` subobject dynamically in `BeginPlay()` or
in the constructor via `CreateDefaultSubobject`.

```cpp
// .h — lightweight
class UBoxComponent;
UCLASS() class HORRORGAME_API UHidingSpotComponent : public UActorComponent { ... };

// .cpp — heavy include stays in translation unit
#include "Components/BoxComponent.h"
TriggerBox = NewObject<UBoxComponent>(Owner, TEXT("HidingSpotTrigger"));
TriggerBox->RegisterComponent();
```

This pattern applies to any component that needs to own `UShapeComponent`,
`UPrimitiveComponent`, or `ULightComponent` subobjects.

---

## 14. create_blueprint + ExecuteSafeTransaction — Hard Crash on Name Collision

### Problem
`create_blueprint` runs inside `ExecuteSafeTransaction`, which executes the command TWICE:
once in a rollback-test sub-transaction, then again for real. Blueprint creation via
`FKismetEditorUtilities::CreateBlueprint` leaves a stale UPackage behind even after the
sub-transaction is cancelled (undo doesn't clean up the package). The second real call then
finds a BP with the same name in the package and hard-crashes with:

> `Assertion failed: FindObject<UBlueprint>(Outer, *NewBPName.ToString()) == 0`
> `[Kismet2.cpp:435]`

The editor process is killed. All unsaved work is lost.

### Root Cause
Blueprint creation is NOT fully undoable — `CreatePackage` + `FKismetEditorUtilities::CreateBlueprint`
creates on-disk state that `FScopedTransaction::Cancel()` cannot reverse.

### Fix Applied (in plugin copy)
Guard `Cmd_CreateBlueprint` with a `FindObject<UBlueprint>` check before calling
`FKismetEditorUtilities::CreateBlueprint`. If the BP already exists, return it idempotently
instead of crashing.

### Deeper Fix for UEAgentForge v0.2.0
Non-undoable commands (`create_blueprint`, `create_material_instance`) should be **excluded
from `ExecuteSafeTransaction`** and routed to a simpler execution path that skips the
rollback-test phase. Add a `IsFullyUndoable(cmd)` helper that returns false for these commands.

---

---

## 15. execute_python — Output Capture Mode

### Problem
`execute_python` runs Python in **exec mode** (not eval mode). Expression return values are
**never captured** in the `"output"` field. The output field always returns `"None"` regardless
of the script's logic. `print()` output also goes to stdout, which is not captured.

### Fix — File-Based Results Pattern
Write results to a JSON file in the script and `Read` the file afterwards:
```python
# In the python script
import json
results = {"class_loaded": repr(unreal.load_class(None, '/Script/Game.MyClass'))}
with open(r'C:/path/to/results.json', 'w') as f:
    json.dump(results, f, indent=2)
```
Then read `results.json` with the `Read` tool to see actual values.

---

## 16. SubobjectDataSubsystem Python API Differences

### Problem (UE 5.7)
Several expected Python API methods/attributes do NOT exist on SubobjectDataHandle or the subsystem:

| Missing | Workaround |
|---|---|
| `handle.is_valid()` | Check `str(fail_reason) not in ('None', '')` after `add_new_subobject` |
| `subsystem.get_subobject_data_from_handle(h)` | Not exposed — use handle count for duplicate detection |
| `blueprint.simple_construction_script` | Not exposed — use handle count from `k2_gather_subobject_data_for_blueprint` |
| `blueprint.parent_class` | Not exposed — know the parent class from project design docs |
| `get_default_object()` on a Python class var | Calling on `generated_class` attr returns bound method, not UClass |

### Pattern for Duplicate Detection
`k2_gather_subobject_data_for_blueprint(bp)` handle count reveals duplicates.
Expected counts (ACharacter-derived with 5 custom components): ~13 handles.
If count is 30+, duplicates exist — delete and recreate the blueprint.

---

## 17. BlackboardKeyType / BehaviorTree — Not Python-Settable

### Problem
The following are either not exposed or protected in UE 5.7 Python:

- `unreal.BlackboardKeyType_Object` → `AttributeError: module 'unreal' has no attribute 'BlackboardKeyType_Object'`
- `unreal.BlackboardKeyType_Bool` → same
- `unreal.BlackboardKeyType_Vector` → same
- `BehaviorTree.BlackboardAsset` → `"Property 'BlackboardAsset' is protected and cannot be read"`

### Consequence
BB_Warden keys (TargetActor, PatrolLocation, bIsHunting, bIsAlerted, bPlayerSeen) and
BT_Warden's Blackboard reference must be set **manually in the editor**.

### Manual Steps (2 minutes)
1. Open BB_Warden → New Key for each: TargetActor (Object→Actor), PatrolLocation (Vector),
   bIsHunting (Bool), bIsAlerted (Bool), bPlayerSeen (Bool)
2. Open BT_Warden → Details → Blackboard Asset → BB_Warden
3. Open BP_WardenCharacter → Event Graph → BeginPlay → Run Behavior Tree (BT_Warden)

### Suggestion for UEAgentForge v0.2.0
Add `add_blackboard_key` and `set_bt_blackboard` commands that call the native C++ API
directly (bypassing Python's property protection) rather than relying on Python reflection.

---

## 18. Remote Control API — PUT not POST

### Problem
All Remote Control API routes use `PUT`, not `POST`. Calling with `POST` returns:
> `errors.com.epicgames.httpserver.route_handler_not_found`

### Fix
Use `curl -X PUT` (or any HTTP client's PUT method) for all `/remote/object/call` requests.

---

## 19. execute_python — Must Bypass ExecuteSafeTransaction

### Problem
`execute_python` was listed in `IsMutatingCommand()` and therefore wrapped in
`ExecuteSafeTransaction`. Python scripts that call non-undoable operations
(`new_level`, `load_level`, `EditorAssetLibrary.make_directory`, Blueprint factory
creation, etc.) cause the rollback verification to fail with:
> Snapshot+Rollback FAILED: Rollback verification FAILED: expected N actors, got M after undo.

Paradoxically, the rollback failure often leaves the side-effects intact (actors remain
in the level, level file is created on disk) while blocking the real-phase execution —
leading to a partially-built level with no clean re-run path.

### Root Cause
`ExecuteSafeTransaction` runs the command TWICE (test phase → real phase). Python scripts
can do arbitrary world-state changes that UE's transaction system cannot cleanly reverse.

### Fix Applied (AgentForgeLibrary.cpp — HorrorGame plugin copy)
1. Removed `execute_python` from `MutatingCmds` in `IsMutatingCommand()`.
2. Removed `execute_python` from both dispatch tables inside `ExecuteSafeTransaction`.
3. Added `execute_python` to the direct-dispatch block alongside read-only commands.

Python scripts now run ONCE with no rollback wrapper. Callers are responsible for
idempotency (check-before-spawn guards, etc.).

---

## 20. take_screenshot — HighResShot Breaks on Paths With Spaces

### Problem
`Cmd_TakeScreenshot` uses:
```cpp
GEditor->Exec(World, *FString::Printf(TEXT("HighResShot %s"), *Path));
```
`FPaths::ProjectSavedDir()` expands to `C:\Users\Rob\Documents\Unreal Projects\HorrorGame\Saved\`.
The space in `Unreal Projects` causes HighResShot to split the path at the space
and treat only `…\Unreal` as the target path — the screenshot is silently discarded.

### Root Cause — Two Bugs
1. `FPaths::ProjectSavedDir()` expands to a path with spaces → HighResShot splits on space.
2. `HighResShot` does NOT accept filename paths at all — only resolution multipliers
   (`HighResShot 2`) or resolution specs (`HighResShot 1920x1080`). Passing a path is
   always "Bad input." The original code was fundamentally broken.

### Fix Applied (AgentForgeLibrary.cpp — HorrorGame plugin copy)
Replaced HighResShot with `FScreenshotRequest::RequestScreenshot()`, the correct
programmatic API. Uses `C:/HGShots/` staging dir (no spaces), file saved on next frame:
```cpp
#include "UnrealClient.h"   // FScreenshotRequest

const FString Dir = TEXT("C:/HGShots");
IFileManager::Get().MakeDirectory(*Dir, true);
const FString Path = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), *Filename, *Timestamp));
FScreenshotRequest::RequestScreenshot(Path, false, false);
```

### Note for Distribution
`FScreenshotRequest::RequestScreenshot` writes on the next rendered frame — the path
is returned immediately. Callers must wait ~1-2 frames before reading the file.

---

## Summary — Priority Order for UEAgentForge v0.2.0

| # | Fix | Impact |
|---|---|---|
| 1 | Remove `ObjectTools` from Build.cs | Blocks build |
| 2 | Add `None=0` to EVerificationPhase | Blocks UHT |
| 3 | Fix `FAssetRenameData` constructor | Blocks compilation |
| 4 | Add PythonScriptPlugin to uplugin deps | Warning on build |
| 5 | Add RemoteControl to uplugin deps | Smoother onboarding |
| 6 | Extend AutoLoadConstitution to find project-named files | Governance correctness |
| 7 | Add `setup_cpp_module` command | Accelerates project onboarding |
| 8 | Fix Trans->GetQueueLength (add Transactor.h include) | Correctness (currently returns 0) |
| 9 | Remove execute_python from ExecuteSafeTransaction | Hard crash / partial level builds |
| 10 | Fix take_screenshot HighResShot path (spaces) | Screenshots silently discarded |

---

## Lessons 21–30 — THE WARDEN Session (2026-02-27)

### Lesson 21: NoPCHs requires explicit includes in ALL files

**Problem:** `PCHUsage = NoPCHs` causes every `.cpp` and `.h` to be compiled without the shared precompiled header, exposing all implicit dependencies.

**Errors seen:**
- `AgentForgeLibrary.h`: `'FJsonObject': undeclared identifier`, `TSharedPtr` errors
- `VerificationEngine.cpp`: `'FScopedTransaction': undeclared identifier`
- `AgentForgeLibrary.cpp`: `'FScopedTransaction': undeclared identifier`

**Fixes:**
```cpp
// AgentForgeLibrary.h — add before .generated.h:
#include "Dom/JsonObject.h"           // FJsonObject, TSharedPtr

// VerificationEngine.cpp and AgentForgeLibrary.cpp — add in WITH_EDITOR block:
#include "ScopedTransaction.h"        // FScopedTransaction
```

**Why NoPCHs?** Prevents LNK2011 "precompiled object not linked" errors during Live Coding patches. For small modules like UEAgentForge, per-file compilation is fast enough.

---

### Lesson 22: execute_python must bypass ExecuteSafeTransaction

**Problem:** `execute_python` was included in `IsMutatingCommand()` and routed through `ExecuteSafeTransaction`, which runs the command in a test transaction, cancels it (rollback test), then re-runs for real. Python scripts that call `new_level()`, `load_level()`, or other non-undoable operations cause rollback verification to fail: "expected N actors, got M after undo."

**Fix:** Route `execute_python` directly in `ExecuteCommandJson()` BEFORE the `ExecuteSafeTransaction` call, and remove it from `IsMutatingCommand()`.

```cpp
// In ExecuteCommandJson(), before ExecuteSafeTransaction:
if (Cmd == TEXT("execute_python")) { return Cmd_ExecutePython(Args); }
```

**Why:** Python scripts often do file I/O, level management, or complex multi-step operations that are inherently non-undoable. The safety guarantee for Python scripts must come from the script itself, not from the C++ transaction wrapper.

---

### Lesson 23: FScreenshotRequest needs an active render frame

**Problem:** `FScreenshotRequest::RequestScreenshot(Path, false, false)` schedules a screenshot for the next rendered frame. If the viewport is idle (editor in background, no user interaction), no frames render and the screenshot is never written to disk.

**Symptoms:** Command returns `{ok: true, path: "..."}` but the file never appears.

**Solution 1 — focused_shot.py pattern:**
```python
import ctypes, time, unreal
user32 = ctypes.windll.user32
# Find UE editor window and bring to foreground
EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_int, ctypes.c_int)
ue_hwnd = None
def _enum(hwnd, _):
    global ue_hwnd
    if user32.IsWindowVisible(hwnd):
        buf = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, buf, 256)
        if 'HorrorGame' in buf.value or 'Unreal Editor' in buf.value:
            ue_hwnd = hwnd
    return True
user32.EnumWindows(EnumProc(_enum), 0)
if ue_hwnd:
    user32.ShowWindow(ue_hwnd, 9)
    user32.SetForegroundWindow(ue_hwnd)
    time.sleep(0.3)   # brief focus settle — OK since very short
# Issue HighResShot and return IMMEDIATELY (no sleep after — sleep blocks game thread!)
world = unreal.EditorLevelLibrary.get_editor_world()
unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1920x1080')
# Shell must sleep 10-12s EXTERNALLY before reading the screenshot file
```

**Solution 2 — redraw_viewports command (new in AgentForge):**
Call `redraw_viewports` before `take_screenshot` to force a frame render, giving `FScreenshotRequest` a frame to capture.

**Critical rule:** NEVER use `time.sleep()` after issuing a render command inside a UE Python script. `time.sleep()` in UE Python blocks the **game thread**, which prevents UE from rendering any frames and thus prevents the screenshot from being captured.

---

### Lesson 24: HighResShot increments filename counter

**Problem:** `HighResShot 1920x1080` always saves to `Saved/Screenshots/WindowsEditor/HighresScreenshot000XX.png` and auto-increments the counter (00000, 00001, 00002...). If the old file wasn't deleted, the new one gets a new number.

**Fix:** Track the counter or delete old screenshots before issuing HighResShot. Alternatively check for the latest file in the directory after the 12s wait.

---

### Lesson 25: UnrealEditorSubsystem has set_level_viewport_camera_info

**Problem:** `unreal.EditorViewportLibrary` is not available in all UE 5.7 projects (depends on whether the module is loaded).

**Working alternative:**
```python
subsystem = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
subsystem.set_level_viewport_camera_info(
    unreal.Vector(x, y, z),
    unreal.Rotator(pitch, yaw, roll)
)
```

**In C++ (AgentForge set_viewport_camera command):** Use `GEditor->GetLevelViewportClients()` and `FLevelEditorViewportClient::SetViewLocation/SetViewRotation/Invalidate()`.

---

### Lesson 26: unreal.Color vs unreal.LinearColor for light properties

**Problem:** `PointLightComponent.light_color` in UE Python requires `unreal.Color` (uint8, 0-255), NOT `unreal.LinearColor` (float, 0.0-1.0).

**Error:** `TypeError: PointLightComponent: Failed to convert type 'LinearColor' to property 'LightColor' (StructProperty) for attribute 'light_color'`

**Fix:**
```python
# WRONG:
lc.set_editor_property('light_color', unreal.LinearColor(1.0, 0.78, 0.45, 1.0))

# CORRECT:
lc.set_editor_property('light_color', unreal.Color(255, 199, 115, 255))
```

---

### Lesson 27: unreal.Rotator parameter order is (roll, pitch, yaw) in UE Python

**Observed behavior:** `unreal.Rotator(0, 0, 180)` results in `{pitch: 0, yaw: 180, roll: 0}` when read back via `get_actor_rotation()`. This suggests the Python binding order is **(roll, pitch, yaw)**, not the C++ convention of `(pitch, yaw, roll)`.

**Recommendation:** Always use keyword arguments to avoid ambiguity:
```python
# Safe — always use keyword args:
unreal.Rotator(pitch=0, yaw=180, roll=0)    # NOT unreal.Rotator(0, 180, 0)
```

**Note:** Verify this against your UE 5.7 Python stubs — the order may differ between UE versions.

---

### Lesson 28: set_mobility works on Actor root component

**Problem:** `actor.get_editor_property('mobility')` raises `AttributeError: 'StaticMeshActor' object has no attribute 'mobility'`. Mobility is a property of the *root component*, not the actor.

**Fix:**
```python
# Get mobility:
root = actor.get_editor_property('root_component')
mobility = root.get_editor_property('mobility')

# Set mobility:
actor.set_mobility(unreal.ComponentMobility.MOVABLE)   # actor helper method
```

---

### Lesson 29: SkyLightComponent.recapture() not exposed in UE Python

**Problem:** `sky_light_component.recapture()` raises `AttributeError`. The recapture method is not in the Python bindings.

**Workaround:** Just set the intensity property — the sky light will recapture on the next frame automatically without calling `recapture()` explicitly.

---

### Lesson 30: Material application to spawned actors — verify mesh assignment

**Problem:** Actors spawned via `spawn_actor` get a default engine cube (100×100×100 cm) as their static mesh. `get_editor_property('static_mesh')` may return `None` even though the default cube IS assigned (it's an engine built-in, not a registered asset with a content path).

**Implication:** Material application via `smc.set_material(slot, mat)` works even when `get_editor_property('static_mesh')` returns None — the material IS being applied to the default cube geometry.

**Verification:** Use `mat = smc.get_material(0); print(mat.get_name())` to confirm the material was applied, not `get_editor_property('static_mesh')`.

---

### Lesson 31: System screenshot via PowerShell (sysshot pattern)

For development visibility when the UE viewport doesn't respond to HighResShot (e.g., blocked by terminal window), use PowerShell via Python subprocess to take a full-screen system capture:

```python
import subprocess, os
os.makedirs('C:/HGShots', exist_ok=True)
ps_script = """
Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
$s = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bmp = New-Object System.Drawing.Bitmap($s.Width, $s.Height)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($s.Location, [System.Drawing.Point]::Empty, $s.Size)
$bmp.Save('C:/HGShots/sysshot.png', [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
"""
subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', ps_script],
               capture_output=True, text=True, timeout=15)
```

This captures whatever is visible on screen. **Limitation:** Terminal windows overlay the UE editor and will appear in the capture. Use `focused_shot.py` (Lesson 23) for clean viewport-only screenshots.
