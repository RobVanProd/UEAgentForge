#include "LLM/AgentForgeLLMSubsystem.h"
#include "LLM/IAgentForgeLLMProvider.h"
#include "LLM/Providers/AnthropicProvider.h"
#include "LLM/Providers/DeepSeekProvider.h"
#include "LLM/Providers/OpenAICompatibleProvider.h"
#include "LLM/Providers/OpenAIProvider.h"
#include "HttpModule.h"
#include "HttpManager.h"
#include "HAL/PlatformMisc.h"

namespace
{
	static TMap<EAgentForgeLLMProvider, FString> GRuntimeApiKeys;

	static FString GetPrimaryEnvVar(EAgentForgeLLMProvider Provider)
	{
		switch (Provider)
		{
		case EAgentForgeLLMProvider::Anthropic: return TEXT("AGENTFORGE_ANTHROPIC_KEY");
		case EAgentForgeLLMProvider::OpenAI: return TEXT("AGENTFORGE_OPENAI_KEY");
		case EAgentForgeLLMProvider::DeepSeek: return TEXT("AGENTFORGE_DEEPSEEK_KEY");
		case EAgentForgeLLMProvider::OpenAICompatible: return TEXT("AGENTFORGE_CUSTOM_KEY");
		default: return FString();
		}
	}

	static FString GetLegacyEnvVar(EAgentForgeLLMProvider Provider)
	{
		switch (Provider)
		{
		case EAgentForgeLLMProvider::Anthropic: return TEXT("PS_ANTHROPICAPIKEY");
		case EAgentForgeLLMProvider::OpenAI: return TEXT("PS_OPENAIAPIKEY");
		case EAgentForgeLLMProvider::DeepSeek: return TEXT("PS_DEEPSEEKAPIKEY");
		case EAgentForgeLLMProvider::OpenAICompatible: return TEXT("PS_OPENAICOMPATIBLEAPIKEY");
		default: return FString();
		}
	}

	static TSharedPtr<IAgentForgeLLMProvider> MakeProvider(EAgentForgeLLMProvider Provider)
	{
		switch (Provider)
		{
		case EAgentForgeLLMProvider::Anthropic:
			return MakeShared<FAnthropicProvider>();
		case EAgentForgeLLMProvider::OpenAI:
			return MakeShared<FOpenAIProvider>();
		case EAgentForgeLLMProvider::DeepSeek:
			return MakeShared<FDeepSeekProvider>();
		case EAgentForgeLLMProvider::OpenAICompatible:
			return MakeShared<FOpenAICompatibleProvider>();
		default:
			return nullptr;
		}
	}
}

void UAgentForgeLLMSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("[UEAgentForge] LLM subsystem initialized."));
}

void UAgentForgeLLMSubsystem::Deinitialize()
{
	Providers.Empty();
	Super::Deinitialize();
	UE_LOG(LogTemp, Log, TEXT("[UEAgentForge] LLM subsystem deinitialized."));
}

void UAgentForgeLLMSubsystem::SendChatRequest(
	const FAgentForgeLLMSettings& Settings,
	const FOnLLMResponseReceived& OnComplete)
{
	const FAgentForgeLLMResponse Response = SendChatRequestBlocking(Settings);
	OnComplete.ExecuteIfBound(Response);
}

void UAgentForgeLLMSubsystem::SendStreamingChatRequest(
	const FAgentForgeLLMSettings& Settings,
	const FOnLLMStreamChunk& OnChunk,
	const FOnLLMResponseReceived& OnComplete)
{
	const FAgentForgeLLMResponse Response = SendChatRequestBlocking(Settings);
	if (Response.bSuccess && !Response.Content.IsEmpty())
	{
		OnChunk.ExecuteIfBound(Response.Content);
	}
	OnComplete.ExecuteIfBound(Response);
}

void UAgentForgeLLMSubsystem::SendStructuredRequest(
	const FString& Prompt,
	const FString& JsonSchema,
	const FAgentForgeLLMSettings& Settings,
	const FOnLLMResponseReceived& OnComplete)
{
	const FAgentForgeLLMResponse Response = SendStructuredRequestBlocking(Prompt, JsonSchema, Settings);
	OnComplete.ExecuteIfBound(Response);
}

void UAgentForgeLLMSubsystem::SetApiKeyRuntime(EAgentForgeLLMProvider Provider, const FString& Key)
{
	if (Key.IsEmpty())
	{
		GRuntimeApiKeys.Remove(Provider);
	}
	else
	{
		GRuntimeApiKeys.FindOrAdd(Provider) = Key;
	}
}

