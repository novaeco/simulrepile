#!/usr/bin/env python3
"""Convert PNG images to raw RGB565 binaries compatible with LVGL."""

from __future__ import annotations

import argparse
import struct
import sys
import zlib
from pathlib import Path
from typing import Iterable, List, Sequence, Tuple

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class PngDecodeError(RuntimeError):
    """Raised when a PNG file cannot be decoded."""


def _read_chunks(data: bytes) -> Iterable[Tuple[bytes, bytes]]:
    offset = 8
    size = len(data)
    while offset < size:
        if offset + 8 > size:
            raise PngDecodeError("Truncated PNG chunk header")
        length = struct.unpack_from(">I", data, offset)[0]
        offset += 4
        chunk_type = data[offset:offset + 4]
        offset += 4
        if offset + length + 4 > size:
            raise PngDecodeError("Truncated PNG chunk payload")
        chunk_payload = data[offset:offset + length]
        offset += length
        offset += 4  # skip CRC
        yield chunk_type, chunk_payload
        if chunk_type == b"IEND":
            break


def _paeth_predictor(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa = abs(p - a)
    pb = abs(p - b)
    pc = abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _reconstruct_scanlines(raw: bytes, width: int, height: int, bpp: int, row_bytes: int) -> List[bytes]:
    rows: List[bytes] = []
    idx = 0
    prev = bytearray(row_bytes)
    for _ in range(height):
        if idx >= len(raw):
            raise PngDecodeError("Unexpected end of image data")
        filter_type = raw[idx]
        idx += 1
        if idx + row_bytes > len(raw):
            raise PngDecodeError("Scanline exceeds available data")
        scan = bytearray(raw[idx:idx + row_bytes])
        idx += row_bytes
        recon = bytearray(row_bytes)
        if filter_type == 0:  # None
            recon[:] = scan
        elif filter_type == 1:  # Sub
            for i in range(row_bytes):
                left = recon[i - bpp] if i >= bpp else 0
                recon[i] = (scan[i] + left) & 0xFF
        elif filter_type == 2:  # Up
            for i in range(row_bytes):
                recon[i] = (scan[i] + prev[i]) & 0xFF
        elif filter_type == 3:  # Average
            for i in range(row_bytes):
                left = recon[i - bpp] if i >= bpp else 0
                up = prev[i]
                recon[i] = (scan[i] + ((left + up) >> 1)) & 0xFF
        elif filter_type == 4:  # Paeth
            for i in range(row_bytes):
                left = recon[i - bpp] if i >= bpp else 0
                up = prev[i]
                up_left = prev[i - bpp] if i >= bpp else 0
                recon[i] = (scan[i] + _paeth_predictor(left, up, up_left)) & 0xFF
        else:
            raise PngDecodeError(f"Unsupported PNG filter type {filter_type}")
        rows.append(bytes(recon))
        prev = recon
    if len(rows) != height:
        raise PngDecodeError("Decoded scanline count mismatch")
    return rows


def _expand_samples(row: bytes, bit_depth: int, samples_per_pixel: int) -> List[int]:
    if bit_depth == 8:
        return list(row)
    if bit_depth < 8:
        pixels: List[int] = []
        value_mask = (1 << bit_depth) - 1
        bits_per_pixel = samples_per_pixel * bit_depth
        # For indexed and grayscale images with bit depths < 8 the samples per pixel is 1 or 2.
        per_byte = 8 // bit_depth
        for byte in row:
            for shift in range(8 - bit_depth, -1, -bit_depth):
                pixels.append((byte >> shift) & value_mask)
                if len(pixels) * bit_depth >= len(row) * 8:
                    break
        return pixels
    if bit_depth == 16:
        if len(row) % 2 != 0:
            raise PngDecodeError("16-bit channel data length is not even")
        pixels16 = struct.unpack(f">{len(row) // 2}H", row)
        return [value >> 8 for value in pixels16]
    raise PngDecodeError(f"Unsupported bit depth {bit_depth}")


def _decode_png(path: Path) -> Tuple[int, int, List[Tuple[int, int, int, int]]]:
    data = path.read_bytes()
    if not data.startswith(PNG_SIGNATURE):
        raise PngDecodeError("Not a PNG file")

    width = height = bit_depth = color_type = None
    compression = filter_method = interlace = None
    palette: List[Tuple[int, int, int]] = []
    alpha_table: List[int] = []
    idat_chunks: List[bytes] = []

    for chunk_type, payload in _read_chunks(data):
        if chunk_type == b"IHDR":
            if len(payload) != 13:
                raise PngDecodeError("Invalid IHDR length")
            width, height, bit_depth, color_type, compression, filter_method, interlace = struct.unpack(
                ">IIBBBBB", payload
            )
        elif chunk_type == b"PLTE":
            if len(payload) % 3 != 0:
                raise PngDecodeError("Invalid palette length")
            palette = [tuple(payload[i:i + 3]) for i in range(0, len(payload), 3)]
        elif chunk_type == b"tRNS":
            alpha_table = list(payload)
        elif chunk_type == b"IDAT":
            idat_chunks.append(payload)
        elif chunk_type == b"IEND":
            break

    if None in (width, height, bit_depth, color_type, compression, filter_method, interlace):
        raise PngDecodeError("Missing required PNG headers")
    if compression != 0 or filter_method != 0 or interlace != 0:
        raise PngDecodeError("Unsupported PNG compression, filter, or interlace method")
    if not idat_chunks:
        raise PngDecodeError("PNG contains no image data")

    bits_per_pixel_map = {
        0: 1,  # grayscale
        2: 3,  # truecolor
        3: 1,  # indexed color
        4: 2,  # grayscale + alpha
        6: 4,  # truecolor + alpha
    }
    if color_type not in bits_per_pixel_map:
        raise PngDecodeError(f"Unsupported PNG color type {color_type}")
    samples_per_pixel = bits_per_pixel_map[color_type]
    bits_per_pixel = samples_per_pixel * bit_depth
    row_bytes = (width * bits_per_pixel + 7) // 8
    bytes_per_pixel = max(1, (samples_per_pixel * bit_depth + 7) // 8)

    try:
        decompressed = zlib.decompress(b"".join(idat_chunks))
    except zlib.error as exc:
        raise PngDecodeError(f"Failed to decompress IDAT data: {exc}") from exc

    rows = _reconstruct_scanlines(decompressed, width, height, bytes_per_pixel, row_bytes)

    rgba_pixels: List[Tuple[int, int, int, int]] = []

    for row in rows:
        if color_type == 3:  # indexed color
            if bit_depth not in (1, 2, 4, 8):
                raise PngDecodeError(f"Unsupported palette bit depth {bit_depth}")
            expanded = _expand_samples(row, bit_depth, 1)
            for index in expanded[:width]:
                if index >= len(palette):
                    raise PngDecodeError(f"Palette index {index} out of range")
                r, g, b = palette[index]
                a = alpha_table[index] if index < len(alpha_table) else 255
                rgba_pixels.append((r, g, b, a))
        elif color_type == 2:  # truecolor
            if bit_depth != 8:
                raise PngDecodeError(f"Unsupported truecolor bit depth {bit_depth}")
            for i in range(0, len(row), 3):
                rgba_pixels.append((row[i], row[i + 1], row[i + 2], 255))
        elif color_type == 6:  # truecolor + alpha
            if bit_depth != 8:
                raise PngDecodeError(f"Unsupported truecolor+alpha bit depth {bit_depth}")
            for i in range(0, len(row), 4):
                rgba_pixels.append((row[i], row[i + 1], row[i + 2], row[i + 3]))
        elif color_type == 0:  # grayscale
            if bit_depth != 8:
                raise PngDecodeError(f"Unsupported grayscale bit depth {bit_depth}")
            for value in row[:width]:
                rgba_pixels.append((value, value, value, 255))
        elif color_type == 4:  # grayscale + alpha
            if bit_depth != 8:
                raise PngDecodeError(f"Unsupported grayscale+alpha bit depth {bit_depth}")
            for i in range(0, len(row), 2):
                gray = row[i]
                alpha = row[i + 1]
                rgba_pixels.append((gray, gray, gray, alpha))
        else:
            raise PngDecodeError(f"Unsupported color type {color_type}")

    if len(rgba_pixels) != width * height:
        raise PngDecodeError("Decoded pixel count mismatch")

    return width, height, rgba_pixels


def _blend_channel(src: int, dst: int, alpha: int) -> int:
    return (src * alpha + dst * (255 - alpha) + 127) // 255


def _rgba_to_rgb565(pixels: Sequence[Tuple[int, int, int, int]], background: Tuple[int, int, int], swap: bool) -> bytes:
    bg_r, bg_g, bg_b = background
    output = bytearray()
    for r, g, b, a in pixels:
        if a < 255:
            r = _blend_channel(r, bg_r, a)
            g = _blend_channel(g, bg_g, a)
            b = _blend_channel(b, bg_b, a)
        value = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3)
        if swap:
            output.append(value & 0xFF)
            output.append((value >> 8) & 0xFF)
        else:
            output.append((value >> 8) & 0xFF)
            output.append(value & 0xFF)
    return bytes(output)


def _parse_background(color: str) -> Tuple[int, int, int]:
    value = color.lstrip("#")
    if len(value) != 6:
        raise argparse.ArgumentTypeError("Background color must be in RRGGBB format")
    try:
        rgb = int(value, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("Background color must be hexadecimal") from exc
    return ((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF)


def _write_if_different(path: Path, data: bytes) -> None:
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def convert(input_path: Path, output_path: Path, background: Tuple[int, int, int], swap: bool) -> None:
    width, height, pixels = _decode_png(input_path)
    raw565 = _rgba_to_rgb565(pixels, background, swap)
    expected_len = width * height * 2
    if len(raw565) != expected_len:
        raise RuntimeError(
            f"Conversion of {input_path} produced {len(raw565)} bytes, expected {expected_len}"
        )
    _write_if_different(output_path, raw565)


def main(argv: Sequence[str]) -> int:
    parser = argparse.ArgumentParser(description="Convert PNG images to raw RGB565 binaries.")
    parser.add_argument("--input", required=True, type=Path, help="Input PNG file")
    parser.add_argument("--output", required=True, type=Path, help="Output .bin file")
    parser.add_argument(
        "--background",
        default="#000000",
        type=_parse_background,
        help="Background color (RRGGBB) used when compositing transparency (default: #000000)",
    )
    parser.add_argument(
        "--no-swap",
        action="store_true",
        help="Disable byte swapping (default swaps bytes to match little-endian RGB565)",
    )

    args = parser.parse_args(argv)
    try:
        convert(args.input, args.output, args.background, swap=not args.no_swap)
    except (PngDecodeError, RuntimeError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
