#ifndef QVCOCOAFUNCTIONS_H
#define QVCOCOAFUNCTIONS_H

#include "openwith.h"

#include <QWindow>
#include <QMenu>
#include <QImage>
#include <QByteArray>
#include <QList>

class QVCocoaFunctions
{
public:
    static void showMenu(QMenu *menu, const QPoint &point, QWindow *window);

    static void setUserDefaults();

    static void registerWillPowerOffObserver();

    static void setFullSizeContentView(QWidget *window, const bool enable);

    static bool getTitlebarHidden(const QWidget *window);

    static void setTitlebarHidden(QWidget *window, const bool hide);

    static void setWindowCollectionBehaviorManaged(QWidget *window);

    static void setVibrancy(bool alwaysDark, QWindow *window);

    static int getObscuredHeight(QWindow *window);

    static bool startWindowDrag(QWindow *window);

    static void setWindowMenu(QMenu *menu);

    static void setAlternate(QMenu *menu, int index);

    static void setDockRecents(const QStringList &recentPathsList);

    static QList<OpenWith::OpenWithItem> getOpenWithItems(const QString &filePath, const bool loadIcons, const QString &defaultSuffix);

    static QByteArray getIccProfileForWindow(const QWindow *window);

    static QList<QByteArray> getAdditionalImageFormats();

    static QList<QString> getAdditionalImageMimeTypes();

    static bool supportsAdditionalImageFormat(const QByteArray &format);

    static QImage readAdditionalImage(const QString &filePath, QString *errorString = nullptr);
};

#endif // QVCOCOAFUNCTIONS_H
