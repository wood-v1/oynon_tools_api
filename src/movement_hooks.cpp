#include "movement_hooks.h"

#include "inline_hook_utils.h"
#include "oynontools_state.h"

#include <array>
#include <cstring>

namespace
{
constexpr DWORD MOVEMENT_DT1_OFFSET = 0x00103B70;
constexpr DWORD MOVEMENT_DT2_OFFSET = 0x00103B96;
constexpr DWORD MOVEMENT_JUMP_OFFSET = 0x001042EE;
constexpr DWORD MOVEMENT_GRAVITY_CONSTANT_OFFSET = 0x002047DF;
constexpr DWORD MOVEMENT_JUMP_SOURCE_OFFSET = 0x001FC368;

constexpr float DEFAULT_FRICTION_MULTIPLIER = 1.0f;
constexpr float DEFAULT_JUMP_HEIGHT_MULTIPLIER = 1.0f;
constexpr int DEFAULT_LANDING_GRAVITY = -2500;

bool g_movementInstalled = false;
bool g_movementDt1Installed = false;
bool g_movementDt2Installed = false;
bool g_movementVerticalInstalled = false;
DWORD g_movementDt1Address = 0;
DWORD g_movementDt2Address = 0;
DWORD g_movementJumpAddress = 0;
DWORD g_movementDt1Return = 0;
DWORD g_movementDt2Return = 0;
DWORD g_movementJumpReturn = 0;
BYTE* g_movementDt1Cave = nullptr;
BYTE* g_movementDt2Cave = nullptr;
BYTE* g_movementJumpCave = nullptr;
float g_frictionMultiplier = DEFAULT_FRICTION_MULTIPLIER;
float g_jumpHeightMultiplier = DEFAULT_JUMP_HEIGHT_MULTIPLIER;
int g_landingGravity = DEFAULT_LANDING_GRAVITY;

void ApplyLandingGravityValue()
{
    const DWORD engineBase = GetStoredEngineBase();
    if (!engineBase || !g_movementInstalled) {
        return;
    }

    BYTE* instruction = reinterpret_cast<BYTE*>(engineBase + MOVEMENT_GRAVITY_CONSTANT_OFFSET);
    DWORD oldProtect = 0;
    if (!::VirtualProtect(instruction + 1, sizeof(float), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return;
    }

    *reinterpret_cast<float*>(instruction + 1) = static_cast<float>(g_landingGravity);

    DWORD dummy = 0;
    ::VirtualProtect(instruction + 1, sizeof(float), oldProtect, &dummy);
    ::FlushInstructionCache(::GetCurrentProcess(), instruction + 1, sizeof(float));
}

void BuildMovementDt1Cave()
{
    std::array<BYTE, 25> code = {
        0xF3,0x0F,0x10,0x44,0x24,0x50,
        0xF3,0x0F,0x59,0x05,0,0,0,0,
        0xF3,0x0F,0x11,0x44,0x24,0x50,
        0xE9,0,0,0,0
    };

    *reinterpret_cast<DWORD*>(code.data() + 10) =
        static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&g_frictionMultiplier));
    *reinterpret_cast<DWORD*>(code.data() + 21) =
        g_movementDt1Return - (static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(g_movementDt1Cave)) +
            static_cast<DWORD>(code.size()));

    std::memcpy(g_movementDt1Cave, code.data(), code.size());
}

void BuildMovementDt2Cave()
{
    std::array<BYTE, 25> code = {
        0xF3,0x0F,0x10,0x44,0x24,0x58,
        0xF3,0x0F,0x59,0x05,0,0,0,0,
        0xF3,0x0F,0x11,0x44,0x24,0x58,
        0xE9,0,0,0,0
    };

    *reinterpret_cast<DWORD*>(code.data() + 10) =
        static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&g_frictionMultiplier));
    *reinterpret_cast<DWORD*>(code.data() + 21) =
        g_movementDt2Return - (static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(g_movementDt2Cave)) +
            static_cast<DWORD>(code.size()));

    std::memcpy(g_movementDt2Cave, code.data(), code.size());
}

void BuildMovementJumpCave(DWORD engineBase)
{
    std::array<BYTE, 21> code = {
        0xF3,0x0F,0x10,0x1D,0,0,0,0,
        0xF3,0x0F,0x59,0x1D,0,0,0,0,
        0xE9,0,0,0,0
    };

    *reinterpret_cast<DWORD*>(code.data() + 4) = engineBase + MOVEMENT_JUMP_SOURCE_OFFSET;
    *reinterpret_cast<DWORD*>(code.data() + 12) =
        static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&g_jumpHeightMultiplier));
    *reinterpret_cast<DWORD*>(code.data() + 17) =
        g_movementJumpReturn - (static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(g_movementJumpCave)) +
            static_cast<DWORD>(code.size()));

    std::memcpy(g_movementJumpCave, code.data(), code.size());
}
}

bool InstallMovementHooks(DWORD engineBase, DWORD hookFlags)
{
    if (g_movementInstalled || !engineBase) {
        return g_movementInstalled;
    }

    const bool installFriction = (hookFlags & OYNON_HOOK_MOVEMENT_FRICTION) != 0;
    const bool installDt1 = installFriction;
    const bool installDt2 = installFriction;
    const bool installVertical = (hookFlags & OYNON_HOOK_MOVEMENT_VERTICAL) != 0;
    if (!installDt1 && !installDt2 && !installVertical) {
        return true;
    }

    g_movementDt1Address = engineBase + MOVEMENT_DT1_OFFSET;
    g_movementDt2Address = engineBase + MOVEMENT_DT2_OFFSET;
    g_movementJumpAddress = engineBase + MOVEMENT_JUMP_OFFSET;
    g_movementDt1Return = g_movementDt1Address + 6;
    g_movementDt2Return = g_movementDt2Address + 6;
    g_movementJumpReturn = g_movementJumpAddress + 8;

    g_movementDt1Cave = static_cast<BYTE*>(::VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    g_movementDt2Cave = static_cast<BYTE*>(::VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    g_movementJumpCave = static_cast<BYTE*>(::VirtualAlloc(nullptr, 256, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_movementDt1Cave || !g_movementDt2Cave || !g_movementJumpCave) {
        return false;
    }

    BuildMovementDt1Cave();
    BuildMovementDt2Cave();
    BuildMovementJumpCave(engineBase);

    if (installDt1) {
        if (!WriteJump(g_movementDt1Address, 6, g_movementDt1Cave)) {
            return false;
        }
        g_movementDt1Installed = true;
    }

    if (installDt2) {
        if (!WriteJump(g_movementDt2Address, 6, g_movementDt2Cave)) {
            return false;
        }
        g_movementDt2Installed = true;
    }

    if (installVertical) {
        if (!WriteJump(g_movementJumpAddress, 8, g_movementJumpCave)) {
            return false;
        }
        g_movementVerticalInstalled = true;
    }

    g_movementInstalled = g_movementDt1Installed || g_movementDt2Installed || g_movementVerticalInstalled;
    ApplyLandingGravityValue();
    return true;
}

BOOL SetMovementFrictionMultiplier(float frictionMultiplier)
{
    g_frictionMultiplier = frictionMultiplier;
    return TRUE;
}

BOOL SetMovementJumpHeightMultiplier(float jumpHeightMultiplier)
{
    g_jumpHeightMultiplier = jumpHeightMultiplier;
    return TRUE;
}

BOOL SetMovementLandingGravity(int landingGravity)
{
    g_landingGravity = landingGravity;
    ApplyLandingGravityValue();
    return TRUE;
}
