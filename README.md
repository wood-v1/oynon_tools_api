# OynonTools

Shared hook library for Pathologic Classic HD runtime mods. The library exposes reusable runtime hooks and helper APIs for console commands, movement tuning, UI redirection, and debug logging.

## API

`OynonInitializeHooksWhenReady(DWORD hookFlags)`

This is the main entry point called during startup. Initializes the shared hook layer for the requested subsystems.

Supported flags:

- `OYNON_HOOK_CONSOLE_READ` - forwards in-game console output to registered callbacks.
- `OYNON_HOOK_CONSOLE_EXECUTE` - allows sending commands into the in-game console.
- `OYNON_HOOK_MOVEMENT_FRICTION` - enables runtime friction/speed multiplier control.
- `OYNON_HOOK_MOVEMENT_VERTICAL` - enables jump height and landing gravity overrides.
- `OYNON_HOOK_UI_DAYCHANGE_TEXT` - enables temporary redirection of `daychange.xml` to a custom UI XML.
- `OYNON_HOOK_UI_PLAYERSTAT_REDIRECT` - enables persistent redirection of `playerstat.xml` to a custom UI XML.
- `OYNON_HOOK_PLAYER_SHOOTING_BLOCK` - allows runtime suppression of the script-visible player shooting state.
- `OYNON_HOOK_PLAYER_EFFECT_CALLBACK` - reports successful player effects after the game applies them.
- `OYNON_HOOK_UI_INVENTORY_STATE` - reports opening and closing inventory-style UI overlays.
- `OYNON_HOOK_PLAYER_USE_CALLBACK` - reports successful player interactions with the target script name.
- `OYNON_HOOK_UI_INVENTORY_REDIRECT` - enables persistent redirection of vanilla player and loot windows to custom UI XML files.
- `OYNON_HOOK_PLAYER_INVENTORY_CAPACITY` - removes the native 12-stack category rejection only for the five-subcontainer player inventory.
- `OYNON_HOOK_UI_WINDOW_PREPARE` - reports the original XML name immediately before a UI window is created.

Engine hooks wait for `Engine.dll` before installing. UI hooks are installed through `UI.dll`; if `UI.dll` is not loaded yet, call `OynonUIPoll()` periodically until the hook is installed.

`OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData)`

Registers a listener for console lines.

`OynonRegisterConsoleMessageFilter(OynonConsoleMessageFilter filter, void* userData)`

Registers a console-line filter. Return `TRUE` to suppress the line from the in-game console after listeners have received it.

`OynonExecCommand(const char* command)`

Executes a console command in the running game.

`OynonSetMovementFrictionMultiplier(float frictionMultiplier)`

Changes horizontal movement friction. For character walking speed tuning.

`OynonSetMovementJumpHeightMultiplier(float jumpHeightMultiplier)`

Changes jump height scaling. For sprint/jump feel tuning.

`OynonSetMovementLandingGravity(int landingGravity)`

Overrides landing gravity. For sprint/jump feel tuning.

`OynonSetPlayerShootingBlocked(BOOL blocked)`

Controls whether new player shooting events are suppressed and the player `IsShooting` script-native call reports `false`. This can stop script-driven attacks without changing the physical input state.

`OynonRegisterPlayerEffectCallback(OynonPlayerEffectCallback callback, void* userData)`

Registers a listener for successful effects applied to the player. The callback runs after the original game method returns.

`OynonSetPlayerBootstrapEffect(const char* effectName)`

Configures one compiled effect to be applied once to each newly created player actor. Configure it before initializing `OYNON_HOOK_PLAYER_EFFECT_CALLBACK`.

`OynonSetPlayerInventoryCategoryCapacity(DWORD capacity)`

Configures the player-only category-capacity override (12..64). The native category check is bypassed when the configured value is above the vanilla value; the caller remains responsible for enforcing its overall inventory limit.

`OynonSetWorldContainerCapacity(DWORD capacity)`

Configures the native `AddItem` capacity override for non-player world containers (12..128). The caller remains responsible for enforcing its chosen visible or gameplay limit.

`OynonStablePrioritizePlayerInventory(...)`

Stably moves the requested numeric item IDs to the front of each player inventory category without using `RemoveItem`/`AddItem`. Relative order is preserved both for matching and non-matching entries. The call also returns per-category item counts and an old-index-to-new-index mapping with a fixed stride of 64 entries per category, allowing a mod to reconcile its own visual layout.

The operation validates the category `GetItem`/`SetItem` methods before changing anything and returns `FALSE` without modifying the inventory on an unsupported executable signature. The API is generic: the caller owns all game- or mod-specific item lists.

`OynonRegisterInventoryStateCallback(OynonInventoryStateCallback callback, void* userData)`

Registers a listener for inventory-style overlay state changes. The callback receives `TRUE` when an inventory, container, corpse, or apparatus overlay opens and `FALSE` when the corresponding UI station closes.

`OynonRegisterPlayerUseCallback(OynonPlayerUseCallback callback, void* userData)`

Registers a listener for successful player interactions. The callback receives the target object's script name after its `OnUse` event is accepted.

`OynonRegisterPlayerShootingAttemptCallback(OynonPlayerShootingAttemptCallback callback, void* userData)`

