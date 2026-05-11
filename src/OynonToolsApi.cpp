#include "OynonToolsApi.h"

#include "console_execute_hook.h"
#include "console_read_hook.h"
#include "debug.h"
#include "movement_hooks.h"
#include "oynontools_state.h"
#include "ui_daychange_hook.h"

BOOL OynonInitializeHooksWhenReady(DWORD hookFlags)
{
    const DWORD needsEngineBase = hookFlags & (
        OYNON_HOOK_CONSOLE_READ | 
        OYNON_HOOK_CONSOLE_EXECUTE | 
        OYNON_HOOK_MOVEMENT_FRICTION | 
        OYNON_HOOK_MOVEMENT_VERTICAL |
        OYNON_HOOK_UI_DAYCHANGE_TEXT
    );

    if (needsEngineBase != 0) {
        while (::GetModuleHandleA("Engine.dll") == nullptr) {
            ::Sleep(100);
        }

        ::Sleep(1000);
    }

    const DWORD resolvedBase = ResolveAndStoreEngineBase(0);
    if (needsEngineBase && resolvedBase == 0) {
        return FALSE;
    }

    AddRequestedHookFlags(hookFlags);

    BOOL ok = TRUE;
    if ((hookFlags & OYNON_HOOK_CONSOLE_READ) && !InstallConsoleReadHooks(resolvedBase)) {
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_CONSOLE_EXECUTE) && !InstallConsoleExecuteHook(resolvedBase)) {
        ok = FALSE;
    }
    if ((hookFlags & (OYNON_HOOK_MOVEMENT_FRICTION | OYNON_HOOK_MOVEMENT_VERTICAL)) &&
        !InstallMovementHooks(resolvedBase, hookFlags)) {
        ok = FALSE;
    }
    if ((hookFlags & OYNON_HOOK_UI_DAYCHANGE_TEXT) && !TryInstallUIDaychangeHook()) {
        ok = FALSE;
    }

    return ok;
}

BOOL OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData)
{
    return RegisterConsoleMessageCallback(callback, userData);
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

BOOL OynonDebugConfigureChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath)
{
    return ConfigureDebugChannel(channelId, enabled, logPath, consoleCapturePath);
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
