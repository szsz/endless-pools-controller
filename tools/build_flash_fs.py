#!/usr/bin/env python3
"""
Build firmware with Arduino CLI and flash it via COM or OTA.

LittleFS note:
- This script no longer builds or uploads LittleFS images.
- To manage LittleFS contents, use the Arduino IDE LittleFS upload tools (recommended).

Defaults:
- Serial COM port: COM3
- OTA host: swimmachine.local
- Chip: esp32s3
- Baud: 921600
- FQBN: esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi
- Sketch: endless-pools-controller.ino in repo root
- arduino-cli is expected to be on PATH

Examples:
  Serial build + flash on COM3 (default):
    python tools/build_flash_fs.py

  Serial with explicit COM port:
    python tools/build_flash_fs.py --port COM5

  OTA build + flash (default host swimmachine.local):
    python tools/build_flash_fs.py --mode ota
"""

import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Optional, List

DEFAULT_MODE = "serial"            # serial | ota
DEFAULT_PORT = "COM3"
DEFAULT_HOST = "swimmachine.local"
DEFAULT_CHIP = "esp32s3"
DEFAULT_BAUD = 921600
DEFAULT_FQBN = "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi"
DEFAULT_SKETCH = "endless-pools-controller.ino"
DEFAULT_BUILD_DIR = "build/arduino"
DEFAULT_OTA_PORT = 3232


def log(msg: str):
    print(msg, flush=True)


def err(msg: str):
    print(f"[ERROR] {msg}", file=sys.stderr, flush=True)


def find_executable(name: str) -> Optional[Path]:
    p = shutil.which(name)
    return Path(p) if p else None


def run(cmd: List[str], check=True) -> int:
    log("> " + " ".join([str(c) for c in cmd]))
    proc = subprocess.run(cmd)
    if check and proc.returncode != 0:
        raise RuntimeError(f"Command failed with exit code {proc.returncode}")
    return proc.returncode


# ---------- Arduino CLI helpers ----------
def arduino_cli() -> Path:
    exe = find_executable("arduino-cli.exe" if sys.platform.startswith("win") else "arduino-cli")
    if not exe:
        raise RuntimeError("arduino-cli not found on PATH. Install Arduino CLI and ensure it's available.")
    return exe


def arduino_build(sketch: Path, fqbn: str, build_path: Path, extra_flags: Optional[List[str]] = None):
    cli = arduino_cli()
    build_path.mkdir(parents=True, exist_ok=True)
    cmd = [str(cli), "compile", "-b", fqbn, "--build-path", str(build_path), "--export-binaries", str(sketch.parent)]
    if extra_flags:
        cmd += extra_flags
    return run(cmd)


def arduino_upload_serial(fqbn: str, build_path: Path, port: str):
    cli = arduino_cli()
    cmd = [str(cli), "upload", "-p", port, "-b", fqbn, "--input-dir", str(build_path)]
    return run(cmd)


def arduino_upload_ota(fqbn: str, build_path: Path, host: str):
    # Arduino CLI can upload via network if it discovers the port; try CLI first
    cli = arduino_cli()
    cmd = [str(cli), "upload", "-p", host, "-b", fqbn, "--input-dir", str(build_path)]
    ret = subprocess.run(cmd).returncode
    return ret


def latest_app_bin(build_path: Path) -> Optional[Path]:
    bins = sorted(build_path.glob("*.bin"), key=lambda p: p.stat().st_mtime, reverse=True)
    for b in bins:
        if b.name.endswith(".ino.bin"):
            return b
    return bins[0] if bins else None


def find_espota(explicit: Optional[str]) -> Optional[Path]:
    if explicit:
        p = Path(explicit)
        if p.exists():
            return p
        return None
    on_path = find_executable("espota.py")
    if on_path:
        return on_path
    # Arduino15 typical locations
    home = Path.home()
    candidates = [
        home / "AppData" / "Local" / "Arduino15",                 # Windows
        home / ".arduino15",                                      # Linux
        home / "Library" / "Arduino15",                           # macOS
    ]
    for base in candidates:
        tools_root = base / "packages"
        if tools_root.exists():
            for tools_dir in tools_root.glob("*/*/tools/*"):
                for py in tools_dir.rglob("espota.py"):
                    if py.is_file():
                        return py
    return None


