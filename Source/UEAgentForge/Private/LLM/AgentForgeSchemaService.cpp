#include "LLM/AgentForgeSchemaService.h"
#include "LLM/AgentForgeLLMSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace
{
	static bool ParseJsonObject(const FString& JsonString, TSharedPtr<FJsonObject>& OutObject, FString& OutError)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, OutObject) || !OutObject.IsValid())
		{
			OutError = Reader->GetErrorMessage();
			return false;
		}
		return true;
	}

	static bool ParseJsonValue(const FString& JsonString, TSharedPtr<FJsonValue>& OutValue, FString& OutError)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, OutValue) || !OutValue.IsValid())
		{
			OutError = Reader->GetErrorMessage();
			return false;
		}
		return true;
	}

	static FString NormalizeSchemaFileName(const FString& SchemaFileName)
	{
		FString FileName = SchemaFileName;
		FileName.TrimStartAndEndInline();
		if (!FileName.EndsWith(TEXT(".json"), ESearchCase::IgnoreCase))
		{
			FileName += TEXT(".json");
		}
		return FileName;
	}

	static TArray<FString> GetSchemaSearchPaths(const FString& SchemaFileName)
	{
		const FString FileName = NormalizeSchemaFileName(SchemaFileName);
		TArray<FString> Paths;
		Paths.Add(FPaths::Combine(FPaths::ProjectContentDir(), TEXT("AgentForge"), TEXT("Schemas"), FileName));

		if (TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEAgentForge")))
		{
			Paths.Add(FPaths::Combine(Plugin->GetContentDir(), TEXT("AgentForge"), TEXT("Schemas"), FileName));
		}

		return Paths;
	}

	static bool JsonValueEquals(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid() || Left->Type != Right->Type)
		{
			return false;
		}

		switch (Left->Type)
		{
		case EJson::String:
			return Left->AsString() == Right->AsString();
		case EJson::Number:
			return FMath::IsNearlyEqual(Left->AsNumber(), Right->AsNumber());
		case EJson::Boolean:
			return Left->AsBool() == Right->AsBool();
		case EJson::Null:
			return true;
		default:
			return Left->AsString() == Right->AsString();
		}
	}

	static bool ValidateJsonValueAgainstSchema(
		const TSharedPtr<FJsonValue>& Value,
		const TSharedPtr<FJsonObject>& Schema,
		const FString& Path,
		FString& OutError)
	{
		if (!Schema.IsValid())
		{
			return true;
		}

		const TArray<TSharedPtr<FJsonValue>>* EnumValues = nullptr;
		if (Schema->TryGetArrayField(TEXT("enum"), EnumValues) && EnumValues && EnumValues->Num() > 0)
		{
			bool bEnumMatched = false;
			for (const TSharedPtr<FJsonValue>& AllowedValue : *EnumValues)
			{
				if (JsonValueEquals(Value, AllowedValue))
				{
					bEnumMatched = true;
					break;
				}
			}

			if (!bEnumMatched)
			{
				OutError = FString::Printf(TEXT("%s is not one of the allowed enum values."), *Path);
				return false;
			}
		}

		FString TypeString;
		Schema->TryGetStringField(TEXT("type"), TypeString);
		TypeString.ToLowerInline();
		if (TypeString.IsEmpty())
		{
			if (Schema->HasField(TEXT("properties")))
			{
				TypeString = TEXT("object");
			}
			else if (Schema->HasField(TEXT("items")))
			{
				TypeString = TEXT("array");
			}
		}

		if (TypeString == TEXT("object"))
		{
			const TSharedPtr<FJsonObject>* ObjectValue = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(ObjectValue) || !ObjectValue || !(*ObjectValue).IsValid())
			{
				OutError = FString::Printf(TEXT("%s must be an object."), *Path);
				return false;
			}

			const TArray<TSharedPtr<FJsonValue>>* RequiredFields = nullptr;
			if (Schema->TryGetArrayField(TEXT("required"), RequiredFields) && RequiredFields)
			{
				for (const TSharedPtr<FJsonValue>& RequiredValue : *RequiredFields)
				{
					const FString FieldName = RequiredValue.IsValid() ? RequiredValue->AsString() : FString();
					if (!FieldName.IsEmpty() && !(*ObjectValue)->HasField(FieldName))
					{
						OutError = FString::Printf(TEXT("%s is missing required field '%s'."), *Path, *FieldName);
						return false;
					}
				}
			}

			const TSharedPtr<FJsonObject>* PropertySchemas = nullptr;
			if (Schema->TryGetObjectField(TEXT("properties"), PropertySchemas) && PropertySchemas && (*PropertySchemas).IsValid())
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*PropertySchemas)->Values)
				{
					if (!(*ObjectValue)->HasField(Pair.Key))
					{
						continue;
					}

					const TSharedPtr<FJsonObject>* FieldSchema = nullptr;
					if (Pair.Value.IsValid() && Pair.Value->TryGetObject(FieldSchema) && FieldSchema && (*FieldSchema).IsValid())
					{
						if (!ValidateJsonValueAgainstSchema(
							(*ObjectValue)->TryGetField(Pair.Key),
							*FieldSchema,
							Path + TEXT(".") + Pair.Key,
							OutError))
						{
							return false;
						}
					}
				}

				if (Schema->HasTypedField<EJson::Boolean>(TEXT("additionalProperties")) &&
					!Schema->GetBoolField(TEXT("additionalProperties")))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*ObjectValue)->Values)
					{
						if (!(*PropertySchemas)->HasField(Pair.Key))
						{
							OutError = FString::Printf(TEXT("%s contains unexpected field '%s'."), *Path, *Pair.Key);
							return false;
						}
					}
				}
			}

			return true;
		}

		if (TypeString == TEXT("array"))
		{
			const TArray<TSharedPtr<FJsonValue>>* ArrayValue = nullptr;
			if (!Value.IsValid() || !Value->TryGetArray(ArrayValue) || !ArrayValue)
			{
				OutError = FString::Printf(TEXT("%s must be an array."), *Path);
				return false;
			}

			const TSharedPtr<FJsonObject>* ItemSchema = nullptr;
			if (Schema->TryGetObjectField(TEXT("items"), ItemSchema) && ItemSchema && (*ItemSchema).IsValid())
			{
				for (int32 Index = 0; Index < ArrayValue->Num(); ++Index)
				{
					if (!ValidateJsonValueAgainstSchema(
						(*ArrayValue)[Index],
						*ItemSchema,
						FString::Printf(TEXT("%s[%d]"), *Path, Index),
						OutError))
					{
						return false;
					}
				}
			}

			return true;
		}

		if (TypeString == TEXT("string"))
		{
			if (!Value.IsValid() || Value->Type != EJson::String)
			{
				OutError = FString::Printf(TEXT("%s must be a string."), *Path);
				return false;
			}
			return true;
		}

		if (TypeString == TEXT("number"))
		{
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("%s must be a number."), *Path);
				return false;
			}
			return true;
		}

		if (TypeString == TEXT("integer"))
		{
			if (!Value.IsValid() || Value->Type != EJson::Number)
			{
				OutError = FString::Printf(TEXT("%s must be an integer."), *Path);
				return false;
			}

			const double NumericValue = Value->AsNumber();
			if (!FMath::IsNearlyEqual(NumericValue, FMath::RoundToDouble(NumericValue)))
			{
				OutError = FString::Printf(TEXT("%s must be an integer."), *Path);
				return false;
			}
			return true;
		}

		if (TypeString == TEXT("boolean"))
		{
			if (!Value.IsValid() || Value->Type != EJson::Boolean)
			{
				OutError = FString::Printf(TEXT("%s must be a boolean."), *Path);
				return false;
			}
			return true;
		}

		return true;
	}
}

