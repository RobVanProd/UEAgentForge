#include "LLM/AgentForgeVisionAnalyzer.h"
#include "LLM/AgentForgeLLMSubsystem.h"
#include "LLM/AgentForgeSchemaService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Base64.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "ImageUtils.h"
#include "UnrealClient.h"
#include "RenderingThread.h"

#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditorViewport.h"
#include "EditorViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"
#endif

namespace
{
	static FString BuildScreenshotPath(const FString& RequestedBaseName)
	{
		FString BaseName = RequestedBaseName;
		BaseName.TrimStartAndEndInline();
		if (BaseName.IsEmpty())
		{
			BaseName = TEXT("AgentForgeVision");
		}
		BaseName = BaseName.Replace(TEXT(" "), TEXT("_"));

		const FString Dir = TEXT("C:/HGShots");
		IFileManager::Get().MakeDirectory(*Dir, true);
		const FString Timestamp = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%s"));
		return FPaths::Combine(Dir, FString::Printf(TEXT("%s_%s.png"), *BaseName, *Timestamp));
	}

#if WITH_EDITOR
	static FLevelEditorViewportClient* GetFirstPerspectiveViewportClient()
	{
		if (!GEditor)
		{
			return nullptr;
		}

		for (FLevelEditorViewportClient* Client : GEditor->GetLevelViewportClients())
		{
			if (Client && Client->IsPerspective())
			{
				return Client;
			}
		}

		return nullptr;
	}

	static FVector ComputeLevelCenter(UWorld* World)
	{
		if (!World)
		{
			return FVector::ZeroVector;
		}

		FBox Bounds(EForceInit::ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor || Actor->IsA<AWorldSettings>())
			{
				continue;
			}

			FVector Origin;
			FVector Extent;
			Actor->GetActorBounds(false, Origin, Extent);
			Bounds += FBox(Origin - Extent, Origin + Extent);
		}

