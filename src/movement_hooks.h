#pragma once

#include "OynonToolsApi.h"

bool InstallMovementHooks(DWORD engineBase, DWORD hookFlags);
BOOL SetMovementFrictionMultiplier(float frictionMultiplier);
BOOL SetMovementJumpHeightMultiplier(float jumpHeightMultiplier);
BOOL SetMovementLandingGravity(int landingGravity);
