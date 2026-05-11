#pragma once

#include "OynonToolsApi.h"

#include <cstddef>
#include <cstdint>

struct InlineHook
{
    std::uintptr_t target = 0;
    void* detour = nullptr;
    std::size_t patchSize = 0;
    BYTE original[16] = {};
    BYTE* trampoline = nullptr;
    bool installed = false;
};

struct ColorValue
{
    float r;
    float g;
    float b;
    float a_pad;
};

bool WriteJump(std::uintptr_t target, std::size_t patchSize, const void* detour);
bool InstallInlineHook(InlineHook& hook);
bool IsFutureTick(DWORD tick, DWORD now);
