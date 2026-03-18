# UEAgentForge v0.5.0 — LLM Integration & MCP Standardization Game Plan

## Overnight Agent Implementation Guide

**Repository:** `https://github.com/RobVanProd/UEAgentForge`
**Branch:** Create `feature/v050-llm-integration` from `main`
**Goal:** Add a multi-provider LLM API layer, structured outputs, MCP server standardization, vision-in-the-loop, and streaming — making UEAgentForge a complete superset of UnrealGenAISupport's free AND paid features while maintaining our unique transaction safety, constitution enforcement, and OAPA reasoning advantages.

---

## CONTEXT FOR THE AGENT

### Current UEAgentForge Architecture
- UE5.5+ plugin, single module `UEAgentForge`, Editor-only, C++ (80.9%) + Python (18.8%)
- Communicates via UE5 **Remote Control API** over HTTP PUT to `127.0.0.1:30010`
- Core class: `UAgentForgeLibrary` — a `UBlueprintFunctionLibrary` with `ExecuteCommandJson()` as the single entry point
- All 30+ commands dispatched through JSON `{"cmd": "...", "args": {...}}`
- Existing subsystems: `VerificationEngine` (4-phase), `ConstitutionParser`, snapshot system, OAPA reasoning, 5-phase level pipeline, preset system
- Python client: `PythonClient/ueagentforge_client.py`
- Dependencies: `RemoteControl`, `PythonScriptPlugin`
- Plugin descriptor: `UEAgentForge.uplugin` (version 0.1.0, Editor module, Default loading phase)

### What We're Adding
UnrealGenAISupport charges $30-50/mo for: streaming, multimodal/vision, structured JSON outputs, TTS/audio, function calling, extended thinking, OpenAI-compatible mode. We will implement the core game-dev-useful features for FREE, integrated with our existing safety stack.

---

## PHASE 1: Multi-Provider LLM Subsystem (HIGHEST PRIORITY)

### 1.1 Create New Source Files

Create the following files under `Source/UEAgentForge/`:

#### `Public/LLM/AgentForgeLLMTypes.h`

```cpp
// Purpose: All shared types for the LLM subsystem
#pragma once
#include "CoreMinimal.h"
#include "AgentForgeLLMTypes.generated.h"

UENUM(BlueprintType)
enum class EAgentForgeLLMProvider : uint8
{
    Anthropic    UMETA(DisplayName = "Anthropic (Claude)"),
    OpenAI       UMETA(DisplayName = "OpenAI"),
    DeepSeek     UMETA(DisplayName = "DeepSeek"),
    OpenAICompatible UMETA(DisplayName = "OpenAI-Compatible (Ollama/Groq/OpenRouter)")
};

USTRUCT(BlueprintType)
struct FAgentForgeChatMessage
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Role; // "system", "user", "assistant"

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Content;

    // For multimodal: base64-encoded image data
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> ImageData;

    // For multimodal: media types corresponding to ImageData entries
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FString> ImageMediaTypes; // "image/png", "image/jpeg", etc.
};

USTRUCT(BlueprintType)
struct FAgentForgeLLMSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString Model; // e.g. "claude-sonnet-4-20250514", "gpt-4o", "deepseek-chat"

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 MaxTokens = 4096;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float Temperature = 0.7f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TArray<FAgentForgeChatMessage> Messages;

    // Optional: JSON schema string for structured outputs
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString ResponseSchema;

    // Optional: System prompt (convenience, prepended to Messages)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString SystemPrompt;

    // For OpenAI-Compatible mode
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FString CustomEndpoint; // e.g. "http://localhost:11434/v1" for Ollama

    // Streaming
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bStreamResponse = false;
};

USTRUCT(BlueprintType)
struct FAgentForgeLLMResponse
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly)
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly)
    FString Content;

    UPROPERTY(BlueprintReadOnly)
    FString ErrorMessage;

    UPROPERTY(BlueprintReadOnly)
    FString RawJSON;

    // For structured outputs: parsed JSON is in Content
    // For reasoning models: thinking content
    UPROPERTY(BlueprintReadOnly)
    FString ReasoningContent;

    // Token usage
    UPROPERTY(BlueprintReadOnly)
    int32 PromptTokens = 0;

    UPROPERTY(BlueprintReadOnly)
    int32 CompletionTokens = 0;
};

// Delegates
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLLMResponseReceived, const FAgentForgeLLMResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLLMStreamChunk, const FString&, Chunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMResponseMulticast, const FAgentForgeLLMResponse&, Response);
```

#### `Public/LLM/AgentForgeLLMSubsystem.h`

```cpp
// Purpose: Editor subsystem managing LLM API calls
// This is the main public API that Blueprint and C++ consumers use
#pragma once
#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "LLM/AgentForgeLLMTypes.h"
#include "AgentForgeLLMSubsystem.generated.h"

class IAgentForgeLLMProvider;

UCLASS()
class UEAGENTFORGE_API UAgentForgeLLMSubsystem : public UEditorSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // --- Core API ---

    // Send a chat completion request (async, non-blocking)
    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    void SendChatRequest(const FAgentForgeLLMSettings& Settings,
                         const FOnLLMResponseReceived& OnComplete);

    // Send with streaming callback
    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    void SendStreamingChatRequest(const FAgentForgeLLMSettings& Settings,
                                  const FOnLLMStreamChunk& OnChunk,
                                  const FOnLLMResponseReceived& OnComplete);

    // Send structured output request (forces JSON schema compliance)
    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    void SendStructuredRequest(const FString& Prompt,
                               const FString& JsonSchema,
                               const FAgentForgeLLMSettings& Settings,
                               const FOnLLMResponseReceived& OnComplete);

    // --- Key Management ---

    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    static void SetApiKeyRuntime(EAgentForgeLLMProvider Provider, const FString& Key);

    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    static FString GetApiKey(EAgentForgeLLMProvider Provider);

    // --- Utility ---

    UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
    static TArray<FString> GetAvailableModels(EAgentForgeLLMProvider Provider);

private:
    // Provider instances (lazy-initialized)
    TMap<EAgentForgeLLMProvider, TSharedPtr<IAgentForgeLLMProvider>> Providers;
    TMap<EAgentForgeLLMProvider, FString> RuntimeApiKeys;

    TSharedPtr<IAgentForgeLLMProvider> GetOrCreateProvider(EAgentForgeLLMProvider Provider);
};
```

