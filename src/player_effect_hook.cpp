#include "player_effect_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr std::uintptr_t PLAYER_APPLY_EFFECT_OFFSET = 0x000D0D90;
constexpr std::size_t PLAYER_APPLY_EFFECT_PATCH_SIZE = 7;
constexpr std::array<BYTE, 13> PLAYER_APPLY_EFFECT_EXPECTED = {
    0x6A, 0xFF, 0x68, 0x00, 0x00, 0x00, 0x00,
    0x64, 0xA1, 0x00, 0x00, 0x00, 0x00
};
constexpr std::array<BYTE, PLAYER_APPLY_EFFECT_EXPECTED.size()> PLAYER_APPLY_EFFECT_MASK = {
    0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

using PlayerApplyEffect_t = bool(__thiscall*)(void* self, const char* effectName, void* params);

struct PlayerEffectListener
{
    OynonPlayerEffectCallback callback = nullptr;
    void* userData = nullptr;
};

InlineHook g_playerApplyEffectHook;
PlayerApplyEffect_t g_originalPlayerApplyEffect = nullptr;
std::mutex g_listenerMutex;
std::vector<PlayerEffectListener> g_playerEffectListeners;

bool MatchesPlayerApplyEffectPrologue(const BYTE* bytes)
{
    for (std::size_t index = 0; index < PLAYER_APPLY_EFFECT_EXPECTED.size(); ++index) {
        if ((bytes[index] & PLAYER_APPLY_EFFECT_MASK[index]) !=
            PLAYER_APPLY_EFFECT_EXPECTED[index]) {
            return false;
        }
    }
    return true;
}

void DispatchPlayerEffect(const char* effectName)
{
    if (!effectName) {
        return;
    }

    std::vector<PlayerEffectListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        listeners = g_playerEffectListeners;
    }

    for (const PlayerEffectListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(effectName, listener.userData);
        }
    }
}

bool __fastcall HookPlayerApplyEffect(void* self, void*, const char* effectName, void* params)
{
    const bool applied = g_originalPlayerApplyEffect
        ? g_originalPlayerApplyEffect(self, effectName, params)
        : false;
    if (applied) {
        DispatchPlayerEffect(effectName);
    }
    return applied;
}
}

bool InstallPlayerEffectHook()
{
    if (g_playerApplyEffectHook.installed) {
        return true;
    }

    HMODULE game = ::GetModuleHandleA(nullptr);
    if (!game) {
        return false;
    }

    const std::uintptr_t target =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_APPLY_EFFECT_OFFSET;
    if (!MatchesPlayerApplyEffectPrologue(reinterpret_cast<const BYTE*>(target))) {
        WriteDebugLog("PGOG", "Oynon player effect hook rejected unexpected Game.exe bytes");
        return false;
    }

    g_playerApplyEffectHook.target = target;
    g_playerApplyEffectHook.detour = reinterpret_cast<void*>(&HookPlayerApplyEffect);
    g_playerApplyEffectHook.patchSize = PLAYER_APPLY_EFFECT_PATCH_SIZE;
    if (!InstallInlineHook(g_playerApplyEffectHook)) {
        WriteDebugLog("PGOG", "Oynon player effect hook install failed");
        return false;
    }

    g_originalPlayerApplyEffect =
        reinterpret_cast<PlayerApplyEffect_t>(g_playerApplyEffectHook.trampoline);
    return true;
}

BOOL RegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_listenerMutex);
    const auto existing = std::find_if(
        g_playerEffectListeners.begin(),
        g_playerEffectListeners.end(),
        [callback, userData](const PlayerEffectListener& listener) {
            return listener.callback == callback && listener.userData == userData;
        });
    if (existing != g_playerEffectListeners.end()) {
        return TRUE;
    }

    g_playerEffectListeners.push_back(PlayerEffectListener{ callback, userData });
    return TRUE;
}
