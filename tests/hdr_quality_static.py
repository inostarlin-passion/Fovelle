#!/usr/bin/env python3
"""Static/build contract tests for the native RAW/HDR pipeline."""

from __future__ import annotations

import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def between(text: str, start: str, end: str) -> str:
    start_index = text.find(start)
    end_index = text.find(end, start_index + len(start))
    if start_index < 0 or end_index < 0:
        return ""
    return text[start_index:end_index]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    repo = args.repo.resolve()
    build_dir = args.build_dir.resolve()
    cocoa = (repo / "src/qvcocoafunctions.mm").read_text(encoding="utf-8")
    cocoa_header = (repo / "src/qvcocoafunctions.h").read_text(encoding="utf-8")
    view = (repo / "src/qvgraphicsview.cpp").read_text(encoding="utf-8")
    namespace_source = (repo / "src/qvnamespace.h").read_text(encoding="utf-8")
    main_window = (repo / "src/mainwindow.cpp").read_text(encoding="utf-8")
    application = (repo / "src/qvapplication.cpp").read_text(encoding="utf-8")
    loader = (repo / "src/qvimageloader.cpp").read_text(encoding="utf-8")
    cmake = (repo / "CMakeLists.txt").read_text(encoding="utf-8")
    qmake = (repo / "qView.pro").read_text(encoding="utf-8")
    plist = (repo / "dist/mac/Info.plist").read_text(encoding="utf-8")
    tests = (repo / "tests/tst_qviewtests.cpp").read_text(encoding="utf-8")
    workflow_paths = (
        repo / ".github/workflows/test.yml",
        repo / ".github/workflows/build.yml",
        repo / ".github/workflows/release.yml",
        repo / ".github/workflows/release-compatibility.yml",
    )
    workflows = {str(path.relative_to(repo)): path.read_text(encoding="utf-8") for path in workflow_paths}

    cases: list[dict] = []

    def case(identifier: str, observations: dict[str, bool | str | int | float]) -> None:
        boolean_values = [value for value in observations.values() if isinstance(value, bool)]
        passed = bool(boolean_values) and all(boolean_values)
        cases.append({
            "id": identifier,
            "test_code": "tests/hdr_quality_static.py",
            "observations": observations,
            "status": "passed" if passed else "failed",
        })

    raw_decode = between(cocoa, "if (result.isRaw)", "else if (result.isImageIOType)")
    nonraw_decode = between(cocoa, "else if (result.isImageIOType)", "CFRelease(source);")
    renderer = between(cocoa, "struct QVCocoaFunctions::HDRRenderer::Impl", "static void hideMenuShortcuts")
    build_workflow = workflows[".github/workflows/build.yml"]
    test_workflow = workflows[".github/workflows/test.yml"]
    release_workflow = workflows[".github/workflows/release.yml"]
    compatibility_workflow = workflows[".github/workflows/release-compatibility.yml"]
    release_script = (repo / "dist/scripts/package-macos-release.sh").read_text(encoding="utf-8")

    case("ST-CI-APPLE-SDK", {
        "build_and_test_use_macos_26": "runs-on: macos-26" in build_workflow and "runs-on: macos-26" in test_workflow,
        "release_uses_macos_15": "runs-on: macos-15" in release_workflow,
        "release_compatibility_uses_macos_15": "runs-on: macos-15" in compatibility_workflow,
        "legacy_macos_14_runner_absent": all("runs-on: macos-14" not in workflow for workflow in workflows.values()),
        "xcode_version_is_verified": all("xcodebuild -version" in workflow for workflow in workflows.values()),
        "sdk_version_is_verified": all("xcrun --sdk macosx --show-sdk-version" in workflow for workflow in workflows.values()),
        "build_and_test_xcode_26_minimum": 'test "${XCODE_VERSION%%.*}" -ge 26' in build_workflow and 'test "${XCODE_VERSION%%.*}" -ge 26' in test_workflow,
        "build_and_test_sdk_26_minimum": 'test "${SDK_VERSION%%.*}" -ge 26' in build_workflow and 'test "${SDK_VERSION%%.*}" -ge 26' in test_workflow,
        "release_xcode_16_minimum": 'test "${XCODE_VERSION%%.*}" -ge 16' in release_workflow,
        "release_sdk_15_exact": 'test "${SDK_VERSION%%.*}" -eq 15' in release_workflow,
        "release_compatibility_sdk_15_exact": 'test "${SDK_VERSION%%.*}" -eq 15' in compatibility_workflow,
        "qt_6_11_2_is_pinned": all("version: '6.11.2'" in workflow for workflow in workflows.values()),
        "legacy_qt_6_8_is_absent": all("version: '6.8" not in workflow for workflow in workflows.values()),
        "deployment_target_is_explicit_everywhere": all("-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0" in workflow for workflow in workflows.values()),
        "deployment_target_is_defaulted_in_cmake": 'set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0"' in cmake,
        "sdk15_only_release_sysroot_is_explicit": 'CMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)"' in release_workflow,
        "sdk15_compatibility_job_has_release_sysroot": 'CMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)"' in compatibility_workflow,
        "release_artifact_checks_sdk_and_min_os": all(token in release_script for token in (
            "assert_macos_deployment_target", "otool -l", "LSMinimumSystemVersion",
            "EXPECTED_MACOS_DEPLOYMENT_TARGET",
        )),
        "sdk15_compile_guards_cover_new_apis": all(token in cocoa for token in (
            "#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 260000",
            "#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 160000",
        )),
    })

    case("ST-CI-BUILD-PARALLELISM", {
        "checks_build_parallelism_is_bounded": (
            "run: cmake --build build --parallel 2" in workflows[".github/workflows/test.yml"]
        ),
        "checks_unbounded_parallelism_is_absent": (
            "run: cmake --build build --parallel\n" not in workflows[".github/workflows/test.yml"]
        ),
        "build_workflow_uses_same_bound": (
            "run: cmake --build build --parallel 2" in workflows[".github/workflows/build.yml"]
        ),
        "release_workflow_uses_same_bound": (
            "run: cmake --build build --parallel 2" in workflows[".github/workflows/release.yml"]
        ),
        "release_compatibility_workflow_uses_same_bound": (
            "run: cmake --build build --parallel 2" in workflows[".github/workflows/release-compatibility.yml"]
        ),
    })

    case("ST-HDR-RAW-CONTENT-UTI", {
        "camera_raw_conformance_check": "UTTypeRAWImage" in cocoa and "conformsToType" in cocoa,
        "classification_uses_source_type": "result.isRaw = isRawImageType(sourceType)" in cocoa,
        "filename_suffix_not_used_in_decoder": "QFileInfo(filePath).suffix" not in raw_decode,
    })
    case("ST-HDR-RAW-CIRAWFILTER", {
        "independent_native_raw_filters": raw_decode.count("filterWithImageURL:fileUrl.toNSURL()") >= 2,
        "sdr_raw_graph": "extendedDynamicRangeAmount = 0.0F" in raw_decode,
        "edr_raw_graph": "extendedDynamicRangeAmount = 1.0F" in raw_decode,
        "camera_baseline_exposure_is_not_mutated": ".baselineExposure =" not in raw_decode,
        "native_graph_published": "make_shared<NativeHDRImage>" in raw_decode,
    })
    case("ST-HDR-RAW-PREVIEW-FALLBACK", {
        "preview_is_conditional_fallback": (
            raw_decode.find("if (result.image.isNull())") < raw_decode.find("CIImage *rawPreview")
        ),
        "processed_preview_requires_authored_gain_map": (
            "const bool hasGainMap" in raw_decode
            and "usesProcessedRawPreview = true" in raw_decode
            and "camera-raw-processed-gain-map" in raw_decode
        ),
        "preview_usage_is_observable": "result.usedRawPreview" in raw_decode,
        "unsupported_camera_is_explicit": "does not support this camera model" in raw_decode,
    })
    case("ST-HDR-NONRAW-METADATA", {
        "apple_gain_map": "kCGImageAuxiliaryDataTypeHDRGainMap" in cocoa,
        "iso_gain_map": "kCGImageAuxiliaryDataTypeISOGainMap" in cocoa,
        "pq_detection": "CGColorSpaceIsPQBased" in cocoa,
        "hlg_detection": "CGColorSpaceIsHLGBased" in cocoa,
        "headroom_read": "CGImageGetContentHeadroom" in cocoa and ".contentHeadroom" in cocoa,
    })
    case("ST-HDR-NONRAW-RECONSTRUCTION", {
        "imageio_hdr_request": "kCGImageSourceDecodeToHDR" in cocoa,
        "core_image_expand": "kCIImageExpandToHDR" in nonraw_decode,
        "orientation_applied": "kCIImageApplyOrientationProperty" in nonraw_decode,
        "hdr_candidate_from_metadata": "const bool hdrCandidate = hasGainMap" in nonraw_decode,
    })
    case("ST-HDR-NONRAW-NONVOLATILE-DECODE", {
        "hdr_decode_is_cached_at_initialization": (
            nonraw_decode.count("kCIImageCacheImmediately") >= 2
            and nonraw_decode.count("(id)kCIImageCacheImmediately : @YES") >= 2
        ),
        "both_hdr_and_sdr_recipes_are_cached": (
            nonraw_decode.find("NSDictionary *hdrOptions")
            < nonraw_decode.find("kCIImageCacheImmediately")
            < nonraw_decode.find("NSDictionary *sdrOptions")
            < nonraw_decode.rfind("kCIImageCacheImmediately")
        ),
        "viewport_transform_occurs_after_source_intermediate": (
            renderer.find("preparedSDRImage = [[sdrSource imageByInsertingIntermediate:YES]")
            < renderer.find("source = imageForTexture(source, viewportSize, corners, actualSize)")
        ),
    })
    case("ST-HDR-FLOAT-INTERMEDIATE", {
        "half_float_context": cocoa.count("kCIFormatRGBAh") >= 2,
        "retained_ci_graph": "CIImage *hdr" in cocoa and "[hdrImage retain]" in cocoa,
        "hdr_handle_crosses_loader": "nativeResult.hdrImage" in loader,
        "rgba8_limited_to_named_fallback_helper": "QImage imageFromCIImage" in cocoa and "kCIFormatRGBA8" in cocoa,
    })
    case("ST-HDR-COLORSYNC", {
        "colorsync_profile": "ColorSyncProfileCreateWithName(kColorSyncDisplayP3Profile)" in cocoa,
        "extended_linear_p3": "CGColorSpaceCreateExtendedLinearized" in cocoa,
        "ci_working_space": "kCIContextWorkingColorSpace" in renderer,
        "ci_output_space": "kCIContextOutputColorSpace" in renderer,
    })
    case("ST-HDR-METAL-EDR-SURFACE", {
        "metal_ci_context": "contextWithMTLDevice" in renderer,
        "rgba16_float_layer": "MTLPixelFormatRGBA16Float" in renderer,
        "edr_requested": "wantsExtendedDynamicRangeContent = YES" in renderer,
        "entire_drawable_is_opaque": (
            "metalLayer.opaque = YES" in renderer and "alpha:1" in renderer
        ),
        "quartzcore_linked": "-framework QuartzCore" in cmake and "-framework QuartzCore" in qmake,
    })
    case("ST-HDR-DISPLAY-ADAPTATION", {
        "current_headroom_per_render": "maximumExtendedDynamicRangeColorComponentValue" in renderer,
        "potential_headroom_per_render": "maximumPotentialExtendedDynamicRangeColorComponentValue" in renderer,
        "window_screen_selected": "nativeView.window.screen" in renderer,
        "full_display_override": "FOVELLE_TEST_DISPLAY_HEADROOM" in renderer,
        "current_only_override": "FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM" in renderer,
    })
    case("ST-HDR-EDR-BOOTSTRAP", {
        "pure_rendering_policy_declared": "displayHeadroomForRendering" in cocoa_header,
        "policy_uses_current_potential_and_content": all(
            token in cocoa for token in ("safeCurrent", "safePotential", "safeContent")
        ),
        "rendering_headroom_drives_target": "state.displayRenderingHeadroom, linearProgress" in renderer,
        "bootstrap_is_observable": "state.bootstrappingEDR" in renderer,
    })
    case("ST-HDR-TONE-MAPPING", {
        "system_tone_map_filter": "CIToneMapHeadroom" in renderer,
        "source_headroom_parameter": "inputSourceHeadroom" in renderer,
        "target_headroom_parameter": "inputTargetHeadroom" in renderer,
        "layer_automatic_tone_map": "CAToneMapModeAutomatic" in renderer,
    })
    case("ST-HDR-DNG-PROCESSED-GAINMAP", {
        "full_resolution_camera_processed_preview": "sdrRawFilter.previewImage" in raw_decode,
        "auxiliary_gain_map_loaded": "kCIImageAuxiliaryHDRGainMap" in raw_decode,
        "full_hdr_endpoint_reconstructed": "imageByApplyingGainMap:gainMap" in raw_decode,
        "gain_map_retained_with_native_graph": (
            "processedHDR, processedSDR, result.hdrMetadata, gainMap" in raw_decode
        ),
        "display_headroom_reconstructed_directly": (
            "imageByApplyingGainMap:nativeImage.gainMapCIImage()" in renderer
            and "headroom:std::max(1.0F, targetHeadroom)" in renderer
        ),
        "baseline_exposure_preserved": ".baselineExposure =" not in raw_decode,
    })
    case("ST-HDR-DNG-GAINMAP-ROI", {
        "processed_gain_map_avoids_post_apply_intermediate": (
            "if (image->metadata().usesProcessedRawPreview)" in renderer
            and "preparedHDRImage = [hdrSource retain]" in renderer
        ),
        "other_raw_sources_keep_managed_intermediates": (
            "preparedHDRImage = [[hdrSource imageByInsertingIntermediate:YES] retain]" in renderer
        ),
        "source_roi_is_transformed_only_for_current_drawable": (
            "imageForTexture(\n                finalDisplayImage, viewportSize" in renderer
            and "source = imageForTexture(source, viewportSize, corners, actualSize)" in renderer
        ),
    })
    case("ST-HDR-GEOMETRY", {
        "four_source_corners": "const QPolygonF sourceCorners" in view,
        "qt_viewport_transform": "viewportTransform().map(sourceCorners)" in view,
        "metal_affine_transform": "CGAffineTransformMake(a, b, c, d, tx, ty)" in renderer,
        "source_size_scene_rect": "QRectF(QPointF(), getCurrentFileDetails().loadedPixmapSize)" in view,
        "actual_drawable_texture_drives_coordinates": "textureSize.width / viewportSize.width()" in renderer,
        "drawable_size_observed": "drawable.texture.width" in renderer and "drawableGeometryMatches" in renderer,
    })
    case("ST-HDR-STAGED-FIRST-FRAME", {
        "layout_gate_closed_before_fit": view.count("hdrLayoutReady = false") >= 2,
        "layout_gate_armed_only_after_geometry_stabilizes": (
            "finishHDRGeometryStabilization" in view and "hdrLayoutReady = true" in view
        ),
        "sdr_proxy_kept_visible": "loadedPixmapItem->setVisible(true)" in view,
        "metal_starts_transparent": "metalLayer.opacity = 0.0F" in renderer,
        "reveal_after_presented_handler": "addPresentedHandler" in renderer and "firstFramePresented = YES" in renderer,
    })
    case("ST-HDR-OFFSCREEN-PREPARATION", {
        "preparation_precedes_first_visible_frame": (
            "needsManagedPreparation && !preparedEndpointsAvailable" in renderer
            and "scheduleHDRPreparation(viewportSize, corners, requestedSize)" in renderer
            and renderer.index("needsManagedPreparation && !preparedEndpointsAvailable")
            < renderer.index("revealAfterPresentation(drawable, commandBuffer, finalHeadroom)")
        ),
        "core_image_manages_both_intermediates": renderer.count("imageByInsertingIntermediate:YES") == 2,
        "float_scratch_texture_warms_intermediates": (
            "MTLPixelFormatRGBA16Float" in renderer
            and "id<MTLTexture> targetTexture = [preparationTexture retain]" in renderer
            and "toMTLTexture:targetTexture" in renderer
        ),
        "app_texture_is_never_reimported_as_ci_input": "imageWithMTLTexture" not in renderer,
        "final_endpoint_is_prepared_on_serial_renderer_queue": (
            "CIImage *finalDisplayImage = preparedDisplayImage(fullTargetHeadroom, 1.0F)" in renderer
            and "dispatch_async(renderQueue" in renderer
            and "[renderContext render:openingFrame" in renderer
        ),
        "partial_headroom_ramp_is_not_prewarmed_or_submitted": (
            "warmProgresses" not in renderer
            and "hdrRenderer->render(viewportSize, viewportCorners, 1.0, interactive)" in view
        ),
        "representative_zoom_roi_is_prewarmed": (
            "interactionWarmCorners" in renderer
            and "(corner - viewportCenter) * 4.0" in renderer
            and "retainedWarmFrames" in renderer
            and "warmOffsets" in renderer
        ),
        "prepared_endpoints_used_for_visible_frame": "preparedDisplayImage" in renderer,
    })
    case("ST-HDR-FIRST-VISIBLE-FINAL", {
        "production_submits_only_final_headroom": (
            "hdrRenderer->render(viewportSize, viewportCorners, 1.0, interactive)" in view
        ),
        "reveal_policy_rejects_partial_endpoint": (
            "isFinalHDRFrameReadyForReveal" in cocoa_header
            and "transitionProgress >= 0.999" in cocoa
            and "isFinalHDRFrameReadyForReveal(" in renderer
        ),
        "proxy_removed_only_after_final_frame_presentation": (
            "rendererState.firstFramePresented" in view
            and "rendererState.firstVisibleFrameUsesFinalHeadroom" in view
            and "loadedPixmapItem->setVisible(false)" in view
        ),
        "manual_opening_ramp_removed": all(
            token not in view for token in (
                "hdrTransitionTimer", "hdrTransitionClock", "hdrTransitionLinearProgress",
                "650.0", "1800",
            )
        ),
        "windowserver_receives_one_final_edr_endpoint": (
            "firstVisibleFrameUsesFinalHeadroom.store(finalHeadroom)" in renderer
            and "addPresentedHandler" in renderer
        ),
        "final_endpoint_uses_compositor_fade": (
            "presentationContainerLayer" in renderer
            and 'animationWithKeyPath:@"opacity"' in renderer
            and "fullTransitionDuration = 0.45" in renderer
        ),
        "previous_hdr_surface_can_cover_navigation": (
            "retainPreviousPresentation" in renderer
            and "previousMetalPresentationVisible" in renderer
            and "previousPersistentPresentationVisible" in renderer
            and "nativeImage && retainPreviousPresentation ? 1.0F : 0.0F" in renderer
        ),
    })
    focus_event_handler = between(
        main_window, "bool MainWindow::event(QEvent *event)",
        "bool MainWindow::eventFilter(QObject *watched, QEvent *event)"
    )
    focus_transition = between(
        renderer, "float currentPresentationOpacity() const",
        "CGColorRef navigationColor"
    )
    case("ST-HDR-FOCUS-PRESENTATION-TRANSITION", {
        "window_activation_is_propagated": (
            "QEvent::WindowActivate" in focus_event_handler
            and "QEvent::WindowDeactivate" in focus_event_handler
            and "setHDRPresentationActive(true)" in focus_event_handler
            and "setHDRPresentationActive(false)" in focus_event_handler
            and "applicationStateChanged" in view
        ),
        "sdr_proxy_is_committed_before_fade_out": (
            "loadedPixmapItem->setVisible(true)" in view
            and "QTimer::singleShot(active ? 0 : 16" in view
        ),
        "reversal_starts_at_onscreen_opacity": (
            "presentationContainerLayer.presentationLayer" in focus_transition
            and "const float startOpacity = currentPresentationOpacity()" in focus_transition
        ),
        "opacity_transition_is_explicit_and_bounded": (
            'animationWithKeyPath:@"opacity"' in focus_transition
            and "fullTransitionDuration = 0.45" in focus_transition
            and "fullTransitionDuration * distance" in focus_transition
        ),
        "edr_is_disabled_only_after_fade_out": (
            "if (!presentationActiveRequested)\n            setExtendedDynamicRangeEnabled(false);"
            in focus_transition
            and "if (presentationActiveRequested)\n            setExtendedDynamicRangeEnabled(true);"
            in focus_transition
        ),
    })
    case("ST-HDR-GEOMETRY-LIFECYCLE", {
        "complete_geometry_comparator": (
            "hdrViewportGeometryEquivalent" in view and "lhsViewportSize != rhsViewportSize" in view
            and "lhsImageCorners.size() != rhsImageCorners.size()" in view
        ),
        "geometry_debounce_is_single_shot": (
            "hdrGeometryTimer->setSingleShot(true)" in view and "hdrGeometryTimer->setInterval(34)" in view
        ),
        "unpresented_generation_can_be_invalidated": (
            "invalidateUnpresentedGeometry" in view and "hdrRenderer->invalidateGeometry()" in view
        ),
        "presented_hdr_is_reused_without_sdr_fallback": (
            "reuseVisibleHDR" in view
            and "loadedPixmapItem->setVisible(!reuseVisibleHDR || !presentationFullyVisible)" in view
            and "rendererState.presentationActiveRequested" in view
            and "rendererState.presentationAnimationInFlight" in view
            and "if (!hdrActivationCompleted)" in view
        ),
        "cached_endpoints_are_geometry_independent": (
            "preparedEndpointsAvailable" in renderer
            and "preparedViewportSize == viewportSize" not in renderer
            and "preparedCorners == corners" not in renderer
        ),
        "layer_tracks_native_view_resize": (
            "syncViewportLayerGeometry" in renderer
            and "viewport->mapTo(hostWidget, QPoint(0, 0))" in renderer
            and "presentationContainerLayer.frame = viewportFrame" in renderer
        ),
        "hdr_host_does_not_promote_sdr_viewport": (
            "viewportWidget->window()->winId()" in renderer
            and "viewportWidget->winId()" not in renderer
        ),
        "hdr_host_clips_to_mapped_viewport": (
            "presentationContainerLayer.masksToBounds = YES" in renderer
            and "presentationContainerLayer.frame = viewportFrame" in renderer
        ),
        "drawable_background_prevents_reused_tile_ghosts": (
            "imageByCompositingOverImage:clearImage" in renderer and "alpha:1" in renderer
        ),
    })
    render_entry = between(
        renderer, "void render(const QSize &viewportSize", "void renderDisplayLinkUpdate"
    )
    display_link_entry = between(
        renderer, "void renderDisplayLinkUpdate", "bool renderToDrawable"
    )
    render_to_drawable = between(
        renderer, "bool renderToDrawable", "HDRRendererDiagnostics diagnostics"
    )
    case("ST-HDR-DISPLAYLINK-LATEST-ONLY", {
        "display_link_is_the_only_drawable_source": (
            "CAMetalDisplayLink" in renderer
            and "renderDisplayLinkUpdate(CAMetalDisplayLinkUpdate *update)" in renderer
            and "update.drawable, viewportSize" in renderer
            and "nextDrawable" not in renderer
        ),
        "display_link_stays_attached_and_is_unpaused_by_requests": (
            "if (displayLink && displayLink.paused)" in render_entry
            and "displayLink.paused = NO" in render_entry
            and "detachDisplayLink" not in renderer
            and "invalidate]" not in render_entry
            and "const bool keepAlive" in display_link_entry
            and "if (!renderPending && !keepAlive)" in display_link_entry
            and "displayLink.paused = YES" in display_link_entry
            and "interactiveKeepAliveUntil" in display_link_entry
            and "pendingRequestTimestamp = CACurrentMediaTime()" in display_link_entry
        ),
        "drawable_resize_precedes_display_link_callback": (
            "metalLayer.drawableSize = requestedSize" in render_entry
            and "syncViewportLayerGeometry();" in render_entry
            and "metalLayer.frame = presentationContainerLayer.bounds" in render_entry
            and "metalLayer.drawableSize = requestedSize" not in render_to_drawable
            and "rebuildDisplayLinkForDrawableResize" in renderer
            and "[previous invalidate]" in renderer
        ),
        "at_most_two_frames_overlap": (
            "framesInFlight.load() >= 2" in display_link_entry
            and "framesInFlight.fetch_add(1)" in render_to_drawable
            and "framesInFlight.fetch_sub(1)" in render_to_drawable
            and "presentationState->frameInFlight" not in renderer
        ),
        "new_requests_overwrite_pending_state": (
            "pendingViewportSize = viewportSize" in renderer
            and "pendingCorners = corners" in renderer
            and "pendingRenderGeneration = ++state.requestedRenderGeneration" in renderer
            and "pendingRequestTimestamp = CACurrentMediaTime()" in renderer
        ),
        "interactive_submissions_are_counted_at_display_cadence": (
            "if (interactive)" in display_link_entry
            and "++state.displayLinkInteractiveSubmissionCount" in display_link_entry
            and "preferredFrameRateRange = CAFrameRateRangeMake(80.0, 120.0, 120.0)" in renderer
        ),
        "display_deadline_is_observed_without_timed_present": (
            "update.targetTimestamp" in display_link_entry
            and "presentDrawable:encodedDrawable" in render_to_drawable
            and "atTime:targetTimestamp" not in render_to_drawable
            and "presentationCallTime > targetTimestamp" in render_to_drawable
        ),
        "presentation_is_command_buffer_ordered": (
            render_to_drawable.index("presentDrawable:encodedDrawable") >= 0
            and render_to_drawable.index("presentDrawable:encodedDrawable")
            < render_to_drawable.index("[encodedCommandBuffer commit]")
        ),
        "core_image_encoding_uses_dedicated_serial_queue": (
            "com.fovelle.hdr-render-encode" in renderer
            and "dispatch_sync(renderQueue" in render_to_drawable
            and "[renderContext render:encodedSource" in render_to_drawable
        ),
    })
    case("ST-HDR-PERSISTENT-COMPOSITOR-FAST-PATH", {
        "full_hdr_surface_is_materialized_off_main_thread": (
            "com.fovelle.hdr-persistent-surface" in renderer
            and "createCGImage:source" in renderer
            and "format:kCIFormatRGBAh" in renderer
            and "deferred:NO" in renderer
        ),
        "persistent_surface_allocation_is_bounded": (
            "maximumPersistentBytes = 512ULL * 1024ULL * 1024ULL" in renderer
            and "width > 16384" in renderer
            and "height > 16384" in renderer
        ),
        "interaction_changes_only_compositor_geometry": (
            "if (persistentSurfaceReady)" in render_entry
            and "updatePersistentSurfaceGeometry(viewportSize, corners)" in render_entry
            and "persistentImageLayer.affineTransform = transform" in renderer
            and "displayLink.paused = YES" in render_entry
        ),
        "metal_fallback_is_hidden_atomically": (
            "persistentImageLayer.contents = reinterpret_cast<id>(surface)" in renderer
            and "metalLayer.hidden = YES" in renderer
            and "[CATransaction setDisableActions:YES]" in renderer
        ),
        "hidden_qt_backing_store_is_parked": (
            "setViewportUpdateMode(QGraphicsView::NoViewportUpdate)" in view
            and "setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate)" in view
        ),
        "focused_120hz_probe_is_observable": (
            "FOVELLE_HDR_TEST_120HZ_INTERACTION" in view
            and '"FOVELLE_HDR_120HZ"' in view
            and 'QStringLiteral("compositor_updates")' in view
            and 'QStringLiteral("metal_presents")' in view
        ),
    })
    case("ST-HDR-PRESENTATION-TELEMETRY", {
        "presented_handler_is_source_of_timing": (
            "addPresentedHandler" in renderer
            and "presentedDrawable.presentedTime" in renderer
            and "CACurrentMediaTime()" in renderer
        ),
        "request_to_presentation_is_measured": (
            "requestToPresentationMilliseconds" in renderer
            and '\\"request_to_present_ms\\"' in renderer
        ),
        "presentation_interval_and_count_are_atomic": (
            "presentedFrameCount" in renderer
            and "lastPresentedIntervalMilliseconds" in renderer
            and "std::atomic" in cocoa
        ),
        "final_endpoint_flag_is_logged_per_presented_frame": (
            '\\"final_headroom\\"' in renderer and '\\"interactive\\"' in renderer
        ),
    })
    case("ST-HDR-UI-REQUEST-COALESCING", {
        "zero_delay_request_timer_is_single_shot": (
            "hdrFrameRequestTimer->setSingleShot(true)" in view
            and "hdrFrameRequestTimer->setInterval(0)" in view
        ),
        "paint_and_scroll_only_publish_requests": (
            "void QVGraphicsView::paintEvent" in view
            and view.count("requestHDRRendererUpdate();") >= 6
        ),
        "one_actual_renderer_call_site": view.count("hdrRenderer->render(") == 1,
        "interaction_latency_is_observable": (
            "interaction_elapsed_ms" in view and "interaction_zoom_ms" in view
        ),
    })
    navigation_button_paint = between(
        main_window, "class ImageNavigationButton", "int MainWindow::navigationEdgeWidth"
    )
    navigation_initialization = between(
        main_window, "void MainWindow::initializeNavigationButtons()",
        "void MainWindow::updateNavigationButtonGeometry()"
    )
    navigation_overlay_sync = between(
        main_window, "void MainWindow::syncNavigationButtonOverlay(",
        "void MainWindow::syncNavigationButtonOverlays()"
    )
    case("ST-HDR-NAV-NATIVE-COMPOSITOR", {
        "sdr_fallback_widget_backing_is_transparent": (
            "WA_TranslucentBackground" in navigation_button_paint
            and "WA_NoSystemBackground" in navigation_button_paint
            and "setAutoFillBackground(false)" in navigation_button_paint
        ),
        "fade_is_applied_inside_paint": (
            'property("paintOpacity")' in navigation_button_paint
            and "painter.setOpacity(paintOpacity)" in navigation_button_paint
        ),
        "navigation_animation_targets_paint_property": (
            'QPropertyAnimation(button, "paintOpacity"' in navigation_initialization
        ),
        "native_fade_uses_live_animation_opacity": (
            'button->property("paintOpacity").toReal(),' in navigation_overlay_sync
            and 'requestedVisible ? button->property("paintOpacity")' not in navigation_overlay_sync
        ),
        "native_button_uses_group_opacity": (
            "navigationButtonLayers[index].allowsGroupOpacity = YES" in renderer
            and "buttonLayer.opacity = boundedOpacity" in renderer
            and "backgroundLayer.opacity = 1.0F" in renderer
            and "chevronLayer.opacity = 1.0F" in renderer
        ),
        "navigation_buttons_have_no_graphics_effect": (
            "QGraphicsOpacityEffect" not in navigation_initialization
        ),
        "hdr_artwork_is_shape_only_native_sublayer": (
            "navigationOverlayLayer" in renderer
            and renderer.count("[CAShapeLayer layer]") >= 2
            and "[nativeView.layer addSublayer:navigationOverlayLayer]" in renderer
            and "CGPathCreateWithRoundedRect" in renderer
        ),
        "qt_widget_is_hidden_for_hdr": (
            "usesNativeHDRNavigationOverlay" in main_window
            and "button->hide()" in main_window
            and "setHDRNavigationOverlay" in main_window
        ),
        "native_overlay_has_no_implicit_rectangular_animation": (
            "[CATransaction setDisableActions:YES]" in renderer
            and "navigationOverlayLayer.zPosition" in renderer
        ),
        "native_overlay_matches_qt_top_left_geometry": (
            "CGRectGetHeight(navigationOverlayLayer.bounds)" in renderer
            and "- viewportRect.y() - frameHeight" in renderer
            and "const CGRect frame = CGRectMake(viewportRect.x(), frameY" in renderer
        ),
        "surface_choice_is_observable": (
            'QStringLiteral("metal-sublayer")' in main_window
            and 'QStringLiteral("qt_widget_visible")' in main_window
        ),
    })
    case("ST-HDR-NAV-CACHED-SAMPLING", {
        "viewport_grab_removed": "viewport()->grab" not in main_window,
        "bounded_proxy_created_once_per_load": (
            "navigationSamplingImage = imageCore.getLoadedPixmap().toImage()" in view
            and "> 384" in view
        ),
        "hover_reads_cached_pixels": (
            "sampleDisplayedImageBrightness" in main_window
            and "navigationSamplingImage.pixelColor" in view
        ),
        "sampling_uses_device_independent_pixmap_geometry": (
            "loadedPixmapItem->mapFromScene" in view
            and "loadedPixmapItem->boundingRect" in view
        ),
    })
    case("ST-HDR-OBSERVABILITY", {
        "json_telemetry": '"FOVELLE_HDR"' in view and "QJsonDocument::Compact" in view,
        "decode_timing": "decodeTimer.nsecsElapsed()" in loader,
        "render_timing": "lastRenderMilliseconds" in renderer,
        "headroom_and_transition_fields": (
            "display_current_headroom" in view and "display_rendering_headroom" in view
            and "transition_progress" in view
        ),
        "geometry_generation_fields": "geometry_generation" in view and "geometry_reset_count" in view,
        "frame_scheduler_fields": all(
            token in view for token in (
                "uses_metal_display_link", "encodes_metal_off_main_thread",
                "render_request_count",
                "coalesced_render_request_count", "requested_render_generation",
                "submitted_render_generation", "presented_frame_count",
                "last_presented_interval_ms", "last_request_to_present_ms",
                "display_link_interaction_pacing",
                "display_link_interactive_submission_count",
                "frames_in_flight",
            )
        ),
        "viewport_crop_fields": all(
            token in view for token in (
                "viewport_global_x", "viewport_global_y", "viewport_logical_width",
                "viewport_logical_height", "viewport_device_pixel_ratio",
                "window_global_x", "window_global_y", "native_window_number",
            )
        ),
        "background_and_content_headroom_fields": all(
            token in view for token in (
                "viewport_background_red", "viewport_background_green",
                "viewport_background_blue", "layer_contents_headroom",
                "layer_contents_headroom_tag_supported", "image_corners",
            )
        ),
    })
    case("ST-HDR-RAW-STABLE-ENDPOINTS", {
        "separate_sdr_and_hdr_filters": (
            "CIRAWFilter *sdrRawFilter" in raw_decode
            and "CIRAWFilter *hdrRawFilter" in raw_decode
        ),
        "interactive_context_caches_intermediates": (
            "kCIContextCacheIntermediates : @YES" in renderer
            and "state.cachesIntermediates = true" in renderer
        ),
        "source_intermediates_are_explicitly_cacheable": (
            renderer.count("imageByInsertingIntermediate:YES") == 2
        ),
        "interactive_cache_is_cleared_on_image_replacement_only": (
            "bool setImage(const HDRImagePtr &newImage)" in renderer
            and "[context clearCaches]" in between(
                renderer, "bool setImage(const HDRImagePtr &newImage)", "void invalidateGeometry()"
            )
            and "[context clearCaches]" not in between(
                renderer, "void invalidateGeometry()", "static CIImage *mixImages"
            )
        ),
    })
    case("ST-HDR-RAW-CONTENT-HEADROOM", {
        "raw_float_peak_is_measured": "maximumCIImageRGBComponent(hdrImage" in raw_decode,
        "unknown_headroom_is_resolved_from_pixels": "resolvedHDRContentHeadroom" in raw_decode,
        "raw_ciimage_is_tagged_when_supported": "imageBySettingContentHeadroom" in raw_decode,
        "metal_layer_uses_content_target_not_potential_directly": (
            "metalLayer.contentsHeadroom = std::max<CGFloat>(1.0, state.targetHeadroom)" in renderer
        ),
        "older_runtime_fallback_is_not_misreported_as_layer_tag": (
            "state.usesLayerContentsHeadroomTag = true" in renderer
            and "state.usesLayerContentsHeadroomTag = false" in renderer
        ),
    })
    case("ST-HDR-THEME-BACKGROUND", {
        "single_shared_theme_contract": (
            "viewportBackgroundColor" in namespace_source
            and "Qv::viewportBackgroundColor(theme)" in main_window
            and "Qv::viewportBackgroundColor(theme)" in view
        ),
        "renderer_accepts_explicit_background": (
            "setBackgroundColor(const QColor &newColor)" in renderer
            and "backgroundColorSpace = colorSyncSrgbColorSpace()" in renderer
        ),
        "appkit_dynamic_background_removed": "NSColor.windowBackgroundColor" not in renderer,
        "background_is_composited_opaque": "imageByCompositingOverImage:clearImage" in renderer,
    })
    case("ST-HDR-INTERACTION-NO-REACTIVATION", {
        "reuse_policy_is_explicit": "canReuseHDRPresentation" in view,
        "geometry_stage_does_not_reset_progress": (
            "hdrTransitionClock.invalidate()" not in between(
                view, "void QVGraphicsView::stageHDRGeometry", "void QVGraphicsView::finishHDRGeometryStabilization"
            )
            and "hdrTransitionLinearProgress = 0.0" not in between(
                view, "void QVGraphicsView::stageHDRGeometry", "void QVGraphicsView::finishHDRGeometryStabilization"
            )
        ),
        "activation_completion_is_observable": (
            "hdrActivationCompleted = true" in view and "hdr_activation_completed" in view
        ),
    })
    menu_bridge = between(
        cocoa, "static void hideMenuShortcuts",
        "void QVCocoaFunctions::setUserDefaults()"
    )
    case("ST-CONTEXT-MENU-POINTER-OWNERSHIP", {
        "qt_context_trigger_runs_after_real_release": (
            "setContextMenuTrigger(Qt::ContextMenuTrigger::Release)" in application
            and "QGuiApplication::mouseButtons().testFlag(Qt::RightButton)" in menu_bridge
            and "popUpNativeContextMenuAfterRelease" in menu_bridge
        ),
        "native_menu_popup_requires_no_pointer_event": (
            "popUpMenuPositioningItem:nil" in menu_bridge
            and "atLocation:screenPoint" in menu_bridge
            and "inView:nil" in menu_bridge
        ),
        "synthetic_pointer_events_are_absent": (
            "mouseEventWithType" not in menu_bridge
            and "[view rightMouseDown:" not in menu_bridge
            and "[view rightMouseUp:" not in menu_bridge
        ),
        "context_event_is_consumed_once": (
            "event->accept()" in main_window
            and "QMainWindow::contextMenuEvent(event)" not in main_window
        ),
    })
    case("ST-HDR-VERSION-0.1.4", {
        "cmake_version": "project(Fovelle VERSION 0.1.4" in cmake,
        "qmake_version": "VERSION = 0.1.4" in qmake,
        "plist_version_twice": plist.count("<string>0.1.4</string>") >= 2,
        "runtime_assertion": 'QString("0.1.4")' in tests,
    })

    started = time.perf_counter()
    build_command = ["cmake", "--build", str(build_dir), "--parallel", "8"]
    build = subprocess.run(build_command, cwd=repo, text=True, capture_output=True, check=False)
    diff = subprocess.run(["git", "diff", "--check"], cwd=repo, text=True, capture_output=True, check=False)
    clang_tidy_binary = shutil.which("clang-tidy")
    if not clang_tidy_binary:
        bundled_tidy = Path("/opt/homebrew/opt/llvm/bin/clang-tidy")
        clang_tidy_binary = str(bundled_tidy) if bundled_tidy.is_file() else None
    clang_tidy_sdk_args: list[str] = []
    if platform.system() == "Darwin":
        sdk_result = subprocess.run(
            ["xcrun", "--show-sdk-path"], text=True, capture_output=True, check=False
        )
        sdk_path = Path(sdk_result.stdout.strip())
        libcxx_path = sdk_path / "usr/include/c++/v1"
        if sdk_result.returncode == 0 and sdk_path.is_dir() and libcxx_path.is_dir():
            # CMake's compile database uses Apple's /usr/bin/c++, whose driver
            # discovers the active SDK automatically. Homebrew clang-tidy uses
            # a different driver and otherwise cannot resolve <type_traits>.
            clang_tidy_sdk_args = [
                "--extra-arg=-isysroot", f"--extra-arg={sdk_path}",
                "--extra-arg=-isystem", f"--extra-arg={libcxx_path}",
            ]
    clang_tidy_command = [
        clang_tidy_binary or "clang-tidy",
        "-p",
        str(build_dir),
        str(repo / "src/qvcocoafunctions.mm"),
        str(repo / "src/qvgraphicsview.cpp"),
        str(repo / "src/qvimagecore.cpp"),
        str(repo / "src/qvimageloader.cpp"),
        *clang_tidy_sdk_args,
        "--quiet",
    ]
    if clang_tidy_binary:
        clang_tidy = subprocess.run(
            clang_tidy_command, cwd=repo, text=True, capture_output=True, check=False, timeout=120
        )
        clang_tidy_output = clang_tidy.stdout + clang_tidy.stderr
        clang_tidy_passed = (
            clang_tidy.returncode == 0
            and "warning:" not in clang_tidy_output
            and "error:" not in clang_tidy_output
        )
    else:
        clang_tidy = None
        clang_tidy_output = "clang-tidy executable not found"
        clang_tidy_passed = False
    elapsed = time.perf_counter() - started
    build_passed = build.returncode == 0
    diff_passed = diff.returncode == 0

    source_paths = [
        repo / "src/qvcocoafunctions.h",
        repo / "src/qvcocoafunctions.mm",
        repo / "src/qvnamespace.h",
        repo / "src/qvgraphicsview.cpp",
        repo / "src/mainwindow.cpp",
        repo / "src/qvimageloader.cpp",
        repo / "docs/hdr_pipeline.md",
    ]
    passed = (
        all(item["status"] == "passed" for item in cases)
        and build_passed
        and diff_passed
        and clang_tidy_passed
    )
    record = {
        "schema_version": "1.0",
        "kind": "static-test-evidence",
        "release": "v0.1.4",
        "generated_at_utc": datetime.now(timezone.utc).isoformat(),
        "host": {"platform": platform.platform(), "python": platform.python_version()},
        "commands": [build_command, ["git", "diff", "--check"], clang_tidy_command],
        "build": {
            "return_code": build.returncode,
            "elapsed_seconds": elapsed,
            "stdout": build.stdout,
            "stderr": build.stderr,
            "passed": build_passed,
        },
        "diff_check": {"return_code": diff.returncode, "output": diff.stdout + diff.stderr, "passed": diff_passed},
        "clang_tidy": {
            "binary": clang_tidy_binary,
            "return_code": clang_tidy.returncode if clang_tidy else None,
            "output": clang_tidy_output,
            "warnings_or_errors": [
                line for line in clang_tidy_output.splitlines() if "warning:" in line or "error:" in line
            ],
            "passed": clang_tidy_passed,
        },
        "source_hashes": [
            {"path": str(path.relative_to(repo)), "sha256": sha256(path), "bytes": path.stat().st_size}
            for path in source_paths
        ],
        "cases": cases,
        "summary": {
            "total": len(cases),
            "passed": sum(item["status"] == "passed" for item in cases),
            "failed": sum(item["status"] != "passed" for item in cases),
        },
        "passed": passed,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"kind": record["kind"], "summary": record["summary"], "passed": passed}, ensure_ascii=False))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
