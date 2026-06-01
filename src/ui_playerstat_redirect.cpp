#include "ui_playerstat_redirect.h"

#include "debug.h"

#include <cstddef>
#include <cstring>

namespace
{
constexpr std::size_t MAX_REDIRECT_XML_LEN = 128;
constexpr const char* VANILLA_PLAYERSTAT_XML = "playerstat.xml";
constexpr const char* PGOG_DEBUG_CHANNEL = "PGOG";

char g_playerstatRedirectXml[MAX_REDIRECT_XML_LEN] = {};
}

void SetUIPlayerstatRedirect(const char* xml)
{
    if (!xml || xml[0] == '\0') {
        g_playerstatRedirectXml[0] = '\0';
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI playerstat redirect cleared");
        return;
    }

    std::strncpy(g_playerstatRedirectXml, xml, sizeof(g_playerstatRedirectXml) - 1);
    g_playerstatRedirectXml[sizeof(g_playerstatRedirectXml) - 1] = '\0';
    WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI playerstat redirect configured");
}

const char* ResolveUIPlayerstatXml(const char* xml)
{
    if (xml &&
        std::strcmp(xml, VANILLA_PLAYERSTAT_XML) == 0 &&
        g_playerstatRedirectXml[0] != '\0') {
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI redirecting playerstat window");
        return g_playerstatRedirectXml;
    }

    return xml;
}