#### `Public/LLM/IAgentForgeLLMProvider.h`

```cpp
// Purpose: Interface that each provider adapter implements
#pragma once
#include "LLM/AgentForgeLLMTypes.h"

class IAgentForgeLLMProvider
{
public:
    virtual ~IAgentForgeLLMProvider() = default;

    // Build the HTTP request for this provider's API format
    virtual void SendRequest(
        const FAgentForgeLLMSettings& Settings,
        const FString& ApiKey,
        TFunction<void(const FAgentForgeLLMResponse&)> OnComplete) = 0;

    // Streaming variant
    virtual void SendStreamingRequest(
        const FAgentForgeLLMSettings& Settings,
        const FString& ApiKey,
        TFunction<void(const FString&)> OnChunk,
        TFunction<void(const FAgentForgeLLMResponse&)> OnComplete) = 0;

    // Return the default API endpoint
    virtual FString GetDefaultEndpoint() const = 0;

    // Return the list of known models
    virtual TArray<FString> GetAvailableModels() const = 0;
};
```

#### `Private/LLM/AgentForgeLLMSubsystem.cpp`

Implementation notes for the agent:
- `Initialize()`: Log startup, don't pre-create providers
- `GetOrCreateProvider()`: Lazy factory — instantiate `FAnthropicProvider`, `FOpenAIProvider`, `FDeepSeekProvider`, or `FOpenAICompatibleProvider`
- `GetApiKey()`: Check RuntimeApiKeys first, then env vars: `AGENTFORGE_ANTHROPIC_KEY`, `AGENTFORGE_OPENAI_KEY`, `AGENTFORGE_DEEPSEEK_KEY`, `AGENTFORGE_CUSTOM_KEY`. Also check the legacy UnrealGenAISupport format (`PS_ANTHROPICAPIKEY` etc.) for drop-in compatibility.
- `SendChatRequest()`: Get provider, get key, validate, call provider->SendRequest()
- `SendStructuredRequest()`: Inject schema into Settings.ResponseSchema, then call SendChatRequest()

#### `Private/LLM/Providers/AnthropicProvider.h` and `.cpp`

```
Endpoint: https://api.anthropic.com/v1/messages
Headers:
  x-api-key: <key>
  anthropic-version: 2023-06-01
  Content-Type: application/json

Request body format:
{
  "model": "<model>",
  "max_tokens": <n>,
  "temperature": <t>,
  "system": "<system_prompt>",
  "messages": [
    {"role": "user", "content": "<text>"}
    // OR for multimodal:
    {"role": "user", "content": [
      {"type": "image", "source": {"type": "base64", "media_type": "image/png", "data": "<b64>"}},
      {"type": "text", "text": "<prompt>"}
    ]}
  ]
}

For structured outputs, use tool_use with a forced tool:
{
  "tools": [{
    "name": "structured_output",
    "description": "Return structured data",
    "input_schema": <parsed_json_schema>
  }],
  "tool_choice": {"type": "tool", "name": "structured_output"}
}

Response parsing:
- content[0].text for text responses
- content[0].input for tool_use responses (structured output)
- usage.input_tokens, usage.output_tokens

Available models (hardcode this list):
- "claude-sonnet-4-20250514"
- "claude-opus-4-20250514"
- "claude-3-7-sonnet-latest"
- "claude-3-5-haiku-latest"

Streaming: Use SSE (text/event-stream), parse "event: content_block_delta" lines
```

#### `Private/LLM/Providers/OpenAIProvider.h` and `.cpp`

```
Endpoint: https://api.openai.com/v1/chat/completions
Headers:
  Authorization: Bearer <key>
  Content-Type: application/json

Request body:
{
  "model": "<model>",
  "max_tokens": <n>,  // NOTE: use "max_completion_tokens" for o-series models
  "temperature": <t>,
  "messages": [
    {"role": "system", "content": "<system>"},
    {"role": "user", "content": "<text>"}
    // OR for vision:
    {"role": "user", "content": [
      {"type": "text", "text": "<prompt>"},
      {"type": "image_url", "image_url": {"url": "data:image/png;base64,<b64>"}}
    ]}
  ]
}

For structured outputs:
{
  "response_format": {
    "type": "json_schema",
    "json_schema": {
      "name": "structured_output",
      "strict": true,
      "schema": <parsed_json_schema>
    }
  }
}

Response parsing:
- choices[0].message.content
- usage.prompt_tokens, usage.completion_tokens

Available models:
- "gpt-4o", "gpt-4o-mini", "gpt-4.1", "gpt-4.1-mini"
- "o3", "o3-mini", "o4-mini" (reasoning — use max_completion_tokens, skip temperature)

Streaming: SSE, parse "data: " lines, extract choices[0].delta.content
```

#### `Private/LLM/Providers/DeepSeekProvider.h` and `.cpp`

```
Endpoint: https://api.deepseek.com/chat/completions
Headers: same as OpenAI format (Authorization: Bearer)

Uses OpenAI-compatible format. Key differences:
- Models: "deepseek-chat" (V3), "deepseek-reasoner" (R1)
- For reasoner: response has reasoning_content field
- System messages mandatory for reasoning model
- No temperature for reasoning model

IMPORTANT: Set HTTP timeout to 180s for reasoning model (it's slow)
```

#### `Private/LLM/Providers/OpenAICompatibleProvider.h` and `.cpp`

```
Same as OpenAI format but:
- Endpoint: Uses Settings.CustomEndpoint (user-provided)
- Default to "http://localhost:11434/v1" (Ollama)
- Key may be empty/dummy for local models
- Model string is passed through as-is

This covers: Ollama, Groq, OpenRouter, LM Studio, vLLM, text-generation-webui
```

