#include "player_inventory_capacity_hook.h"

#include "debug.h"
#include "console_execute_hook.h"
#include "inline_hook_utils.h"
#include "player_effect_hook.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>

namespace
{
constexpr const char* ADD_ITEM_NAME = "AddItem";
constexpr const char* GET_PLAYER_CONTAINER_NAME = "GetPlayerContainer";
constexpr std::size_t ADD_ITEM_SCAN_SIZE = 0x500;
constexpr std::size_t ADD_ITEM_PATCH_SIZE = 6;
constexpr std::size_t GET_PLAYER_CONTAINER_PATCH_SIZE = 5;
constexpr DWORD VANILLA_CATEGORY_CAPACITY = 12;
constexpr DWORD MAX_CATEGORY_CAPACITY = 64;
constexpr DWORD MAX_WORLD_CONTAINER_CAPACITY = 128;
constexpr DWORD PLAYER_CONTAINER_HASH = 0x0C06562A;
constexpr unsigned int PLAYER_CATEGORY_COUNT = 5;
constexpr unsigned int PLAYER_CATEGORY_MAPPING_STRIDE = 64;
constexpr BYTE CAPACITY_CHECK_PATTERN[] = {
    0x85, 0xFF, 0x0F, 0x95, 0xC1, 0x03, 0xCB, 0x3B, 0xCA, 0x76
};
constexpr std::size_t CATEGORY_CAPACITY_FIELD_OFFSET = 0x08;

using ScriptAddItem_t = bool(__thiscall*)(void* self, void* arguments, void* result, unsigned int parameterCount);
using ScriptGetPlayerContainer_t = bool(__thiscall*)(void* self, void* arguments, void* result, unsigned int parameterCount);
using GetSubContainerCount_t = unsigned int(__thiscall*)(void* self);
using GetSubContainer_t = void*(__thiscall*)(void* self, unsigned int category);
using LookupObject_t = void*(__thiscall*)(void* self, DWORD hash);
using SetPlayerHandsItem_t = void(__thiscall*)(void* self, int itemId);
using GetCategoryCapacity_t = unsigned int(__thiscall*)(void* self);
using DirectPlayerAddItem_t = bool(__thiscall*)(
    void* self,
    void* argument0,
    void* argument1,
    void* argument2,
    void* argument3,
    void* argument4);
using GetCategoryItemCount_t = unsigned int(__thiscall*)(void* self);

struct NativeInventoryItem
{
    DWORD itemId = 0;
    void* definition = nullptr;
};

using GetCategoryItem_t = void(__thiscall*)(
    void* self,
    NativeInventoryItem* item,
    unsigned int index,
    unsigned int* amount);
using SetCategoryItem_t = void(__thiscall*)(
    void* self,
    unsigned int index,
    const NativeInventoryItem* item,
    unsigned int amount);
using RetainInterface_t = void*(__thiscall*)(void* self);
using ReleaseInterface_t = void(__thiscall*)(void* self);

struct StableInventoryEntry
{
    NativeInventoryItem item;
    unsigned int amount = 0;
    unsigned int sourceIndex = 0;
};

struct CapacityVtablePatch
{
    void** slot = nullptr;
    GetCategoryCapacity_t original = nullptr;
};

InlineHook g_addItemHook;
InlineHook g_getPlayerContainerHook;
ScriptAddItem_t g_originalAddItem = nullptr;
ScriptGetPlayerContainer_t g_originalGetPlayerContainer = nullptr;
BYTE* g_capacityBranch = nullptr;
DWORD g_playerCategoryCapacity = MAX_CATEGORY_CAPACITY;
DWORD g_worldContainerCapacity = VANILLA_CATEGORY_CAPACITY;
void* g_playerContainer = nullptr;
void* g_playerInventory = nullptr;
void* g_playerNativeContext = nullptr;
std::array<void*, PLAYER_CATEGORY_COUNT> g_playerCategories = {};
std::array<CapacityVtablePatch, PLAYER_CATEGORY_COUNT> g_capacityVtablePatches = {};
unsigned int g_capacityVtablePatchCount = 0;
void** g_directPlayerAddItemSlot = nullptr;
DirectPlayerAddItem_t g_originalDirectPlayerAddItem = nullptr;
thread_local unsigned int g_playerCapacityOverrideDepth = 0;
thread_local unsigned int g_playerCapacityOverrideCalls = 0;
thread_local bool g_suppressCaptureDiagnostics = false;

bool SetBranchOpcode(BYTE opcode)
{
    if (!g_capacityBranch) {
        return false;
    }
    DWORD oldProtect = 0;
    if (!::VirtualProtect(g_capacityBranch, 1, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    *g_capacityBranch = opcode;
    DWORD dummy = 0;
    ::VirtualProtect(g_capacityBranch, 1, oldProtect, &dummy);
    ::FlushInstructionCache(::GetCurrentProcess(), g_capacityBranch, 1);
    return true;
}

unsigned int GetSubContainerCount(void* self)
{
    if (!self) {
        return 0;
    }
    void** vtable = *reinterpret_cast<void***>(self);
    if (!vtable || !vtable[2]) {
        return 0;
    }
    const auto getSubContainerCount = reinterpret_cast<GetSubContainerCount_t>(vtable[2]);
    return getSubContainerCount(self);
}

bool IsPlayerInventory(void* self, unsigned int subContainerCount, void* observedPlayer)
{
    if (self == g_playerInventory || self == g_playerContainer || self == observedPlayer) {
        return true;
    }
    return subContainerCount == 5;
}

bool IsPlayerCategory(void* self)
{
    for (void* category : g_playerCategories) {
        if (category && category == self) {
            return true;
        }
    }
    return false;
}

bool IsReadableMemory(const void* address, std::size_t size)
{
    if (!address || size == 0) {
        return false;
    }
    MEMORY_BASIC_INFORMATION memory = {};
    if (::VirtualQuery(address, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) ||
        memory.Protect == PAGE_NOACCESS) {
        return false;
    }
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address);
    const std::uintptr_t end = begin + size;
    const std::uintptr_t regionEnd =
        reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return end >= begin && end <= regionEnd;
}

template <std::size_t Size>
bool MatchesCode(const void* address, const std::array<BYTE, Size>& expected)
{
    const BYTE* code = static_cast<const BYTE*>(address);
    for (unsigned int depth = 0; depth < 3; ++depth) {
        if (!IsReadableMemory(code, expected.size())) {
            return false;
        }
        if (std::memcmp(code, expected.data(), expected.size()) == 0) {
            return true;
        }
        if (code[0] != 0xE9 || !IsReadableMemory(code, 5)) {
            return false;
        }
        const std::int32_t relative = *reinterpret_cast<const std::int32_t*>(code + 1);
        code = code + 5 + relative;
    }
    return false;
}

bool ValidateCategoryReorderMethods(
    void* category,
    GetCategoryItemCount_t& getCount,
    GetCategoryItem_t& getItem,
    SetCategoryItem_t& setItem)
{
    constexpr std::array<BYTE, 10> GET_COUNT_EXPECTED = {
        0x8B, 0x41, 0x10, 0x2B, 0x41, 0x0C, 0xC1, 0xF8, 0x05, 0xC3
    };
    constexpr std::array<BYTE, 8> GET_ITEM_EXPECTED = {
        0x8B, 0xD1, 0x8B, 0x4C, 0x24, 0x08, 0x8B, 0x42
    };
    constexpr std::array<BYTE, 12> SET_ITEM_EXPECTED = {
        0x55, 0x8B, 0x6C, 0x24, 0x10, 0x56,
        0x8B, 0x74, 0x24, 0x0C, 0x57, 0x8B
    };

    if (!IsReadableMemory(category, sizeof(void*))) {
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(category);
    if (!IsReadableMemory(vtable, sizeof(void*) * 13)) {
        return false;
    }

    getCount = reinterpret_cast<GetCategoryItemCount_t>(vtable[5]);
    getItem = reinterpret_cast<GetCategoryItem_t>(vtable[6]);
    setItem = reinterpret_cast<SetCategoryItem_t>(vtable[12]);
    return MatchesCode(reinterpret_cast<const void*>(getCount), GET_COUNT_EXPECTED) &&
        MatchesCode(reinterpret_cast<const void*>(getItem), GET_ITEM_EXPECTED) &&
        MatchesCode(reinterpret_cast<const void*>(setItem), SET_ITEM_EXPECTED);
}

bool IsCategoryStorageReady(void* category)
{
    GetCategoryItemCount_t getCount = nullptr;
    GetCategoryItem_t getItem = nullptr;
    SetCategoryItem_t setItem = nullptr;
    if (!ValidateCategoryReorderMethods(category, getCount, getItem, setItem) ||
        !IsReadableMemory(category, 0x14)) {
        return false;
    }

    const DWORD capacity = *reinterpret_cast<DWORD*>(
        static_cast<BYTE*>(category) + CATEGORY_CAPACITY_FIELD_OFFSET);
    if (capacity < VANILLA_CATEGORY_CAPACITY || capacity > MAX_WORLD_CONTAINER_CAPACITY) {
        return false;
    }

    BYTE* begin = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(category) + 0x0C);
    BYTE* end = *reinterpret_cast<BYTE**>(static_cast<BYTE*>(category) + 0x10);
    if (!begin && !end) {
        return true;
    }
    if (!begin || !end || end < begin) {
        return false;
    }

    const std::uintptr_t byteCount =
        reinterpret_cast<std::uintptr_t>(end) - reinterpret_cast<std::uintptr_t>(begin);
    if ((byteCount & 0x1F) != 0 ||
        byteCount > static_cast<std::uintptr_t>(MAX_WORLD_CONTAINER_CAPACITY) * 0x20) {
        return false;
    }
    return byteCount == 0 || IsReadableMemory(begin, static_cast<std::size_t>(byteCount));
}

bool ArePlayerCategoriesReady(
    const std::array<void*, PLAYER_CATEGORY_COUNT>& categories)
{
    for (void* category : categories) {
        if (!IsCategoryStorageReady(category)) {
            return false;
        }
    }
    return true;
}

bool IsPlayerInventoryManager(void* candidate)
{
    constexpr std::array<BYTE, 13> GET_SUBCONTAINER_EXPECTED = {
        0x8B, 0x49, 0xFC, 0x8B, 0x44, 0x24, 0x04,
        0x8B, 0x04, 0x81, 0xC2, 0x04, 0x00
    };
    if (!IsReadableMemory(candidate, sizeof(void*))) {
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(candidate);
    return IsReadableMemory(vtable, sizeof(void*) * 4) &&
        MatchesCode(vtable[3], GET_SUBCONTAINER_EXPECTED);
}

void* RetainInventoryDefinition(void* definition)
{
    if (!definition || !IsReadableMemory(definition, sizeof(void*))) {
        return definition;
    }
    void** vtable = *reinterpret_cast<void***>(definition);
    if (!IsReadableMemory(vtable, sizeof(void*) * 11) || !vtable[10]) {
        return nullptr;
    }
    return reinterpret_cast<RetainInterface_t>(vtable[10])(definition);
}

void ReleaseInventoryDefinition(void* definition)
{
    if (!definition || !IsReadableMemory(definition, sizeof(void*))) {
        return;
    }
    void** vtable = *reinterpret_cast<void***>(definition);
    if (IsReadableMemory(vtable, sizeof(void*) * 2) && vtable[1]) {
        reinterpret_cast<ReleaseInterface_t>(vtable[1])(definition);
    }
}

bool IsPriorityItemId(DWORD itemId, const DWORD* priorityItemIds, DWORD priorityItemIdCount)
{
    for (DWORD index = 0; index < priorityItemIdCount; ++index) {
        if (priorityItemIds[index] == itemId) {
            return true;
        }
    }
    return false;
}

unsigned int __fastcall HookCategoryCapacity(void* self, void*)
{
    if (g_playerCapacityOverrideDepth > 0 || IsPlayerCategory(self)) {
        if (g_playerCapacityOverrideDepth > 0) {
            ++g_playerCapacityOverrideCalls;
        }
        return g_playerCategoryCapacity;
    }
    if (IsReadableMemory(self, sizeof(void*))) {
        void** vtable = *reinterpret_cast<void***>(self);
        if (IsReadableMemory(vtable, sizeof(void*) * 4)) {
            void** slot = vtable + 3;
            for (unsigned int index = 0; index < g_capacityVtablePatchCount; ++index) {
                if (g_capacityVtablePatches[index].slot == slot &&
                    g_capacityVtablePatches[index].original) {
                    return g_capacityVtablePatches[index].original(self);
                }
            }
        }
    }
    return VANILLA_CATEGORY_CAPACITY;
}

bool SetPlayerCategoryCapacityField(void* category)
{
    BYTE* capacityAddress = static_cast<BYTE*>(category) + CATEGORY_CAPACITY_FIELD_OFFSET;
    if (!IsReadableMemory(capacityAddress, sizeof(DWORD))) {
        return false;
    }
    *reinterpret_cast<DWORD*>(capacityAddress) = g_playerCategoryCapacity;
    return *reinterpret_cast<DWORD*>(capacityAddress) == g_playerCategoryCapacity;
}

bool PatchCapacityVtable(void* category)
{
    if (!IsReadableMemory(category, sizeof(void*))) {
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(category);
    if (!IsReadableMemory(vtable, sizeof(void*) * 4) || !vtable[3]) {
        return false;
    }
    void** slot = vtable + 3;
    for (unsigned int index = 0; index < g_capacityVtablePatchCount; ++index) {
        if (g_capacityVtablePatches[index].slot == slot) {
            return true;
        }
    }
    if (g_capacityVtablePatchCount >= g_capacityVtablePatches.size()) {
        return false;
    }

    const auto original = reinterpret_cast<GetCategoryCapacity_t>(*slot);
    DWORD oldProtect = 0;
    if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    *slot = reinterpret_cast<void*>(&HookCategoryCapacity);
    DWORD dummy = 0;
    ::VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
    ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));

    g_capacityVtablePatches[g_capacityVtablePatchCount++] = { slot, original };
    return true;
}

void CapturePlayerCategoriesFromInventory(void* playerInventory);

bool __fastcall HookDirectPlayerAddItem(
    void* self,
    void*,
    void* argument0,
    void* argument1,
    void* argument2,
    void* argument3,
    void* argument4)
{
    const bool playerInventory = self && self == g_playerContainer;
    if (playerInventory) {
        // CapturePlayerCategories installs this vtable hook only after the
        // current player categories have already been validated.  This shared
        // virtual method is also queried continuously by the UI, so repeating
        // the five-category scan here turns a cheap call into thousands of
        // VirtualQuery calls per second and permanently halves the frame rate.
        ++g_playerCapacityOverrideDepth;
        g_playerCapacityOverrideCalls = 0;
    }

    const bool added = g_originalDirectPlayerAddItem
        ? g_originalDirectPlayerAddItem(
            self,
            argument0,
            argument1,
            argument2,
            argument3,
            argument4)
        : false;

    if (playerInventory) {
        const unsigned int capacityCalls = g_playerCapacityOverrideCalls;
        --g_playerCapacityOverrideDepth;
        if (!added) {
            char message[160] = {};
            std::snprintf(
                message,
                sizeof(message),
                "Oynon direct player AddItem failed capacity-calls=%u",
                capacityCalls);
            WriteDebugLog("OynonTools", message);
        }
    }
    return added;
}

bool PatchDirectPlayerAddItemVtable(void* playerContainer)
{
    if (!IsReadableMemory(playerContainer, sizeof(void*))) {
        return false;
    }
    void** vtable = *reinterpret_cast<void***>(playerContainer);
    if (!IsReadableMemory(vtable, sizeof(void*) * 5) || !vtable[4]) {
        return false;
    }
    void** slot = vtable + 4;
    if (g_directPlayerAddItemSlot == slot) {
        return true;
    }
    if (g_directPlayerAddItemSlot) {
        WriteDebugLog("OynonTools", "Oynon direct player AddItem vtable changed unexpectedly");
        return false;
    }

    const auto original = reinterpret_cast<DirectPlayerAddItem_t>(*slot);
    DWORD oldProtect = 0;
    if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return false;
    }
    *slot = reinterpret_cast<void*>(&HookDirectPlayerAddItem);
    DWORD dummy = 0;
    ::VirtualProtect(slot, sizeof(void*), oldProtect, &dummy);
    ::FlushInstructionCache(::GetCurrentProcess(), slot, sizeof(void*));

    g_directPlayerAddItemSlot = slot;
    g_originalDirectPlayerAddItem = original;
    WriteDebugLog("OynonTools", "Oynon direct player AddItem vtable hook installed");
    return true;
}

void CapturePlayerCategories(void* playerContainer)
{
    if (!playerContainer) {
        return;
    }
    // Game loads can reuse the same embedded player-container address while
    // replacing the five category objects. Always refresh the category array;
    // retaining the old pointers disables stable sorting after loading a save.
    // GetPlayerContainer returns the interface embedded at player+0x60.
    // The category manager used by Game.exe::AddItem is at player+0x3C and
    // its category pointer array is stored at player+0x38.  Therefore the
    // array pointer is exactly 0x28 bytes before this interface.  Reading the
    // array mirrors Game.exe 0x4D3040 without invoking an unrelated virtual
    // method on the GetPlayerContainer interface.
    BYTE* categoryArrayField = static_cast<BYTE*>(playerContainer) - 0x28;
    if (!IsReadableMemory(categoryArrayField, sizeof(void*))) {
        WriteDebugLog("OynonTools", "Oynon player category array field is unreadable");
        return;
    }
    void** categoryArray = *reinterpret_cast<void***>(categoryArrayField);
    if (!IsReadableMemory(categoryArray, sizeof(void*) * PLAYER_CATEGORY_COUNT)) {
        WriteDebugLog("OynonTools", "Oynon player category array is unreadable");
        return;
    }
    std::array<void*, PLAYER_CATEGORY_COUNT> categories = {};
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        categories[category] = categoryArray[category];
        if (!IsReadableMemory(categories[category], sizeof(void*))) {
            WriteDebugLog("OynonTools", "Oynon player category container lookup failed");
            return;
        }
    }
    if (!ArePlayerCategoriesReady(categories)) {
        WriteDebugLog("OynonTools", "Oynon player categories are present but not initialized");
        return;
    }

