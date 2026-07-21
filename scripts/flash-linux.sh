#!/usr/bin/env bash
#
# flash-linux.sh — one-shot INITIAL (USB serial) firmware flash for the
# Endless Pools Controller (ESP32-S3) on Linux.
#
# Why this exists: OTA only replaces the app image, it does NOT write the
# flash partition table. This project uses a custom partitions.csv (8MB flash,
# dual 3MB app slots + LittleFS), so the very first flash MUST be over USB.
# After this succeeds once, use scripts/ota_upload.py for future updates.
#
# What it does:
#   1. Verifies arduino-cli + the esp32 core are available (installs the core if missing).
#   2. Creates a local otapassword.h if you don't have one (the sketch #includes it,
#      so the build fails without it). The header is gitignored — never committed.
#   3. Auto-detects the ESP32-S3 USB serial port (or use --port).
#   4. Compiles with the correct FQBN (PartitionScheme=custom -> partitions.csv)
#      and writes bootloader + partition table + otadata + app over serial,
#      then reads the table back to verify it matches partitions.csv.
#
# Why the partition table matters: partitions.csv defines the two 3MB app slots
# (ota_0/ota_1). A board flashed without it has no second slot, so a later OTA
# dies with "Could Not Activate The Firmware". Use --erase to clear a stale one.
#
# Usage:
#   scripts/flash-linux.sh                         # autodetect port, build + flash
#   scripts/flash-linux.sh --port /dev/ttyUSB0     # explicit port
#   scripts/flash-linux.sh --ota-password 'secret' # set OTA password on first build
#   scripts/flash-linux.sh --build-only            # compile only, don't upload
#   scripts/flash-linux.sh --erase                 # full chip erase first (also wipes LittleFS)
#   scripts/flash-linux.sh --no-data               # firmware only, keep the device's filesystem
#   scripts/flash-linux.sh --no-verify             # skip the partition-table read-back
#   scripts/flash-linux.sh -y                       # don't prompt (CI / non-interactive)
#
# By default it also flashes data/ (web UI + data/workouts/*.json + wifi_config.json)
# into the LittleFS partition over USB, so a blank board is fully provisioned with
# no network or PSK needed. This replaces whatever is on the device's filesystem.
#
set -euo pipefail

# ---------- pretty logging ----------
info()  { printf '\033[0;36m[INFO]\033[0m %s\n'  "$*"; }
ok()    { printf '\033[0;32m[OK]\033[0m %s\n'    "$*"; }
warn()  { printf '\033[0;33m[WARN]\033[0m %s\n'  "$*" >&2; }
err()   { printf '\033[0;31m[ERROR]\033[0m %s\n' "$*" >&2; }
die()   { err "$*"; exit 1; }

# ---------- locate repo root (this file lives in repo_root/scripts/) ----------
SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="${SCRIPT_DIR%/scripts}"

# ---------- defaults ----------
FQBN_DEFAULT="esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=8M,PartitionScheme=custom"
ESP32_INDEX_URL="https://espressif.github.io/arduino-esp32/package_esp32_index.json"
# External Arduino libraries the sketch #includes (the esp32 core bundles WiFi/ETH/LittleFS/OTA/SPI).
REQUIRED_LIBS=(
  "ArduinoJson"
  "Async TCP"                                   # ESP32Async fork (NOT dvarrel "AsyncTCP")
  "ESP Async WebServer"                         # ESP32Async fork; IDF5-compatible (NOT lacamera "ESPAsyncWebServer")
  "Adafruit GFX Library"
  "ESP32 HUB75 LED MATRIX PANEL DMA Display"
)
BUILD_DIR="$REPO_ROOT/build/arduino"
OTA_HEADER="$REPO_ROOT/otapassword.h"

PARTITIONS_CSV="$REPO_ROOT/partitions.csv"
PORT=""
FQBN="$FQBN_DEFAULT"
OTA_PASSWORD=""
BUILD_ONLY=0
SKIP_CORE=0
ASSUME_YES=0
VERBOSE=0
ERASE=0
NO_VERIFY=0
WITH_DATA=1          # also flash data/ (web UI + workouts) into the LittleFS partition

