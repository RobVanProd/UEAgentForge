#pragma once

#include "LLM/Providers/OpenAIProvider.h"

class FOpenAICompatibleProvider : public FOpenAIProvider
{
public:
	virtual FString GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const override;
	virtual TArray<FString> GetAvailableModels() const override;
};