    bool categoriesChanged = playerContainer != g_playerContainer;
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        if (categories[category] != g_playerCategories[category]) {
            categoriesChanged = true;
            break;
        }
    }
    if (!categoriesChanged) {
        PatchDirectPlayerAddItemVtable(playerContainer);
        return;
    }

    g_playerContainer = playerContainer;
    g_playerInventory = static_cast<BYTE*>(playerContainer) - 0x24;
    PatchDirectPlayerAddItemVtable(playerContainer);
    g_playerCategories = categories;
    bool patched = true;
    for (void* category : g_playerCategories) {
        patched = SetPlayerCategoryCapacityField(category) &&
            PatchCapacityVtable(category) && patched;
    }

    char message[192] = {};
    std::snprintf(
        message,
        sizeof(message),
        "Oynon player container=%p categories captured=%u vtables=%u",
        g_playerContainer,
        patched ? PLAYER_CATEGORY_COUNT : 0u,
        g_capacityVtablePatchCount);
    WriteDebugLog("OynonTools", message);
}

void CapturePlayerCategoriesFromInventory(void* playerInventory)
{
    if (!playerInventory || !IsReadableMemory(playerInventory, sizeof(void*))) {
        return;
    }
    void** vtable = *reinterpret_cast<void***>(playerInventory);
    if (!IsReadableMemory(vtable, sizeof(void*) * 4) || !vtable[3]) {
        if (!g_suppressCaptureDiagnostics) {
            WriteDebugLog("OynonTools", "Oynon player inventory category getter is unreadable");
        }
        return;
    }

    const auto getSubContainer = reinterpret_cast<GetSubContainer_t>(vtable[3]);
    std::array<void*, PLAYER_CATEGORY_COUNT> categories = {};
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        categories[category] = getSubContainer(playerInventory, category);
        if (!IsReadableMemory(categories[category], sizeof(void*))) {
            if (!g_suppressCaptureDiagnostics) {
                WriteDebugLog("OynonTools", "Oynon direct player category lookup failed");
            }
            return;
        }
    }
    if (!ArePlayerCategoriesReady(categories)) {
        if (!g_suppressCaptureDiagnostics) {
            WriteDebugLog("OynonTools", "Oynon direct player categories are not initialized");
        }
        return;
    }

    const bool firstCapture = g_playerInventory != playerInventory;
    bool categoriesChanged = firstCapture;
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        if (g_playerCategories[category] != categories[category]) {
            categoriesChanged = true;
        }
    }
    g_playerInventory = playerInventory;
    g_playerCategories = categories;
    bool patched = true;
    for (void* category : g_playerCategories) {
        patched = SetPlayerCategoryCapacityField(category) &&
            PatchCapacityVtable(category) && patched;
    }

    if (categoriesChanged) {
        char message[224] = {};
        std::snprintf(
            message,
            sizeof(message),
            "Oynon AddItem player inventory=%p interface=%p categories refreshed=%u vtables=%u",
            g_playerInventory,
            g_playerContainer,
            patched ? PLAYER_CATEGORY_COUNT : 0u,
            g_capacityVtablePatchCount);
        WriteDebugLog("OynonTools", message);
    }
}

