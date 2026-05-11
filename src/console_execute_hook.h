#pragma once

#include "OynonToolsApi.h"

bool InstallConsoleExecuteHook(DWORD engineBase);
BOOL ExecuteConsoleCommand(const char* command);
