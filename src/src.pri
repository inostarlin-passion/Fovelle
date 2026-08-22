SOURCES += \
    $$PWD/main.cpp \
    $$PWD/mainwindow.cpp \
    $$PWD/openwith.cpp \
    $$PWD/qvfileenumerator.cpp \
    $$PWD/qvgraphicsview.cpp \
    $$PWD/qvgraphicsimageitem.cpp \
    $$PWD/qvmenu.cpp \
    $$PWD/qvoptionsdialog.cpp \
    $$PWD/qvapplication.cpp \
    $$PWD/qvaboutdialog.cpp \
    $$PWD/qvrenamedialog.cpp \
    $$PWD/qvwelcomedialog.cpp \
    $$PWD/qvinfodialog.cpp \
    $$PWD/qvimagecore.cpp \
    $$PWD/qvimageloader.cpp \
    $$PWD/qvmovie.cpp \
    $$PWD/qvshortcutdialog.cpp \
    $$PWD/actionmanager.cpp \
    $$PWD/axislocker.cpp \
    $$PWD/logicalpixelfitter.cpp \
    $$PWD/scrollhelper.cpp \
    $$PWD/settingsmanager.cpp \
    $$PWD/shortcutmanager.cpp \
    $$PWD/simplefonticonengine.cpp \
    $$PWD/updatechecker.cpp

macx:SOURCES += $$PWD/qvcocoafunctions.mm

HEADERS += \
    $$PWD/mainwindow.h \
    $$PWD/openwith.h \
    $$PWD/qvfileenumerator.h \
    $$PWD/qvgraphicsview.h \
    $$PWD/qvgraphicsimageitem.h \
    $$PWD/qvmenu.h \
    $$PWD/qvnamespace.h \
    $$PWD/qvoptionsdialog.h \
    $$PWD/qvapplication.h \
    $$PWD/qvaboutdialog.h \
    $$PWD/qvrenamedialog.h \
    $$PWD/qvwelcomedialog.h \
    $$PWD/qvinfodialog.h \
    $$PWD/qvimagecore.h \
    $$PWD/qvimageloader.h \
    $$PWD/qvmovie.h \
    $$PWD/qvshortcutdialog.h \
    $$PWD/actionmanager.h \
    $$PWD/axislocker.h \
    $$PWD/logicalpixelfitter.h \
    $$PWD/scrollhelper.h \
    $$PWD/settingsmanager.h \
    $$PWD/shortcutmanager.h \
    $$PWD/simplefonticonengine.h \
    $$PWD/updatechecker.h

macx:HEADERS += $$PWD/qvcocoafunctions.h

FORMS += \
    $$PWD/mainwindow.ui \
    $$PWD/qvoptionsdialog.ui \
    $$PWD/qvaboutdialog.ui \
    $$PWD/qvwelcomedialog.ui \
    $$PWD/qvinfodialog.ui \
    $$PWD/qvshortcutdialog.ui
