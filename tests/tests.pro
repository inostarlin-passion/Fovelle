QT += core testlib gui network widgets svg concurrent

macx:LIBS += -framework Cocoa -framework CoreGraphics -framework ImageIO -framework CoreImage -framework Metal -framework QuartzCore -framework ColorSync -framework CoreServices -framework UniformTypeIdentifiers

VERSION_FILE = $$clean_path($$PWD/../VERSION)
!exists($$VERSION_FILE) {
    error("Missing project version file: $$VERSION_FILE")
}
VERSION = $$cat($$VERSION_FILE, lines)
!contains(VERSION, ^[0-9]+\.[0-9]+\.[0-9]+$) {
    error("VERSION must contain exactly MAJOR.MINOR.PATCH without a v prefix")
}
DISTFILES += $$VERSION_FILE
DEFINES += "VERSION=$$VERSION"
DEFINES += "VERSION_STRING=\"$$VERSION\""

CONFIG += qt console warn_on depend_includepath testcase c++17
DEFINES += FOVELLE_HDR_SURFACE_TESTING FOVELLE_MOVIE_TEST_METRICS
DEFINES += FOVELLE_TEST_FIXTURE_DIR=\\\"$$PWD/data\\\"
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += tst_qviewtests.cpp native_titlebar_probe.mm native_color_fixtures.mm

INCLUDEPATH += ../src
include( ../src/src.pri )

SOURCES -= $$absolute_path(../src/main.cpp)
