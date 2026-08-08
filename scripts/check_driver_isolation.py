#!/usr/bin/env python3
"""Verify that isolated firmware profiles link only their selected drivers."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
CORE = Path(os.environ.get("PLATFORMIO_CORE_DIR", ROOT / ".platformio-core"))

FAMILIES = {
    "shark": ("studio::SharkDriver", "shark::SharkClient"),
    "canon_ble": ("studio::CanonBleDriver", "canon_ble::CanonBleClient"),
    "canon_trigger": (
        "studio::CanonTriggerDriver",
        "canon_trigger::CanonTriggerClient",
    ),
    "tascam": ("studio::TascamX8Driver", "tascam_x8::TascamX8Client"),
    "home_assistant": (
        "studio::HomeAssistantDriver",
        "home_assistant::HomeAssistantClient",
    ),
    "amaran": ("studio::AmaranLightDriver", "amaran_light::AmaranRuntime"),
    "zhiyun": ("studio::ZhiyunLightDriver", "zhiyun_x100::X100Client"),
}

# Zhiyun mesh nodes intentionally use the shared Amaran proxy runtime without
# linking the Amaran driver adapter.
PROFILES = {
    "shark_nano_ii": {"shark"},
    "canon_ble": {"canon_ble"},
    "canon_trigger": {"canon_trigger"},
    "tascam_x8": {"tascam"},
    "home_assistant": {"home_assistant"},
    "amaran_light": {"amaran"},
    "zhiyun_x100": {"zhiyun", "amaran"},
}

DRIVER_GLOBAL_CONSTRUCTORS = (
    "_GLOBAL__sub_I__ZN5shark",
    "_GLOBAL__sub_I__ZN9canon_ble",
    "_GLOBAL__sub_I__ZN13canon_trigger",
    "_GLOBAL__sub_I__ZN9tascam_x8",
    "_GLOBAL__sub_I__ZN14home_assistant",
    "_GLOBAL__sub_I__ZN12amaran_light",
    "_GLOBAL__sub_I__ZN11zhiyun_x100",
)


def nm_path() -> str:
    candidate = shutil.which("riscv32-esp-elf-nm")
    if candidate:
        return candidate
    bundled = CORE / "packages/toolchain-riscv32-esp/bin/riscv32-esp-elf-nm"
    if bundled.exists():
        return str(bundled)
    raise FileNotFoundError("riscv32-esp-elf-nm was not found")


def symbols(nm: str, profile: str) -> str:
    elf = ROOT / ".pio/build" / profile / "firmware.elf"
    if not elf.exists():
        raise FileNotFoundError(f"missing {elf}; build {profile} first")
    return subprocess.run(
        [nm, "-C", str(elf)],
        check=True,
        capture_output=True,
        text=True,
    ).stdout


def main() -> int:
    nm = nm_path()
    failures: list[str] = []
    for profile, allowed in PROFILES.items():
        linked = symbols(nm, profile)
        for family, tokens in FAMILIES.items():
            present = any(token in linked for token in tokens)
            if family in allowed and not present:
                failures.append(f"{profile}: missing expected {family} symbols")
            if family not in allowed and present:
                failures.append(f"{profile}: unexpectedly links {family}")
        if profile == "home_assistant" and "studio::ble::bleCentral" in linked:
            failures.append("home_assistant: unexpectedly links BLE central")
        for constructor in DRIVER_GLOBAL_CONSTRUCTORS:
            if constructor in linked:
                failures.append(
                    f"{profile}: driver namespace-scope constructor {constructor}"
                )

    if failures:
        print("Driver isolation audit failed:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1
    print(f"Driver isolation audit passed for {len(PROFILES)} profiles")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
