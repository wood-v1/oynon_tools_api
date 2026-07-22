#include "ui_inventory_redirect.h"

#include "debug.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace
{
constexpr std::size_t MAX_REDIRECT_XML_LEN = 128;
constexpr const char* VANILLA_INVENTORY_XML = "inventory.xml";
constexpr const char* VANILLA_CONTAINER_XML = "container.xml";
constexpr const char* VANILLA_CORPSE_XML = "corpse.xml";
constexpr const char* OYNONTOOLS_DEBUG_CHANNEL = "OynonTools";

char g_inventoryRedirectXml[MAX_REDIRECT_XML_LEN] = {};
char g_containerRedirectXml[MAX_REDIRECT_XML_LEN] = {};
char g_corpseRedirectXml[MAX_REDIRECT_XML_LEN] = {};

void CopyRedirect(char* destination, std::size_t destinationSize, const char* xml)
{
    if (!xml || xml[0] == '\0') {
        destination[0] = '\0';
        return;
    }

    std::strncpy(destination, xml, destinationSize - 1);
    destination[destinationSize - 1] = '\0';
}

bool MatchesRedirectXml(const char* xml, const char* redirectXml)
{
    if (!xml || !redirectXml || redirectXml[0] == '\0') {
        return false;
    }
    if (std::strcmp(xml, redirectXml) == 0) {
        return true;
    }

    const char* extension = std::strrchr(redirectXml, '.');
    if (!extension || std::strcmp(extension, ".xml") != 0) {
        return false;
    }

    const std::size_t baseLen = static_cast<std::size_t>(extension - redirectXml);
    const std::size_t xmlLen = std::strlen(xml);
    return xmlLen > baseLen + 5 &&
        std::strncmp(xml, redirectXml, baseLen) == 0 &&
        xml[baseLen] == '_' &&
        std::strcmp(xml + xmlLen - 4, ".xml") == 0;
}
}

void SetUIInventoryRedirect(const char* xml)
{
    if (!xml || xml[0] == '\0') {
        g_inventoryRedirectXml[0] = '\0';
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI inventory redirect cleared");
        return;
    }

    CopyRedirect(g_inventoryRedirectXml, sizeof(g_inventoryRedirectXml), xml);
    WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI inventory redirect configured");
}

void SetUILootRedirects(const char* containerXml, const char* corpseXml)
{
    CopyRedirect(g_containerRedirectXml, sizeof(g_containerRedirectXml), containerXml);
    CopyRedirect(g_corpseRedirectXml, sizeof(g_corpseRedirectXml), corpseXml);
    WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI loot redirects configured");
}

const char* ResolveUIInventoryXml(const char* xml)
{
    if (xml &&
        std::strcmp(xml, VANILLA_INVENTORY_XML) == 0 &&
        g_inventoryRedirectXml[0] != '\0') {
        std::string line = "Oynon UI redirecting inventory window: ";
        line += xml;
        line += " -> ";
        line += g_inventoryRedirectXml;
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, line.c_str());
        return g_inventoryRedirectXml;
    }

    if (xml &&
        std::strcmp(xml, VANILLA_CONTAINER_XML) == 0 &&
        g_containerRedirectXml[0] != '\0') {
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI redirecting container window");
        return g_containerRedirectXml;
    }

    if (xml &&
        std::strcmp(xml, VANILLA_CORPSE_XML) == 0 &&
        g_corpseRedirectXml[0] != '\0') {
        WriteDebugLog(OYNONTOOLS_DEBUG_CHANNEL, "Oynon UI redirecting corpse window");
        return g_corpseRedirectXml;
    }

    return xml;
}

bool IsUIInventoryRedirectXml(const char* xml)
{
    return MatchesRedirectXml(xml, g_inventoryRedirectXml) ||
        MatchesRedirectXml(xml, g_containerRedirectXml) ||
        MatchesRedirectXml(xml, g_corpseRedirectXml);
}
