#include "Modules/ModuleManager.h"
#include "ConstitutionParser.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

/**
 * UEAgentForge module — Editor-only plugin that surfaces an AI agent control bridge
 * over the Unreal Remote Control API with transaction safety, 4-phase verification,
 * and constitution enforcement.
 */
class FUEAgentForgeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		// Auto-discover and load the project constitution at editor startup.
		// This is a soft operation — the plugin works without a constitution
		// but logs a warning if none is found.
		UConstitutionParser* Parser = UConstitutionParser::Get();
		if (Parser)
		{
			const FString LoadedPath = Parser->AutoLoadConstitution();
			if (!LoadedPath.IsEmpty())
			{
				UE_LOG(LogTemp, Log,
				       TEXT("[UEAgentForge] Constitution loaded: %s (%d rules)"),
				       *LoadedPath, Parser->GetRules().Num());
			}
			else
			{
				UE_LOG(LogTemp, Warning,
				       TEXT("[UEAgentForge] No constitution file found. "
				            "Place ue_dev_constitution.md in your project root or "
				            "use Constitution/ue_dev_constitution_template.md as a starting point."));
			}
		}
#endif
	}

	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FUEAgentForgeModule, UEAgentForge)
