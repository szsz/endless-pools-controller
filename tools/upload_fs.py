#!/usr/bin/env python3
"""
Upload LittleFS contents to ESP32/ESP32-S3 via:
- Serial/COM (write LittleFS image straight to the filesystem partition using esptool.py)
- OTA (best effort: uses espota.py --spiffs; works on ESP8266; for ESP32 this requires firmware support)

Defaults and auto-detection:
- Data directory: ./data
- Partition label used by firmware: "spiffs" (see LittleFS.begin(..., "spiffs"))
- Partition offset/size:
    1) If --offset/--size provided, use those
    2) Else try to parse ./serial.log for "spiffs : addr" and "size"
    3) Else (serial mode only) read partition table (0x8000) via esptool and parse entries
- LittleFS image creation: uses mklittlefs (auto-discovered under Arduino15 packages). Override with --mklittlefs.

Examples:
  Serial/COM (auto-detect FS partition from serial.log or partition table):
    python tools/upload_fs.py --port COM8 --chip esp32s3 --baud 921600

  Serial/COM with explicit params:
    python tools/upload_fs.py --port COM8 --offset 0x00310000 --size 896K

  OTA (requires firmware support for FS OTA on ESP32; ESP8266 works):
    python tools/upload_fs.py --ota 192.168.4.1 --auth mypass --size 896K

  Only build a LittleFS image (no upload):
    python tools/upload_fs.py --build-only --size 896K

Notes:
- Block/page defaults for mklittlefs are set to 4096/256 to match ESP defaults.
- For ESP32, standard ArduinoOTA updates only the app image. FS OTA needs explicit firmware support.
"""

import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Optional, Tuple

# ---------- Constants ----------
DEFAULT_LABEL = "spiffs"  # matches firmware's LittleFS.begin(..., "spiffs")
DEFAULT_DATA_DIR = "data"
DEFAULT_BAUD = 921600
DEFAULT_CHIP = "esp32s3"
DEFAULT_OTA_PORT = 3232
MKLITTLEFS_BLOCK = 4096
MKLITTLEFS_PAGE = 256

PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_LENGTH = 0x1000  # 4KB is typical, 0xC00 also common


# ---------- Utilities ----------
def log(msg: str):
    print(msg, flush=True)


def err(msg: str):
    print(f"[ERROR] {msg}", file=sys.stderr, flush=True)


def parse_size_arg(s: str) -> int:
    """
    Parse sizes like "896K", "1.5M", "1536000" (bytes).
    """
    s = s.strip().upper()
    m = re.fullmatch(r"(\d+(?:\.\d+)?)([KMG]?)", s)
    if not m:
        raise ValueError(f"Invalid size: {s}")
    val = float(m.group(1))
    unit = m.group(2)
    if unit == "K":
        val *= 1024
    elif unit == "M":
        val *= 1024 * 1024
    elif unit == "G":
        val *= 1024 * 1024 * 1024
    return int(val)


def human_bytes(n: int) -> str:
    for unit in ["B", "KB", "MB", "GB"]:
        if n < 1024.0 or unit == "GB":
            return f"{n:.1f} {unit}" if unit != "B" else f"{n} {unit}"
        n /= 1024.0
    return f"{n} B"


