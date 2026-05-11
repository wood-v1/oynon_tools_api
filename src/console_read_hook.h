#pragma once

#include "OynonToolsApi.h"

bool InstallConsoleReadHooks(DWORD engineBase);
BOOL RegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData);
