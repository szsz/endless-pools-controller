#!/usr/bin/env python3
"""
Upload a local directory's files into the ESP32 LittleFS via HTTP, one file per request.

- The device must be running the firmware exposing:
    POST /api/upload?path=relative/sub/file
    Headers:
      X-PSK: <pre-shared-key>
      Content-Type: application/octet-stream
    Body: raw file bytes

- The PSK must match the device: first 10 characters of OTA_PASSWORD (from otapassword.h) or the value configured via sketch.yaml.
  This script resolves PSK in the following priority:
    1) -k/--psk argument
    2) UPLOAD_PSK environment variable
    3) sketch.yaml: ota_password (first 10 chars)
    4) otapassword.h: OTA_PASSWORD (first 10 chars)
  It will also automatically try all available candidates if it receives HTTP 401 Unauthorized.

Usage:
  python scripts/upload_http_data.py --dir data
  python scripts/upload_http_data.py --base http://192.168.1.123 --dir data
  # Optional override:
  python scripts/upload_http_data.py --dir data -k YOUR_PSK

Notes:
- Remote path is derived from the relative path under --dir. For example:
    local: data/static/app.js  -> remote: static/app.js
    local: data/index.html     -> remote: index.html
- Subfolders will be created automatically by the device.
"""

import argparse
import json
import os
import sys
import time
from pathlib import Path
from typing import List, Tuple
from urllib.parse import quote
from urllib.request import Request, urlopen
from urllib.error import HTTPError, URLError

SKIP_NAMES = {'.DS_Store', 'Thumbs.db'}

REPO_ROOT = Path(__file__).resolve().parent.parent

def clean_yaml_value(s: str | None) -> str | None:
    if s is None:
        return None
    s = s.strip()
    # strip inline comments
    import re as _re
    m = _re.match(r"^(.*?)(\s+#.*)?$", s)
    if m:
        s = m.group(1).strip()
    # strip surrounding quotes
    if len(s) >= 2 and ((s.startswith('"') and s.endswith('"')) or (s.startswith("'") and s.endswith("'"))):
        s = s[1:-1]
    return s

def read_yaml_ota_password(sketch_yaml_path: Path) -> str | None:
    """
    Read ota_password from sketch.yaml if present. Returns the raw password string (not truncated),
    or None if not found or placeholder.
    """
    try:
        if not sketch_yaml_path.is_file():
            return None
        text = sketch_yaml_path.read_text(encoding="utf-8", errors="ignore")
        import re as _re
        m = _re.search(r"(?m)^\s*ota_password:\s*(.+)$", text)
        if not m:
            return None
        val = clean_yaml_value(m.group(1))
        if not val or val == "REPLACE_WITH_OTA_PASSWORD":
            return None
        return val
    except Exception:
        return None

def derive_psk_from_header(header_path: str = "otapassword.h") -> str | None:
    """
    Parse otapassword.h and return the first 10 characters of OTA_PASSWORD, or None if not found.
    """
    try:
        with open(header_path, "r", encoding="utf-8") as f:
            txt = f.read()
        # Look for a line like: #define OTA_PASSWORD "...."
        key = 'OTA_PASSWORD'
        if key not in txt:
            return None
        # naive parse: find first quote after OTA_PASSWORD
        idx = txt.find(key)
        q1 = txt.find('"', idx)
        if q1 == -1:
            q1 = txt.find("'", idx)
        if q1 == -1:
            return None
        q2 = txt.find('"', q1 + 1) if txt[q1] == '"' else txt.find("'", q1 + 1)
        if q2 == -1:
            return None
        value = txt[q1 + 1:q2]
        if not value:
            return None
        return value[:10]
    except Exception:
        return None

def make_psk_candidates(cli_psk: str | None) -> List[Tuple[str, str]]:
    """
    Build a prioritized list of (source_label, psk_value) candidates.
    Priority: CLI > env > sketch.yaml (first10) > header (first10)
    """
    cands: List[Tuple[str, str]] = []
    seen: set[str] = set()

    def add(label: str, value: str | None):
        if not value:
            return
        if value in seen:
            return
        seen.add(value)
        cands.append((label, value))

    yaml_pw = read_yaml_ota_password(REPO_ROOT / "sketch.yaml")
    env_psk = os.environ.get("UPLOAD_PSK")
    header_psk = derive_psk_from_header()

    add("cli -k", cli_psk)
    add("env UPLOAD_PSK", env_psk)
    add("sketch.yaml ota_password (first10)", (yaml_pw[:10] if yaml_pw else None))
    add("otapassword.h first10", header_psk)

    return cands