		return Bounds.IsValid ? Bounds.GetCenter() : FVector::ZeroVector;
	}

	static bool CaptureConfiguredViewportScreenshot(const FString& BaseName, FString& OutPath, FString& OutError)
	{
		OutError.Reset();
		OutPath = BuildScreenshotPath(BaseName);
		IFileManager::Get().Delete(*OutPath, false, true, true);

		FLevelEditorViewportClient* ViewportClient = GetFirstPerspectiveViewportClient();
		if (!ViewportClient || !ViewportClient->Viewport)
		{
			OutError = TEXT("No perspective viewport is available for screenshot capture.");
			return false;
		}

		ViewportClient->Invalidate();
		if (GEditor)
		{
			GEditor->RedrawAllViewports(true);
		}
		FlushRenderingCommands();

		FViewport* Viewport = ViewportClient->Viewport;
		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			OutError = TEXT("Viewport size is invalid for screenshot capture.");
			return false;
		}

		TArray<FColor> Bitmap;
		FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
		ReadFlags.SetLinearToGamma(true);
		if (!Viewport->ReadPixels(Bitmap, ReadFlags))
		{
			OutError = TEXT("Viewport pixel readback failed.");
			return false;
		}

		for (FColor& Pixel : Bitmap)
		{
			Pixel.A = 255;
		}

		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(
			Size.X,
			Size.Y,
			TArrayView64<const FColor>(Bitmap.GetData(), Bitmap.Num()),
			CompressedPng);
		if (CompressedPng.Num() == 0 || !FFileHelper::SaveArrayToFile(CompressedPng, *OutPath))
		{
			OutError = FString::Printf(TEXT("Failed to save screenshot: %s"), *OutPath);
			return false;
		}

		return true;
	}

	static bool LoadFileAsBase64(const FString& FilePath, FString& OutBase64, FString& OutError)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to read screenshot: %s"), *FilePath);
			return false;
		}

		OutBase64 = FBase64::Encode(Bytes);
		return true;
	}

	static FAgentForgeLLMResponse ExecuteVisionRequest(
		const TArray<FString>& ImagePaths,
		const FString& Prompt,
		EAgentForgeLLMProvider Provider,
		const FString& Model,
		const FString& ResponseSchema)
	{
		FAgentForgeLLMResponse Response;
		if (!GEditor)
		{
			Response.ErrorMessage = TEXT("GEditor null.");
			return Response;
		}

		if (ImagePaths.Num() == 0)
		{
			Response.ErrorMessage = TEXT("No screenshots were supplied for vision analysis.");
			return Response;
		}

		if (Model.IsEmpty())
		{
			Response.ErrorMessage = TEXT("Vision analysis requires a model.");
			return Response;
		}

		if (Provider != EAgentForgeLLMProvider::OpenAICompatible &&
			UAgentForgeLLMSubsystem::GetApiKey(Provider).IsEmpty())
		{
			Response.ErrorMessage = TEXT("No API key configured for the selected provider.");
			return Response;
		}

		TArray<FString> EncodedImages;
		EncodedImages.Reserve(ImagePaths.Num());
		for (const FString& ImagePath : ImagePaths)
		{
			FString EncodedImage;
			FString ReadError;
			if (!LoadFileAsBase64(ImagePath, EncodedImage, ReadError))
			{
				Response.ErrorMessage = ReadError;
				return Response;
			}
			EncodedImages.Add(MoveTemp(EncodedImage));
		}

		UAgentForgeLLMSubsystem* LLM = GEditor->GetEditorSubsystem<UAgentForgeLLMSubsystem>();
		if (!LLM)
		{
			Response.ErrorMessage = TEXT("LLM subsystem unavailable.");
			return Response;
		}

		FAgentForgeChatMessage Message;
		Message.Role = TEXT("user");
		Message.Content = Prompt;
		Message.ImageData = EncodedImages;
		for (int32 Index = 0; Index < EncodedImages.Num(); ++Index)
		{
			Message.ImageMediaTypes.Add(TEXT("image/png"));
		}

		FAgentForgeLLMSettings Settings;
		Settings.Provider = Provider;
		Settings.Model = Model;
		Settings.MaxTokens = 1024;
		Settings.Temperature = 0.2f;
		Settings.Messages.Add(MoveTemp(Message));
		Settings.ResponseSchema = ResponseSchema;

		Response = LLM->SendChatRequestBlocking(Settings);
		if (Response.bSuccess && !ResponseSchema.IsEmpty() &&
			!UAgentForgeSchemaService::ValidateJsonAgainstSchema(Response.Content, ResponseSchema))
		{
			Response.bSuccess = false;
			Response.ErrorMessage = TEXT("Vision response did not satisfy the expected schema.");
		}

		return Response;
	}

	static FAgentForgeLLMResponse CaptureAndAnalyzeCurrentViewport(
		const FString& Prompt,
		EAgentForgeLLMProvider Provider,
		const FString& Model,
		const FString& ResponseSchema)
	{
		FString ScreenshotPath;
		FString CaptureError;
		if (!CaptureConfiguredViewportScreenshot(TEXT("vision_viewport"), ScreenshotPath, CaptureError))
		{
			FAgentForgeLLMResponse Response;
			Response.ErrorMessage = CaptureError;
			return Response;
		}

		return ExecuteVisionRequest({ ScreenshotPath }, Prompt, Provider, Model, ResponseSchema);
	}

	static FAgentForgeLLMResponse CaptureAndAnalyzeMultiView(
		const FString& Prompt,
		EAgentForgeLLMProvider Provider,
		const FString& Model,
		const FString& ResponseSchema)
	{
		FAgentForgeLLMResponse Response;

		FLevelEditorViewportClient* ViewportClient = GetFirstPerspectiveViewportClient();
		if (!ViewportClient)
		{
			Response.ErrorMessage = TEXT("No perspective viewport is available for multi-view capture.");
			return Response;
		}

		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World)
		{
			Response.ErrorMessage = TEXT("No editor world.");
			return Response;
		}

		const FVector OriginalLocation = ViewportClient->GetViewLocation();
		const FRotator OriginalRotation = ViewportClient->GetViewRotation();
		const FVector Center = ComputeLevelCenter(World);
		const float Radius = 2600.0f;

		struct FViewPreset
		{
			FString Name;
			FVector Offset;
			FRotator Rotation;
		};

		const TArray<FViewPreset> Presets = {
			{ TEXT("top"), FVector(0.0f, 0.0f, Radius), FRotator(-89.0f, 0.0f, 0.0f) },
			{ TEXT("front"), FVector(-Radius, 0.0f, Radius * 0.32f), FRotator(-14.0f, 0.0f, 0.0f) },
			{ TEXT("side"), FVector(0.0f, -Radius, Radius * 0.32f), FRotator(-14.0f, 90.0f, 0.0f) },
			{ TEXT("tension"), FVector(-Radius * 0.55f, Radius * 0.2f, Radius * 0.12f), FRotator(-6.0f, -18.0f, 0.0f) },
		};

		TArray<FString> ImagePaths;
		ImagePaths.Reserve(Presets.Num());

		for (const FViewPreset& Preset : Presets)
		{
			ViewportClient->SetViewLocation(Center + Preset.Offset);
			ViewportClient->SetViewRotation(Preset.Rotation);
			ViewportClient->Invalidate();

			FString ScreenshotPath;
			FString CaptureError;
			if (!CaptureConfiguredViewportScreenshot(TEXT("vision_") + Preset.Name, ScreenshotPath, CaptureError))
			{
				ViewportClient->SetViewLocation(OriginalLocation);
				ViewportClient->SetViewRotation(OriginalRotation);
				ViewportClient->Invalidate();
				Response.ErrorMessage = CaptureError;
				return Response;
			}

			ImagePaths.Add(ScreenshotPath);
		}

		ViewportClient->SetViewLocation(OriginalLocation);
		ViewportClient->SetViewRotation(OriginalRotation);
		ViewportClient->Invalidate();

		return ExecuteVisionRequest(ImagePaths, Prompt, Provider, Model, ResponseSchema);
	}

	static const FString& GetVisionQualitySchema()
	{
		static const FString Schema = TEXT(R"json({
  "type": "object",
  "properties": {
    "score": { "type": "number" },
    "feedback": { "type": "string" },
    "issues": { "type": "array", "items": { "type": "string" } },
    "strengths": { "type": "array", "items": { "type": "string" } },
    "composition_score": { "type": "number" },
    "lighting_score": { "type": "number" },
    "set_dressing_score": { "type": "number" }
  },
  "required": ["score", "feedback", "issues", "strengths"],
  "additionalProperties": false
})json");
		return Schema;
	}

	static const FString& GetVisionQualityPrompt()
	{
		static const FString Prompt =
			TEXT("Analyze the provided Unreal Engine level screenshot or screenshot set. ")
			TEXT("Return only JSON. Score the overall visual quality from 0 to 100. ")
			TEXT("Give concise feedback, list the most important issues, and list strengths. ")
			TEXT("Consider composition, lighting, set dressing, readability, atmosphere, and obvious technical problems.");
		return Prompt;
	}
