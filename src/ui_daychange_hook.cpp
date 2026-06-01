#include "ui_daychange_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"

#include <cstring>

namespace
{
constexpr DWORD VANILLA_DAYCHANGE_BUSY_MS = 9000;
constexpr std::size_t MAX_REDIRECT_XML_LEN = 128;
constexpr const char* VANILLA_DAYCHANGE_XML = "daychange.xml";
constexpr const char* PGOG_DEBUG_CHANNEL = "PGOG";

DWORD g_messageRedirectUntil = 0;
DWORD g_vanillaDaychangeBusyUntil = 0;
char g_redirectXml[MAX_REDIRECT_XML_LEN] = {};
}

void RequestUIDaychangeRedirect(const char* xml, DWORD ttlMs)
{
    if (!xml || xml[0] == '\0') {
        return;
    }

    std::strncpy(g_redirectXml, xml, sizeof(g_redirectXml) - 1);
    g_redirectXml[sizeof(g_redirectXml) - 1] = '\0';
    g_messageRedirectUntil = ::GetTickCount() + ttlMs;
    WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirect armed");
}

BOOL IsVanillaUIDaychangeActive(DWORD now)
{
    return IsFutureTick(g_vanillaDaychangeBusyUntil, now) ? TRUE : FALSE;
}

const char* ResolveUIDaychangeXml(const char* xml, DWORD now)
{
    const bool redirectActive =
        IsFutureTick(g_messageRedirectUntil, now) && g_redirectXml[0] != '\0';
    const bool daychangeRequested =
        xml && std::strcmp(xml, VANILLA_DAYCHANGE_XML) == 0;

    if (redirectActive && daychangeRequested) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirecting daychange window");
        g_messageRedirectUntil = 0;
        g_vanillaDaychangeBusyUntil = 0;
        return g_redirectXml;
    }

    if (daychangeRequested) {
        g_vanillaDaychangeBusyUntil = now + VANILLA_DAYCHANGE_BUSY_MS;
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI observed vanilla daychange window");
    }

    if (g_messageRedirectUntil != 0 && !redirectActive) {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirect expired before daychange window creation");
        g_messageRedirectUntil = 0;
        g_redirectXml[0] = '\0';
    }

    return xml;
}
