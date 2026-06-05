#include "player_use_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr std::uintptr_t PLAYER_USE_EVENT_OFFSET = 0x00187FB0;
constexpr std::uintptr_t PLAYER_USE_EVENT_GOG_OFFSET = 0x00188010;
constexpr std::size_t PLAYER_USE_EVENT_PATCH_SIZE = 6;
constexpr std::array<BYTE, 15> PLAYER_USE_EVENT_EXPECTED = {
    0x8B, 0x49, 0x08, 0x83, 0xEC, 0x10, 0x8B, 0x01,
    0x68, 0x2A, 0x56, 0x06, 0x0C, 0xFF, 0x50
};
constexpr std::size_t SCRIPT_INFO_OFFSET = 0x08;
constexpr std::size_t SCRIPT_NAME_OFFSET = 0x08;
constexpr std::size_t MAX_SCRIPT_NAME_LENGTH = 260;

using PlayerUseEvent_t = bool(__thiscall*)(void* self, void* script);

struct PlayerUseListener
{
    OynonPlayerUseCallback callback = nullptr;
    void* userData = nullptr;
};

InlineHook g_playerUseEventHook;
PlayerUseEvent_t g_originalPlayerUseEvent = nullptr;
std::mutex g_listenerMutex;
std::vector<PlayerUseListener> g_playerUseListeners;

bool MatchesPlayerUseEventPrologue(const BYTE* bytes)
{
    return std::memcmp(
        bytes,
        PLAYER_USE_EVENT_EXPECTED.data(),
        PLAYER_USE_EVENT_EXPECTED.size()) == 0;
}

bool TryResolvePlayerUseEventTarget(HMODULE game, std::uintptr_t& out)
{
    const std::uintptr_t steamTarget =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_USE_EVENT_OFFSET;
    if (MatchesPlayerUseEventPrologue(reinterpret_cast<const BYTE*>(steamTarget))) {
        out = steamTarget;
        return true;
    }

    WriteDebugLog("PGOG", "Oynon player use Steam bytes rejected; Trying GOG version offsets");

    const std::uintptr_t gogTarget =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_USE_EVENT_GOG_OFFSET;
    if (MatchesPlayerUseEventPrologue(reinterpret_cast<const BYTE*>(gogTarget))) {
        WriteDebugLog("PGOG", "Oynon player use hook using GOG version offsets");
        out = gogTarget;
        return true;
    }

    return false;
}

bool HasBinSuffix(const char* scriptName, std::size_t length)
{
    if (!scriptName || length < 4) {
        return false;
    }

    const char* suffix = scriptName + length - 4;
    return suffix[0] == '.' &&
        std::tolower(static_cast<unsigned char>(suffix[1])) == 'b' &&
        std::tolower(static_cast<unsigned char>(suffix[2])) == 'i' &&
        std::tolower(static_cast<unsigned char>(suffix[3])) == 'n';
}

bool TryReadRuntimeScriptName(void* script, char* out, std::size_t outSize)
{
    if (!script || !out || outSize < 2) {
        return false;
    }

    __try {
        const BYTE* scriptBytes = static_cast<const BYTE*>(script);
        const void* scriptInfo =
            *reinterpret_cast<void* const*>(scriptBytes + SCRIPT_INFO_OFFSET);
        if (!scriptInfo) {
            return false;
        }

        const BYTE* scriptInfoBytes = static_cast<const BYTE*>(scriptInfo);
        const char* scriptName =
            *reinterpret_cast<const char* const*>(scriptInfoBytes + SCRIPT_NAME_OFFSET);
        if (!scriptName) {
            return false;
        }

        for (std::size_t index = 0; index + 1 < outSize; ++index) {
            const char character = scriptName[index];
            out[index] = character;
            if (character == '\0') {
                return index > 0 && HasBinSuffix(out, index);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    out[0] = '\0';
    return false;
}

void DispatchPlayerUse(const char* scriptName)
{
    if (!scriptName) {
        return;
    }

    std::vector<PlayerUseListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        listeners = g_playerUseListeners;
    }

    for (const PlayerUseListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(scriptName, listener.userData);
        }
    }
}

bool __fastcall HookPlayerUseEvent(void* self, void*, void* script)
{
    const bool used = g_originalPlayerUseEvent
        ? g_originalPlayerUseEvent(self, script)
        : false;
    if (!used) {
        return false;
    }

    char scriptName[MAX_SCRIPT_NAME_LENGTH] = {};
    if (TryReadRuntimeScriptName(script, scriptName, sizeof(scriptName))) {
        DispatchPlayerUse(scriptName);
    }
    return true;
}
}

bool InstallPlayerUseHook()
{
    if (g_playerUseEventHook.installed) {
        return true;
    }

    HMODULE game = ::GetModuleHandleA(nullptr);
    if (!game) {
        return false;
    }

    std::uintptr_t target = 0;
    if (!TryResolvePlayerUseEventTarget(game, target)) {
        WriteDebugLog("PGOG", "Oynon player use hook rejected unexpected Game.exe bytes");
        return false;
    }

    g_playerUseEventHook.target = target;
    g_playerUseEventHook.detour = reinterpret_cast<void*>(&HookPlayerUseEvent);
    g_playerUseEventHook.patchSize = PLAYER_USE_EVENT_PATCH_SIZE;
    if (!InstallInlineHook(g_playerUseEventHook)) {
        WriteDebugLog("PGOG", "Oynon player use hook install failed");
        return false;
    }

    g_originalPlayerUseEvent =
        reinterpret_cast<PlayerUseEvent_t>(g_playerUseEventHook.trampoline);
    return true;
}

BOOL RegisterPlayerUseCallback(OynonPlayerUseCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_listenerMutex);
    const auto existing = std::find_if(
        g_playerUseListeners.begin(),
        g_playerUseListeners.end(),
        [callback, userData](const PlayerUseListener& listener) {
            return listener.callback == callback && listener.userData == userData;
        });
    if (existing != g_playerUseListeners.end()) {
        return TRUE;
    }

    g_playerUseListeners.push_back(PlayerUseListener{ callback, userData });
    return TRUE;
}
