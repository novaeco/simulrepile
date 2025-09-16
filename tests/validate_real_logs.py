#!/usr/bin/env python3
"""Validate CSV logs produced in real mode (components/logging/logging_real.c)."""

import csv
import sys
from pathlib import Path

EXPECTED_HEADER = [
    "timestamp",
    "temp_c",
    "humidity_pct",
    "target_temp_c",
    "target_humidity_pct",
    "heating",
    "pumping",
    "uv",
    "manual_heat",
    "manual_pump",
    "manual_uv",
    "energy_heat_wh",
    "energy_pump_wh",
    "energy_uv_wh",
    "total_energy_wh",
    "alarm_flags",
]


def validate_file(path: Path) -> bool:
    ok = True
    with path.open(newline="") as f:
        reader = csv.reader(f)
        try:
            header = next(reader)
        except StopIteration:
            print(f"[ERROR] {path}: empty file", file=sys.stderr)
            return False
        if header != EXPECTED_HEADER:
            print(f"[ERROR] {path}: unexpected header {header}", file=sys.stderr)
            ok = False
        for line_no, row in enumerate(reader, start=2):
            if len(row) != len(EXPECTED_HEADER):
                print(f"[ERROR] {path}:{line_no} -> column count {len(row)}", file=sys.stderr)
                ok = False
                continue
            try:
                int(row[0])
                float(row[1])
                float(row[2])
                float(row[3])
                float(row[4])
                int(row[5])
                int(row[6])
                int(row[7])
                int(row[8])
                int(row[9])
                int(row[10])
                float(row[11])
                float(row[12])
                float(row[13])
                float(row[14])
                int(row[15])
            except ValueError as exc:
                print(f"[ERROR] {path}:{line_no} -> {exc}", file=sys.stderr)
                ok = False
    return ok


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(f"Usage: {argv[0]} <log1.csv> [log2.csv ...]", file=sys.stderr)
        return 1
    success = True
    for arg in argv[1:]:
        path = Path(arg)
        if not path.exists():
            print(f"[ERROR] {path}: file not found", file=sys.stderr)
            success = False
            continue
        if not validate_file(path):
            success = False
    return 0 if success else 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
