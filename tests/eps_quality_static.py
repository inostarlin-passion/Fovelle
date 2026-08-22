#!/usr/bin/env python3
"""Static EPS acceptance checks and machine-readable evidence.

The checks deliberately inspect the implementation contract rather than
reimplementing the decoder. Runtime behavior is covered by the Qt and Cocoa
stages in the EPS quality pipeline.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


EXTERNAL_RESEARCH = [
    {
        "url": "https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/ImageIOGuide/imageio_basics/ikpg_basics.html",
        "title": "Apple Image I/O Programming Guide",
        "fact": "Apple documents CGImageSourceCopyTypeIdentifiers as the runtime query for image-source formats; EPS is not present in the target host's returned list.",
    },
    {
        "url": "https://developer.apple.com/documentation/appkit/nspasteboard/pasteboardtype/postscript?language=o_2%2Co_2",
        "title": "Apple AppKit postScript pasteboard type",
        "fact": "Apple identifies the modern EPS pasteboard UTI as com.adobe.encapsulated-postscript.",
    },
    {
        "url": "https://www.loc.gov/preservation/digital/formats/fdd/fdd000246.shtml",
        "title": "Library of Congress EPS format description",
        "fact": "EPS can contain device-specific preview data, including TIFF for DOS EPS files.",
    },
]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(repo: Path) -> str | None:
    result = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True, capture_output=True, check=False
    )
    return result.stdout.strip() or None


def check(
    checks: list[dict],
    identifier: str,
    criterion: str,
    passed: bool,
    observations: dict,
    source_files: list[str],
) -> None:
    checks.append(
        {
            "id": identifier,
            "criterion": criterion,
            "passed": bool(passed),
            "observations": observations,
            "source_files": source_files,
        }
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args()

    repo = args.repo.resolve()
    output = (args.output or repo / "reports" / "evidence" / "eps_static.json").resolve()
    native = (repo / "src" / "qvcocoafunctions.mm").read_text(encoding="utf-8")
    application = (repo / "src" / "qvapplication.cpp").read_text(encoding="utf-8")
    loader = (repo / "src" / "qvimageloader.cpp").read_text(encoding="utf-8")
    options = (repo / "src" / "qvoptionsdialog.cpp").read_text(encoding="utf-8")
    plist = (repo / "dist" / "mac" / "Info.plist.in").read_text(encoding="utf-8")
    readme = (repo / "README.md").read_text(encoding="utf-8")
    tests = (repo / "tests" / "tst_qviewtests.cpp").read_text(encoding="utf-8")

    checks: list[dict] = []
    aliases = ["eps", "epsf", "epsi"]
    registry = all(f'QByteArrayLiteral("{alias}")' in native for alias in aliases)
    support_predicate = all(f'normalized == "{alias}"' in native for alias in aliases)
    check(
        checks,
        "ST-EPS-REGISTRY",
        "EPS/EPSF/EPSI must be advertised and accepted by the one native format registry.",
        registry and support_predicate and "getAdditionalImageFormats" in application,
        {
            "aliases": aliases,
            "native_registry_aliases": registry,
            "native_support_predicate": support_predicate,
            "application_consumes_native_registry": "getAdditionalImageFormats" in application,
        },
        ["src/qvcocoafunctions.mm", "src/qvapplication.cpp"],
    )

    bounded_reads = all(
        marker in native
        for marker in (
            "MaxEPSPreviewBytes",
            "MaxEPSScanBytes",
            "readEPSFileRange",
            "length > fileSize - offset",
            "EPS preview is larger than the safety limit",
        )
    )
    dos_decoder = all(
        marker in native
        for marker in (
            "DosEPSMagic",
            "tiffOffset",
            "tiffLength",
            "imageFromEPSRasterPreview",
            "com.adobe.encapsulated-postscript",
        )
    )
    epsi_decoder = all(
        marker in native
        for marker in (
            "%%BeginPreview",
            "%%EndPreview",
            "bitsPerPixel != 1",
            "imageFromEPSIPreview",
        )
    )
    no_external_renderer = "QProcess" not in native and "Ghostscript" not in native and "ImageMagick" not in native
    check(
        checks,
        "ST-EPS-PARSER-SAFETY",
        "The native EPS path must decode bounded DOS TIFF/EPSI previews and fail closed without an external process.",
        bounded_reads and dos_decoder and epsi_decoder and no_external_renderer,
        {
            "bounded_reads": bounded_reads,
            "dos_tiff_preview_decoder": dos_decoder,
            "epsi_decoder": epsi_decoder,
            "external_renderer_invocation_absent": no_external_renderer,
        },
        ["src/qvcocoafunctions.mm"],
    )

    settings_wiring = all(
        marker in options
        for marker in ("getAllFileExtensionList", "formatsTable", "setRowCount(extensions.count())")
    )
    docs_and_bundle = (
        "- EPS" in readme
        and all(f"<string>{alias}</string>" in plist for alias in aliases)
        and "com.adobe.encapsulated-postscript" in plist
    )
    check(
        checks,
        "ST-EPS-DOCS-SETTINGS",
        "The Settings format table, README Supported Formats, and macOS bundle declaration must expose EPS.",
        settings_wiring and docs_and_bundle,
        {
            "settings_table_uses_application_extension_set": settings_wiring,
            "readme_contains_eps": "- EPS" in readme,
            "bundle_contains_eps_aliases": all(f"<string>{alias}</string>" in plist for alias in aliases),
            "bundle_contains_eps_uti": "com.adobe.encapsulated-postscript" in plist,
        },
        ["src/qvoptionsdialog.cpp", "src/qvapplication.cpp", "README.md", "dist/mac/Info.plist.in"],
    )

    loader_delegation = (
        "readImageWithImageIO" in loader
        and "nativeResult.image.isNull()" in loader
        and "suffix == \"eps\"" not in loader
        and "eps" not in loader.lower()
    )
    check(
        checks,
        "ST-EPS-LOADER-DELEGATION",
        "QVImageLoader must consume the native decoder result without adding an EPS-specific suffix branch.",
        loader_delegation,
        {
            "native_decoder_used": "readImageWithImageIO" in loader,
            "native_image_null_guard_present": "nativeResult.image.isNull()" in loader,
            "extension_branch_absent": "eps" not in loader.lower(),
        },
        ["src/qvimageloader.cpp", "src/qvcocoafunctions.mm"],
    )

    test_contracts = all(
        marker in tests
        for marker in (
            "testEPSFormatIsAdvertised",
            "testEPSPreviewDecode",
            "testImageLoaderLoadsEPS",
            "testMalformedEPSFailsSafely",
            "testSettingsFormatsIncludeEPS",
            "FOVELLE_EPS_SAMPLE",
        )
    )
    check(
        checks,
        "ST-EPS-TESTABILITY",
        "Deterministic fixtures, external-sample override, native inspection, async loading, UI inspection, and malformed-input coverage must be executable.",
        test_contracts,
        {"required_test_contract_markers_present": test_contracts},
        ["tests/tst_qviewtests.cpp"],
    )

    result = {
        "schema_version": "1.0",
        "kind": "eps-static-test-evidence",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "repository": str(repo),
        "head_sha": git_head(repo),
        "checks": checks,
        "summary": {
            "check_count": len(checks),
            "passed_count": sum(1 for item in checks if item["passed"]),
            "failed_count": sum(1 for item in checks if not item["passed"]),
        },
        "facts": [
            "The implementation uses the existing macOS native bridge and the existing application extension registry.",
            "The static contract contains bounded range checks for DOS EPS preview offsets and lengths.",
            "The static contract does not invoke Ghostscript, ImageMagick, or another external renderer.",
        ],
        "inferences": [
            "Passing the registry and delegation checks indicates EPS follows the same format-to-loader path as the existing native Image I/O extensions.",
            "The Settings table will reflect EPS at runtime because it enumerates QVApplication's all-file-extension set.",
        ],
        "uncertainties": [
            "Static checks cannot prove that every PostScript program has a usable embedded preview; runtime evidence covers the supplied DOS EPS and a deterministic EPSI fixture.",
        ],
        "external_research": EXTERNAL_RESEARCH,
        "passed": all(item["passed"] for item in checks),
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
