import os
import shutil
from pathlib import Path

import pytest


@pytest.fixture(name="sd_mount")
def fixture_sd_mount(tmp_path: Path) -> Path:
    mount = tmp_path / "sdcard_mock"
    mount.mkdir()
    sentinel = mount / "selftest.txt"
    sentinel.write_text("OK SIMULATED 0\n", encoding="utf-8")
    return mount


def test_sd_sentinel_exists(sd_mount: Path) -> None:
    sentinel = sd_mount / "selftest.txt"
    assert sentinel.exists(), "Le fichier sentinel doit exister"
    content = sentinel.read_text(encoding="utf-8")
    assert content.startswith("OK"), content


def test_acceptance_script(tmp_path: Path, sd_mount: Path) -> None:
    log = tmp_path / "boot.log"
    log.write_text(
        "sdmmc_card_print_info\nSD selftest.txt written\nName: SIM\nType: SIM\n",
        encoding="utf-8",
    )
    cmd = [
        "python",
        "tests/acceptance_sd.py",
        "--log",
        str(log),
        "--mount",
        str(sd_mount),
    ]
    exit_code = os.spawnvp(os.P_WAIT, cmd[0], cmd)
    assert exit_code == 0, f"acceptance_sd.py a échoué (code {exit_code})"