### 1.2 HTTP Implementation Details

Use UE5's `FHttpModule`:

```cpp
// Pattern for all providers:
TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
HttpRequest->SetURL(Endpoint);
HttpRequest->SetVerb("POST");
HttpRequest->SetHeader("Content-Type", "application/json");
// ... provider-specific headers ...
HttpRequest->SetContentAsString(RequestBody);
HttpRequest->SetTimeout(180.0f); // generous for reasoning models
HttpRequest->OnProcessRequestComplete().BindLambda(
    [OnComplete](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess) {
        // Parse on game thread via AsyncTask(ENamedThreads::GameThread, ...)
    });
HttpRequest->ProcessRequest();
```

For streaming, UE5's HTTP module doesn't natively support SSE chunked reading. Implement by:
1. Set `HttpRequest->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnHttpThread)`
2. Use `OnRequestProgress()` delegate to receive chunks as they arrive
3. Buffer and parse SSE lines (`data: {...}\n\n`)
4. Fire OnChunk callback for each parsed delta
5. Accumulate full response for OnComplete

### 1.3 Wire Into Existing Command System

Add new commands to `AgentForgeLibrary`'s `ExecuteCommandJson` dispatcher:

| Command | Args | Description |
|---------|------|-------------|
| `llm_chat` | `{provider, model, messages, system, max_tokens, temperature}` | Basic chat completion |
| `llm_structured` | `{provider, model, prompt, schema, system}` | Structured JSON output |
| `llm_stream` | `{provider, model, messages, system}` | Streaming (returns chunks via a polling mechanism or accumulated) |
| `llm_set_key` | `{provider, key}` | Set API key at runtime |
| `llm_get_models` | `{provider}` | List available models |

### 1.4 Update Build.cs

Add to `PublicDependencyModuleNames`:
- `HTTP`
- `Json`
- `JsonUtilities`

These should already be available but ensure they're listed.

### 1.5 Update .uplugin

Bump `VersionName` to `"0.5.0"`. Add to description: "Now includes multi-provider LLM API support (Anthropic, OpenAI, DeepSeek, Ollama) with structured outputs, streaming, and vision."

---

## PHASE 2: Structured Output / Schema Service

### 2.1 Create `Public/LLM/AgentForgeSchemaService.h`

```cpp
UCLASS()
class UEAGENTFORGE_API UAgentForgeSchemaService : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Convenience: send a prompt with a JSON schema and get validated JSON back
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
    static void RequestStructuredOutput(
        const FString& Prompt,
        const FString& JsonSchemaString,
        const FOnLLMResponseReceived& OnComplete,
        EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::OpenAI,
        const FString& Model = TEXT("gpt-4o"));

    // Load schema from file (Content/AgentForge/Schemas/*.json)
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
    static FString LoadSchemaFromFile(const FString& SchemaFileName);

    // Validate a JSON string against a schema
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
    static bool ValidateJsonAgainstSchema(const FString& JsonString, const FString& SchemaString);
};
```

### 2.2 Implementation

- `RequestStructuredOutput`: Creates `FAgentForgeLLMSettings`, sets `ResponseSchema`, calls subsystem
- `LoadSchemaFromFile`: Reads from `FPaths::ProjectContentDir() / "AgentForge/Schemas/"` + filename
- `ValidateJsonAgainstSchema`: Basic validation — check required fields exist, types match. Don't need a full JSON Schema validator; just structural validation.

### 2.3 Ship Example Schemas

Create `Content/AgentForge/Schemas/` directory with example files:

**`npc_personality.json`:**
```json
{
  "type": "object",
  "properties": {
    "name": {"type": "string"},
    "personality_traits": {"type": "array", "items": {"type": "string"}},
    "backstory": {"type": "string"},
    "dialogue_style": {"type": "string", "enum": ["formal", "casual", "aggressive", "timid"]},
    "relationships": {"type": "array", "items": {
      "type": "object",
      "properties": {
        "character": {"type": "string"},
        "attitude": {"type": "string"}
      }
    }}
  },
  "required": ["name", "personality_traits", "backstory", "dialogue_style"]
}
```

**`quest_structure.json`:**
```json
{
  "type": "object",
  "properties": {
    "quest_name": {"type": "string"},
    "description": {"type": "string"},
    "objectives": {"type": "array", "items": {
      "type": "object",
      "properties": {
        "id": {"type": "integer"},
        "description": {"type": "string"},
        "type": {"type": "string", "enum": ["collect", "kill", "escort", "explore", "interact"]},
        "target_count": {"type": "integer"},
        "optional": {"type": "boolean"}
      },
      "required": ["id", "description", "type"]
    }},
    "rewards": {"type": "object", "properties": {
      "xp": {"type": "integer"},
      "items": {"type": "array", "items": {"type": "string"}}
    }},
    "difficulty": {"type": "string", "enum": ["easy", "medium", "hard", "legendary"]}
  },
  "required": ["quest_name", "description", "objectives", "difficulty"]
}
```

**`level_layout.json`:**
```json
{
  "type": "object",
  "properties": {
    "rooms": {"type": "array", "items": {
      "type": "object",
      "properties": {
        "name": {"type": "string"},
        "purpose": {"type": "string"},
        "dimensions": {"type": "object", "properties": {
          "width": {"type": "number"}, "length": {"type": "number"}, "height": {"type": "number"}
        }},
        "connections": {"type": "array", "items": {"type": "string"}},
        "props": {"type": "array", "items": {"type": "string"}},
        "lighting_mood": {"type": "string"}
      },
      "required": ["name", "purpose", "dimensions"]
    }},
    "theme": {"type": "string"},
    "player_start_room": {"type": "string"}
  },
  "required": ["rooms", "theme", "player_start_room"]
}
```

---

## PHASE 3: MCP Server Standardization

### 3.1 Create `PythonClient/mcp_server/agentforge_mcp_server.py`

This wraps UEAgentForge's existing Remote Control HTTP interface as a standard MCP server that Claude Desktop, Cursor, and Windsurf can connect to.

