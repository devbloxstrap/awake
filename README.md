# Awake 2.0

A lightweight standalone Windows utility that keeps your PC awake without installing the full PowerToys suite.

Version 2.0 adds a proper Windows settings interface while keeping the low-overhead native tray application design.

## What's new in 2.0

- New light settings window inspired by the familiar Awake workflow
- Main **Awake On/Off** switch
- Four behavior modes:
  - Keep using the selected power plan
  - Keep awake indefinitely
  - Keep awake for a time interval
  - Keep awake until an expiration date/time
- **Keep screen on** option
- Persistent settings under the current Windows user
- Live remaining-time status
- Tray quick actions and Open Awake command
- Single-instance behavior now brings the existing window forward
- Embedded application/tray icon
- File and product version metadata set to **2.0.0**
- `--minimized` startup option

## Tray controls

Right-click the tray icon for quick access to Off, selected power plan, indefinite mode, 30-minute / 1-hour / 2-hour presets, Keep screen on, and Exit.

Left-click the tray icon to open the settings window. Closing the settings window hides it to the tray; use **Exit** from the tray menu to terminate Awake.

## Command line

```text
Awake.exe
Awake.exe --minimized
Awake.exe --display-on true
Awake.exe --time-limit 3600
Awake.exe --time-limit 3600 --display-on true
Awake.exe --off
Awake.exe --help
```

`--time-limit` is specified in seconds.

## Build

Requirements:

- Windows 10 or Windows 11
- Visual Studio 2022 Build Tools with **Desktop development with C++**
- CMake 3.20+

From an x64 Native Tools Command Prompt:

```bat
build-msvc.bat
```

Or:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
```

The executable is produced at:

```text
build\Release\Awake.exe
```

## GitHub Actions

Pushes to `main`, pull requests, and manual runs build the Windows x64 application and upload a packaged `Awake-v2.0.0-win-x64.zip` artifact.

## How it works

Awake uses the documented Windows `SetThreadExecutionState` API. Active keep-awake modes request `ES_SYSTEM_REQUIRED | ES_CONTINUOUS`; when **Keep screen on** is enabled, `ES_DISPLAY_REQUIRED` is also requested. Returning to Off or selected-power-plan mode releases the request with `ES_CONTINUOUS`.

## Relationship to Microsoft PowerToys

This repository is an independent standalone implementation inspired by the functionality and interaction model of Microsoft PowerToys Awake. It does not require PowerToys, the PowerToys Runner, telemetry libraries, the PowerToys Settings application, or shared PowerToys DLLs.

Microsoft PowerToys is an open-source Microsoft project. This repository is not affiliated with or endorsed by Microsoft.

## License

MIT. See [LICENSE](LICENSE).
