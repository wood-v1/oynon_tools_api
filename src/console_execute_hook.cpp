#include "console_execute_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"
#include "ui_window_hook.h"

#include <algorithm>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace
{
using Execute_t = void(__thiscall*)(void* engine, const char* command);

constexpr DWORD EXECUTE_OFFSET = 0x0003CA60;
constexpr DWORD EXECUTE_HOOK_OFFSET = 0x0003CA7E;
constexpr UINT EXECUTE_QUEUED_COMMAND_MESSAGE = WM_APP + 0x04F1;
constexpr std::size_t MAX_COMMANDS_PER_DISPATCH = 128;

InlineHook g_executeHook;
Execute_t g_execute = nullptr;
void* g_engineInstance = nullptr;
std::mutex g_dispatchMutex;
struct PendingWork
{
    std::string command;
    GameThreadTask task = nullptr;
    void* context = nullptr;
};

std::deque<PendingWork> g_pendingWork;
HWND g_dispatchWindow = nullptr;
DWORD g_dispatchThreadId = 0;
WNDPROC g_originalWindowProc = nullptr;
bool g_dispatchMessagePosted = false;
bool g_dispatchInstallLogged = false;
bool g_firstDispatchLogged = false;
DWORD g_firstProducerThreadId = 0;
thread_local bool g_drainingCommands = false;

struct ConsoleCommandFilter
{
    OynonConsoleCommandFilter filter = nullptr;
    void* userData = nullptr;
};

std::mutex g_filterMutex;
std::vector<ConsoleCommandFilter> g_commandFilters;

struct WindowCandidate
{
    HWND window = nullptr;
    LONG area = 0;
};

BOOL CALLBACK FindGameWindow(HWND window, LPARAM parameter)
{
    DWORD processId = 0;
    ::GetWindowThreadProcessId(window, &processId);
    if (processId != ::GetCurrentProcessId() || ::GetWindow(window, GW_OWNER) != nullptr) {
        return TRUE;
    }

    RECT client = {};
    if (!::GetClientRect(window, &client)) {
        return TRUE;
    }

    const LONG width = client.right - client.left;
    const LONG height = client.bottom - client.top;
    const LONG area = width > 0 && height > 0 ? width * height : 0;
    WindowCandidate* candidate = reinterpret_cast<WindowCandidate*>(parameter);
    if (candidate && area > candidate->area) {
        candidate->window = window;
        candidate->area = area;
    }
    return TRUE;
}

void PostQueuedCommandMessageLocked()
{
    if (g_dispatchMessagePosted || !g_dispatchWindow || g_pendingWork.empty()) {
        return;
    }

    if (::PostMessageA(g_dispatchWindow, EXECUTE_QUEUED_COMMAND_MESSAGE, 0, 0)) {
        g_dispatchMessagePosted = true;
    }
}

void DrainQueuedCommands()
{
    std::deque<PendingWork> work;
    DWORD producerThreadId = 0;
    bool logDispatch = false;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        const std::size_t count =
            (std::min)(g_pendingWork.size(), MAX_COMMANDS_PER_DISPATCH);
        for (std::size_t index = 0; index < count; ++index) {
            work.push_back(std::move(g_pendingWork.front()));
            g_pendingWork.pop_front();
        }
        // The posted message is now being handled. A follow-up is posted below
        // if producers added more work than this bounded dispatch can process.
        g_dispatchMessagePosted = false;
        if (!g_firstDispatchLogged && !work.empty()) {
            g_firstDispatchLogged = true;
            producerThreadId = g_firstProducerThreadId;
            logDispatch = true;
        }
    }

    if (logDispatch) {
        char line[160] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Oynon console dispatcher first drain producer=%lu consumer=%lu commands=%lu",
            static_cast<unsigned long>(producerThreadId),
            static_cast<unsigned long>(::GetCurrentThreadId()),
            static_cast<unsigned long>(work.size()));
        WriteDebugLog("OynonTools", line);
    }

    g_drainingCommands = true;
    for (const PendingWork& pending : work) {
        if (pending.task) {
            pending.task(pending.context);
        }
        else if (g_execute && g_engineInstance && !pending.command.empty()) {
            g_execute(g_engineInstance, pending.command.c_str());
        }
    }
    g_drainingCommands = false;

    std::lock_guard<std::mutex> lock(g_dispatchMutex);
    PostQueuedCommandMessageLocked();
}

LRESULT CALLBACK CommandDispatcherWindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    if (message == EXECUTE_QUEUED_COMMAND_MESSAGE) {
        DrainQueuedCommands();
        return 0;
    }

    WNDPROC original = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        original = g_originalWindowProc;
        if (message == WM_NCDESTROY && window == g_dispatchWindow) {
            g_dispatchWindow = nullptr;
            g_dispatchThreadId = 0;
            g_originalWindowProc = nullptr;
            g_dispatchMessagePosted = false;
        }
    }
    return original
        ? ::CallWindowProcA(original, window, message, wParam, lParam)
        : ::DefWindowProcA(window, message, wParam, lParam);
}

