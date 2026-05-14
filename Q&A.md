# Frequently Asked Questions (FAQ)

## General

### Q: What is this project?
A:

- Flexible open-source ESP32-based Audio Message Player for playing TTS, TTM, MP3 files, and live radio streams with WebUI, MQTT, FTP server, and GPIO-triggered actions.
- Designed for smart home environments with MQTT-based remote control and automation support.
- With the addition of a PIR sensor or button, it can operate autonomously as an eDog or AudioBox without a smart home system. Any movement or button press will be reported to a smart home system via MQTT.

### Q: Which hardware is supported?
A: 
- ESP32-A1S Audio kit

### Q: Is this project open source?
A: Yes. The source code is available on GitHub under the GPLv3 License

## Installation

### Q: How do I install the firmware?
A:

1. Open the Web Flasher:  
   https://nmaciol.github.io/ESP32-Audio-Message-Player-firmware/
2. Flash the firmware to the ESP32.
3. Download `sd-card-content.zip` and extract the files to a FAT32-formatted SD card.
4. Configure the `app.json` file according to your setup.
5. Insert the SD card and reboot the device.

### Q: Can I update over Wi-Fi (OTA)?
A: No. Currently, OTA updates are not supported. Firmware updates must be installed using the Web Flasher.

## Features

### Q: Does the device support WebUI?
A: Yes. The WebUI can be enabled or disabled in the settings.

### Q: Can I play internet radio streams?
A: Yes. Live streams and local audio files are supported.

### Q: How do I control playback and volume?
A:

- `Key3` → Stop playback
- `Key4` → GPIO23 input for PIR sensor or button to trigger playback from the SoundPool
- `Key5` → Volume Down
- `Key6` → Volume Up
- `Key1` → Reboot device
- `MQTT` → Remote control via MQTT commands
- `WebUI` → Control the device through the web interface

## Troubleshooting
### Q: During system startup, error messages are announced through the speaker. What do they mean?
A:

#### Error 1 – Config file not found

Possible causes:

- The SD/MMC card is not formatted correctly
- The SD card is corrupted
- The SD card is incompatible with the ESP32
- The `app.json` file was not found

Solution:

1. Format the SD card as `FAT32`
2. Extract the contents of `sd-card-content.zip` to the SD card
3. Configure the `app.json` file correctly
4. Reinsert the SD card and reboot the device

#### Error 2 – Config file error

Possible causes:

- The `app.json` file contains invalid JSON
- The configuration structure is incorrect
  
Solution:

1. Open `app.json`
2. Validate and correct the JSON structure
3. Save the file and reboot the device

#### Error 3 – Network connection error

Possible causes:

- Wrong Wi-Fi SSID or password
- Unsupported Wi-Fi network
- Weak Wi-Fi signal

Solution:

- Check the Wi-Fi SSID and password
- Ensure a `2.4 GHz` Wi-Fi network is used
- Restart the device
- Move the device closer to the router if needed

### Q: The WebUI cannot be opened
A:

Possible causes:

- The WebUI is disabled in `app.json`
- The WebUI was disabled using `Key4`

Solution:

1. Open and check the `app.json` configuration
2. Ensure the WebUI setting is enabled and reboot 
3. Or press and hold `Key4` for at least 10 seconds to enable the WebUI again

### Q: The sound or audio file is not playing
A:

Possible causes:

- The MP3 file does not exist
- The audio file format is unsupported or corrupted
- Unsupported sample rate (recommended: `44100 Hz`)

Solution:

1. Check whether the MP3 file exists on the SD card
2. Try another audio file
3. Convert the audio file to a supported MP3 format with a sample rate of `44100 Hz`


### Q: Can't connect to the FTP server?
A:

Possible causes:

- The Wi-Fi signal is too weak
- The FTP server is disabled in `app.json`
- The ESP32 has a poor wireless connection

Solution:

1. Ensure the FTP server is enabled in `app.json`
2. Reboot the device after changing the configuration
3. Move the device closer to the Wi-Fi router
4. Use an ESP32 board with an external antenna for better Wi-Fi performance

### Q: The live stream or internet radio playback has crackling sounds or pauses. Why?
A:

Possible causes:

- Poor internet connection quality
- Network congestion or router overload
- Weak Wi-Fi signal strength

Solution:

1. Check the stability and speed of your internet connection
2. Move the device closer to the Wi-Fi router
3. Restart the router and the device
4. In the router settings, increase the network priority for the AMPlayer device
5. Try changing the Wi-Fi transmission channel
6. If the issue persists, consider changing your internet provider


### Q: I hear noise from the speaker. What can I do?
A: Keep the external antenna away from the PCB and audio components.


## Contributing

### Q: How can I contribute?
A:
- Open issues
- Submit pull requests
- Improve documentation

## Support

### Q: Where can I ask questions?
A:
- GitHub Issues
- Discussions
- Community forums

## License

This project is licensed under the GPLv3 License.