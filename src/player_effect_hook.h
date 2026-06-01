#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerEffectHook();
BOOL RegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
