#pragma once

#include "OynonToolsApi.h"

bool TryInstallUIWindowHook();
bool IsUIWindowHookInstalled();
void PollUIWindowHook();
BOOL RegisterUIWindowPrepareListener(OynonUIWindowPrepareCallback callback, void* userData);
