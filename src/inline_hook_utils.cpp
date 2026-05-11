#include "inline_hook_utils.h"

#include <cstring>

bool WriteJump(std::uintptr_t target, std::size_t patchSize, const void* detour)
{
    DWORD oldProtect = 0;
    if (!::VirtualProtect(reinterpret_cast<void*>(target), patchSize, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }

    BYTE* bytes = reinterpret_cast<BYTE*>(target);
    bytes[0] = 0xE9;
    *reinterpret_cast<DWORD*>(bytes + 1) = static_cast<DWORD>(
        reinterpret_cast<std::uintptr_t>(detour) - target - 5);

    for (std::size_t index = 5; index < patchSize; ++index) {
        bytes[index] = 0x90;
    }

    DWORD dummy = 0;
    ::VirtualProtect(reinterpret_cast<void*>(target), patchSize, oldProtect, &dummy);
    ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<void*>(target), patchSize);
    return true;
}

bool InstallInlineHook(InlineHook& hook)
{
    if (!hook.target || !hook.detour || hook.patchSize < 5 || hook.patchSize > sizeof(hook.original)) {
        return false;
    }

    std::memcpy(hook.original, reinterpret_cast<void*>(hook.target), hook.patchSize);

    BYTE* trampoline = static_cast<BYTE*>(::VirtualAlloc(
        nullptr,
        hook.patchSize + 5,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        return false;
    }

    std::memcpy(trampoline, hook.original, hook.patchSize);
    const DWORD jumpBackSource = static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(trampoline)) +
        static_cast<DWORD>(hook.patchSize);
    trampoline[hook.patchSize] = 0xE9;
    *reinterpret_cast<DWORD*>(trampoline + hook.patchSize + 1) =
        static_cast<DWORD>(hook.target + hook.patchSize) - jumpBackSource - 5;

    if (!WriteJump(hook.target, hook.patchSize, hook.detour)) {
        ::VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    hook.trampoline = trampoline;
    hook.installed = true;
    return true;
}

bool IsFutureTick(DWORD tick, DWORD now)
{
    return tick != 0 && (LONG)(tick - now) > 0;
}
