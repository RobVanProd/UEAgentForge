// UEAgentForge — ConstitutionParser.cpp
// Loads and enforces project governance rules from a markdown file.

#include "ConstitutionParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

UConstitutionParser* UConstitutionParser::Singleton = nullptr;

// ============================================================================
UConstitutionParser* UConstitutionParser::Get()
{
	if (!Singleton || !IsValid(Singleton))
	{
		Singleton = NewObject<UConstitutionParser>(GetTransientPackage(), NAME_None, RF_Standalone);
		Singleton->AddToRoot(); // prevent GC
	}
	return Singleton;
}

// ============================================================================
FString UConstitutionParser::AutoLoadConstitution()
{
	// Search order: project-adjacent → project-local → plugin template
	const FString ProjectDir = FPaths::ProjectDir();

	TArray<FString> CandidatePaths =
	{
		FPaths::Combine(ProjectDir, TEXT("../ue_dev_constitution.md")),
		FPaths::Combine(ProjectDir, TEXT("Constitution/ue_dev_constitution.md")),
		FPaths::Combine(ProjectDir, TEXT("ue_dev_constitution.md")),
	};

	// Also try plugin-relative template as final fallback
	FString PluginDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("UEAgentForge"));
	CandidatePaths.Add(FPaths::Combine(PluginDir, TEXT("Constitution/ue_dev_constitution_template.md")));

	for (const FString& Path : CandidatePaths)
	{
		FString Normalized = FPaths::ConvertRelativePathToFull(Path);
		if (IFileManager::Get().FileExists(*Normalized))
		{
			const int32 Count = LoadConstitution(Normalized);
			if (Count > 0)
			{
				return Normalized;
			}
		}
	}

	return FString();
}

// ============================================================================
int32 UConstitutionParser::LoadConstitution(const FString& MarkdownFilePath)
{
	Rules.Empty();
	LoadedFilePath.Empty();

	TArray<FString> Lines;
	if (!FFileHelper::LoadFileToStringArray(Lines, *MarkdownFilePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[UEAgentForge] Failed to read constitution: %s"), *MarkdownFilePath);
		return -1;
	}

	// State machine: look for headings that signal rule sections, then collect bullets.
	bool bInRuleSection = false;
	int32 RuleIndex = 0;

	const TArray<FString> RuleSectionKeywords =
	{
		TEXT("Non-negotiable"),
		TEXT("Rules"),
		TEXT("Constraints"),
		TEXT("Requirements"),
		TEXT("Enforcement"),
	};

	for (const FString& RawLine : Lines)
	{
		FString Line = RawLine.TrimStartAndEnd();

		// Detect section headings
		if (Line.StartsWith(TEXT("#")))
		{
			bInRuleSection = false;
			for (const FString& Keyword : RuleSectionKeywords)
			{
				if (Line.Contains(Keyword))
				{
					bInRuleSection = true;
					break;
				}
			}
			continue;
		}

		// Parse bullet lines in rule sections
		if (bInRuleSection && (Line.StartsWith(TEXT("- ")) || Line.StartsWith(TEXT("* "))))
		{
			const FString BulletText = Line.Mid(2).TrimStart();
			if (!BulletText.IsEmpty())
			{
				FConstitutionRule Rule = ParseBulletLine(BulletText, RuleIndex++);
				Rules.Add(Rule);
			}
		}
	}

	if (Rules.Num() > 0)
	{
		LoadedFilePath = MarkdownFilePath;
	}

	return Rules.Num();
}

// ============================================================================
FConstitutionRule UConstitutionParser::ParseBulletLine(const FString& Line, int32 RuleIndex) const
{
	FConstitutionRule Rule;
	Rule.RuleId      = FString::Printf(TEXT("RULE_%03d"), RuleIndex);
	Rule.Description = Line;
	Rule.bIsBlocking = true; // All rules are blocking by default
	Rule.TriggerKeywords = ExtractKeywords(Line);
	return Rule;
}

// ============================================================================
TArray<FString> UConstitutionParser::ExtractKeywords(const FString& Description) const
{
	TArray<FString> Keywords;

	// Extract quoted phrases (text in backticks or "quotes")
	{
		int32 Start = INDEX_NONE;
		for (int32 i = 0; i < Description.Len(); ++i)
		{
			const TCHAR C = Description[i];
			if (C == '`' || C == '"')
			{
				if (Start == INDEX_NONE)
				{
					Start = i + 1;
				}
				else
				{
					const FString Phrase = Description.Mid(Start, i - Start).TrimStartAndEnd();
					if (!Phrase.IsEmpty())
					{
						Keywords.AddUnique(Phrase);
					}
					Start = INDEX_NONE;
				}
			}
		}
	}

	// Extract meaningful words (nouns/proper nouns, length > 5, skip common words)
	static const TArray<FString> StopWords =
	{
		TEXT("change"), TEXT("iteration"), TEXT("should"), TEXT("never"), TEXT("always"),
		TEXT("avoid"), TEXT("prefer"), TEXT("keep"), TEXT("make"), TEXT("ensure"),
		TEXT("with"), TEXT("from"), TEXT("that"), TEXT("this"), TEXT("over"), TEXT("for"),
		TEXT("and"), TEXT("not"), TEXT("use"), TEXT("only"),
	};

	TArray<FString> Words;
	Description.ParseIntoArray(Words, TEXT(" "), true);
	for (FString Word : Words)
	{
		// Strip punctuation
		Word = Word.TrimStartAndEnd();
		while (!Word.IsEmpty() && !FChar::IsAlpha(Word[Word.Len() - 1]))
		{
			Word.RemoveAt(Word.Len() - 1);
		}
		Word.ToLowerInline();
		if (Word.Len() > 5 && !StopWords.Contains(Word))
		{
			Keywords.AddUnique(Word);
		}
	}

	return Keywords;
}

// ============================================================================
bool UConstitutionParser::ValidateAction(const FString& ActionDesc, TArray<FString>& OutViolations) const
{
	OutViolations.Empty();
	const FString ActionLower = ActionDesc.ToLower();

	for (const FConstitutionRule& Rule : Rules)
	{
		for (const FString& Keyword : Rule.TriggerKeywords)
		{
			if (ActionLower.Contains(Keyword.ToLower()))
			{
				const FString Violation = FString::Printf(
					TEXT("[%s] %s"), *Rule.RuleId, *Rule.Description);
				OutViolations.AddUnique(Violation);
				break; // One violation report per rule
			}
		}
	}

	// Return true only if no blocking violations
	for (const FString& V : OutViolations)
	{
		// All violations are blocking in this version
		return false;
	}
	return true;
}
