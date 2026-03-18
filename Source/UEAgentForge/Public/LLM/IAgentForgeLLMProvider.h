#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "LLM/AgentForgeLLMTypes.h"

class IAgentForgeLLMProvider
{
public:
	virtual ~IAgentForgeLLMProvider() = default;

	virtual bool PrepareRequest(
		const FAgentForgeLLMSettings& Settings,
		const FString& ApiKey,
		const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
		FString& OutError) const = 0;

	virtual FAgentForgeLLMResponse ParseResponse(
		const FHttpResponsePtr& HttpResponse,
		bool bSucceeded) const = 0;

	virtual FString GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const = 0;
	virtual TArray<FString> GetAvailableModels() const = 0;
};