```python
#!/usr/bin/env python3
"""
UEAgentForge MCP Server
Exposes all AgentForge commands as MCP tools for AI assistants.
"""

import json
import os
import requests
from mcp.server import Server
from mcp.types import Tool, TextContent
import mcp.server.stdio

# --- Configuration ---
UNREAL_HOST = os.environ.get("UNREAL_HOST", "127.0.0.1")
UNREAL_PORT = int(os.environ.get("UNREAL_PORT", "30010"))
AGENTFORGE_OBJECT_PATH = "/Script/UEAgentForge.Default__AgentForgeLibrary"

app = Server("agentforge-mcp")

def call_agentforge(cmd: str, args: dict = None) -> dict:
    """Send a command to UEAgentForge via Remote Control API."""
    payload = {"cmd": cmd}
    if args:
        payload["args"] = args

    url = f"http://{UNREAL_HOST}:{UNREAL_PORT}/remote/object/call"
    response = requests.put(url, json={
        "objectPath": AGENTFORGE_OBJECT_PATH,
        "functionName": "ExecuteCommandJson",
        "parameters": {"RequestJson": json.dumps(payload)}
    }, timeout=300)

    return response.json()


# ============================
# TOOL DEFINITIONS
# ============================
# Register every UEAgentForge command as an MCP tool.
# Group them logically for AI assistant discoverability.

@app.list_tools()
async def list_tools():
    return [
        # --- Meta / Health ---
        Tool(name="ping", description="Health check. Returns plugin version and constitution status.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="get_forge_status", description="Plugin version, constitution rules loaded, last verification result.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="run_verification", description="Run 4-phase verification protocol. phase_mask is bitmask 1-15 (1=PreFlight, 2=Snapshot+Rollback, 4=PostVerify, 8=BuildCheck). Use 15 for all.", inputSchema={
            "type": "object",
            "properties": {"phase_mask": {"type": "integer", "default": 15}},
        }),
        Tool(name="enforce_constitution", description="Check if a proposed action is allowed by the project constitution.", inputSchema={
            "type": "object",
            "properties": {"action": {"type": "string", "description": "Description of the action to validate"}},
            "required": ["action"]
        }),

        # --- Observation ---
        Tool(name="get_all_level_actors", description="List all actors in the current level with transforms, classes, and paths.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="get_actor_components", description="Get all components of a named actor.", inputSchema={
            "type": "object",
            "properties": {"actor_name": {"type": "string"}},
            "required": ["actor_name"]
        }),
        Tool(name="get_current_level", description="Get current level package path and map lock status.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="get_actor_bounds", description="Get AABB bounds of an actor.", inputSchema={
            "type": "object",
            "properties": {"actor_name": {"type": "string"}},
            "required": ["actor_name"]
        }),

        # --- Actor Control ---
        Tool(name="spawn_actor", description="Spawn an actor by class path at a transform. Example class: /Script/Engine.StaticMeshActor", inputSchema={
            "type": "object",
            "properties": {
                "class_path": {"type": "string"},
                "x": {"type": "number", "default": 0}, "y": {"type": "number", "default": 0}, "z": {"type": "number", "default": 0},
                "rx": {"type": "number", "default": 0}, "ry": {"type": "number", "default": 0}, "rz": {"type": "number", "default": 0},
            },
            "required": ["class_path"]
        }),
        Tool(name="set_actor_transform", description="Move/rotate/scale an actor by name.", inputSchema={
            "type": "object",
            "properties": {
                "actor_name": {"type": "string"},
                "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"},
                "rx": {"type": "number"}, "ry": {"type": "number"}, "rz": {"type": "number"},
            },
            "required": ["actor_name"]
        }),
        Tool(name="delete_actor", description="Delete an actor by label.", inputSchema={
            "type": "object", "properties": {"actor_name": {"type": "string"}}, "required": ["actor_name"]
        }),
        Tool(name="save_current_level", description="Save the current map.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="take_screenshot", description="Capture viewport to PNG.", inputSchema={"type": "object", "properties": {}}),

        # --- Spatial Queries ---
        Tool(name="cast_ray", description="Line trace from origin in direction, returns hit info.", inputSchema={
            "type": "object",
            "properties": {
                "origin_x": {"type": "number"}, "origin_y": {"type": "number"}, "origin_z": {"type": "number"},
                "dir_x": {"type": "number"}, "dir_y": {"type": "number"}, "dir_z": {"type": "number"},
                "distance": {"type": "number", "default": 10000},
            },
            "required": ["origin_x", "origin_y", "origin_z", "dir_x", "dir_y", "dir_z"]
        }),
        Tool(name="query_navmesh", description="Project a point onto the navigation mesh.", inputSchema={
            "type": "object",
            "properties": {"x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}},
            "required": ["x", "y", "z"]
        }),
        Tool(name="spawn_actor_at_surface", description="Raycast spawn with surface-normal alignment.", inputSchema={
            "type": "object",
            "properties": {"class_path": {"type": "string"}, "x": {"type": "number"}, "y": {"type": "number"}, "z": {"type": "number"}},
            "required": ["class_path", "x", "y", "z"]
        }),
        Tool(name="analyze_level_composition", description="Get actor density, bounding box, AI recommendations for the level.", inputSchema={"type": "object", "properties": {}}),

        # --- Blueprint Manipulation ---
        Tool(name="create_blueprint", description="Create a new Blueprint asset with a parent class.", inputSchema={
            "type": "object",
            "properties": {"asset_path": {"type": "string"}, "parent_class": {"type": "string"}},
            "required": ["asset_path", "parent_class"]
        }),
        Tool(name="compile_blueprint", description="Compile a Blueprint and return errors.", inputSchema={
            "type": "object",
            "properties": {"blueprint_path": {"type": "string"}},
            "required": ["blueprint_path"]
        }),

        # --- Material & Content ---
        Tool(name="create_material_instance", description="Create a Material Instance Constant from a parent material.", inputSchema={
            "type": "object",
            "properties": {"asset_path": {"type": "string"}, "parent_material": {"type": "string"}},
            "required": ["asset_path", "parent_material"]
        }),
        Tool(name="set_material_params", description="Set scalar/vector parameters on a Material Instance.", inputSchema={
            "type": "object",
            "properties": {"material_path": {"type": "string"}, "params": {"type": "object"}},
            "required": ["material_path", "params"]
        }),

        # --- Transaction Safety ---
        Tool(name="begin_transaction", description="Open a named undo transaction.", inputSchema={
            "type": "object",
            "properties": {"name": {"type": "string"}},
            "required": ["name"]
        }),
        Tool(name="end_transaction", description="Commit the open transaction.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="undo_transaction", description="Undo the last transaction.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="create_snapshot", description="Create a JSON snapshot of all actor states.", inputSchema={
            "type": "object",
            "properties": {"name": {"type": "string"}},
            "required": ["name"]
        }),

        # --- Python Scripting ---
        Tool(name="execute_python", description="Run Python code in the Unreal editor process.", inputSchema={
            "type": "object",
            "properties": {"code": {"type": "string"}},
            "required": ["code"]
        }),

        # --- Performance ---
        Tool(name="get_perf_stats", description="Get frame time, draw calls, memory, and actor count.", inputSchema={"type": "object", "properties": {}}),

        # --- OAPA Reasoning (v0.3.0) ---
        Tool(name="observe_analyze_plan_act", description="Full OAPA closed-loop: Observe→Analyze→Plan→Act→Verify. Iterates until score target is met.", inputSchema={
            "type": "object",
            "properties": {
                "goal": {"type": "string", "description": "Natural language goal"},
                "target_score": {"type": "number", "default": 0.7},
                "max_iterations": {"type": "integer", "default": 5},
            },
            "required": ["goal"]
        }),
        Tool(name="enhance_horror_scene", description="One-shot horror pipeline: genre rules + thematic props + verify + screenshot.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="get_semantic_env_snapshot", description="Lighting analysis, darkness score, PP state, horror rating 0-100.", inputSchema={"type": "object", "properties": {}}),

        # --- Level Pipeline (v0.4.0) ---
        Tool(name="create_blockout_level", description="Phase I: Parse mission text, generate bubble-diagram layout, place rooms + corridors + PlayerStart + NavMesh.", inputSchema={
            "type": "object",
            "properties": {"mission_brief": {"type": "string"}},
            "required": ["mission_brief"]
        }),
        Tool(name="convert_to_whitebox_modular", description="Phase II: Replace blockout actors with real modular kit meshes.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="apply_set_dressing", description="Phase III: Scatter story-aware props inside each room.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="apply_professional_lighting", description="Phase IV: Key/fill/rim lights, height fog, PP settings, horror score.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="add_living_systems", description="Phase V: Niagara ambient particles + AudioVolume soundscapes.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="generate_full_quality_level", description="Run all 5 phases with quality loop. Iterates Phase IV+V until threshold is met.", inputSchema={
            "type": "object",
            "properties": {
                "mission_brief": {"type": "string"},
                "quality_threshold": {"type": "number", "default": 0.8},
                "max_iterations": {"type": "integer", "default": 3},
            },
            "required": ["mission_brief"]
        }),
        Tool(name="list_presets", description="List all available level preset names.", inputSchema={"type": "object", "properties": {}}),
        Tool(name="load_preset", description="Load a named level preset.", inputSchema={
            "type": "object",
            "properties": {"preset_name": {"type": "string"}},
            "required": ["preset_name"]
        }),

        # --- NEW: LLM Commands (v0.5.0) ---
        Tool(name="llm_chat", description="Send a chat completion to any LLM provider. Supports Anthropic Claude, OpenAI, DeepSeek, and Ollama.", inputSchema={
            "type": "object",
            "properties": {
                "provider": {"type": "string", "enum": ["Anthropic", "OpenAI", "DeepSeek", "OpenAICompatible"]},
                "model": {"type": "string"},
                "messages": {"type": "array", "items": {"type": "object", "properties": {"role": {"type": "string"}, "content": {"type": "string"}}}},
                "system": {"type": "string"},
                "max_tokens": {"type": "integer", "default": 4096},
                "temperature": {"type": "number", "default": 0.7},
            },
            "required": ["provider", "model", "messages"]
        }),
        Tool(name="llm_structured", description="Get structured JSON output from any LLM. Provider enforces the schema.", inputSchema={
            "type": "object",
            "properties": {
                "provider": {"type": "string", "enum": ["Anthropic", "OpenAI", "DeepSeek", "OpenAICompatible"]},
                "model": {"type": "string"},
                "prompt": {"type": "string"},
                "schema": {"type": "object", "description": "JSON Schema the response must conform to"},
                "system": {"type": "string"},
            },
            "required": ["provider", "model", "prompt", "schema"]
        }),
    ]


@app.call_tool()
async def call_tool(name: str, arguments: dict) -> list[TextContent]:
    """Route MCP tool calls to UEAgentForge commands."""
    try:
        result = call_agentforge(name, arguments if arguments else None)
        return [TextContent(type="text", text=json.dumps(result, indent=2))]
    except requests.exceptions.ConnectionError:
        return [TextContent(type="text", text=json.dumps({
            "error": "Cannot connect to Unreal Engine. Ensure the editor is running with Remote Control API enabled on port " + str(UNREAL_PORT)
        }))]
    except Exception as e:
        return [TextContent(type="text", text=json.dumps({"error": str(e)}))]


async def main():
    async with mcp.server.stdio.stdio_server() as (read_stream, write_stream):
        await app.run(read_stream, write_stream, app.create_initialization_options())

if __name__ == "__main__":
    import asyncio
    asyncio.run(main())
```

