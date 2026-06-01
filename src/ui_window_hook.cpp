#include "ui_window_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"
#include "oynontools_state.h"
#include "ui_daychange_hook.h"
#include "ui_inventory_state.h"
#include "ui_playerstat_redirect.h"

#include <array>
#include <cstdint>
#include <cstring>

namespace
{
using CreateWnd_t = void* (__thiscall*)(void* self, void* station, const char* xml, void* eventReceiver);
using RemoveWndStation_t = void(__thiscall*)(void* self, void* station);

constexpr DWORD UI_HOOK_RETRY_MS = 1000;
constexpr DWORD UI_CREATE_WND_OFFSET = 0x0001BD60;
constexpr DWORD UI_REMOVE_WND_STATION_OFFSET = 0x0001C460;
constexpr std::array<BYTE, 5> UI_CREATE_WND_EXPECTED = {
    0x55, 0x8B, 0xEC, 0x6A, 0xFF
};
constexpr std::array<BYTE, 6> UI_REMOVE_WND_STATION_EXPECTED = {
    0x57, 0x8B, 0xF9, 0x8B, 0x57, 0x20
};
constexpr const char* PGOG_DEBUG_CHANNEL = "PGOG";

InlineHook g_createWndHook;
InlineHook g_removeWndStationHook;
CreateWnd_t g_originalCreateWnd = nullptr;
RemoveWndStation_t g_originalRemoveWndStation = nullptr;
DWORD g_lastCreateWndHookAttempt = 0;

bool IsHookPatched(const InlineHook& hook, const void* detour)
{
    if (!hook.target || !hook.installed) {
        return false;
    }

    const BYTE* bytes = reinterpret_cast<const BYTE*>(hook.target);
    if (bytes[0] != 0xE9) {
        return false;
    }

    const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(bytes + 1);
    const std::uintptr_t patchedTarget =
        hook.target + 5 + static_cast<std::intptr_t>(relative);
    return patchedTarget == reinterpret_cast<std::uintptr_t>(detour);
}

void* __fastcall HookCreateWnd(void* self, void*, void* station, const char* xml, void* eventReceiver)
{
    ObserveUIInventoryWindow(station, xml);

    const char* resolvedXml = ResolveUIPlayerstatXml(xml);
    if (resolvedXml == xml) {
        resolvedXml = ResolveUIDaychangeXml(xml, ::GetTickCount());
    }

    return g_originalCreateWnd
        ? g_originalCreateWnd(self, station, resolvedXml, eventReceiver)
        : nullptr;
}

void __fastcall HookRemoveWndStation(void* self, void*, void* station)
{
    ObserveUIInventoryStationRemoved(station);
    if (g_originalRemoveWndStation) {
        g_originalRemoveWndStation(self, station);
    }
}
}

bool TryInstallUIWindowHook()
{
    const bool inventoryHookRequested =
        (GetRequestedHookFlags() & OYNON_HOOK_UI_INVENTORY_STATE) != 0;
    if (g_createWndHook.installed &&
        (!inventoryHookRequested || g_removeWndStationHook.installed)) {
        return true;
    }

    HMODULE uiModule = ::GetModuleHandleA("UI.dll");
    if (!uiModule) {
        return false;
    }

    if (!g_createWndHook.installed) {
        g_createWndHook.target = reinterpret_cast<std::uintptr_t>(uiModule) + UI_CREATE_WND_OFFSET;
        g_createWndHook.detour = reinterpret_cast<void*>(&HookCreateWnd);
        g_createWndHook.patchSize = UI_CREATE_WND_EXPECTED.size();

        if (std::memcmp(
                reinterpret_cast<const void*>(g_createWndHook.target),
                UI_CREATE_WND_EXPECTED.data(),
                UI_CREATE_WND_EXPECTED.size()) != 0) {
            WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI create-window hook rejected unexpected UI.dll bytes");
            return false;
        }

        if (!InstallInlineHook(g_createWndHook)) {
            WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI create-window hook install failed");
            return false;
        }

        g_originalCreateWnd = reinterpret_cast<CreateWnd_t>(g_createWndHook.trampoline);
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI create-window hook installed");
    }

    if (inventoryHookRequested && !g_removeWndStationHook.installed) {
        g_removeWndStationHook.target =
            reinterpret_cast<std::uintptr_t>(uiModule) + UI_REMOVE_WND_STATION_OFFSET;
        g_removeWndStationHook.detour = reinterpret_cast<void*>(&HookRemoveWndStation);
        g_removeWndStationHook.patchSize = UI_REMOVE_WND_STATION_EXPECTED.size();

        if (std::memcmp(
                reinterpret_cast<const void*>(g_removeWndStationHook.target),
                UI_REMOVE_WND_STATION_EXPECTED.data(),
                UI_REMOVE_WND_STATION_EXPECTED.size()) != 0) {
            WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI remove-station hook rejected unexpected UI.dll bytes");
            return false;
        }

        if (!InstallInlineHook(g_removeWndStationHook)) {
            WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI remove-station hook install failed");
            return false;
        }

        g_originalRemoveWndStation =
            reinterpret_cast<RemoveWndStation_t>(g_removeWndStationHook.trampoline);
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI remove-station hook installed");
    }

    return true;
}

bool IsUIWindowHookInstalled()
{
    return g_createWndHook.installed;
}

void PollUIWindowHook()
{
    const DWORD uiHookFlags = OYNON_HOOK_UI_DAYCHANGE_TEXT |
        OYNON_HOOK_UI_PLAYERSTAT_REDIRECT |
        OYNON_HOOK_UI_INVENTORY_STATE;
    if (!(GetRequestedHookFlags() & uiHookFlags)) {
        return;
    }

    const bool inventoryHookRequested =
        (GetRequestedHookFlags() & OYNON_HOOK_UI_INVENTORY_STATE) != 0;
    const bool createWndHookPatched =
        IsHookPatched(g_createWndHook, reinterpret_cast<const void*>(&HookCreateWnd));
    const bool removeWndStationHookPatched =
        IsHookPatched(g_removeWndStationHook, reinterpret_cast<const void*>(&HookRemoveWndStation));
    if (createWndHookPatched &&
        (!inventoryHookRequested || removeWndStationHookPatched)) {
        return;
    }

    if (g_createWndHook.installed && !createWndHookPatched) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI create-window hook patch was lost, retrying install");
        g_createWndHook.installed = false;
        g_originalCreateWnd = nullptr;
    }
    if (g_removeWndStationHook.installed && !removeWndStationHookPatched) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI remove-station hook patch was lost, retrying install");
        g_removeWndStationHook.installed = false;
        g_originalRemoveWndStation = nullptr;
    }

    const DWORD now = ::GetTickCount();
    if (now - g_lastCreateWndHookAttempt < UI_HOOK_RETRY_MS) {
        return;
    }

    g_lastCreateWndHookAttempt = now;
    TryInstallUIWindowHook();
}
