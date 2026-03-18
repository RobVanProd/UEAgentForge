#pragma once

#include "CoreMinimal.h"
#include "AgentForgeLLMTypes.generated.h"

UENUM(BlueprintType)
enum class EAgentForgeLLMProvider : uint8
{
	Anthropic UMETA(DisplayName = "Anthropic"),
	OpenAI UMETA(DisplayName = "OpenAI"),
	DeepSeek UMETA(DisplayName = "DeepSeek"),
	OpenAICompatible UMETA(DisplayName = "OpenAI-Compatible")
};

USTRUCT(BlueprintType)
struct FAgentForgeChatMessage
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString Role = TEXT("user");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString Content;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	TArray<FString> ImageData;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	TArray<FString> ImageMediaTypes;
};

USTRUCT(BlueprintType)
struct FAgentForgeLLMSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString Model;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	int32 MaxTokens = 1024;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	float Temperature = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	TArray<FAgentForgeChatMessage> Messages;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString ResponseSchema;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString SystemPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	FString CustomEndpoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AgentForge|LLM")
	bool bStreamResponse = false;
};

USTRUCT(BlueprintType)
struct FAgentForgeLLMResponse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	FString Content;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	FString RawJSON;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	FString ReasoningContent;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	int32 PromptTokens = 0;

	UPROPERTY(BlueprintReadOnly, Category = "AgentForge|LLM")
	int32 CompletionTokens = 0;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLLMResponseReceived, const FAgentForgeLLMResponse&, Response);
DECLARE_DYNAMIC_DELEGATE_OneParam(FOnLLMStreamChunk, const FString&, Chunk);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLLMResponseMulticast, const FAgentForgeLLMResponse&, Response);
