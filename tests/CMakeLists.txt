find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(FOVELLE_TEST_SOURCES
    tst_qviewtests.cpp
    native_titlebar_probe.mm
    ../src/mainwindow.cpp
    ../src/openwith.cpp
    ../src/qvfileenumerator.cpp
    ../src/qvgraphicsview.cpp
    ../src/qvgraphicsimageitem.cpp
    ../src/qvmenu.cpp
    ../src/qvoptionsdialog.cpp
    ../src/qvapplication.cpp
    ../src/qvaboutdialog.cpp
    ../src/qvrenamedialog.cpp
    ../src/qvinfodialog.cpp
    ../src/colorinformation.cpp
    ../src/qvimagecore.cpp
    ../src/qvimageloader.cpp
    ../src/qvmovie.cpp
    ../src/qvshortcutdialog.cpp
    ../src/actionmanager.cpp
    ../src/axislocker.cpp
    ../src/logicalpixelfitter.cpp
    ../src/scrollhelper.cpp
    ../src/settingsmanager.cpp
    ../src/shortcutmanager.cpp
    ../src/simplefonticonengine.cpp
    ../src/updatechecker.cpp
    ../src/nativedialogs.cpp
    ../src/qvcocoafunctions.mm
)

set(FOVELLE_TEST_HEADERS
    ../src/mainwindow.h
    ../src/openwith.h
    ../src/qvfileenumerator.h
    ../src/qvgraphicsview.h
    ../src/qvgraphicsimageitem.h
    ../src/qvmenu.h
    ../src/qvnamespace.h
    ../src/qvoptionsdialog.h
    ../src/qvapplication.h
    ../src/qvaboutdialog.h
    ../src/qvrenamedialog.h
    ../src/qvinfodialog.h
    ../src/colorinformation.h
    ../src/qvimagecore.h
    ../src/qvimageloader.h
    ../src/qvmovie.h
    ../src/qvshortcutdialog.h
    ../src/actionmanager.h
    ../src/axislocker.h
    ../src/logicalpixelfitter.h
    ../src/scrollhelper.h
    ../src/settingsmanager.h
    ../src/shortcutmanager.h
    ../src/simplefonticonengine.h
    ../src/updatechecker.h
    ../src/nativedialogs.h
    ../src/qvcocoafunctions.h
)

set(FOVELLE_TEST_FORMS
    ../src/mainwindow.ui
    ../src/qvoptionsdialog.ui
    ../src/qvaboutdialog.ui
    ../src/qvinfodialog.ui
    ../src/qvshortcutdialog.ui
)

add_executable(fovelle_tests
    ${FOVELLE_TEST_SOURCES}
    ${FOVELLE_TEST_HEADERS}
    ${FOVELLE_TEST_FORMS}
    ../resources/resources.qrc
)
fovelle_link_sparkle(fovelle_tests)
target_include_directories(fovelle_tests PRIVATE ../src)
target_link_libraries(fovelle_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    ZLIB::ZLIB
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Test
    "-framework Cocoa"
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework CoreImage"
    "-framework Metal"
    "-framework QuartzCore"
    "-framework ColorSync"
    "-framework CoreServices"
    "-framework UniformTypeIdentifiers"
)
target_compile_definitions(fovelle_tests PRIVATE
    "VERSION=${PROJECT_VERSION}"
    "VERSION_STRING=\"${PROJECT_VERSION}\""
    "FOVELLE_TEST_FIXTURE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/data\""
    QT_DEPRECATED_WARNINGS
    QT_NO_FOREACH
)
if(FOVELLE_BUILD_TRANSLATIONS)
    target_compile_definitions(fovelle_tests PRIVATE
        "FOVELLE_TRANSLATIONS_DIR=\"${PROJECT_BINARY_DIR}\""
    )
    if(TARGET Fovelle_lrelease)
        add_dependencies(fovelle_tests Fovelle_lrelease)
    endif()
