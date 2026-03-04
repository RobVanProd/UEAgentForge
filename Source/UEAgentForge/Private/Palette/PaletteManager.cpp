// Copyright UEAgentForge Project. All Rights Reserved.
// PaletteManager.cpp - palette discovery and JSON loading.

#include "Palette/PaletteManager.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	static TArray<FString> GetPaletteDirectoriesInternal()
	{
		TArray<FString> Directories;

		const FString ProjectDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ForgePalettes"));
		Directories.Add(ProjectDir);

		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEAgentForge"));
		if (Plugin.IsValid())
		{
			const FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir() / TEXT("ForgePalettes"));
			Directories.AddUnique(PluginDir);
		}

		return Directories;
	}
}

FString FPaletteManager::GetPaletteDirectory()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / TEXT("ForgePalettes"));
}

TArray<FString> FPaletteManager::ListAvailablePalettes()
{
	TArray<FString> PaletteIds;
	for (const FString& PaletteDir : GetPaletteDirectoriesInternal())
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(PaletteDir / TEXT("*.json")), true, false);
		for (const FString& FileName : Files)
		{
			const FString AbsolutePath = PaletteDir / FileName;
			TSharedPtr<FJsonObject> Palette;
			FString Error;
			if (!LoadPaletteFromFile(AbsolutePath, Palette, Error) || !Palette.IsValid())
			{
				continue;
			}

			FString PaletteId;
			if (Palette->TryGetStringField(TEXT("palette_id"), PaletteId) && !PaletteId.IsEmpty())
			{
				PaletteIds.AddUnique(PaletteId);
			}
		}
	}

	PaletteIds.Sort();
	return PaletteIds;
}

bool FPaletteManager::LoadPaletteById(
	const FString& PaletteId,
	TSharedPtr<FJsonObject>& OutPalette,
	FString& OutError)
{
	OutPalette.Reset();
	OutError.Empty();

	const FString Wanted = PaletteId.TrimStartAndEnd();
	if (Wanted.IsEmpty())
	{
		OutError = TEXT("Palette id is empty.");
		return false;
	}

	for (const FString& PaletteDir : GetPaletteDirectoriesInternal())
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(PaletteDir / TEXT("*.json")), true, false);
		for (const FString& FileName : Files)
		{
			TSharedPtr<FJsonObject> Candidate;
			FString FileError;
			if (!LoadPaletteFromFile(PaletteDir / FileName, Candidate, FileError) || !Candidate.IsValid())
			{
				continue;
			}

			FString CandidateId;
			if (!Candidate->TryGetStringField(TEXT("palette_id"), CandidateId))
			{
				continue;
			}

			if (CandidateId.Equals(Wanted, ESearchCase::IgnoreCase))
			{
				OutPalette = Candidate;
				return true;
			}
		}
	}

	OutError = FString::Printf(TEXT("Palette not found: %s"), *Wanted);
	return false;
}

bool FPaletteManager::LoadPaletteFromFile(
	const FString& AbsoluteFilePath,
	TSharedPtr<FJsonObject>& OutPalette,
	FString& OutError)
{
	OutPalette.Reset();
	OutError.Empty();

	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *AbsoluteFilePath))
	{
		OutError = FString::Printf(TEXT("Unable to read palette file: %s"), *AbsoluteFilePath);
		return false;
	}

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, OutPalette) || !OutPalette.IsValid())
	{
		OutError = FString::Printf(TEXT("Invalid palette JSON: %s"), *AbsoluteFilePath);
		return false;
	}

	return true;
}
