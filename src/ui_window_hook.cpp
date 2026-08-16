#include "ui_window_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"
#include "oynontools_state.h"
#include "ui_daychange_hook.h"
#include "ui_inventory_redirect.h"
#include "ui_inventory_state.h"
#include "ui_playerstat_redirect.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

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
constexpr const char* OYNONTOOLS_DEBUG_CHANNEL = "OynonTools";
constexpr std::size_t MAX_RECENT_UI_HOST_CONTEXTS = 32;

InlineHook g_createWndHook;
InlineHook g_removeWndStationHook;
CreateWnd_t g_originalCreateWnd = nullptr;
RemoveWndStation_t g_originalRemoveWndStation = nullptr;
DWORD g_lastCreateWndHookAttempt = 0;
SRWLOCK g_prepareListenerLock = SRWLOCK_INIT;

struct UIWindowPrepareListener
{
    OynonUIWindowPrepareCallback callback = nullptr;
    void* userData = nullptr;
};

std::vector<UIWindowPrepareListener> g_prepareListeners;
thread_local bool g_dispatchingUIWindowPrepare = false;

struct UIWindowCreatedListener
{
    OynonUIWindowCreatedCallback callback = nullptr;
    void* userData = nullptr;
};

std::vector<UIWindowCreatedListener> g_createdListeners;

struct UICompanionWindow
{
    std::string hostXml;
    std::string companionXml;
};

std::vector<UICompanionWindow> g_companionWindows;

struct UIHostContext
{
    std::string originalXml;
    void* self = nullptr;
    void* station = nullptr;
    void* eventReceiver = nullptr;
};

struct UIPersistentCompanionWindow
{
    std::string hostXml;
    std::string companionXml;
    void* attachedStation = nullptr;
};

struct UIPendingPersistentCompanion
{
    std::string hostXml;
    std::string companionXml;
    void* self = nullptr;
    void* station = nullptr;
    void* eventReceiver = nullptr;
};

std::vector<UIHostContext> g_recentHostContexts;
std::vector<UIPersistentCompanionWindow> g_persistentCompanionWindows;

struct UIWindowRedirect
{
    std::string hostXml;
    std::string replacementXml;
};

std::vector<UIWindowRedirect> g_windowRedirects;
std::vector<UIWindowRedirect> g_oneShotWindowRedirects;

void* TimedCreateWnd(
    void* self,
    void* station,
    const char* originalXml,
    const char* resolvedXml,
    void* eventReceiver);

