#pragma once

#include "OynonToolsApi.h"

BOOL ConfigureDebugChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath);
BOOL ConfigureLauncherDebugChannel(const char* channelId, BOOL captureConsole);
BOOL ClearDebugConsoleCapture(const char* channelId);
void OpenDebugConsole();
void WriteDebugLog(const char* channelId, const char* line);
void AppendDebugConsoleCaptureLine(const char* channelId, const char* line);
