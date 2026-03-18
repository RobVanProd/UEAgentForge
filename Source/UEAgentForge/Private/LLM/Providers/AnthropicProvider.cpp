#include "LLM/Providers/AnthropicProvider.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	static FString SerializeJson(const TSharedPtr<FJsonObject>& Object)
	{
		FString Output;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
		FJsonSerializer::Serialize(Object.ToSharedRef(), Writer);
		return Output;
	}

	static FString BuildAnthropicSystemPrompt(const FAgentForgeLLMSettings& Settings)
	{
		if (Settings.ResponseSchema.IsEmpty())
		{
			return Settings.SystemPrompt;
		}

		const FString StructuredSuffix = FString::Printf(
			TEXT(" Return ONLY valid JSON that matches this JSON Schema exactly: %s"),
			*Settings.ResponseSchema);
		return Settings.SystemPrompt.IsEmpty()
			? StructuredSuffix.TrimStartAndEnd()
			: Settings.SystemPrompt + StructuredSuffix;
	}
}

bool FAnthropicProvider::PrepareRequest(
	const FAgentForgeLLMSettings& Settings,
	const FString& ApiKey,
	const TSharedRef<IHttpRequest, ESPMode::ThreadSafe>& Request,
	FString& OutError) const
{
	OutError.Reset();
	if (Settings.Model.IsEmpty())
	{
		OutError = TEXT("llm_chat requires a non-empty model.");
		return false;
	}

	Request->SetURL(GetDefaultEndpoint(Settings));
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("x-api-key"), ApiKey);
	Request->SetHeader(TEXT("anthropic-version"), TEXT("2023-06-01"));

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model);
	Root->SetNumberField(TEXT("max_tokens"), Settings.MaxTokens);
	Root->SetNumberField(TEXT("temperature"), Settings.Temperature);

	const FString SystemPrompt = BuildAnthropicSystemPrompt(Settings);
	if (!SystemPrompt.IsEmpty())
	{
		Root->SetStringField(TEXT("system"), SystemPrompt);
	}

	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const FAgentForgeChatMessage& Message : Settings.Messages)
	{
		TSharedPtr<FJsonObject> MessageObj = MakeShared<FJsonObject>();
		MessageObj->SetStringField(TEXT("role"), Message.Role.IsEmpty() ? TEXT("user") : Message.Role);

		if (Message.ImageData.Num() == 0)
		{
			MessageObj->SetStringField(TEXT("content"), Message.Content);
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> ContentArray;
			for (int32 Index = 0; Index < Message.ImageData.Num(); ++Index)
			{
				const FString MediaType =
					Message.ImageMediaTypes.IsValidIndex(Index) && !Message.ImageMediaTypes[Index].IsEmpty()
						? Message.ImageMediaTypes[Index]
						: TEXT("image/png");

				TSharedPtr<FJsonObject> SourceObj = MakeShared<FJsonObject>();
				SourceObj->SetStringField(TEXT("type"), TEXT("base64"));
				SourceObj->SetStringField(TEXT("media_type"), MediaType);
				SourceObj->SetStringField(TEXT("data"), Message.ImageData[Index]);

				TSharedPtr<FJsonObject> ImageObj = MakeShared<FJsonObject>();
				ImageObj->SetStringField(TEXT("type"), TEXT("image"));
				ImageObj->SetObjectField(TEXT("source"), SourceObj);
				ContentArray.Add(MakeShared<FJsonValueObject>(ImageObj));
			}

			if (!Message.Content.IsEmpty())
			{
				TSharedPtr<FJsonObject> TextObj = MakeShared<FJsonObject>();
				TextObj->SetStringField(TEXT("type"), TEXT("text"));
				TextObj->SetStringField(TEXT("text"), Message.Content);
				ContentArray.Add(MakeShared<FJsonValueObject>(TextObj));
			}

			MessageObj->SetArrayField(TEXT("content"), ContentArray);
		}

		Messages.Add(MakeShared<FJsonValueObject>(MessageObj));
	}
	Root->SetArrayField(TEXT("messages"), Messages);

	Request->SetContentAsString(SerializeJson(Root));
	return true;
}

FAgentForgeLLMResponse FAnthropicProvider::ParseResponse(
	const FHttpResponsePtr& HttpResponse,
	bool bSucceeded) const
{
	FAgentForgeLLMResponse Response;
	Response.bSuccess = false;

	if (!bSucceeded || !HttpResponse.IsValid())
	{
		Response.ErrorMessage = TEXT("Anthropic request failed before receiving a response.");
		return Response;
	}

	Response.RawJSON = HttpResponse->GetContentAsString();
	const int32 ResponseCode = HttpResponse->GetResponseCode();
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		Response.ErrorMessage = FString::Printf(TEXT("Anthropic HTTP %d: %s"), ResponseCode, *Response.RawJSON);
		return Response;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.RawJSON);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Response.ErrorMessage = FString::Printf(TEXT("Anthropic response JSON parse failed: %s"), *Reader->GetErrorMessage());
		return Response;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (Root->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray)
	{
		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& PartValue : *ContentArray)
		{
			const TSharedPtr<FJsonObject>* PartObj = nullptr;
			if (PartValue.IsValid() && PartValue->TryGetObject(PartObj) && PartObj && (*PartObj).IsValid())
			{
				FString Type;
				(*PartObj)->TryGetStringField(TEXT("type"), Type);
				if (Type == TEXT("text") && (*PartObj)->HasTypedField<EJson::String>(TEXT("text")))
				{
					Parts.Add((*PartObj)->GetStringField(TEXT("text")));
				}
			}
		}
		Response.Content = FString::Join(Parts, TEXT("\n"));
	}

	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj && (*UsageObj).IsValid())
	{
		if ((*UsageObj)->HasTypedField<EJson::Number>(TEXT("input_tokens")))
		{
			Response.PromptTokens = (int32)(*UsageObj)->GetNumberField(TEXT("input_tokens"));
		}
		if ((*UsageObj)->HasTypedField<EJson::Number>(TEXT("output_tokens")))
		{
			Response.CompletionTokens = (int32)(*UsageObj)->GetNumberField(TEXT("output_tokens"));
		}
	}

	Response.bSuccess = !Response.Content.IsEmpty();
	if (!Response.bSuccess)
	{
		Response.ErrorMessage = TEXT("Anthropic response did not contain text content.");
	}
	return Response;
}

FString FAnthropicProvider::GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const
{
	if (!Settings.CustomEndpoint.IsEmpty())
	{
		return Settings.CustomEndpoint;
	}
	return TEXT("https://api.anthropic.com/v1/messages");
}

TArray<FString> FAnthropicProvider::GetAvailableModels() const
{
	return {
		TEXT("claude-sonnet-4-20250514"),
		TEXT("claude-opus-4-20250514"),
		TEXT("claude-3-7-sonnet-latest"),
		TEXT("claude-3-5-haiku-latest")
	};
}
