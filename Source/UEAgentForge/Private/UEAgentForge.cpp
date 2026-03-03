#include "Modules/ModuleManager.h"
#include "ConstitutionParser.h"
#include "AgentForgeLibrary.h"
#include "Misc/CoreDelegates.h"

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
		// Reset shutdown guard on startup.
		GShutdownRequested = false;

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

		PreExitHandle = FCoreDelegates::OnPreExit.AddRaw(this, &FUEAgentForgeModule::HandleEnginePreExit);
		EnginePreExitHandle = FCoreDelegates::OnEnginePreExit.AddRaw(this, &FUEAgentForgeModule::HandleEnginePreExit);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		HandleEnginePreExit();
		if (PreExitHandle.IsValid())
		{
			FCoreDelegates::OnPreExit.Remove(PreExitHandle);
			PreExitHandle.Reset();
		}
		if (EnginePreExitHandle.IsValid())
		{
			FCoreDelegates::OnEnginePreExit.Remove(EnginePreExitHandle);
			EnginePreExitHandle.Reset();
		}
#endif
	}

private:
#if WITH_EDITOR
	void HandleEnginePreExit()
	{
		if (GShutdownRequested) { return; }
		GShutdownRequested = true;
		UAgentForgeLibrary::MarkEngineShuttingDown();
	}

	FDelegateHandle PreExitHandle;
	FDelegateHandle EnginePreExitHandle;
	bool GShutdownRequested = false;
#endif
};

IMPLEMENT_MODULE(FUEAgentForgeModule, UEAgentForge)
