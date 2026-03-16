#!/usr/bin/env python3
"""
OTA uploader for Endless Pools Controller (ESP32).

What this script does
- Uploads a compiled application (*.ino.bin) to your device over-the-air using espota.py.
- Optionally compiles the sketch with Arduino CLI before uploading.
- Reads defaults (default_port, ota_password, default_fqbn) from sketch.yaml if present.
- If no FQBN is provided and sketch.yaml has none, it uses a sensible fallback with:
  FlashSize=8M, PartitionScheme=custom (expects partitions.csv in sketch root), PSRAM=opi

Quick usage
- If your device host/IP is known and you already compiled:
    python scripts/ota_upload.py --target 192.168.1.50
- If you want to compile first (recommended if the .bin doesn't exist yet):
    python scripts/ota_upload.py --target swimmachine.local --build
- Specify password explicitly (otherwise read from sketch.yaml if set):
    python scripts/ota_upload.py --target 192.168.1.50 --password YOUR_OTA_PASSWORD
- Specify a custom FQBN (if you need to override options):
    python scripts/ota_upload.py --target 192.168.1.50 --build --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=8M,PartitionScheme=custom,PSRAM=opi
- Use defaults from sketch.yaml (if it contains default_port/ota_password/default_fqbn):
    python scripts/ota_upload.py

Notes
- OTA uses espota.py and updates only the app image. To switch partition table to the included custom 8M layout (3MB app + 3MB app + 1.5MB SPIFFS), do one serial upload or CLI upload (not OTA) that flashes the new partition scheme.
- The compile step (when used) will:
  - use your provided --fqbn, or sketch.yaml:default_fqbn, or the fallback with FlashSize=8M,PartitionScheme=custom
  - inject -DOTA_PASSWORD="..." into the firmware if a password is provided or found in sketch.yaml
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


def find_espota() -> Optional[Path]:
    """
    Locate espota.py from the installed Arduino ESP32 core.
    Supports Windows (LOCALAPPDATA\Arduino15), Linux (~/.arduino15), and macOS (~/Library/Arduino15),
    and falls back to PATH lookup.
    """
    candidates: list[Path] = []

    # Windows: %LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\{version}
    localapp = os.environ.get("LOCALAPPDATA")
    if localapp:
        candidates.append(Path(localapp) / "Arduino15" / "packages" / "esp32" / "hardware" / "esp32")

    # Linux: ~/.arduino15/packages/esp32/hardware/esp32/{version}
    home = Path.home()
    candidates.append(home / ".arduino15" / "packages" / "esp32" / "hardware" / "esp32")

    # macOS: ~/Library/Arduino15/packages/esp32/hardware/esp32/{version}
    candidates.append(home / "Library" / "Arduino15" / "packages" / "esp32" / "hardware" / "esp32")

    for base in candidates:
        if base.is_dir():
            versions = sorted((d for d in base.iterdir() if d.is_dir()), key=lambda p: p.name, reverse=True)
            for v in versions:
                p1 = v / "tools" / "espota.py"
                if p1.is_file():
                    return p1
                p2 = v / "tools" / "espota" / "espota.py"
                if p2.is_file():
                    return p2

    # Fallback: espota.py in PATH
    for p in os.environ.get("PATH", "").split(os.pathsep):
        cand = Path(p) / "espota.py"
        if cand.is_file():
            return cand
    return None


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

def read_ota_password_from_header(header_path: Path) -> Optional[str]:
    """
    Extract OTA_PASSWORD from a C/C++ header, accepting either double or single quotes.
    Returns the string value or None if not found.
    """
    try:
        text = header_path.read_text(encoding="utf-8", errors="ignore")
    except Exception:
        return None
    m = re.search(r'(?m)^\s*#\s*define\s+OTA_PASSWORD\s+["\\\'](.*?)["\\\']\s*$', text)
    if not m:
        return None
    return m.group(1)


def derive_fqbn(cli_fqbn: Optional[str], yaml_fqbn: Optional[str]) -> str:
    if cli_fqbn:
        return cli_fqbn
    if yaml_fqbn:
        # Ensure overrides are applied for this project goal
        # Append/override FlashSize and PartitionScheme, leaving other options intact
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


def compile_sketch(fqbn: str, build_dir: Path, repo_root: Path, ota_pw: Optional[str], debug: bool, verbose: bool) -> int:
    arduino = find_arduino_cli()
    if not arduino:
        print("ERROR: arduino-cli not found in PATH. Install Arduino CLI or compile with IDE first.", file=sys.stderr)
        return 10
    cmd = [arduino, "compile", "-b", fqbn, "--build-path", str(build_dir), "--export-binaries", str(repo_root)]
    flags: list[str] = []
    if ota_pw:
        flags.append(f'-DOTA_PASSWORD="{ota_pw}"')
    if debug:
        flags.append("-DDEBUG")
    if flags:
        flags_str = " ".join(flags)
        cmd += ["--build-property", f"compiler.cpp.extra_flags={flags_str}",
                "--build-property", f"compiler.c.extra_flags={flags_str}"]
    if verbose:
        print("[INFO] compile cmd:", " ".join(cmd))
    try:
        res = subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print("ERROR: Failed to execute arduino-cli. Verify your Arduino CLI installation.", file=sys.stderr)
        return 11
    return res.returncode


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32 OTA firmware uploader using espota.py (with optional Arduino CLI compile)")
    parser.add_argument("--target", "-t", help="Target IP/hostname. Defaults to default_port from sketch.yaml if available, otherwise swimmachine.local.")
    parser.add_argument("--bin", "-f", dest="bin_path", help="Path to application binary (*.ino.bin). Defaults to first found under build/arduino.")
    parser.add_argument("--password", "-a", help="OTA password. Defaults to sketch.yaml ota_password if set; otherwise extracted from otapassword.h.")
    parser.add_argument("--service-port", "-p", type=int, default=3232, help="OTA service port (default: 3232)")
    parser.add_argument("--fqbn", help="Fully Qualified Board Name (for compile). Overrides sketch.yaml default_fqbn if provided.")
    parser.add_argument("--build", action="store_true", help="Compile the sketch before uploading. If not set, will auto-compile only if binary not found.")
    parser.add_argument("--verbose", "-v", action="store_true", help="Verbose output")
    parser.add_argument("--debug", action="store_true", help="Define DEBUG to enable verbose PSK debug in server 401 responses (security risk).")
    args = parser.parse_args()

    default_port, yaml_ota_pw, yaml_fqbn = read_sketch_defaults(SKETCH_YAML)

    target = args.target or default_port or "swimmachine.local"
    if not target:
        print("ERROR: No --target specified and sketch.yaml does not define default_port.", file=sys.stderr)
        return 2

    # If default_port is a COM port, it's not valid for OTA
    if re.match(r"(?i)^COM\d+$", target):
        print(f"ERROR: Target '{target}' looks like a serial port. OTA requires a host/IP. Use Arduino IDE/CLI serial upload to flash partitions first.", file=sys.stderr)
        return 2

    ota_pw = args.password if args.password is not None else (yaml_ota_pw or read_ota_password_from_header(REPO_ROOT / "otapassword.h"))

    # Ensure we have a .bin (compile if requested or missing)
    bin_path = Path(args.bin_path) if args.bin_path else find_app_bin(BUILD_DIR)
    need_compile = args.build or (bin_path is None)
    if need_compile:
        fqbn = derive_fqbn(args.fqbn, yaml_fqbn)
        if args.verbose:
            print("[INFO] repo root:", REPO_ROOT)
            print("[INFO] using FQBN:", fqbn)
            if ota_pw:
                print("[INFO] injecting OTA_PASSWORD for build")
        rc = compile_sketch(fqbn, BUILD_DIR, REPO_ROOT, ota_pw, args.debug, args.verbose)
        if rc != 0:
            print("[ERROR] Build failed.", file=sys.stderr)
            return rc
        # Locate compiled binary again after build
        bin_path = find_app_bin(BUILD_DIR)

    if not bin_path or not bin_path.is_file():
        print(f"ERROR: Could not locate application binary (*.ino.bin).", file=sys.stderr)
        print(f" - Searched default build dir: {BUILD_DIR}", file=sys.stderr)
        print(" - Try: python scripts/ota_upload.py --target <host> --build", file=sys.stderr)
        return 3

    espota = find_espota()
    if not espota:
        print("ERROR: espota.py not found. Ensure Arduino ESP32 core is installed (via Board Manager) in Arduino IDE/CLI.", file=sys.stderr)
        return 4

    cmd = [sys.executable, str(espota), "-i", target, "-p", str(args.service_port), "-f", str(bin_path)]
    if ota_pw:
        cmd += ["-a", ota_pw]

    if args.verbose:
        print("[INFO] repo root:", REPO_ROOT)
        print("[INFO] using espota:", espota)
        print("[INFO] target:", target)
        print("[INFO] service port:", args.service_port)
        print("[INFO] bin:", bin_path)
        if ota_pw:
            print("[INFO] password: (provided or from sketch.yaml)")
        else:
            print("[INFO] password: (none)")

    print(f"[INFO] Uploading via OTA to {target}:{args.service_port} ...")
    try:
        res = subprocess.run(cmd, check=False)
    except FileNotFoundError:
        print("ERROR: Failed to execute Python or espota.py. Verify your Python installation.", file=sys.stderr)
        return 5

    if res.returncode != 0:
        print("[ERROR] OTA upload failed.", file=sys.stderr)
        return res.returncode

    print("[OK] OTA upload completed successfully.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
