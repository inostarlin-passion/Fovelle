#!/usr/bin/env python3
"""Audit the rendered right edge of Settings labels in saved screenshots."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from PIL import Image


GENERAL_ROWS = (
    ("language", 103, 113),
    ("appearance", 151, 162),
    ("smooth_scaling", 229, 240),
    ("slideshow_direction", 341, 352),
    ("slideshow_interval", 377, 388),
    ("after_deletion", 420, 430),
    ("auto_update_check", 498, 509),
)

MOUSE_ROWS = (
    ("double_click", 134, 144),
    ("alt_double_click", 176, 186),
    ("drag", 218, 228),
    ("alt_drag", 260, 270),
    ("mode", 361, 371),
    ("middle_click", 397, 407),
    ("alt_middle_click", 439, 449),
    ("vertical_scroll", 516, 526),
    ("horizontal_scroll", 558, 569),
    ("alt_vertical_scroll", 600, 610),
    ("alt_horizontal_scroll", 642, 653),
)


def audit_screenshot(
    path: Path,
    rows: tuple[tuple[str, int, int], ...],
    label_x_limit: int,
    threshold: int = 180,
) -> dict[str, Any]:
    image = Image.open(path).convert("L")
    width, height = image.size
    pixels = image.load()
    observations: list[dict[str, Any]] = []

    for name, top, bottom in rows:
        if top < 0 or bottom >= height or top > bottom:
            observations.append(
                {
                    "name": name,
                    "row": [top, bottom],
                    "passed": False,
                    "error": "row band is outside screenshot",
                }
            )
            continue

        coordinates = [
            (x, y)
            for y in range(top, bottom + 1)
            for x in range(min(label_x_limit, width))
            if pixels[x, y] < threshold
        ]
        right_edge = max((x for x, _ in coordinates), default=None)
        observations.append(
            {
                "name": name,
                "row": [top, bottom],
                "right_edge": right_edge,
                "dark_pixel_count": len(coordinates),
                "passed": right_edge is not None,
            }
        )

    edges = [item.get("right_edge") for item in observations]
    passed = bool(observations) and all(item["passed"] for item in observations)
    passed = passed and len(set(edges)) == 1
    return {
        "path": str(path),
        "size": [width, height],
        "threshold": threshold,
        "label_x_limit_exclusive": label_x_limit,
        "expected_terminal_colon": "U+003A",
        "rows": observations,
        "right_edges": edges,
        "right_edges_equal": len(set(edges)) == 1,
        "passed": passed,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--general", type=Path, required=True)
    parser.add_argument("--mouse", type=Path, required=True)
    args = parser.parse_args()

    audits = {
        "general": audit_screenshot(args.general, GENERAL_ROWS, 106),
        "mouse": audit_screenshot(args.mouse, MOUSE_ROWS, 121),
    }
    result = {
        "schema_version": "1.0",
        "audit": "rendered_settings_label_right_edges",
        "audits": audits,
        "passed": all(item["passed"] for item in audits.values()),
    }
    print(json.dumps(result, ensure_ascii=False, sort_keys=True))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