Registers a listener for player shooting attempts after requesting `OYNON_HOOK_PLAYER_SHOOTING_BLOCK`. The callback receives `FALSE` for a newly accepted shooting event and `TRUE` when an active script-driven attack requests another held-input iteration through `IsShooting`.

`OynonUIPoll()`

Retries installing the shared low-level UI window hooks if `UI.dll` was not ready during initial startup or if another UI reload replaced a patch. This neutral polling API is shared by daychange, playerstat, and inventory features.

`OynonUIDaychangePoll()`

Compatibility wrapper for `OynonUIPoll()`. Existing mods may continue to call it.

`OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs)`

Arms a short-lived redirect from vanilla `daychange.xml` to a custom XML file. The redirect is active for `ttlMs` milliseconds.

`OynonUIDaychangeIsVanillaActive(DWORD now)`

Reports whether the vanilla daychange window is currently active or was just opened.

`OynonUIPlayerstatSetRedirect(const char* xml)`

Redirects vanilla `playerstat.xml` window creation to a custom XML file. Pass `nullptr` or an empty string to clear the redirect.

This redirect is persistent after it is configured. Request `OYNON_HOOK_UI_PLAYERSTAT_REDIRECT` during initialization, then call `OynonUIPlayerstatSetRedirect("my_playerstat.xml")` once your custom XML is available.

`OynonUIInventorySetRedirect(const char* xml)`

Redirects vanilla `inventory.xml` window creation to a custom XML file. Pass `nullptr` or an empty string to clear the redirect.

This redirect is persistent after it is configured. Request `OYNON_HOOK_UI_INVENTORY_REDIRECT` during initialization, then call `OynonUIInventorySetRedirect("my_inventory.xml")` once your custom XML is available.
Inventory-state classification treats the configured XML and its resolution variants, such as `my_inventory_1024x768.xml`, as inventory overlays.

`OynonUILootSetRedirects(const char* containerXml, const char* corpseXml)`

Redirects vanilla `container.xml` and `corpse.xml` window creation independently. Either redirect can be cleared with `nullptr` or an empty string. These redirects use the same persistent inventory UI hook and participate in inventory-overlay state classification.

`OynonRegisterUIWindowPrepareCallback(OynonUIWindowPrepareCallback callback, void* userData)`

Registers a generic callback that receives the original XML name immediately before `CreateWnd`. The callback does not redirect or replace the window. Request `OYNON_HOOK_UI_WINDOW_PREPARE` during initialization.

`OynonUIInventoryPoll()`

Polls inventory overlay classification and also retries installing the shared UI hooks. Call this periodically after requesting `OYNON_HOOK_UI_INVENTORY_STATE`.

`OynonUIInventoryGetOverlayKind()`

Returns the currently active inventory-style overlay:

- `OYNON_INVENTORY_OVERLAY_NONE` - no tracked inventory overlay is open.
- `OYNON_INVENTORY_OVERLAY_PLAYER` - the player inventory is open.
- `OYNON_INVENTORY_OVERLAY_CONTAINER` - a world container is open.
- `OYNON_INVENTORY_OVERLAY_CORPSE` - a corpse inventory is open.
- `OYNON_INVENTORY_OVERLAY_OTHER` - another tracked inventory-style window, such as an apparatus, is open.

Request `OYNON_HOOK_UI_INVENTORY_STATE` and call `OynonUIInventoryPoll()` periodically before reading this value. Redirected inventory, container, and corpse XML files retain their corresponding overlay kinds.

`OynonDebugConfigureChannel(const char* channelId, BOOL enabled, const char* logPath, const char* consoleCapturePath)`

Configures a debug channel manually. When enabled, `OynonDebugLog` writes to `logPath`. If `consoleCapturePath` is not empty, console-capture lines can be appended there too.

`OynonDebugConfigureLauncherChannel(const char* channelId, BOOL captureConsole)`

Configures a debug channel from `GameModLauncher.ini`. The launcher setting `[Logging] Enabled` controls whether the channel is active. Logs are written next to the loaded OynonTools module as `Debug.log`, and console capture is written to `Console.log` when `captureConsole` is true.

`OynonDebugClearConsoleCapture(const char* channelId)`

Clears the configured console-capture file for a debug channel.

`OynonDebugOpenConsole()`

Opens and positions a debug console window.

`OynonDebugLog(const char* channelId, const char* line)`

Writes one line to a configured debug channel.

`OynonDebugAppendConsoleCaptureLine(const char* channelId, const char* line)`

Appends one captured console line to the configured console-capture file. Disabled channels and channels without a capture path are ignored.

## Build

Requirements:

- Windows toolchain for native Win32 builds
- Visual Studio with x86/Win32 toolchain
- CMake

This DLL must be built as Win32/x86. The project intentionally fails configuration on 64-bit builds because the hooks target the original 32-bit game executable.

Example configure and build:

```powershell
cmake -S . -B build-win32 -G "Visual Studio 17 2022" -A Win32
cmake --build build-win32 --config Release
```

Artifacts:

- `bin/Win32/<Config>/OynonTools.dll`
- `lib/Win32/<Config>/OynonTools.lib`
