# Constitution System

The constitution system allows project teams to define governance rules in a markdown file that UEAgentForge enforces at runtime against every AI agent action. This makes your project's development conventions machine-readable and automatically enforced — not just documentation.

## How it works

At editor startup, `UConstitutionParser::AutoLoadConstitution()` searches for a markdown file in these locations (first match wins):

1. `{ProjectDir}/../ue_dev_constitution.md` (workspace root, siblings to the project)
2. `{ProjectDir}/Constitution/ue_dev_constitution.md`
3. `{ProjectDir}/ue_dev_constitution.md`
4. `{PluginDir}/Constitution/ue_dev_constitution_template.md` (fallback template)

The parser extracts rules from bullet-point lists (`-` or `*`) under headings that contain any of: `Non-negotiable`, `Rules`, `Constraints`, `Requirements`, or `Enforcement`.

## File format

```markdown
# My Project Constitution

## Non-negotiable constraints

- One change per iteration. Never batch multiple unrelated changes.
- No plugin source edits unless explicitly approved by a human reviewer.
- No magic numbers for gameplay tuning. Use UPROPERTY.
- Keep Tick minimal. Disable by default, enable only when necessary.
```

Each bullet becomes a `FConstitutionRule` with:
- **RuleId** — auto-generated (`RULE_000`, `RULE_001`, ...)
- **Description** — the full bullet text
- **TriggerKeywords** — auto-extracted from the description text
- **bIsBlocking** — all rules are blocking in v0.1.0

## Keyword extraction

The parser auto-extracts keywords from each rule using two strategies:

1. **Quoted/backtick phrases** — text inside `` ` `` or `"` is extracted as a whole phrase
2. **Meaningful words** — words longer than 5 characters that are not common stop words

Example rule:
```
- No `Oceanology` source edits (use wrapper/facade integration).
```

Extracted keywords: `Oceanology`, `source`, `wrapper`, `facade`, `integration`

If an action description contains any of these keywords (case-insensitive), the rule fires.

## Validating an action

The `enforce_constitution` command checks a free-text action description:

```bash
# Via HTTP
POST to ExecuteCommandJson with:
{"cmd": "enforce_constitution", "args": {"action_description": "edit the Oceanology plugin header"}}
```

Response:
```json
{
  "allowed": false,
  "violations": [
    "[RULE_003] No `Oceanology` source edits (use wrapper/facade integration)."
  ]
}
```

Allowed action:
```json
{
  "cmd": "enforce_constitution",
  "args": {"action_description": "spawn a static mesh actor at the origin"}
}
```

Response:
```json
{ "allowed": true, "violations": [] }
```

## Checking constitution at startup

The Output Log will show:
```
[UEAgentForge] Constitution loaded: C:/Users/.../ue_dev_constitution.md (12 rules)
```

Or if not found:
```
[UEAgentForge] No constitution file found. Place ue_dev_constitution.md in your project root...
```

Check status at any time:
```json
{ "cmd": "get_forge_status" }
```

```json
{
  "constitution_loaded": true,
  "constitution_rules_loaded": 12,
  "constitution_path": "C:/Users/.../ue_dev_constitution.md"
}
```

## Example constitution for a UE5 game project

```markdown
# UE Development Constitution

## Non-negotiable constraints

- One change per iteration.
- Components over bloated actor classes.
- No Oceanology source edits (use wrapper/facade integration).
- No magic numbers for gameplay tuning. Use UPROPERTY.
- Keep Tick minimal and explicit. Disable by default.
- Prefer additive, reversible edits with clear audit trail.
- Verify behavior (build, logs, or agent response) after each change.
- No direct coupling between systems. Use delegates or interfaces.
- Never modify third-party marketplace plugin source files.
- Confirm target map and asset path is inside the project before mutating.

## Rules for Blueprint editing

- Always compile after modifying a Blueprint graph.
- Do not add nodes that reference engine-private classes.
- Test Blueprint changes in the test map before applying to production levels.

## Rules for C++ changes

- One C++ source file change per build-test cycle.
- Always wrap editor-only code in `#if WITH_EDITOR`.
- Use `UPROPERTY(EditAnywhere)` for all designer-tunable values.

## Rules for content management

- Asset names must follow the naming convention: `{Prefix}_{DescriptiveName}`.
- Never delete assets without checking references first.
- Production content lives under `/Game/` — test content under `/Game/AgentForgeTest/`.
```

## Using the template

Copy the template to your project root:

```bash
cp Plugins/UEAgentForge/Constitution/ue_dev_constitution_template.md ue_dev_constitution.md
```

Then edit it for your project's specific rules. The file is plain markdown — you can include it in your version control and share it with the entire team.

## Python client integration

```python
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()

# Check before any action
chk = client.enforce_constitution("modify Oceanology plugin source")
if not chk.get("allowed"):
    print("BLOCKED:", chk.get("violations"))
    raise SystemExit(1)

# Constitution is also automatically checked inside ExecuteSafeTransaction
# for every mutating command when verify=True
```

## Extending the parser (v0.2.0 roadmap)

Future versions will support:
- **Severity levels** — `[WARN]` vs `[BLOCK]` prefixes on rules
- **Regex patterns** — `[regex: pattern]` for precise matching
- **Allowlists** — explicitly permitted actions that override keyword triggers
- **Remote constitution URLs** — load rules from a team-shared URL
- **Rule categories** — apply different rule sets to different command types