void UAgentForgeSchemaService::RequestStructuredOutput(
	const FString& Prompt,
	const FString& JsonSchemaString,
	const FOnLLMResponseReceived& OnComplete,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	const FAgentForgeLLMResponse Response = RequestStructuredOutputBlocking(Prompt, JsonSchemaString, Provider, Model);
	OnComplete.ExecuteIfBound(Response);
}

FString UAgentForgeSchemaService::LoadSchemaFromFile(const FString& SchemaFileName)
{
	FString Contents;
	for (const FString& Candidate : GetSchemaSearchPaths(SchemaFileName))
	{
		if (FFileHelper::LoadFileToString(Contents, *Candidate))
		{
			return Contents;
		}
	}

	return FString();
}

bool UAgentForgeSchemaService::ValidateJsonAgainstSchema(const FString& JsonString, const FString& SchemaString)
{
	TSharedPtr<FJsonValue> JsonValue;
	TSharedPtr<FJsonObject> SchemaObject;
	FString ParseError;
	if (!ParseJsonValue(JsonString, JsonValue, ParseError))
	{
		return false;
	}

	if (!ParseJsonObject(SchemaString, SchemaObject, ParseError))
	{
		return false;
	}

	FString ValidationError;
	return ValidateJsonValueAgainstSchema(JsonValue, SchemaObject, TEXT("$"), ValidationError);
}

FAgentForgeLLMResponse UAgentForgeSchemaService::RequestStructuredOutputBlocking(
	const FString& Prompt,
	const FString& JsonSchemaString,
	EAgentForgeLLMProvider Provider,
	const FString& Model,
	const FString& SystemPrompt,
	int32 MaxTokens,
	float Temperature,
	const FString& CustomEndpoint)
{
	FAgentForgeLLMResponse Response;

#if WITH_EDITOR
	if (!GEditor)
	{
		Response.ErrorMessage = TEXT("GEditor null.");
		return Response;
	}

	UAgentForgeLLMSubsystem* LLM = GEditor->GetEditorSubsystem<UAgentForgeLLMSubsystem>();
	if (!LLM)
	{
		Response.ErrorMessage = TEXT("LLM subsystem unavailable.");
		return Response;
	}

	FAgentForgeLLMSettings Settings;
	Settings.Provider = Provider;
	Settings.Model = Model;
	Settings.SystemPrompt = SystemPrompt;
	Settings.MaxTokens = MaxTokens;
	Settings.Temperature = Temperature;
	Settings.CustomEndpoint = CustomEndpoint;

	Response = LLM->SendStructuredRequestBlocking(Prompt, JsonSchemaString, Settings);
	if (Response.bSuccess && !ValidateJsonAgainstSchema(Response.Content, JsonSchemaString))
	{
		Response.bSuccess = false;
		Response.ErrorMessage = TEXT("Structured response did not satisfy the provided schema.");
	}
#else
	Response.ErrorMessage = TEXT("Editor only.");
#endif

	return Response;
}
