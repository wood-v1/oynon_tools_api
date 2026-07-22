#pragma once

void SetUIInventoryRedirect(const char* xml);
void SetUILootRedirects(const char* containerXml, const char* corpseXml);
const char* ResolveUIInventoryXml(const char* xml);
bool IsUIInventoryRedirectXml(const char* xml);