# mklittlefs ships with the esp32 core; builds the filesystem image from data/.
find_mklittlefs() {
  local p
  p="$(ls -d "$HOME"/.arduino15/packages/esp32/tools/mklittlefs/*/mklittlefs 2>/dev/null | sort -V | tail -1)"
  if [ -n "$p" ] && [ -x "$p" ]; then echo "$p"; return 0; fi
  command -v mklittlefs >/dev/null 2>&1 && { command -v mklittlefs; return 0; }
  return 1
}

# Read the LittleFS/SPIFFS partition's offset+size straight out of partitions.csv.
read_fs_partition() {
  awk -F',' '!/^[[:space:]]*#/ && NF>=5 {
    gsub(/[ \t]/,"");
    if ($3=="spiffs" || $3=="littlefs") { print $4" "$5; exit }
  }' "$PARTITIONS_CSV"
}

# esptool ships with the esp32 core; used for --erase and partition verification.
find_esptool() {
  local p
  p="$(ls -d "$HOME"/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | sort -V | tail -1)"
  if [ -n "$p" ] && [ -x "$p" ]; then echo "$p"; return 0; fi
  for c in esptool esptool.py; do
    if command -v "$c" >/dev/null 2>&1; then command -v "$c"; return 0; fi
  done
  return 1
}

usage() { awk 'NR==1{next} /^#/{sub(/^# ?/,""); print; next} {exit}' "${BASH_SOURCE[0]}"; exit 0; }

# ---------- parse args ----------
while [ $# -gt 0 ]; do
  case "$1" in
    -p|--port)          PORT="${2:?--port needs a value}"; shift 2;;
    --fqbn)             FQBN="${2:?--fqbn needs a value}"; shift 2;;
    --ota-password)     OTA_PASSWORD="${2:?--ota-password needs a value}"; shift 2;;
    --build-only)       BUILD_ONLY=1; shift;;
    --erase)            ERASE=1; shift;;
    --no-verify)        NO_VERIFY=1; shift;;
    --no-data)          WITH_DATA=0; shift;;
    --skip-core)        SKIP_CORE=1; shift;;
    -y|--yes)           ASSUME_YES=1; shift;;
    -v|--verbose)       VERBOSE=1; shift;;
    -h|--help)          usage;;
    *) die "Unknown argument: $1 (try --help)";;
  esac
done

confirm() { # confirm "question" -> returns 0 if yes
  [ "$ASSUME_YES" = 1 ] && return 0
  local ans; read -r -p "$1 [y/N] " ans; [[ "$ans" =~ ^[Yy]$ ]];
}

# ---------- 1. prerequisites ----------
command -v arduino-cli >/dev/null 2>&1 || die \
"arduino-cli not found in PATH. Install it, then re-run:
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
  # then add ./bin (or the install dir) to your PATH
  Docs: https://arduino.github.io/arduino-cli/latest/installation/"

info "arduino-cli: $(arduino-cli version 2>/dev/null | head -1)"

if [ "$SKIP_CORE" = 0 ]; then
  if arduino-cli core list 2>/dev/null | grep -q '^esp32:esp32'; then
    info "esp32 core already installed."
  else
    info "Installing esp32 core (this can take a few minutes)…"
    arduino-cli core update-index --additional-urls "$ESP32_INDEX_URL"
    arduino-cli core install esp32:esp32 --additional-urls "$ESP32_INDEX_URL"
    ok "esp32 core installed."
  fi

  info "Ensuring required Arduino libraries are installed…"
  # arduino-cli lib install is idempotent; installs declared dependencies too.
  arduino-cli lib install "${REQUIRED_LIBS[@]}"
  ok "Libraries ready."
fi

# ---------- 2. otapassword.h (required for the build) ----------
if [ -f "$OTA_HEADER" ]; then
  info "Using existing otapassword.h"
  [ -n "$OTA_PASSWORD" ] && warn "otapassword.h already exists; --ota-password ignored (edit the file to change it)."
else
  if [ -z "$OTA_PASSWORD" ]; then
    OTA_PASSWORD="CHANGE_ME_$(od -An -N4 -tx1 /dev/urandom 2>/dev/null | tr -d ' \n' || echo deadbeef)"
    warn "No otapassword.h and no --ota-password given; generating a placeholder."
    warn "Edit $OTA_HEADER and re-flash to set a real OTA password."
  fi
  info "Creating otapassword.h (gitignored, never committed)…"
  cat > "$OTA_HEADER" <<EOF