def espota_upload_app(espota_py: Path, host: str, port: int, auth: Optional[str], app_bin: Path, dry_run=False):
    py_exe = sys.executable or "python"
    cmd = [py_exe, str(espota_py), "-i", host, "-p", str(port)]
    if auth:
        cmd += ["-a", auth]
    cmd += ["-f", str(app_bin)]
    if dry_run:
        log(f"[dry-run] Would run: {' '.join(cmd)}")
        return 0
    return run(cmd)


def main():
    ap = argparse.ArgumentParser(description="Build firmware and flash via COM or OTA (no LittleFS operations).")
    ap.add_argument("--mode", choices=["serial", "ota"], default=DEFAULT_MODE, help="serial (COM) or ota (default: serial)")
    ap.add_argument("--port", default=DEFAULT_PORT, help="Serial COM port for flashing (default: COM3)")
    ap.add_argument("--host", default=DEFAULT_HOST, help="OTA hostname/IP (default: swimmachine.local)")
    ap.add_argument("--chip", default=DEFAULT_CHIP, help=f"(unused for build; kept for compatibility) default: {DEFAULT_CHIP}")
    ap.add_argument("--baud", type=int, default=DEFAULT_BAUD, help=f"(unused for build; kept for compatibility) default: {DEFAULT_BAUD}")
    ap.add_argument("--fqbn", default=DEFAULT_FQBN, help="Fully Qualified Board Name with options for Arduino CLI")
    ap.add_argument("--sketch", default=DEFAULT_SKETCH, help="Path to .ino sketch (default: endless-pools-controller.ino)")
    ap.add_argument("--build-dir", default=DEFAULT_BUILD_DIR, help="Arduino CLI build output dir (default: build/arduino)")
    ap.add_argument("--auth", help="OTA password (if configured)")
    ap.add_argument("--espota", help="Path to espota.py (optional for OTA fallback)")
    ap.add_argument("--build-only", action="store_true", help="Only build the app; skip flashing")
    ap.add_argument("--dry-run", action="store_true", help="Print commands without executing")

    args = ap.parse_args()

    root = Path(__file__).resolve().parents[1]
    sketch_path = (root / args.sketch).resolve()
    build_path = (root / args.build_dir).resolve()

    if not sketch_path.exists():
        err(f"Sketch not found: {sketch_path}")
        sys.exit(2)

    # Build app
    try:
        arduino_build(sketch_path, args.fqbn, build_path)
    except Exception as e:
        err(f"Arduino CLI build failed: {e}")
        sys.exit(3)

    if args.build_only:
        log("Build-only mode requested; skipping flashing.")
        return

    # Flash app
    if args.mode == "serial":
        try:
            arduino_upload_serial(args.fqbn, build_path, args.port)
        except Exception as e:
            err(f"Serial upload failed: {e}")
            sys.exit(3)
        # Let device reboot
        time.sleep(2.0)
        log("Serial app upload complete.")
    else:
        # Try Arduino CLI OTA; fallback to espota.py if needed
        ret = arduino_upload_ota(args.fqbn, build_path, args.host)
        if ret != 0:
            app_bin = latest_app_bin(build_path)
            if not app_bin:
                err("Could not locate app .bin in build output for espota fallback.")
                sys.exit(3)
            espota = find_espota(args.espota)
            if not espota:
                err("espota.py not found for OTA fallback. Install ESP32/ESP8266 core or provide --espota path.")
                sys.exit(3)
            log(f"Falling back to espota.py app upload: {espota}")
            if espota_upload_app(espota, args.host, DEFAULT_OTA_PORT, args.auth, app_bin, args.dry_run) != 0:
                err("espota.py app OTA upload failed.")
                sys.exit(3)
        # Allow reboot after OTA
        time.sleep(2.0)
        log("OTA app upload complete.")

    log("Done. (LittleFS operations are intentionally not handled by this script; use Arduino IDE LittleFS upload tools.)")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        err("Interrupted.")
        sys.exit(130)
