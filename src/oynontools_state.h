#pragma once

#include "OynonToolsApi.h"

DWORD ResolveAndStoreEngineBase(DWORD engineBase);
DWORD GetStoredEngineBase();

void AddRequestedHookFlags(DWORD hookFlags);
DWORD GetRequestedHookFlags();
