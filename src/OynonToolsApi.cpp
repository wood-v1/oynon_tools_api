#include "OynonToolsApi.h"

#include "console_execute_hook.h"
#include "console_read_hook.h"
#include "debug.h"
#include "keyboard_input.h"
#include "movement_hooks.h"
#include "oynontools_state.h"
#include "player_effect_hook.h"
#include "player_inventory_capacity_hook.h"
#include "player_shooting_hook.h"
#include "player_use_hook.h"
#include "ui_daychange_hook.h"
#include "ui_inventory_redirect.h"
#include "ui_inventory_state.h"
#include "ui_playerstat_redirect.h"
#include "ui_window_hook.h"

BOOL OynonInitializeHooksWhenReady(DWORD hookFlags)
{
    ConfigureLauncherDebugChannel("OynonTools", FALSE);

    const DWORD engineHookFlags = hookFlags & (
        OYNON_HOOK_CONSOLE_READ | 
        OYNON_HOOK_CONSOLE_EXECUTE | 
        OYNON_HOOK_MOVEMENT_FRICTION | 
        OYNON_HOOK_MOVEMENT_VERTICAL
    );

    if (engineHookFlags != 0) {
        while (::GetModuleHandleA("Engine.dll") == nullptr) {
            ::Sleep(100);
        }

        ::Sleep(1000);
    }

    const DWORD resolvedBase = ResolveAndStoreEngineBase(0);
    if (engineHookFlags != 0 && resolvedBase == 0) {
        WriteDebugLog("OynonTools", "Oynon init failed: Engine base resolution returned 0");
        return FALSE;
    }

    AddRequestedHookFlags(hookFlags);

    BOOL ok = TRUE;
    if ((hookFlags & OYNON_HOOK_CONSOLE_READ) && !InstallConsoleReadHooks(resolvedBase)) {
        WriteDebugLog("OynonTools", "Oynon init failed: console read hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_CONSOLE_EXECUTE) && !InstallConsoleExecuteHook(resolvedBase)) {
        WriteDebugLog("OynonTools", "Oynon init failed: console execute hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & (OYNON_HOOK_MOVEMENT_FRICTION | OYNON_HOOK_MOVEMENT_VERTICAL)) &&
        !InstallMovementHooks(resolvedBase, hookFlags)) {
        WriteDebugLog("OynonTools", "Oynon init failed: movement hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_PLAYER_SHOOTING_BLOCK) && !InstallPlayerShootingHook()) {
        WriteDebugLog("OynonTools", "Oynon init failed: player shooting hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_PLAYER_EFFECT_CALLBACK) && !InstallPlayerEffectHook()) {
        WriteDebugLog("OynonTools", "Oynon init failed: player effect hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_PLAYER_INVENTORY_CAPACITY) && !InstallPlayerInventoryCapacityHook()) {
        WriteDebugLog("OynonTools", "Oynon init failed: player inventory capacity hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_PLAYER_USE_CALLBACK) && !InstallPlayerUseHook()) {
        WriteDebugLog("OynonTools", "Oynon init failed: player use hook install failed");
        ok = FALSE;
    }
    if (hookFlags & (OYNON_HOOK_UI_DAYCHANGE_TEXT |
        OYNON_HOOK_UI_PLAYERSTAT_REDIRECT |
        OYNON_HOOK_UI_INVENTORY_STATE |
        OYNON_HOOK_UI_INVENTORY_REDIRECT |
        OYNON_HOOK_UI_WINDOW_PREPARE)) {
        if (::GetModuleHandleA("UI.dll") == nullptr) {
            WriteDebugLog("OynonTools", "Oynon UI hook deferred until UI.dll loads");
        }
        else if (!TryInstallUIWindowHook()) {
            WriteDebugLog("OynonTools", "Oynon init failed: UI hook install failed");
            ok = FALSE;
        }
    }

    return ok;
}

BOOL OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData)
{
    return RegisterConsoleMessageCallback(callback, userData);
}

BOOL OynonRegisterConsoleMessageFilter(OynonConsoleMessageFilter filter, void* userData)
{
    return RegisterConsoleMessageFilter(filter, userData);
}

BOOL OynonRegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData)
{
    return RegisterPlayerEffectCallback(callback, userData);
}

BOOL OynonRegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData)
{
    return RegisterInventoryStateCallback(callback, userData);
}

BOOL OynonRegisterUIWindowPrepareCallback(OynonUIWindowPrepareCallback callback, void* userData)
{
    return RegisterUIWindowPrepareListener(callback, userData);
}

BOOL OynonRegisterPlayerUseCallback(OynonPlayerUseCallback callback, void* userData)
{
    return RegisterPlayerUseCallback(callback, userData);
}

BOOL OynonRegisterPlayerShootingAttemptCallback(OynonPlayerShootingAttemptCallback callback, void* userData)
{
    return RegisterPlayerShootingAttemptCallback(callback, userData);
}

BOOL OynonRegisterKeyboardCallback(OynonKeyboardCallback callback, void* userData)
{
    return RegisterKeyboardCallback(callback, userData);
}

BOOL OynonSetPlayerBootstrapEffect(const char* effectName)
{
    return SetPlayerBootstrapEffect(effectName);
}

BOOL OynonSetPlayerInventoryCategoryCapacity(DWORD capacity)
{
    return ConfigurePlayerInventoryCategoryCapacity(capacity);
}

BOOL OynonSetWorldContainerCapacity(DWORD capacity)
{
    return ConfigureWorldContainerCapacity(capacity);
}

BOOL OynonSetPlayerHandsItem(int itemId)
{
    return SetObservedPlayerHandsItem(itemId);
}

BOOL OynonStablePrioritizePlayerInventory(
    const DWORD* priorityItemIds,
    DWORD priorityItemIdCount,
    DWORD* oldToNewIndices,
    DWORD mappingCapacity,
    DWORD* categoryItemCounts,
    DWORD categoryCount,
    BOOL* changed)
{
    return StablePrioritizePlayerInventory(
        priorityItemIds,
        priorityItemIdCount,
        oldToNewIndices,
        mappingCapacity,
        categoryItemCounts,
        categoryCount,
        changed);
}

BOOL OynonExecCommand(const char* command)
{
    return ExecuteConsoleCommand(command);
}

BOOL OynonSetMovementFrictionMultiplier(float frictionMultiplier)
{
    return SetMovementFrictionMultiplier(frictionMultiplier);
}

BOOL OynonSetMovementJumpHeightMultiplier(float jumpHeightMultiplier)
{
    return SetMovementJumpHeightMultiplier(jumpHeightMultiplier);
}

BOOL OynonSetMovementLandingGravity(int landingGravity)
{
    return SetMovementLandingGravity(landingGravity);
}

BOOL OynonSetPlayerShootingBlocked(BOOL blocked)
{
    return SetPlayerShootingBlocked(blocked);
}

void OynonUIDaychangePoll()
{
    PollUIWindowHook();
}

void OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs)
{
    RequestUIDaychangeRedirect(xml, ttlMs);
}

BOOL OynonUIDaychangeIsVanillaActive(DWORD now)
{
    return IsVanillaUIDaychangeActive(now);
}

void OynonUIPlayerstatSetRedirect(const char* xml)
{
    SetUIPlayerstatRedirect(xml);
}

void OynonUIInventorySetRedirect(const char* xml)
{
    SetUIInventoryRedirect(xml);
}

void OynonUILootSetRedirects(const char* containerXml, const char* corpseXml)
{
    SetUILootRedirects(containerXml, corpseXml);
}

BOOL OynonUISetWindowRedirect(const char* hostXml, const char* replacementXml)
{
    return SetUIWindowRedirect(hostXml, replacementXml);
}

BOOL OynonUISetOneShotWindowRedirect(const char* hostXml, const char* replacementXml)
{
    return SetUIOneShotWindowRedirect(hostXml, replacementXml);
}

BOOL OynonUISetCompanionWindow(const char* hostXml, const char* companionXml)
{
    return SetUICompanionWindow(hostXml, companionXml);
}

void OynonUIInventoryPoll()
{
    PollUIInventoryState();
}

DWORD OynonUIInventoryGetOverlayKind()
{
    return GetUIInventoryOverlayKind();
}

void OynonUIPoll()
{
    PollUIWindowHook();
}

void OynonKeyboardPoll()
{
    PollKeyboardInput();
}

BOOL OynonDebugConfigureChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath)
{
    return ConfigureDebugChannel(channelId, enabled, logPath, consoleCapturePath);
}

BOOL OynonDebugConfigureLauncherChannel(const char* channelId, BOOL captureConsole)
{
    return ConfigureLauncherDebugChannel(channelId, captureConsole);
}

BOOL OynonDebugClearConsoleCapture(const char* channelId)
{
    return ClearDebugConsoleCapture(channelId);
}

void OynonDebugOpenConsole()
{
    OpenDebugConsole();
}

void OynonDebugLog(const char* channelId, const char* line)
{
    WriteDebugLog(channelId, line);
}

void OynonDebugAppendConsoleCaptureLine(const char* channelId, const char* line)
{
    AppendDebugConsoleCaptureLine(channelId, line);
}