bool EnsureCommandDispatcher()
{
    std::lock_guard<std::mutex> lock(g_dispatchMutex);
    if (g_dispatchWindow && ::IsWindow(g_dispatchWindow) && g_originalWindowProc) {
        return true;
    }

    WindowCandidate candidate;
    ::EnumWindows(&FindGameWindow, reinterpret_cast<LPARAM>(&candidate));
    if (!candidate.window) {
        return false;
    }

    ::SetLastError(ERROR_SUCCESS);
    const LONG_PTR original = ::SetWindowLongPtrA(
        candidate.window,
        GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&CommandDispatcherWindowProc));
    if (original == 0 && ::GetLastError() != ERROR_SUCCESS) {
        return false;
    }

    g_dispatchWindow = candidate.window;
    g_dispatchThreadId = ::GetWindowThreadProcessId(candidate.window, nullptr);
    g_originalWindowProc = reinterpret_cast<WNDPROC>(original);
    PostQueuedCommandMessageLocked();

    if (!g_dispatchInstallLogged) {
        char line[128] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Oynon console dispatcher installed on game thread %lu",
            static_cast<unsigned long>(g_dispatchThreadId));
        WriteDebugLog("OynonTools", line);
        g_dispatchInstallLogged = true;
    }
    return true;
}

bool ShouldSuppressConsoleCommand(const char* command)
{
    if (!command) {
        return false;
    }

    std::vector<ConsoleCommandFilter> filters;
    {
        std::lock_guard<std::mutex> lock(g_filterMutex);
        filters = g_commandFilters;
    }
    for (const ConsoleCommandFilter& entry : filters) {
        if (entry.filter && entry.filter(command, entry.userData)) {
            return true;
        }
    }
    return false;
}

void __fastcall ExecuteDetour(void* engine, void*, const char* command)
{
    g_engineInstance = engine;
    if (!ShouldSuppressConsoleCommand(command) && g_execute) {
        g_execute(engine, command);
    }
}
}

bool InstallConsoleExecuteHook(DWORD engineBase)
{
    if (g_executeHook.installed) {
        return true;
    }

    g_executeHook.target = engineBase + EXECUTE_OFFSET;
    g_executeHook.detour = reinterpret_cast<void*>(&ExecuteDetour);
    g_executeHook.patchSize = 6;
    if (!InstallInlineHook(g_executeHook)) {
        return false;
    }
    g_execute = reinterpret_cast<Execute_t>(g_executeHook.trampoline);
    return true;
}

BOOL RegisterConsoleCommandFilter(OynonConsoleCommandFilter filter, void* userData)
{
    if (!filter) {
        return FALSE;
    }
    std::lock_guard<std::mutex> lock(g_filterMutex);
    const auto existing = std::find_if(
        g_commandFilters.begin(),
        g_commandFilters.end(),
        [filter, userData](const ConsoleCommandFilter& entry) {
            return entry.filter == filter && entry.userData == userData;
        });
    if (existing != g_commandFilters.end()) {
        return TRUE;
    }
    g_commandFilters.push_back({filter, userData});
    return TRUE;
}

BOOL ExecuteConsoleCommand(const char* command)
{
    if (!command || !g_engineInstance || !g_execute) {
        return FALSE;
    }

    EnsureCommandDispatcher();

    DWORD dispatchThreadId = 0;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        dispatchThreadId = g_dispatchThreadId;
    }
    if (dispatchThreadId != 0 &&
        ::GetCurrentThreadId() == dispatchThreadId &&
        !g_drainingCommands) {
        g_execute(g_engineInstance, command);
        return TRUE;
    }

    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        if (g_firstProducerThreadId == 0) {
            g_firstProducerThreadId = ::GetCurrentThreadId();
        }
        PendingWork pending;
        pending.command = command;
        g_pendingWork.emplace_back(std::move(pending));
        PostQueuedCommandMessageLocked();
    }
    return TRUE;
}

BOOL ExecuteConsoleCommandInUIWindowPrepare(const char* command)
{
    if (!command || !g_engineInstance || !g_execute ||
        !IsDispatchingUIWindowPrepare()) {
        return FALSE;
    }

    // A prepare callback runs synchronously inside the engine's CreateWnd
    // call. Executing a small state command here makes it visible to the root
    // script's init(), before an already-disabled external container can stop
    // all subsequent UI updates.
    g_execute(g_engineInstance, command);
    return TRUE;
}

BOOL DispatchGameThreadTask(GameThreadTask task, void* context)
{
    if (!task) {
        return FALSE;
    }

    EnsureCommandDispatcher();

    DWORD dispatchThreadId = 0;
    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        dispatchThreadId = g_dispatchThreadId;
    }
    if (dispatchThreadId != 0 &&
        ::GetCurrentThreadId() == dispatchThreadId &&
        !g_drainingCommands) {
        task(context);
        return TRUE;
    }

    {
        std::lock_guard<std::mutex> lock(g_dispatchMutex);
        if (g_firstProducerThreadId == 0) {
            g_firstProducerThreadId = ::GetCurrentThreadId();
        }
        PendingWork pending;
        pending.task = task;
        pending.context = context;
        g_pendingWork.emplace_back(std::move(pending));
        PostQueuedCommandMessageLocked();
    }
    return TRUE;
}
