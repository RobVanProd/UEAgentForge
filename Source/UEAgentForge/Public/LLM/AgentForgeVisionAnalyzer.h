#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "LLM/AgentForgeLLMTypes.h"
#include "AgentForgeVisionAnalyzer.generated.h"

UCLASS()
class UEAGENTFORGE_API UAgentForgeVisionAnalyzer : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
	static void AnalyzeViewport(
		const FString& AnalysisPrompt,
		const FOnLLMResponseReceived& OnComplete,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
	static void AnalyzeScreenshot(
		const FString& ScreenshotPath,
		const FString& AnalysisPrompt,
		const FOnLLMResponseReceived& OnComplete,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	UFUNCTION(BlueprintCallable, Category = "AgentForge|Vision")
	static void AnalyzeMultiView(
		const FString& AnalysisPrompt,
		const FOnLLMResponseReceived& OnComplete,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	static FAgentForgeLLMResponse AnalyzeViewportBlocking(
		const FString& AnalysisPrompt,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	static FAgentForgeLLMResponse AnalyzeScreenshotBlocking(
		const FString& ScreenshotPath,
		const FString& AnalysisPrompt,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	static FAgentForgeLLMResponse AnalyzeMultiViewBlocking(
		const FString& AnalysisPrompt,
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"));

	static FAgentForgeLLMResponse RequestQualityScoreBlocking(
		EAgentForgeLLMProvider Provider = EAgentForgeLLMProvider::Anthropic,
		const FString& Model = TEXT("claude-sonnet-4-20250514"),
		bool bMultiView = false);
};
