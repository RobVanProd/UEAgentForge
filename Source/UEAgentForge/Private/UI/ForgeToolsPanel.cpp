// Copyright UEAgentForge Project. All Rights Reserved.
// ForgeToolsPanel.cpp - UI lifetime scaffold.

#include "UI/ForgeToolsPanel.h"

bool FForgeToolsPanel::bInitialized = false;

void FForgeToolsPanel::Initialize()
{
	bInitialized = true;
}

void FForgeToolsPanel::Shutdown()
{
	bInitialized = false;
}

bool FForgeToolsPanel::IsInitialized()
{
	return bInitialized;
}

