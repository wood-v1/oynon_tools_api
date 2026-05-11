#pragma once

#include "OynonToolsApi.h"

bool TryInstallUIDaychangeHook();
bool IsUIDaychangeHookInstalled();
void PollUIDaychangeHook();
void RequestUIDaychangeRedirect(const char* xml, DWORD ttlMs);
BOOL IsVanillaUIDaychangeActive(DWORD now);
