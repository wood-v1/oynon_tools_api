#pragma once

#include "OynonToolsApi.h"

BOOL ConfigureDebugChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath);
void OpenDebugConsole();
void WriteDebugLog(const char* channelId, const char* line);
void AppendDebugConsoleCaptureLine(const char* channelId, const char* line);