### 3.2 Create `PythonClient/mcp_server/requirements.txt`

```
mcp>=1.0.0
requests>=2.28.0
```

### 3.3 Create MCP Config Templates

**`PythonClient/mcp_server/claude_desktop_config.example.json`:**
```json
{
  "mcpServers": {
    "agentforge": {
      "command": "python",
      "args": ["<YOUR_PROJECT>/Plugins/UEAgentForge/PythonClient/mcp_server/agentforge_mcp_server.py"],
      "env": {
        "UNREAL_HOST": "127.0.0.1",
        "UNREAL_PORT": "30010"
      }
    }
  }
}
```

**`PythonClient/mcp_server/cursor_mcp.example.json`:** (same format, placed in `.cursor/mcp.json`)

### 3.4 Create Setup Script

**`PythonClient/mcp_server/setup.py`:**
```python
#!/usr/bin/env python3
"""Auto-configure MCP for Claude Desktop / Cursor."""
import json, os, sys, platform

def find_claude_config():
    if platform.system() == "Windows":
        return os.path.join(os.environ["APPDATA"], "Claude", "claude_desktop_config.json")
    elif platform.system() == "Darwin":
        return os.path.expanduser("~/Library/Application Support/Claude/claude_desktop_config.json")
    else:
        return os.path.expanduser("~/.config/Claude/claude_desktop_config.json")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    server_path = os.path.join(script_dir, "agentforge_mcp_server.py")

    config_path = find_claude_config()
    config = {}
    if os.path.exists(config_path):
        with open(config_path) as f:
            config = json.load(f)

    config.setdefault("mcpServers", {})
    config["mcpServers"]["agentforge"] = {
        "command": sys.executable,
        "args": [server_path],
        "env": {
            "UNREAL_HOST": "127.0.0.1",
            "UNREAL_PORT": "30010"
        }
    }

    os.makedirs(os.path.dirname(config_path), exist_ok=True)
    with open(config_path, 'w') as f:
        json.dump(config, f, indent=2)

    print(f"MCP config written to: {config_path}")
    print("Restart Claude Desktop to connect.")

if __name__ == "__main__":
    main()
```

