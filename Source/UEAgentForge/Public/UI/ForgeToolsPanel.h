// Copyright UEAgentForge Project. All Rights Reserved.
// ForgeToolsPanel - editor UI bootstrap scaffolding.

#pragma once

#include "CoreMinimal.h"

class UEAGENTFORGE_API FForgeToolsPanel
{
public:
	static void Initialize();
	static void Shutdown();
	static bool IsInitialized();

private:
	static bool bInitialized;
};