def find_arduino15_dir() -> Optional[Path]:
    """
    Try to locate Arduino15 dir cross-platform.
    Windows: typically C:\\Users\\<User>\\AppData\\Local\\Arduino15
    Note: When running Python from the Microsoft Store, LOCALAPPDATA may point under ...\\Packages\\...\\LocalCache;
    in that case we climb up parent directories to reach the real AppData\\Local.
    Linux: ~/.arduino15
    macOS: ~/Library/Arduino15
    """
    home = Path.home()

    # Windows
    if os.name == "nt":
        env_p = os.environ.get("LOCALAPPDATA")
        bases = []
        if env_p:
            p = Path(env_p)
            # Try the reported LOCALAPPDATA path itself
            bases.append(p)
            # Climb up to escape possible Packages/LocalCache redirection (up to 5 levels)
            q = p
            for _ in range(5):
                q = q.parent
                bases.append(q)
        # Also try the canonical user AppData\\Local path
        bases.append(home / "AppData" / "Local")

        # Probe for Arduino15 under each base
        for b in bases:
            cand = b / "Arduino15"
            try:
                if cand.exists():
                    return cand
            except Exception:
                # Ignore permission/path errors and continue
                pass

    # Linux and macOS
    for candidate in [
        home / ".arduino15",
        home / "Library" / "Arduino15",  # macOS
    ]:
        if candidate.exists():
            return candidate
    return None


def find_arduinoide_plugins_dir() -> Optional[Path]:
    """
    Locate Arduino IDE plugins dir where the LittleFS plugin may install mklittlefs:
    Windows: C:\\Users\\<User>\\.arduinoIDE\\plugins
    macOS: ~/.arduinoIDE/plugins or ~/Library/Application Support/arduino-ide/plugins
    Linux: ~/.arduinoIDE/plugins
    """
    home = Path.home()
    candidates = []
    if os.name == "nt":
        candidates.append(home / ".arduinoIDE" / "plugins")
    else:
        candidates.append(home / ".arduinoIDE" / "plugins")
        # Legacy macOS location for some IDE builds
        candidates.append(home / "Library" / "Application Support" / "arduino-ide" / "plugins")
    for c in candidates:
        if c.exists():
            return c
    return None


def find_executable(name: str) -> Optional[Path]:
    """
    Return full path to executable if found on PATH.
    """
    p = shutil.which(name)
    return Path(p) if p else None


def find_mklittlefs(explicit: Optional[str]) -> Optional[Path]:
    if explicit:
        p = Path(explicit)
        if p.exists():
            return p
        err(f"mklittlefs not found at: {explicit}")
        return None

    # Try PATH first
    on_path = find_executable("mklittlefs.exe" if os.name == "nt" else "mklittlefs")
    if on_path:
        return on_path

    # Search Arduino IDE plugins (.arduinoIDE/plugins) for mklittlefs tool
    ide_plugins = find_arduinoide_plugins_dir()
    if ide_plugins:
        for exe in ide_plugins.rglob("mklittlefs*"):
            if exe.is_file() and "mklittlefs" in exe.name.lower():
                return exe

    # Search Arduino15 packages for mklittlefs tool
    ar15 = find_arduino15_dir()
    if ar15:
        # Search recursively but limit depth to avoid long scans
        for tools_dir in (ar15 / "packages").glob("*/*/tools/*"):
            # e.g., .../packages/esp32/tools/mklittlefs/ or .../packages/esp8266/tools/mklittlefs/
            for exe in tools_dir.rglob("mklittlefs*"):
                if exe.is_file() and "mklittlefs" in exe.name.lower():
                    return exe

    return None


def find_espota(explicit: Optional[str]) -> Optional[Path]:
    if explicit:
        p = Path(explicit)
        if p.exists():
            return p
        err(f"espota.py not found at: {explicit}")
        return None

    # Search PATH for espota.py
    on_path = find_executable("espota.py")
    if on_path:
        return on_path

    # Arduino15 typical locations
    ar15 = find_arduino15_dir()
    if ar15:
        for tools_dir in (ar15 / "packages").glob("*/*/tools/*"):
            for py in tools_dir.rglob("espota.py"):
                if py.is_file():
                    return py
    return None


def ensure_data_dir(path: Path) -> None:
    if not path.exists():
        raise FileNotFoundError(f"Data directory not found: {path}")
    if not path.is_dir():
        raise NotADirectoryError(f"Not a directory: {path}")


