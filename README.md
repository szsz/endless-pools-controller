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

## License

This project is licensed under the terms of the LICENSE file provided in this repository.
