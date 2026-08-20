TARGET = Fovelle
VERSION = 0.1.4

QT += core gui network widgets svg

TEMPLATE = app

QMAKE_PROJECT_DEPTH = 0

# allows use of version variables elsewhere
DEFINES += "VERSION=$$VERSION"
DEFINES += "VERSION_STRING=\"$$VERSION\""

# build folder organization
DESTDIR = bin
OBJECTS_DIR = build
MOC_DIR = build
UI_DIR = build
RCC_DIR = build

CONFIG -= debug_and_release debug_and_release_target

# enable c++17
CONFIG += c++17

# Print if this is a debug or release build
CONFIG(debug, debug|release) {
    message("This is a debug build")
} else {
    message("This is a release build")
}

# Check nightly variable
# to use: qmake NIGHTLY=VERSION
!isEmpty(NIGHTLY) {
    message("This is nightly $$NIGHTLY")
    DEFINES += "NIGHTLY=$$NIGHTLY"
}

!macx {
    error("Fovelle supports macOS only.")
}

# macOS specific stuff
macx {
    LIBS += -framework Cocoa -framework CoreGraphics -framework ImageIO -framework CoreImage -framework Metal -framework QuartzCore -framework ColorSync -framework CoreServices -framework UniformTypeIdentifiers

    QMAKE_TARGET_BUNDLE_PREFIX = "io.github.inostarlin-passion"
    QMAKE_INFO_PLIST = "dist/mac/Info.plist"
    ICON = "dist/mac/qView.icns"
    QMAKE_TARGET_DESCRIPTION = "Fovelle"
    QMAKE_TARGET_COPYRIGHT = "Copyright \\251 2018-2025 jurplel and qView contributors; Fovelle modifications \\251 2026 Fovelle contributors"
}

# The following define makes your compiler emit warnings if you use
# any feature of Qt which has been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

# Ban usage of Qt's built in foreach utility for better code style
DEFINES += QT_NO_FOREACH

include(src/src.pri)

CONFIG += lrelease embed_translations
TRANSLATIONS += $$files(i18n/qview_*.ts)

lupdate_only {
    TRANSLATIONS += i18n/template.ts
}

qtbase_translations.files = \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_de.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_es.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_fr.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_ja.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_ko.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_ru.qm \
    $$[QT_INSTALL_TRANSLATIONS]/qtbase_zh_CN.qm
qtbase_translations.base = $$[QT_INSTALL_TRANSLATIONS]
qtbase_translations.prefix = /qt-translations

RESOURCES += \
    resources/resources.qrc \
    qtbase_translations