def read_serial_log_for_spiffs(log_path: Path, label: str) -> Optional[Tuple[int, int]]:
    """
    Parse serial.log lines:
      spiffs : addr: 0x00310000, size:   896.0 KB, type: DATA, subtype: SPIFFS
    Return (offset, size) in bytes if found.
    """
    if not log_path.exists():
        return None
    addr_re = re.compile(rf"{re.escape(label)}\s*:\s*addr:\s*(0x[0-9a-fA-F]+),\s*size:\s*([0-9\.]+)\s*([KMG]B)", re.IGNORECASE)
    with log_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = addr_re.search(line)
            if m:
                off_s = m.group(1)
                size_val = float(m.group(2))
                unit = m.group(3).upper()
                if unit == "KB":
                    size_b = int(size_val * 1024)
                elif unit == "MB":
                    size_b = int(size_val * 1024 * 1024)
                elif unit == "GB":
                    size_b = int(size_val * 1024 * 1024 * 1024)
                else:
                    continue
                return (int(off_s, 16), size_b)
    return None


def run_subprocess(cmd: list, check=True) -> int:
    log("> " + " ".join([str(c) for c in cmd]))
    proc = subprocess.run(cmd)
    if check and proc.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {proc.returncode}")
    return proc.returncode


def build_littlefs_image(mklittlefs: Path, data_dir: Path, size_bytes: int, out_path: Path, block=MKLITTLEFS_BLOCK, page=MKLITTLEFS_PAGE, dry_run=False):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(mklittlefs),
        "-c", str(data_dir),
        "-b", str(block),
        "-p", str(page),
        "-s", str(size_bytes),
        str(out_path),
    ]
    if dry_run:
        log(f"[dry-run] Would run: {' '.join(cmd)}")
        return
    log(f"Building LittleFS image: {out_path} ({human_bytes(size_bytes)})")
    run_subprocess(cmd)


def parse_partition_table_entries(blob: bytes):
    """
    Parse ESP-IDF partition table entries.
    Entry layout (32 bytes):
      uint16_t magic = 0x50AA (LE)
      uint8_t  type
      uint8_t  subtype
      uint32_t offset (LE)
      uint32_t size   (LE)  // in bytes
      char     label[16]    // zero-terminated
      uint32_t flags (LE)
    """
    entries = []
    ENTRY_SIZE = 32
    MAGIC = 0x50AA
    for i in range(0, len(blob), ENTRY_SIZE):
        chunk = blob[i:i+ENTRY_SIZE]
        if len(chunk) < ENTRY_SIZE:
            break
        magic, = struct.unpack_from("<H", chunk, 0)
        if magic != MAGIC:
            # could be padding (0xFF), skip
            continue
        type_b = chunk[2]
        subtype_b = chunk[3]
        offset, size = struct.unpack_from("<II", chunk, 4)
        label_bytes = chunk[12:28]
        label = label_bytes.split(b"\x00", 1)[0].decode("ascii", errors="ignore")
        flags, = struct.unpack_from("<I", chunk, 28)
        entries.append({
            "type": type_b,
            "subtype": subtype_b,
            "offset": offset,
            "size": size,
            "label": label,
            "flags": flags,
        })
    return entries


def esptool_read_partition_bin(chip: str, port: str, baud: int, out_file: Path, dry_run=False):
    # Prefer running esptool.py as module to ensure correct version
    py_exe = sys.executable or "python"
    cmd = [
        py_exe, "-m", "esptool",
        "--chip", chip,
        "--port", port,
        "--baud", str(baud),
        "read_flash",
        hex(PARTITION_TABLE_OFFSET),
        hex(PARTITION_TABLE_LENGTH),
        str(out_file),
    ]
    if dry_run:
        log(f"[dry-run] Would run: {' '.join(cmd)}")
        return
    run_subprocess(cmd)