bool CapturePlayerCategoriesFromObservedPlayer()
{
    void* observedPlayer = GetObservedPlayerObject();
    if (!observedPlayer) {
        return false;
    }

    // PlayerApplyEffect is dispatched by a secondary player interface rather
    // than the complete object. Locate the embedded category manager by its
    // verified GetSubContainer method instead of assuming one executable-
    // specific interface offset.
    constexpr int SEARCH_BEFORE = 0x80;
    constexpr int SEARCH_AFTER = 0x80;
    BYTE* observed = static_cast<BYTE*>(observedPlayer);
    for (int offset = -SEARCH_BEFORE; offset <= SEARCH_AFTER; offset += 4) {
        void* candidate = observed + offset;
        if (!IsPlayerInventoryManager(candidate)) {
            continue;
        }

        const bool previousSuppression = g_suppressCaptureDiagnostics;
        g_suppressCaptureDiagnostics = true;
        CapturePlayerCategoriesFromInventory(candidate);
        g_suppressCaptureDiagnostics = previousSuppression;
        if (g_playerInventory == candidate) {
            char message[192] = {};
            std::snprintf(
                message,
                sizeof(message),
                "Oynon player categories captured from bootstrap interface offset=%d",
                offset);
            WriteDebugLog("OynonTools", message);
            return true;
        }
    }
    return false;
}

