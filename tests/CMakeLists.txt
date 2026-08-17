# Tests for Fovelle
# This CMakeLists.txt should be included by the parent project

# Find Qt Test component and other dependencies
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core Gui Network Widgets Svg Concurrent Test REQUIRED)

# Get the same source files as the main project
set(TEST_SOURCES
    tst_actionmanagertests.cpp
    ../src/mainwindow.cpp
    ../src/openwith.cpp
    ../src/qvgraphicsview.cpp
    ../src/qvoptionsdialog.cpp
    ../src/qvapplication.cpp
    ../src/qvaboutdialog.cpp
    ../src/qvrenamedialog.cpp
    ../src/qvwelcomedialog.cpp
    ../src/qvinfodialog.cpp
    ../src/qvimagecore.cpp
    ../src/qvshortcutdialog.cpp
    ../src/actionmanager.cpp
    ../src/settingsmanager.cpp
    ../src/shortcutmanager.cpp
    ../src/qvcocoafunctions.mm
)

# Add updatechecker if enabled
if(NOT QV_DISABLE_ONLINE_VERSION_CHECK)
    list(APPEND TEST_SOURCES ../src/updatechecker.cpp)
endif()

# Test executable
add_executable(actionmanagertests ${TEST_SOURCES})

# Add include directory for source headers
target_include_directories(actionmanagertests PRIVATE ../src)

# Add Qt resources
set(TEST_RESOURCES ../resources/resources.qrc)
target_sources(actionmanagertests PRIVATE ${TEST_RESOURCES})

# Link to Qt libraries
target_link_libraries(actionmanagertests PRIVATE 
    Qt${QT_VERSION_MAJOR}::Core 
    Qt${QT_VERSION_MAJOR}::Gui 
    Qt${QT_VERSION_MAJOR}::Network 
    Qt${QT_VERSION_MAJOR}::Widgets 
    Qt${QT_VERSION_MAJOR}::Svg 
    Qt${QT_VERSION_MAJOR}::Concurrent
    Qt${QT_VERSION_MAJOR}::Test
)

target_link_libraries(actionmanagertests PRIVATE "-framework Cocoa")
target_compile_definitions(actionmanagertests PRIVATE COCOA_LOADED)

# Add compile definitions
target_compile_definitions(actionmanagertests PRIVATE
    "VERSION=${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}"
    "VERSION_STRING=\"${PROJECT_VERSION}\""
    QT_DEPRECATED_WARNINGS
    QT_NO_FOREACH
)

if(QV_DISABLE_ONLINE_VERSION_CHECK)
    target_compile_definitions(actionmanagertests PRIVATE QV_DISABLE_ONLINE_VERSION_CHECK)
endif()

# Add the test
add_test(NAME ActionManagerTests COMMAND actionmanagertests)
set_tests_properties(ActionManagerTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
