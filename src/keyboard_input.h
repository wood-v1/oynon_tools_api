#pragma once

#include "OynonToolsApi.h"

BOOL RegisterKeyboardCallback(OynonKeyboardCallback callback, void* userData);
void PollKeyboardInput();
