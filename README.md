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

Engine hooks wait for `Engine.dll` before installing. UI hooks are installed through `UI.dll`; if `UI.dll` is not loaded yet, call `OynonUIDaychangePoll()` periodically until the hook is installed.

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

`OynonUIDaychangePoll()`

Retries installing the shared UI hook if `UI.dll` was not ready during initial startup. This is used by both daychange and playerstat UI redirection.

`OynonUIDaychangeRequestRedirect(const char* xml, DWORD ttlMs)`

Arms a short-lived redirect from vanilla `daychange.xml` to a custom XML file. The redirect is active for `ttlMs` milliseconds.

`OynonUIDaychangeIsVanillaActive(DWORD now)`

Reports whether the vanilla daychange window is currently active or was just opened.

`OynonUIPlayerstatSetRedirect(const char* xml)`

Redirects vanilla `playerstat.xml` window creation to a custom XML file. Pass `nullptr` or an empty string to clear the redirect.

This redirect is persistent after it is configured. Request `OYNON_HOOK_UI_PLAYERSTAT_REDIRECT` during initialization, then call `OynonUIPlayerstatSetRedirect("my_playerstat.xml")` once your custom XML is available.

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
