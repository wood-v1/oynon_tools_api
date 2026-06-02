#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerUseHook();
BOOL RegisterPlayerUseCallback(OynonPlayerUseCallback callback, void* userData);
