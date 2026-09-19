# Awake

A lightweight standalone Windows utility that keeps your PC awake without installing the full PowerToys suite.

Awake runs as a small native Windows tray application. It does not require PowerToys, the PowerToys Runner, Settings UI, telemetry libraries, or shared PowerToys DLLs.

## Features

- Keep the PC awake indefinitely
- Optional **keep display on** mode
- 30 minute, 1 hour, and 2 hour tray presets
- Command-line time limits
- System tray controls
- Single-instance protection
- Native Windows executable with no separate runtime dependency

## Usage

Run `Awake.exe`. With no arguments, Awake keeps the system awake indefinitely and places an icon in the notification area.

```text
Awake.exe
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
- CMake

From an x64 Native Tools Command Prompt for Visual Studio 2022:

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

Every push to `main` runs the Windows build workflow. The compiled `Awake.exe` is uploaded as the `Awake-win-x64` workflow artifact.

## How it works

Awake uses the documented Windows `SetThreadExecutionState` API. While active it requests `ES_SYSTEM_REQUIRED | ES_CONTINUOUS`. When **keep display on** is enabled, it also requests `ES_DISPLAY_REQUIRED`. Returning to Off mode resets the execution state with `ES_CONTINUOUS`.

## Relationship to Microsoft PowerToys

This repository is an independent, brand-neutral standalone implementation inspired by the functionality of Microsoft PowerToys Awake. It does not use Microsoft or PowerToys branding, logos, icons, telemetry, Settings UI, or shared PowerToys components.

Microsoft PowerToys is an open-source Microsoft project. This repository is not affiliated with or endorsed by Microsoft.

## License

MIT. See [LICENSE](LICENSE).
