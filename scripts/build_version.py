"""Inject reproducible firmware version and Git identity macros."""

from __future__ import annotations

import subprocess
import os
from pathlib import Path

Import("env")  # type: ignore[name-defined]  # Provided by PlatformIO/SCons.


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))  # type: ignore[name-defined]


def git(*args: str) -> str:
  try:
    return subprocess.check_output(
        ["git", *args], cwd=PROJECT_DIR, text=True,
        stderr=subprocess.DEVNULL).strip()
  except (OSError, subprocess.CalledProcessError):
    return ""


commit = git("rev-parse", "--short=7", "HEAD") or "unknown"
commit_date = git("show", "-s", "--format=%cs", "HEAD") or "unknown"
if commit != "unknown" and git("status", "--porcelain"):
  commit += "-dirty"

version = env.GetProjectOption("custom_firmware_version", "0.3.0")  # type: ignore[name-defined]
release_sequence = os.environ.get(
    "BLEEP_RELEASE_SEQUENCE",
    env.GetProjectOption("custom_release_sequence", "0"))  # type: ignore[name-defined]
release_channel = os.environ.get(
    "BLEEP_RELEASE_CHANNEL",
    env.GetProjectOption("custom_release_channel", "local"))  # type: ignore[name-defined]
version = os.environ.get("BLEEP_FIRMWARE_VERSION", version)
env.Append(CPPDEFINES=[  # type: ignore[name-defined]
    ("BLEEP_FIRMWARE_VERSION", env.StringifyMacro(version)),
    ("BLEEP_GIT_COMMIT", env.StringifyMacro(commit)),
    ("BLEEP_GIT_DATE", env.StringifyMacro(commit_date)),
    ("BLEEP_RELEASE_SEQUENCE", release_sequence + "ULL"),
    ("BLEEP_RELEASE_CHANNEL", env.StringifyMacro(release_channel)),
])
