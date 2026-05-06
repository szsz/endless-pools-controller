#!/usr/bin/env python3
"""
End-to-end installer for Endless Pools Controller.

Steps performed (in order):
  1) Auto-detect the ESP32-S3 serial port (Espressif USB VID 303A) and ask the user to confirm.
  2) Compile + flash the firmware over USB serial via scripts/serial_upload.py.
  3) After the device reboots, prompt for its address (LAN hostname/IP, or AP fallback)
     and upload the data/ directory (Web UI + workouts) into LittleFS via HTTP, using
     scripts/upload_http_data.py.

Stdlib only - no external Python packages required.

Examples:
  py -3 scripts/install.py
  py -3 scripts/install.py --port COM3 --base http://192.168.4.1
  py -3 scripts/install.py --skip-firmware --base http://swimmachine.local
"""

from __future__ import annotations

import argparse
import json
import os
import re
import socket
import subprocess
import sys
from pathlib import Path
from typing import List, Optional, Tuple
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPTS_DIR = REPO_ROOT / "scripts"
DATA_DIR = REPO_ROOT / "data"

ESPRESSIF_VID = "303A"  # Espressif Systems (ESP32-S3 native USB CDC)


# ---------- Port detection ----------

def _detect_ports_windows() -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    """Return [(port, name, vid, pid)] using PowerShell + Win32_PnPEntity."""
    ps_script = (
        "Get-CimInstance -ClassName Win32_PnPEntity | "
        "Where-Object { $_.Name -match 'COM\\d+' } | "
        "Select-Object Name, DeviceID | ConvertTo-Json -Compress"
    )
    try:
        res = subprocess.run(
            ["powershell", "-NoProfile", "-NonInteractive", "-Command", ps_script],
            capture_output=True, text=True, timeout=15,
        )
    except Exception as e:
        print(f"[WARN] PowerShell port enumeration failed: {e}", file=sys.stderr)
        return []
    if res.returncode != 0 or not res.stdout.strip():
        return []
    try:
        data = json.loads(res.stdout)
    except Exception:
        return []
    if isinstance(data, dict):
        data = [data]

    out: List[Tuple[str, str, Optional[str], Optional[str]]] = []
    for entry in data:
        name = entry.get("Name", "") or ""
        devid = entry.get("DeviceID", "") or ""
        m = re.search(r"\((COM\d+)\)", name)
        if not m:
            continue
        port = m.group(1)
        vid = pid = None
        mvid = re.search(r"VID_([0-9A-F]+)", devid, re.IGNORECASE)
        mpid = re.search(r"PID_([0-9A-F]+)", devid, re.IGNORECASE)
        if mvid:
            vid = mvid.group(1).upper()
        if mpid:
            pid = mpid.group(1).upper()
        out.append((port, name, vid, pid))
    return out


def _detect_ports_unix() -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    import glob
    ports: List[Tuple[str, str, Optional[str], Optional[str]]] = []
    patterns = ("/dev/ttyUSB*", "/dev/ttyACM*", "/dev/cu.usb*", "/dev/cu.SLAB*", "/dev/cu.wchusb*")
    for pat in patterns:
        for p in sorted(glob.glob(pat)):
            ports.append((p, p, None, None))
    return ports


def detect_ports() -> List[Tuple[str, str, Optional[str], Optional[str]]]:
    return _detect_ports_windows() if os.name == "nt" else _detect_ports_unix()


def pick_port(ports: List[Tuple[str, str, Optional[str], Optional[str]]]) -> Optional[Tuple[str, str, Optional[str], Optional[str]]]:
    if not ports:
        return None
    espressif = [p for p in ports if p[2] == ESPRESSIF_VID]

    if len(espressif) == 1:
        return espressif[0]

    if len(espressif) > 1:
        print("Multiple Espressif (VID 303A) devices detected:")
        for i, (port, name, vid, pid) in enumerate(espressif, 1):
            print(f"  [{i}] {port}  {name}  VID:{vid} PID:{pid}")
        choice = input("Pick a number (empty to abort): ").strip()
        if not choice:
            return None
        try:
            return espressif[int(choice) - 1]
        except (ValueError, IndexError):
            print("Invalid selection.", file=sys.stderr)
            return None

    # No Espressif device found — let the user pick from anything we can see
    print("No Espressif (VID 303A) device found. All visible serial devices:")
    if not ports:
        return None
    for i, (port, name, vid, pid) in enumerate(ports, 1):
        extra = f"VID:{vid} PID:{pid}" if vid else ""
        print(f"  [{i}] {port}  {name}  {extra}".rstrip())
    choice = input("Pick a number (empty to abort): ").strip()
    if not choice:
        return None
    try:
        return ports[int(choice) - 1]
    except (ValueError, IndexError):
        print("Invalid selection.", file=sys.stderr)
        return None


