#pragma once

#include "LLM/Providers/OpenAIProvider.h"

class FDeepSeekProvider : public FOpenAIProvider
{
public:
	virtual FString GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const override;
	virtual TArray<FString> GetAvailableModels() const override;
};
