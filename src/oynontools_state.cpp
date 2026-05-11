#include "oynontools_state.h"

#include <cstdint>

namespace
{
DWORD g_engineBase = 0;
DWORD g_requestedHookFlags = 0;

DWORD ResolveEngineBase(DWORD engineBase)
{
    if (engineBase != 0) {
        return engineBase;
    }

    HMODULE engine = ::GetModuleHandleA("Engine.dll");
    return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(engine));
}
}

DWORD ResolveAndStoreEngineBase(DWORD engineBase)
{
    const DWORD resolvedBase = ResolveEngineBase(engineBase);
    if (resolvedBase != 0) {
        g_engineBase = resolvedBase;
    }

    return resolvedBase;
}

DWORD GetStoredEngineBase()
{
    return g_engineBase;
}

void AddRequestedHookFlags(DWORD hookFlags)
{
    g_requestedHookFlags |= hookFlags;
}

DWORD GetRequestedHookFlags()
{
    return g_requestedHookFlags;
}
