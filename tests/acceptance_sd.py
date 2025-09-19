#!/usr/bin/env python3
"""Validate SD-card bring-up logs and sentinel file creation."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

TIMEOUT_PATTERN = re.compile(r"timeout", re.IGNORECASE)
INFO_PATTERN = re.compile(r"sdmmc_card_print_info", re.IGNORECASE)
SELFTEST_PATTERN = re.compile(r"SD selftest.txt written")
NAME_PATTERN = re.compile(r"^.*Name:\s+", re.IGNORECASE)
TYPE_PATTERN = re.compile(r"^.*Type:\s+", re.IGNORECASE)


def check_log(path: Path) -> list[str]:
    errors: list[str] = []
    if not path.exists():
        return [f"[LOG] {path}: fichier introuvable"]

    text = path.read_text(encoding="utf-8", errors="ignore")
    if TIMEOUT_PATTERN.search(text):
        errors.append(f"[LOG] {path}: des timeouts SD apparaissent encore")
    if not INFO_PATTERN.search(text):
        errors.append(f"[LOG] {path}: sdmmc_card_print_info absent")
    if not SELFTEST_PATTERN.search(text):
        errors.append(f"[LOG] {path}: message 'SD selftest.txt written' manquant")

    name_ok = any(NAME_PATTERN.match(line) for line in text.splitlines())
    type_ok = any(TYPE_PATTERN.match(line) for line in text.splitlines())
    if not name_ok or not type_ok:
        errors.append(f"[LOG] {path}: informations carte incomplètes (Name/Type)")

    return errors


def check_mount(mount: Path) -> list[str]:
    errors: list[str] = []
    if not mount.exists():
        return [f"[MOUNT] {mount}: point de montage absent"]

    sentinel = mount / "selftest.txt"
    if not sentinel.exists():
        errors.append(f"[MOUNT] {sentinel}: fichier selftest manquant")
        return errors

    content = sentinel.read_text(encoding="utf-8", errors="ignore").strip()
    if not content.startswith("OK"):
        errors.append(f"[MOUNT] {sentinel}: contenu inattendu -> '{content}'")
    return errors


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", dest="logs", action="append", required=True,
                        help="Chemin vers un fichier de log à analyser")
    parser.add_argument("--mount", dest="mount", type=Path, required=True,
                        help="Répertoire représentant /sdcard")
    args = parser.parse_args(argv[1:])

    failures: list[str] = []
    for log_path in args.logs:
        failures.extend(check_log(Path(log_path)))

    failures.extend(check_mount(args.mount))

    if failures:
        for failure in failures:
            print(failure, file=sys.stderr)
        return 1

    print("SD acceptance checks passed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
