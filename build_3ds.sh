#!/usr/bin/env bash
# Build the CIA and optionally send it to the console over FTP.
# Interactive menu with no arguments; see `./build_3ds.sh --help`.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if command -v python3 >/dev/null 2>&1; then
    PYTHON_BIN="python3"
elif command -v python >/dev/null 2>&1; then
    PYTHON_BIN="python"
else
    echo "Error: Python 3 not found." >&2
    exit 1
fi

exec "$PYTHON_BIN" "$ROOT_DIR/platform/3ds/tools/build_3ds.py" "$@"
