#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerShootingHook();
BOOL SetPlayerShootingBlocked(BOOL blocked);
BOOL RegisterPlayerShootingAttemptCallback(OynonPlayerShootingAttemptCallback callback, void* userData);
