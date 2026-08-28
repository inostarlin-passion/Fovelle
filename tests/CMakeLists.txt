find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

set(FOVELLE_TEST_SOURCES
    tst_qviewtests.cpp
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
target_include_directories(fovelle_tests PRIVATE ../src)
target_link_libraries(fovelle_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
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

# This is an opt-in system-level reproduction driver.  Unlike the QtTest
# target above, it launches the real app and posts a fixed CoreGraphics HID
# drag trajectory through kCGHIDEventTap.  The external fixture is deliberately
# configurable because it lives on the developer's mounted test volume and is
# not available on hosted CI runners.
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

add_test(NAME FovelleTests COMMAND fovelle_tests)
set_tests_properties(FovelleTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000"
    TIMEOUT 90
)

add_test(NAME FovelleShortcutSettingsTests COMMAND fovelle_tests)
set_tests_properties(FovelleShortcutSettingsTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000;FOVELLE_TEST_SUITE=ShortcutSettingsTests"
    TIMEOUT 90
)
