QT += core testlib gui network widgets

macx:LIBS += -framework Cocoa -framework CoreGraphics -framework ImageIO -framework CoreImage -framework Metal -framework ColorSync -framework CoreServices

VERSION = 1.0
DEFINES += "VERSION=$$VERSION"

CONFIG += qt console warn_on depend_includepath testcase
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += tst_qviewtests.cpp

INCLUDEPATH += ../src
include( ../src/src.pri )

SOURCES -= $$absolute_path(../src/main.cpp)