FString UAgentForgeLLMSubsystem::GetApiKey(EAgentForgeLLMProvider Provider)
{
	if (const FString* RuntimeKey = GRuntimeApiKeys.Find(Provider))
	{
		return *RuntimeKey;
	}

	const FString Primary = GetPrimaryEnvVar(Provider);
	if (!Primary.IsEmpty())
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(*Primary);
		if (!Value.IsEmpty())
		{
			return Value;
		}
	}

	const FString Legacy = GetLegacyEnvVar(Provider);
	if (!Legacy.IsEmpty())
	{
		const FString Value = FPlatformMisc::GetEnvironmentVariable(*Legacy);
		if (!Value.IsEmpty())
		{
			return Value;
		}
	}

	if (Provider == EAgentForgeLLMProvider::OpenAICompatible)
	{
		return FPlatformMisc::GetEnvironmentVariable(TEXT("AGENTFORGE_OPENAI_KEY"));
	}

	return FString();
}

TArray<FString> UAgentForgeLLMSubsystem::GetAvailableModels(EAgentForgeLLMProvider Provider)
{
	if (TSharedPtr<IAgentForgeLLMProvider> Handler = MakeProvider(Provider))
	{
		return Handler->GetAvailableModels();
	}
	return {};
}

FAgentForgeLLMResponse UAgentForgeLLMSubsystem::SendChatRequestBlocking(const FAgentForgeLLMSettings& Settings)
{
	return ExecuteBlockingRequest(Settings);
}

FAgentForgeLLMResponse UAgentForgeLLMSubsystem::SendStructuredRequestBlocking(
	const FString& Prompt,
	const FString& JsonSchema,
	const FAgentForgeLLMSettings& Settings)
{
	FAgentForgeLLMSettings StructuredSettings = Settings;
	StructuredSettings.ResponseSchema = JsonSchema;
	StructuredSettings.Messages.Empty();

	FAgentForgeChatMessage PromptMessage;
	PromptMessage.Role = TEXT("user");
	PromptMessage.Content = Prompt;
	StructuredSettings.Messages.Add(PromptMessage);

	return ExecuteBlockingRequest(StructuredSettings);
}

TSharedPtr<IAgentForgeLLMProvider> UAgentForgeLLMSubsystem::GetOrCreateProvider(EAgentForgeLLMProvider Provider)
{
	if (const TSharedPtr<IAgentForgeLLMProvider>* Existing = Providers.Find(Provider))
	{
		return *Existing;
	}

	TSharedPtr<IAgentForgeLLMProvider> Created = MakeProvider(Provider);
	if (Created.IsValid())
	{
		Providers.Add(Provider, Created);
	}
	return Created;
}

FAgentForgeLLMResponse UAgentForgeLLMSubsystem::ExecuteBlockingRequest(const FAgentForgeLLMSettings& Settings)
{
	FAgentForgeLLMResponse Response;

	TSharedPtr<IAgentForgeLLMProvider> Provider = GetOrCreateProvider(Settings.Provider);
	if (!Provider.IsValid())
	{
		Response.ErrorMessage = TEXT("Unsupported LLM provider.");
		return Response;
	}

	const FString ApiKey = GetApiKey(Settings.Provider);
	if (ApiKey.IsEmpty() && Settings.Provider != EAgentForgeLLMProvider::OpenAICompatible)
	{
		Response.ErrorMessage = TEXT("No API key configured for the selected provider.");
		return Response;
	}

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	FString PrepareError;
	if (!Provider->PrepareRequest(Settings, ApiKey, Request, PrepareError))
	{
		Response.ErrorMessage = PrepareError;
		return Response;
	}

	Request->OnProcessRequestComplete().BindLambda(
		[&Response, Provider](FHttpRequestPtr /*Unused*/, FHttpResponsePtr HttpResponse, bool bSucceeded)
		{
			Response = Provider->ParseResponse(HttpResponse, bSucceeded);
		});

	if (!Request->ProcessRequest())
	{
		Response.ErrorMessage = TEXT("Failed to dispatch HTTP request.");
		return Response;
	}

	FHttpModule::Get().GetHttpManager().Flush(EHttpFlushReason::FullFlush);
	if (!Response.bSuccess && Response.ErrorMessage.IsEmpty() && Response.RawJSON.IsEmpty())
	{
		Response.ErrorMessage = TEXT("HTTP request completed without a parsed response.");
	}

	return Response;
}
