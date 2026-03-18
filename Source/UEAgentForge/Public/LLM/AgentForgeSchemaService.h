#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LLM/AgentForgeLLMTypes.h"
#include "AgentForgeSchemaService.generated.h"

UCLASS()
class UEAGENTFORGE_API UAgentForgeSchemaService : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
	static void RequestStructuredOutput(
		const FString& Prompt,
		const FString& JsonSchemaString,
		const FOnLLMResponseReceived& OnComplete,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::OpenAI,
		const FString& Model = TEXT("gpt-4o"));

	UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
	static FString LoadSchemaFromFile(const FString& SchemaFileName);

	UFUNCTION(BlueprintCallable, Category = "AgentForge|Schema")
	static bool ValidateJsonAgainstSchema(const FString& JsonString, const FString& SchemaString);

	static FAgentForgeLLMResponse RequestStructuredOutputBlocking(
		const FString& Prompt,
		const FString& JsonSchemaString,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::OpenAI,
		const FString& Model = TEXT("gpt-4o"),
		const FString& SystemPrompt = FString(),
		int32 MaxTokens = 1024,
		float Temperature = 0.2f,
		const FString& CustomEndpoint = FString());
};
