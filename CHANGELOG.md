# Changelog

## 2.0.0

- Improved UI rendering with anti-aliased rounded controls and ClearType-quality text.
- Added per-monitor-v2 DPI awareness fallback for sharper rendering at Windows display scaling.

- Refined the settings UI with a compact Fluent-inspired glass layout and Windows 11 Mica backdrop.
- Replaced the classic mode combo box with a modern custom selector.
- Added hover feedback, status pill, cleaner spacing, and dynamic compact window sizing.

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
