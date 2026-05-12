#include "ui_daychange_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"
#include "oynontools_state.h"

#include <cstring>

namespace
{
using CreateWnd_t = void* (__thiscall*)(void* self, void* station, const char* xml, void* eventReceiver);

constexpr DWORD UI_HOOK_RETRY_MS = 1000;
constexpr DWORD UI_CREATE_WND_OFFSET = 0x0001BD60;
constexpr DWORD VANILLA_DAYCHANGE_BUSY_MS = 9000;
constexpr std::size_t MAX_REDIRECT_XML_LEN = 128;
constexpr const char* VANILLA_DAYCHANGE_XML = "daychange.xml";
constexpr const char* PGOG_DEBUG_CHANNEL = "PGOG";

InlineHook g_createWndHook;
CreateWnd_t g_originalCreateWnd = nullptr;
DWORD g_lastCreateWndHookAttempt = 0;
DWORD g_messageRedirectUntil = 0;
DWORD g_vanillaDaychangeBusyUntil = 0;
char g_redirectXml[MAX_REDIRECT_XML_LEN] = {};

void* __fastcall HookCreateWnd(void* self, void*, void* station, const char* xml, void* eventReceiver);

bool IsCreateWndHookPatched()
{
    if (!g_createWndHook.target || !g_createWndHook.installed) {
        return false;
    }

    const BYTE* bytes = reinterpret_cast<const BYTE*>(g_createWndHook.target);
    if (bytes[0] != 0xE9) {
        return false;
    }

    const DWORD relative = *reinterpret_cast<const DWORD*>(bytes + 1);
    const std::uintptr_t patchedTarget =
        g_createWndHook.target + 5 + static_cast<std::uintptr_t>(relative);
    return patchedTarget == reinterpret_cast<std::uintptr_t>(&HookCreateWnd);
}

void* __fastcall HookCreateWnd(void* self, void*, void* station, const char* xml, void* eventReceiver)
{
    const DWORD now = ::GetTickCount();
    const bool redirectActive = IsFutureTick(g_messageRedirectUntil, now) && g_redirectXml[0] != '\0';
    const bool daychangeRequested = xml && std::strcmp(xml, VANILLA_DAYCHANGE_XML) == 0;

    if (redirectActive && daychangeRequested) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirecting daychange window");
        g_messageRedirectUntil = 0;
        g_vanillaDaychangeBusyUntil = 0;
        return g_originalCreateWnd ? g_originalCreateWnd(self, station, g_redirectXml, eventReceiver) : nullptr;
    }

    if (daychangeRequested) {
        if (redirectActive) {
            WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirect was active but fell through");
        }
        g_vanillaDaychangeBusyUntil = now + VANILLA_DAYCHANGE_BUSY_MS;
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI observed vanilla daychange window");
    }

    if (g_messageRedirectUntil != 0 && !redirectActive) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirect expired before daychange window creation");
        g_messageRedirectUntil = 0;
        g_redirectXml[0] = '\0';
    }

    return g_originalCreateWnd ? g_originalCreateWnd(self, station, xml, eventReceiver) : nullptr;
}
}

bool TryInstallUIDaychangeHook()
{
    if (g_createWndHook.installed) {
        return true;
    }

    HMODULE uiModule = ::GetModuleHandleA("UI.dll");
    if (!uiModule) {
        return false;
    }

    g_createWndHook.target = reinterpret_cast<std::uintptr_t>(uiModule) + UI_CREATE_WND_OFFSET;
    g_createWndHook.detour = reinterpret_cast<void*>(&HookCreateWnd);
    g_createWndHook.patchSize = 5;

    if (!InstallInlineHook(g_createWndHook)) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI hook install failed");
        return false;
    }

    g_originalCreateWnd = reinterpret_cast<CreateWnd_t>(g_createWndHook.trampoline);
    WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI hook installed");
    return true;
}

bool IsUIDaychangeHookInstalled()
{
    return g_createWndHook.installed;
}

void PollUIDaychangeHook()
{
    if (!(GetRequestedHookFlags() & OYNON_HOOK_UI_DAYCHANGE_TEXT)) {
        return;
    }

    if (g_createWndHook.installed && IsCreateWndHookPatched()) {
        return;
    }

    if (g_createWndHook.installed && !IsCreateWndHookPatched()) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI hook patch was lost, retrying install");
        g_createWndHook.installed = false;
        g_originalCreateWnd = nullptr;
    }

    const DWORD now = ::GetTickCount();
    if (now - g_lastCreateWndHookAttempt < UI_HOOK_RETRY_MS) {
        return;
    }

    g_lastCreateWndHookAttempt = now;
    TryInstallUIDaychangeHook();
}

void RequestUIDaychangeRedirect(const char* xml, DWORD ttlMs)
{
    if (!xml || xml[0] == '\0') {
        return;
    }

    std::strncpy(g_redirectXml, xml, sizeof(g_redirectXml) - 1);
    g_redirectXml[sizeof(g_redirectXml) - 1] = '\0';
    g_messageRedirectUntil = ::GetTickCount() + ttlMs;
    WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirect armed");
}

BOOL IsVanillaUIDaychangeActive(DWORD now)
{
    return IsFutureTick(g_vanillaDaychangeBusyUntil, now) ? TRUE : FALSE;
}
