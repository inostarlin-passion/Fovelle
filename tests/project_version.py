"""Read the repository's single product-version source."""

from __future__ import annotations

import re
from pathlib import Path


_SEMVER = re.compile(r"[0-9]+\.[0-9]+\.[0-9]+")


def read_project_version(repo: Path | None = None) -> str:
    """Return the validated version from the repository VERSION file."""

    root = (repo or Path(__file__).resolve().parents[1]).resolve()
    version_path = root / "VERSION"
    version = version_path.read_text(encoding="utf-8").strip()
    if _SEMVER.fullmatch(version) is None:
        raise ValueError(
            f"{version_path} must contain MAJOR.MINOR.PATCH without a v prefix"
        )
    return version


PROJECT_VERSION = read_project_version()
