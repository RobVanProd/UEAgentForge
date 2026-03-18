#include "LLM/Providers/DeepSeekProvider.h"

FString FDeepSeekProvider::GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const
{
	if (!Settings.CustomEndpoint.IsEmpty())
	{
		return Settings.CustomEndpoint;
	}
	return TEXT("https://api.deepseek.com/chat/completions");
}

TArray<FString> FDeepSeekProvider::GetAvailableModels() const
{
	return {
		TEXT("deepseek-chat"),
		TEXT("deepseek-reasoner")
	};
}
