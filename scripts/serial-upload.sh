#!/usr/bin/env bash
# Cross-platform Bash helper to build (optional) and upload firmware over serial.
# Wraps scripts/serial_upload.py so you can call it from Bash on Linux/macOS/WSL.
set -euo pipefail

# Resolve repo root (this file lives in repo_root/scripts/)
SCRIPT_DIR="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
REPO_ROOT="${SCRIPT_DIR%/scripts}"

# Choose Python interpreter
if command -v python3 >/dev/null 2>&1; then
  PY=python3
elif command -v python >/dev/null 2>&1; then
  PY=python
else
  echo "ERROR: Python 3 is required (python3 or python not found in PATH)." >&2
  exit 127
fi

cd "$REPO_ROOT"
exec "$PY" "scripts/serial_upload.py" "$@"
