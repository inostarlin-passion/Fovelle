#ifndef QVCOCOAFUNCTIONS_H
#define QVCOCOAFUNCTIONS_H

#include "openwith.h"
#include "qvnamespace.h"

#include <QWindow>
#include <QMenu>
#include <QImage>
#include <QByteArray>
#include <QList>
#include <QSize>

#include <memory>

class QVCocoaFunctions
{
public:
    struct NativeImageReadResult
    {
        QImage image;
        QSize intrinsicSize;
        QString typeIdentifier;
        QString errorString;
        bool isImageIOType {false};
        bool isRaw {false};
        bool usedRawPreview {false};
    };

    class AnimatedImage
    {
    public:
        virtual ~AnimatedImage() = default;

        virtual bool isValid() const = 0;
        virtual int frameCount() const = 0;
        virtual int loopCount() const = 0;
        virtual QImage frame(int frameNumber) const = 0;
        virtual int frameDelay(int frameNumber) const = 0;
    };

    static void showMenu(QMenu *menu, const QPoint &point, QWindow *window);

    static void setUserDefaults();

    static void registerWillPowerOffObserver();

    static void setFullSizeContentView(QWidget *window, const bool enable);

    static bool getTitlebarHidden(const QWidget *window);

    static void setTitlebarHidden(QWidget *window, const bool hide);

    static void setWindowCollectionBehaviorManaged(QWidget *window);

    static void setWindowTheme(Qv::Theme theme, QWindow *window);

    static QString getWindowAppearanceName(const QWindow *window);

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

    // Image I/O identifies the file from its contents. The caller must not use
    // a filename extension to decide whether this is a RAW image. Native
    // decoding intentionally preserves the source pixel dimensions so a later
    // zoom can reveal source detail instead of enlarging a screen thumbnail.
    static NativeImageReadResult readImageWithImageIO(const QString &filePath);

    static QImage readAdditionalImage(const QString &filePath, QString *errorString = nullptr);

    static std::unique_ptr<AnimatedImage> createAnimatedImage(const QString &filePath);
};

#endif // QVCOCOAFUNCTIONS_H
