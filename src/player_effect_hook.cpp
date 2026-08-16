#include "player_effect_hook.h"

#include "console_execute_hook.h"
#include "debug.h"
#include "inline_hook_utils.h"
#include "player_inventory_capacity_hook.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace
{
constexpr std::uintptr_t PLAYER_APPLY_EFFECT_OFFSET = 0x000D0D90;
constexpr std::uintptr_t PLAYER_APPLY_EFFECT_GOG_OFFSET = 0x000D0DF0;
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
std::string g_playerBootstrapEffect;
void* g_observedPlayer = nullptr;
void* g_bootstrappedPlayer = nullptr;
void* g_deferredBootstrapPlayer = nullptr;
void* g_confirmedBootstrapPlayer = nullptr;

struct PendingPlayerEffect
{
    std::string effectName;
};

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

bool TryResolvePlayerApplyEffectTarget(HMODULE game, std::uintptr_t& out)
{
    const std::uintptr_t steamTarget =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_APPLY_EFFECT_OFFSET;
    if (MatchesPlayerApplyEffectPrologue(reinterpret_cast<const BYTE*>(steamTarget))) {
        out = steamTarget;
        return true;
    }

    WriteDebugLog("OynonTools", "Oynon player effect Steam bytes rejected; Trying GOG version offsets");

    const std::uintptr_t gogTarget =
        reinterpret_cast<std::uintptr_t>(game) + PLAYER_APPLY_EFFECT_GOG_OFFSET;
    if (MatchesPlayerApplyEffectPrologue(reinterpret_cast<const BYTE*>(gogTarget))) {
        WriteDebugLog("OynonTools", "Oynon player effect hook using GOG version offsets");
        out = gogTarget;
        return true;
    }

    return false;
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

void __stdcall ApplyObservedPlayerEffectOnGameThread(void* context)
{
    std::unique_ptr<PendingPlayerEffect> pending(
        static_cast<PendingPlayerEffect*>(context));
    if (!pending || pending->effectName.empty()) {
        return;
    }

    void* player = nullptr;
    PlayerApplyEffect_t applyEffect = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        player = g_observedPlayer;
        applyEffect = g_originalPlayerApplyEffect;
    }
    if (!player || !applyEffect) {
        WriteDebugLog(
            "OynonTools",
            "Oynon observed-player effect skipped: player is unavailable");
        return;
    }

    const bool applied = applyEffect(
        player,
        pending->effectName.c_str(),
        nullptr);
    WriteDebugLog(
        "OynonTools",
        applied
            ? "Oynon observed-player effect applied on game thread"
            : "Oynon observed-player effect rejected on game thread");
}

bool __fastcall HookPlayerApplyEffect(void* self, void*, const char* effectName, void* params)
{
    const bool applied = g_originalPlayerApplyEffect
        ? g_originalPlayerApplyEffect(self, effectName, params)
        : false;
    if (applied) {
        bool playerChanged = false;
        bool bootstrapPending = false;
        {
            std::lock_guard<std::mutex> lock(g_listenerMutex);
            if (self != g_observedPlayer) {
                g_observedPlayer = self;
                g_bootstrappedPlayer = nullptr;
                g_deferredBootstrapPlayer = nullptr;
                g_confirmedBootstrapPlayer = nullptr;
                playerChanged = true;
            }
            bootstrapPending = !g_playerBootstrapEffect.empty() &&
                g_bootstrappedPlayer != g_observedPlayer;
        }
        if (playerChanged) {
            // Category objects and the script-native context belong to the
            // previous world.  Their memory may remain readable briefly after
            // a save-to-new-game transition, so readability checks alone are
            // insufficient to prevent a stale virtual call.
            ResetCapturedPlayerInventoryState();
            WriteDebugLog(
                "OynonTools",
                "Oynon player bootstrap queued until inventory is ready");
        }

        // A successful ApplyEffect only proves that a player-like actor exists.
        // The prologue and finale hubs also create transitional player actors,
        // but those actors do not own a complete five-category inventory. Probe
        // the verified inventory manager before constructing the bootstrap task.
        if (bootstrapPending) {
            RefreshObservedPlayerInventoryState();
            TryApplyPendingPlayerBootstrap();
        }
        DispatchPlayerEffect(effectName);
    }
    return applied;
}

}

BOOL SetPlayerBootstrapEffect(const char* effectName)
{
    std::lock_guard<std::mutex> lock(g_listenerMutex);
    g_playerBootstrapEffect = effectName ? effectName : "";
    g_bootstrappedPlayer = nullptr;
    g_deferredBootstrapPlayer = nullptr;
    g_confirmedBootstrapPlayer = nullptr;
    return TRUE;
}

BOOL RearmPlayerBootstrapEffect()
{
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_playerBootstrapEffect.empty()) {
            return FALSE;
        }

        // A save load may reuse the same player address after destroying the
        // object that previously occupied it. Do not let the readiness path
        // inspect that stale object before a native call from the new HUD has
        // supplied its current script context.
        g_observedPlayer = nullptr;
        g_bootstrappedPlayer = nullptr;
        g_deferredBootstrapPlayer = nullptr;
        g_confirmedBootstrapPlayer = nullptr;
    }

    ResetCapturedPlayerInventoryState();
    WriteDebugLog("OynonTools", "Oynon player bootstrap rearmed for UI session");
    return TRUE;
}