---

## PHASE 4: Vision-in-the-Loop (OAPA Enhancement)

### 4.1 Create `Public/LLM/AgentForgeVisionAnalyzer.h`

```cpp
UCLASS()
class UEAGENTFORGE_API UAgentForgeVisionAnalyzer : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Capture viewport and send to a vision model for analysis
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
    static void AnalyzeViewport(
        const FString& AnalysisPrompt,
        const FOnLLMResponseReceived& OnComplete,
        EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
        const FString& Model = TEXT("claude-sonnet-4-20250514"));

    // Analyze a specific screenshot file
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
    static void AnalyzeScreenshot(
        const FString& ScreenshotPath,
        const FString& AnalysisPrompt,
        const FOnLLMResponseReceived& OnComplete,
        EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
        const FString& Model = TEXT("claude-sonnet-4-20250514"));

    // Multi-angle analysis (uses get_multi_view_capture angles)
    UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
    static void AnalyzeMultiView(
        const FString& AnalysisPrompt,
        const FOnLLMResponseReceived& OnComplete,
        EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic);
};
```

### 4.2 Implementation

- `AnalyzeViewport()`:
  1. Call existing `take_screenshot` command to capture viewport PNG
  2. Read the PNG file, base64-encode it
  3. Create multimodal message with image + analysis prompt
  4. Send through LLM subsystem with the image data in `FAgentForgeChatMessage::ImageData`

- `AnalyzeMultiView()`:
  1. Call `get_multi_view_capture` for each angle (top, front, side, tension)
  2. Base64-encode all four images
  3. Send as a single multimodal message: "Here are 4 views of the level. [analysis prompt]"

### 4.3 Wire Into OAPA Loop

Modify the existing `observe_analyze_plan_act` command handler:

In the **Observe** phase, after collecting the semantic environment snapshot, also run `AnalyzeViewport` with a standardized prompt like:
```
"Analyze this game level screenshot. Describe: 1) Overall composition and visual quality, 2) Lighting effectiveness, 3) Prop placement and set dressing quality, 4) Any visual issues (z-fighting, floating objects, obvious gaps), 5) Rate overall quality 0-100."
```

In the **Analyze** phase, parse the vision model's quality rating and factor it into the quality score alongside the existing heuristic `EvaluateLevelQuality()`. Use weighted average: `final_score = 0.4 * heuristic_score + 0.6 * vision_score`.

Add a new command:

| Command | Args | Description |
|---------|------|-------------|
| `vision_analyze` | `{prompt, provider, model, multi_view}` | Analyze current viewport via vision model |
| `vision_quality_score` | `{provider, model}` | Get a 0-100 quality score from vision analysis |

---

## PHASE 5: Python Client Updates

### 5.1 Update `PythonClient/ueagentforge_client.py`

Add methods to the `AgentForgeClient` class:

```python
# --- LLM Methods ---

def llm_chat(self, provider: str, model: str, messages: list,
             system: str = None, max_tokens: int = 4096,
             temperature: float = 0.7) -> dict:
    """Send a chat completion request through UEAgentForge."""
    args = {
        "provider": provider,
        "model": model,
        "messages": messages,
        "max_tokens": max_tokens,
        "temperature": temperature,
    }
    if system:
        args["system"] = system
    return self._call("llm_chat", args)

def llm_structured(self, provider: str, model: str, prompt: str,
                   schema: dict, system: str = None) -> dict:
    """Get structured JSON output from an LLM."""
    args = {
        "provider": provider,
        "model": model,
        "prompt": prompt,
        "schema": schema,
    }
    if system:
        args["system"] = system
    return self._call("llm_structured", args)

def generate_npc_personality(self, description: str,
                             provider: str = "OpenAI",
                             model: str = "gpt-4o") -> dict:
    """Convenience: Generate an NPC personality using structured output."""
    schema = json.load(open(os.path.join(
        os.path.dirname(__file__), "..", "Content", "AgentForge", "Schemas", "npc_personality.json"
    )))
    return self.llm_structured(provider, model, description, schema)

def generate_quest(self, description: str,
                   provider: str = "OpenAI",
                   model: str = "gpt-4o") -> dict:
    """Convenience: Generate a quest structure using structured output."""
    schema = json.load(open(os.path.join(
        os.path.dirname(__file__), "..", "Content", "AgentForge", "Schemas", "quest_structure.json"
    )))
    return self.llm_structured(provider, model, description, schema)

# --- Vision Methods ---

def vision_analyze(self, prompt: str, provider: str = "Anthropic",
                   model: str = "claude-sonnet-4-20250514",
                   multi_view: bool = False) -> dict:
    """Analyze the current viewport using a vision model."""
    return self._call("vision_analyze", {
        "prompt": prompt,
        "provider": provider,
        "model": model,
        "multi_view": multi_view,
    })

def vision_quality_score(self, provider: str = "Anthropic",
                         model: str = "claude-sonnet-4-20250514") -> dict:
    """Get a 0-100 quality score from vision analysis."""
    return self._call("vision_quality_score", {
        "provider": provider,
        "model": model,
    })
```

