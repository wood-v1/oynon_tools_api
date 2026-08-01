#include "keyboard_input.h"

#include <array>
#include <mutex>
#include <vector>

namespace
{
struct KeyboardListener
{
    OynonKeyboardCallback callback = nullptr;
    void* userData = nullptr;
};

std::mutex g_keyboardMutex;
std::vector<KeyboardListener> g_keyboardListeners;
std::array<bool, 256> g_keyStates{};

bool IsGameWindowForeground()
{
    HWND foreground = ::GetForegroundWindow();
    if (foreground == nullptr) {
        return false;
    }

    DWORD processId = 0;
    ::GetWindowThreadProcessId(foreground, &processId);
    return processId == ::GetCurrentProcessId();
}

void ResetKeyStates()
{
    g_keyStates.fill(false);
}
}

BOOL RegisterKeyboardCallback(OynonKeyboardCallback callback, void* userData)
{
    if (callback == nullptr) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_keyboardMutex);
    g_keyboardListeners.push_back({callback, userData});
    return TRUE;
}

void PollKeyboardInput()
{
    if (!IsGameWindowForeground()) {
        ResetKeyStates();
        return;
    }

    std::vector<KeyboardListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_keyboardMutex);
        listeners = g_keyboardListeners;
    }

    if (listeners.empty()) {
        ResetKeyStates();
        return;
    }

    for (DWORD virtualKey = 0; virtualKey < g_keyStates.size(); ++virtualKey) {
        const bool pressed = (::GetAsyncKeyState(static_cast<int>(virtualKey)) & 0x8000) != 0;
        if (pressed == g_keyStates[virtualKey]) {
            continue;
        }

        g_keyStates[virtualKey] = pressed;
        for (const KeyboardListener& listener : listeners) {
            listener.callback(virtualKey, pressed ? TRUE : FALSE, listener.userData);
        }
    }
}
