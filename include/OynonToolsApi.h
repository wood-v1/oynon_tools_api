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
    OYNON_HOOK_PLAYER_USE_CALLBACK = 1u << 9,
    OYNON_HOOK_UI_INVENTORY_REDIRECT = 1u << 10,
    OYNON_HOOK_PLAYER_INVENTORY_CAPACITY = 1u << 11,
    OYNON_HOOK_UI_WINDOW_PREPARE = 1u << 12,
};

enum OynonInventoryOverlayKind : DWORD
{
    OYNON_INVENTORY_OVERLAY_NONE = 0,
    OYNON_INVENTORY_OVERLAY_PLAYER = 1,
    OYNON_INVENTORY_OVERLAY_CONTAINER = 2,
    OYNON_INVENTORY_OVERLAY_CORPSE = 3,
    OYNON_INVENTORY_OVERLAY_OTHER = 4,
};

using OynonConsoleMessageCallback = void(__stdcall*)(const char* message, void* userData);
using OynonConsoleMessageFilter = BOOL(__stdcall*)(const char* message, void* userData);
using OynonConsoleCommandFilter = BOOL(__stdcall*)(const char* command, void* userData);
using OynonPlayerEffectCallback = void(__stdcall*)(const char* effectName, void* userData);
using OynonInventoryStateCallback = void(__stdcall*)(BOOL opened, void* userData);
using OynonUIWindowPrepareCallback = void(__stdcall*)(const char* xml, void* userData);
using OynonUIWindowCreatedCallback = void(__stdcall*)(
    const char* originalXml,
    const char* resolvedXml,
    BOOL succeeded,
    DWORD elapsedMicroseconds,
    void* userData);
using OynonPlayerUseCallback = void(__stdcall*)(const char* scriptName, void* userData);
using OynonPlayerShootingAttemptCallback = void(__stdcall*)(BOOL repeated, void* userData);
using OynonKeyboardCallback = void(__stdcall*)(DWORD virtualKey, BOOL pressed, void* userData);

OYNONTOOLS_API BOOL OynonInitializeHooksWhenReady(DWORD hookFlags);

OYNONTOOLS_API BOOL OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterConsoleMessageFilter(OynonConsoleMessageFilter filter, void* userData);
OYNONTOOLS_API BOOL OynonRegisterConsoleCommandFilter(OynonConsoleCommandFilter filter, void* userData);
OYNONTOOLS_API BOOL OynonRegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterUIWindowPrepareCallback(OynonUIWindowPrepareCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterUIWindowCreatedCallback(OynonUIWindowCreatedCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterPlayerUseCallback(OynonPlayerUseCallback callback, void* userData);
// Returns the script currently executing inside the hooked player-use call.
// This is primarily useful from re-entrant UI creation callbacks: the regular
// player-use callback is intentionally dispatched only after the original use
// handler succeeds, which can be too late to classify a window it created.
OYNONTOOLS_API BOOL OynonGetActivePlayerUseScript(char* buffer, DWORD bufferCapacity);
OYNONTOOLS_API BOOL OynonRegisterPlayerShootingAttemptCallback(OynonPlayerShootingAttemptCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonRegisterKeyboardCallback(OynonKeyboardCallback callback, void* userData);
OYNONTOOLS_API BOOL OynonSetPlayerBootstrapEffect(const char* effectName);
OYNONTOOLS_API BOOL OynonRearmPlayerBootstrapEffect();
OYNONTOOLS_API BOOL OynonConfirmPlayerBootstrapReady();
OYNONTOOLS_API BOOL OynonSetPlayerInventoryCategoryCapacity(DWORD capacity);
OYNONTOOLS_API BOOL OynonSetWorldContainerCapacity(DWORD capacity);
OYNONTOOLS_API BOOL OynonSetPlayerHandsItem(int itemId);
OYNONTOOLS_API BOOL OynonApplyObservedPlayerEffect(const char* effectName);
OYNONTOOLS_API BOOL OynonStablePrioritizePlayerInventory(
    const DWORD* priorityItemIds,
    DWORD priorityItemIdCount,
    DWORD* oldToNewIndices,
    DWORD mappingCapacity,
    DWORD* categoryItemCounts,
    DWORD categoryCount,
    BOOL* changed);

OYNONTOOLS_API BOOL OynonExecCommand(const char* command);
OYNONTOOLS_API BOOL OynonExecCommandInUIWindowPrepare(const char* command);

OYNONTOOLS_API BOOL OynonSetMovementFrictionMultiplier(float frictionMultiplier);
OYNONTOOLS_API BOOL OynonSetMovementJumpHeightMultiplier(float jumpHeightMultiplier);
OYNONTOOLS_API BOOL OynonSetMovementLandingGravity(int landingGravity);
OYNONTOOLS_API BOOL OynonSetPlayerShootingBlocked(BOOL blocked);

OYNONTOOLS_API void OynonUIDaychangePoll();
OYNONTOOLS_API void OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs);
OYNONTOOLS_API BOOL OynonUIDaychangeIsVanillaActive(DWORD now);
OYNONTOOLS_API void OynonUIPlayerstatSetRedirect(const char* xml);
OYNONTOOLS_API void OynonUIInventorySetRedirect(const char* xml);
OYNONTOOLS_API void OynonUILootSetRedirects(const char* containerXml, const char* corpseXml);
OYNONTOOLS_API BOOL OynonUISetWindowRedirect(const char* hostXml, const char* replacementXml);
OYNONTOOLS_API BOOL OynonUISetOneShotWindowRedirect(const char* hostXml, const char* replacementXml);
OYNONTOOLS_API BOOL OynonUISetCompanionWindow(const char* hostXml, const char* companionXml);
OYNONTOOLS_API BOOL OynonUIAddPersistentCompanionWindow(const char* hostXml, const char* companionXml);
OYNONTOOLS_API BOOL OynonUIRemovePersistentCompanionWindow(const char* hostXml, const char* companionXml);
OYNONTOOLS_API void OynonUIInventoryPoll();
OYNONTOOLS_API DWORD OynonUIInventoryGetOverlayKind();
OYNONTOOLS_API void OynonUIPoll();
OYNONTOOLS_API void OynonKeyboardPoll();

OYNONTOOLS_API BOOL OynonDebugConfigureChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath);
OYNONTOOLS_API BOOL OynonDebugConfigureLauncherChannel(const char* channelId, BOOL captureConsole);
OYNONTOOLS_API BOOL OynonDebugClearConsoleCapture(const char* channelId);
OYNONTOOLS_API void OynonDebugOpenConsole();
OYNONTOOLS_API void OynonDebugLog(const char* channelId, const char* line);
OYNONTOOLS_API void OynonDebugAppendConsoleCaptureLine(const char* channelId, const char* line);
