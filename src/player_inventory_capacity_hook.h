#pragma once

#include "OynonToolsApi.h"

bool InstallPlayerInventoryCapacityHook();
BOOL ConfigurePlayerInventoryCategoryCapacity(DWORD capacity);
BOOL ConfigureWorldContainerCapacity(DWORD capacity);
void ResetCapturedPlayerInventoryState();
bool RefreshObservedPlayerInventoryState();
bool IsObservedPlayerInventoryReady();
BOOL SetObservedPlayerHandsItem(int itemId);
BOOL StablePrioritizePlayerInventory(
    const DWORD* priorityItemIds,
    DWORD priorityItemIdCount,
    DWORD* oldToNewIndices,
    DWORD mappingCapacity,
    DWORD* categoryItemCounts,
    DWORD categoryCount,
    BOOL* changed);