def auto_detect_fs_partition_serial(chip: str, port: str, baud: int, label: str, dry_run=False) -> Optional[Tuple[int, int]]:
    """
    Attempt to read the partition table over serial and locate the FS partition by label.
    """
    with tempfile.TemporaryDirectory() as td:
        out_bin = Path(td) / "partitions.bin"
        esptool_read_partition_bin(chip, port, baud, out_bin, dry_run=dry_run)
        if dry_run:
            return None
        blob = out_bin.read_bytes()
        entries = parse_partition_table_entries(blob)
        # Match by label first; else try type=data (0x01) and subtype=SPIFFS (0x82)
        for e in entries:
            if e["label"] == label:
                return e["offset"], e["size"]
        for e in entries:
            if e["type"] == 0x01 and e["subtype"] == 0x82:  # DATA / SPIFFS
                return e["offset"], e["size"]
    return None


def esptool_write_flash(chip: str, port: str, baud: int, offset: int, image_path: Path, dry_run=False):
    py_exe = sys.executable or "python"
    cmd = [
        py_exe, "-m", "esptool",
        "--chip", chip,
        "--port", port,
        "--baud", str(baud),
        "write_flash",
        hex(offset), str(image_path),
    ]
    if dry_run:
        log(f"[dry-run] Would run: {' '.join(cmd)}")
        return
    run_subprocess(cmd)


def espota_upload_fs(espota_py: Path, host: str, port: int, auth: Optional[str], image_path: Path, dry_run=False):
    """
    Use espota.py --spiffs to upload FS image.
    On ESP32 this generally requires explicit firmware support. If upload fails, exit with error.
    """
    py_exe = sys.executable or "python"
    cmd = [py_exe, str(espota_py), "-i", host, "-p", str(port)]
    if auth:
        cmd += ["-a", auth]
    # --spiffs flag is required for FS upload on espota-based flows
    cmd += ["--spiffs", "-f", str(image_path)]
    if dry_run:
        log(f"[dry-run] Would run: {' '.join(cmd)}")
        return
    ret = subprocess.run(cmd)
    if ret.returncode != 0:
        raise RuntimeError("espota.py FS upload failed. Note: On ESP32, FS OTA is not supported unless your firmware implements it.")