#endif
}

void UAgentForgeVisionAnalyzer::AnalyzeViewport(
	const FString& AnalysisPrompt,
	const FOnLLMResponseReceived& OnComplete,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	const FAgentForgeLLMResponse Response = AnalyzeViewportBlocking(AnalysisPrompt, Provider, Model);
	OnComplete.ExecuteIfBound(Response);
}

void UAgentForgeVisionAnalyzer::AnalyzeScreenshot(
	const FString& ScreenshotPath,
	const FString& AnalysisPrompt,
	const FOnLLMResponseReceived& OnComplete,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	const FAgentForgeLLMResponse Response = AnalyzeScreenshotBlocking(ScreenshotPath, AnalysisPrompt, Provider, Model);
	OnComplete.ExecuteIfBound(Response);
}

void UAgentForgeVisionAnalyzer::AnalyzeMultiView(
	const FString& AnalysisPrompt,
	const FOnLLMResponseReceived& OnComplete,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	const FAgentForgeLLMResponse Response = AnalyzeMultiViewBlocking(AnalysisPrompt, Provider, Model);
	OnComplete.ExecuteIfBound(Response);
}

FAgentForgeLLMResponse UAgentForgeVisionAnalyzer::AnalyzeViewportBlocking(
	const FString& AnalysisPrompt,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	FAgentForgeLLMResponse Response;

#if WITH_EDITOR
	const FString Prompt = AnalysisPrompt.IsEmpty()
		? TEXT("Analyze this Unreal Engine level screenshot and describe composition, lighting, atmosphere, set dressing quality, and obvious visual issues.")
		: AnalysisPrompt;
	Response = CaptureAndAnalyzeCurrentViewport(Prompt, Provider, Model, FString());
#else
	Response.ErrorMessage = TEXT("Editor only.");
#endif

	return Response;
}

FAgentForgeLLMResponse UAgentForgeVisionAnalyzer::AnalyzeScreenshotBlocking(
	const FString& ScreenshotPath,
	const FString& AnalysisPrompt,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	FAgentForgeLLMResponse Response;

#if WITH_EDITOR
	if (ScreenshotPath.IsEmpty() || !IFileManager::Get().FileExists(*ScreenshotPath))
	{
		Response.ErrorMessage = FString::Printf(TEXT("Screenshot not found: %s"), *ScreenshotPath);
		return Response;
	}

	const FString Prompt = AnalysisPrompt.IsEmpty()
		? TEXT("Analyze this Unreal Engine level screenshot and describe composition, lighting, atmosphere, set dressing quality, and obvious visual issues.")
		: AnalysisPrompt;
	Response = ExecuteVisionRequest({ ScreenshotPath }, Prompt, Provider, Model, FString());
#else
	Response.ErrorMessage = TEXT("Editor only.");
#endif

	return Response;
}

FAgentForgeLLMResponse UAgentForgeVisionAnalyzer::AnalyzeMultiViewBlocking(
	const FString& AnalysisPrompt,
	EAgentForgeLLMProvider Provider,
	const FString& Model)
{
	FAgentForgeLLMResponse Response;

#if WITH_EDITOR
	const FString Prompt = AnalysisPrompt.IsEmpty()
		? TEXT("Analyze these four Unreal Engine level screenshots from different angles and describe composition, lighting, atmosphere, spatial readability, and obvious visual issues.")
		: AnalysisPrompt;
	Response = CaptureAndAnalyzeMultiView(Prompt, Provider, Model, FString());
#else
	Response.ErrorMessage = TEXT("Editor only.");
#endif

	return Response;
}

FAgentForgeLLMResponse UAgentForgeVisionAnalyzer::RequestQualityScoreBlocking(
	EAgentForgeLLMProvider Provider,
	const FString& Model,
	bool bMultiView)
{
	FAgentForgeLLMResponse Response;

#if WITH_EDITOR
	Response = bMultiView
		? CaptureAndAnalyzeMultiView(GetVisionQualityPrompt(), Provider, Model, GetVisionQualitySchema())
		: CaptureAndAnalyzeCurrentViewport(GetVisionQualityPrompt(), Provider, Model, GetVisionQualitySchema());
#else
	Response.ErrorMessage = TEXT("Editor only.");
#endif

	return Response;
}
