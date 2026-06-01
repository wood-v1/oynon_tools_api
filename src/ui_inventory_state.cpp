#include "ui_inventory_state.h"

#include "ui_window_hook.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
constexpr DWORD UI_INVENTORY_CLASSIFY_DELAY_MS = 25;
constexpr const char* VANILLA_INVENTORY_XML = "inventory.xml";
constexpr const char* VANILLA_CONTAINER_XML = "container.xml";
constexpr const char* VANILLA_CORPSE_XML = "corpse.xml";
constexpr const char* VANILLA_APPARATUS_XML = "apparatus.xml";
constexpr const char* VANILLA_DOCTOR_APPARATUS_XML = "dapparatus.xml";

struct InventoryStateListener
{
    OynonInventoryStateCallback callback = nullptr;
    void* userData = nullptr;
};

std::mutex g_inventoryListenerMutex;
std::mutex g_inventoryStateMutex;
std::vector<InventoryStateListener> g_inventoryStateListeners;
void* g_inventoryCandidateStation = nullptr;
void* g_inventoryOverlayStation = nullptr;
DWORD g_inventoryCandidateTick = 0;
std::atomic<bool> g_inventoryOverlayOpen{ false };

void DispatchInventoryState(bool opened)
{
    if (g_inventoryOverlayOpen.exchange(opened) == opened) {
        return;
    }

    std::vector<InventoryStateListener> listeners;
    {
        std::lock_guard<std::mutex> lock(g_inventoryListenerMutex);
        listeners = g_inventoryStateListeners;
    }

    for (const InventoryStateListener& listener : listeners) {
        if (listener.callback) {
            listener.callback(opened ? TRUE : FALSE, listener.userData);
        }
    }
}

bool IsInventoryOverlayXml(const char* xml)
{
    return std::strcmp(xml, VANILLA_INVENTORY_XML) == 0 ||
        std::strcmp(xml, VANILLA_CONTAINER_XML) == 0 ||
        std::strcmp(xml, VANILLA_CORPSE_XML) == 0 ||
        std::strcmp(xml, VANILLA_APPARATUS_XML) == 0 ||
        std::strcmp(xml, VANILLA_DOCTOR_APPARATUS_XML) == 0;
}
}

void ObserveUIInventoryWindow(void* station, const char* xml)
{
    if (!station || !xml) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_inventoryStateMutex);
    if (IsInventoryOverlayXml(xml)) {
        g_inventoryCandidateStation = station;
        g_inventoryCandidateTick = ::GetTickCount();
    }
}

void ObserveUIInventoryStationRemoved(void* station)
{
    bool closedInventoryOverlay = false;
    {
        std::lock_guard<std::mutex> lock(g_inventoryStateMutex);
        if (station == g_inventoryCandidateStation) {
            g_inventoryCandidateStation = nullptr;
            g_inventoryCandidateTick = 0;
        }
        if (station == g_inventoryOverlayStation) {
            g_inventoryOverlayStation = nullptr;
            closedInventoryOverlay = true;
        }
    }

    if (closedInventoryOverlay) {
        DispatchInventoryState(false);
    }
}

BOOL RegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData)
{
    if (!callback) {
        return FALSE;
    }

    std::lock_guard<std::mutex> lock(g_inventoryListenerMutex);
    const auto existing = std::find_if(
        g_inventoryStateListeners.begin(),
        g_inventoryStateListeners.end(),
        [callback, userData](const InventoryStateListener& listener) {
            return listener.callback == callback && listener.userData == userData;
        });
    if (existing != g_inventoryStateListeners.end()) {
        return TRUE;
    }

    g_inventoryStateListeners.push_back(InventoryStateListener{ callback, userData });
    return TRUE;
}

void PollUIInventoryState()
{
    PollUIWindowHook();

    bool openedInventoryOverlay = false;
    {
        std::lock_guard<std::mutex> lock(g_inventoryStateMutex);
        if (!g_inventoryCandidateStation) {
            return;
        }

        const DWORD now = ::GetTickCount();
        if (now - g_inventoryCandidateTick < UI_INVENTORY_CLASSIFY_DELAY_MS) {
            return;
        }

        g_inventoryOverlayStation = g_inventoryCandidateStation;
        openedInventoryOverlay = true;

        g_inventoryCandidateStation = nullptr;
        g_inventoryCandidateTick = 0;
    }

    if (openedInventoryOverlay) {
        DispatchInventoryState(true);
    }
}
