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

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	void SendChatRequest(const FAgentForgeLLMSettings& Settings, const FOnLLMResponseReceived& OnComplete);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	void SendStreamingChatRequest(
		const FAgentForgeLLMSettings& Settings,
		const FOnLLMStreamChunk& OnChunk,
		const FOnLLMResponseReceived& OnComplete);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	void SendStructuredRequest(
		const FString& Prompt,
		const FString& JsonSchema,
		const FAgentForgeLLMSettings& Settings,
		const FOnLLMResponseReceived& OnComplete);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	static void SetApiKeyRuntime(EAgentForgeLLMProvider Provider, const FString& Key);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	static FString GetApiKey(EAgentForgeLLMProvider Provider);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|LLM")
	static TArray<FString> GetAvailableModels(EAgentForgeLLMProvider Provider);

	FAgentForgeLLMResponse SendChatRequestBlocking(const FAgentForgeLLMSettings& Settings);
	FAgentForgeLLMResponse SendStructuredRequestBlocking(
		const FString& Prompt,
		const FString& JsonSchema,
		const FAgentForgeLLMSettings& Settings);

private:
	TMap<EAgentForgeLLMProvider, TSharedPtr<IAgentForgeLLMProvider>> Providers;

	TSharedPtr<IAgentForgeLLMProvider> GetOrCreateProvider(EAgentForgeLLMProvider Provider);
	FAgentForgeLLMResponse ExecuteBlockingRequest(const FAgentForgeLLMSettings& Settings);
};
