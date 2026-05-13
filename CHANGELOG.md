# Changelog

All notable changes to the AMPlayer project will be documented in this file.

## [5.0.1] - 2026-05-13

* Add MQTT commands to enable/disable for Stop4Press, ActionKeys, WebUI, FTP server und MQTT client and press/key4. The changed states are not saved to app.ini.
* Add a button to the WebUI to simulate pressing KEY4
* Add Key4 long-press  (10s+) action to toggle Web UI on/off
* Extended `SoundPool` / `AudioPool` with the possibility to play live streams (radio stations).
* Improvement: Random selection of entries from SoundPool/AudioPool

### Notes

* Pressing `Key4` plays a random entry, `Key6` increases the volume, and `Key5` decreases the volume. This is just one way to control the AMPlayer. The best solution is the IKEA STYRBAR remote control.

## [5.0.0] - 2026-05-10

### Changed

- Chore: update espressif32 platform to 7.0.0

Notes

- If you encounter issues while compiling in VS Code / PlatformIO, see the section “Issue compiling in VS Code”in this README.md
- No sound or audio files are included in the SD card template on GitHub due to copyright restrictions. Audio content can be downloaded from the following sources:
  - https://www.ohrka.de/ueber-ohrka/100-neue-ohrka-maerchen
  - https://pixabay.com/

## [5.0.0] - 2026-05-08

### Added

- Web UI for test and control
- eDog mode with PIR sensor support or AudioGameBox mode for small children - Button-based audio trigger system with random audio playback (e.g. animal sounds)
- Added option to disable MQTT client, FTP server, ActionKeys, and WebUI
-

### Changed

- Improved  handling Low Power Mode – Modem Sleep

### Fixed

* Fixed issue where QueueList repository was unreachable

Notes

- Audio files should use 44.1 kHz sample rate for best compatibility
- Recommended audio sources: CC0 / royalty-free libraries
- Web UI and Power Saving modes cannot be used together
- The `Key-Value "stop2press"` stops the currently playing sound/audio when the button is pressed again before the interval has elapsed. Once the interval has elapsed, the button must be pressed three times.Stop playing the current sound 'stop2Press' when the button is pressed again is
