# Changelog

All notable changes to the AMPlayer project will be documented in this file.

## [4.1.0] - 2026-05-08

### Added

- Web UI for test and control
- eDog mode with PIR sensor support or AudioGameBox mode for small children - Button-based audio trigger system with random audio playback (e.g. animal sounds)
- Added option to disable MQTT client, FTP server, ActionKeys, and WebUI
-

### Changed

- Improved  handling Low Power Mode – Modem Sleep

### Fixed

* Fixed issue where QueueList repository was unreachable

.

Notes

- Audio files should use 44.1 kHz sample rate for best compatibility
- Recommended audio sources: CC0 / royalty-free libraries
- Web UI and Power Saving modes cannot be used together
- The `Key-Value "stop2press"` stops the currently playing sound/audio when the button is pressed again before the interval has elapsed. Once the interval has elapsed, the button must be pressed three times.Stop playing the current sound 'stop2Press' when the button is pressed again is
