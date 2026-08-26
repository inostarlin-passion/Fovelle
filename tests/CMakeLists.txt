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
    "VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
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

add_test(NAME FovelleTests COMMAND fovelle_tests)
set_tests_properties(FovelleTests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=cocoa;QT_FATAL_WARNINGS=1;QTEST_FUNCTION_TIMEOUT=30000"
    TIMEOUT 90
)

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME FovelleTaskAcceptanceAudit
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/ci_quality_pipeline.py"
            --repo "${PROJECT_SOURCE_DIR}"
            --build-dir "${CMAKE_BINARY_DIR}"
            --skip-build
    )
    set_tests_properties(FovelleTaskAcceptanceAudit PROPERTIES TIMEOUT 360)
endif()
