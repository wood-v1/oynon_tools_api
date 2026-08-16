#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerEffectHook();
BOOL RegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
BOOL SetPlayerBootstrapEffect(const char* effectName);
BOOL RearmPlayerBootstrapEffect();
BOOL ConfirmPlayerBootstrapReady();
void* GetObservedPlayerObject();
void RecoverObservedPlayerFromInventoryManager(void* playerInventory);
BOOL ApplyObservedPlayerEffect(const char* effectName);
void TryApplyPendingPlayerBootstrap();