#pragma once
#ifndef OTA_PASSWORD
#define OTA_PASSWORD "$OTA_PASSWORD"
#endif
EOF
  ok "Wrote $OTA_HEADER"
fi

# ---------- 3. serial port ----------
if [ -z "$PORT" ]; then
  info "Auto-detecting ESP32-S3 serial port…"
  preferred=""; others=()
  for dev in /dev/ttyUSB* /dev/ttyACM*; do
    [ -e "$dev" ] || continue
    vid=""
    command -v udevadm >/dev/null 2>&1 && \
      vid="$(udevadm info -q property -n "$dev" 2>/dev/null | sed -n 's/^ID_VENDOR_ID=//p')"
    case "$vid" in
      303a|10c4|1a86|0403) [ -z "$preferred" ] && preferred="$dev" || others+=("$dev");;  # Espressif/CP210x/CH340/FTDI
      *) others+=("$dev");;
    esac
  done
  if [ -n "$preferred" ]; then
    PORT="$preferred"
  elif [ "${#others[@]}" -eq 1 ]; then
    PORT="${others[0]}"
  elif [ "${#others[@]}" -gt 1 ]; then
    die "Multiple serial ports found (${others[*]}). Re-run with --port <dev>."
  else
    die "No serial device found under /dev/ttyUSB* or /dev/ttyACM*. Plug the board in and check the USB cable."
  fi
  ok "Using port: $PORT"
fi
[ -e "$PORT" ] || die "Port $PORT does not exist."

# permission sanity check
if [ ! -w "$PORT" ]; then
  warn "$PORT is not writable by you. You likely need to join the 'dialout' group:"
  warn "  sudo usermod -aG dialout \"\$USER\"   # then log out/in (or: newgrp dialout)"
fi

# ---------- 4. compile + upload ----------
V=(); [ "$VERBOSE" = 1 ] && V=(-v)

# ---------- partition table (partitions.csv) ----------
# The FQBN sets PartitionScheme=custom, so Arduino builds the partition binary
# straight from partitions.csv and the upload writes it to 0x8000. Without this
# table there is no second OTA slot, and OTA later fails with
# "Could Not Activate The Firmware".
[ -f "$PARTITIONS_CSV" ] || die "partitions.csv not found at $PARTITIONS_CSV (the FQBN uses PartitionScheme=custom and needs it)."
case "$FQBN" in
  *PartitionScheme=custom*) : ;;
  *) warn "FQBN does not request PartitionScheme=custom — partitions.csv will NOT be used!";;
esac
info "Custom partition table from partitions.csv (flashed to 0x8000):"
awk -F',' '!/^[[:space:]]*#/ && NF>=5 {
  gsub(/^[ \t]+|[ \t]+$/,"",$1); gsub(/^[ \t]+|[ \t]+$/,"",$2);
  gsub(/^[ \t]+|[ \t]+$/,"",$3); gsub(/^[ \t]+|[ \t]+$/,"",$4); gsub(/^[ \t]+|[ \t]+$/,"",$5);
  printf "         %-9s %-5s %-8s %-10s %s\n", $1,$2,$3,$4,$5 }' "$PARTITIONS_CSV"

info "Compiling  (FQBN: $FQBN)"
arduino-cli compile "${V[@]}" -b "$FQBN" \
  --build-path "$BUILD_DIR" --export-binaries "$REPO_ROOT"
ok "Build complete → $BUILD_DIR"

if [ "$BUILD_ONLY" = 1 ]; then
  ok "--build-only set; skipping upload."
  exit 0
fi

confirm "Flash $PORT now? This reboots the device and rewrites the partition table." || die "Aborted by user."

# --erase: wipe everything first. Use this to clear a stale/incompatible
# partition table or bad otadata (the state that makes OTA fail later).
if [ "$ERASE" = 1 ]; then
  ESPTOOL="$(find_esptool)" || die "esptool not found (needed for --erase); install the esp32 core first."
  warn "--erase wipes the ENTIRE flash, including LittleFS: web UI, workouts and wifi_config.json."
  warn "You will need to re-upload data/ afterwards (scripts/upload_http_data.py)."
  confirm "Erase all flash on $PORT?" || die "Aborted by user."
  info "Erasing flash …"
  "$ESPTOOL" --chip esp32s3 --port "$PORT" erase-flash
  ok "Flash erased."
