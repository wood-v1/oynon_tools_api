#pragma once

#include "OynonToolsApi.h"

bool InstallConsoleExecuteHook(DWORD engineBase);
BOOL ExecuteConsoleCommand(const char* command);
BOOL ExecuteConsoleCommandInUIWindowPrepare(const char* command);
BOOL RegisterConsoleCommandFilter(OynonConsoleCommandFilter filter, void* userData);
using GameThreadTask = void(__stdcall*)(void* context);
BOOL DispatchGameThreadTask(GameThreadTask task, void* context);