def upload_file_with_psk_candidates(base: str, psk_cands: List[Tuple[str, str]], rel_path: str, local_path: str, retries: int = 2) -> Tuple[dict, Tuple[str, str]]:
    """
    Try uploading a file using a list of PSK candidates.
    Falls back to next candidate on HTTP 401 Unauthorized.
    Returns (json_response, (label, psk)) for the successful candidate.
    Raises RuntimeError if all candidates fail.
    """
    url = f"{base.rstrip('/')}/api/upload?path={quote(rel_path.replace(os.sep, '/'))}"
    with open(local_path, 'rb') as f:
        data = f.read()

    last_err = None
    for label, psk in psk_cands:
        req = Request(url, data=data, method='POST')
        req.add_header('X-PSK', psk or '')
        req.add_header('Content-Type', 'application/octet-stream')

        for attempt in range(retries + 1):
            try:
                with urlopen(req, timeout=30) as resp:
                    body = resp.read()
                    try:
                        return json.loads(body.decode('utf-8', errors='replace')), (label, psk)
                    except Exception:
                        # Not JSON? return text
                        return {'path': rel_path, 'size': len(data), 'raw': body.decode('utf-8', errors='replace')}, (label, psk)
            except HTTPError as e:
                # 401 Unauthorized => try next PSK candidate
                if e.code == 401:
                    last_err = f"HTTP 401 Unauthorized (with {label})"
                    break  # try next candidate
                last_err = f"HTTP {e.code} {e.reason}: {e.read().decode('utf-8', errors='replace')}"
            except URLError as e:
                last_err = f"URL error: {e.reason}"
            except Exception as e:
                last_err = f"Error: {e}"

            if attempt < retries:
                time.sleep(0.5 * (attempt + 1))
            else:
                # move to next candidate only for 401; otherwise stop
                if isinstance(last_err, str) and "401" in last_err:
                    break
                raise RuntimeError(f"Upload failed for {rel_path} with {label}: {last_err}")

        # next candidate

    raise RuntimeError(f"Upload failed for {rel_path}: {last_err or 'no valid PSK candidates succeeded'}")

def walk_files(root_dir: str):
    """
    Yield (rel_path, abs_path) pairs for all files under root_dir.
    """
    root_dir = os.path.abspath(root_dir)
    for dirpath, _, filenames in os.walk(root_dir):
        for fn in filenames:
            if fn in SKIP_NAMES:
                continue
            abs_path = os.path.join(dirpath, fn)
            rel_path = os.path.relpath(abs_path, root_dir)
            yield (rel_path, abs_path)

def main():
    ap = argparse.ArgumentParser(description="Upload files to ESP32 LittleFS via HTTP")
    ap.add_argument('-b', '--base', default='http://swimmachine.local', help='Base URL (default: http://swimmachine.local), e.g. http://esp32.local or http://192.168.1.50')
    ap.add_argument('-d', '--dir', default='data', help='Local directory to upload (default: data)')
    ap.add_argument('-k', '--psk', default=None, help='Pre-shared key; default resolution: -k > env UPLOAD_PSK > sketch.yaml ota_password (first10) > otapassword.h (first10)')
    ap.add_argument('--dry-run', action='store_true', help='List files but do not upload')
    args = ap.parse_args()

    if not os.path.isdir(args.dir):
        print(f"[ERROR] Directory not found: {args.dir}", file=sys.stderr)
        sys.exit(2)

    # Build PSK candidates and print info (masked)
    psk_cands = make_psk_candidates(args.psk)
    if not psk_cands:
        print("[ERROR] No PSK candidates available. Provide -k or set ota_password in sketch.yaml or define OTA_PASSWORD in otapassword.h.", file=sys.stderr)
        sys.exit(3)

    def mask(psk: str) -> str:
        return psk[:2] + "*" * max(0, len(psk) - 4) + psk[-2:] if len(psk) >= 4 else "***"

    print("[INFO] PSK candidates (in order):")
    for label, p in psk_cands:
        print(f"  - {label}: {mask(p)}")

    total = 0
    ok = 0
    chosen_label_psk: Tuple[str, str] | None = None
    start = time.time()

    for rel, abs_path in walk_files(args.dir):
        total += 1
        remote_rel = rel.replace('\\', '/')
        print(f"[{total}] {rel} -> {remote_rel}", flush=True)
        if args.dry_run:
            continue
        # After first success, reuse the successful PSK only
        try_cands = [chosen_label_psk] if chosen_label_psk else psk_cands
        try_cands = [c for c in try_cands if c]  # drop None

        try:
            resp, chosen = upload_file_with_psk_candidates(args.base, try_cands, remote_rel, abs_path)
            ok += 1
            if not chosen_label_psk:
                chosen_label_psk = chosen
                print(f"  Using PSK source: {chosen_label_psk[0]}")
            print(f"  OK  ({resp.get('size', '?')} bytes) -> {resp.get('path', remote_rel)}")
        except Exception as e:
            print(f"  FAIL: {e}", file=sys.stderr)

    dur = time.time() - start
    print(f"\nDone. {ok}/{total} files uploaded in {dur:.1f}s (base={args.base})")
    if not args.dry_run and ok != total:
        sys.exit(1)

if __name__ == '__main__':
    main()
