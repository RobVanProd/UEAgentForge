#include "LLM/Providers/OpenAIProvider.h"
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

	static TArray<FAgentForgeChatMessage> ResolveMessages(const FAgentForgeLLMSettings& Settings)
	{
		TArray<FAgentForgeChatMessage> Resolved = Settings.Messages;
		if (!Settings.SystemPrompt.IsEmpty())
		{
			FAgentForgeChatMessage SystemMessage;
			SystemMessage.Role = TEXT("system");
			SystemMessage.Content = Settings.SystemPrompt;
			Resolved.Insert(SystemMessage, 0);
		}
		return Resolved;
	}

	static TSharedPtr<FJsonValue> BuildOpenAIContentValue(const FAgentForgeChatMessage& Message)
	{
		if (Message.ImageData.Num() == 0)
		{
			return MakeShared<FJsonValueString>(Message.Content);
		}

		TArray<TSharedPtr<FJsonValue>> ContentArray;
		if (!Message.Content.IsEmpty())
		{
			TSharedPtr<FJsonObject> TextPart = MakeShared<FJsonObject>();
			TextPart->SetStringField(TEXT("type"), TEXT("text"));
			TextPart->SetStringField(TEXT("text"), Message.Content);
			ContentArray.Add(MakeShared<FJsonValueObject>(TextPart));
		}

		for (int32 Index = 0; Index < Message.ImageData.Num(); ++Index)
		{
			const FString MediaType =
				Message.ImageMediaTypes.IsValidIndex(Index) && !Message.ImageMediaTypes[Index].IsEmpty()
					? Message.ImageMediaTypes[Index]
					: TEXT("image/png");
			const FString DataUrl = FString::Printf(TEXT("data:%s;base64,%s"), *MediaType, *Message.ImageData[Index]);
			TSharedPtr<FJsonObject> ImageUrl = MakeShared<FJsonObject>();
			ImageUrl->SetStringField(TEXT("url"), DataUrl);

			TSharedPtr<FJsonObject> ImagePart = MakeShared<FJsonObject>();
			ImagePart->SetStringField(TEXT("type"), TEXT("image_url"));
			ImagePart->SetObjectField(TEXT("image_url"), ImageUrl);
			ContentArray.Add(MakeShared<FJsonValueObject>(ImagePart));
		}

		return MakeShared<FJsonValueArray>(ContentArray);
	}

	static FString ExtractOpenAIContent(const TSharedPtr<FJsonObject>& Root)
	{
		const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
		if (!Root->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
		{
			return FString();
		}

		const TSharedPtr<FJsonObject>* ChoiceObj = nullptr;
		if (!(*Choices)[0]->TryGetObject(ChoiceObj) || !ChoiceObj || !(*ChoiceObj).IsValid())
		{
			return FString();
		}

		const TSharedPtr<FJsonObject>* MessageObj = nullptr;
		if (!(*ChoiceObj)->TryGetObjectField(TEXT("message"), MessageObj) || !MessageObj || !(*MessageObj).IsValid())
		{
			return FString();
		}

		if ((*MessageObj)->HasTypedField<EJson::String>(TEXT("content")))
		{
			return (*MessageObj)->GetStringField(TEXT("content"));
		}

		const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
		if ((*MessageObj)->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray)
		{
			TArray<FString> Parts;
			for (const TSharedPtr<FJsonValue>& PartValue : *ContentArray)
			{
				const TSharedPtr<FJsonObject>* PartObj = nullptr;
				if (PartValue.IsValid() && PartValue->TryGetObject(PartObj) && PartObj && (*PartObj).IsValid())
				{
					if ((*PartObj)->HasTypedField<EJson::String>(TEXT("text")))
					{
						Parts.Add((*PartObj)->GetStringField(TEXT("text")));
					}
				}
			}
			return FString::Join(Parts, TEXT("\n"));
		}

		return FString();
	}
}

