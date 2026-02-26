# Getting Started

## Overview

UEAgentForge is an Unreal Engine 5 editor plugin that gives AI agents (Claude, GPT, or any HTTP client) a safe, transactional, constitution-enforced control surface over the entire editor. It replaces brittle Remote Control API scripts with a production-grade framework that guarantees rollback, enforces your project's rules, and verifies every change before it lands.

## Prerequisites

| Requirement | Version | Notes |
|---|---|---|
| Unreal Engine | 5.5 or later | 5.7 recommended |
| Remote Control API plugin | Bundled with UE | Must be enabled |
| Python Script Plugin | Bundled with UE | Optional — required for `execute_python` |
| Python (host machine) | 3.9+ | For the Python client only |
| `requests` library | Any recent | `pip install requests` |

## Installation

### Step 1: Add the plugin

Copy (or `git clone`) the `UEAgentForge` folder into your project's `Plugins/` directory:

```
YourProject/
└── Plugins/
    └── UEAgentForge/
        ├── UEAgentForge.uplugin
        ├── Source/
        └── ...
```

Or use it as a submodule:

```bash
cd YourProject/Plugins
git submodule add https://github.com/RobVanProd/UEAgentForge.git UEAgentForge
```

### Step 2: Enable Remote Control API

1. Open your project in Unreal Editor
2. Go to `Edit → Plugins → Remote Control`
3. Enable **Remote Control API** and **Remote Control**
4. Restart the editor when prompted

### Step 3: Re-generate project files

Right-click your `.uproject` file → **Generate Visual Studio project files** (or JetBrains Rider equivalent).

### Step 4: Build

**While editor is closed** (recommended for first build):
```bash
# Windows
"C:\Program Files\Epic Games\UE_5.7\Engine\Build\BatchFiles\Build.bat" ^
    YourProjectEditor Win64 Development ^
    -Project="C:\path\to\YourProject\YourProject.uproject" ^
    -WaitMutex -architecture=x64
```

**While editor is open** (Live Coding):
Press `Ctrl+Alt+F11` — the editor must accept the patch when prompted.

### Step 5: Verify

Open the Output Log (`Window → Output Log`) and look for:

```
[UEAgentForge] Constitution loaded: .../ue_dev_constitution.md (12 rules)
```

If no constitution file is found:
```
[UEAgentForge] No constitution file found. Place ue_dev_constitution.md in your project root...
```

This is a warning, not an error — the plugin works without a constitution.

## Enable the Python client

```bash
pip install requests
cd UEAgentForge/PythonClient
python ueagentforge_client.py
```

You should see:

```json
{
  "pong": "UEAgentForge v0.1.0",
  "version": "0.1.0",
  "constitution_loaded": true,
  "constitution_rules": 12
}
```

## First command

```python
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()
actors = client.get_all_level_actors()
print(f"Level has {len(actors)} actors.")
```

## Next steps

- [Command Reference](02_command_reference.md) — full list of all 30+ commands
- [Verification Protocol](03_verification_protocol.md) — how the 4-phase safety system works
- [Constitution System](04_constitution_system.md) — project governance via markdown
- [Python Client](05_python_client.md) — client API, context managers, examples
- [C++ Integration](06_cpp_integration.md) — using AgentForgeLibrary from C++
- [Architecture](07_architecture.md) — plugin internals deep-dive
- [Troubleshooting](08_troubleshooting.md) — common issues and fixes