std::string ConsumeUIOneShotWindowRedirect(const char* hostXml)
{
    if (!hostXml) {
        return {};
    }

    std::string result;
    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (auto entry = g_oneShotWindowRedirects.begin();
         entry != g_oneShotWindowRedirects.end();
         ++entry) {
        if (entry->hostXml == hostXml) {
            result = entry->replacementXml;
            g_oneShotWindowRedirects.erase(entry);
            break;
        }
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return result;
}

std::string ResolveUIWindowRedirect(const char* hostXml)
{
    if (!hostXml) {
        return {};
    }

    std::string result;
    ::AcquireSRWLockShared(&g_prepareListenerLock);
    for (const UIWindowRedirect& entry : g_windowRedirects) {
        if (entry.hostXml == hostXml) {
            result = entry.replacementXml;
            break;
        }
    }
    ::ReleaseSRWLockShared(&g_prepareListenerLock);
    return result;
}

std::string ResolveUICompanionWindow(const char* hostXml)
{
    if (!hostXml) {
        return {};
    }

    std::string result;
    ::AcquireSRWLockShared(&g_prepareListenerLock);
    for (const UICompanionWindow& entry : g_companionWindows) {
        if (entry.hostXml == hostXml) {
            result = entry.companionXml;
            break;
        }
    }
    ::ReleaseSRWLockShared(&g_prepareListenerLock);
    return result;
}

void RememberUIHostContext(
    const char* originalXml,
    void* self,
    void* station,
    void* eventReceiver,
    void* window)
{
    if (!window || !originalXml || originalXml[0] == '\0' || !self || !station) {
        return;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (auto entry = g_recentHostContexts.begin(); entry != g_recentHostContexts.end();) {
        if (entry->originalXml == originalXml && entry->station == station) {
            entry = g_recentHostContexts.erase(entry);
        }
        else {
            ++entry;
        }
    }
    g_recentHostContexts.push_back({ originalXml, self, station, eventReceiver });
    if (g_recentHostContexts.size() > MAX_RECENT_UI_HOST_CONTEXTS) {
        g_recentHostContexts.erase(g_recentHostContexts.begin());
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
}

std::vector<UIPendingPersistentCompanion> CollectPendingPersistentCompanions()
{
    std::vector<UIPendingPersistentCompanion> pending;
    ::AcquireSRWLockShared(&g_prepareListenerLock);
    for (const UIPersistentCompanionWindow& companion : g_persistentCompanionWindows) {
        if (companion.companionXml.empty()) {
            continue;
        }
        for (auto context = g_recentHostContexts.rbegin();
             context != g_recentHostContexts.rend();
             ++context) {
            if (context->originalXml == companion.hostXml &&
                context->station != companion.attachedStation) {
                pending.push_back({
                    companion.hostXml,
                    companion.companionXml,
                    context->self,
                    context->station,
                    context->eventReceiver
                });
                break;
            }
        }
    }
    ::ReleaseSRWLockShared(&g_prepareListenerLock);
    return pending;
}

void MarkPersistentCompanionAttached(
    const std::string& hostXml,
    const std::string& companionXml,
    void* station)
{
    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (UIPersistentCompanionWindow& companion : g_persistentCompanionWindows) {
        if (companion.hostXml == hostXml && companion.companionXml == companionXml) {
            companion.attachedStation = station;
            break;
        }
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
}

void CreatePendingPersistentCompanions()
{
    if (!g_originalCreateWnd) {
        return;
    }

    const std::vector<UIPendingPersistentCompanion> pending =
        CollectPendingPersistentCompanions();
    for (const UIPendingPersistentCompanion& companion : pending) {
        void* window = TimedCreateWnd(
            companion.self,
            companion.station,
            companion.companionXml.c_str(),
            companion.companionXml.c_str(),
            companion.eventReceiver);
        if (window) {
            MarkPersistentCompanionAttached(
                companion.hostXml,
                companion.companionXml,
                companion.station);
            WriteDebugLog(
                OYNONTOOLS_DEBUG_CHANNEL,
                "Oynon persistent UI companion window created");
        }
        else {
            WriteDebugLog(
                OYNONTOOLS_DEBUG_CHANNEL,
                "Oynon persistent UI companion window creation failed");
        }
    }
}

void ForgetUIStation(void* station)
{
    if (!station) {
        return;
    }
    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (auto entry = g_recentHostContexts.begin(); entry != g_recentHostContexts.end();) {
        if (entry->station == station) {
            entry = g_recentHostContexts.erase(entry);
        }
        else {
            ++entry;
        }
    }
    for (UIPersistentCompanionWindow& companion : g_persistentCompanionWindows) {
        if (companion.attachedStation == station) {
            companion.attachedStation = nullptr;
        }
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
}

void DispatchUIWindowPrepare(const char* xml)
{
    std::vector<UIWindowPrepareListener> listeners;
    ::AcquireSRWLockShared(&g_prepareListenerLock);
    listeners = g_prepareListeners;
    ::ReleaseSRWLockShared(&g_prepareListenerLock);

    const bool wasDispatching = g_dispatchingUIWindowPrepare;
    g_dispatchingUIWindowPrepare = true;
    for (const UIWindowPrepareListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(xml, listener.userData);
        }
    }
    g_dispatchingUIWindowPrepare = wasDispatching;
}

void DispatchUIWindowCreated(
    const char* originalXml,
    const char* resolvedXml,
    void* window,
    DWORD elapsedMicroseconds)
{
    std::vector<UIWindowCreatedListener> listeners;
    ::AcquireSRWLockShared(&g_prepareListenerLock);
    listeners = g_createdListeners;
    ::ReleaseSRWLockShared(&g_prepareListenerLock);

    for (const UIWindowCreatedListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(
                originalXml,
                resolvedXml,
                window ? TRUE : FALSE,
                elapsedMicroseconds,
                listener.userData);
        }
    }
}

DWORD ElapsedMicroseconds(const LARGE_INTEGER& start, const LARGE_INTEGER& finish)
{
    static LARGE_INTEGER frequency = []() {
        LARGE_INTEGER value = {};
        ::QueryPerformanceFrequency(&value);
        return value;
    }();
    if (frequency.QuadPart <= 0 || finish.QuadPart <= start.QuadPart) {
        return 0;
    }

    const unsigned long long ticks = static_cast<unsigned long long>(
        finish.QuadPart - start.QuadPart);
    const unsigned long long microseconds =
        (ticks * 1000000ull) / static_cast<unsigned long long>(frequency.QuadPart);
    return microseconds > MAXDWORD
        ? MAXDWORD
        : static_cast<DWORD>(microseconds);
}

void* TimedCreateWnd(
    void* self,
    void* station,
    const char* originalXml,
    const char* resolvedXml,
    void* eventReceiver)
{
    LARGE_INTEGER start = {};
    LARGE_INTEGER finish = {};
    ::QueryPerformanceCounter(&start);
    void* window = g_originalCreateWnd
        ? g_originalCreateWnd(self, station, resolvedXml, eventReceiver)
        : nullptr;
    ::QueryPerformanceCounter(&finish);
    DispatchUIWindowCreated(
        originalXml,
        resolvedXml,
        window,
        ElapsedMicroseconds(start, finish));
    return window;
}

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
    DispatchUIWindowPrepare(xml);
    ObserveUIInventoryWindow(station, xml);

    const std::string oneShotRedirect = ConsumeUIOneShotWindowRedirect(xml);
    const std::string genericRedirect = oneShotRedirect.empty()
        ? ResolveUIWindowRedirect(xml)
        : oneShotRedirect;
    const char* resolvedXml = genericRedirect.empty()
        ? xml
        : genericRedirect.c_str();
    if (resolvedXml == xml) {
        resolvedXml = ResolveUIInventoryXml(xml);
    }
    if (resolvedXml == xml) {
        resolvedXml = ResolveUIPlayerstatXml(xml);
    }
    if (resolvedXml == xml) {
        resolvedXml = ResolveUIDaychangeXml(xml, ::GetTickCount());
    }

    void* window = TimedCreateWnd(
        self,
        station,
        xml,
        resolvedXml,
        eventReceiver);

    RememberUIHostContext(xml, self, station, eventReceiver, window);

    const std::string companionXml = ResolveUICompanionWindow(xml);
    if (g_originalCreateWnd &&
        !companionXml.empty() &&
        companionXml != xml) {
        TimedCreateWnd(
            self,
            station,
            companionXml.c_str(),
            companionXml.c_str(),
            eventReceiver);
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI companion window created");
    }
    CreatePendingPersistentCompanions();
    return window;
}

BOOL RegisterUIWindowPrepareCallbackImpl(OynonUIWindowPrepareCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (const UIWindowPrepareListener& listener : g_prepareListeners) {
        if (listener.callback == callback && listener.userData == userData) {
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    g_prepareListeners.push_back({ callback, userData });
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

BOOL RegisterUIWindowCreatedCallbackImpl(OynonUIWindowCreatedCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (const UIWindowCreatedListener& listener : g_createdListeners) {
        if (listener.callback == callback && listener.userData == userData) {
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    g_createdListeners.push_back({ callback, userData });
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

void __fastcall HookRemoveWndStation(void* self, void*, void* station)
{
    ObserveUIInventoryStationRemoved(station);
    ForgetUIStation(station);
    if (g_originalRemoveWndStation) {
        g_originalRemoveWndStation(self, station);
    }
}
}

BOOL RegisterUIWindowPrepareListener(OynonUIWindowPrepareCallback callback, void* userData)
{
    return RegisterUIWindowPrepareCallbackImpl(callback, userData);
}

bool IsDispatchingUIWindowPrepare()
{
    return g_dispatchingUIWindowPrepare;
}

BOOL RegisterUIWindowCreatedListener(OynonUIWindowCreatedCallback callback, void* userData)
{
    return RegisterUIWindowCreatedCallbackImpl(callback, userData);
}

BOOL SetUIWindowRedirect(const char* hostXml, const char* replacementXml)
{
    if (!hostXml || hostXml[0] == '\0') {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (UIWindowRedirect& entry : g_windowRedirects) {
        if (entry.hostXml == hostXml) {
            entry.replacementXml = replacementXml ? replacementXml : "";
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    g_windowRedirects.push_back({
        hostXml,
        replacementXml ? replacementXml : ""
    });
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

BOOL SetUIOneShotWindowRedirect(const char* hostXml, const char* replacementXml)
{
    if (!hostXml || hostXml[0] == '\0') {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (UIWindowRedirect& entry : g_oneShotWindowRedirects) {
        if (entry.hostXml == hostXml) {
            entry.replacementXml = replacementXml ? replacementXml : "";
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    if (replacementXml && replacementXml[0] != '\0') {
        g_oneShotWindowRedirects.push_back({ hostXml, replacementXml });
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

BOOL SetUICompanionWindow(const char* hostXml, const char* companionXml)
{
    if (!hostXml || hostXml[0] == '\0') {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (UICompanionWindow& entry : g_companionWindows) {
        if (entry.hostXml == hostXml) {
            entry.companionXml = companionXml ? companionXml : "";
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    g_companionWindows.push_back({
        hostXml,
        companionXml ? companionXml : ""
    });
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

BOOL AddUIPersistentCompanionWindow(const char* hostXml, const char* companionXml)
{
    if (!hostXml || hostXml[0] == '\0' || !companionXml || companionXml[0] == '\0') {
        return FALSE;
    }

    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (const UIPersistentCompanionWindow& entry : g_persistentCompanionWindows) {
        if (entry.hostXml == hostXml && entry.companionXml == companionXml) {
            ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
            return TRUE;
        }
    }
    g_persistentCompanionWindows.push_back({ hostXml, companionXml, nullptr });
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return TRUE;
}

BOOL RemoveUIPersistentCompanionWindow(const char* hostXml, const char* companionXml)
{
    if (!hostXml || hostXml[0] == '\0' || !companionXml || companionXml[0] == '\0') {
        return FALSE;
    }

    bool removed = false;
    ::AcquireSRWLockExclusive(&g_prepareListenerLock);
    for (auto entry = g_persistentCompanionWindows.begin();
         entry != g_persistentCompanionWindows.end();) {
        if (entry->hostXml == hostXml && entry->companionXml == companionXml) {
            entry = g_persistentCompanionWindows.erase(entry);
            removed = true;
        }
        else {
            ++entry;
        }
    }
    ::ReleaseSRWLockExclusive(&g_prepareListenerLock);
    return removed ? TRUE : FALSE;
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
            WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI create-window hook rejected unexpected UI.dll bytes");
            return false;
        }

        if (!InstallInlineHook(g_createWndHook)) {
            WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI create-window hook install failed");
            return false;
        }

        g_originalCreateWnd = reinterpret_cast<CreateWnd_t>(g_createWndHook.trampoline);
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI create-window hook installed");
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
            WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI remove-station hook rejected unexpected UI.dll bytes");
            return false;
        }

        if (!InstallInlineHook(g_removeWndStationHook)) {
            WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI remove-station hook install failed");
            return false;
        }

        g_originalRemoveWndStation =
            reinterpret_cast<RemoveWndStation_t>(g_removeWndStationHook.trampoline);
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI remove-station hook installed");
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
        OYNON_HOOK_UI_INVENTORY_STATE |
        OYNON_HOOK_UI_INVENTORY_REDIRECT |
        OYNON_HOOK_UI_WINDOW_PREPARE;
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
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI create-window hook patch was lost, retrying install");
        g_createWndHook.installed = false;
        g_originalCreateWnd = nullptr;
    }
    if (g_removeWndStationHook.installed && !removeWndStationHookPatched) {
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI remove-station hook patch was lost, retrying install");
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