void CapturePlayerContainerFromNative(void* self)
{
    if (!IsReadableMemory(self, 0x10)) {
        return;
    }
    void* context = *reinterpret_cast<void**>(static_cast<BYTE*>(self) + 0x0C);
    if (!IsReadableMemory(context, sizeof(void*))) {
        return;
    }
    g_playerNativeContext = context;
    void** vtable = *reinterpret_cast<void***>(context);
    if (!IsReadableMemory(vtable, sizeof(void*) * 3) || !vtable[2]) {
        return;
    }
    const auto lookup = reinterpret_cast<LookupObject_t>(vtable[2]);
    CapturePlayerCategories(lookup(context, PLAYER_CONTAINER_HASH));
}

bool __fastcall HookScriptGetPlayerContainer(
    void* self,
    void*,
    void* arguments,
    void* result,
    unsigned int parameterCount)
{
    CapturePlayerContainerFromNative(self);
    const bool resultValue = g_originalGetPlayerContainer
        ? g_originalGetPlayerContainer(self, arguments, result, parameterCount)
        : false;
    TryApplyPendingPlayerBootstrap();
    return resultValue;
}

bool __fastcall HookScriptAddItem(
    void* self,
    void*,
    void* arguments,
    void* result,
    unsigned int parameterCount)
{
    void* observedPlayer = GetObservedPlayerObject();
    const unsigned int subContainerCount = GetSubContainerCount(self);
    const bool playerInventory = IsPlayerInventory(self, subContainerCount, observedPlayer);
    if (playerInventory) {
        CapturePlayerCategoriesFromInventory(self);
    }
    const DWORD requestedCapacity = playerInventory
        ? g_playerCategoryCapacity
        : g_worldContainerCapacity;
    bool branchPatched = false;
    if (requestedCapacity > VANILLA_CATEGORY_CAPACITY &&
        g_capacityBranch && *g_capacityBranch == 0x76) {
        branchPatched = SetBranchOpcode(0xEB);
    }
    if (requestedCapacity > VANILLA_CATEGORY_CAPACITY && !branchPatched) {
        WriteDebugLog(
            "OynonTools",
            playerInventory
                ? "Oynon player AddItem capacity branch was not patched"
                : "Oynon world-container AddItem capacity branch was not patched");
    }

    if (playerInventory) {
        ++g_playerCapacityOverrideDepth;
        g_playerCapacityOverrideCalls = 0;
    }
    const bool added = g_originalAddItem
        ? g_originalAddItem(self, arguments, result, parameterCount)
        : false;
    if (playerInventory) {
        --g_playerCapacityOverrideDepth;
    }

    if (branchPatched) {
        SetBranchOpcode(0x76);
    }
    if (playerInventory) {
        TryApplyPendingPlayerBootstrap();
    }
    return added;
}