bool FOpenAIProvider::PrepareRequest(
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
	if (!ApiKey.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("model"), Settings.Model);
	if (Settings.Model.StartsWith(TEXT("o")))
	{
		Root->SetNumberField(TEXT("max_completion_tokens"), Settings.MaxTokens);
	}
	else
	{
		Root->SetNumberField(TEXT("max_tokens"), Settings.MaxTokens);
		Root->SetNumberField(TEXT("temperature"), Settings.Temperature);
	}

	TArray<TSharedPtr<FJsonValue>> MessageArray;
	for (const FAgentForgeChatMessage& Message : ResolveMessages(Settings))
	{
		TSharedPtr<FJsonObject> MessageObj = MakeShared<FJsonObject>();
		MessageObj->SetStringField(TEXT("role"), Message.Role.IsEmpty() ? TEXT("user") : Message.Role);
		MessageObj->SetField(TEXT("content"), BuildOpenAIContentValue(Message));
		MessageArray.Add(MakeShared<FJsonValueObject>(MessageObj));
	}
	Root->SetArrayField(TEXT("messages"), MessageArray);

	if (!Settings.ResponseSchema.IsEmpty())
	{
		TSharedPtr<FJsonObject> SchemaObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Settings.ResponseSchema);
		if (FJsonSerializer::Deserialize(Reader, SchemaObj) && SchemaObj.IsValid())
		{
			TSharedPtr<FJsonObject> JsonSchema = MakeShared<FJsonObject>();
			JsonSchema->SetStringField(TEXT("name"), TEXT("structured_output"));
			JsonSchema->SetBoolField(TEXT("strict"), true);
			JsonSchema->SetObjectField(TEXT("schema"), SchemaObj);

			TSharedPtr<FJsonObject> ResponseFormat = MakeShared<FJsonObject>();
			ResponseFormat->SetStringField(TEXT("type"), TEXT("json_schema"));
			ResponseFormat->SetObjectField(TEXT("json_schema"), JsonSchema);
			Root->SetObjectField(TEXT("response_format"), ResponseFormat);
		}
		else
		{
			OutError = FString::Printf(TEXT("Invalid JSON schema: %s"), *Reader->GetErrorMessage());
			return false;
		}
	}

	Request->SetContentAsString(SerializeJson(Root));
	return true;
}

FAgentForgeLLMResponse FOpenAIProvider::ParseResponse(
	const FHttpResponsePtr& HttpResponse,
	bool bSucceeded) const
{
	FAgentForgeLLMResponse Response;
	Response.bSuccess = false;

	if (!bSucceeded || !HttpResponse.IsValid())
	{
		Response.ErrorMessage = TEXT("OpenAI request failed before receiving a response.");
		return Response;
	}

	Response.RawJSON = HttpResponse->GetContentAsString();
	const int32 ResponseCode = HttpResponse->GetResponseCode();
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		Response.ErrorMessage = FString::Printf(TEXT("OpenAI HTTP %d: %s"), ResponseCode, *Response.RawJSON);
		return Response;
	}

	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response.RawJSON);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		Response.ErrorMessage = FString::Printf(TEXT("OpenAI response JSON parse failed: %s"), *Reader->GetErrorMessage());
		return Response;
	}

	Response.Content = ExtractOpenAIContent(Root);

	const TSharedPtr<FJsonObject>* UsageObj = nullptr;
	if (Root->TryGetObjectField(TEXT("usage"), UsageObj) && UsageObj && (*UsageObj).IsValid())
	{
		if ((*UsageObj)->HasTypedField<EJson::Number>(TEXT("prompt_tokens")))
		{
			Response.PromptTokens = (int32)(*UsageObj)->GetNumberField(TEXT("prompt_tokens"));
		}
		if ((*UsageObj)->HasTypedField<EJson::Number>(TEXT("completion_tokens")))
		{
			Response.CompletionTokens = (int32)(*UsageObj)->GetNumberField(TEXT("completion_tokens"));
		}
	}

	Response.bSuccess = !Response.Content.IsEmpty();
	if (!Response.bSuccess)
	{
		Response.ErrorMessage = TEXT("OpenAI response did not contain message content.");
	}
	return Response;
}

FString FOpenAIProvider::GetDefaultEndpoint(const FAgentForgeLLMSettings& Settings) const
{
	if (!Settings.CustomEndpoint.IsEmpty())
	{
		return Settings.CustomEndpoint;
	}
	return TEXT("https://api.openai.com/v1/chat/completions");
}

TArray<FString> FOpenAIProvider::GetAvailableModels() const
{
	return {
		TEXT("gpt-4.1"),
		TEXT("gpt-4.1-mini"),
		TEXT("gpt-4o"),
		TEXT("gpt-4o-mini"),
		TEXT("o4-mini"),
		TEXT("o3")
	};
}
