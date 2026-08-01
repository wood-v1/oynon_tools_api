#pragma once

#include "OynonToolsApi.h"

bool TryInstallUIWindowHook();
bool IsUIWindowHookInstalled();
void PollUIWindowHook();
BOOL RegisterUIWindowPrepareListener(OynonUIWindowPrepareCallback callback, void* userData);
BOOL SetUIWindowRedirect(const char* hostXml, const char* replacementXml);
BOOL SetUIOneShotWindowRedirect(const char* hostXml, const char* replacementXml);
BOOL SetUICompanionWindow(const char* hostXml, const char* companionXml);
