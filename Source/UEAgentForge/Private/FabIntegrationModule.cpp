// UEAgentForge — FabIntegrationModule.cpp
// Fab.com marketplace search + local asset import pipeline.

#include "FabIntegrationModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#if WITH_EDITOR
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "Factories/FbxImportUI.h"
#include "Factories/FbxFactory.h"
#include "Factories/TextureFactory.h"
#include "Factories/SoundFactory.h"
#include "EditorFramework/AssetImportData.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
// HTTP for Fab search
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#endif

// ============================================================================
//  FILE-SCOPE HELPERS
// ============================================================================
static FString FabError(const FString& Msg)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetStringField(TEXT("error"), Msg);
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return Out;
}

static FString FabJson(const TSharedPtr<FJsonObject>& O)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(O.ToSharedRef(), W);
	return Out;
}

// ============================================================================
//  SEARCH_FAB_ASSETS
// ============================================================================
FString FFabIntegrationModule::SearchFabAssets(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return FabError(TEXT("search_fab_assets: invalid args."));

	FString Query;
	Args->TryGetStringField(TEXT("query"), Query);
	if (Query.IsEmpty())
		return FabError(TEXT("search_fab_assets requires 'query' arg."));

	const int32 MaxResults = Args->HasField(TEXT("max_results"))
		? (int32)Args->GetNumberField(TEXT("max_results")) : 20;
	const bool bFreeOnly = Args->HasField(TEXT("free_only"))
		? Args->GetBoolField(TEXT("free_only")) : true;

	// ── Synchronous HTTP request to Fab.com listing search ───────────────────
	// NOTE: Fab.com does not have a public documented API. This uses their
	// internal JSON API endpoint (observed from web traffic). The endpoint or
	// response format may change without notice.
	//
	// URL pattern: https://www.fab.com/i/listings?q=<query>&category=3d-assets&sort=-published_at
	// Free filter: &attributes[price_type][0]=free
	//
	// We make a synchronous blocking HTTP request (acceptable in editor-only context).

	FString EncodedQuery = FGenericPlatformHttp::UrlEncode(Query);
	FString URL = FString::Printf(
		TEXT("https://www.fab.com/i/listings?q=%s&sort_by=-published_at&per_page=%d"),
		*EncodedQuery, FMath::Min(MaxResults, 50));

	if (bFreeOnly)
	{
		URL += TEXT("&price_max=0");
	}

	// Synchronous HTTP via blocking event
	FHttpModule& HttpModule = FHttpModule::Get();
	TSharedRef<IHttpRequest> Request = HttpModule.CreateRequest();
	Request->SetURL(URL);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"),     TEXT("application/json"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("UEAgentForge/0.2.0 (Unreal Engine Editor)"));

	// Process synchronously (blocking) — only valid in editor context
	bool bCompleted = false;
	int32  ResponseCode = 0;
	FString ResponseBody;

	Request->OnProcessRequestComplete().BindLambda(
		[&](FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bSuccess)
		{
			if (bSuccess && Resp.IsValid())
			{
				ResponseCode = Resp->GetResponseCode();
				ResponseBody = Resp->GetContentAsString();
			}
			bCompleted = true;
		});

	Request->ProcessRequest();

	// Pump the HTTP module until done (editor game thread — safe)
	const double TimeoutSec = 10.0;
	const double StartTime  = FPlatformTime::Seconds();
	while (!bCompleted && (FPlatformTime::Seconds() - StartTime) < TimeoutSec)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.1f);
		FPlatformProcess::Sleep(0.1f);
	}

	if (!bCompleted)
		return FabError(TEXT("Fab search request timed out (10s). Check internet connectivity."));

	if (ResponseCode == 0)
		return FabError(TEXT("Fab search request failed (no response). Check internet connectivity."));

	// ── Parse JSON response ───────────────────────────────────────────────────
	TSharedPtr<FJsonObject> RespObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);

	TArray<TSharedPtr<FJsonValue>> ResultsArr;

	if (FJsonSerializer::Deserialize(Reader, RespObj) && RespObj.IsValid())
	{
		// Try top-level "results" array (common API pattern)
		const TArray<TSharedPtr<FJsonValue>>* Listings = nullptr;
		if (!RespObj->TryGetArrayField(TEXT("results"), Listings))
		{
			RespObj->TryGetArrayField(TEXT("listings"), Listings);
		}

		if (Listings)
		{
			for (const TSharedPtr<FJsonValue>& Item : *Listings)
			{
				const TSharedPtr<FJsonObject>* ItemObj;
				if (!Item->TryGetObject(ItemObj)) { continue; }

				TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();

				// Extract common fields — field names vary by API version
				FString Title, Id, Type, Url, Thumbnail;
				double  Price = 0.0;

				(*ItemObj)->TryGetStringField(TEXT("title"),       Title);
				(*ItemObj)->TryGetStringField(TEXT("uid"),         Id);
				if (Id.IsEmpty()) { (*ItemObj)->TryGetStringField(TEXT("id"), Id); }
				(*ItemObj)->TryGetStringField(TEXT("type"),        Type);
				(*ItemObj)->TryGetStringField(TEXT("slug"),        Url);
				(*ItemObj)->TryGetStringField(TEXT("thumbnail"),   Thumbnail);
				(*ItemObj)->TryGetNumberField(TEXT("price"),       Price);

				Entry->SetStringField(TEXT("title"),     Title);
				Entry->SetStringField(TEXT("id"),        Id);
				Entry->SetNumberField(TEXT("price"),     Price);
				Entry->SetStringField(TEXT("type"),      Type);
				Entry->SetStringField(TEXT("url"),       TEXT("https://www.fab.com/listings/") + Url);
				Entry->SetStringField(TEXT("thumbnail"), Thumbnail);

				ResultsArr.Add(MakeShared<FJsonValueObject>(Entry));

				if (ResultsArr.Num() >= MaxResults) { break; }
			}
		}
	}

	// ── Build response ────────────────────────────────────────────────────────
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField  (TEXT("ok"),         true);
	Out->SetStringField(TEXT("query"),      Query);
	Out->SetNumberField(TEXT("count"),      ResultsArr.Num());
	Out->SetBoolField  (TEXT("free_only"),  bFreeOnly);
	Out->SetArrayField (TEXT("results"),    ResultsArr);

	if (ResultsArr.IsEmpty())
	{
		Out->SetStringField(TEXT("note"),
			TEXT("No results returned. The Fab.com internal API endpoint is undocumented and may "
			     "have changed. Try searching at https://www.fab.com directly."));
	}

	return FabJson(Out);