BOOL ConfirmPlayerBootstrapReady()
{
    void* player = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_playerBootstrapEffect.empty() || !g_observedPlayer) {
            return FALSE;
        }
        player = g_observedPlayer;
        g_confirmedBootstrapPlayer = player;
    }

    // The caller confirms a gameplay lifecycle point (for example the first
    // real-world transition UI). Structural inventory validation remains a
    // separate requirement and is intentionally performed on the game thread.
    RefreshObservedPlayerInventoryState();
    TryApplyPendingPlayerBootstrap();

    std::lock_guard<std::mutex> lock(g_listenerMutex);
    return g_observedPlayer == player ? TRUE : FALSE;
}

void* GetObservedPlayerObject()
{
    std::lock_guard<std::mutex> lock(g_listenerMutex);
    return g_observedPlayer;
}

void RecoverObservedPlayerFromInventoryManager(void* playerInventory)
{
    if (!playerInventory) {
        return;
    }

    // The five-category inventory manager is embedded 0x18 bytes before the
    // interface used by Player::ApplyEffect. Save loading restores inventory
    // data directly and may never call ApplyEffect, so the regular hook has no
    // opportunity to observe the reconstructed player. The relation is the
    // inverse of the verified -24 bootstrap scan in
    // CapturePlayerCategoriesFromObservedPlayer().
    void* recoveredPlayer = static_cast<BYTE*>(playerInventory) + 0x18;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_observedPlayer != recoveredPlayer) {
            g_observedPlayer = recoveredPlayer;
            g_bootstrappedPlayer = nullptr;
            g_deferredBootstrapPlayer = nullptr;
            g_confirmedBootstrapPlayer = nullptr;
            changed = true;
        }
    }

    if (changed) {
        WriteDebugLog(
            "OynonTools",
            "Oynon observed player recovered from five-category inventory manager");
    }
}

BOOL ApplyObservedPlayerEffect(const char* effectName)
{
    if (!effectName || !effectName[0] || !IsObservedPlayerInventoryReady()) {
        return FALSE;
    }

    auto pending = std::make_unique<PendingPlayerEffect>();
    pending->effectName = effectName;
    if (!DispatchGameThreadTask(
            &ApplyObservedPlayerEffectOnGameThread,
            pending.get())) {
        return FALSE;
    }
    pending.release();
    return TRUE;
}

void TryApplyPendingPlayerBootstrap()
{
    void* player = nullptr;
    std::string bootstrapEffect;
    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_playerBootstrapEffect.empty() || !g_observedPlayer ||
            g_bootstrappedPlayer == g_observedPlayer ||
            g_confirmedBootstrapPlayer != g_observedPlayer) {
            return;
        }
        player = g_observedPlayer;
        bootstrapEffect = g_playerBootstrapEffect;
    }

    if (!IsObservedPlayerInventoryReady()) {
        bool shouldLog = false;
        {
            std::lock_guard<std::mutex> lock(g_listenerMutex);
            if (g_observedPlayer == player && g_deferredBootstrapPlayer != player) {
                g_deferredBootstrapPlayer = player;
                shouldLog = true;
            }
        }
        if (shouldLog) {
            WriteDebugLog(
                "OynonTools",
                "Oynon player bootstrap deferred: five-category inventory is not ready");
        }
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_observedPlayer != player || g_bootstrappedPlayer == player ||
            g_playerBootstrapEffect != bootstrapEffect) {
            return;
        }
        // Reserve before entering the game method. The original method may
        // synchronously cause another observed inventory callback.
        g_bootstrappedPlayer = player;
        g_deferredBootstrapPlayer = nullptr;
    }

    const bool bootstrapApplied = g_originalPlayerApplyEffect
        ? g_originalPlayerApplyEffect(player, bootstrapEffect.c_str(), nullptr)
        : false;
    if (!bootstrapApplied) {
        std::lock_guard<std::mutex> lock(g_listenerMutex);
        if (g_observedPlayer == player && g_bootstrappedPlayer == player) {
            g_bootstrappedPlayer = nullptr;
        }
    }
    WriteDebugLog("OynonTools", bootstrapApplied
        ? "Oynon player bootstrap effect applied after inventory validation"
        : "Oynon player bootstrap effect rejected after inventory validation");
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

    std::uintptr_t target = 0;
    if (!TryResolvePlayerApplyEffectTarget(game, target)) {
        WriteDebugLog("OynonTools", "Oynon player effect hook rejected unexpected Game.exe bytes");
        return false;
    }

    g_playerApplyEffectHook.target = target;
    g_playerApplyEffectHook.detour = reinterpret_cast<void*>(&HookPlayerApplyEffect);
    g_playerApplyEffectHook.patchSize = PLAYER_APPLY_EFFECT_PATCH_SIZE;
    if (!InstallInlineHook(g_playerApplyEffectHook)) {
        WriteDebugLog("OynonTools", "Oynon player effect hook install failed");
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
