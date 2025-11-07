#!/usr/bin/env python3
"""
Serial uploader for Endless Pools Controller (ESP32) using Arduino CLI.

What this script does
- Compiles the sketch with Arduino CLI (if requested or if no binary exists)
- Uploads the compiled app over a serial port (e.g. COM3) using Arduino CLI
- Reads defaults (default_port, default_fqbn, ota_password) from sketch.yaml if present
  - Note: ota_password is injected at compile time as -DOTA_PASSWORD="..." to keep OTA consistent

Quick usage
- Upload to COM3 (compile if needed):
    python scripts/serial_upload.py --port COM3
- Force a rebuild and then upload:
    python scripts/serial_upload.py --port COM3 --build
- Override FQBN and upload:
    python scripts/serial_upload.py --port COM3 --build --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi

Notes
- Requires Arduino CLI in PATH (https://arduino.github.io/arduino-cli/latest/installation/)
- FQBN falls back to an ESP32-S3 Dev Module with 8M Flash and custom partition scheme suitable for this project
- The custom partition scheme expects partitions.csv in the sketch root
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path
from shutil import which
from typing import Optional, Tuple

REPO_ROOT = Path(__file__).resolve().parent.parent
SKETCH_PATH = REPO_ROOT  # endless-pools-controller.ino is in repo root
SKETCH_YAML = REPO_ROOT / "sketch.yaml"
BUILD_DIR = REPO_ROOT / "build" / "arduino"


def clean_yaml_value(s: Optional[str]) -> Optional[str]:
    if s is None:
        return None
    s = s.strip()
    # strip inline comments
    m = re.match(r"^(.*?)(\s+#.*)?$", s)
    if m:
        s = m.group(1).strip()
    # strip surrounding quotes
    if len(s) >= 2 and ((s.startswith('"') and s.endswith('"')) or (s.startswith("'") and s.endswith("'"))):
        s = s[1:-1]
    return s


def read_sketch_defaults(sketch_yaml: Path) -> Tuple[Optional[str], Optional[str], Optional[str]]:
    default_port = None
    ota_password = None
    default_fqbn = None
    if sketch_yaml.is_file():
        text = sketch_yaml.read_text(encoding="utf-8", errors="ignore")
        m1 = re.search(r"(?m)^\s*default_port:\s*(.+)$", text)
        if m1:
            default_port = clean_yaml_value(m1.group(1))
        m2 = re.search(r"(?m)^\s*ota_password:\s*(.+)$", text)
        if m2:
            ota_password = clean_yaml_value(m2.group(1))
        m3 = re.search(r"(?m)^\s*default_fqbn:\s*(.+)$", text)
        if m3:
            default_fqbn = clean_yaml_value(m3.group(1))
    if ota_password == "REPLACE_WITH_OTA_PASSWORD":
        ota_password = None
    return default_port, ota_password, default_fqbn


def is_serial_port(s: str) -> bool:
    # Accept Windows COM ports (e.g., COM3) and Unix-like device paths (e.g., /dev/ttyUSB0, /dev/cu.usbserial-0001)
    if re.match(r"(?i)^COM\d+$", s):
        return True
    if s.startswith("/dev/tty") or s.startswith("/dev/cu"):
        return True
    return False

def normalize_port(port: str) -> str:
    """
    Normalize serial port names for the current OS.
    - On Windows (Git Bash), map /dev/ttyS{n} -> COM{n+1}
    - Otherwise, return as-is.
    """
    if os.name == "nt":
        m = re.match(r"^/dev/ttyS(\d+)$", port)
        if m:
            try:
                idx = int(m.group(1))
                return f"COM{idx + 1}"
            except Exception:
                return port
    return port


def find_app_bin(build_dir: Path) -> Optional[Path]:
    if not build_dir.is_dir():
        return None
    # Prefer top-level *.ino.bin in build/arduino
    for f in build_dir.iterdir():
        if f.is_file() and f.name.endswith(".ino.bin"):
            return f
    # Otherwise search recursively
    for f in build_dir.rglob("*.ino.bin"):
        return f
    return None


def find_arduino_cli() -> Optional[str]:
    return which("arduino-cli")


def derive_fqbn(cli_fqbn: Optional[str], yaml_fqbn: Optional[str]) -> str:
    if cli_fqbn:
        return cli_fqbn
    if yaml_fqbn:
        # Ensure overrides are applied for this project goal
        base = yaml_fqbn
        # If FlashSize present, replace its value; otherwise append
        if re.search(r"(?:^|,)\s*FlashSize=", base):
            base = re.sub(r"(?:^|,)\s*FlashSize=[^,]+", ",FlashSize=8M", base)
        else:
            base = base + ",FlashSize=8M"
        if re.search(r"(?:^|,)\s*PartitionScheme=", base):
            base = re.sub(r"(?:^|,)\s*PartitionScheme=[^,]+", ",PartitionScheme=custom", base)
        else:
            base = base + ",PartitionScheme=custom"
        return base
    # Fallback for ESP32-S3 Dev Module with our desired layout
    return "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,PSRAM=opi,FlashSize=8M,PartitionScheme=custom"


def compile_sketch(fqbn: str, build_dir: Path, repo_root: Path, ota_pw: Optional[str], verbose: bool) -> int:
    arduino = find_arduino_cli()
    if not arduino:
        print("ERROR: arduino-cli not found in PATH. Install Arduino CLI or compile with IDE first.", file=sys.stderr)
        return 10
    cmd = [arduino, "compile", "-b", fqbn, "--build-path", str(build_dir), "--export-binaries", str(repo_root)]
    if ota_pw:
        # Inject OTA_PASSWORD so the firmware uses the same password for future OTA
        define = f'-DOTA_PASSWORD="{ota_pw}"'
        cmd += ["--build-property", f"compiler.cpp.extra_flags={define}",
                "--build-property", f"compiler.c.extra_flags={define}"]
    if verbose:
        cmd.insert(1, "-v")
        print("[INFO] compile cmd:", " ".join(cmd))
    try:
        res = subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print("ERROR: Failed to execute arduino-cli. Verify your Arduino CLI installation.", file=sys.stderr)
        return 11
    return res.returncode


def upload_serial(port: str, fqbn: str, build_dir: Path, repo_root: Path, verbose: bool) -> int:
    arduino = find_arduino_cli()
    if not arduino:
        print("ERROR: arduino-cli not found in PATH.", file=sys.stderr)
        return 12
    cmd = [arduino, "upload", "-p", port, "-b", fqbn, "--input-dir", str(build_dir), str(repo_root)]
    if verbose:
        cmd.insert(1, "-v")
        print("[INFO] upload cmd:", " ".join(cmd))
    try:
        res = subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print("ERROR: Failed to execute arduino-cli. Verify your Arduino CLI installation.", file=sys.stderr)
        return 13
    return res.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32 serial firmware uploader using Arduino CLI (compiles if needed)")
    parser.add_argument("--port", "-p", help="Serial port (e.g. COM3). Defaults to sketch.yaml:default_port if that looks like a COM port.")
    parser.add_argument("--fqbn", help="Fully Qualified Board Name. Overrides sketch.yaml default_fqbn if provided.")
    parser.add_argument("--build", action="store_true", help="Force compile the sketch before uploading.")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    args = parser.parse_args()

    default_port, yaml_ota_pw, yaml_fqbn = read_sketch_defaults(SKETCH_YAML)

    # Determine port
    port = args.port
    if not port and default_port and is_serial_port(default_port):
        port = default_port
    if not port:
        print("ERROR: No --port specified and sketch.yaml default_port is missing or not a serial port.", file=sys.stderr)
        print(" - Examples:", file=sys.stderr)
        print("   - Windows: python scripts/serial_upload.py --port COM3", file=sys.stderr)
        print("   - Linux:   python3 scripts/serial_upload.py --port /dev/ttyUSB0", file=sys.stderr)
        print("   - macOS:   python3 scripts/serial_upload.py --port /dev/cu.usbserial-0001", file=sys.stderr)
        return 2
    if not is_serial_port(port):
        print(f"ERROR: '{port}' does not look like a valid serial port.", file=sys.stderr)
        print(" - Examples:", file=sys.stderr)
        print("   - Windows: COM3", file=sys.stderr)
        print("   - Linux:   /dev/ttyUSB0", file=sys.stderr)
        print("   - macOS:   /dev/cu.usbserial-0001", file=sys.stderr)
        return 2

    # Build if requested or if binary not present
    fqbn = derive_fqbn(args.fqbn, yaml_fqbn)
    bin_path = find_app_bin(BUILD_DIR)
    need_compile = args.build or (bin_path is None)
    norm_port = normalize_port(port)

    if args.verbose:
        print("[INFO] repo root:", REPO_ROOT)
        print("[INFO] port:", norm_port)
        print("[INFO] using FQBN:", fqbn)
        if yaml_ota_pw:
            print("[INFO] injecting OTA_PASSWORD for build (from sketch.yaml)")

    if need_compile:
        rc = compile_sketch(fqbn, BUILD_DIR, REPO_ROOT, yaml_ota_pw, args.verbose)
        if rc != 0:
            print("[ERROR] Build failed.", file=sys.stderr)
            return rc
        bin_path = find_app_bin(BUILD_DIR)

    if not bin_path or not bin_path.is_file():
        print(f"ERROR: Could not locate application binary (*.ino.bin).", file=sys.stderr)
        print(f" - Searched default build dir: {BUILD_DIR}", file=sys.stderr)
        print(" - Try: python scripts/serial_upload.py --port COM3 --build", file=sys.stderr)
        return 3

    print(f"[INFO] Uploading to {norm_port} via Arduino CLI ...")
    rc = upload_serial(norm_port, fqbn, BUILD_DIR, REPO_ROOT, args.verbose)
    if rc != 0:
        print("[ERROR] Serial upload failed.", file=sys.stderr)
        return rc

    print("[OK] Serial upload completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