BYTE* FindBytes(BYTE* begin, std::size_t length, const BYTE* pattern, std::size_t patternLength)
{
    if (!begin || patternLength == 0 || length < patternLength) {
        return nullptr;
    }
    for (std::size_t offset = 0; offset <= length - patternLength; ++offset) {
        if (std::memcmp(begin + offset, pattern, patternLength) == 0) {
            return begin + offset;
        }
    }
    return nullptr;
}

BYTE* ResolveScriptNativeTarget(HMODULE game, const char* nativeName, DWORD parameterCount)
{
    BYTE* base = reinterpret_cast<BYTE*>(game);
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return nullptr;
    }
    const std::size_t imageSize = nt->OptionalHeader.SizeOfImage;
    BYTE* name = FindBytes(
        base,
        imageSize,
        reinterpret_cast<const BYTE*>(nativeName),
        std::strlen(nativeName) + 1);
    if (!name) {
        return nullptr;
    }

    const std::uintptr_t nameAddress = reinterpret_cast<std::uintptr_t>(name);
    for (std::size_t offset = 0; offset + 24 <= imageSize; offset += sizeof(void*)) {
        BYTE* entry = base + offset;
        if (*reinterpret_cast<std::uintptr_t*>(entry) != nameAddress ||
            *reinterpret_cast<DWORD*>(entry + 4) != parameterCount) {
            continue;
        }
        BYTE* thunk = *reinterpret_cast<BYTE**>(entry + 8);
        if (!thunk || thunk < base || thunk + 5 >= base + imageSize || thunk[0] != 0xE9) {
            continue;
        }
        const std::int32_t relative = *reinterpret_cast<std::int32_t*>(thunk + 1);
        BYTE* target = thunk + 5 + relative;
        if (target < base || target + 16 >= base + imageSize) {
            continue;
        }
        return target;
    }
    return nullptr;
}
}

