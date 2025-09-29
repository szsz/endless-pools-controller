# Endless Pools Controller (ESP32-S3)

An ESP32-S3-based controller for a swim machine, featuring a web-based user interface, programmable workouts, and WiFi/Ethernet connectivity. This project allows you to manage, run, and monitor swim workouts from your phone or computer.

Note: I am not affiliated with Endless Pools.

Tested hardware: Waveshare ESP32-S3-ETH (W5500 over SPI). See board details and pinouts on the Waveshare wiki:
- https://www.waveshare.com/wiki/ESP32-S3-ETH

---

## Table of Contents

- [Introduction](#introduction)
- [Swim Machine Protocol](#swim-machine-protocol)
- [Deploying to ESP32-S3 (Arduino IDE/CLI)](#deploying-to-esp32-s3-arduino-idecli)
  - [Arduino IDE (ESP32-S3-ETH settings)](#arduino-ide-esp32-s3-eth-settings)
  - [Arduino CLI (FQBN and OTA)](#arduino-cli-fqbn-and-ota)
- [Uploading Data Files (Web UI, Workouts, etc.)](#uploading-data-files-web-ui-workouts-etc)
- [Over-the-Air (OTA) Updates](#over-the-air-ota-updates)
- [User Manual](#user-manual)
  - [WiFi Setup](#wifi-setup)
  - [Connecting to the Webserver](#connecting-to-the-webserver)
  - [Managing Workouts](#managing-workouts)
- [Tips: Preventing Phone Screen Lock (Guided Access)](#tips-preventing-phone-screen-lock-guided-access)
- [Viewer and UDP Monitor](#viewer-and-udp-monitor)
  - [Node.js Viewer](#nodejs-viewer)
  - [Web UI Status Page](#web-ui-status-page)
- [UDP Message Formats](#udp-message-formats)
- [License](#license)

---

## Introduction

This project is a smart controller for a swim machine, built on the ESP32-S3 microcontroller. It provides:
- A web-based interface for configuring and running workouts.
- WiFi and Ethernet connectivity for easy access from any device.
- Persistent storage of workouts and preferences.
- Real-time monitoring and control of the swim machine.

---

## Swim Machine Protocol

The swim machine is controlled via a state machine and communicates over UDP. The protocol was reverse engineered and rewritten.

### Workout Structure

- Segment: Each workout consists of one or more segments.
  - `pace100s`: Pace in seconds per 100 meters (0 = rest).
  - `durSec`: Duration of the segment in seconds.

### State Machine & API

- Initialization: `SwimMachine::begin()` sets up the protocol and network event handling.
- Workout Control:
  - `loadWorkout(segments)`: Load a new workout (list of segments).
  - `start()`: Begin the workout.
  - `pause()`: Pause or resume the workout.
  - `stop()`: Abort the workout.
  - `tick()`: Call regularly in the main loop to advance the state machine.
- Status: `getStatus()` returns the current state (active, paused, current segment, elapsed time).
- Networking: `setPeerIP(ip)` sets the peer for UDP communication.

---

## Deploying to ESP32-S3 (Arduino IDE/CLI)

Hardware (tested)
- Waveshare ESP32-S3-ETH (ESP32-S3R8 with 16MB flash, 8MB PSRAM, W5500 SPI Ethernet)
  - SPI Ethernet (W5500) pins (per Waveshare docs):
    - CS=GPIO14, RST=GPIO9, INT=GPIO10, SCK=GPIO13, MISO=GPIO12, MOSI=GPIO11
  - Camera and other interfaces are available but not required for this project
- Swim machine hardware (relay/motor control, sensors as needed).
- USB Type‑C cable.

Software (common)
- Filesystem: LittleFS for web UI and workout data storage.
- Web server: ESPAsyncWebServer + AsyncTCP (ESP32).
- JSON: ArduinoJson.
- mDNS: ESPmDNS (included with ESP32 Arduino core).

Note on Ethernet: This firmware uses the Arduino Ethernet support for SPI W5500 (ETH.h). No RMII/PHY configuration is needed for ESP32-S3-ETH since Ethernet is via W5500 on SPI. The default W5500 pin definitions in `ConnectionManager.h`/`NetworkSetup.h` match the Waveshare board:
- ETH_TYPE=ETH_PHY_W5500
- ETH_CS=14, ETH_IRQ=10, ETH_RST=9, ETH_SPI_SCK=13, ETH_SPI_MISO=12, ETH_SPI_MOSI=11

Reference: https://www.waveshare.com/wiki/ESP32-S3-ETH

### Arduino IDE (ESP32-S3-ETH settings)

Requirements
- Arduino IDE 2.x
- ESP32 boards support by Espressif Systems (Boards Manager). Install “ESP32 by Espressif Systems” (2.0.12 or newer recommended per Waveshare docs).
- Libraries (Library Manager or GitHub):
  - ArduinoJson
  - ESPAsyncWebServer (ESP32-compatible)
  - AsyncTCP (ESP32)
  - LittleFS (ESP32) support is included in the ESP32 core

Board selection
- Tools > Board > esp32 > ESP32S3 Dev Module

Recommended board options for Waveshare ESP32-S3-ETH:
- USB Mode: Hardware CDC and JTAG
- USB CDC On Boot: Enabled (needed if board exposes only USB CDC)
- Flash Size: 16MB
- Partition Scheme: Huge APP
- PSRAM: OPI PSRAM (8MB)
- CPU Frequency, Upload Speed: defaults are fine unless you need changes

Build and upload (USB)
- Open `endless-pools-controller.ino`
- Select board and port
- Click Upload

### Arduino CLI (FQBN and OTA)

Project helpers included in this repo:
- `sketch.yaml`: Defines default_fqbn and a profile `esp32s3-eth` with the required options.
- `scripts/ota-upload.bat`: Windows helper to compile with Arduino CLI and upload via OTA using sketch.yaml (default_fqbn or an explicit profile).

ESP32-S3 Dev Module FQBN (example)
- The exact option keys can vary by ESP32 core version. A typical FQBN with the requested settings looks like:
```
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=huge_app,PSRAM=opi
```
If Arduino CLI reports an error about options, run:
```
arduino-cli board details esp32:esp32:esp32s3
```
to list the supported option names for your installed core version, then adjust `sketch.yaml` accordingly (edit default_fqbn or the `esp32s3-eth` profile).

Using the helper script (Windows)
- Prerequisite: Install Arduino CLI and the ESP32 core (e.g. `arduino-cli core install esp32:esp32`).
- Optional: Configure your WiFi/Ethernet so the device is reachable by hostname or IP (default OTA service port: 3232).
- To compile and OTA upload in one step:
```
scripts\ota-upload.bat 192.168.1.50
```
- If your device advertises mDNS and your network supports it, you can use:
```
scripts\ota-upload.bat swimmachine.local
```
- Optionally select the explicit profile defined in `sketch.yaml`:
```
scripts\ota-upload.bat 192.168.1.50 esp32s3-eth
```
- The script uses `sketch.yaml` (default_fqbn or specified profile), builds to `build\arduino`, then uploads over the network using Arduino CLI’s OTA.

---

## Uploading Data Files (Web UI, Workouts, etc.)

The ESP32-S3 uses LittleFS to store web UI files and workout data.

Where are the files?
- Web UI files (`index.html`, `run.html`, `status.html`), static assets (`/static`), and workout JSON files (`/workouts`) are all placed in the `data/` directory at the repository root.

How to upload to the device?
- Arduino IDE:
  - Arduino LittleFS Upload tool (Arduino IDE 2.x, recommended): https://github.com/earlephilhower/arduino-littlefs-upload?tab=readme-ov-file
    - Use the LittleFS Upload menu under Tools to upload the contents of `data/` to the device.
  - ESP32 LittleFS Uploader plugin (lorol): https://github.com/lorol/arduino-esp32fs-plugin
    - Use Tools > ESP32 Sketch Data Upload to upload `data/` to LittleFS.

---

## Over-the-Air (OTA) Updates

Arduino OTA is built into the firmware and enabled at boot.

Defaults
- Hostname: swimmachine
- Port: 3232
- Password: defined in local `otapassword.h` (not committed)

Requirements
- The device must have an IP on your LAN (Wi‑Fi STA or Ethernet).
- Your computer must be on the same network.

Local secret setup
- Set `ota_password` in `sketch.yaml`. This value is passed to Arduino CLI for OTA and injected into the firmware at build time.
  - WARNING: This stores a secret in plain text in your repo. Consider rotating it regularly or moving secrets to a private repo/CI environment.
- Optional fallback for local builds: the code still contains `otapassword.h`. If no build-time `OTA_PASSWORD` is injected, the firmware falls back to the value from this header:
```c++
#pragma once
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "REPLACE_WITH_A_LONG_RANDOM_SECRET"
#endif
```

Arduino IDE
1. Power the device and wait for it to connect to the network.
2. In Arduino IDE: Tools > Port, select the Network Port for `swimmachine` (or the device’s IP).
3. Click Upload. When prompted for a password, enter the value of `OTA_PASSWORD` from your local `otapassword.h`.
4. The device will report progress over the network and reboot when done.

Arduino CLI (Windows helper)
- With `sketch.yaml` configured, compile and OTA upload in one step (uses `default_port` and `ota_password` from `sketch.yaml`):
```
scripts\ota-upload.bat
```
- You can also specify the target and/or profile explicitly:
```
scripts\ota-upload.bat swimmachine.local
scripts\ota-upload.bat 192.168.1.50 esp32s3-eth
```
- The script reads `default_fqbn` / profile, `default_port`, and `ota_password` from `sketch.yaml`, injects `OTA_PASSWORD` at build time, builds to `build\arduino`, and uploads via Arduino CLI OTA.

Command-line (espota.py, alternative)
- You can also use Espressif’s `espota.py` script directly to upload a compiled binary:
  - Export/compile a .bin (Arduino IDE: Sketch > Export Compiled Binary or use Arduino CLI build output).
  - Then run:
    ```
    python espota.py -i DEVICE_IP -p 3232 --auth=YOUR_OTA_PASSWORD -f PATH_TO_BINARY.bin
    ```
  - `espota.py` is bundled with the Arduino ESP32 core. Adjust the path if needed.

Notes
- OTA also prints progress to the serial log (115200). You’ll see “OTA: Start”, periodic progress, then “OTA: End”.
- If mDNS is available on your network, you can reach the device by `swimmachine.local`; otherwise use its IP address.

---

## User Manual

### WiFi Setup

First Boot or WiFi Change
- On boot, the device tries to connect using stored WiFi credentials (saved in LittleFS).
- If connection fails within ~15 seconds, it starts AP (Access Point) mode for configuration.
- Connect your phone or computer to the WiFi network named `SwimMachine_Config`.
- Open a browser and go to http://192.168.0.3
- Enter your WiFi SSID and password, then save. The device will reboot and connect to your network.

Normal Operation
- The device attempts to connect to the last used WiFi network on boot.
- If successful, it will be accessible on your local network.

### Connecting to the Webserver

Once connected to WiFi or Ethernet, access the web UI:
- By mDNS hostname: http://swimmachine.local (on networks that support mDNS).
- Or by device IP address (check serial output or your router’s device list).

The web UI allows you to:
- View and manage workouts.
- Start, pause, or stop workouts.
- Monitor swim machine status in real time.

### Managing Workouts

- Workouts are stored as JSON files in `/data/workouts/`.
- Example workout files are provided in this folder. You can use them as templates for your own workouts.
- You can add, edit, or delete workouts via the web UI.
- Workouts can also be uploaded directly to the device using the filesystem upload process above.

---

## Tips: Preventing Phone Screen Lock (Guided Access)

iOS (iPhone/iPad)
- Guided Access:
  1. Settings > Accessibility > Guided Access (enable)
  2. Set a passcode
  3. Open the swim machine web UI in Safari
  4. Triple-click side or home button to start Guided Access
- Auto-Lock: Settings > Display & Brightness > Auto-Lock > Never (remember to restore after workout)

Android
- Screen Pinning:
  1. Settings > Security > Screen pinning (enable)
  2. Open the swim machine web UI in Chrome
  3. Use Overview > app icon > Pin
- Keep Screen On: Settings > Display > Sleep > longer duration or Never (if available)

---

## Viewer and UDP Monitor

This project includes two tools for monitoring UDP messages from the swim machine: a Node.js-based viewer and a web-based status page.

### Node.js Viewer

The `viewer` directory contains a Node.js application that listens for UDP messages from the swim machine and provides a real-time web interface for monitoring and analyzing these messages.

Features
- Receives and parses UDP packets on ports 9750 and 45654.
- Decodes commands using `commands.csv`.
- Serves a web interface via Express and Socket.IO for real-time updates.
- Displays message details such as command, parameters, timestamps, and more.

Usage
1. Install dependencies:
   ```sh
   cd viewer
   npm install
   ```
2. Start the server:
   ```sh
   node server.js
   ```
3. Open http://localhost:3000 in your browser to view the live UDP monitor.

File Overview
- `server.js`: Main server code (Express, UDP, Socket.IO).
- `commands.csv`: Command code mappings.
- `public/`: Static files for the web interface.

### Web UI Status Page

The `data/status.html` file provides a lightweight, browser-based UDP message monitor, designed to be served from the device’s web server.

Features
- Displays the last 10,000 UDP messages in a table.
- Auto-scrolls as new messages arrive.
- Shows details such as port, message ID, command, parameters, timestamps, speeds, pace, remaining time, and runtime.
- Includes a Copy feature for message data.

Usage
- Access the status page via the device’s web server (e.g., http://swimmachine.local/status.html or the device’s IP address).
- The page updates in real time as UDP messages are received by the device.

---

## UDP Message Formats

Communication between the client (controller/web UI) and the swim machine hardware is performed using fixed-size UDP messages:

### 44-byte message (Client → Machine)

| Byte(s) | Name         | Description                                                      |
|---------|--------------|------------------------------------------------------------------|
| 0       | Header       | Always 0x0A                                                      |
| 1       | Fixed        | Always 0xF0                                                      |
| 2       | msgId        | Message index/counter (idx2) must be between 0x64 and 0xC6       |
| 3       | cmd          | Command code (see `commands.csv`)                                |
| 4-5     | param        | Command parameter (uint16, little-endian)                        |
| 6-11    | param        | Additional parameters (not used in this project)                 |
| 12-31   | Reserved     | Zero-filled                                                      |
| 32-35   | timestamp    | Monotonic tick (uint32, little-endian, ms since boot)           |
| 36      | Footer       | Always 0x97                                                      |
| 37      | Footer2      | Always 0x01                                                      |
| 38-39   | Reserved     | Zero-filled                                                      |
| 40-43   | CRC32        | CRC-32 of bytes 0-39 (uint32, little-endian)                    |

All other bytes are zeroed. The message is sent to the swim machine to control its operation.

### 111-byte message (Machine → Client)

| Byte(s) | Name             | Description                                                      |
|---------|------------------|------------------------------------------------------------------|
| 0       | Fixed            | Always 0x0A                                                      |
| 1       | Fixed            | Always 0xF0                                                      |
| 2       | msgId            | Message index/counter echoed back (confirms receipt)            |
| 3       | cmd              | Status code (see `commands.csv`)                                 |
| 4       | curSpeed         | Current speed                                                    |
| 5       | tgtSpeed         | Target speed                                                     |
| 6       | not known        |                                                                  |
| 7-8     | pace             | Pace (seconds per 100m)                                          |
| 9-10    | not known        |                                                                  |
| 11-12   | remaining        | Remaining time in current segment                                |
| 13-22   | not known        |                                                                  |
| 23-26   | runtimeSec       | Runtime in seconds (float, little-endian)                        |
| 27-30   | totalRuntimeSec  | Total runtime in seconds (float, little-endian)                  |
| 31-70   | not known        |                                                                  |
| 71-74   | timestamp        | Timestamp (uint32, little-endian, seconds since epoch)           |
| 75-110  | Not known        |                                                                  |
| 107-110 | CRC32            | CRC-32 of bytes 0-106 (uint32, little-endian)                    |

- `pace` and `remaining` are encoded as two bytes: total seconds = byte0 + 256 * byte1, then formatted as mm:ss.
- Other fields may be present but are not used by the standard viewer.

Both message types use binary encoding for efficiency and are exchanged over the local network. The key distinction is:
- 44 bytes: Sent by the client to control the machine.
- 111 bytes: Sent by the machine to report status to the client.

The last 4 bytes of the 111-byte message contain a CRC32 checksum, calculated over bytes 0 to 106, ensuring data integrity.

---

## License

This project is licensed under the terms of the LICENSE file provided in this repository.
