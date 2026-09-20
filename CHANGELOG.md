# Changelog

## 2.0.0

- Fixed MSVC duplicate-manifest build failure.
- Removed duplicate UNICODE macro definitions.

- Simplified the settings UI to a focused single-page layout and removed inactive General, Appearance, and About navigation items.
- Replaced the blurry single-resolution icon with a crisp multi-resolution Awake icon for window, taskbar, and tray use.
- Refined On/Off switches with a cleaner compact Windows-style design.
- Enabled Common Controls v6 through an application manifest for more modern native controls.

- Added a full native Windows settings window.
- Added On/Off and Keep screen on switches.
- Added selected-power-plan, indefinite, interval, and expiration modes.
- Added persisted per-user settings.
- Added live countdown/status updates.
- Improved tray controls and single-instance behavior.
- Added an embedded application icon and Windows version metadata.
- Added `--minimized` startup support.
- Updated CI packaging for Windows x64 release artifacts.