#else
	return FabError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  DOWNLOAD_FAB_ASSET  (stub — no public API)
// ============================================================================
FString FFabIntegrationModule::DownloadFabAsset(const TSharedPtr<FJsonObject>& Args)
{
	TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
	O->SetBoolField  (TEXT("ok"),           false);
	O->SetStringField(TEXT("error"),
		TEXT("Fab.com does not provide a public download API. "
		     "Asset downloads require the Epic Games Launcher or "
		     "the Fab plugin inside the Unreal Editor."));
	O->SetStringField(TEXT("workaround"),
		TEXT("1) Browse https://www.fab.com and add the asset to your library. "
		     "2) In the Unreal Editor: top menu → Browse > Fab (or visit the Fab tab). "
		     "3) Find the asset and click 'Download'. "
		     "4) Once downloaded, use import_local_asset to import it into your project."));
	O->SetStringField(TEXT("import_folder"),
		TEXT("After downloading via the EGL, assets are in: "
		     "%LOCALAPPDATA%/UnrealEngine/Common/UEFab or your Vault Cache folder."));
	return FabJson(O);
}

// ============================================================================
//  IMPORT_LOCAL_ASSET
// ============================================================================
FString FFabIntegrationModule::ImportLocalAsset(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	if (!Args.IsValid())
		return FabError(TEXT("import_local_asset: invalid args."));

	FString FilePath, DestPath;
	Args->TryGetStringField(TEXT("file_path"),        FilePath);
	Args->TryGetStringField(TEXT("destination_path"), DestPath);

	if (FilePath.IsEmpty())
		return FabError(TEXT("import_local_asset requires 'file_path' arg."));
	if (DestPath.IsEmpty())
		DestPath = TEXT("/Game/FabImports");

	if (!FPaths::FileExists(FilePath))
		return FabError(FString::Printf(TEXT("File not found: %s"), *FilePath));

	// Determine factory by file extension
	FString Ext = FPaths::GetExtension(FilePath).ToLower();
	UFactory* Factory = nullptr;

	if (Ext == TEXT("fbx") || Ext == TEXT("obj"))
	{
		UFbxFactory* FbxFact = NewObject<UFbxFactory>(GetTransientPackage());
		if (FbxFact)
		{
			UFbxImportUI* ImportUI = NewObject<UFbxImportUI>(FbxFact);
			ImportUI->bImportMesh            = true;
			ImportUI->bImportAnimations      = false;
			ImportUI->bImportMaterials       = true;
			ImportUI->bImportTextures        = true;
			ImportUI->StaticMeshImportData->bCombineMeshes = true;
			FbxFact->ImportUI = ImportUI;
			Factory = FbxFact;
		}
	}
	else if (Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") ||
	         Ext == TEXT("tga") || Ext == TEXT("bmp") || Ext == TEXT("exr"))
	{
		Factory = NewObject<UTextureFactory>(GetTransientPackage());
	}
	else if (Ext == TEXT("wav"))
	{
		Factory = NewObject<USoundFactory>(GetTransientPackage());
	}

	// Use AssetTools to import
	FAssetToolsModule& AssetToolsModule =
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	TArray<UObject*> Imported;
	if (Factory)
	{
		Imported = AssetTools.ImportAssets(TArray<FString>{FilePath}, DestPath, Factory);
	}
	else
	{
		// Generic import — let AssetTools pick the factory
		Imported = AssetTools.ImportAssets(TArray<FString>{FilePath}, DestPath);
	}

	if (Imported.IsEmpty())
		return FabError(FString::Printf(
			TEXT("Import failed for '%s'. Check the file format and destination path."), *FilePath));

	UObject* ImportedAsset = Imported[0];
	FString  AssetPath     = ImportedAsset->GetPathName();

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),         true);
	Resp->SetStringField(TEXT("asset_path"), AssetPath);
	Resp->SetStringField(TEXT("package"),    ImportedAsset->GetOutermost()->GetName());
	Resp->SetStringField(TEXT("type"),       ImportedAsset->GetClass()->GetName());
	Resp->SetNumberField(TEXT("imported_count"), Imported.Num());
	return FabJson(Resp);