BOOL ConfigurePlayerInventoryCategoryCapacity(DWORD capacity)
{
    if (capacity < VANILLA_CATEGORY_CAPACITY || capacity > MAX_CATEGORY_CAPACITY) {
        return FALSE;
    }
    g_playerCategoryCapacity = capacity;
    return TRUE;
}

BOOL ConfigureWorldContainerCapacity(DWORD capacity)
{
    if (capacity < VANILLA_CATEGORY_CAPACITY || capacity > MAX_WORLD_CONTAINER_CAPACITY) {
        return FALSE;
    }
    g_worldContainerCapacity = capacity;
    return TRUE;
}

void ResetCapturedPlayerInventoryState()
{
    g_playerContainer = nullptr;
    g_playerInventory = nullptr;
    g_playerNativeContext = nullptr;
    g_playerCategories.fill(nullptr);
    g_playerCapacityOverrideDepth = 0;
    g_playerCapacityOverrideCalls = 0;
    WriteDebugLog("OynonTools", "Oynon player inventory session state reset");
}

bool IsObservedPlayerInventoryReady()
{
    return GetObservedPlayerObject() != nullptr &&
        g_playerInventory != nullptr &&
        ArePlayerCategoriesReady(g_playerCategories);
}

bool RefreshObservedPlayerInventoryState()
{
    if (IsObservedPlayerInventoryReady()) {
        return true;
    }
    if (!CapturePlayerCategoriesFromObservedPlayer()) {
        return false;
    }
    return IsObservedPlayerInventoryReady();
}

void __stdcall ApplyObservedPlayerHandsItem(void* itemIdContext)
{
    const int itemId = static_cast<int>(
        reinterpret_cast<std::intptr_t>(itemIdContext));
    void* context = g_playerNativeContext;
    if (!IsReadableMemory(context, sizeof(void*))) {
        WriteDebugLog("OynonTools", "Oynon queued SetPlayerHandsItem rejected: player context unavailable");
        return;
    }

    void** vtable = *reinterpret_cast<void***>(context);
    if (!IsReadableMemory(vtable, sizeof(void*) * 5) || !vtable[4] ||
        !IsReadableMemory(vtable[4], 8)) {
        WriteDebugLog("OynonTools", "Oynon queued SetPlayerHandsItem rejected: context method unavailable");
        return;
    }

    LARGE_INTEGER frequency = {};
    LARGE_INTEGER started = {};
    LARGE_INTEGER finished = {};
    ::QueryPerformanceFrequency(&frequency);
    ::QueryPerformanceCounter(&started);
    const auto setHandsItem = reinterpret_cast<SetPlayerHandsItem_t>(vtable[4]);
    setHandsItem(context, itemId);
    ::QueryPerformanceCounter(&finished);
    const long long elapsedUs = frequency.QuadPart > 0
        ? (finished.QuadPart - started.QuadPart) * 1000000LL / frequency.QuadPart
        : 0;
    if (elapsedUs >= 5000) {
        char line[144] = {};
        std::snprintf(
            line,
            sizeof(line),
            "Oynon SetPlayerHandsItem item=%d elapsed_us=%lld",
            itemId,
            elapsedUs);
        WriteDebugLog("OynonTools", line);
    }
}