fi

# Writes 4 regions: 0x0 bootloader, 0x8000 partition table (from partitions.csv),
# 0xe000 otadata/boot_app0, 0x10000 application.
info "Uploading over serial to $PORT …"
arduino-cli upload "${V[@]}" -p "$PORT" -b "$FQBN" \
  --input-dir "$BUILD_DIR" "$REPO_ROOT"

# Read the table back and prove it matches partitions.csv, so a later OTA
# can't fail with "Could Not Activate The Firmware" for lack of a second slot.
if [ "$NO_VERIFY" = 0 ]; then
  if ESPTOOL="$(find_esptool)"; then
    GEN="$(ls "$BUILD_DIR"/*.ino.partitions.bin 2>/dev/null | head -1)"
    if [ -n "$GEN" ]; then
      TMP="$(mktemp)"
      if "$ESPTOOL" --chip esp32s3 --port "$PORT" read-flash 0x8000 "$(stat -c%s "$GEN")" "$TMP" >/dev/null 2>&1 \
         && cmp -s "$GEN" "$TMP"; then
        ok "Partition table on device matches partitions.csv (verified at 0x8000)."
      else
        warn "Could not verify the partition table at 0x8000 (the device may have already rebooted)."
        warn "Skip this check with --no-verify."
      fi
      rm -f "$TMP"
    fi
  else
    warn "esptool not found; skipping partition-table verification."
  fi
fi

# ---------- filesystem: data/ (web UI + workouts) -> LittleFS partition ----------
# Done over USB from a mklittlefs image, so no network/PSK is needed and a blank
# board comes up fully provisioned (index.html, /workouts/*.json, wifi_config.json).
if [ "$WITH_DATA" = 1 ]; then
  if [ ! -d "$REPO_ROOT/data" ]; then
    warn "No data/ directory — skipping filesystem flash."
  else
    MKLFS="$(find_mklittlefs)" || die "mklittlefs not found (ships with the esp32 core). Use --no-data to skip."
    ESPTOOL="$(find_esptool)" || die "esptool not found (needed to write the filesystem). Use --no-data to skip."
    FS_LINE="$(read_fs_partition)"
    [ -n "$FS_LINE" ] || die "No spiffs/littlefs partition found in partitions.csv."
    FS_OFFSET="${FS_LINE%% *}"
    FS_SIZE="${FS_LINE##* }"

    N_FILES=$(find "$REPO_ROOT/data" -type f | wc -l)
    N_WORKOUTS=$(find "$REPO_ROOT/data/workouts" -type f -name '*.json' 2>/dev/null | wc -l)
    info "Filesystem: $N_FILES files from data/ (incl. $N_WORKOUTS workouts) -> $FS_OFFSET (size $FS_SIZE)"
    warn "This replaces the device's current LittleFS contents (workouts saved on the device will be overwritten)."
    if confirm "Flash data/ to the device filesystem?"; then
      FS_IMG="$BUILD_DIR/littlefs.bin"
      "$MKLFS" -c "$REPO_ROOT/data" -b 4096 -p 256 -s "$FS_SIZE" "$FS_IMG" >/dev/null
      ok "Built filesystem image ($(stat -c%s "$FS_IMG") bytes)"
      "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 921600 write-flash "$FS_OFFSET" "$FS_IMG"
      ok "Filesystem flashed (web UI + workouts)."
    else
      warn "Skipped filesystem flash."
    fi
  fi
fi
cat <<'NEXT'

Next steps
  1. The device boots into WiFi-setup (SoftAP) mode on first run:
     connect your phone/PC to WiFi network "swimmachine" (password 12345678),
     open http://192.168.4.1/wifi, and enter your home WiFi credentials.
  2. Once it's on your LAN (http://swimmachine.local), upload the web UI + workouts:
       python3 scripts/upload_http_data.py
  3. Future updates can go over the air:
       python3 scripts/ota_upload.py --target swimmachine.local --build
NEXT