endif()
if(QV_DISABLE_ONLINE_VERSION_CHECK)
    target_compile_definitions(fovelle_tests PRIVATE QV_DISABLE_ONLINE_VERSION_CHECK)
endif()

# This is an opt-in system-level reproduction driver. Unlike the QtTest target
# above, it launches the real app and posts a fixed CoreGraphics HID drag
# trajectory through kCGHIDEventTap. The external fixture is deliberately
# configurable because it lives on the developer's mounted test volume and is
# not available on hosted CI runners. Keep it out of the default CTest gate:
# an installed fixture and Accessibility/Post Event permissions are external
# state, so a local desktop run must opt into this test explicitly.
option(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION
    "Register the Accessibility-dependent native drag reproduction in CTest"
    OFF)

add_executable(fovelle_native_drag_helper
    native_drag_helper.mm
)
set_target_properties(fovelle_native_drag_helper PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
target_link_libraries(fovelle_native_drag_helper PRIVATE
    "-framework AppKit"
    "-framework ApplicationServices"
    "-framework CoreGraphics"
)

set(FOVELLE_NATIVE_DRAG_IMAGE
    "/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/3.jpeg"
    CACHE FILEPATH "Image used by the native CoreGraphics drag reproduction")
if(FOVELLE_ENABLE_NATIVE_DRAG_REPRODUCTION)
    add_test(NAME FovelleNativeDragReproduction
        COMMAND fovelle_native_drag_helper
            --app "$<TARGET_FILE:Fovelle>"
            --image "${FOVELLE_NATIVE_DRAG_IMAGE}"
    )
    set_tests_properties(FovelleNativeDragReproduction PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 90
        LABELS "native-ui;fullscreen;requires-accessibility"
    )
endif()

add_test(NAME FovelleTests
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/run_qt_test_suites.py
        $<TARGET_FILE:fovelle_tests>)
set_tests_properties(FovelleTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000"
    # This entry runs eight suites sequentially; hosted macOS runners exceed
    # 180s while still making progress. Individual Qt functions retain 30s.
    TIMEOUT 360
)

add_test(NAME FovelleNativeAlertsStatic
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/native_alerts_acceptance.py)
set_tests_properties(FovelleNativeAlertsStatic PROPERTIES
    TIMEOUT 30
    LABELS "macos;native-dialog;appearance;source-contract;acceptance"
)

add_test(NAME FovelleNativeAlertsDynamic COMMAND fovelle_tests
    testNativeMessageBoxesUseCocoaAlertsAcrossAppearanceAndSeverity
    testNativeMessageBoxStandardButtonResponses -v1)
set_tests_properties(FovelleNativeAlertsDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 45
    LABELS "macos;native-dialog;appearance;dynamic;acceptance"
)

add_test(NAME FovelleNavigationBoundaryHintStatic
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/navigation_boundary_hint_static.py)
set_tests_properties(FovelleNavigationBoundaryHintStatic PROPERTIES
    TIMEOUT 30
    LABELS "navigation;hint;localization;source-contract;acceptance"
)

add_test(NAME FovelleHDRFocusTransitionEvidence
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/test_hdr_focus_transition_metrics.py)
set_tests_properties(FovelleHDRFocusTransitionEvidence PROPERTIES
    TIMEOUT 30
    LABELS "hdr;focus;transition;dynamic-evidence;acceptance"
)

add_test(NAME FovelleNavigationBoundaryHintDynamic COMMAND fovelle_tests
    testNavigationBoundaryHintFeedbackAndAppearance
    testViewportImageRemainsSharpAfterFocusLoss -v1)
set_tests_properties(FovelleNavigationBoundaryHintDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 45
    LABELS "navigation;hint;focus;dynamic;acceptance"
)

set(FOVELLE_FULLSCREEN_AVIF_IMAGE
    "/Volumes/CRYSTAL/仓库/Fovelle App/sdr_test/1.avif"
    CACHE FILEPATH "AVIF image used by the Cocoa zoomed full-screen exit regression")
if(APPLE AND EXISTS "${FOVELLE_FULLSCREEN_AVIF_IMAGE}")
    add_test(NAME FovelleSDRFullScreenPresentation
        COMMAND fovelle_tests testProvidedRasterFullScreenTransitionKeepsImageVisible)
    set_tests_properties(FovelleSDRFullScreenPresentation PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=SDRSampleInteractionTests;FOVELLE_FULLSCREEN_SDR_IMAGE=${FOVELLE_FULLSCREEN_AVIF_IMAGE}"
        TIMEOUT 45
        RUN_SERIAL TRUE
        LABELS "native-ui;fullscreen;sdr")
endif()

add_test(NAME FovelleToggleFitStabilitySourceContract
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/toggle_fit_stability_static.py
        --repo ${CMAKE_SOURCE_DIR}
        --output ${CMAKE_BINARY_DIR}/test-results/toggle-fit-stability-static.json)
set_tests_properties(FovelleToggleFitStabilitySourceContract PROPERTIES
    TIMEOUT 30
    LABELS "zoom;toggle-fit;source-contract;acceptance"
)

add_test(NAME FovelleToggleFitStabilityAcceptance COMMAND fovelle_tests
    testToggleFitReturnHasMonotonicStableTerminalSize -v1)
set_tests_properties(FovelleToggleFitStabilityAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 45
    LABELS "zoom;toggle-fit;dynamic;acceptance"
)

add_test(NAME FovelleZoomScrollbarDurationSourceContract
    COMMAND ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/zoom_scrollbar_duration_static.py
        --repo ${CMAKE_SOURCE_DIR}
        --output ${CMAKE_BINARY_DIR}/test-results/zoom-scrollbar-duration-static.json)
set_tests_properties(FovelleZoomScrollbarDurationSourceContract PROPERTIES
    TIMEOUT 30
    LABELS "zoom;scrollbar;duration;source-contract;acceptance"
)

add_test(NAME FovelleScrollbarZoomDurationAcceptance COMMAND fovelle_tests
    testZoomAnchorProjectionIsNearestFeasible
    testZoomTransitionCoversWheelKeyboardAndMenus
    testZoomAnchorProjectsInsideAndOutsideImage
    testWheelZoomCrossesHorizontalScrollbarWithoutPositionJump -v1)
set_tests_properties(FovelleScrollbarZoomDurationAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 60
    LABELS "zoom;scrollbar;duration;dynamic;acceptance"
)

add_test(NAME FovelleToggleFitAnchorAcceptance COMMAND fovelle_tests
    testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor
    testToggleFitAnd100FreezesViewportCenterDuringScrollbarTransition -v1)
set_tests_properties(FovelleToggleFitAnchorAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 60
    LABELS "zoom;toggle-fit;anchor;dynamic;acceptance"
)

add_test(NAME FovelleToggleFitTrajectoryAcceptance COMMAND fovelle_tests
    testToggleFitAnd100CenterTrajectoryIsLinear -v1)
set_tests_properties(FovelleToggleFitTrajectoryAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 60
    LABELS "zoom;toggle-fit;trajectory;dynamic;acceptance"
)

add_test(NAME FovelleFiveIssueZoomAcceptance COMMAND fovelle_tests
    testZoomOutClearsStaleVerticalScrollRange
    testMousePanKeepsOverflowRangeAndContinuity
    testKeyboardZoomUsesCursorAnchor
    testToggleFitAnd100UsesDisplayedStateAndDirectionalAnchor
    testToggleFitAnd100UsesDisplayedState
    testMouseWheelKeepsBottomRightAnchor
    testZoomTerminalStateDoesNotRewriteViewport)
set_tests_properties(FovelleFiveIssueZoomAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 150
    LABELS "zoom;dynamic;acceptance"
)

add_test(NAME FovelleFourIssueZoomAcceptance COMMAND fovelle_tests
    testWheelZoomHasNoPositionJumpTrajectory
    testZoomOutHasNoTransientVerticalScrollBar
    testRightOutsideWheelZoomHasNoTransientHorizontalScrollBar
    testToggleFitTo100HasNoAvoidableBlankSpace)
set_tests_properties(FovelleFourIssueZoomAcceptance PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 120
    LABELS "zoom;dynamic;acceptance;four-issues"
)

add_test(NAME FovelleShortcutSettingsTests COMMAND fovelle_tests)
set_tests_properties(FovelleShortcutSettingsTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=ShortcutSettingsTests"
    TIMEOUT 90
)

add_test(NAME FovelleZoomScrollbarTrajectory COMMAND fovelle_tests
    testZoomKeepsVerticalScrollbarTrajectoryStable -v1)
set_tests_properties(FovelleZoomScrollbarTrajectory PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 120
    LABELS "zoom;scrollbar;trajectory"
)

add_test(NAME FovelleZoomScrollbarTrajectoryHiDpi COMMAND fovelle_tests
    testZoomKeepsVerticalScrollbarTrajectoryStable -v1)
set_tests_properties(FovelleZoomScrollbarTrajectoryHiDpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QT_SCALE_FACTOR=2;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 120
    LABELS "zoom;scrollbar;trajectory;hidpi"
)

add_test(NAME FovelleZoomScrollbarExpensiveRefinement COMMAND fovelle_tests
    testZoomKeepsVerticalScrollbarPositionDuringExpensiveRefinement -v1)
set_tests_properties(FovelleZoomScrollbarExpensiveRefinement PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 60
    LABELS "zoom;scrollbar;refinement;dynamic;acceptance"
)

add_test(NAME FovelleZoomScrollbarVerticalTopology COMMAND fovelle_tests
    testZoomKeepsVerticalScrollbarPositionWhenVerticalRangeAppears -v1)
set_tests_properties(FovelleZoomScrollbarVerticalTopology PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=GraphicsViewTests"
    TIMEOUT 60
    LABELS "zoom;scrollbar;vertical-topology;dynamic;acceptance"
)

add_test(NAME FovelleHiddenTitlebarFullScreen COMMAND fovelle_tests
    testTitlebarPresentationDuringFullScreen -v1)
set_tests_properties(FovelleHiddenTitlebarFullScreen PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 90
    RUN_SERIAL TRUE
    LABELS "macos;titlebar;fullscreen;dynamic;acceptance")

add_test(NAME FovelleFullScreenMetricsGate
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_fullscreen_system_metrics.py)
set_tests_properties(FovelleFullScreenMetricsGate PROPERTIES
    TIMEOUT 15
    LABELS "fullscreen;metrics;unit")

add_test(NAME FovelleFullScreenRefinement COMMAND fovelle_tests
    testFullScreenDefersExpensiveRefinement -v1)
set_tests_properties(FovelleFullScreenRefinement PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 30
    RUN_SERIAL TRUE
    LABELS "macos;fullscreen;refinement;dynamic;acceptance")

add_test(NAME FovelleNativeFullScreenRoundTrip COMMAND fovelle_tests
    testNativeFullScreenRoundTrip testExitFullscreenActionUsesEscapePath -v1)
set_tests_properties(FovelleNativeFullScreenRoundTrip PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 120 RUN_SERIAL TRUE
    LABELS "macos;fullscreen;native;dynamic;acceptance")

add_test(NAME FovelleFullScreenVisualContinuity COMMAND fovelle_tests
    testFullScreenVisualContinuity -v1)
set_tests_properties(FovelleFullScreenVisualContinuity PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 90 RUN_SERIAL TRUE
    LABELS "macos;fullscreen;continuity;dynamic;acceptance")

add_test(NAME FovelleHDRFullScreenVisualContinuity COMMAND fovelle_tests
    testHDRFullScreenVisualContinuity -v1)
set_tests_properties(FovelleHDRFullScreenVisualContinuity PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=60000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 300 RUN_SERIAL TRUE
    LABELS "macos;fullscreen;hdr;continuity;dynamic;acceptance")

# Display capture validates a controlled native content handoff separately
# from natural-animation characterization. HDR requires the local fixture.
add_test(NAME FovelleFullScreenBottomBackgroundHandoff COMMAND fovelle_tests
    testFullScreenBottomBackgroundHandoff -v1)
set_tests_properties(FovelleFullScreenBottomBackgroundHandoff PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=60000;FOVELLE_TEST_SUITE=WindowBehaviorTests"
    TIMEOUT 120 RUN_SERIAL TRUE
    LABELS "macos;fullscreen;display-capture;background-handoff;requires-desktop")

add_executable(fovelle_sparkle_probe sparkle_standard_ui_probe.mm)
set_target_properties(fovelle_sparkle_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF BUILD_WITH_INSTALL_RPATH TRUE INSTALL_RPATH "@executable_path/../Frameworks")
target_compile_options(fovelle_sparkle_probe PRIVATE -fobjc-arc)
target_link_libraries(fovelle_sparkle_probe PRIVATE "${FOVELLE_SPARKLE_FRAMEWORK}" "-framework AppKit")
add_test(NAME FovelleUpdateStatic
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/update_acceptance.py --static-only)
add_test(NAME FovelleUpdateConfiguration COMMAND fovelle_tests
    testUpdateConfigurationValidation testUpdateCheckFrequencyPolicy -v1)
set_tests_properties(FovelleUpdateConfiguration PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=FeatureTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 30)
option(FOVELLE_ENABLE_UPDATE_INSTALL_TESTS "Run disposable Sparkle UI/install/relaunch fixtures on a logged-in macOS desktop" OFF)
if(FOVELLE_ENABLE_UPDATE_INSTALL_TESTS)
    add_test(NAME FovelleUpdateInstall
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/update_acceptance.py)
    set_tests_properties(FovelleUpdateInstall PROPERTIES TIMEOUT 240 RUN_SERIAL TRUE LABELS "macos;update;native-ui;installer")
endif()

add_test(NAME FovelleHDRBrightnessStatic
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/hdr_brightness_static.py)
set_tests_properties(FovelleHDRBrightnessStatic PROPERTIES TIMEOUT 30 LABELS "hdr;brightness;static;acceptance")
add_test(NAME FovelleHDRBrightnessDynamic COMMAND fovelle_tests
    testHDRBrightnessTimingAndBezier testHDRBrightnessLinearLightPixels
    testTransitionCurveIsBoundedAndMonotonic testDisplayHeadroomBootstrapsFromPotentialCapability
    testHDRHeadroomIsClampedToContentAndDisplay testSDRDisplayForcesUnitHeadroom)
set_tests_properties(FovelleHDRBrightnessDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=HDRPolicyTests"
    TIMEOUT 60 LABELS "hdr;brightness;dynamic;acceptance")

add_test(NAME FovelleHDRColorPixels COMMAND fovelle_tests
    testHDRBrightnessPreservesEndpointColors -v1)
set_tests_properties(FovelleHDRColorPixels PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=HDRPolicyTests"
    TIMEOUT 30 LABELS "hdr;color;dynamic;acceptance")
set(FOVELLE_HDR_COLOR_FIXTURE_DIR "/Volumes/CRYSTAL/仓库/Fovelle App/hdr_test"
    CACHE PATH "Local HDR color regression fixtures")
if(EXISTS "${FOVELLE_HDR_COLOR_FIXTURE_DIR}/1.JPG" AND EXISTS "${FOVELLE_HDR_COLOR_FIXTURE_DIR}/2.DNG"
   AND EXISTS "${FOVELLE_HDR_COLOR_FIXTURE_DIR}/3.dng" AND EXISTS "${FOVELLE_HDR_COLOR_FIXTURE_DIR}/4.nef")
    add_test(NAME FovelleHDRColorSamples COMMAND fovelle_tests testHDRColorFidelity -v1)
    set_tests_properties(FovelleHDRColorSamples PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=HDRSampleTests;FOVELLE_HDR_JPEG_SAMPLE=${FOVELLE_HDR_COLOR_FIXTURE_DIR}/1.JPG;FOVELLE_HDR_RAW_SAMPLE=${FOVELLE_HDR_COLOR_FIXTURE_DIR}/2.DNG;FOVELLE_HDR_PLAIN_DNG_SAMPLE=${FOVELLE_HDR_COLOR_FIXTURE_DIR}/3.dng;FOVELLE_HDR_NEF_SAMPLE=${FOVELLE_HDR_COLOR_FIXTURE_DIR}/4.nef"
        TIMEOUT 90 LABELS "hdr;color;dynamic;local-fixture;acceptance")
    add_test(NAME FovelleHDRColorNative COMMAND fovelle_tests testHDRBrightnessNativeColorFidelity -v1)
    set_tests_properties(FovelleHDRColorNative PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=HDRPolicyTests;FOVELLE_HDR_JPEG_SAMPLE=${FOVELLE_HDR_COLOR_FIXTURE_DIR}/1.JPG"
        TIMEOUT 45 RUN_SERIAL TRUE LABELS "hdr;color;native;local-fixture;acceptance")
endif()

add_test(NAME FovelleZoomColorStatic
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/zoom_color_acceptance_static.py)
set_tests_properties(FovelleZoomColorStatic PROPERTIES TIMEOUT 30 LABELS "zoom;color;localization;static;acceptance")
add_test(NAME FovelleKeepZoomDynamic COMMAND fovelle_tests
    testKeepZoomAcrossNavigation testKeepCalculatedZoomAndFailedNavigation testKeepZoomWindowIsolationAndSession -v1)
set_tests_properties(FovelleKeepZoomDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=GraphicsViewTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 90 LABELS "zoom;dynamic;acceptance")
add_test(NAME FovelleColorInformationDynamic COMMAND fovelle_tests
    testSourceColorMetadata testColorContainerBoundaries testColorInformationFollowsPresentation -v1)
set_tests_properties(FovelleColorInformationDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=ImageCoreAndMovieTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 60 LABELS "color;dynamic;acceptance")
add_test(NAME FovelleZoomColorLocalization COMMAND fovelle_tests
    testKeepZoomPreferenceAndColorTranslations -v1)
set_tests_properties(FovelleZoomColorLocalization PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=FeatureTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 30 LABELS "zoom;color;localization;dynamic;acceptance")

add_test(NAME FovelleKeepZoomHiDpi COMMAND fovelle_tests
    testKeepZoomAcrossNavigation testKeepCalculatedZoomAndFailedNavigation testKeepZoomWindowIsolationAndSession -v1)
set_tests_properties(FovelleKeepZoomHiDpi PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QT_SCALE_FACTOR=2;FOVELLE_TEST_SUITE=GraphicsViewTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 90 LABELS "zoom;hidpi;dynamic;acceptance")

add_test(NAME FovelleKeepZoomPersistenceStatic
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/keep_zoom_persistence_static.py)
set_tests_properties(FovelleKeepZoomPersistenceStatic PROPERTIES
    TIMEOUT 30 LABELS "zoom;settings;persistence;static;acceptance")
add_test(NAME FovelleKeepZoomPersistenceDynamic COMMAND fovelle_tests
    testKeepZoomSurvivesStartupMigration testKeepZoomPersistsAcrossProcesses -v1)
set_tests_properties(FovelleKeepZoomPersistenceDynamic PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;FOVELLE_TEST_SUITE=FeatureTests;FOVELLE_DISABLE_AUTO_UPDATE_CHECK=1"
    TIMEOUT 90 LABELS "zoom;settings;persistence;restart;dynamic;acceptance")
