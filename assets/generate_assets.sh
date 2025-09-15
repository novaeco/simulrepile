#!/usr/bin/env bash
set -e
ROOT_DIR=$(dirname "$0")
for dir in textures icons sprites; do
  if [ -d "$ROOT_DIR/$dir" ]; then
    for img in "$ROOT_DIR/$dir"/*.png; do
      [ -e "$img" ] || continue
      out="${img%.png}.bin"
      echo "Converting $img -> $out"
      lv_img_conv --format raw --bpp 16 --swap -i "$img" -o "$out"
    done
  fi
done

