"""Inject reproducible firmware version and Git identity macros."""

from __future__ import annotations

import subprocess
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

version = env.GetProjectOption("custom_firmware_version", "0.2.0-dev")  # type: ignore[name-defined]
env.Append(CPPDEFINES=[  # type: ignore[name-defined]
    ("BLEEP_FIRMWARE_VERSION", env.StringifyMacro(version)),
    ("BLEEP_GIT_COMMIT", env.StringifyMacro(commit)),
    ("BLEEP_GIT_DATE", env.StringifyMacro(commit_date)),
])
