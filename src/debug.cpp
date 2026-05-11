#include "debug.h"

#include "inline_hook_utils.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
struct DebugChannel
{
    std::string channelId;
    bool enabled = false;
    std::string logPath;
    std::string consoleCapturePath;
};

std::mutex g_debugMutex;
std::vector<DebugChannel> g_debugChannels;
bool g_debugConsoleOpened = false;
HANDLE g_debugConsoleHandle = nullptr;

void MoveConsoleToRightSide()
{
    HWND consoleWindow = ::GetConsoleWindow();
    if (!consoleWindow) {
        return;
    }

    RECT consoleRect = {};
    if (!::GetWindowRect(consoleWindow, &consoleRect)) {
        return;
    }

    HMONITOR monitor = ::MonitorFromWindow(consoleWindow, MONITOR_DEFAULTTONEAREST);
    MONITORINFO monitorInfo = {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!::GetMonitorInfoA(monitor, &monitorInfo)) {
        return;
    }

    const int width = consoleRect.right - consoleRect.left;
    const int height = consoleRect.bottom - consoleRect.top;
    const int x = monitorInfo.rcWork.right - width;
    const int y = monitorInfo.rcWork.top;

    ::SetWindowPos(
        consoleWindow,
        nullptr,
        x,
        y,
        width,
        height,
        SWP_NOZORDER | SWP_NOACTIVATE);
}

std::size_t TrimTrailingNewlines(char* line)
{
    std::size_t length = std::strlen(line);
    while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r')) {
        --length;
    }

    line[length] = '\0';
    return length;
}

void AppendLineToPath(const char* path, const char* line, std::size_t length)
{
    if (!path || !path[0]) {
        return;
    }

    HANDLE file = ::CreateFileA(
        path,
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    if (length) {
        DWORD bytesWritten = 0;
        ::SetFilePointer(file, 0, nullptr, FILE_END);
        ::WriteFile(file, line, static_cast<DWORD>(length), &bytesWritten, nullptr);
        ::WriteFile(file, "\r\n", 2, &bytesWritten, nullptr);
    }

    ::CloseHandle(file);
}

DebugChannel* FindDebugChannelLocked(const char* channelId)
{
    if (!channelId || !channelId[0]) {
        return nullptr;
    }

    for (DebugChannel& channel : g_debugChannels) {
        if (channel.channelId == channelId) {
            return &channel;
        }
    }

    return nullptr;
}
}

BOOL ConfigureDebugChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath)
{
    if (!channelId || !channelId[0]) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_debugMutex);
    DebugChannel* channel = FindDebugChannelLocked(channelId);
    if (!channel) {
        g_debugChannels.push_back(DebugChannel{});
        channel = &g_debugChannels.back();
        channel->channelId = channelId;
    }

    channel->enabled = enabled != FALSE;
    channel->logPath = logPath ? logPath : "";
    channel->consoleCapturePath = consoleCapturePath ? consoleCapturePath : "";
    return TRUE;
}

void OpenDebugConsole()
{
    std::lock_guard<std::mutex> lock(g_debugMutex);
    if (g_debugConsoleOpened) {
        return;
    }

    bool anyEnabled = false;
    for (const DebugChannel& channel : g_debugChannels) {
        if (channel.enabled) {
            anyEnabled = true;
            break;
        }
    }
    if (!anyEnabled) {
        return;
    }

    if (!::AllocConsole()) {
        return;
    }

    ::SetConsoleTitleA("OynonTools Debug Console");
    MoveConsoleToRightSide();
    g_debugConsoleHandle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    g_debugConsoleOpened = true;
}

void WriteDebugLog(const char* channelId, const char* line)
{
    if (!line) {
        return;
    }

    char buffer[4096] = {};
    std::snprintf(buffer, sizeof(buffer), "[%s] %s", channelId ? channelId : "debug", line);
    const std::size_t length = TrimTrailingNewlines(buffer);

    std::lock_guard<std::mutex> lock(g_debugMutex);
    DebugChannel* channel = FindDebugChannelLocked(channelId);
    if (!channel || !channel->enabled) {
        return;
    }

    if (!channel->logPath.empty()) {
        AppendLineToPath(channel->logPath.c_str(), buffer, length);
    }

    if (g_debugConsoleOpened && g_debugConsoleHandle && g_debugConsoleHandle != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        if (length) {
            ::WriteConsoleA(g_debugConsoleHandle, buffer, static_cast<DWORD>(length), &bytesWritten, nullptr);
        }
        ::WriteConsoleA(g_debugConsoleHandle, "\r\n", 2, &bytesWritten, nullptr);
    }
}

void AppendDebugConsoleCaptureLine(const char* channelId, const char* line)
{
    if (!line) {
        return;
    }

    char buffer[4096] = {};
    std::snprintf(buffer, sizeof(buffer), "[console] %s", line);
    const std::size_t length = TrimTrailingNewlines(buffer);

    std::lock_guard<std::mutex> lock(g_debugMutex);
    DebugChannel* channel = FindDebugChannelLocked(channelId);
    if (!channel || channel->consoleCapturePath.empty()) {
        return;
    }

    AppendLineToPath(channel->consoleCapturePath.c_str(), buffer, length);
}