### 5.2 New Example Scripts

**`PythonClient/examples/llm_npc_dialogue.py`:**
```python
"""Example: Generate NPC dialogue using the LLM subsystem."""
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()

# Generate a personality
personality = client.generate_npc_personality(
    "A grizzled blacksmith in a medieval fantasy town who secretly works for the thieves guild"
)
print("Generated personality:", json.dumps(personality, indent=2))

# Use it for dialogue
response = client.llm_chat(
    provider="Anthropic",
    model="claude-sonnet-4-20250514",
    system=f"You are {personality['name']}. {personality['backstory']}. "
           f"Speak in a {personality['dialogue_style']} manner.",
    messages=[{"role": "user", "content": "Do you have any special items for sale?"}]
)
print("NPC says:", response.get("content"))
```

**`PythonClient/examples/vision_quality_loop.py`:**
```python
"""Example: Use vision-in-the-loop to iteratively improve a level."""
from ueagentforge_client import AgentForgeClient

client = AgentForgeClient()

# Generate initial level
client.create_blockout_level("An abandoned hospital with a lobby, surgery wing, and rooftop")

# Run vision quality loop
for i in range(3):
    score = client.vision_quality_score()
    print(f"Iteration {i+1}: Quality score = {score.get('score', 0)}")

    if score.get('score', 0) >= 80:
        print("Quality threshold met!")
        break

    # Use OAPA with vision feedback
    client.observe_analyze_plan_act(
        goal="Improve the visual quality based on this feedback: " + score.get('feedback', ''),
        target_score=0.8,
        max_iterations=2
    )

client.save_current_level()
```

---

## PHASE 6: Documentation & README Updates

### 6.1 Update `README.md`

Add a new comparison row to the feature table:

```markdown
| Multi-provider LLM API      | ✗        | Partial  | ✓ Anthropic, OpenAI, DeepSeek, Ollama |
| Structured JSON outputs      | ✗        | Partial  | ✓ Schema-enforced via any provider    |
| Vision-in-the-loop           | ✗        | ✗        | ✓ OAPA + multimodal quality scoring   |
| MCP server (standard)        | Partial  | ✗        | ✓ All 40+ commands as MCP tools       |
| Response streaming           | ✗        | Partial  | ✓ SSE with per-token callbacks        |
| Free & open source           | ✓        | ✗        | ✓ MIT                                 |
```

Add new sections:

```markdown
## LLM API Integration (v0.5.0)

UEAgentForge includes a built-in multi-provider LLM subsystem. No external plugins needed.

### Supported Providers
- **Anthropic Claude** (claude-sonnet-4, claude-opus-4, claude-3.7-sonnet)
- **OpenAI** (gpt-4o, gpt-4.1, o3, o4-mini)
- **DeepSeek** (deepseek-chat V3, deepseek-reasoner R1)
- **OpenAI-Compatible** (Ollama, Groq, OpenRouter, LM Studio — any endpoint)

### Quick Start
```python
client = AgentForgeClient()

# Simple chat
response = client.llm_chat(
    provider="Anthropic",
    model="claude-sonnet-4-20250514",
    messages=[{"role": "user", "content": "Design a horror level layout"}],
    system="You are a game level designer."
)

# Structured output with schema enforcement
npc = client.llm_structured(
    provider="OpenAI",
    model="gpt-4o",
    prompt="Generate a mysterious shopkeeper NPC",
    schema={"type": "object", "properties": {"name": {"type": "string"}, ...}}
)

# Vision analysis
score = client.vision_quality_score()
```

### Setting API Keys

Set environment variables:
```bash
# Windows
setx AGENTFORGE_ANTHROPIC_KEY "sk-ant-..."
setx AGENTFORGE_OPENAI_KEY "sk-..."
setx AGENTFORGE_DEEPSEEK_KEY "sk-..."

# Linux/Mac
export AGENTFORGE_ANTHROPIC_KEY="sk-ant-..."
```

Or set at runtime:
```python
client._call("llm_set_key", {"provider": "Anthropic", "key": "sk-ant-..."})
```

## MCP Server

UEAgentForge ships with a standard MCP server compatible with Claude Desktop, Cursor, and Windsurf.

### Setup
```bash
pip install mcp requests
python PythonClient/mcp_server/setup.py  # Auto-configures Claude Desktop
```

All 40+ UEAgentForge commands are available as MCP tools, including transaction safety,
constitution enforcement, the 5-phase level pipeline, and LLM integration.
```

---

## FILE CREATION CHECKLIST

The agent should create these files in this order:

