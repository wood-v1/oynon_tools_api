#include "player_shooting_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr std::uintptr_t SCRIPT_IS_SHOOTING_OFFSET = 0x0017B010;
constexpr std::uintptr_t SCRIPT_IS_SHOOTING_GOG_OFFSET = 0x0017B070;
constexpr std::size_t SCRIPT_IS_SHOOTING_PATCH_SIZE = 6;
constexpr std::uintptr_t PLAYER_START_SHOOTING_EVENT_OFFSET = 0x00187ED0;
constexpr std::size_t PLAYER_START_SHOOTING_EVENT_PATCH_SIZE = 6;
constexpr std::size_t SCRIPT_CONTEXT_PLAYER_OFFSET = 0x6C;
constexpr std::size_t PLAYER_SHOOTING_OFFSET = 0x34;

constexpr std::array<BYTE, SCRIPT_IS_SHOOTING_PATCH_SIZE> SCRIPT_IS_SHOOTING_EXPECTED = {
    0x83, 0x7C, 0x24, 0x08, 0x01, 0x56
};
constexpr std::array<BYTE, PLAYER_START_SHOOTING_EVENT_PATCH_SIZE> PLAYER_START_SHOOTING_EVENT_EXPECTED = {
    0x8B, 0x4C, 0x24, 0x04, 0x6A, 0x00
};

using ScriptIsShooting_t = bool(__thiscall*)(void* self, void** variables, unsigned long parameterCount, void* result);
using PlayerStartShootingEvent_t = bool(__thiscall*)(void* self, void* script);

InlineHook g_scriptIsShootingHook;
ScriptIsShooting_t g_originalScriptIsShooting = nullptr;
InlineHook g_playerStartShootingEventHook;
PlayerStartShootingEvent_t g_originalPlayerStartShootingEvent = nullptr;
std::atomic<bool> g_playerShootingBlocked{ false };

struct PlayerShootingAttemptListener
{
    OynonPlayerShootingAttemptCallback callback = nullptr;
    void* userData = nullptr;
};

std::mutex g_listenerMutex;
std::vector<PlayerShootingAttemptListener> g_playerShootingAttemptListeners;

bool MatchesBytes(std::uintptr_t target, const BYTE* expected, std::size_t size)
{
    return std::memcmp(reinterpret_cast<const void*>(target), expected, size) == 0;
}

bool TryResolveScriptIsShootingTarget(HMODULE game, std::uintptr_t& out)
{
    const std::uintptr_t steamTarget =
        reinterpret_cast<std::uintptr_t>(game) + SCRIPT_IS_SHOOTING_OFFSET;
    if (MatchesBytes(
            steamTarget,
            SCRIPT_IS_SHOOTING_EXPECTED.data(),
            SCRIPT_IS_SHOOTING_EXPECTED.size())) {
        out = steamTarget;
        return true;
    }

    WriteDebugLog("PGOG", "Oynon script IsShooting Steam bytes rejected; Trying GOG version offsets");

    const std::uintptr_t gogTarget =
        reinterpret_cast<std::uintptr_t>(game) + SCRIPT_IS_SHOOTING_GOG_OFFSET;
    if (MatchesBytes(
            gogTarget,
            SCRIPT_IS_SHOOTING_EXPECTED.data(),
            SCRIPT_IS_SHOOTING_EXPECTED.size())) {
        WriteDebugLog("PGOG", "Oynon script IsShooting hook using GOG version offsets");
        out = gogTarget;
        return true;
    }

    return false;
}

void DispatchPlayerShootingAttempt(BOOL repeated)
{
    std::vector<PlayerShootingAttemptListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        listeners = g_playerShootingAttemptListeners;
    }

    for (const PlayerShootingAttemptListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(repeated, listener.userData);
        }
    }
}

