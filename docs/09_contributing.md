# Contributing

UEAgentForge is an open-source project and contributions are welcome. This document explains how to contribute effectively.

## Development setup

1. Fork the repository: https://github.com/RobVanProd/UEAgentForge
2. Clone your fork into a UE5.5+ project's `Plugins/` directory
3. Open the project in Unreal Editor — the plugin builds automatically
4. Make changes to `Source/UEAgentForge/`
5. Test using the Python client examples in `PythonClient/examples/`

## One change per PR

Follow the same rule as the constitution: **one logical change per pull request**. This makes review fast and keeps the git history clean.

Examples of good PRs:
- Add a single new command (`get_level_stats`)
- Fix a bug in the `cast_ray` hit normal calculation
- Improve the ConstitutionParser to support severity levels

Examples of bad PRs:
- Add 5 new commands + refactor VerificationEngine + update README
- "Various improvements" with unrelated changes

## Adding a new command

Follow this checklist:

**1. Add to the header (`AgentForgeLibrary.h`)**

Document the command in the header comment block:
```cpp
 *   my_new_command      → {ok, result_field}
 *                          args: required_arg, [optional_arg=default]
```

Declare the private handler:
```cpp
static FString Cmd_MyNewCommand(const TSharedPtr<FJsonObject>& Args);
```

**2. Decide: mutating or read-only?**

If the command changes editor state (creates, modifies, or deletes anything): add it to `IsMutatingCommand()`:
```cpp
TEXT("my_new_command"),
```

Read-only commands (observation, queries): add to the direct routing block in `ExecuteCommandJson`.

**3. Implement the handler (`AgentForgeLibrary.cpp`)**

```cpp
FString UAgentForgeLibrary::Cmd_MyNewCommand(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
    FString RequiredArg;
    Args->TryGetStringField(TEXT("required_arg"), RequiredArg);
    if (RequiredArg.IsEmpty()) { return ErrorResponse(TEXT("my_new_command requires 'required_arg'.")); }

    // ... implementation ...

    TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
    Obj->SetBoolField(TEXT("ok"), true);
    Obj->SetStringField(TEXT("result_field"), Result);
    return ToJsonString(Obj);
#else
    return ErrorResponse(TEXT("Editor only."));
#endif
}
```

**4. Add to the routing table**

In `ExecuteCommandJson` (read-only) or inside `ExecuteSafeTransaction`'s real-execute block and rollback-test block (mutating).

**5. Add a Python client method**

In `PythonClient/ueagentforge_client.py`:
```python
def my_new_command(self, required_arg: str, optional_arg: str = "default") -> ForgeResult:
    return self.execute("my_new_command", {
        "required_arg": required_arg,
        "optional_arg": optional_arg,
    })
```

**6. Document the command**

Add a section to `docs/02_command_reference.md` with:
- Command name
- Description
- Args table
- Example response

**7. Add an example (optional but appreciated)**

Create `PythonClient/examples/NN_my_new_command.py` demonstrating the command.

## Testing

There is no automated test suite yet (this is a v0.1.0 limitation). Test manually:

1. Open the editor with a test level (use `setup_test_level` to create a clean baseline)
2. Run `PythonClient/examples/03_verified_workflow.py` — this is the closest thing to an integration test
3. Run your new example
4. Verify the Output Log shows no errors
5. Verify `Ctrl+Z` in the editor correctly undoes your command

## Code style

Follow UE5 C++ conventions:
- `PascalCase` for classes, functions, and variables
- `k` prefix for constants: `const int32 kMaxActors = 100;`
- Always wrap editor code in `#if WITH_EDITOR`
- Use `MakeShared<FJsonObject>()` not `new`
- Prefer `TryGetStringField` over `GetStringField` (safe access)
- Return `ErrorResponse(TEXT("..."))` on all failure paths
- Every handler must have the `#else return ErrorResponse(TEXT("Editor only.")); #endif` block

## Commit messages

Use conventional commits:
- `feat: add get_level_stats command`
- `fix: cast_ray hit normal incorrect for complex geometry`
- `docs: add troubleshooting section for blueprint errors`
- `chore: update Build.cs with missing NavigationSystem dep`

Always include the co-author line:
```
Co-Authored-By: Claude Sonnet 4.6 <noreply@anthropic.com>
```

## What we need most

The highest-value contributions for v0.2.0:

| Feature | Complexity | Impact |
|---|---|---|
| Blueprint node creation (not just editing) | High | High |
| Automated test suite | Medium | High |
| ConstitutionParser severity levels (`[WARN]` vs `[BLOCK]`) | Low | Medium |
| `get_blueprint_graph` — return node list as JSON | Medium | High |
| `set_actor_property` — set any UPROPERTY by name | Medium | High |
| Niagara system control commands | Medium | Medium |
| Material layer parameter support | Low | Medium |
| Multi-GPU perf stats | Low | Low |
| Wiki/GitHub Pages documentation | Low | High |

## License

All contributions are under the MIT license. By submitting a PR, you agree that your contribution is licensed under MIT and that Rob Van retains copyright over the project as a whole.
