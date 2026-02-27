#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * UEAgentForge — FabIntegrationModule
 *
 * Asset acquisition: search the Fab.com marketplace, import downloaded assets.
 *
 * ─── IMPORTANT LIMITATION ────────────────────────────────────────────────────
 *
 * Fab.com does NOT provide a public download API. Asset downloads require the
 * Epic Games Launcher or the Fab plugin inside the Unreal Editor (authenticated).
 * The download_fab_asset command returns a clear explanation of this limitation.
 *
 * ─── COMMANDS ───────────────────────────────────────────────────────────────
 *
 *   search_fab_assets     → {ok, query, count, results[{title,id,price,type,url,thumbnail}]}
 *                           args: query, [max_results=20], [free_only=true]
 *                           NOTE: Uses Fab.com's public web API (best-effort; may require
 *                           updates if the endpoint changes). Returns free assets only by default.
 *
 *   download_fab_asset    → {error, message, workaround}
 *                           args: asset_id
 *                           STUB: Always returns an informational error — see workaround.
 *
 *   import_local_asset    → {ok, asset_path, package, type}
 *                           args: file_path, destination_path
 *                           Imports a file already on disk (FBX, OBJ, PNG, WAV, etc.)
 *                           into the UE Content Browser.
 *
 *   list_imported_assets  → [{asset_name, asset_path, type}]
 *                           args: content_path (e.g., "/Game/FabImports/")
 */
class UEAGENTFORGE_API FFabIntegrationModule
{
public:
	/** Search Fab.com marketplace (free assets only by default). */
	static FString SearchFabAssets(const TSharedPtr<FJsonObject>& Args);

	/** Stub: always returns download limitation message. */
	static FString DownloadFabAsset(const TSharedPtr<FJsonObject>& Args);

	/** Import a local file (FBX/OBJ/PNG/WAV) into the UE Content Browser. */
	static FString ImportLocalAsset(const TSharedPtr<FJsonObject>& Args);

	/** List assets in a Content Browser folder. */
	static FString ListImportedAssets(const TSharedPtr<FJsonObject>& Args);
};