#else
	return FabError(TEXT("Editor only."));
#endif
}

// ============================================================================
//  LIST_IMPORTED_ASSETS
// ============================================================================
FString FFabIntegrationModule::ListImportedAssets(const TSharedPtr<FJsonObject>& Args)
{
#if WITH_EDITOR
	FString ContentPath = TEXT("/Game/FabImports");
	if (Args.IsValid()) { Args->TryGetStringField(TEXT("content_path"), ContentPath); }

	FAssetRegistryModule& RegistryModule =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& Registry = RegistryModule.Get();

	TArray<FAssetData> Assets;
	Registry.GetAssetsByPath(FName(*ContentPath), Assets, /*bRecursive=*/true);

	TArray<TSharedPtr<FJsonValue>> ResultArr;
	for (const FAssetData& Asset : Assets)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_name"), Asset.AssetName.ToString());
		Entry->SetStringField(TEXT("asset_path"), Asset.GetSoftObjectPath().ToString());
		Entry->SetStringField(TEXT("type"),       Asset.AssetClassPath.GetAssetName().ToString());
		ResultArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Resp = MakeShared<FJsonObject>();
	Resp->SetBoolField  (TEXT("ok"),           true);
	Resp->SetStringField(TEXT("content_path"), ContentPath);
	Resp->SetNumberField(TEXT("count"),        ResultArr.Num());
	Resp->SetArrayField (TEXT("assets"),       ResultArr);
	return FabJson(Resp);
#else
	return FabError(TEXT("Editor only."));
#endif
}