bool __fastcall HookScriptIsShooting(
    void* self,
    void*,
    void** variables,
    unsigned long parameterCount,
    void* result)
{
    if (!g_originalScriptIsShooting || !g_playerShootingBlocked.load() || !self) {
        const bool success = g_originalScriptIsShooting
            ? g_originalScriptIsShooting(self, variables, parameterCount, result)
            : false;
        if (success && self) {
            BYTE* player = *reinterpret_cast<BYTE**>(
                static_cast<BYTE*>(self) + SCRIPT_CONTEXT_PLAYER_OFFSET);
            if (player && player[PLAYER_SHOOTING_OFFSET]) {
                DispatchPlayerShootingAttempt(TRUE);
            }
        }
        return success;
    }

    BYTE* player = *reinterpret_cast<BYTE**>(
        static_cast<BYTE*>(self) + SCRIPT_CONTEXT_PLAYER_OFFSET);
    if (!player) {
        return g_originalScriptIsShooting(self, variables, parameterCount, result);
    }

    const BYTE shooting = player[PLAYER_SHOOTING_OFFSET];
    player[PLAYER_SHOOTING_OFFSET] = FALSE;
    const bool success = g_originalScriptIsShooting(self, variables, parameterCount, result);
    player[PLAYER_SHOOTING_OFFSET] = shooting;
    return success;
}

bool __fastcall HookPlayerStartShootingEvent(void* self, void*, void* script)
{
    if (g_playerShootingBlocked.load()) {
        return true;
    }

    const bool success = g_originalPlayerStartShootingEvent
        ? g_originalPlayerStartShootingEvent(self, script)
        : false;
    if (success) {
        DispatchPlayerShootingAttempt(FALSE);
    }
    return success;
}
}

bool InstallPlayerShootingHook()
{
    if (g_scriptIsShootingHook.installed && g_playerStartShootingEventHook.installed) {
        return true;
    }

    HMODULE game = ::GetModuleHandleA(nullptr);
    if (!game) {
        return false;
    }

    std::uintptr_t target = 0;
    if (!g_scriptIsShootingHook.installed &&
        !TryResolveScriptIsShootingTarget(game, target)) {
        WriteDebugLog("PGOG", "Oynon init failed: script IsShooting hook rejected unexpected Game.exe bytes");
        return false;
    }

    const std::uintptr_t startShootingEventTarget =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_START_SHOOTING_EVENT_OFFSET;
    if (!g_playerStartShootingEventHook.installed &&
        !MatchesBytes(
            startShootingEventTarget,
            PLAYER_START_SHOOTING_EVENT_EXPECTED.data(),
            PLAYER_START_SHOOTING_EVENT_EXPECTED.size())) {
        WriteDebugLog("PGOG", "Oynon init failed: player start-shooting hook rejected unexpected Game.exe bytes");
        return false;
    }

    if (!g_scriptIsShootingHook.installed) {
        g_scriptIsShootingHook.target = target;
        g_scriptIsShootingHook.detour = reinterpret_cast<void*>(&HookScriptIsShooting);
        g_scriptIsShootingHook.patchSize = SCRIPT_IS_SHOOTING_PATCH_SIZE;
        if (!InstallInlineHook(g_scriptIsShootingHook)) {
            return false;
        }

        g_originalScriptIsShooting =
            reinterpret_cast<ScriptIsShooting_t>(g_scriptIsShootingHook.trampoline);
    }

    if (!g_playerStartShootingEventHook.installed) {
        g_playerStartShootingEventHook.target = startShootingEventTarget;
        g_playerStartShootingEventHook.detour =
            reinterpret_cast<void*>(&HookPlayerStartShootingEvent);
        g_playerStartShootingEventHook.patchSize = PLAYER_START_SHOOTING_EVENT_PATCH_SIZE;
        if (!InstallInlineHook(g_playerStartShootingEventHook)) {
            return false;
        }

        g_originalPlayerStartShootingEvent =
            reinterpret_cast<PlayerStartShootingEvent_t>(
                g_playerStartShootingEventHook.trampoline);
    }

    return true;
}

BOOL SetPlayerShootingBlocked(BOOL blocked)
{
    g_playerShootingBlocked.store(blocked != FALSE);
    return TRUE;
}

BOOL RegisterPlayerShootingAttemptCallback(
    OynonPlayerShootingAttemptCallback callback,
    void* userData)
{
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_listenerMutex);
    const auto existing = std::find_if(
        g_playerShootingAttemptListeners.begin(),
        g_playerShootingAttemptListeners.end(),
        [callback, userData](const PlayerShootingAttemptListener& listener) {
            return listener.callback == callback && listener.userData == userData;
        });
    if (existing != g_playerShootingAttemptListeners.end()) {
        return TRUE;
    }

    g_playerShootingAttemptListeners.push_back(
        PlayerShootingAttemptListener{ callback, userData });
    return TRUE;
}
