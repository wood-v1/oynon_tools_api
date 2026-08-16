#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerEffectHook();
BOOL RegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
BOOL SetPlayerBootstrapEffect(const char* effectName);
BOOL ConfirmPlayerBootstrapReady();
void* GetObservedPlayerObject();
BOOL ApplyObservedPlayerEffect(const char* effectName);
void TryApplyPendingPlayerBootstrap();
