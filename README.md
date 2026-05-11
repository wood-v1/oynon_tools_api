# OynonTools

Shared hook library for Pathologic Classic HD runtime mods. The library allows you to run custom scripts in the game without touching game files and exposes an API of useful functions for modding.

## API

`OynonInitializeHooksWhenReady(DWORD hookFlags)`

This is the main entry point called during startup. Initializes the shared hook layer for the requested subsystems.

Supported flags:

- `OYNON_HOOK_CONSOLE_READ` - forwards in-game console output to registered callbacks.
- `OYNON_HOOK_CONSOLE_EXECUTE` - allows send commands into the in-game console.
- `OYNON_HOOK_MOVEMENT_FRICTION` - enables runtime friction/speed multiplier control.
- `OYNON_HOOK_MOVEMENT_VERTICAL` - enables jump height and landing gravity overrides.
- `OYNON_HOOK_UI_DAYCHANGE_TEXT` - enables temporary redirection of `daychange.xml` to a custom UI XML.

API Methods:

`OynonRegisterConsoleMessageCallback(OynonConsoleMessageCallback callback, void* userData)`

Registers a listener for console lines.

`OynonExecCommand(const char* command)`

Executes a console command in the running game.

`OynonSetMovementFrictionMultiplier(float frictionMultiplier)`

Changes horizontal movement friction. For character walking speed tuning.

`OynonSetMovementJumpHeightMultiplier(float jumpHeightMultiplier)`

Changes jump height scaling. UFor sprint/jump feel tuning.

`OynonSetMovementLandingGravity(int landingGravity)`

Overrides landing gravity. For sprint/jump feel tuning.

`OynonUIDaychangePoll()`

Retries installing the daychange UI hook if `UI.dll` was not ready during initial startup.

`OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs)`

Arms a short-lived redirect from vanilla `daychange.xml` to a custom XML file.

`OynonUIDaychangeIsVanillaActive(DWORD now)`

Reports whether the vanilla daychange window is currently active or was just opened.

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