### C++ Files (Source/UEAgentForge/)
1. `Public/LLM/AgentForgeLLMTypes.h` — All structs, enums, delegates
2. `Public/LLM/IAgentForgeLLMProvider.h` — Provider interface
3. `Public/LLM/AgentForgeLLMSubsystem.h` — Main subsystem header
4. `Private/LLM/AgentForgeLLMSubsystem.cpp` — Subsystem implementation
5. `Private/LLM/Providers/AnthropicProvider.h` — Anthropic adapter header
6. `Private/LLM/Providers/AnthropicProvider.cpp` — Anthropic adapter impl
7. `Private/LLM/Providers/OpenAIProvider.h` — OpenAI adapter header
8. `Private/LLM/Providers/OpenAIProvider.cpp` — OpenAI adapter impl
9. `Private/LLM/Providers/DeepSeekProvider.h` — DeepSeek adapter header
10. `Private/LLM/Providers/DeepSeekProvider.cpp` — DeepSeek adapter impl
11. `Private/LLM/Providers/OpenAICompatibleProvider.h` — Compatible adapter header
12. `Private/LLM/Providers/OpenAICompatibleProvider.cpp` — Compatible adapter impl
13. `Public/LLM/AgentForgeSchemaService.h` — Schema service header
14. `Private/LLM/AgentForgeSchemaService.cpp` — Schema service impl
15. `Public/LLM/AgentForgeVisionAnalyzer.h` — Vision analyzer header
16. `Private/LLM/AgentForgeVisionAnalyzer.cpp` — Vision analyzer impl

### Modify Existing C++ Files
17. `AgentForgeLibrary.h` — Add UFUNCTION declarations for new commands
18. `AgentForgeLibrary.cpp` — Add command dispatch cases in ExecuteCommandJson for: `llm_chat`, `llm_structured`, `llm_stream`, `llm_set_key`, `llm_get_models`, `vision_analyze`, `vision_quality_score`
19. `UEAgentForge.Build.cs` — Add `"HTTP"`, `"Json"`, `"JsonUtilities"` to dependencies
20. `UEAgentForge.uplugin` — Bump version to 0.5.0

### Python Files
21. `PythonClient/mcp_server/agentforge_mcp_server.py` — Full MCP server
22. `PythonClient/mcp_server/requirements.txt` — Dependencies
23. `PythonClient/mcp_server/setup.py` — Auto-config script
24. `PythonClient/mcp_server/claude_desktop_config.example.json` — Example config
25. Update `PythonClient/ueagentforge_client.py` — Add LLM + vision methods

### Content Files
26. `Content/AgentForge/Schemas/npc_personality.json`
27. `Content/AgentForge/Schemas/quest_structure.json`
28. `Content/AgentForge/Schemas/level_layout.json`

### Example Scripts
29. `PythonClient/examples/llm_npc_dialogue.py`
30. `PythonClient/examples/vision_quality_loop.py`
31. `PythonClient/examples/structured_output_demo.py`

### Documentation
32. Update `README.md` with new sections

---

## IMPLEMENTATION ORDER (Critical Path)

```
Step 1:  Types + Interface          (files 1-2)        ~30 min
Step 2:  Subsystem                  (files 3-4)        ~45 min
Step 3:  OpenAI Provider            (files 7-8)        ~45 min  ← start here, simplest
Step 4:  Anthropic Provider         (files 5-6)        ~45 min
Step 5:  DeepSeek Provider          (files 9-10)       ~20 min  (inherits from OpenAI)
Step 6:  Compatible Provider        (files 11-12)      ~20 min  (inherits from OpenAI)
Step 7:  Build.cs + .uplugin        (files 19-20)      ~5 min
Step 8:  Wire into AgentForgeLib    (files 17-18)      ~30 min
Step 9:  Schema Service             (files 13-14)      ~30 min
Step 10: Schema JSON files          (files 26-28)      ~15 min
Step 11: Vision Analyzer            (files 15-16)      ~45 min
Step 12: MCP Server                 (files 21-24)      ~45 min
Step 13: Python Client updates      (file 25)          ~30 min
Step 14: Example scripts            (files 29-31)      ~20 min
Step 15: README                     (file 32)          ~20 min
                                                    Total: ~7 hrs
```

---

## TESTING CHECKLIST

After implementation, verify:

1. **Compilation**: Plugin compiles with no errors in UE 5.5+
2. **Ping**: `{"cmd": "ping"}` returns version 0.5.0
3. **Key Management**: `llm_set_key` stores and retrieves keys
4. **OpenAI Chat**: `llm_chat` with OpenAI returns a response (need a valid key)
5. **Anthropic Chat**: `llm_chat` with Anthropic returns a response
6. **Structured Output**: `llm_structured` returns valid JSON matching schema
7. **MCP Server**: `python agentforge_mcp_server.py` starts, Claude Desktop sees tools
8. **MCP Ping**: Calling `ping` via MCP returns forge status
9. **Existing Commands**: All v0.4.0 commands still work (no regressions)
10. **Python Client**: `AgentForgeClient().llm_chat(...)` returns a response
11. **Schemas Load**: `LoadSchemaFromFile("npc_personality.json")` returns valid JSON

---

## CRITICAL IMPLEMENTATION NOTES

### DO:
- Use `FHttpModule::Get().CreateRequest()` for ALL HTTP calls — this is UE5's standard async HTTP
- Use `AsyncTask(ENamedThreads::GameThread, ...)` to fire delegates on the game thread
- Use `TSharedPtr` for provider instances (no raw pointers)
- Support env var format `AGENTFORGE_<PROVIDER>_KEY` as primary, `PS_<PROVIDER>APIKEY` as fallback
- Set HTTP timeout to 180s minimum (reasoning models are slow)
- Parse JSON with `FJsonSerializer::Deserialize` + `TSharedPtr<FJsonObject>`
- Return proper error messages in FAgentForgeLLMResponse when API calls fail
- Keep the provider interface minimal — don't over-abstract

### DO NOT:
- Do NOT add third-party HTTP libraries (no libcurl wrappers, no cpr)
- Do NOT add any marketplace dependencies
- Do NOT break the existing ExecuteCommandJson dispatch pattern
- Do NOT make the LLM subsystem a Runtime module (keep it Editor-only for now)
- Do NOT store API keys on disk (env vars or runtime-only)
- Do NOT implement TTS/audio/image generation in this phase (scope creep)
- Do NOT use `FPlatformProcess::Sleep` for streaming (use async delegates)
- Do NOT modify any existing command behaviors

### ARCHITECTURE PRINCIPLES:
- Every new feature goes through the existing JSON command interface
- LLM providers are swappable without changing consumer code
- Blueprint and C++ have the same API surface
- Constitution enforcement applies to LLM-generated actions too
- All HTTP calls are non-blocking
```