BOOL SetObservedPlayerHandsItem(int itemId)
{
    // SetPlayerHandsItem touches live actor/render state. Script effects and
    // console callbacks can request it from worker threads, so serialize it on
    // the same game-window thread used for engine console execution.
    return DispatchGameThreadTask(
        &ApplyObservedPlayerHandsItem,
        reinterpret_cast<void*>(static_cast<std::intptr_t>(itemId)));
}

BOOL StablePrioritizePlayerInventory(
    const DWORD* priorityItemIds,
    DWORD priorityItemIdCount,
    DWORD* oldToNewIndices,
    DWORD mappingCapacity,
    DWORD* categoryItemCounts,
    DWORD categoryCount,
    BOOL* changed)
{
    constexpr DWORD REQUIRED_MAPPING_CAPACITY =
        PLAYER_CATEGORY_COUNT * PLAYER_CATEGORY_MAPPING_STRIDE;
    if (!priorityItemIds || priorityItemIdCount == 0 ||
        !oldToNewIndices || mappingCapacity < REQUIRED_MAPPING_CAPACITY ||
        !categoryItemCounts || categoryCount < PLAYER_CATEGORY_COUNT ||
        !changed) {
        return FALSE;
    }

    *changed = FALSE;
    for (DWORD index = 0; index < mappingCapacity; ++index) {
        oldToNewIndices[index] = 0xFFFFFFFFu;
    }
    for (DWORD category = 0; category < categoryCount; ++category) {
        categoryItemCounts[category] = 0;
    }

    // A world/save transition can leave the previous category objects mapped
    // and readable even though they no longer belong to the active player.
    // Refresh from the observed player before validating/reordering.
    CapturePlayerCategoriesFromObservedPlayer();

    bool categoriesAvailable = true;
    for (void* category : g_playerCategories) {
        if (!category || !IsReadableMemory(category, sizeof(void*))) {
            categoriesAvailable = false;
            break;
        }
    }
    if (!categoriesAvailable) {
        if (!CapturePlayerCategoriesFromObservedPlayer()) {
            WriteDebugLog(
                "OynonTools",
                "Oynon stable inventory priority unavailable: player categories not captured");
            return FALSE;
        }
    }

    std::array<GetCategoryItemCount_t, PLAYER_CATEGORY_COUNT> getCountMethods = {};
    std::array<GetCategoryItem_t, PLAYER_CATEGORY_COUNT> getItemMethods = {};
    std::array<SetCategoryItem_t, PLAYER_CATEGORY_COUNT> setItemMethods = {};
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        if (!ValidateCategoryReorderMethods(
                g_playerCategories[category],
                getCountMethods[category],
                getItemMethods[category],
                setItemMethods[category])) {
            static bool signatureFailureLogged = false;
            if (!signatureFailureLogged) {
                WriteDebugLog(
                    "OynonTools",
                    "Oynon stable inventory priority disabled: category methods rejected");
                signatureFailureLogged = true;
            }
            return FALSE;
        }
    }

    std::array<std::vector<StableInventoryEntry>, PLAYER_CATEGORY_COUNT> sourceByCategory;
    std::array<std::vector<StableInventoryEntry>, PLAYER_CATEGORY_COUNT> orderedByCategory;
    std::array<bool, PLAYER_CATEGORY_COUNT> categoryChanged = {};
    const auto releaseCapturedItems = [&sourceByCategory]() {
        for (const auto& source : sourceByCategory) {
            for (const StableInventoryEntry& entry : source) {
                ReleaseInventoryDefinition(entry.item.definition);
            }
        }
    };

    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        void* categoryContainer = g_playerCategories[category];
        const unsigned int itemCount = getCountMethods[category](categoryContainer);
        if (itemCount > PLAYER_CATEGORY_MAPPING_STRIDE) {
            releaseCapturedItems();
            WriteDebugLog(
                "OynonTools",
                "Oynon stable inventory priority rejected: category exceeds mapping capacity");
            return FALSE;
        }
        categoryItemCounts[category] = itemCount;

        std::vector<StableInventoryEntry>& source = sourceByCategory[category];
        source.reserve(itemCount);
        for (unsigned int index = 0; index < itemCount; ++index) {
            StableInventoryEntry entry = {};
            getItemMethods[category](
                categoryContainer,
                &entry.item,
                index,
                &entry.amount);
            entry.sourceIndex = index;
            if (entry.item.definition) {
                entry.item.definition = RetainInventoryDefinition(entry.item.definition);
                if (!entry.item.definition) {
                    releaseCapturedItems();
                    WriteDebugLog(
                        "OynonTools",
                        "Oynon stable inventory priority rejected: item reference retain failed");
                    return FALSE;
                }
            }
            source.push_back(entry);
        }

        std::vector<StableInventoryEntry>& ordered = orderedByCategory[category];
        ordered.reserve(itemCount);
        for (const StableInventoryEntry& entry : source) {
            if (IsPriorityItemId(entry.item.itemId, priorityItemIds, priorityItemIdCount)) {
                ordered.push_back(entry);
            }
        }
        for (const StableInventoryEntry& entry : source) {
            if (!IsPriorityItemId(entry.item.itemId, priorityItemIds, priorityItemIdCount)) {
                ordered.push_back(entry);
            }
        }

        const DWORD mappingBase = category * PLAYER_CATEGORY_MAPPING_STRIDE;
        for (unsigned int newIndex = 0; newIndex < itemCount; ++newIndex) {
            const unsigned int oldIndex = ordered[newIndex].sourceIndex;
            oldToNewIndices[mappingBase + oldIndex] = newIndex;
            categoryChanged[category] =
                categoryChanged[category] || oldIndex != newIndex;
        }
    }

    bool anyChanged = false;
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        if (categoryChanged[category]) {
            void* categoryContainer = g_playerCategories[category];
            const auto& ordered = orderedByCategory[category];
            for (unsigned int newIndex = 0;
                 newIndex < static_cast<unsigned int>(ordered.size());
                 ++newIndex) {
                const StableInventoryEntry& entry = ordered[newIndex];
                setItemMethods[category](
                    categoryContainer,
                    newIndex,
                    &entry.item,
                    entry.amount);
            }
            anyChanged = true;
        }
    }

    releaseCapturedItems();
    *changed = anyChanged ? TRUE : FALSE;
    return TRUE;
}

