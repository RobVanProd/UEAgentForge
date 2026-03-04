// Copyright UEAgentForge Project. All Rights Reserved.
// PaletteManager - curated asset palette loading utilities.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UEAGENTFORGE_API FPaletteManager
{
public:
	static FString GetPaletteDirectory();
	static TArray<FString> ListAvailablePalettes();

	static bool LoadPaletteById(
		const FString& PaletteId,
		TSharedPtr<FJsonObject>& OutPalette,
		FString& OutError);

	static bool LoadPaletteFromFile(
		const FString& AbsoluteFilePath,
		TSharedPtr<FJsonObject>& OutPalette,
		FString& OutError);
};

