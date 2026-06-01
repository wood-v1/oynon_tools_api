#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifdef OYNONTOOLS_EXPORTS
#define OYNONTOOLS_API extern "C" __declspec(dllexport)
#else
#define OYNONTOOLS_API extern "C" __declspec(dllimport)
#endif

enum OynonHookFlags : DWORD
{
    OYNON_HOOK_CONSOLE_READ = 1u << 0,
    OYNON_HOOK_CONSOLE_EXECUTE = 1u << 1,
    OYNON_HOOK_MOVEMENT_FRICTION = 1u << 2,
    OYNON_HOOK_MOVEMENT_VERTICAL = 1u << 3,
    OYNON_HOOK_UI_DAYCHANGE_TEXT = 1u << 4,
    OYNON_HOOK_UI_PLAYERSTAT_REDIRECT = 1u << 5,
    OYNON_HOOK_PLAYER_SHOOTING_BLOCK = 1u << 6,
    OYNON_HOOK_PLAYER_EFFECT_CALLBACK = 1u << 7,
    OYNON_HOOK_UI_INVENTORY_STATE = 1u << 8,
};

using OynonConsoleMessageCallback = void(__stdcall*)(const char* message, void* userData);
using OynonConsoleMessageFilter = BOOL(__stdcall*)(const char* message, void* userData);
using OynonPlayerEffectCallback = void(__stdcall*)(const char* effectName, void* userData);
using OynonInventoryStateCallback = void(__stdcall*)(BOOL opened, void* userData);

OYNONTOOLS_API BOOL OynonInitializeHooksWhenReady(DWORD hookFlags);

OYNONTOOLS_API BOOL OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterConsoleMessageFilter(OynonConsoleMessageFilter filter, void* userData);
OYNONTOOLS_API BOOL OynonRegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData);

OYNONTOOLS_API BOOL OynonExecCommand(const char* command);

OYNONTOOLS_API BOOL OynonSetMovementFrictionMultiplier(float frictionMultiplier);
OYNONTOOLS_API BOOL OynonSetMovementJumpHeightMultiplier(float jumpHeightMultiplier);
OYNONTOOLS_API BOOL OynonSetMovementLandingGravity(int landingGravity);
OYNONTOOLS_API BOOL OynonSetPlayerShootingBlocked(BOOL blocked);

OYNONTOOLS_API void OynonUIDaychangePoll();
OYNONTOOLS_API void OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs);
OYNONTOOLS_API BOOL OynonUIDaychangeIsVanillaActive(DWORD now);
OYNONTOOLS_API void OynonUIPlayerstatSetRedirect(const char* xml);
OYNONTOOLS_API void OynonUIInventoryPoll();
OYNONTOOLS_API void OynonUIPoll();

OYNONTOOLS_API BOOL OynonDebugConfigureChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath);
OYNONTOOLS_API BOOL OynonDebugConfigureLauncherChannel(const char* channelId, BOOL captureConsole);
OYNONTOOLS_API BOOL OynonDebugClearConsoleCapture(const char* channelId);
OYNONTOOLS_API void OynonDebugOpenConsole();
OYNONTOOLS_API void OynonDebugLog(const char* channelId, const char* line);
OYNONTOOLS_API void OynonDebugAppendConsoleCaptureLine(const char* channelId, const char* line);