bool InstallPlayerInventoryCapacityHook()
{
    if (g_addItemHook.installed) {
        return true;
    }
    HMODULE game = ::GetModuleHandleA(nullptr);
    if (!game) {
        return false;
    }
    BYTE* target = ResolveScriptNativeTarget(game, ADD_ITEM_NAME, 3);
    if (!target) {
        WriteDebugLog("OynonTools", "Oynon player inventory AddItem target not found");
        return false;
    }
    BYTE* capacityPattern = FindBytes(
        target,
        ADD_ITEM_SCAN_SIZE,
        CAPACITY_CHECK_PATTERN,
        sizeof(CAPACITY_CHECK_PATTERN));
    if (!capacityPattern) {
        WriteDebugLog("OynonTools", "Oynon player inventory capacity branch not found");
        return false;
    }
    g_capacityBranch = capacityPattern + sizeof(CAPACITY_CHECK_PATTERN) - 1;

    const BYTE expectedPrologue[] = { 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00 };
    if (std::memcmp(target, expectedPrologue, sizeof(expectedPrologue)) != 0) {
        WriteDebugLog("OynonTools", "Oynon player inventory AddItem prologue rejected");
        return false;
    }

    g_addItemHook.target = reinterpret_cast<std::uintptr_t>(target);
    g_addItemHook.detour = reinterpret_cast<void*>(&HookScriptAddItem);
    g_addItemHook.patchSize = ADD_ITEM_PATCH_SIZE;
    if (!InstallInlineHook(g_addItemHook)) {
        WriteDebugLog("OynonTools", "Oynon player inventory AddItem hook install failed");
        return false;
    }
    g_originalAddItem = reinterpret_cast<ScriptAddItem_t>(g_addItemHook.trampoline);

    BYTE* getPlayerTarget = ResolveScriptNativeTarget(game, GET_PLAYER_CONTAINER_NAME, 1);
    const BYTE getPlayerExpected[] = { 0x83, 0x7C, 0x24, 0x08, 0x01 };
    if (!getPlayerTarget ||
        std::memcmp(getPlayerTarget, getPlayerExpected, sizeof(getPlayerExpected)) != 0) {
        WriteDebugLog("OynonTools", "Oynon GetPlayerContainer target not found or rejected");
        return false;
    }
    g_getPlayerContainerHook.target = reinterpret_cast<std::uintptr_t>(getPlayerTarget);
    g_getPlayerContainerHook.detour = reinterpret_cast<void*>(&HookScriptGetPlayerContainer);
    g_getPlayerContainerHook.patchSize = GET_PLAYER_CONTAINER_PATCH_SIZE;
    if (!InstallInlineHook(g_getPlayerContainerHook)) {
        WriteDebugLog("OynonTools", "Oynon GetPlayerContainer hook install failed");
        return false;
    }
    g_originalGetPlayerContainer =
        reinterpret_cast<ScriptGetPlayerContainer_t>(g_getPlayerContainerHook.trampoline);
    WriteDebugLog("OynonTools", "Oynon safe player inventory capacity hook installed");
    return true;
}
