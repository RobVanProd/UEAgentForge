# Troubleshooting

## Connection issues

### "Cannot connect to Unreal Editor at http://127.0.0.1:30010/..."

**Cause:** The Remote Control API is not running, the editor is not open, or the port is blocked.

**Fixes:**
1. Open the Unreal Editor with your project
2. Confirm Remote Control API is enabled: `Edit → Plugins → Remote Control → Remote Control API` ✓
3. Check the Output Log for: `LogRemoteControl: Httpserver started on port 30010`
4. Check Windows Firewall — allow port 30010 for local connections
5. Try: `curl http://127.0.0.1:30010/remote/info` — should return JSON

### `{"error": "UEAgentForge requires WITH_EDITOR."}`

**Cause:** You are calling the plugin in a non-editor build (Game or Server target).

**Fix:** UEAgentForge is Editor-only by design. Only call it from Development Editor builds.

### HTTP 404 / object not found

**Cause:** The plugin is not loaded or the CDO path is wrong.

**Check:**
```bash
curl -X GET http://127.0.0.1:30010/remote/object/describe \
  -H "Content-Type: application/json" \
  -d '{"objectPath": "/Script/UEAgentForge.Default__AgentForgeLibrary"}'
```

If this returns 404, the plugin is not loaded.

**Fix:**
1. Confirm `UEAgentForge.uplugin` is in `Plugins/UEAgentForge/`
2. Check `{ProjectName}.uproject` lists the plugin, or that `EnabledByDefault: true` is set in the uplugin
3. Check the Output Log for build errors when the editor launched

---

## Build errors

### `error C2039: 'FAssetRenameData': is not a member of 'IAssetTools'`

**Cause:** Wrong include. In UE 5.7, `FAssetRenameData` is declared in `IAssetTools.h`, not a separate header.

**Fix:** The plugin already includes `IAssetTools.h` — do not add a separate `AssetRenameData.h` include.

### `error: 'GNumPrimitivesDrawnRHI': redefinition...` or array subscript error

**Cause:** In UE 5.7, `GNumPrimitivesDrawnRHI` is an array `[MAX_NUM_GPUS]`, not a scalar.

**Fix:** Access as `GNumPrimitivesDrawnRHI[0]` — already done in the plugin source.

### `'IPythonScriptPlugin' : base class undefined`

**Cause:** PythonScriptPlugin module not in Build.cs.

**Fix:** Add `"PythonScriptPlugin"` to `PrivateDependencyModuleNames` — already present in `UEAgentForge.Build.cs`. If you removed it, add it back.

### `LNK2019: unresolved external symbol` for navigation functions

**Cause:** `NavigationSystem` module missing.

**Fix:** Already in `UEAgentForge.Build.cs`. If you're extending the plugin in your own module, add `"NavigationSystem"` to your Build.cs.

---

## Runtime errors

### `{"error": "No editor world."}`

**Cause:** The editor has no level open, or GEditor is null (very early startup).

**Fix:** Open a level in the editor before sending commands. Wait for the editor to finish loading.

### `{"error": "Actor not found: MyLabel"}`

**Cause:** The actor label doesn't exist in the current level.

**Debug:** Use `get_all_level_actors` to see what labels are actually in the level:
```bash
{"cmd": "get_all_level_actors"}
```

Actor labels are editor display names and can change. Use `object_path` for `set_actor_transform` if the label is unstable.

### `{"error": "Class not found: /Script/Engine.MyClass"}`

**Cause:** The class path is incorrect or the asset is not loaded.

**Fix:** Use the full C++ class path for engine classes (e.g. `/Script/Engine.StaticMeshActor`). For Blueprint classes, use the generated class path ending in `_C` (e.g. `/Game/MyBPs/BP_MyActor.BP_MyActor_C`).

### `{"error": "Blueprint has no generated class CDO."}`

**Cause:** The Blueprint has never been compiled, or compilation failed previously.

**Fix:** Call `compile_blueprint` first, then retry `set_bp_cdo_property`.

---

## Verification failures

### Phase 2 Snapshot+Rollback FAILED: "expected N actors, got M after undo"

**Cause:** The command type does not support reliable FScopedTransaction rollback. Some engine operations (e.g. certain destructive asset operations) cannot be cleanly undone.

**Workaround:** Run with `phase_mask=1` (PreFlight only) for commands that don't support rollback. Manually take a snapshot before and restore from it if needed.

### Phase 1 FAILED: Constitution violations

**Cause:** The action description matched one or more constitution rule keywords.

**Fix:**
1. Review the violations in the error response
2. If the action is legitimately needed, get explicit approval from a human reviewer and either update the constitution or use `phase_mask=0x04|0x08` to bypass Phase 1 for this command
3. If the violation is a false positive, refine the trigger keywords in your constitution file

### `{"error": "Constitution violations: [RULE_000] One change per iteration..."}`

**Cause:** Common for commands with names like `batch`, `multiple`, or `all` in the action description.

**Fix:** The PreFlight description is the `cmd` field value. Keep command names simple and the constitution will not trigger on them. The keyword extraction is keyword-based, not intent-based.

---

## Python client issues

### `ModuleNotFoundError: No module named 'requests'`

```bash
pip install requests
```

### `json.JSONDecodeError` on response

**Cause:** The Remote Control API returned something unexpected (HTML error page, empty body).

**Debug:** Enable verbose mode:
```python
client = AgentForgeClient(verbose=True)
```

This logs every request and raw response to DEBUG.

### Timeout errors on `run_verification`

**Cause:** Phase 4 (BuildCheck) triggers Blueprint compilation which can take 5-30+ seconds in large projects.

**Fix:** Increase the timeout or run without Phase 4:
```python
client = AgentForgeClient(timeout=120.0)
report = client.run_verification(phase_mask=7)  # skip BuildCheck
```

---

## Constitution not loading

### "No constitution file found"

**Cause:** No `ue_dev_constitution.md` found in any search location.

**Fix:**
```bash
cp Plugins/UEAgentForge/Constitution/ue_dev_constitution_template.md ue_dev_constitution.md
```

Place it at the workspace root (sibling to your `.uproject` file) or inside `{ProjectDir}/`.

### Constitution loaded but validation always passes

**Cause:** The rule keywords extracted from your constitution don't match the action descriptions being tested.

**Debug:** Check what rules were loaded:
```python
status = client.get_forge_status()
print(status["constitution_rules_loaded"])  # how many rules
```

Test a specific action:
```python
result = client.enforce_constitution("edit Oceanology source")
print(result)
```

If it returns `allowed: true` when you expect `false`, the keyword "Oceanology" or "source" may not have been extracted from your rule. Check that the rule text contains those words and isn't wrapped in parentheses or other punctuation that strips them.

---

## Performance

### Commands take >5 seconds

Most commands should complete in <100ms. Slow commands:
- `run_verification` with `phase_mask=15` — normal, Phase 4 compiles BPs
- `get_all_level_actors` in very large levels (1000+ actors) — normal
- `take_screenshot` — depends on viewport resolution

### High memory usage after many snapshots

Snapshots are written to disk but also kept in `Saved/AgentForgeSnapshots/`. Clean old ones:
```bash
# Windows PowerShell
Remove-Item "C:\...\Saved\AgentForgeSnapshots\*.json" -Force
```

---

## Getting help

- **GitHub Issues:** https://github.com/RobVanProd/UEAgentForge/issues
- **Output Log:** Always check `Window → Output Log` in the editor for `[UEAgentForge]` prefixed messages
- **Verbose HTTP:** Enable verbose logging in the Python client to see raw request/response pairs
