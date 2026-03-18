#pragma once

#include "LLM/IAgentForgeLLMProvider.h"

class FAnthropicProvider : public IAgentForgeLLMProvider
{
public:
	virtual bool PrepareRequest(
		const FAgentForgeLLMSettings& Settings,
		const FString& ApiKey,
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
		FString& OutError) const override;

	virtual FAgentForgeLLMResponse ParseResponse(
		const FHttpResponsePtr& HttpResponse,
		bool bSucceeded) const override;

	virtual FString GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const override;
	virtual TArray<FString> GetAvailableModels() const override;
};
