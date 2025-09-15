#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PYTHON_BIN="${PYTHON:-python3}"
CONVERTER="$ROOT_DIR/tools/png_to_raw565.py"

if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
  echo "Error: python3 interpreter not found in PATH" >&2
  exit 1
fi

if [ ! -f "$CONVERTER" ]; then
  echo "Error: converter script not found at $CONVERTER" >&2
  exit 1
fi

shopt -s nullglob
for dir in textures icons sprites; do
  SRC_DIR="$ROOT_DIR/$dir"
  [ -d "$SRC_DIR" ] || continue
  for img in "$SRC_DIR"/*.png; do
    out="${img%.png}.bin"
    echo "Converting $img -> $out"
    "$PYTHON_BIN" "$CONVERTER" --input "$img" --output "$out"
  done
done
shopt -u nullglob
