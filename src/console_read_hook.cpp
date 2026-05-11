#include "console_read_hook.h"

#include "inline_hook_utils.h"

#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
using AddConsoleMessage1_t = void(__thiscall*)(void* self, const char* message);
using AddConsoleMessage2_t = void(__thiscall*)(void* self, const char* message, const ColorValue& color);

constexpr DWORD ADD_CONSOLE_MESSAGE_1_OFFSET = 0x0003C0F0;
constexpr DWORD ADD_CONSOLE_MESSAGE_2_OFFSET = 0x0003C2D0;

struct ConsoleMessageListener
{
    OynonConsoleMessageCallback callback = nullptr;
    void* userData = nullptr;
};

std::mutex g_listenerMutex;
std::vector<ConsoleMessageListener> g_consoleMessageListeners;

InlineHook g_consoleHook1;
InlineHook g_consoleHook2;
AddConsoleMessage1_t g_originalAddConsoleMessage1 = nullptr;
AddConsoleMessage2_t g_originalAddConsoleMessage2 = nullptr;

void DispatchConsoleMessage(const char* message)
{
    if (!message) {
        return;
    }

    std::vector<ConsoleMessageListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        listeners = g_consoleMessageListeners;
    }

    for (const ConsoleMessageListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(message, listener.userData);
        }
    }
}

void __fastcall HookAddConsoleMessage1(void* self, void*, const char* message)
{
    DispatchConsoleMessage(message);
    if (g_originalAddConsoleMessage1) {
        g_originalAddConsoleMessage1(self, message);
    }
}

void __fastcall HookAddConsoleMessage2(void* self, void*, const char* message, const ColorValue& color)
{
    DispatchConsoleMessage(message);
    if (g_originalAddConsoleMessage2) {
        g_originalAddConsoleMessage2(self, message, color);
    }
}
}

bool InstallConsoleReadHooks(DWORD engineBase)
{
    if (g_consoleHook1.installed && g_consoleHook2.installed) {
        return true;
    }

    g_consoleHook1.target = engineBase + ADD_CONSOLE_MESSAGE_1_OFFSET;
    g_consoleHook1.detour = reinterpret_cast<void*>(&HookAddConsoleMessage1);
    g_consoleHook1.patchSize = 7;

    g_consoleHook2.target = engineBase + ADD_CONSOLE_MESSAGE_2_OFFSET;
    g_consoleHook2.detour = reinterpret_cast<void*>(&HookAddConsoleMessage2);
    g_consoleHook2.patchSize = 7;

    if (!InstallInlineHook(g_consoleHook1)) {
        return false;
    }
    g_originalAddConsoleMessage1 = reinterpret_cast<AddConsoleMessage1_t>(g_consoleHook1.trampoline);

    if (!InstallInlineHook(g_consoleHook2)) {
        return false;
    }
    g_originalAddConsoleMessage2 = reinterpret_cast<AddConsoleMessage2_t>(g_consoleHook2.trampoline);
    return true;
}

BOOL RegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_listenerMutex);
    const auto existing = std::find_if(
        g_consoleMessageListeners.begin(),
        g_consoleMessageListeners.end(),
        [callback, userData](const ConsoleMessageListener& listener) {
            return listener.callback == callback && listener.userData == userData;
        });
    if (existing != g_consoleMessageListeners.end()) {
        return TRUE;
    }

    g_consoleMessageListeners.push_back(ConsoleMessageListener{ callback, userData });
    return TRUE;
}
