#include "console_execute_hook.h"

#include "inline_hook_utils.h"

namespace
{
using Execute_t = void(__thiscall*)(void* engine, const char* command);

constexpr DWORD EXECUTE_OFFSET = 0x0003CA60;
constexpr DWORD EXECUTE_HOOK_OFFSET = 0x0003CA7E;

InlineHook g_executeHook;
Execute_t g_execute = nullptr;
void* g_engineInstance = nullptr;

__declspec(naked) void ExecuteDetour()
{
    __asm {
        mov dword ptr [g_engineInstance], ecx
        jmp dword ptr [g_executeHook.trampoline]
    }
}
}

bool InstallConsoleExecuteHook(DWORD engineBase)
{
    if (g_executeHook.installed) {
        return true;
    }

    g_execute = reinterpret_cast<Execute_t>(engineBase + EXECUTE_OFFSET);
    g_executeHook.target = engineBase + EXECUTE_HOOK_OFFSET;
    g_executeHook.detour = reinterpret_cast<void*>(&ExecuteDetour);
    g_executeHook.patchSize = 6;

    return InstallInlineHook(g_executeHook);
}

BOOL ExecuteConsoleCommand(const char* command)
{
    if (!command || !g_engineInstance || !g_execute) {
        return FALSE;
    }

    g_execute(g_engineInstance, command);
    return TRUE;
}
