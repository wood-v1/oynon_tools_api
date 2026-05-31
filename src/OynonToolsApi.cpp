#include "OynonToolsApi.h"

#include "console_execute_hook.h"
#include "console_read_hook.h"
#include "debug.h"
#include "movement_hooks.h"
#include "oynontools_state.h"
#include "player_shooting_hook.h"
#include "ui_daychange_hook.h"

BOOL OynonInitializeHooksWhenReady(DWORD hookFlags)
{
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
        WriteDebugLog("PGOG", "Oynon init failed: Engine base resolution returned 0");
        return FALSE;
    }

    AddRequestedHookFlags(hookFlags);

    BOOL ok = TRUE;
    if ((hookFlags & OYNON_HOOK_CONSOLE_READ) && !InstallConsoleReadHooks(resolvedBase)) {
        WriteDebugLog("PGOG", "Oynon init failed: console read hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_CONSOLE_EXECUTE) && !InstallConsoleExecuteHook(resolvedBase)) {
        WriteDebugLog("PGOG", "Oynon init failed: console execute hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & (OYNON_HOOK_MOVEMENT_FRICTION | OYNON_HOOK_MOVEMENT_VERTICAL)) &&
        !InstallMovementHooks(resolvedBase, hookFlags)) {
        WriteDebugLog("PGOG", "Oynon init failed: movement hook install failed");
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_PLAYER_SHOOTING_BLOCK) && !InstallPlayerShootingHook()) {
        WriteDebugLog("PGOG", "Oynon init failed: player shooting hook install failed");
        ok = FALSE;
    }
    if (hookFlags & (OYNON_HOOK_UI_DAYCHANGE_TEXT | OYNON_HOOK_UI_PLAYERSTAT_REDIRECT)) {
        if (::GetModuleHandleA("UI.dll") == nullptr) {
            WriteDebugLog("PGOG", "Oynon UI hook deferred until UI.dll loads");
        }
        else if (!TryInstallUIDaychangeHook()) {
            WriteDebugLog("PGOG", "Oynon init failed: UI hook install failed");
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
    PollUIDaychangeHook();
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
