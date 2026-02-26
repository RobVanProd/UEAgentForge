#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ConstitutionParser.generated.h"

/**
 * A single parsed rule from the constitution markdown.
 */
USTRUCT(BlueprintType)
struct UEAGENTFORGE_API FConstitutionRule
{
	GENERATED_BODY()

	/** Short identifier, e.g. "NO_SOURCE_EDITS" */
	UPROPERTY(BlueprintReadOnly) FString RuleId;

	/** Human-readable description extracted from the markdown. */
	UPROPERTY(BlueprintReadOnly) FString Description;

	/**
	 * Keywords that trigger this rule when found in an action description.
	 * Example: {"Oceanology", "plugin source", "modify source"}
	 */
	UPROPERTY(BlueprintReadOnly) TArray<FString> TriggerKeywords;

	/** Whether violating this rule hard-blocks the action (true) or just warns (false). */
	UPROPERTY(BlueprintReadOnly) bool bIsBlocking = true;
};

/**
 * UConstitutionParser — loads and enforces project governance rules from a markdown file.
 *
 * At plugin startup, LoadConstitution() is called automatically if a constitution file
 * is found at the following locations (searched in order):
 *   1. ProjectDir/../ue_dev_constitution.md
 *   2. ProjectDir/Constitution/ue_dev_constitution.md
 *   3. PluginDir/Constitution/ue_dev_constitution_template.md  (fallback)
 *
 * The parser extracts rules from bullet-point lists under headings like:
 *   "## Non-negotiable constraints" or "## Rules" or "## Constraints"
 *
 * Format recognized:
 *   - One change per iteration.
 *   - No Oceanology source edits (use wrapper/facade integration).
 *   - No magic numbers for gameplay tuning.
 *
 * Each bullet becomes a FConstitutionRule with auto-generated trigger keywords.
 */
UCLASS(NotBlueprintable)
class UEAGENTFORGE_API UConstitutionParser : public UObject
{
	GENERATED_BODY()

public:
	/** Singleton accessor. */
	static UConstitutionParser* Get();

	/**
	 * Load and parse a constitution markdown file.
	 * Returns the number of rules loaded, or -1 on failure.
	 */
	int32 LoadConstitution(const FString& MarkdownFilePath);

	/**
	 * Auto-discover and load the constitution.
	 * Searches default locations relative to the project and plugin dirs.
	 * Returns the path that was loaded, or empty string on failure.
	 */
	FString AutoLoadConstitution();

	/**
	 * Validate an action description against all loaded rules.
	 * Returns true if the action is allowed.
	 * OutViolations contains descriptions of any violated rules.
	 */
	bool ValidateAction(const FString& ActionDesc, TArray<FString>& OutViolations) const;

	/** All loaded rules. */
	const TArray<FConstitutionRule>& GetRules() const { return Rules; }

	/** Path of the currently loaded constitution file. */
	FString GetConstitutionPath() const { return LoadedFilePath; }

	/** Whether a constitution has been successfully loaded. */
	bool IsLoaded() const { return Rules.Num() > 0; }

private:
	/** Parse a single markdown bullet line into a FConstitutionRule. */
	FConstitutionRule ParseBulletLine(const FString& Line, int32 RuleIndex) const;

	/** Extract keywords from a rule description for trigger matching. */
	TArray<FString> ExtractKeywords(const FString& Description) const;

	TArray<FConstitutionRule> Rules;
	FString                   LoadedFilePath;

	static UConstitutionParser* Singleton;
};