# ---------- Prompts ----------

def ask_yes_no(prompt: str, default_yes: bool = True) -> bool:
    suffix = " [Y/n] " if default_yes else " [y/N] "
    try:
        ans = input(prompt + suffix).strip().lower()
    except EOFError:
        return default_yes
    if not ans:
        return default_yes
    return ans in ("y", "yes")


def ask_action(default: str = "both") -> str:
    """Ask whether to flash firmware, upload data, or both. Returns 'firmware' | 'data' | 'both'."""
    print()
    print("What would you like to do?")
    print("  [F] Flash firmware only (compile + USB serial upload)")
    print("  [D] Upload data only   (Web UI + workouts -> LittleFS over HTTP)")
    print("  [B] Both               (firmware then data; the typical fresh install)")
    while True:
        try:
            ans = input(f"Choice [F/D/B] (default: {default[0].upper()}): ").strip().lower()
        except EOFError:
            return default
        if not ans:
            return default
        if ans in ("f", "firmware"):
            return "firmware"
        if ans in ("d", "data"):
            return "data"
        if ans in ("b", "both"):
            return "both"
        print("  Please answer F, D, or B.")


def print_wifi_tutorial() -> None:
    print()
    print("=" * 72)
    print("WiFi / network setup")
    print("=" * 72)
    print("The device needs to be reachable on the network before files can be")
    print("uploaded. Pick the path that matches your situation:")
    print()
    print("A) First install or device not yet on your WiFi")
    print("   1. On your phone or PC, open WiFi settings.")
    print("   2. Connect to the WiFi network named 'swimmachine'")
    print("      (password: 12345678).")
    print("   3. In a browser, open  http://192.168.4.1/wifi")
    print("   4. Enter your home WiFi SSID and password and click Save.")
    print("      The device will reboot and join your WiFi (~10-15 s).")
    print("   5. Reconnect your PC to your normal WiFi.")
    print("   6. Use  http://swimmachine.local  (or the device's IP from your")
    print("      router's DHCP table / serial log).")
    print()
    print("B) Device already on your LAN (Ethernet or saved WiFi)")
    print("   - Use  http://swimmachine.local  or the LAN IP directly.")
    print()
    print("C) Change WiFi credentials later")
    print("   - From your LAN, open  http://swimmachine.local/wifi  and")
    print("     re-submit the form. The device will reboot onto the new WiFi.")
    print()
    print("Tip: if 'swimmachine.local' (mDNS) doesn't resolve on your network,")
    print("     use the IP address instead, e.g. http://192.168.1.50")
    print("=" * 72)
    print()


def normalize_base(s: str) -> str:
    s = s.strip()
    if not s:
        return s
    if s == "4":
        return "http://192.168.4.1"
    if not re.match(r"^https?://", s, re.IGNORECASE):
        s = "http://" + s
    return s.rstrip("/")


def check_reachable(base: str, timeout: float = 3.0) -> Optional[str]:
    """
    Probe <base>/wifi (a C++ route always present on the device).
    Returns None if reachable, otherwise an error string.
    """
    base = base.rstrip("/")
    parsed = urlparse(base)
    host = parsed.hostname
    port = parsed.port or (443 if parsed.scheme == "https" else 80)
    if not host:
        return f"invalid URL: {base}"
    # DNS / mDNS
    try:
        socket.getaddrinfo(host, port, proto=socket.IPPROTO_TCP)
    except socket.gaierror as e:
        return f"could not resolve host '{host}' ({e})"
    except Exception as e:
        return f"address lookup error: {e}"
    # HTTP
    url = base + "/wifi"
    try:
        req = Request(url, method="GET")
        with urlopen(req, timeout=timeout) as resp:
            _ = resp.read(64)
            return None
    except HTTPError:
        # Any HTTP response means the device is up
        return None
    except URLError as e:
        return f"connection failed: {getattr(e, 'reason', e)}"
    except socket.timeout:
        return f"timeout after {timeout:.0f}s"
    except Exception as e:
        return f"error: {e}"


