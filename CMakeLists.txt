cmake_minimum_required(VERSION 3.16)

project(Fovelle VERSION 0.1.4 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "Fovelle supports macOS only.")
endif()

option(BUILD_TESTS "Build the deterministic Qt test suite" OFF)
option(FOVELLE_BUILD_TRANSLATIONS "Build and embed Qt translation catalogs" ON)
option(QV_DISABLE_ONLINE_VERSION_CHECK "Disable automatic version checks" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 QUIET COMPONENTS Core Gui Network Widgets Svg Concurrent)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Gui Network Widgets Svg Concurrent)
    set(QT_VERSION_MAJOR 5)
endif()

set(FOVELLE_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/openwith.cpp
    src/qvfileenumerator.cpp
    src/qvgraphicsview.cpp
    src/qvmenu.cpp
    src/qvoptionsdialog.cpp
    src/qvapplication.cpp
    src/qvaboutdialog.cpp
    src/qvrenamedialog.cpp
    src/qvwelcomedialog.cpp
    src/qvinfodialog.cpp
    src/qvimagecore.cpp
    src/qvimageloader.cpp
    src/qvmovie.cpp
    src/qvshortcutdialog.cpp
    src/actionmanager.cpp
    src/axislocker.cpp
    src/logicalpixelfitter.cpp
    src/scrollhelper.cpp
    src/settingsmanager.cpp
    src/shortcutmanager.cpp
    src/simplefonticonengine.cpp
    src/updatechecker.cpp
    src/qvcocoafunctions.mm
)

set(FOVELLE_HEADERS
    src/mainwindow.h
    src/openwith.h
    src/qvfileenumerator.h
    src/qvgraphicsview.h
    src/qvmenu.h
    src/qvnamespace.h
    src/qvoptionsdialog.h
    src/qvapplication.h
    src/qvaboutdialog.h
    src/qvrenamedialog.h
    src/qvwelcomedialog.h
    src/qvinfodialog.h
    src/qvimagecore.h
    src/qvimageloader.h
    src/qvmovie.h
    src/qvshortcutdialog.h
    src/actionmanager.h
    src/axislocker.h
    src/logicalpixelfitter.h
    src/scrollhelper.h
    src/settingsmanager.h
    src/shortcutmanager.h
    src/simplefonticonengine.h
    src/updatechecker.h
    src/qvcocoafunctions.h
)

set(FOVELLE_FORMS
    src/mainwindow.ui
    src/qvoptionsdialog.ui
    src/qvaboutdialog.ui
    src/qvwelcomedialog.ui
    src/qvinfodialog.ui
    src/qvshortcutdialog.ui
)

set(FOVELLE_RESOURCES resources/resources.qrc)

add_executable(${PROJECT_NAME} MACOSX_BUNDLE
    ${FOVELLE_SOURCES}
    ${FOVELLE_HEADERS}
    ${FOVELLE_FORMS}
    ${FOVELLE_RESOURCES}
)
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Concurrent
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
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    "VERSION_STRING=\"${PROJECT_VERSION}\""
    QT_DEPRECATED_WARNINGS
    QT_NO_FOREACH
)
if(QV_DISABLE_ONLINE_VERSION_CHECK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QV_DISABLE_ONLINE_VERSION_CHECK)
endif()

if(DEFINED NIGHTLY)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "NIGHTLY=\"${NIGHTLY}\"")
endif()

set(MACOSX_BUNDLE_ICON_FILE "qView.icns")
set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/dist/mac/${MACOSX_BUNDLE_ICON_FILE}")
set_source_files_properties(${app_icon_macos} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
target_sources(${PROJECT_NAME} PRIVATE ${app_icon_macos})

set(MACOSX_ICON_FILENAME ${MACOSX_BUNDLE_ICON_FILE})
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/mac/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    @ONLY
)
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.inostarlin-passion.Fovelle"
    MACOSX_BUNDLE_BUNDLE_NAME "Fovelle"
    MACOSX_BUNDLE_COPYRIGHT "Copyright © 2018–2025 jurplel and qView contributors; Fovelle modifications © 2026 Fovelle contributors"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

if(FOVELLE_BUILD_TRANSLATIONS)
    # A multilingual build without LinguistTools silently produces an
    # English-only bundle, while the settings dialog enumerates only embedded
    # .qm resources. Treat catalogs as a required release input instead.
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS LinguistTools)
    file(GLOB FOVELLE_TRANSLATIONS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/qview_*.ts")
    if(QT_VERSION_MAJOR EQUAL 6)
        qt_add_translations(${PROJECT_NAME}
            TS_FILES ${FOVELLE_TRANSLATIONS}
            RESOURCE_PREFIX /i18n
        )
    else()
        qt5_add_translation(FOVELLE_QM_FILES ${FOVELLE_TRANSLATIONS})
        target_sources(${PROJECT_NAME} PRIVATE ${FOVELLE_QM_FILES})
    endif()
endif()

include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/Fovelle)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
