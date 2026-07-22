#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerEffectHook();
BOOL RegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
BOOL SetPlayerBootstrapEffect(const char* effectName);
void* GetObservedPlayerObject();
