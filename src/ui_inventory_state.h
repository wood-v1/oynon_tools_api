#pragma once

#include "OynonToolsApi.h"

void ObserveUIInventoryWindow(void* station, const char* xml);
void ObserveUIInventoryStationRemoved(void* station);
BOOL RegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData);
void PollUIInventoryState();
