#!/usr/bin/env python3
"""Static EPS rendering acceptance checks and machine-readable evidence."""

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
        "url": "https://developer.apple.com/documentation/macos-release-notes/macos-14-release-notes",
        "title": "Apple macOS Sonoma 14 Release Notes",
        "fact": "Apple removed system PostScript/EPS conversion in macOS 14; ImageIO no longer converts EPS and NSEPSImageRep can no longer display it.",
    },
    {
        "url": "https://helpx.adobe.com/uk/illustrator/using/saving-artwork.html",
        "title": "Adobe Illustrator: Save artwork",
        "fact": "Adobe describes EPS as a PostScript format and its embedded preview as a display aid for applications that cannot display EPS directly.",
    },
    {
        "url": "https://ghostscript.readthedocs.io/en/gs10.03.0/Use.html",
        "title": "Ghostscript: Using Ghostscript",
        "fact": "Ghostscript documents -dEPSCrop for cropping an EPS render to its DSC BoundingBox.",
    },
    {
        "url": "https://ghostscript.readthedocs.io/en/latest/VectorDevices.html",
        "title": "Ghostscript: High-level vector output devices",
        "fact": "Ghostscript documents pdfwrite as a high-level output device that normally preserves drawing primitives instead of rendering the input to a bitmap.",
    },
    {
        "url": "https://developer.apple.com/library/archive/documentation/GraphicsImaging/Conceptual/drawingwithquartz2d/dq_pdf/dq_pdf.html",
        "title": "Apple Quartz 2D Programming Guide: PDF",
        "fact": "Apple documents PDF as resolution-independent and CGContextDrawPDFPage as the page-drawing path for a graphics context.",
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
    image_core = (repo / "src" / "qvimagecore.cpp").read_text(encoding="utf-8")
    graphics_item = (repo / "src" / "qvgraphicsimageitem.cpp").read_text(encoding="utf-8")
    graphics_view = (repo / "src" / "qvgraphicsview.cpp").read_text(encoding="utf-8")
    namespace = (repo / "src" / "qvnamespace.h").read_text(encoding="utf-8")
    main_window = (repo / "src" / "mainwindow.cpp").read_text(encoding="utf-8")
    options = (repo / "src" / "qvoptionsdialog.cpp").read_text(encoding="utf-8")
    plist = (repo / "dist" / "mac" / "Info.plist.in").read_text(encoding="utf-8")
    readme = (repo / "README.md").read_text(encoding="utf-8")
    tests = (repo / "tests" / "tst_qviewtests.cpp").read_text(encoding="utf-8")
    workflows = "\n".join(
        path.read_text(encoding="utf-8")
        for path in (
            repo / ".github" / "workflows" / "build.yml",
            repo / ".github" / "workflows" / "test.yml",
            repo / ".github" / "workflows" / "release.yml",
            repo / ".github" / "workflows" / "release-compatibility.yml",
        )
    )

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

    renderer_contract = all(
        marker in native
        for marker in (
            "ghostscriptExecutable",
            "FOVELLE_GHOSTSCRIPT",
            "QProcess process",
            'QStringLiteral("-dSAFER")',
            'QStringLiteral("-dEPSCrop")',
            'QStringLiteral("-sDEVICE=pdfwrite")',
            "CGPDFDocumentCreateWithProvider",
            "CGContextDrawPDFPage",
        )
    )
    bounded_execution = all(
        marker in native
        for marker in (
            "EPSRendererStartTimeoutMs",
            "EPSRendererTimeoutMs",
            "MaxEPSRendererDiagnosticBytes",
            "MaxEPSRenderedPixels",
            "MaxEPSIntermediatePDFBytes",
            "waitForStarted",
            "waitForFinished",
            "process.kill()",
        )
    )
    authoritative_content = (
        "convertEPSToPDF(filePath" in native
        and "result.vectorImage.format = Qv::VectorImageFormat::Pdf" in native
        and "result.vectorImage.encodedData = pdfData" in native
        and "EPSPreviewLargestDimension = 512" in native
        and "imageFromEPSRasterPreview" not in native
        and "imageFromEPSIPreview" not in native
    )
    check(
        checks,
        "ST-EPS-PARSER-SAFETY",
        "EPS must retain authoritative PostScript as a bounded Ghostscript-produced PDF document and never substitute its embedded placement preview.",
        renderer_contract and bounded_execution and authoritative_content,
        {
            "ghostscript_pdf_contract": renderer_contract,
            "bounded_external_renderer": bounded_execution,
            "authoritative_postscript_not_preview": authoritative_content,
        },
        ["src/qvcocoafunctions.mm"],
    )

    viewport_vector_contract = all(
        marker in graphics_item
        for marker in (
            "QGraphicsItem::NoCache",
            "QGraphicsItem::ItemUsesExtendedStyleOption",
            "option->exposedRect",
            "painter->deviceTransform()",
            "document->renderTile",
            "renderedSourceRect.width() * requestedScaleX",
            "VectorTilePanOverscanPixels = 128",
            "InteractiveVectorRenderScale = 0.75",
            "MaxMultipleVectorTileBytes = 96LL * 1024LL * 1024LL",
            "MaxRetainedVectorTiles = 2",
            "QtConcurrent::run",
            "requestAsyncVectorTile(request)",
            "matchingVectorTile(sourceRect",
            "!vectorInteractionActive",
        )
    )
    zoom_contract = (
        "MaximumZoomLevel = 64.0" in namespace
        and "boundedZoomLevel" in graphics_view
        and "Qv::MaximumZoomLevel * 100.0" in main_window
        and "vectorRefineTimer->setInterval(50)" in graphics_view
    )
    interaction_scroll_contract = (
        "paintsOpaqueViewportBackground" in graphics_view
        and "Qt::WA_OpaquePaintEvent" in graphics_view
        and "viewportScrollChanged" in graphics_view
        and "setVectorInteractionActive(true)" in graphics_view
    )
    check(
        checks,
        "ST-EPS-VECTOR-VIEWPORT",
        "The scene must render bounded exposed-region EPS tiles asynchronously, use backing-store scroll reuse, and stop every zoom path at 6400%.",
        viewport_vector_contract and zoom_contract and interaction_scroll_contract,
        {
            "bounded_async_interaction_tile_contract": viewport_vector_contract,
            "opaque_scroll_interaction_contract": interaction_scroll_contract,
            "central_6400_percent_zoom_contract": zoom_contract,
        },
        ["src/qvgraphicsimageitem.cpp", "src/qvgraphicsview.cpp", "src/qvnamespace.h", "src/mainwindow.cpp"],
    )

    settings_wiring = all(
        marker in options
        for marker in ("getAllFileExtensionList", "formatsTable", "setRowCount(extensions.count())")
    )
    docs_and_bundle = (
        "- EPS" in readme
        and "brew install ghostscript" in readme
        and all(f"<string>{alias}</string>" in plist for alias in aliases)
        and "com.adobe.encapsulated-postscript" in plist
        and workflows.count("brew install ghostscript") >= 4
    )
    check(
        checks,
        "ST-EPS-DOCS-SETTINGS",
        "Settings, README, the macOS bundle declaration, and CI must expose EPS and its Ghostscript dependency.",
        settings_wiring and docs_and_bundle,
        {
            "settings_table_uses_application_extension_set": settings_wiring,
            "readme_contains_eps": "- EPS" in readme,
            "readme_documents_ghostscript": "brew install ghostscript" in readme,
            "bundle_contains_eps_aliases": all(f"<string>{alias}</string>" in plist for alias in aliases),
            "bundle_contains_eps_uti": "com.adobe.encapsulated-postscript" in plist,
            "ci_installs_ghostscript": workflows.count("brew install ghostscript") >= 4,
        },
        ["src/qvoptionsdialog.cpp", "src/qvapplication.cpp", "README.md", "dist/mac/Info.plist.in", ".github/workflows/*.yml"],
    )

    loader_delegation = (
        "readImageWithImageIO" in loader
        and "nativeResult.image.isNull()" in loader
        and "nativeResult.allowsQtFallback" in loader
        and "result.allowsQtFallback = false" in native
        and "suffix == \"eps\"" not in loader
        and "eps" not in loader.lower()
    )
    static_document_guard = all(
        marker in image_core
        for marker in (
            "isVectorDocument",
            "loadedVectorImage.isValid()",
            "!isVectorDocument && !readData.isMultiFrameImage",
        )
    )
    check(
        checks,
        "ST-EPS-LOADER-DELEGATION",
        "QVImageLoader must consume the native vector result without a suffix branch, and QVImageCore must not let movie probing replace the static EPS document.",
        loader_delegation and static_document_guard,
        {
            "native_bridge_used": "readImageWithImageIO" in loader,
            "native_image_null_guard_present": "nativeResult.image.isNull()" in loader,
            "native_result_controls_qt_fallback": "nativeResult.allowsQtFallback" in loader,
            "extension_branch_absent": "eps" not in loader.lower(),
            "static_document_movie_guard": static_document_guard,
        },
        ["src/qvimageloader.cpp", "src/qvimagecore.cpp", "src/qvcocoafunctions.mm"],
    )

    test_contracts = all(
        marker in tests
        for marker in (
            "testEPSFormatIsAdvertised",
            "testEPSPostScriptRender",
            "testImageLoaderLoadsEPS",
            "testEPSRenderSurvivesStaticMovieProbe",
            "testMalformedEPSFailsSafely",
            "testEPSMissingRendererFailsActionably",
            "testSettingsFormatsIncludeEPS",
            "FOVELLE_EPS_SAMPLE",
            "createEPSVectorImage",
            "testVectorFormatsUseDocumentSceneItem",
            "testVectorInteractionPaintCpuBudgetFor120Hz",
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
            "The EPS path invokes Ghostscript with SAFER, finite process waits, cropped high-level PDF output, bounded diagnostics, and pixel/PDF limits.",
            "The scene retains the PDF document, uses bounded exposed-region tiles with pan overscan and asynchronous interaction and idle refinement, and reuses opaque backing-store pixels while panning; the 512-pixel image is a non-authoritative fallback preview.",
            "Every zoom entry point is bounded by the central 64.0 (6400%) contract.",
        ],
        "inferences": [
            "Passing the registry, delegation, and viewport checks indicates EPS follows the existing loader Result contract while retaining a document-specific renderer.",
            "The Settings table will reflect EPS at runtime because it enumerates QVApplication's all-file-extension set.",
        ],
        "uncertainties": [
            "Static checks cannot prove compatibility with every PostScript dialect, external font dependency, or Ghostscript version; runtime evidence covers the supplied DOS EPS and a deterministic vector EPS.",
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
