# Endless Pools Controller (ESP32-S3)

An ESP32-S3-based controller for a swim machine, featuring a web-based user interface, programmable workouts, and WiFi/Ethernet connectivity. This project allows you to manage, run, and monitor swim workouts from your phone or computer.

Note: I am not affiliated with Endless Pools.

Tested hardware: Waveshare ESP32-S3-ETH (W5500 over SPI). See board details and pinouts on the Waveshare wiki:
- https://www.waveshare.com/wiki/ESP32-S3-ETH

---

## Table of Contents

- [Introduction](#introduction)
- [Quick Install (one command)](#quick-install-one-command)
- [Swim Machine Protocol](#swim-machine-protocol)
- [Deploying to ESP32-S3 (Arduino IDE/CLI)](#deploying-to-esp32-s3-arduino-idecli)
  - [HUB75 LED Panel Wiring](#hub75-led-panel-wiring)
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

## Quick Install (one command)

For a fresh device, the easiest path is the all-in-one installer in `scripts/install.py`. It:

1. Asks what to run: **F**irmware only, **D**ata only, or **B**oth (default).
2. (Firmware) Auto-detects the ESP32-S3's USB serial port (Espressif USB VID `303A`) and asks you to confirm.
3. (Firmware) Compiles the firmware with Arduino CLI and flashes it over USB serial (this also writes the custom partition table — required before OTA can be used).
4. (Data) Probes `http://swimmachine.local` silently. If the device responds, the upload starts straight away. If not, prints a built-in WiFi setup tutorial and re-prompts for a base URL until the device is reachable (or you abort).
5. (Data) Uploads `data/` (Web UI + workouts) into LittleFS over HTTP.

Prerequisites
- Python 3.10+ (`py -3 --version` on Windows, `python3 --version` elsewhere).
- Arduino CLI on `PATH`, with the ESP32 core installed: `arduino-cli core install esp32:esp32`.
- ESP32-S3 connected via USB Type‑C.
- Stdlib only — no `pip install` needed.

Run it
- Windows (Command Prompt / PowerShell):
  ```
  py -3 scripts\install.py
  ```
- Windows (Git Bash / WSL) — use forward slashes, since Bash treats `\` as an escape:
  ```
  py -3 scripts/install.py
  ```
- macOS / Linux:
  ```
  python3 scripts/install.py
  ```

What you'll see
1. **Action prompt** — `[F]irmware only / [D]ata only / [B]oth`. Press Enter to accept the default (Both).
2. **(Firmware step)** The script lists detected serial ports and picks the Espressif one (e.g. `COM3` on Windows). If multiple are found you'll be asked to choose; if none look like Espressif you can pick any visible serial device. After confirming, it runs `scripts/serial_upload.py --port <COMx> --build` to compile + flash.
3. **(Data step)** The device reboots and is given a moment to come up. The script silently probes `http://swimmachine.local/wifi` (a route always served by the firmware, no LittleFS dependency). If it responds, the upload starts immediately.
4. **If the device is unreachable**, the script prints an inline **WiFi setup tutorial** and asks for a different URL:
   - Press Enter to retry `http://swimmachine.local` after fixing the connection.
   - Type `4` for `http://192.168.4.1` (the device's AP-mode address on first boot — connect your PC to the `swimmachine` WiFi network with password `12345678` first).
   - Or paste any URL/IP, e.g. `http://192.168.1.50`.

   The loop continues until the probe succeeds or you choose to abort.
5. Once the device is reachable, the script runs `scripts/upload_http_data.py` to push every file under `data/` into LittleFS via the device's `/api/upload` endpoint.

WiFi tutorial (what the script prints when it can't reach the device)

The same instructions are embedded in the script. Summary:

- **First install / device offline:** connect your phone or PC to the WiFi network named `swimmachine` (password `12345678`), open `http://192.168.4.1/wifi`, enter your home WiFi SSID + password, save. The device reboots onto your WiFi.
- **Device already on the LAN:** use `http://swimmachine.local` or the LAN IP shown on your router/serial log.
- **Change WiFi later:** from your LAN open `http://swimmachine.local/wifi` and re-submit the form.

Useful flags
- `--action firmware|data|both` — skip the action prompt.
- `--port COM3` — skip auto-detect.
- `--base http://192.168.4.1` — skip the auto-probe / prompt; use this URL (still probed; on failure the script falls back to the prompt loop).
- `--skip-firmware` — alias for `--action data`.
- `--skip-data` — alias for `--action firmware`.
- `-y` / `--yes` — don't ask to confirm the auto-detected port or final upload.

After the first install, subsequent firmware updates are typically done over the network (see [Over-the-Air (OTA) Updates](#over-the-air-ota-updates)) and data files via `scripts/upload_http_data.py` directly.

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
- Optional: HUB75 RGB LED matrix panel (64×64, 1/32 scan, single chained panel). Used to display workout status and a swimmer animation. The controller runs fine without it. See [HUB75 LED Panel Wiring](#hub75-led-panel-wiring) below.
- USB Type‑C cable.

Software (common)
- Filesystem: LittleFS for web UI and workout data storage.
- Web server: ESPAsyncWebServer + AsyncTCP (ESP32).
- JSON: ArduinoJson.
- mDNS: ESPmDNS (included with ESP32 Arduino core).
- HUB75 driver (optional, only if a panel is connected): ESP32-HUB75-MatrixPanel-I2S-DMA (mrfaptastic/Adafruit GFX-compatible).

Note on Ethernet: This firmware uses the Arduino Ethernet support for SPI W5500 (ETH.h). No RMII/PHY configuration is needed for ESP32-S3-ETH since Ethernet is via W5500 on SPI. The default W5500 pin definitions in `ConnectionManager.h`/`NetworkSetup.h` match the Waveshare board:
- ETH_TYPE=ETH_PHY_W5500
- ETH_CS=14, ETH_IRQ=10, ETH_RST=9, ETH_SPI_SCK=13, ETH_SPI_MISO=12, ETH_SPI_MOSI=11

Reference: https://www.waveshare.com/wiki/ESP32-S3-ETH

### HUB75 LED Panel Wiring

The HUB75 LED matrix panel is OPTIONAL — the controller works without it. If no panel is attached, the firmware still runs normally; the on-panel display code simply has nothing to drive. Skip this section if you don't want a status display.

The firmware drives a 64×64 1/32-scan RGB matrix panel using the [ESP32-HUB75-MatrixPanel-I2S-DMA](https://github.com/mrfaptastic/ESP32-HUB75-MatrixPanel-I2S-DMA) library. It shows the current/upcoming workout segments (time, meters, pace) when running a workout and a swimmer animation when idle.

Panel configuration (see `hub75.cpp`):
- Width: 64, Height: 64, Chain length: 1
- Scan type: 1/32 (E line is required for 64-row panels)

GPIO mapping (ESP32-S3 → HUB75 connector):

| HUB75 signal | ESP32-S3 GPIO | Purpose                          |
|--------------|---------------|----------------------------------|
| R1           | GPIO21        | Red, top half                    |
| G1           | GPIO17        | Green, top half                  |
| B1           | GPIO16        | Blue, top half                   |
| R2           | GPIO18        | Red, bottom half                 |
| G2           | GPIO15        | Green, bottom half               |
| B2           | GPIO3         | Blue, bottom half                |
| A            | GPIO40        | Row select bit 0                 |
| B            | GPIO41        | Row select bit 1                 |
| C            | GPIO42        | Row select bit 2                 |
| D            | GPIO45        | Row select bit 3 (see note below)|
| E            | GPIO39        | Row select bit 4 (64-row panels) |
| CLK          | GPIO46        | Pixel clock                      |
| LAT (STB)    | GPIO47        | Latch / strobe                   |
| OE           | GPIO48        | Output enable (active low)       |
| GND          | GND           | Connect ALL HUB75 GND pins       |

The 16-pin HUB75 IDC connector is laid out (looking at the back of the panel, with the keying/notch up):

```
   R1  | G1
   B1  | GND
   R2  | G2
   B2  | E
    A  | B
    C  | D
  CLK  | LAT
   OE  | GND
```

Note on the D pin
- Some HUB75 panels (mostly 1/8 or 1/16 scan, but also some mislabeled or older 1/32 boards) silkscreen the D pin position as `GND`. If you see what looks like *three* GND pins on the connector instead of the usual two, the third one is almost certainly D.
- To verify with a multimeter: with the panel powered off, set the meter to continuity and check each pin labeled "GND" against a known-good GND (e.g. the ground pad of the power input). The pin that does **not** beep / does not show continuity to other grounds is the D line — wire that to GPIO45.
- If you wire a real GND pin to GPIO45 by mistake, the panel will only display the top half (rows 0–31) of a 64-row panel. That symptom is the giveaway.

Power
- HUB75 panels are 5 V. A 64×64 panel can pull several amps at full white; supply it from a dedicated 5 V PSU sized for your panel (do NOT power it from the ESP32 board's 5 V rail).
- Tie panel ground and ESP32 ground together. The data lines are 3.3 V from the ESP32-S3 — most HUB75 panels accept this directly; if yours latches unreliably, add a 74AHCT245 level shifter on R1/G1/B1/R2/G2/B2/A/B/C/D/E/CLK/LAT/OE.

Brightness
- Brightness (0–100%) is persisted in `/settings.json` on LittleFS and exposed via the web UI. See `HUB75_setBrightnessPercent()` in `hub75.h`.

Required library
- Install via Library Manager: "ESP32 HUB75 LED MATRIX PANEL DMA Display" (mrfaptastic).

### Arduino IDE (ESP32-S3-ETH settings)

![ESP32-S3-ETH (Waveshare) W5500 config](ESP32S3ETH%20Waveshare%20W5500%20config.png)

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
- Flash Size: 8MB
- Partition Scheme: Custom (uses partitions.csv)
- PSRAM: OPI PSRAM (8MB)
- CPU Frequency, Upload Speed: defaults are fine unless you need changes

Build and upload (USB)
- Open `endless-pools-controller.ino`
- Select board and port
- Click Upload

### Arduino CLI (FQBN and OTA)

Helpers:
- scripts/ota-upload.bat: Windows helper to compile with Arduino CLI and upload via OTA. If you provide a sketch.yaml, the script will read default_fqbn/default_port/ota_password from it; otherwise specify target and options explicitly.
- scripts/ota-upload.ps1 and scripts/ota_upload.py: alternative helpers for PowerShell/Python.
- scripts/ota-upload.sh and scripts/serial-upload.sh: Bash helpers for Linux/macOS/WSL that wrap the Python scripts. Make them executable once with: `chmod +x scripts/*.sh`.

ESP32-S3 Dev Module FQBN (example)
- The exact option keys can vary by ESP32 core version. A typical FQBN with the requested settings looks like:
```
esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi
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
- If `sketch.yaml` is present, the script reads its settings; otherwise pass target/profile explicitly or build first. It builds to `build\arduino`, then uploads over the network using Arduino CLI’s OTA.

Using the helper script (Bash: Linux/macOS/WSL)
- Prerequisite: Install Arduino CLI and the ESP32 core (e.g. `arduino-cli core install esp32:esp32`).
- Optional: Configure your WiFi/Ethernet so the device is reachable by hostname or IP (default OTA service port: 3232).
- To compile and OTA upload in one step:
```
./scripts/ota-upload.sh 192.168.1.50
```
- If your device advertises mDNS and your network supports it, you can use:
```
./scripts/ota-upload.sh swimmachine.local
```
- Optionally select the explicit profile defined in `sketch.yaml`:
```
./scripts/ota-upload.sh 192.168.1.50 esp32s3-eth
```

Running the Python scripts directly (Windows/macOS/Linux)
- Prerequisites:
  - Install Python 3.10+.
  - Install Arduino CLI and the ESP32 core (e.g. `arduino-cli core install esp32:esp32`) if you want the scripts to build/upload.
- Verify Python:
  - Windows (Command Prompt): `py -3 --version`
  - PowerShell: `py -3 --version`
  - macOS/Linux: `python3 --version`

Windows (Command Prompt)
- Serial (first flash, flashes custom partitions):
```
py -3 scripts\serial_upload.py --port COM5 --build
```
- OTA (build if needed, then upload):
```
py -3 scripts\ota_upload.py --target 192.168.1.50 --build
```

Notes for Windows:
- If `python` opens Microsoft Store or shows “Python was not found”, prefer using the Python Launcher: `py -3 ...`.
- Alternatively, install Python from https://www.python.org/downloads/ and check “Add python.exe to PATH”.
- Or disable App execution aliases: Settings > Apps > Advanced app settings > App execution aliases (turn off Python entries).

PowerShell
- Serial:
```
py -3 .\scripts\serial_upload.py --port COM5 --build
```
- OTA:
```
py -3 .\scripts\ota_upload.py --target swimmachine.local --build
```

macOS / Linux
- Serial:
```
python3 scripts/serial_upload.py --port /dev/ttyUSB0 --build
```
- OTA:
```
python3 scripts/ota_upload.py --target swimmachine.local --build
```

WSL / Git Bash
- You can use `python3` (if installed in your environment).
- The serial script accepts `/dev/ttyS{n}` and will map it to `COM{n+1}` automatically on Windows.

Notes
- The Python scripts only use the standard library.
- For OTA, ensure the Arduino ESP32 core is installed; the script auto-detects `espota.py` on Windows, Linux (`~/.arduino15`), and macOS (`~/Library/Arduino15`).

Initial flash over serial from Bash (sets custom partitions)
- Linux (example):
```
./scripts/serial-upload.sh --port /dev/ttyUSB0 --build
```
- macOS (example):
```
./scripts/serial-upload.sh --port /dev/cu.usbserial-0001 --build
```

Notes
- If you get a permission error running the .sh files, mark them executable:
```
chmod +x scripts/*.sh
```
- On Windows Git Bash, you can still use the Bash helpers; the serial script will map /dev/ttyS{n} to COM{n+1} automatically.

---

## Initial Flash (Serial) vs Subsequent OTA Updates

Why a first serial flash?
- OTA (espota.py/Arduino OTA) only replaces the application image; it does not change the flash partition table.
- This project uses a custom partitions.csv for 8MB flash with 3MB APP (dual slots) and 1.5MB SPIFFS.
- Therefore, you must perform one serial/USB flash to install the custom partition scheme. After that, use OTA for future updates.

First flash (serial) options

Option A – Arduino IDE (USB)
1. Tools > Board: esp32 > ESP32S3 Dev Module
2. Tools options (recommended for Waveshare ESP32-S3-ETH):
   - USB Mode: Hardware CDC and JTAG
   - USB CDC On Boot: Enabled
   - Flash Size: 8MB
   - Partition Scheme: Custom (uses partitions.csv)
   - PSRAM: OPI PSRAM (8MB)
3. Connect the board via USB, select the COM port under Tools > Port.
4. Sketch > Upload.
5. Optional: Upload the contents of the `data/` folder to LittleFS using the LittleFS upload tool (see “Uploading Data Files” below).

Option B – Windows helper scripts (USB serial)
- Batch:
  - scripts\ota-upload.bat COM5
- PowerShell:
  - .\scripts\ota-upload.ps1 COM5
Notes:
- Replace COM5 with your actual serial port.
- These helpers pass the required FQBN options (FlashSize=8M, PartitionScheme=custom) so the custom partition table is flashed.

Option C – Arduino CLI (USB serial)
- Compile with custom FQBN and export binaries:
  - arduino-cli compile -b "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi" --build-path build\arduino --export-binaries .
- Upload over serial (replace COM5 with your port):
  - arduino-cli upload -p COM5 -b "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi" --input-dir build\arduino

Subsequent OTA updates (after first serial flash)

Option A – Windows batch helper
- scripts\ota-upload.bat 192.168.1.50
- scripts\ota-upload.bat swimmachine.local
- Optional profile (if using sketch.yaml profiles):
  - scripts\ota-upload.bat 192.168.1.50 esp32s3-eth

Option B – PowerShell helper
- .\scripts\ota-upload.ps1 192.168.1.50
- .\scripts\ota-upload.ps1 swimmachine.local

Option C – Python helper (builds if needed, then OTA uploads)
- python scripts\ota_upload.py --target 192.168.1.50 --build
- python scripts\\ota_upload.py --target swimmachine.local --build
- If a binary already exists (build\arduino\*.ino.bin), you can omit --build.

OTA upload and data refesh
python scripts\\ota_upload.py --target swimmachine.local --build
python scripts\\upload_http_data.py

Passwords and defaults
- OTA service port: 3232 (default).
- OTA password:
  - Batch/PowerShell helpers read ota_password from sketch.yaml (if present) and also inject it at compile time.
  - Python helper accepts --password or reads from sketch.yaml if present; when building it injects -DOTA_PASSWORD="...".
- Target host:
  - If not provided, helpers try to use default_port from sketch.yaml.

Troubleshooting
- If OTA fails immediately: verify the device is reachable (IP/mDNS), the OTA password matches, and the service port (3232) is open on your network.
- If the device was previously flashed with a different partition table: perform one serial upload again to reapply the custom table, then resume OTA.

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

Notes
- You can also upload via the device’s HTTP API using the provided Python helper (see below).
- Authentication: uploads require a PSK equal to the first 10 characters of OTA_PASSWORD (defined in local otapassword.h). The Python helper auto-derives this PSK by reading otapassword.h unless you override it.

### Uploading via HTTP (Python tool)

A lightweight uploader is provided at `tools/upload_http_data.py`. It walks a local directory (default: `data/`) and uploads each file, preserving relative paths, to the device’s LittleFS via the `/api/upload` endpoint.

Prerequisites
- Python 3.10+ installed
- Device reachable by hostname (mDNS) or IP (e.g., http://swimmachine.local or http://192.168.1.50)
- The device must be running this firmware (which exposes `/api/upload`)

Basic usage (auto-derives PSK from `otapassword.h`)
```
python tools/upload_http_data.py --base http://swimmachine.local --dir data
```

Using an IP instead of mDNS:
```
python tools/upload_http_data.py --base http://192.168.1.50 --dir data
```

Override PSK (optional)
```
python tools/upload_http_data.py --base http://swimmachine.local --dir data -k YOUR_PSK
```

Dry-run (list what would be uploaded, but don’t send)
```
python tools/upload_http_data.py --base http://swimmachine.local --dir data --dry-run
```

What it does
- For each file under `--dir`, computes a relative path and uploads to `/api/upload?path=RELATIVE/PATH`
  - Example: `data/static/app.js` → remote path `static/app.js`
  - Example: `data/index.html` → remote path `index.html`
- Sends raw file bytes with headers:
  - `X-PSK: <pre-shared-key>` (first 10 chars of OTA_PASSWORD)
  - `Content-Type: application/octet-stream`
- Creates subfolders automatically on the device as needed

Single-file upload via curl (alternative)
```
curl -X POST --data-binary @data/index.html \
  -H "X-PSK: $(python -c "import re;print(re.search(r'OTA_PASSWORD\\s+\\\"(.*?)\\\"',open('otapassword.h').read()).group(1)[:10])")" \
  -H "Content-Type: application/octet-stream" \
  "http://swimmachine.local/api/upload?path=index.html"
```

Troubleshooting
- 401 Unauthorized: PSK mismatch. Ensure the PSK equals the first 10 characters of `OTA_PASSWORD` in `otapassword.h`, or pass `-k` explicitly.
- Connection errors: verify the device is on your network and the URL/hostname is reachable.
- After uploading, hard-refresh your browser to avoid cached old assets (Ctrl+F5/Shift+Reload).

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
- Connect your phone or computer to the WiFi network named `swimmachine` (password `12345678`).
- Open a browser and go to http://192.168.4.1
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

### Node.js Viewer

- Install Node.js 18+ and dependencies:
  - cd viewer
  - npm install
- Run the viewer:
  - node server.js
- Open http://localhost:3000
- It listens for UDP on:
  - 9750 (44-byte control messages)
  - 45654 (111-byte status messages)
  These are parsed and streamed to the browser via Socket.IO.

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