# ---------- Main ----------
def main():
    ap = argparse.ArgumentParser(description="LittleFS uploader via Serial (esptool) or OTA (espota).")
    ap.add_argument("--data-dir", default=DEFAULT_DATA_DIR, help="Path to data directory to pack into LittleFS (default: ./data)")
    ap.add_argument("--label", default=DEFAULT_LABEL, help='Partition label to target (default: "spiffs")')
    ap.add_argument("--size", help="Filesystem size (e.g. 896K, 1.5M). Required if auto-detection fails, and REQUIRED for OTA unless serial.log provides it.")
    ap.add_argument("--offset", help="Filesystem offset (hex like 0x00310000). If omitted, try auto-detect.")

    # Serial options
    ap.add_argument("--port", help="Serial/COM port for esptool (e.g., COM8 or /dev/ttyUSB0)")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"Baud rate for esptool (default: {DEFAULT_BAUD})")
    ap.add_argument("--chip", default=DEFAULT_CHIP, help=f"Target chip for esptool (default: {DEFAULT_CHIP})")

    # OTA options
    ap.add_argument("--ota", help="Device IP/host for OTA (espota.py). If set, OTA mode is used instead of serial.")
    ap.add_argument("--ota-port", type=int, default=DEFAULT_OTA_PORT, help=f"espota port (default: {DEFAULT_OTA_PORT})")
    ap.add_argument("--auth", help="OTA password (if configured)")

    # Tool overrides
    ap.add_argument("--mklittlefs", help="Path to mklittlefs executable")
    ap.add_argument("--espota", help="Path to espota.py")

    # Other
    ap.add_argument("--out", help="Output FS image path (default: ./.build/littlefs.bin)")
    ap.add_argument("--build-only", action="store_true", help="Only build the FS image, do not upload")
    ap.add_argument("--dry-run", action="store_true", help="Print commands without executing")

    args = ap.parse_args()

    project_root = Path(__file__).resolve().parents[1]
    data_dir = (project_root / args.data_dir).resolve()
    ensure_data_dir(data_dir)

    # Determine offset/size
    offset = int(args.offset, 16) if args.offset else None
    size_bytes = parse_size_arg(args.size) if args.size else None

    # Try serial.log for offset/size
    if offset is None or size_bytes is None:
        serial_log_val = read_serial_log_for_spiffs(project_root / "serial.log", args.label)
        if serial_log_val:
            auto_off, auto_size = serial_log_val
            if offset is None:
                offset = auto_off
                log(f"Detected partition offset from serial.log: 0x{offset:08X}")
            if size_bytes is None:
                size_bytes = auto_size
                log(f"Detected partition size from serial.log: {human_bytes(size_bytes)}")

    # If still missing and serial mode (no --ota), read partition table via esptool
    if args.ota is None and (offset is None or size_bytes is None):
        if not args.port:
            err("Serial mode auto-detect requires --port or provide --offset/--size explicitly.")
            sys.exit(2)
        try:
            det = auto_detect_fs_partition_serial(args.chip, args.port, args.baud, args.label, dry_run=args.dry_run)
        except Exception as e:
            det = None
            err(f"Partition table read failed: {e}")
        if det:
            auto_off, auto_size = det
            if offset is None:
                offset = auto_off
                log(f"Detected partition offset via esptool: 0x{offset:08X}")
            if size_bytes is None:
                size_bytes = auto_size
                log(f"Detected partition size via esptool: {human_bytes(size_bytes)}")

    # In OTA mode, we must have size to build image (offset is not used for OTA)
    if args.ota and size_bytes is None:
        err("OTA mode requires filesystem size (--size) or detectable via serial.log.")
        sys.exit(2)

    # In serial mode, both offset and size are required
    if not args.ota:
        if offset is None or size_bytes is None:
            err("Serial mode requires filesystem --offset and --size (or auto-detect must succeed).")
            sys.exit(2)

    # Locate mklittlefs
    mklittlefs = find_mklittlefs(args.mklittlefs)
    if not mklittlefs:
        err("mklittlefs not found. Install an Arduino LittleFS uploader or provide --mklittlefs path.")
        err("Try: https://github.com/earlephilhower/arduino-littlefs-upload")
        err("On Windows, check C:\\Users\\<username>\\.arduinoIDE\\plugins (mklittlefs.exe is typically under a subfolder like ...\\arduino-littlefs-upload\\bin\\mklittlefs.exe).")
        sys.exit(3)
    log(f"Using mklittlefs: {mklittlefs}")

    out_path = Path(args.out) if args.out else (project_root / ".build" / "littlefs.bin")
    out_path = out_path.resolve()

    # Build image
    build_littlefs_image(mklittlefs, data_dir, size_bytes, out_path, dry_run=args.dry_run)

    if args.build_only:
        log("Build-only mode, not uploading.")
        return

    # Upload
    if args.ota:
        espota = find_espota(args.espota)
        if not espota:
            err("espota.py not found. Install Arduino ESP32/ESP8266 core or provide --espota path.")
            sys.exit(3)
        log(f"Using espota.py: {espota}")
        log("Warning: ESP32 FS OTA requires explicit firmware support; otherwise this will fail.")
        espota_upload_fs(espota, args.ota, args.ota_port, args.auth, out_path, dry_run=args.dry_run)
        log("OTA LittleFS upload attempted.")
    else:
        # Serial write via esptool.py
        esptool_write_flash(args.chip, args.port, args.baud, offset, out_path, dry_run=args.dry_run)
        log("Serial LittleFS upload complete.")

    log("Done.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        err("Interrupted.")
        sys.exit(130)
