# Endless Pools Controller

An Arduino/ESP8266-based controller for a swim machine, featuring a web-based user interface, programmable workouts, and WiFi connectivity. This project allows you to manage, run, and monitor swim workouts from your phone or computer.

---

## Table of Contents

- [Introduction](#introduction)
- [Swim Machine Protocol](#swim-machine-protocol)
- [Deploying the Code to Arduino/ESP8266](#deploying-the-code-to-arduinoesp8266)
- [Uploading Data Files (Web UI, Workouts, etc.)](#uploading-data-files-web-ui-workouts-etc)
- [User Manual](#user-manual)
  - [WiFi Setup](#wifi-setup)
  - [Connecting to the Webserver](#connecting-to-the-webserver)
  - [Managing Workouts](#managing-workouts)
- [Tips: Preventing Phone Screen Lock (Guided Access)](#tips-preventing-phone-screen-lock-guided-access)
- [Viewer and UDP Monitor](#viewer-and-udp-monitor)
  - [Node.js Viewer](#nodejs-viewer)
  - [Web UI Status Page](#web-ui-status-page)
- [License](#license)

---

## Introduction

This project is a smart controller for a swim machine, built on the ESP8266 microcontroller. It provides:
- A web-based interface for configuring and running workouts.
- WiFi connectivity for easy access from any device.
- Persistent storage of workouts and preferences.
- Real-time monitoring and control of the swim machine.

---

## Swim Machine Protocol

The swim machine is controlled via a state machine and communicates over UDP. The protocol was reverse engineered and rewritten.

### UDP Message Formats

Communication between the client (controller/web UI) and the swim machine hardware is performed using fixed-size UDP messages:

#### 44-byte message (Client → Machine)

| Byte(s) | Name         | Description                                                      |
|---------|--------------|------------------------------------------------------------------|
| 0       | Header       | Always 0x0A                                                      |
| 1       | Fixed        | Always 0xF0                                                      |
| 2       | msgId        | Message index/counter (idx2) must be between 0x64 and 0xC6       |
| 3       | cmd          | Command code (see `commands.csv`)                                |
| 4-5     | param        | Command parameter (uint16, little-endian)                        |

| 6-7     | param        | Additional command parameter (uint16, little-endian), not used in this project |
| 8-9     | param        | Additional command parameter (uint16, little-endian), not used in this project |
| 10-11   | param        | Additional command parameter (uint16, little-endian), not used in this project |
| 12-31   | Reserved     | Zero-filled                                                      |
| 32-35   | timestamp    | Monotonic tick (uint32, little-endian, ms since boot)            |
| 36      | Footer       | Always 0x97                                                      |
| 37      | Footer2      | Always 0x01                                                      |
| 38-39   | Reserved     | Zero-filled                                                      |
| 40-43   | CRC32        | CRC-32 of bytes 0-39 (uint32, little-endian)                     |

All other bytes are zeroed. The message is sent to the swim machine to control its operation.

#### 111-byte message (Machine → Client)

| Byte(s) | Name             | Description                                                      |
|---------|------------------|------------------------------------------------------------------|
| 0       | Fixed            | Always 0x0A                                                     |
| 1       | Fixed            | Always 0xF0                                                     |
| 2       | msgId            | Message index/counter echoed back, feedback that message was received |
| 3       | cmd              | Status code (see `commands.csv`)                                |
| 4       | curSpeed         | Current speed                                                    |
| 5       | tgtSpeed         | Target speed                                                     |
| 6       | not known       |                                                                   |
| 7-8     | pace             | Pace (seconds per 100m)                                          |
| 9-10    | not known       |                                                                  |
| 11-12   | remaining        | Remaining time in current segment                              |
| 13-22   | not known       |                                                                  |
| 23-26   | runtimeSec       | Runtime in seconds (float, little-endian)                        |
| 27-30   | totalRuntimeSec  | Total runtime in seconds (float, little-endian)                  |
| 31-70   | not known       |                                                                  |
| 71-74   | timestamp        | Timestamp (uint32, little-endian, seconds since epoch)           |
| 75-110  | Not known        |                                                                  |
| 107-110 | CRC32            | CRC-32 of bytes 0-106 (uint32, little-endian)                    |

- **pace** and **remaining** are encoded as two bytes: total seconds = byte0 + 256 * byte1, then formatted as mm:ss.
- Other fields may be present but are not used by the standard viewer.

Both message types use binary encoding for efficiency and are exchanged over the local network. The key distinction is:
- **44 bytes:** Sent by the client to control the machine.
- **111 bytes:** Sent by the machine to report status to the client.

The last 4 bytes of the 111-byte message contain a CRC32 checksum, calculated over bytes 0 to 106, ensuring data integrity.

### Workout Structure

- **Segment**: Each workout consists of one or more segments.
  - `pace100s`: Pace in seconds per 100 meters (0 = rest).
  - `durSec`: Duration of the segment in seconds.

### State Machine & API

- **Initialization**: `SwimMachine::begin()` sets up the protocol and network event handling.
- **Workout Control**:
  - `loadWorkout(segments)`: Load a new workout (list of segments).
  - `start()`: Begin the workout.
  - `pause()`: Pause or resume the workout.
  - `stop()`: Abort the workout.
  - `tick()`: Call regularly in the main loop to advance the state machine.
- **Status**: `getStatus()` returns the current state (active, paused, current segment, elapsed time).
- **Networking**: `setPeerIP(ip)` sets the peer for UDP communication.

---

## Deploying the Code to Arduino/ESP8266

1. **Hardware Requirements**
   - ESP8266-based board (e.g., NodeMCU, Wemos D1 Mini).
   - Swim machine hardware (relay/motor control, sensors as needed).

2. **Software Requirements**
   - [Arduino IDE](https://www.arduino.cc/en/software)
   - ESP8266 board support (install via Arduino Boards Manager).
   - Required libraries:
     - `ESPAsyncWebServer`
     - `ESPAsyncTCP`
     - `LittleFS`
     - `ArduinoJson`
     - `ESP8266WiFi`
     - `ESP8266mDNS`

3. **Uploading the Firmware**
   - Open `endless-pools-controller.ino` in Arduino IDE.
   - Select the correct ESP8266 board and port.
   - Install all required libraries via Library Manager.
   - Click **Upload** to flash the firmware.

---

## Uploading Data Files (Web UI, Workouts, etc.)

The ESP8266 uses LittleFS to store web UI files and workout data.

1. **Install the LittleFS Data Upload Tool**
   - Download and install the [ESP8266 LittleFS Data Upload tool](https://github.com/earlephilhower/arduino-esp8266littlefs-plugin).
   - Web UI files (`index.html`, `run.html`, `status.html`, `/static` assets, and `/workouts` JSON files) are in the `data/` directory.

2. **Upload Files to the Device**
   - In Arduino IDE, select **Tools > ESP8266 LittleFS Data Upload**.
   - This will upload all files from the `data/` directory to the ESP8266's filesystem.

---

## User Manual

### WiFi Setup

- **First Boot or WiFi Change**:
  - If the device cannot connect to a stored WiFi network, it will start in Access Point (AP) mode.
  - Connect your phone or computer to the WiFi network named `SwimMachine_Config`.
  - Open a browser and go to [http://192.168.0.3](http://192.168.0.3).
  - Enter your WiFi SSID and password, then save. The device will reboot and connect to your network.

- **Normal Operation**:
  - The device will attempt to connect to the last used WiFi network on boot.
  - If successful, it will be accessible on your local network.

### Connecting to the Webserver

- Once connected to WiFi, access the web UI:
  - By device hostname: [http://swimmachine.local](http://swimmachine.local) (if your device supports mDNS).
  - Or by device IP address (check serial output or your router's device list).

- The web UI allows you to:
  - View and manage workouts.
  - Start, pause, or stop workouts.
  - Monitor swim machine status in real time.

### Managing Workouts

- Workouts are stored as JSON files in `/data/workouts/`.
- Two example workout files are provided in this folder: `1750867563160.json` and `1750942801592.json`. You can use these as templates for your own workouts.
- You can add, edit, or delete workouts via the web UI.
- Workouts can also be uploaded directly to the device using the LittleFS Data Upload tool.

---

## Tips: Preventing Phone Screen Lock (Guided Access)

To keep your phone screen on while using the web UI during a workout, use the following features:

### iOS (iPhone/iPad)

- **Guided Access**:
  1. Go to **Settings > Accessibility > Guided Access** and enable it.
  2. Set a passcode if prompted.
  3. Open Safari and navigate to the swim machine web UI.
  4. Triple-click the side or home button to start Guided Access.
  5. The screen will stay on and locked to the web UI.

- **Auto-Lock**:
  - Go to **Settings > Display & Brightness > Auto-Lock** and set to "Never" (remember to change back after your workout).

### Android

- **Screen Pinning**:
  1. Go to **Settings > Security > Screen pinning** and enable it.
  2. Open Chrome and navigate to the swim machine web UI.
  3. Tap the Overview button, then the app icon, and select "Pin".
  4. The screen will stay on and locked to the web UI.

- **Keep Screen On**:
  - Go to **Settings > Display > Sleep** and set to a longer duration or "Never" (if available).

---

## Viewer and UDP Monitor

This project includes two tools for monitoring UDP messages from the swim machine: a Node.js-based viewer and a web-based status page.

### Node.js Viewer

The `viewer` directory contains a Node.js application that listens for UDP messages from the swim machine and provides a real-time web interface for monitoring and analyzing these messages.

**Features:**
- Receives and parses UDP packets on ports 9750 and 45654.
- Decodes commands using `commands.csv`.
- Serves a web interface via Express and Socket.IO for real-time updates.
- Displays message details such as command, parameters, timestamps, and more.

**Usage:**
1. Install dependencies:
   ```sh
   cd viewer
   npm install
   ```
2. Start the server:
   ```sh
   node server.js
   ```
3. Open [http://localhost:3000](http://localhost:3000) in your browser to view the live UDP monitor.

**File Overview:**
- `server.js`: Main server code (Express, UDP, Socket.IO).
- `commands.csv`: Command code mappings.
- `public/`: Static files for the web interface.

### Web UI Status Page

The `data/status.html` file provides a lightweight, browser-based UDP message monitor, designed to be served from the ESP8266's web server.

**Features:**
- Displays the last 10,000 UDP messages in a table.
- Auto-scrolls as new messages arrive.
- Shows details such as port, message ID, command, parameters, timestamps, speeds, pace, remaining time, and runtime.
- Includes a "Copy" feature for message data.

**Usage:**
- Access the status page via the device's web server (e.g., [http://swimmachine.local/status.html](http://swimmachine.local/status.html) or the device's IP address).
- The page updates in real time as UDP messages are received by the device.

---

## License

This project is licensed under the terms of the LICENSE file provided in this repository.