def resolve_base_url(args_base: Optional[str], default_base: str = "http://swimmachine.local") -> Optional[str]:
    """
    Pick a reachable base URL.
    - If --base was passed, use it (probe and loop on failure).
    - Otherwise, silently probe the default first; only show the tutorial and
      prompt if the default doesn't respond.
    Returns the working base URL, or None if the user aborts.
    """
    # Attempt 1: explicit --base or default
    candidate = normalize_base(args_base) if args_base else default_base
    print(f"Probing device at {candidate} ...")
    err = check_reachable(candidate)
    if err is None:
        print(f"  OK - device responded at {candidate}")
        return candidate
    print(f"  Unreachable: {err}")

    # Fallback: show tutorial + loop until reachable
    tutorial_shown = False
    while True:
        if not tutorial_shown:
            print_wifi_tutorial()
            tutorial_shown = True
        try:
            raw = input("Device base URL (Enter to retry swimmachine.local, '4' for AP at 192.168.4.1, blank line + Ctrl-C to abort): ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return None
        candidate = normalize_base(raw) if raw else default_base
        print(f"Probing device at {candidate} ...")
        err = check_reachable(candidate)
        if err is None:
            print(f"  OK - device responded at {candidate}")
            return candidate
        print(f"  Unreachable: {err}")
        if not ask_yes_no("Try a different address (or fix WiFi setup and retry)?", default_yes=True):
            return None


# ---------- Step runners ----------

def run_step(label: str, cmd: List[str]) -> int:
    print()
    print(f"=== {label} ===")
    print("$ " + " ".join(cmd))
    try:
        return subprocess.run(cmd).returncode
    except FileNotFoundError as e:
        print(f"[ERROR] Failed to launch: {e}", file=sys.stderr)
        return 127


# ---------- Main ----------

def main() -> int:
    ap = argparse.ArgumentParser(
        description="Endless Pools Controller installer: pick action, build+flash firmware, upload data/ over HTTP.",
    )
    ap.add_argument("--port", help="Serial port (e.g. COM3). If omitted, auto-detect and prompt.")
    ap.add_argument("--base", help="Base URL for HTTP data upload (e.g. http://swimmachine.local). Default: probe http://swimmachine.local first; only prompt on failure.")
    ap.add_argument(
        "--action",
        choices=("firmware", "data", "both"),
        help="Skip the action prompt and run this step directly.",
    )
    ap.add_argument("--skip-firmware", action="store_true", help="Alias for --action data.")
    ap.add_argument("--skip-data", action="store_true", help="Alias for --action firmware.")
    ap.add_argument("-y", "--yes", action="store_true", help="Don't ask to confirm an auto-detected port or final upload.")
    args = ap.parse_args()

    py = sys.executable or "python"

    # Resolve action: explicit flag > legacy --skip-* > interactive prompt
    if args.action:
        action = args.action
    elif args.skip_firmware and args.skip_data:
        print("[ERROR] --skip-firmware and --skip-data together leave nothing to do.", file=sys.stderr)
        return 2
    elif args.skip_firmware:
        action = "data"
    elif args.skip_data:
        action = "firmware"
    else:
        action = ask_action(default="both")

    do_firmware = action in ("firmware", "both")
    do_data = action in ("data", "both")

    # Step 1+2: detect port + build + flash
    if do_firmware:
        port = args.port
        if not port:
            print("Scanning for serial ports...")
            ports = detect_ports()
            if not ports:
                print("[ERROR] No serial ports detected. Plug the ESP32-S3 in via USB and retry.", file=sys.stderr)
                return 2
            picked = pick_port(ports)
            if not picked:
                print("[ERROR] No port selected; aborting.", file=sys.stderr)
                return 2
            port, name, vid, pid = picked
            tag = f"VID:{vid} PID:{pid}" if vid else ""
            if vid == ESPRESSIF_VID:
                tag = f"Espressif (VID:{vid} PID:{pid})"
            print(f"Detected: {port}  {name}  {tag}".rstrip())
        else:
            print(f"Using user-specified port: {port}")

        if not args.yes:
            if not ask_yes_no(f"Build and flash firmware to {port}?", default_yes=True):
                print("Aborted by user.")
                return 1

        cmd = [py, str(SCRIPTS_DIR / "serial_upload.py"), "--port", port, "--build"]
        rc = run_step("Build + flash firmware", cmd)
        if rc != 0:
            print(f"[ERROR] Firmware build/flash failed (exit {rc}).", file=sys.stderr)
            return rc
        print()
        print("Firmware flashed. The device should now reboot.")

    # Step 3: HTTP data upload — only if device is reachable
    if not do_data:
        return 0

    if not DATA_DIR.is_dir():
        print(f"[ERROR] Data directory not found: {DATA_DIR}", file=sys.stderr)
        return 3

    base = resolve_base_url(args.base)
    if not base:
        print("[ERROR] No reachable device base URL; aborting data upload.", file=sys.stderr)
        return 4

    if not args.yes:
        if not ask_yes_no(f"Upload contents of {DATA_DIR} to {base} ?", default_yes=True):
            print("Skipping data upload.")
            return 0

    cmd = [py, str(SCRIPTS_DIR / "upload_http_data.py"), "--base", base, "--dir", str(DATA_DIR)]
    rc = run_step("Upload data/ to LittleFS via HTTP", cmd)
    if rc != 0:
        print(f"[ERROR] Data upload failed (exit {rc}).", file=sys.stderr)
        return rc

    print()
    print(f"Done. Open the web UI at {base}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
