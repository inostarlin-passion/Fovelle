cmake_minimum_required(VERSION 3.16)

project(Fovelle VERSION 0.1.0 LANGUAGES CXX)

if(NOT APPLE)
    message(FATAL_ERROR "Fovelle supports macOS only.")
endif()

# Optional test building
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
endif()

# Generate a compile_commands.json file for clang-tidy and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Fovelle is distributed as a macOS application bundle.
set(EXECUTABLE_TYPE MACOSX_BUNDLE)

# Enable C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable Qt's automatic tools
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Find required Qt components
find_package(Qt6 QUIET COMPONENTS Core Gui Network Widgets Svg Concurrent)
if(Qt6_FOUND)
    set(QT_VERSION_MAJOR 6)
    set(QT_VERSION ${Qt6_VERSION})
    # Find optional tools
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
else()
    set(QT5_FIND_COMPONENTS Core Gui Network Widgets Svg Concurrent)
    find_package(Qt5 REQUIRED COMPONENTS ${QT5_FIND_COMPONENTS})
    set(QT_VERSION_MAJOR 5)
    set(QT_VERSION ${Qt5_VERSION})
    # Find optional tools
    find_package(Qt5 QUIET COMPONENTS LinguistTools)
endif()

# Source files
set(PROJECT_SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/openwith.cpp
    src/qvgraphicsview.cpp
    src/qvoptionsdialog.cpp
    src/qvapplication.cpp
    src/qvaboutdialog.cpp
    src/qvrenamedialog.cpp
    src/qvwelcomedialog.cpp
    src/qvinfodialog.cpp
    src/qvimagecore.cpp
    src/qvshortcutdialog.cpp
    src/actionmanager.cpp
    src/settingsmanager.cpp
    src/shortcutmanager.cpp
    src/qvcocoafunctions.mm
)

# Header files
set(PROJECT_HEADERS
    src/mainwindow.h
    src/openwith.h
    src/qvgraphicsview.h
    src/qvoptionsdialog.h
    src/qvapplication.h
    src/qvaboutdialog.h
    src/qvrenamedialog.h
    src/qvwelcomedialog.h
    src/qvinfodialog.h
    src/qvimagecore.h
    src/qvshortcutdialog.h
    src/actionmanager.h
    src/settingsmanager.h
    src/shortcutmanager.h
    src/qvcocoafunctions.h
)

# UI form files
set(PROJECT_FORMS
    src/mainwindow.ui
    src/qvoptionsdialog.ui
    src/qvaboutdialog.ui
    src/qvwelcomedialog.ui
    src/qvinfodialog.ui
    src/qvshortcutdialog.ui
)

# Disable both manual and automatic checking for updates when requested.
option(QV_DISABLE_ONLINE_VERSION_CHECK "Disable online version check" OFF)
if(NOT QV_DISABLE_ONLINE_VERSION_CHECK)
    list(APPEND PROJECT_SOURCES src/updatechecker.cpp)
    list(APPEND PROJECT_HEADERS src/updatechecker.h)
endif()

# Add the executable and link Qt libraries
set(PROJECT_RESOURCES resources/resources.qrc)
add_executable(${PROJECT_NAME} ${EXECUTABLE_TYPE} ${PROJECT_SOURCES} ${PROJECT_HEADERS} ${PROJECT_FORMS} ${PROJECT_RESOURCES})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Gui
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
    Qt${QT_VERSION_MAJOR}::Concurrent
    "-framework Cocoa"
)

# Add compile definitions based on options
if(QV_DISABLE_ONLINE_VERSION_CHECK)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QV_DISABLE_ONLINE_VERSION_CHECK)
endif()

# Set the icon based on the Qt version, as per the Qt documentation.
if(QT_VERSION_MAJOR LESS 6)
    set(MACOSX_BUNDLE_ICON_FILE "qView_legacy.icns")
else()
    set(MACOSX_BUNDLE_ICON_FILE "qView.icns")
endif()

set(app_icon_macos "${CMAKE_CURRENT_SOURCE_DIR}/dist/mac/${MACOSX_BUNDLE_ICON_FILE}")
set_source_files_properties(${app_icon_macos} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)
target_sources(${PROJECT_NAME} PRIVATE ${app_icon_macos})

# The Info.plist.in file contains placeholders that will be replaced by CMake.
set(MACOSX_ICON_FILENAME ${MACOSX_BUNDLE_ICON_FILE})
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/dist/mac/Info.plist.in"
    "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    @ONLY
)

# Set bundle properties. MACOSX_BUNDLE_INFO_PLIST tells CMake to use our
# generated Info.plist instead of the default one.
set_target_properties(${PROJECT_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.inostarlin-passion.Fovelle"
    MACOSX_BUNDLE_BUNDLE_NAME "Fovelle"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
)

target_compile_definitions(${PROJECT_NAME} PRIVATE COCOA_LOADED)

# Compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    "VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    "VERSION_STRING=\"${PROJECT_VERSION}\""
    QT_DEPRECATED_WARNINGS
    QT_NO_FOREACH
)

# Check for nightly builds, which are identified by the NIGHTLY preprocessor definition.
if(DEFINED NIGHTLY)
    message(STATUS "This is nightly ${NIGHTLY}")
    target_compile_definitions(${PROJECT_NAME} PRIVATE "NIGHTLY=\"${NIGHTLY}\"")
endif()

# Handle translations
if(Qt${QT_VERSION_MAJOR}_LinguistTools_FOUND)
    message(STATUS "LinguistTools found, building translations.")
    file(GLOB TRANSLATIONS "i18n/qview_*.ts")
    if(QT_VERSION_MAJOR EQUAL 5)
        # For Qt5, use qt5_add_translation (not qt5_add_translations)
        qt5_add_translation(QM_FILES ${TRANSLATIONS})
        # Add the generated .qm files as resources or install them
        target_sources(${PROJECT_NAME} PRIVATE ${QM_FILES})
    else()
        # For Qt6, use qt_add_translations
        qt_add_translations(${PROJECT_NAME} TS_FILES ${TRANSLATIONS})
    endif()
else()
    message(STATUS "LinguistTools not found, skipping translations.")
endif()

# Installation rules
include(GNUInstallDirs)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/Fovelle)

# Include tests if requested
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()
