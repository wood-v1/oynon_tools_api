#include "player_inventory_capacity_hook.h"

#include "debug.h"
#include "inline_hook_utils.h"
#include "player_effect_hook.h"

#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <array>

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
constexpr BYTE CAPACITY_CHECK_PATTERN[] = {
    0x85, 0xFF, 0x0F, 0x95, 0xC1, 0x03, 0xCB, 0x3B, 0xCA, 0x76
};
constexpr std::size_t CATEGORY_CAPACITY_FIELD_OFFSET = 0x08;

using ScriptAddItem_t = bool(__thiscall*)(void* self, void* arguments, void* result, unsigned int parameterCount);
using ScriptGetPlayerContainer_t = bool(__thiscall*)(void* self, void* arguments, void* result, unsigned int parameterCount);
using GetSubContainerCount_t = unsigned int(__thiscall*)(void* self);
using GetSubContainer_t = void*(__thiscall*)(void* self, unsigned int category);
using LookupObject_t = void*(__thiscall*)(void* self, DWORD hash);
using GetCategoryCapacity_t = unsigned int(__thiscall*)(void* self);
using DirectPlayerAddItem_t = bool(__thiscall*)(
    void* self,
    void* argument0,
    void* argument1,
    void* argument2,
    void* argument3,
    void* argument4);

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
std::array<void*, PLAYER_CATEGORY_COUNT> g_playerCategories = {};
std::array<CapacityVtablePatch, PLAYER_CATEGORY_COUNT> g_capacityVtablePatches = {};
unsigned int g_capacityVtablePatchCount = 0;
void** g_directPlayerAddItemSlot = nullptr;
DirectPlayerAddItem_t g_originalDirectPlayerAddItem = nullptr;
thread_local unsigned int g_playerCapacityOverrideDepth = 0;
thread_local unsigned int g_playerCapacityOverrideCalls = 0;

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
        // The direct player-container interface is embedded at player+0x60;
        // the category manager used by the actual add operation is at +0x3C.
        CapturePlayerCategoriesFromInventory(static_cast<BYTE*>(self) - 0x24);
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
    if (playerContainer == g_playerContainer) {
        PatchDirectPlayerAddItemVtable(playerContainer);
        return;
    }
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

    g_playerContainer = playerContainer;
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
        WriteDebugLog("OynonTools", "Oynon player inventory category getter is unreadable");
        return;
    }

    const auto getSubContainer = reinterpret_cast<GetSubContainer_t>(vtable[3]);
    std::array<void*, PLAYER_CATEGORY_COUNT> categories = {};
    for (unsigned int category = 0; category < PLAYER_CATEGORY_COUNT; ++category) {
        categories[category] = getSubContainer(playerInventory, category);
        if (!IsReadableMemory(categories[category], sizeof(void*))) {
            WriteDebugLog("OynonTools", "Oynon direct player category lookup failed");
            return;
        }
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

void CapturePlayerContainerFromNative(void* self)
{
    if (!IsReadableMemory(self, 0x10)) {
        return;
    }
    void* context = *reinterpret_cast<void**>(static_cast<BYTE*>(self) + 0x0C);
    if (!IsReadableMemory(context, sizeof(void*))) {
        return;
    }
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
    return g_originalGetPlayerContainer
        ? g_originalGetPlayerContainer(self, arguments, result, parameterCount)
        : false;
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
