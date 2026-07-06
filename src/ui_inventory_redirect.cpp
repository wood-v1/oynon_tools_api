#include "ui_inventory_redirect.h"

#include "debug.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace
{
constexpr std::size_t MAX_REDIRECT_XML_LEN = 128;
constexpr const char* VANILLA_INVENTORY_XML = "inventory.xml";
constexpr const char* PGOG_DEBUG_CHANNEL = "PGOG";

char g_inventoryRedirectXml[MAX_REDIRECT_XML_LEN] = {};
}

void SetUIInventoryRedirect(const char* xml)
{
    if (!xml || xml[0] == '\0') {
        g_inventoryRedirectXml[0] = '\0';
        WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI inventory redirect cleared");
        return;
    }

    std::strncpy(g_inventoryRedirectXml, xml, sizeof(g_inventoryRedirectXml) - 1);
    g_inventoryRedirectXml[sizeof(g_inventoryRedirectXml) - 1] = '\0';
    WriteDebugLog(PGOG_DEBUG_CHANNEL, "Oynon UI inventory redirect configured");
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
        WriteDebugLog(PGOG_DEBUG_CHANNEL, line.c_str());
        return g_inventoryRedirectXml;
    }

    return xml;
}

bool IsUIInventoryRedirectXml(const char* xml)
{
    if (!xml || g_inventoryRedirectXml[0] == '\0') {
        return false;
    }
    if (std::strcmp(xml, g_inventoryRedirectXml) == 0) {
        return true;
    }

    const char* extension = std::strrchr(g_inventoryRedirectXml, '.');
    if (!extension || std::strcmp(extension, ".xml") != 0) {
        return false;
    }

    const std::size_t baseLen = static_cast<std::size_t>(extension - g_inventoryRedirectXml);
    const std::size_t xmlLen = std::strlen(xml);
    return xmlLen > baseLen + 5 &&
        std::strncmp(xml, g_inventoryRedirectXml, baseLen) == 0 &&
        xml[baseLen] == '_' &&
        std::strcmp(xml + xmlLen - 4, ".xml") == 0;
}
