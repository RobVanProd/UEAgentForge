#include "LLM/Providers/OpenAICompatibleProvider.h"

namespace
{
	static FString NormalizeCompatibleEndpoint(const FString& Endpoint)
	{
		if (Endpoint.IsEmpty())
		{
			return TEXT("http://localhost:11434/v1/chat/completions");
		}
		if (Endpoint.EndsWith(TEXT("/chat/completions")))
		{
			return Endpoint;
		}
		if (Endpoint.EndsWith(TEXT("/v1")))
		{
			return Endpoint + TEXT("/chat/completions");
		}
		return Endpoint + TEXT("/chat/completions");
	}
}

FString FOpenAICompatibleProvider::GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const
{
	return NormalizeCompatibleEndpoint(Settings.CustomEndpoint);
}

TArray<FString> FOpenAICompatibleProvider::GetAvailableModels() const
{
	return {
		TEXT("llama3.1"),
		TEXT("qwen2.5"),
		TEXT("deepseek-r1"),
		TEXT("mistral"),
		TEXT("custom")
	};
}
