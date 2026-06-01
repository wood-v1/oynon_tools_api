#pragma once

#include "OynonToolsApi.h"

void RequestUIDaychangeRedirect(const char* xml, DWORD ttlMs);
BOOL IsVanillaUIDaychangeActive(DWORD now);
const char* ResolveUIDaychangeXml(const char* xml, DWORD now);
