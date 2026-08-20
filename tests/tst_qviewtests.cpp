#include <QtTest>
#include <algorithm>
#include <numeric>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QImage>
#include <QLineF>
#include <QFile>
#include <QPainter>
#include <QPropertyAnimation>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <QWheelEvent>
#include <QScrollBar>
#include <QSet>
#include <QTableWidget>

#include "mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvgraphicsview.h"
#include "qvimagecore.h"
#include "qvimageloader.h"
#include "qvmovie.h"
#include "qvoptionsdialog.h"
#include "qvinfodialog.h"

class ImageLoaderTests : public QObject
{
    Q_OBJECT

private slots:
    void testImageLoaderPriorities();
    void testImageLoaderCacheAndAttachment();
    void testImageLoaderRetainedDuringDelivery();
    void testImageLoaderForegroundRequestPreservesCache();
    void testImageLoaderSupersededForegroundDiscarded();
    void testImageLoaderDisabledRetention();
    void testImageLoaderCachedErrorRetry();
    void testImageLoaderDestructionDuringLoad();
    void testImageLoaderLoadsWebpWithImageIOFallback();
    void testImageLoaderLoadsAvifWithImageIOFallback();
    void testImageLoaderAppliesWebpOrientation();
    void testImageLoaderAppliesAvifOrientation();
    void testImageLoaderLoadsTiffWithImageIO();
    void testImageIOUsesContentTypeInsteadOfFilenameExtension();
    void testImageLoaderPreservesSourceResolutionForZoom();
};

class ActionManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void testClonedActionsUntracked();
    void testApplicationIdentity();
    void testAboutDialogIdentity();
    void testWindowTitleIdentity();
    void testLastWindowClosedPolicy();
    void testReturnKeyEntersFullscreen();
    void testKeypadEnterEntersFullscreen();
    void testEnterDoesNotExitFullscreen();
    void testEscapeExitsFullscreen();
    void testEscapeRestoresLoadedImageWithoutGeometryJump();
    void testEscapeClosesWindow();
};

class FeatureTests : public QObject
{
    Q_OBJECT

private slots:
    void testApplicationVersionIsCurrent();
    void testWindowIconIsCleared();
    void testTitlebarDocumentProxyIsClearedForLoadedFile();
    void testTitlebarIconClearingIsIdempotent();
    void testSettingsFormatsIncludeNativeImageFormats();
    void testSettingsFormatsIncludeTiffAndSystemRawFormats();
    void testSmallImageOneToOneSettingIsExposedInImageOptions();
    void testOpenWithWorkerTeardownContract();
};

class GraphicsViewTests : public QObject
{
    Q_OBJECT

private slots:
    void testMouseWheelUsesOneDiscreteStep();
    void testTouchpadWheelCanUseFractionalSteps();
    void testImageIsCenteredAfterOpeningWithScrollBars();
    void testTouchpadWheelRespectsConfiguredZoomWithScrollBars();
    void testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling();
    void testRotatedZoomToFitUsesUnobscuredViewport();
    void testZoomAcrossScrollbarThresholdKeepsViewportCenterStable();
    void testTouchpadPanUsesPixelsWithoutChangingZoom();
    void testFitZoomSurvivesInverseWheelStepsAndFullscreenResize();
    void testManualZoomRemainsManualAcrossResize();
    void testSmallImageOneToOnePolicyUsesViewportAndWindowMode();
    void testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages();
    void testNativePinchZoomChangesScaleAtGesturePosition();
    void testNativePanChangesViewport();
    void testScrollBarsFollowImageOverflowAxes();
    void testScrollBarsMatchTheme();
    void testNativeGestureResponsePerformance();
};

class ApplicationEventTests : public QObject
{
    Q_OBJECT

private slots:
    void testFileOpenEventIsDeferredAndLoadsImage();
    void testFileOpenEventWithoutPathIsIgnored();
};

class ImageCoreAndMovieTests : public QObject
{
    Q_OBJECT

private slots:
    void testColorSpaceConversion();
    void testMovieSpeedAndSingleFrameRead();
    void testAnimatedPngPlaysBeyondFirstFrame();
};

class WindowBehaviorTests : public QObject
{
    Q_OBJECT

private slots:
    void testFullscreenDefaultShortcutIsEnterAndConfigurable();
    void testEnterDoesNotBypassClearedFullscreenShortcut();
    void testConfiguredFullscreenShortcutStillWorks();
    void testPracticalTitlebarTextUsesFilenameAndSequence();
    void testDefaultTitlebarTextIsPractical();
    void testVerboseTitlebarTextUsesAllRequestedFields();
    void testThemeSettingsReplaceRemovedColorControls();
    void testThemeAppliesNativeAppearanceAndViewportBackground();
    void testCheckerboardOverridesThemeAndRestoresBackground();
    void testNavigationEdgeActivationExcludesTitlebar();
    void testNavigationButtonSizingAndNoDelay();
    void testNavigationButtonsUseActualContentContrast();
    void testNavigationButtonsFadeTransition();
    void testNavigationButtonsClickSwitchesFiles();
};

class TestableImageCore : public QVImageCore
{
public:
    using QVImageCore::handleColorSpaceConversion;
};

static QString createTestImage(const QTemporaryDir &dir, const QString &name, const QColor color, const QSize size = QSize(32, 32))
{
    const QString path = dir.filePath(name + ".png");
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    if (!image.save(path))
        return {};
    return path;
}

static QString createTransparentImage(const QTemporaryDir &dir, const QString &name, const QSize size = QSize(1, 1))
{
    const QString path = dir.filePath(name + ".png");
    QImage image(size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    if (!image.save(path))
        return {};
    return path;
}

static QString createSplitImage(const QTemporaryDir &dir, const QString &name, const QSize size = QSize(1600, 1000))
{
    const QString path = dir.filePath(name + ".png");
    QImage image(size, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, size.width() / 2, size.height()), QColorConstants::White);
    painter.fillRect(QRect(size.width() / 2, 0, size.width() - size.width() / 2, size.height()), QColorConstants::Black);
    painter.end();
    if (!image.save(path))
        return {};
    return path;
}

static QString createHighResolutionDetailImage(const QTemporaryDir &dir, const QString &name, const QSize size = QSize(2400, 1600))
{
    const QString path = dir.filePath(name + ".png");
    QImage image(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
    {
        for (int x = 0; x < size.width(); ++x)
            image.setPixelColor(x, y, ((x + y) % 2 == 0) ? QColorConstants::White : QColorConstants::Black);
    }
    if (!image.save(path))
        return {};
    return path;
}

static QString createTiffImage(const QTemporaryDir &dir, const QString &name, const QString &extension = QStringLiteral("tiff"))
{
    const QString path = dir.filePath(name + "." + extension);
    // Keep this fixture independent of Qt image plugins. Image I/O is the
    // subject under test, so the test must not require a TIFF writer plugin.
    static const QByteArray tinyTiffBase64 =
        "SUkqAAgAAAAKAAABAwABAAAABAAAAAEBAwABAAAAAwAAAAIBAwADAAAAhgAAAAMBAwABAAAAAQAAAAYBAwABAAAAAgAAABEBBAABAAAAjAAAABUBAwABAAAAAwAAABYBBAABAAAAAwAAABcBBAABAAAAJAAAABwBAwABAAAAAQAAAAAAAAAIAAgACAAAgIAAgIAAgIAAgIAAgIAAgIAAgIAAgIAAgIAAgIAAgIAAgIA=";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(tinyTiffBase64)) <= 0)
        return {};
    return path;
}

static void sendMouseMove(QWidget *widget, const QPoint &position)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QMouseEvent event(
        QEvent::MouseMove,
        QPointF(position),
        QPointF(position),
        QPointF(widget->mapToGlobal(position)),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier);
#else
    QMouseEvent event(
        QEvent::MouseMove,
        position,
        widget->mapToGlobal(position),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier);
#endif
    QCoreApplication::sendEvent(widget, &event);
}

static bool sendNativeGesture(
    QVGraphicsView *view,
    const Qt::NativeGestureType type,
    const QPoint &position,
    const qreal value = 0.0,
    const QPointF &delta = {})
{
    QWidget *receiver = view->viewport();
    const QPointF localPosition(position);
    // The production handler consumes local viewport coordinates.  Keep the
    // synthetic scene coordinate independent from QWidget parent hierarchy so
    // the test remains deterministic for native Cocoa widgets.
    const QPointF scenePosition(position);
    const QPointF globalPosition(receiver->mapToGlobal(position));
#if QT_VERSION >= QT_VERSION_CHECK(6, 2, 0)
    QNativeGestureEvent event(
        type,
        QPointingDevice::primaryPointingDevice(),
        2,
        localPosition,
        scenePosition,
        globalPosition,
        value,
        delta);
#else
    QNativeGestureEvent event(
        type,
        nullptr,
        localPosition,
        scenePosition,
        globalPosition,
        value,
        0,
        0);
#endif
    const bool delivered = QCoreApplication::sendEvent(receiver, &event);
    return delivered && event.isAccepted();
}

static bool containsColor(const QImage &image, const QRect &area, const QColor &color)
{
    const QRect clippedArea = area.intersected(image.rect());
    for (int y = clippedArea.top(); y <= clippedArea.bottom(); ++y)
    {
        for (int x = clippedArea.left(); x <= clippedArea.right(); ++x)
        {
            if (image.pixelColor(x, y) == color)
                return true;
        }
    }
    return false;
}

static QString createBase64Image(const QTemporaryDir &dir, const QString &name, const QString &extension, const QByteArray &base64)
{
    const QString path = dir.filePath(name + "." + extension);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(base64)) <= 0)
        return {};
    return path;
}

class ScopedOptionValues
{
public:
    explicit ScopedOptionValues(const QHash<QString, QVariant> &values)
    {
        QSettings settings;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
        {
            const QString fullKey = QStringLiteral("options/") + it.key();
            savedValues.insert(it.key(), {settings.contains(fullKey), settings.value(fullKey)});
            settings.setValue(fullKey, it.value());
        }
        settings.sync();
        qvApp->getSettingsManager().loadSettings();
    }

    ~ScopedOptionValues()
    {
        QSettings settings;
        for (auto it = savedValues.cbegin(); it != savedValues.cend(); ++it)
        {
            const QString fullKey = QStringLiteral("options/") + it.key();
            if (it.value().first)
                settings.setValue(fullKey, it.value().second);
            else
                settings.remove(fullKey);
        }
        settings.sync();
        qvApp->getSettingsManager().loadSettings();
    }

private:
    QHash<QString, QPair<bool, QVariant>> savedValues;
};

class ScopedShortcutValues
{
public:
    explicit ScopedShortcutValues(const QHash<QString, QVariant> &values)
    {
        QSettings settings;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
        {
            const QString fullKey = QStringLiteral("shortcuts/") + it.key();
            savedValues.insert(it.key(), {settings.contains(fullKey), settings.value(fullKey)});
            settings.setValue(fullKey, it.value());
        }
        settings.sync();
        qvApp->getShortcutManager().updateShortcuts();
    }

    ~ScopedShortcutValues()
    {
        QSettings settings;
        for (auto it = savedValues.cbegin(); it != savedValues.cend(); ++it)
        {
            const QString fullKey = QStringLiteral("shortcuts/") + it.key();
            if (it.value().first)
                settings.setValue(fullKey, it.value().second);
            else
                settings.remove(fullKey);
        }
        settings.sync();
        qvApp->getShortcutManager().updateShortcuts();
    }

private:
    QHash<QString, QPair<bool, QVariant>> savedValues;
};

static std::optional<QVImageLoader::Result> loadImage(const QString &path)
{
    QVImageLoader loader;
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const quint64 requestId = loader.requestImage(path);
    loader.setDesiredImages({{path, 0}});
    if (!readySpy.wait(5000) && readySpy.isEmpty())
        return {};
    if (readySpy.isEmpty() || readySpy.at(0).at(0).toULongLong() != requestId)
        return {};
    return qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
}

static const QByteArray tinyWebpBase64 =
    "UklGRlIAAABXRUJQVlA4WAoAAAAQAAAAAAAAAAAAQUxQSAIAAAAArlZQOCAqAAAAkAEAnQEqAQABAAIANCWgAnS6AAOYAP7wumv/BBbUemHHh/c1FbFtAAAA";

static const QByteArray tinyAvifBase64 =
    "AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUEAAAG7bWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAADppbG9jAAAAAEQAAAMAAQAAAAEAAAI9AAAAHwACAAAAAQAAAisAAAASAAMAAAABAAAB4wAAAEgAAABbaWluZgAAAAAAAwAAABppbmZlAgAAAAABAABhdjAxQ29sb3IAAAAAGmluZmUCAAAAAAIAAGF2MDFBbHBoYQAAAAAZaW5mZQIAAAAAAwAARXhpZkV4aWYAAAAAKGlyZWYAAAAAAAAADmF1eGwAAgABAAEAAAAOY2RzYwADAAEAAQAAAMNpcHJwAAAAnWlwY28AAAAUaXNwZQAAAAAAAAABAAAAAQAAABBwaXhpAAAAAAMICAgAAAAMYXYxQ4EgAAAAAAATY29scm5jbHgAAQANAAaAAAAADnBpeGkAAAAAAQgAAAAMYXYxQ4EAHAAAAAA4YXV4QwAAAAB1cm46bXBlZzptcGVnQjpjaWNwOnN5c3RlbXM6YXV4aWxpYXJ5OmFscGhhAAAAAB5pcG1hAAAAAAAAAAIAAQQBAoMEAAIEAQWGBwAAAIFtZGF0AAAAAE1NACoAAAAIAAGHaQAEAAAAAQAAABoAAAAAAAOgAQADAAAAAQABAACgAgAEAAAAAQAAAAGgAwAEAAAAAQAAAAEAAAAAEgAKBBgABhUyCBAATiImmSrQEgAKBzgABhAQ0GkyEhAAAE4dz4eZAFvClYOQUfU8Kg==";

static const QByteArray tinyAnimatedPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAACGFjVEwAAAACAAAAAPONk3AAAAAaZmNUTAAAAAAAAAACAAAAAgAAAAAAAAAAAAEACgAA6FTcAAAAABFJREFUeJxj+P+fgQGEGWAMAE/CB/njowPaAAAAGmZjVEwAAAABAAAAAgAAAAIAAAAAAAAAAAABAAoAAHMnNtQAAAAVZmRBVAAAAAJ4nGP4z8DwH4QZYAwAR8oH+YqD/xkAAAAASUVORK5CYII=";

static const QByteArray orientedWebpBase64 =
    "UklGRmYAAABXRUJQVlA4WAoAAAAIAAAAAQAAAgAAVlA4TCUAAAAvAYAAAC8gEEjaH3qN+RcQFPk/2vwHH0QCg0AgDVFkMMAR/Y8GAEVYSUYaAAAATU0AKgAAAAgAAQESAAMAAAABAAYAAAAAAAA=";

static const QByteArray orientedAvifBase64 =
    "AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUEAAAD1bWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAAB5pbG9jAAAAAEQAAAEAAQAAAAEAAAEdAAAAYwAAAChpaW5mAAAAAAABAAAAGmluZmUCAAAAAAEAAGF2MDFDb2xvcgAAAAB0aXBycAAAAFRpcGNvAAAAFGlzcGUAAAAAAAAAAgAAAAMAAAAQcGl4aQAAAAADCAgIAAAADGF2MUOBIAAAAAAAE2NvbHJuY2x4AAEADQAAgAAAAAlpcm90AQAAABhpcG1hAAAAAAAAAAEAAQUBAoMEhQAAAGttZGF0EgAKBzgAcwgIaAEyVhAAAIu7FZVujlR7Yotii5zIf////////81uz4UZYgX13041615VbWdWdWb15VbWdWezuZv/////73qwfKnW17zgsHyp1tesH216wfKnW16wfKnSp2tA";

static void reportFullscreenMetric(const QString &phase, qint64 elapsedMs)
{
    qInfo().noquote() << QStringLiteral("FS_METRIC %1_ms=%2").arg(phase).arg(elapsedMs);
}

void ImageLoaderTests::testImageLoaderPriorities()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString adjacentBefore = createTestImage(dir, "adjacent-before", Qt::green);
    const QString adjacentAfter = createTestImage(dir, "adjacent-after", Qt::blue);
    const QString extendedBefore = createTestImage(dir, "extended-before", Qt::cyan);
    const QString extendedAfter = createTestImage(dir, "extended-after", Qt::magenta);
    QVERIFY(!target.isEmpty());
    QVERIFY(!adjacentBefore.isEmpty());
    QVERIFY(!adjacentAfter.isEmpty());
    QVERIFY(!extendedBefore.isEmpty());
    QVERIFY(!extendedAfter.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    QVERIFY(startedSpy.isValid());
    QVERIFY(readySpy.isValid());

    const quint64 requestId = loader.requestImage(target);
    loader.setDesiredImages({
        {target, 0},
        {adjacentBefore, 1},
        {adjacentAfter, 1},
        {extendedBefore, 2},
        {extendedAfter, 2}
    });

    QCOMPARE(startedSpy.size(), 1);
    QCOMPARE(startedSpy.at(0).at(0).toString(), target);
    QCOMPARE(startedSpy.at(0).at(1).toInt(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), requestId);
    const auto result = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QCOMPARE(result.absoluteFilePath, target);
    QVERIFY(!result.image.isNull());

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.size(), 5, 5000);
    const QList<int> expectedPriorities {0, 1, 1, 2, 2};
    for (int i = 0; i < expectedPriorities.size(); ++i)
        QCOMPARE(startedSpy.at(i).at(1).toInt(), expectedPriorities.at(i));
}

void ImageLoaderTests::testImageLoaderCacheAndAttachment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    const quint64 attachedRequestId = loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), attachedRequestId);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.setDesiredImages({});
    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
}

void ImageLoaderTests::testImageLoaderRetainedDuringDelivery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    connect(&loader, &QVImageLoader::imageReady, this,
        [&loader, path](quint64, const QVImageLoader::Result &) {
            loader.setDesiredImages({{path, 0}});
        });

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);
}

void ImageLoaderTests::testImageLoaderForegroundRequestPreservesCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachedPath = createTestImage(dir, "cached", Qt::green);
    const QString foregroundPath = createTestImage(dir, "foreground", Qt::blue);
    QVERIFY(!cachedPath.isEmpty());
    QVERIFY(!foregroundPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(cachedPath);
    loader.setDesiredImages({{cachedPath, 0}});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(foregroundPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 2);

    loader.requestImage(cachedPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
    QCOMPARE(startedSpy.size(), 2);
}

void ImageLoaderTests::testImageLoaderSupersededForegroundDiscarded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createTestImage(dir, "first", Qt::red);
    const QString secondPath = createTestImage(dir, "second", Qt::blue);
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(firstPath);
    const quint64 secondRequestId = loader.requestImage(secondPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), secondRequestId);

    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    loader.requestImage(firstPath);
    QCOMPARE(startedSpy.size(), 3);
}

void ImageLoaderTests::testImageLoaderDisabledRetention()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::black);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
}

void ImageLoaderTests::testImageLoaderCachedErrorRetry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath("invalid.png");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not an image"), 12);
    file.close();

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    const auto firstResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QVERIFY(firstResult.errorData.has_value());

    loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    const auto secondResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(1).at(1));
    QVERIFY(secondResult.errorData.has_value());
}

void ImageLoaderTests::testImageLoaderDestructionDuringLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString queuedPreload = createTestImage(dir, "queued-preload", Qt::blue);
    QVERIFY(!target.isEmpty());
    QVERIFY(!queuedPreload.isEmpty());

    QStringList startedPaths;
    auto *loader = new QVImageLoader;
    connect(loader, &QVImageLoader::loadStarted, this,
        [&startedPaths](const QString &path, int) { startedPaths.append(path); });

    loader->requestImage(target);
    loader->setDesiredImages({
        {target, 0},
        {queuedPreload, 1}
    });
    QCOMPARE(startedPaths, QStringList {target});

    delete loader;
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();

    QCOMPARE(startedPaths, QStringList {target});
}

void ImageLoaderTests::testImageLoaderLoadsWebpWithImageIOFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("webp"));
    const QString path = createBase64Image(dir, "native-webp", "webp", tinyWebpBase64);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QCOMPARE(result->image.size(), QSize(1, 1));
    QVERIFY(!result->errorData.has_value());
}

// TC-ORI-WEBP
// Test purpose: verify that a WebP EXIF orientation is applied by both the native decoder and the loader.
// Preconditions: macOS Image I/O advertises WebP support and a temporary directory is writable.
// Input data: a lossless 2x3 WebP whose EXIF orientation is RightTop (90-degree clockwise display).
// Steps: decode through QVCocoaFunctions, then open the same file through QVImageLoader.
// Expected result: both paths produce a 3x2 image with the rotated corner colors and no error data.
// Postcondition: the temporary fixture and all loader resources are released.
void ImageLoaderTests::testImageLoaderAppliesWebpOrientation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("webp"));
    const QString path = createBase64Image(dir, "oriented-webp", "webp", orientedWebpBase64);
    QVERIFY(!path.isEmpty());

    QString nativeError;
    const QImage nativeImage = QVCocoaFunctions::readAdditionalImage(path, &nativeError);
    QVERIFY2(!nativeImage.isNull(), qPrintable(nativeError));
    QCOMPARE(nativeImage.size(), QSize(3, 2));
    QCOMPARE(nativeImage.pixelColor(0, 0), QColor(Qt::magenta));
    QCOMPARE(nativeImage.pixelColor(2, 0), QColor(Qt::red));
    QCOMPARE(nativeImage.pixelColor(0, 1), QColor(Qt::cyan));
    QCOMPARE(nativeImage.pixelColor(2, 1), QColor(Qt::green));

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QCOMPARE(result->image.size(), QSize(3, 2));
    QVERIFY(!result->errorData.has_value());
}

// TC-ORI-AVIF
// Test purpose: verify that an AVIF rotation item is applied by both the native decoder and the loader.
// Preconditions: macOS Image I/O advertises AVIF support and a temporary directory is writable.
// Input data: a lossless 2x3 AVIF carrying an irot rotation item, encoded from the same color fixture.
// Steps: decode through QVCocoaFunctions, then open the same file through QVImageLoader.
// Expected result: both paths produce the rotated 3x2 image without an unsupported-format error.
// Postcondition: the temporary fixture and all loader resources are released.
void ImageLoaderTests::testImageLoaderAppliesAvifOrientation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("avif"));
    const QString path = createBase64Image(dir, "oriented-avif", "avif", orientedAvifBase64);
    QVERIFY(!path.isEmpty());

    QString nativeError;
    const QImage nativeImage = QVCocoaFunctions::readAdditionalImage(path, &nativeError);
    QVERIFY2(!nativeImage.isNull(), qPrintable(nativeError));
    QCOMPARE(nativeImage.size(), QSize(3, 2));

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QCOMPARE(result->image.size(), QSize(3, 2));
    QVERIFY(!result->errorData.has_value());
}

void ImageLoaderTests::testImageLoaderLoadsAvifWithImageIOFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("avif"));
    const QString path = createBase64Image(dir, "native-avif", "avif", tinyAvifBase64);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QCOMPARE(result->image.size(), QSize(1, 1));
    QVERIFY(!result->errorData.has_value());
}

// TC-IMG-TIFF
// Test purpose: verify TIFF is decoded through the Image I/O path and remains
// available to the normal asynchronous image loader.
// Preconditions: macOS Image I/O advertises public.tiff and the temporary
// directory is writable.
// Input data: a deterministic 4x3 TIFF fixture.
// Steps: read the fixture through the native bridge and QVImageLoader.
// Expected result: Image I/O reports public.tiff; both images are non-empty,
// correctly sized, and carry no loader error.
// Postcondition: temporary TIFF and loader resources are released.
void ImageLoaderTests::testImageLoaderLoadsTiffWithImageIO()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("tiff"));
    const QString path = createTiffImage(dir, "native-tiff");
    QVERIFY(!path.isEmpty());

    const auto nativeResult = QVCocoaFunctions::readImageWithImageIO(path);
    QVERIFY(nativeResult.isImageIOType);
    QCOMPARE(nativeResult.typeIdentifier, QStringLiteral("public.tiff"));
    QVERIFY(!nativeResult.image.isNull());
    QCOMPARE(nativeResult.image.size(), QSize(4, 3));

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QCOMPARE(result->image.size(), QSize(4, 3));
    QVERIFY(!result->errorData.has_value());
}

// TC-RAW-TYPE-DETECTION
// Test purpose: prove RAW classification is based on Image I/O's content UTI,
// not the filename extension used by the caller.
// Preconditions: Image I/O can decode public.tiff; a temporary directory is writable.
// Input data: a valid TIFF copied to a misleading .nef filename.
// Steps: pass the disguised file to readImageWithImageIO and inspect its UTI,
// isRaw flag, and rendered pixels.
// Expected result: the source is identified as public.tiff and isRaw is false;
// the file still renders successfully despite the .nef suffix.
// Postcondition: temporary files and native image resources are released.
void ImageLoaderTests::testImageIOUsesContentTypeInsteadOfFilenameExtension()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString tiffPath = createTiffImage(dir, "content-identified");
    QVERIFY(!tiffPath.isEmpty());
    const QString disguisedPath = dir.filePath("content-identified.nef");
    QVERIFY(QFile::copy(tiffPath, disguisedPath));

    const auto result = QVCocoaFunctions::readImageWithImageIO(disguisedPath);
    QVERIFY(result.isImageIOType);
    QCOMPARE(result.typeIdentifier, QStringLiteral("public.tiff"));
    QVERIFY(!result.isRaw);
    QVERIFY(!result.image.isNull());
    QCOMPARE(result.image.size(), QSize(4, 3));
}

// TC-IMG-FULL-RES
// Test purpose: prove that a source larger than the screen-sized loader hint is
// retained at full resolution so a later zoom can reveal original detail.
// Preconditions: Image I/O supports PNG; a temporary directory is writable;
// the deterministic fixture is larger than the loader's 1920px default hint.
// Input data: a 2400x1600 one-pixel checkerboard PNG.
// Steps: decode through Image I/O with a small hint, then load through the
// asynchronous QVImageLoader using its production default.
// Expected result: intrinsic size and decoded image size remain 2400x1600, and
// alternating source pixels remain distinguishable rather than being decoded
// as a 1920px thumbnail.
// Postcondition: the temporary source and loader resources are released.
void ImageLoaderTests::testImageLoaderPreservesSourceResolutionForZoom()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QSize sourceSize(2400, 1600);
    const QString path = createHighResolutionDetailImage(dir, "full-resolution-detail", sourceSize);
    QVERIFY(!path.isEmpty());

    const auto nativeResult = QVCocoaFunctions::readImageWithImageIO(path);
    QVERIFY(nativeResult.isImageIOType);
    QCOMPARE(nativeResult.intrinsicSize, sourceSize);
    QCOMPARE(nativeResult.image.size(), sourceSize);
    QVERIFY(nativeResult.image.pixelColor(0, 0) != nativeResult.image.pixelColor(1, 0));

    const auto loaderResult = loadImage(path);
    QVERIFY(loaderResult.has_value());
    QVERIFY(!loaderResult->errorData.has_value());
    QCOMPARE(loaderResult->intrinsicSize, sourceSize);
    QCOMPARE(loaderResult->image.size(), sourceSize);
    QVERIFY(loaderResult->image.pixelColor(0, 0) != loaderResult->image.pixelColor(1, 0));
}

void ActionManagerTests::testClonedActionsUntracked()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length() != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(), fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testApplicationIdentity()
{
    QCOMPARE(QCoreApplication::organizationName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("io.github.inostarlin-passion"));
    QCOMPARE(QCoreApplication::applicationName(), QString("Fovelle"));
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.3"));
}

// TC-APP-VERSION
// Test purpose: verify the application reports the released semantic version.
// Preconditions: the QVApplication has been constructed with the CMake version
// definitions.
// Input data: QCoreApplication::applicationVersion().
// Steps: read the runtime application version.
// Expected result: the value is exactly 0.1.3.
// Postcondition: no application or settings state changes.
void FeatureTests::testApplicationVersionIsCurrent()
{
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.3"));
}

// TC-TITLEBAR-APP-ICON
// Test purpose: verify that a newly created image window has no window-level icon.
// Preconditions: the QApplication has been constructed and the bundle is available.
// Input data: a new MainWindow with no document loaded.
// Steps: construct the window and inspect its explicit QWidget icon.
// Expected result: the window icon is null; application/bundle identity is tested separately.
// Postcondition: the window is closed without changing application settings.
void FeatureTests::testWindowIconIsCleared()
{
    QVERIFY(qvApp->windowIcon().isNull());
    MainWindow window;
    QVERIFY(window.windowIcon().isNull());
    window.close();
    QVERIFY(QFile::exists(":/icons/Fovelle.png"));
}

// TC-TITLEBAR-DOCUMENT-ICON
// Test purpose: verify that opening an image does not associate a document path with the native window.
// Preconditions: Cocoa is available, a visible MainWindow can load a temporary PNG, and quit-on-last-window is disabled.
// Input data: one valid 32x32 PNG file.
// Steps: show the window, open the PNG, wait for the pixmap, then inspect QWidget and QWindow path state.
// Expected result: the image loads and the QWidget/QWindow file paths remain empty; the title still contains the filename.
// Postcondition: the temporary file and test window are released and the original quit policy is restored.
void FeatureTests::testTitlebarDocumentProxyIsClearedForLoadedFile()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "titlebar-document", Qt::blue);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QVERIFY(window.windowHandle());

    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    QVERIFY(window.windowIcon().isNull());
    QVERIFY(window.windowFilePath().isEmpty());
    QVERIFY(window.windowHandle()->filePath().isEmpty());
    QVERIFY(window.windowTitle().contains(QFileInfo(imagePath).fileName()));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-TITLEBAR-IDEMPOTENCE
// Test purpose: verify that repeated document transitions and titlebar state changes cannot restore either icon.
// Preconditions: Cocoa is available, a visible MainWindow can load two temporary PNG files, and quit-on-last-window is disabled.
// Input data: two valid PNG files and repeated titlebar hidden/visible transitions.
// Steps: load each file, toggle the titlebar state, and inspect the window icon and native file path after every transition.
// Expected result: every observation has a null window icon and an empty native file path.
// Postcondition: the window is closed and the original quit policy is restored.
void FeatureTests::testTitlebarIconClearingIsIdempotent()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstImagePath = createTestImage(dir, "titlebar-first", Qt::red);
    const QString secondImagePath = createTestImage(dir, "titlebar-second", Qt::green);
    QVERIFY(!firstImagePath.isEmpty());
    QVERIFY(!secondImagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    const auto verifyTitlebarState = [&]() {
        QVERIFY(window.windowIcon().isNull());
        QVERIFY(window.windowFilePath().isEmpty());
        QVERIFY(window.windowHandle());
        QVERIFY(window.windowHandle()->filePath().isEmpty());
    };

    window.openFile(firstImagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    verifyTitlebarState();

    window.setTitlebarHidden(true);
    verifyTitlebarState();
    window.setTitlebarHidden(false);
    verifyTitlebarState();

    window.openFile(secondImagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    verifyTitlebarState();

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void FeatureTests::testSettingsFormatsIncludeNativeImageFormats()
{
    const auto additionalFormats = QVCocoaFunctions::getAdditionalImageFormats();
    QVERIFY(additionalFormats.contains("webp"));
    QVERIFY(additionalFormats.contains("avif"));
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("avifs"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".webp"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".avif"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".avifs"));
}

// TC-FMT-TIFF-RAW
// Test purpose: verify Settings → Formats is sourced from the same Image I/O
// registry as the loader, including TIFF and any RAW types available on this OS.
// Preconditions: QVApplication has initialized its Image I/O-backed extension registry.
// Input data: the current CGImageSource-supported type/tag set and a constructed
// Settings dialog.
// Steps: inspect the registry, the application extension set, and the Formats table.
// Expected result: TIFF aliases are present; every dynamically advertised RAW
// extension is reflected in the application and Settings table without a hardcoded
// camera-model list.
// Postcondition: the Settings dialog is destroyed without changing user settings.
void FeatureTests::testSettingsFormatsIncludeTiffAndSystemRawFormats()
{
    const auto additionalFormats = QVCocoaFunctions::getAdditionalImageFormats();
    QVERIFY(additionalFormats.contains("tif") || additionalFormats.contains("tiff"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".tif"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".tiff"));

    for (const auto &format : additionalFormats)
        QVERIFY(qvApp->getAllFileExtensionList().contains("." + QString::fromUtf8(format)));

    QVOptionsDialog dialog;
    const auto *formatsTable = dialog.findChild<QTableWidget *>("formatsTable");
    QVERIFY(formatsTable);
    QSet<QString> tableExtensions;
    for (int row = 0; row < formatsTable->rowCount(); ++row)
        tableExtensions.insert(formatsTable->item(row, 0)->text());
    QVERIFY(tableExtensions.contains(".tif"));
    QVERIFY(tableExtensions.contains(".tiff"));
}

// TC-IMG-SMALL-SETTING
// Test purpose: verify that the Image settings page exposes and persists the
// new small-image 1:1 option.
// Preconditions: a QVApplication exists and the settings store is writable.
// Input data: the option starts disabled, then the Image-page checkbox is enabled.
// Steps: construct QVOptionsDialog, locate the named checkbox, invoke Apply,
// and read the persisted SettingsManager value.
// Expected result: the checkbox is present, labeled for 1:1 display, and the
// saved setting becomes true.
// Postcondition: ScopedOptionValues restores the user's original setting.
void FeatureTests::testSmallImageOneToOneSettingIsExposedInImageOptions()
{
    ScopedOptionValues options({{"smallimageoneone", false}});

    QVOptionsDialog dialog;
    auto *checkbox = dialog.findChild<QCheckBox *>("smallImagesOneToOneCheckbox");
    QVERIFY(checkbox);
    QVERIFY(checkbox->text().contains(QStringLiteral("1:1")));
    QVERIFY(!checkbox->isChecked());

    auto *buttonBox = dialog.findChild<QDialogButtonBox *>("buttonBox");
    QVERIFY(buttonBox);
    QAbstractButton *applyButton = buttonBox->button(QDialogButtonBox::Apply);
    QVERIFY(applyButton);

    checkbox->setChecked(true);
    QVERIFY(QMetaObject::invokeMethod(
        &dialog,
        "buttonBoxClicked",
        Qt::DirectConnection,
        Q_ARG(QAbstractButton *, applyButton)));
    QVERIFY(qvApp->getSettingsManager().getBoolean("smallimageoneone"));
}

// TC-ISSUE-864-OPENWITH-TEARDOWN
// Test purpose: exercise Open With population while a window is closed, which
// is the shutdown race described by GitHub issue #864.
// Preconditions: Cocoa QPA is active; a visible MainWindow can load a PNG.
// Input data: one deterministic 32x32 PNG and an explicit Open With request.
// Steps: load the PNG, start the Open With worker, close the window, and let its
// destructor run while the worker may still be active.
// Expected result: the test process remains alive and teardown completes within
// the bounded five-second window without a SIGABRT/QPixmap fatal error.
// Postcondition: the stack window and temporary fixture are released.
void FeatureTests::testOpenWithWorkerTeardownContract()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "openwith-teardown", Qt::darkCyan);
    QVERIFY(!imagePath.isEmpty());

    QElapsedTimer teardownTimer;
    teardownTimer.start();
    {
        MainWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.resize(640, 480);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
        window.openFile(imagePath);
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

        window.requestPopulateOpenWithMenu();
        window.close();
    }

    QVERIFY(teardownTimer.elapsed() < 5000);
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void GraphicsViewTests::testMouseWheelUsesOneDiscreteStep()
{
    const qreal factor = QVGraphicsView::wheelZoomFactor(240, 1.25, false);
    QVERIFY(qFuzzyCompare(factor, 1.25));
    QVERIFY(qFuzzyCompare(QVGraphicsView::wheelZoomFactor(-240, 1.25, false), 0.8));
}

void GraphicsViewTests::testTouchpadWheelCanUseFractionalSteps()
{
    const qreal factor = QVGraphicsView::wheelZoomFactor(240, 1.25, true);
    QVERIFY(qFuzzyCompare(factor, 1.5625));
}

// TC-IMAGE-CENTER-WITH-SCROLLBARS
// Test purpose: verify that opening an overflowing image leaves its content
// centered in the visible scene instead of placing it in the lower-right area.
// Preconditions: a visible 640x480 Cocoa window uses OriginalSize mode and
// loads a 1200x900 image, so both AsNeeded scrollbars are required.
// Input data: one deterministic 1200x900 PNG opened at zoom 1.0.
// Steps: open the image, wait for the pixmap item and both scrollbars, and
// compare the scene center of the image with the scene coordinate at the usable
// viewport center.
// Expected result: the scene rectangle follows the current pixmap item on both
// Retina and non-Retina runners, both bars are visible, and the two scene
// centers differ by no more than two scene pixels.
// Postcondition: the window, settings, image, and temporary directory are released.
void GraphicsViewTests::testImageIsCenteredAfterOpeningWithScrollBars()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Expensive)},
        {"scalingtwoenabled", true}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "image-center-scrollbars", Qt::darkCyan, QSize(1200, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible(),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !view->scene()->itemsBoundingRect().isEmpty() &&
            view->scene()->itemsBoundingRect().width() >= 1200,
        3000);
    QCoreApplication::processEvents();

    const QRectF imageSceneRect = view->scene()->itemsBoundingRect();
    QVERIFY(view->sceneRect().contains(imageSceneRect));
    if (window.getViewportPosition().obscuredHeight > 0)
        QVERIFY(view->sceneRect().top() < imageSceneRect.top());
    else
        QCOMPARE(view->sceneRect(), imageSceneRect);
    QRect usableViewportRect = view->viewport()->rect();
    usableViewportRect.setTop(window.getViewportPosition().obscuredHeight);
    const QPointF imageCenterInViewport = view->mapFromScene(imageSceneRect.center());
    qInfo() << "center verification" << imageSceneRect << usableViewportRect
            << imageCenterInViewport << usableViewportRect.center();
    QVERIFY(QLineF(imageCenterInViewport, usableViewportRect.center()).length() <= 2.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-WHEEL-ZOOM-SCROLLBAR-REGRESSION
// Test purpose: verify a configured wheel-to-zoom action remains active when
// both AsNeeded scrollbars are visible, even if macOS/Qt reports the event as
// originating from a TouchPad device.
// Preconditions: a visible 640x480 Cocoa window loads a 1200x900 image at 1:1;
// vertical wheel action is Zoom, horizontal wheel action is None, and both
// scrollbars have non-zero ranges.
// Input data: QWheelEvent(angleDelta=(0,120), pixelDelta=(0,120),
// phase=NoScrollPhase) with a TouchPad device.
// Steps: wait for expensive scaling, center the scrollbars, dispatch the event
// to the viewport, and inspect zoom, event acceptance, and scrollbar state.
// Expected result: the event follows configured Zoom and changes zoom to 1.25;
// it is not diverted to a wheel-based pan bridge by scrollbar visibility.
// Postcondition: the window, settings, temporary device, and image are released.
void GraphicsViewTests::testTouchpadWheelRespectsConfiguredZoomWithScrollBars()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Expensive)},
        {"scalingtwoenabled", true},
        {"viewportverticalscrollaction", static_cast<int>(Qv::ViewportScrollAction::Zoom)},
        {"viewporthorizontalscrollaction", static_cast<int>(Qv::ViewportScrollAction::None)}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "wheel-zoom-scrollbars", Qt::darkCyan, QSize(1200, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible(),
        2000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !view->scene()->itemsBoundingRect().isEmpty() &&
            view->scene()->itemsBoundingRect().width() >= 1200,
        3000);
    view->horizontalScrollBar()->setValue(
        (view->horizontalScrollBar()->minimum() + view->horizontalScrollBar()->maximum()) / 2);
    view->verticalScrollBar()->setValue(
        (view->verticalScrollBar()->minimum() + view->verticalScrollBar()->maximum()) / 2);
    QCoreApplication::processEvents();

    const QPoint wheelPosition = view->viewport()->rect().center();
    const QPoint globalWheelPosition = view->viewport()->mapToGlobal(wheelPosition);
    QPointingDevice touchpad(
        QStringLiteral("discrete wheel device"),
        2,
        QInputDevice::DeviceType::TouchPad,
        QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Scroll,
        5,
        0);
    QWheelEvent wheelEvent(
        QPointF(wheelPosition),
        QPointF(globalWheelPosition),
        QPoint(0, 120),
        QPoint(0, 120),
        Qt::NoButton,
        Qt::NoModifier,
        Qt::NoScrollPhase,
        false,
        Qt::MouseEventNotSynthesized,
        &touchpad);

    QVERIFY(QCoreApplication::sendEvent(view->viewport(), &wheelEvent));
    QVERIFY(wheelEvent.isAccepted());
    QVERIFY(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), 1.25));
    QVERIFY(view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-LAYOUT-OPEN-FIT
// Test purpose: verify that an automatically fitted image is aligned to the
// unobscured viewport, remains fitted after the delayed high-quality pixmap
// replacement, and survives the resulting scrollbar layout pass.
// Preconditions: a visible 640x480 Cocoa window uses ZoomToFit, Never window
// resizing, and Expensive smooth scaling; the fixture has a portrait-ish
// aspect ratio that fits in the viewport without overflow.
// Input data: one deterministic 1200x1000 PNG opened at the default zoom mode.
// Steps: open the image, wait for loading and the delayed scaling timer, then
// inspect both scrollbar visibility and the image center in the viewport.
// Expected result: neither AsNeeded scrollbar is visible, the transformed
// image is no larger than the usable viewport, its top and bottom are inside
// that usable rectangle, and its center is within two viewport pixels of the
// usable viewport center.
// Postcondition: the window, settings, image, and temporary directory are released.
void GraphicsViewTests::testOpeningZoomToFitDoesNotGainScrollBarsAfterExpensiveScaling()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Expensive)},
        {"scalingtwoenabled", true},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "opening-fit", Qt::darkCyan, QSize(1200, 1000));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->getCalculatedZoomMode().has_value() &&
            view->getCalculatedZoomMode().value() == Qv::CalculatedZoomMode::ZoomToFit,
        2000);
    QTest::qWait(150);
    QCoreApplication::processEvents();

    QVERIFY(!view->horizontalScrollBar()->isVisible());
    QVERIFY(!view->verticalScrollBar()->isVisible());
    const QRectF imageRect = view->scene()->itemsBoundingRect();
    const QRect usableViewport = [&]() {
        QRect result = view->viewport()->rect();
        result.setTop(window.getViewportPosition().obscuredHeight);
        return result;
    }();
    const QRect imageRectInViewport = view->mapFromScene(imageRect).boundingRect();
    QVERIFY(imageRectInViewport.width() <= usableViewport.width() + 2);
    QVERIFY(imageRectInViewport.height() <= usableViewport.height() + 2);
    QVERIFY(imageRectInViewport.top() >= usableViewport.top() - 2);
    QVERIFY(imageRectInViewport.bottom() <= usableViewport.bottom() + 2);
    const QPointF imageCenterInViewport = view->mapFromScene(imageRect.center());
    QVERIFY(QLineF(imageCenterInViewport, usableViewport.center()).length() <= 2.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-LAYOUT-ROTATED-FIT
// Test purpose: verify that the titlebar compensation follows the transformed
// scene axis when a fitted image is rotated.
// Preconditions: a visible 640x480 Cocoa window uses ZoomToFit and Never
// window resizing; the fixture has a non-square aspect ratio.
// Input data: rotate the opened 800x600 PNG by 90 degrees.
// Steps: open the image, rotate it, reapply ZoomToFit, and inspect the mapped
// image rectangle and scrollbar state.
// Expected result: the rotated image has no overflow, is contained by the
// unobscured viewport, and is centered within two viewport pixels of it.
// Postcondition: the window, settings, image, and temporary directory are released.
void GraphicsViewTests::testRotatedZoomToFitUsesUnobscuredViewport()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "rotated-fit", Qt::darkMagenta, QSize(800, 600));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->rotateImage(90);
    view->fitOrConstrainImage();
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(
        !view->horizontalScrollBar()->isVisible() && !view->verticalScrollBar()->isVisible(),
        2000);

    QRect usableViewport = view->viewport()->rect();
    usableViewport.setTop(window.getViewportPosition().obscuredHeight);
    const QRect imageRectInViewport = view->mapFromScene(view->scene()->itemsBoundingRect()).boundingRect();
    QVERIFY(imageRectInViewport.top() >= usableViewport.top() - 2);
    QVERIFY(imageRectInViewport.bottom() <= usableViewport.bottom() + 2);
    QVERIFY(QLineF(imageRectInViewport.center(), usableViewport.center()).length() <= 2.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-LAYOUT-ZOOM-SCROLLBAR-THRESHOLD
// Test purpose: verify that zooming across the point where AsNeeded
// scrollbars appear does not change the image-relative point at the viewport
// center.
// Preconditions: a visible 640x480 Cocoa window uses OriginalSize and loads a
// 600x800 image, so the initial image has only vertical overflow and the next
// 1.25x step introduces horizontal overflow.
// Input data: one center-anchored zoom-in step.
// Steps: record the normalized image coordinate at the viewport center, zoom
// in once, inspect the immediate scrollbar layout, then wait for high-quality
// scaling and inspect it again.
// Expected result: both overflow axes are available and the normalized image
// coordinate changes by no more than 0.5% at either transition.
// Postcondition: the window, settings, image, and temporary directory are released.
void GraphicsViewTests::testZoomAcrossScrollbarThresholdKeepsViewportCenterStable()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Expensive)},
        {"scalingtwoenabled", true},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "zoom-scrollbar-threshold", Qt::darkYellow, QSize(600, 800));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        !view->horizontalScrollBar()->isVisible() && !view->verticalScrollBar()->isVisible(),
        2000);
    const auto normalizedImageCoordinateAtViewportCenter = [view]() {
        const QRectF imageRect = view->scene()->itemsBoundingRect();
        const QPointF scenePoint = view->mapToScene(view->viewport()->rect().center());
        return QPointF(
            (scenePoint.x() - imageRect.left()) / imageRect.width(),
            (scenePoint.y() - imageRect.top()) / imageRect.height());
    };
    const QPointF imageCoordinateBefore = normalizedImageCoordinateAtViewportCenter();

    view->zoomIn();
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible(),
        2000);
    QCoreApplication::processEvents();

    const QPointF imageCoordinateAfterLayout = normalizedImageCoordinateAtViewportCenter();
    QVERIFY(QLineF(imageCoordinateBefore, imageCoordinateAfterLayout).length() <= 0.005);
    QTest::qWait(150);
    QCoreApplication::processEvents();
    const QPointF imageCoordinateAfterScaling = normalizedImageCoordinateAtViewportCenter();
    QVERIFY(QLineF(imageCoordinateAfterLayout, imageCoordinateAfterScaling).length() <= 0.005);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-GESTURE-TOUCHPAD-PAN
// Test purpose: verify that a two-finger touchpad scroll stream is interpreted
// as pixel panning rather than as a configured wheel-to-zoom action.
// Preconditions: a visible 640x480 Cocoa window uses OriginalSize, both scroll
// actions are configured as Zoom, and a 1600x900 image has both overflow axes.
// Input data: TouchPad QWheelEvents with ScrollBegin, one ScrollUpdate carrying
// pixelDelta=(80,60), and ScrollEnd.
// Steps: center both scrollbars, dispatch the synthetic phased stream, and
// inspect acceptance, scrollbar movement, and zoom.
// Expected result: the stream is accepted, both scrollbar values change within
// range, and zoom remains exactly 1.0; no pinch/zoom side effect occurs.
// Postcondition: the window, settings, device, image, and temporary directory are released.
void GraphicsViewTests::testTouchpadPanUsesPixelsWithoutChangingZoom()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"onetoonepixelsizing", false},
        {"viewportverticalscrollaction", static_cast<int>(Qv::ViewportScrollAction::Zoom)},
        {"viewporthorizontalscrollaction", static_cast<int>(Qv::ViewportScrollAction::Zoom)}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "touchpad-pan", Qt::darkGreen, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible(),
        2000);
    view->horizontalScrollBar()->setValue(
        (view->horizontalScrollBar()->minimum() + view->horizontalScrollBar()->maximum()) / 2);
    view->verticalScrollBar()->setValue(
        (view->verticalScrollBar()->minimum() + view->verticalScrollBar()->maximum()) / 2);
    QCoreApplication::processEvents();

    const QPoint position = view->viewport()->rect().center();
    const QPoint globalPosition = view->viewport()->mapToGlobal(position);
    QPointingDevice touchpad(
        QStringLiteral("phased touchpad"),
        3,
        QInputDevice::DeviceType::TouchPad,
        QPointingDevice::PointerType::Finger,
        QInputDevice::Capability::Scroll,
        5,
        0);
    const auto sendWheel = [&](const Qt::ScrollPhase phase, const QPoint pixelDelta, const QPoint angleDelta) {
        QWheelEvent event(
            QPointF(position),
            QPointF(globalPosition),
            pixelDelta,
            angleDelta,
            Qt::NoButton,
            Qt::NoModifier,
            phase,
            false,
            Qt::MouseEventNotSynthesized,
            &touchpad);
        return QCoreApplication::sendEvent(view->viewport(), &event) && event.isAccepted();
    };

    const int horizontalBefore = view->horizontalScrollBar()->value();
    const int verticalBefore = view->verticalScrollBar()->value();
    const qreal zoomBefore = view->getZoomLevel();
    QVERIFY(sendWheel(Qt::ScrollBegin, {}, {}));
    QVERIFY(sendWheel(Qt::ScrollUpdate, QPoint(80, 60), QPoint(80, 60)));
    QVERIFY(sendWheel(Qt::ScrollEnd, {}, {}));

    QVERIFY(view->horizontalScrollBar()->value() != horizontalBefore);
    QVERIFY(view->verticalScrollBar()->value() != verticalBefore);
    QVERIFY(view->horizontalScrollBar()->value() >= view->horizontalScrollBar()->minimum());
    QVERIFY(view->horizontalScrollBar()->value() <= view->horizontalScrollBar()->maximum());
    QVERIFY(view->verticalScrollBar()->value() >= view->verticalScrollBar()->minimum());
    QVERIFY(view->verticalScrollBar()->value() <= view->verticalScrollBar()->maximum());
    QVERIFY(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), zoomBefore));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-ZOOM-FULLSCREEN
// Test purpose: verify that returning to the fit ratio restores fit intent and that a fullscreen resize recalculates it.
// Preconditions: a visible non-fullscreen MainWindow can load a writable 1600x900 PNG fixture.
// Input data: one discrete zoom-in step, one inverse zoom-out step, then a fullscreen enter/exit transition.
// Steps: load the fixture, force ZoomToFit, apply the inverse wheel-equivalent steps, and toggle fullscreen twice.
// Expected result: ZoomToFit remains active; a changed fullscreen viewport receives a recalculated zoom level;
// after exit, ZoomToFit remains active and the transition completes within the bounded timeout.
// Postcondition: the window closes and the application quit policy is restored.
void GraphicsViewTests::testFitZoomSurvivesInverseWheelStepsAndFullscreenResize()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "fullscreen-fit", Qt::darkBlue, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    window.resize(640, 480);
    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    QCoreApplication::processEvents();
    QVERIFY(view->getCalculatedZoomMode().has_value());
    QCOMPARE(view->getCalculatedZoomMode().value(), Qv::CalculatedZoomMode::ZoomToFit);

    const qreal fitZoomBeforeWheel = view->getZoomLevel();
    const QPoint wheelPos = view->viewport()->rect().center();
    const QPoint globalWheelPos = view->viewport()->mapToGlobal(wheelPos);
    const auto sendMouseWheel = [&](const int delta) {
        QWheelEvent wheelEvent(
            QPointF(wheelPos),
            QPointF(globalWheelPos),
            QPoint(),
            QPoint(0, delta),
            Qt::NoButton,
            Qt::NoModifier,
            Qt::NoScrollPhase,
            false);
        QApplication::sendEvent(view->viewport(), &wheelEvent);
    };
    sendMouseWheel(120);
    sendMouseWheel(-120);
    QVERIFY(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), fitZoomBeforeWheel));
    QVERIFY(view->getCalculatedZoomMode().has_value());
    QCOMPARE(view->getCalculatedZoomMode().value(), Qv::CalculatedZoomMode::ZoomToFit);

    const QSize normalViewport = view->viewport()->size();
    const qreal zoomBeforeFullscreen = view->getZoomLevel();
    QElapsedTimer transitionTimer;
    transitionTimer.start();
    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 5000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTRY_VERIFY_WITH_TIMEOUT(
        view->getCalculatedZoomMode().has_value() &&
            view->getCalculatedZoomMode().value() == Qv::CalculatedZoomMode::ZoomToFit,
        5000);

    const QSize fullscreenViewport = view->viewport()->size();
    if (fullscreenViewport != normalViewport)
        QVERIFY(!QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), zoomBeforeFullscreen));

    transitionTimer.restart();
    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    QTRY_VERIFY_WITH_TIMEOUT(
        view->getCalculatedZoomMode().has_value() &&
            view->getCalculatedZoomMode().value() == Qv::CalculatedZoomMode::ZoomToFit,
        5000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-ZOOM-MANUAL-RESIZE
// Test purpose: verify that a genuine manual zoom remains manual when the viewport is resized.
// Preconditions: a visible non-fullscreen MainWindow can load a writable 1600x900 PNG fixture.
// Input data: one zoom-in step followed by a non-fullscreen window resize.
// Steps: load the fixture, establish ZoomToFit, zoom in once, resize the window, and inspect mode/level.
// Expected result: the calculated zoom mode stays unset and the manually selected level is preserved.
// Postcondition: the window closes and the application quit policy is restored.
void GraphicsViewTests::testManualZoomRemainsManualAcrossResize()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "manual-resize", Qt::darkRed, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    window.resize(640, 480);
    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    QCoreApplication::processEvents();
    window.zoomIn();
    const qreal manualZoom = view->getZoomLevel();
    QVERIFY(!view->getCalculatedZoomMode().has_value());

    window.resize(1000, 800);
    QCoreApplication::processEvents();
    QVERIFY(!view->getCalculatedZoomMode().has_value());
    QVERIFY(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), manualZoom));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-IMG-SMALL-POLICY
// Test purpose: verify the small-image decision uses the real display area and
// is gated by both the checkbox and Window matches image size = Never.
// Preconditions: none beyond the pure policy helper being linked.
// Input data: a 100x80 image, strict boundary sizes, disabled setting, and a
// non-Never window resize mode.
// Steps: evaluate the helper for a fitting viewport, equal-width boundary,
// oversized image, disabled option, and WhenLaunching mode.
// Expected result: only the strictly smaller image with the option enabled and
// Never mode requests 1:1 display.
// Postcondition: no application or persistent setting state is changed.
void GraphicsViewTests::testSmallImageOneToOnePolicyUsesViewportAndWindowMode()
{
    const QSizeF smallImage(100, 80);
    const QSize fittingViewport(120, 100);

    QVERIFY(QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        smallImage, fittingViewport, true, Qv::WindowResizeMode::Never));
    QVERIFY(!QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        smallImage, QSize(100, 100), true, Qv::WindowResizeMode::Never));
    QVERIFY(!QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        smallImage, QSize(99, 100), true, Qv::WindowResizeMode::Never));
    QVERIFY(!QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        QSizeF(121, 80), fittingViewport, true, Qv::WindowResizeMode::Never));
    QVERIFY(!QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        smallImage, fittingViewport, false, Qv::WindowResizeMode::Never));
    QVERIFY(!QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
        smallImage, fittingViewport, true, Qv::WindowResizeMode::WhenLaunching));
}

// TC-IMG-SMALL-OPEN-BROWSE
// Test purpose: verify the enabled option produces 1:1 zoom both when a small
// image is opened directly and when the next image is browsed.
// Preconditions: Window matches image size is Never, the option is enabled,
// an automatic zoom mode and a visible 640x480 window are available.
// Input data: two deterministic small PNGs in one folder, opened in sequence.
// Steps: open the first file and observe zoom; navigate to the next file and
// observe the new file and zoom after asynchronous loading completes.
// Expected result: both images use zoom level 1.0; switching the automatic mode
// to FillWindow does not upscale an eligible small image either.
// Postcondition: the window closes and ScopedOptionValues restores settings.
void GraphicsViewTests::testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages()
{
    ScopedOptionValues options({
        {"smallimageoneone", true},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString openedImagePath = createTestImage(dir, "01-open-small", Qt::darkYellow, QSize(32, 24));
    const QString browsedImagePath = createTestImage(dir, "02-browse-small", Qt::darkMagenta, QSize(48, 36));
    QVERIFY(!openedImagePath.isEmpty());
    QVERIFY(!browsedImagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    window.openFile(openedImagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->getCalculatedZoomMode().has_value() &&
            view->getCalculatedZoomMode().value() == Qv::CalculatedZoomMode::ZoomToFit &&
            QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), 1.0),
        5000);
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), QFileInfo(openedImagePath).absoluteFilePath());

    view->goToFile(Qv::GoToFileMode::Next);
    QTRY_VERIFY_WITH_TIMEOUT(
        window.getCurrentFileDetails().fileInfo.absoluteFilePath() == QFileInfo(browsedImagePath).absoluteFilePath() &&
            window.getIsPixmapLoaded(),
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), 1.0), 5000);

    view->setCalculatedZoomMode(Qv::CalculatedZoomMode::FillWindow);
    QTRY_VERIFY_WITH_TIMEOUT(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), 1.0), 5000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-GESTURE-NATIVE-ZOOM
// Test purpose: verify Apple native pinch zoom changes the zoom ratio and keeps
// the scene point under the two-finger gesture position stable.
// Preconditions: a visible 640x480 Cocoa window loads a 1600x900 image in
// OriginalSize mode, and the native gesture receiver is the viewport.
// Input data: BeginNativeGesture, ZoomNativeGesture(value=0.25) at the viewport
// center, and EndNativeGesture.
// Steps: record the zoom and scene point, dispatch the three native events, and
// compare the resulting zoom and scene point.
// Expected result: every event is accepted, zoom becomes 125%, and the scene
// point moves by no more than two scene pixels.
// Postcondition: the window and temporary image are released.
void GraphicsViewTests::testNativePinchZoomChangesScaleAtGesturePosition()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "native-pinch", Qt::darkCyan, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QCoreApplication::processEvents();

    const QPoint gesturePosition = view->viewport()->rect().center();
    const QPointF scenePointBefore = view->mapToScene(gesturePosition);
    const qreal zoomBefore = view->getZoomLevel();
    QVERIFY(sendNativeGesture(view, Qt::BeginNativeGesture, gesturePosition));
    QVERIFY(sendNativeGesture(view, Qt::ZoomNativeGesture, gesturePosition, 0.25));
    QVERIFY(sendNativeGesture(view, Qt::EndNativeGesture, gesturePosition));

    QCOMPARE(view->getZoomLevel(), zoomBefore * QVGraphicsView::nativeGestureZoomFactor(0.25));
    const QPointF scenePointAfter = view->mapToScene(gesturePosition);
    QVERIFY(QLineF(scenePointBefore, scenePointAfter).length() <= 2.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-GESTURE-NATIVE-PAN
// Test purpose: verify Apple native pan changes the visible viewport in both
// axes while preserving the image zoom ratio.
// Preconditions: a visible 640x480 Cocoa window loads a 1600x900 image at 1:1,
// so both scroll axes have a non-zero range.
// Input data: BeginNativeGesture, PanNativeGesture(delta=(80,60)), and
// EndNativeGesture.
// Steps: center both scrollbars, dispatch the native event stream, and inspect
// the scrollbar values and zoom level.
// Expected result: the event stream is accepted, both scrollbar values change
// within their ranges, and zoom remains unchanged.
// Postcondition: the window and temporary image are released.
void GraphicsViewTests::testNativePanChangesViewport()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "native-pan", Qt::darkYellow, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    view->horizontalScrollBar()->setValue(
        (view->horizontalScrollBar()->minimum() + view->horizontalScrollBar()->maximum()) / 2);
    view->verticalScrollBar()->setValue(
        (view->verticalScrollBar()->minimum() + view->verticalScrollBar()->maximum()) / 2);
    QCoreApplication::processEvents();

    const QPoint gesturePosition = view->viewport()->rect().center();
    const int horizontalBefore = view->horizontalScrollBar()->value();
    const int verticalBefore = view->verticalScrollBar()->value();
    const qreal zoomBefore = view->getZoomLevel();
    QVERIFY(sendNativeGesture(view, Qt::BeginNativeGesture, gesturePosition));
    QVERIFY(sendNativeGesture(view, Qt::PanNativeGesture, gesturePosition, 0.0, QPointF(80.0, 60.0)));
    QVERIFY(sendNativeGesture(view, Qt::EndNativeGesture, gesturePosition));

    QVERIFY(view->horizontalScrollBar()->value() != horizontalBefore);
    QVERIFY(view->verticalScrollBar()->value() != verticalBefore);
    QVERIFY(view->horizontalScrollBar()->value() >= view->horizontalScrollBar()->minimum());
    QVERIFY(view->horizontalScrollBar()->value() <= view->horizontalScrollBar()->maximum());
    QVERIFY(view->verticalScrollBar()->value() >= view->verticalScrollBar()->minimum());
    QVERIFY(view->verticalScrollBar()->value() <= view->verticalScrollBar()->maximum());
    QVERIFY(QVGraphicsView::zoomLevelsEquivalent(view->getZoomLevel(), zoomBefore));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SCROLLBAR-AXES
// Test purpose: verify each scroll axis is visible exactly when the transformed
// image exceeds the corresponding viewport dimension, and is placed at the
// right/bottom edge by QAbstractScrollArea.
// Preconditions: WindowResizeMode is Never and OriginalSize is active so the
// fixture dimensions are not automatically fitted away.
// Input data: 100x100, 1600x100, 100x1600, and 1600x1600 PNG fixtures.
// Steps: open each fixture in a 640x480 window, wait for the view to settle, and
// inspect both policies, visibility flags, and edge geometry.
// Expected result: none, horizontal-only, vertical-only, and both axes are
// observed respectively; visible bars touch the required window edges.
// Postcondition: every window and fixture is released.
void GraphicsViewTests::testScrollBarsFollowImageOverflowAxes()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const auto checkCase = [&](const QString &name, const QSize imageSize, const bool horizontal, const bool vertical) {
        const QString imagePath = createTestImage(dir, name, Qt::darkBlue, imageSize);
        QVERIFY(!imagePath.isEmpty());

        MainWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.resize(640, 480);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
        window.openFile(imagePath);
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
        auto *view = window.findChild<QVGraphicsView *>();
        QVERIFY(view);
        view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
        QTRY_VERIFY_WITH_TIMEOUT(
            view->horizontalScrollBar()->isVisible() == horizontal &&
                view->verticalScrollBar()->isVisible() == vertical,
            2000);
        QCOMPARE(view->horizontalScrollBarPolicy(), Qt::ScrollBarAsNeeded);
        QCOMPARE(view->verticalScrollBarPolicy(), Qt::ScrollBarAsNeeded);

        if (horizontal)
        {
            const QPoint barBottomRight = view->mapFromGlobal(
                view->horizontalScrollBar()->mapToGlobal(view->horizontalScrollBar()->rect().bottomRight()));
            QCOMPARE(barBottomRight.y(), view->rect().bottom());
        }
        else
            QVERIFY(!view->horizontalScrollBar()->isVisible());
        if (vertical)
        {
            const QPoint barBottomRight = view->mapFromGlobal(
                view->verticalScrollBar()->mapToGlobal(view->verticalScrollBar()->rect().bottomRight()));
            QCOMPARE(barBottomRight.x(), view->rect().right());
        }
        else
            QVERIFY(!view->verticalScrollBar()->isVisible());

        window.close();
    };

    checkCase("scroll-none", QSize(100, 100), false, false);
    checkCase("scroll-horizontal", QSize(1600, 100), true, false);
    checkCase("scroll-vertical", QSize(100, 1600), false, true);
    checkCase("scroll-both", QSize(1600, 1600), true, true);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SCROLLBAR-THEME
// Test purpose: verify the scrollbar handle and track stylesheet follows the
// persisted Light/Dark Theme and updates live without reopening the image.
// Preconditions: a visible window has a horizontally overflowing image.
// Input data: Light Theme followed by Dark Theme through SettingsManager.
// Steps: inspect both scrollbar styles under Light, write Dark to QSettings,
// reload settings, and inspect the same widgets again.
// Expected result: the Light and Dark styles are exact theme-specific contracts,
// including distinct track and handle colors, and the dynamic theme property
// changes on both bars.
// Postcondition: the original Theme setting and window are restored.
void GraphicsViewTests::testScrollBarsMatchTheme()
{
    ScopedOptionValues options({
        {"theme", static_cast<int>(Qv::Theme::Light)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "scroll-theme", Qt::darkGreen, QSize(1600, 100));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(view->horizontalScrollBar()->isVisible(), 2000);

    const auto *horizontal = view->horizontalScrollBar();
    const auto *vertical = view->verticalScrollBar();
    QCOMPARE(horizontal->styleSheet(), QVGraphicsView::scrollBarStyleSheet(Qv::Theme::Light));
    QCOMPARE(vertical->styleSheet(), QVGraphicsView::scrollBarStyleSheet(Qv::Theme::Light));
    QCOMPARE(horizontal->property("scrollBarTheme").toInt(), static_cast<int>(Qv::Theme::Light));
    QVERIFY(horizontal->styleSheet().contains("QScrollBar::handle"));
    QVERIFY(horizontal->styleSheet().contains("QScrollBar::add-page"));

    QSettings settings;
    settings.setValue("options/theme", static_cast<int>(Qv::Theme::Dark));
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
    QTRY_COMPARE_WITH_TIMEOUT(horizontal->styleSheet(), QVGraphicsView::scrollBarStyleSheet(Qv::Theme::Dark), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(vertical->styleSheet(), QVGraphicsView::scrollBarStyleSheet(Qv::Theme::Dark), 2000);
    QCOMPARE(horizontal->property("scrollBarTheme").toInt(), static_cast<int>(Qv::Theme::Dark));
    QVERIFY(horizontal->styleSheet().contains("#2f2f2f"));
    QVERIFY(horizontal->styleSheet().contains("#6b6b6b"));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-GESTURE-PERF
// Test purpose: measure native gesture-stream response under the target
// workload and enforce the time-behavior contract.
// Preconditions: a 1600x900 image is loaded at 1:1 with both scroll axes
// available; the test runs on the Cocoa event loop.
// Input data: 240 accepted PanNativeGesture events with 1-pixel alternating
// deltas, bracketed by Begin/End events.
// Steps: time each event dispatch, calculate average, P99, maximum, and stream
// throughput, and compare them with the documented budgets.
// Expected result: average <=16.67ms, P99 <=33.33ms, maximum <=50ms, and
// throughput >=60 accepted events/second.
// Postcondition: the window, fixture, and temporary event state are released.
void GraphicsViewTests::testNativeGestureResponsePerformance()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "native-gesture-performance", Qt::darkMagenta, QSize(1600, 900));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QCoreApplication::processEvents();

    constexpr int eventCount = 240;
    const QPoint gesturePosition = view->viewport()->rect().center();
    QVector<double> samples;
    samples.reserve(eventCount);
    QVERIFY(sendNativeGesture(view, Qt::BeginNativeGesture, gesturePosition));
    QElapsedTimer streamTimer;
    streamTimer.start();
    for (int i = 0; i < eventCount; ++i)
    {
        QElapsedTimer eventTimer;
        eventTimer.start();
        QVERIFY(sendNativeGesture(
            view,
            Qt::PanNativeGesture,
            gesturePosition,
            0.0,
            QPointF(i % 2 == 0 ? 1.0 : -1.0, i % 3 == 0 ? 1.0 : -1.0)));
        samples.append(eventTimer.nsecsElapsed() / 1000000.0);
    }
    const double streamMilliseconds = streamTimer.nsecsElapsed() / 1000000.0;
    QVERIFY(sendNativeGesture(view, Qt::EndNativeGesture, gesturePosition));

    std::sort(samples.begin(), samples.end());
    const double averageMilliseconds = std::accumulate(samples.cbegin(), samples.cend(), 0.0) / samples.size();
    const int p99Index = qMax(0, static_cast<int>(qCeil(samples.size() * 0.99)) - 1);
    const double p99Milliseconds = samples.at(p99Index);
    const double maximumMilliseconds = samples.constLast();
    const double throughput = eventCount / (streamMilliseconds / 1000.0);
    qInfo().noquote() << QStringLiteral(
        "GESTURE_PERF average_ms=%1 p99_ms=%2 max_ms=%3 throughput_events_per_second=%4 count=%5")
        .arg(averageMilliseconds, 0, 'f', 3)
        .arg(p99Milliseconds, 0, 'f', 3)
        .arg(maximumMilliseconds, 0, 'f', 3)
        .arg(throughput, 0, 'f', 3)
        .arg(eventCount);
    QVERIFY(averageMilliseconds <= 16.67);
    QVERIFY(p99Milliseconds <= 33.33);
    QVERIFY(maximumMilliseconds <= 50.0);
    QVERIFY(throughput >= 60.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testAboutDialogIdentity()
{
    QVAboutDialog dialog;
    const auto *logoLabel = dialog.findChild<QLabel *>("logoLabel");
    const auto *subtitleLabel = dialog.findChild<QLabel *>("subtitleLabel");
    const auto *infoLabel = dialog.findChild<QLabel *>("infoLabel2");

    QVERIFY(logoLabel);
    QVERIFY(subtitleLabel);
    QVERIFY(infoLabel);
    QCOMPARE(dialog.windowTitle(), QString("About Fovelle"));
    QCOMPARE(logoLabel->text(), QString("Fovelle"));
    QCOMPARE(subtitleLabel->text(), QString("version 0.1.3"));

    const QString visibleText = QTextDocumentFragment::fromHtml(infoLabel->text()).toPlainText();
    const QString expectedText =
        "Based on qView\n"
        "Copyright © 2018–2025 jurplel and qView contributors\n"
        "Fovelle modifications © 2026 Fovelle contributors\n"
        "Includes portions of commits from jdpurcell/qView by jdpurcell\n\n"
        "Licensed under GPLv3";
    QCOMPARE(visibleText, expectedText);
    QVERIFY(infoLabel->text().contains("https://github.com/inostarlin-passion/Fovelle"));
    QVERIFY(infoLabel->text().contains("https://github.com/jdpurcell/qView"));
    QVERIFY(!infoLabel->text().contains("interversehq.com"));
}

void ActionManagerTests::testWindowTitleIdentity()
{
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.buildWindowTitle();
    QCOMPARE(window.windowTitle(), QString("Fovelle"));
    window.close();
}

void ActionManagerTests::testLastWindowClosedPolicy()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
    QVERIFY(qvApp->quitOnLastWindowClosed());
}

// TC-FS-01
// Test purpose: verify that the Return/Enter key enters full screen from a loaded image.
// Preconditions: an active, visible non-full-screen MainWindow with a loaded PNG.
// Input data: Qt::Key_Return.
// Steps: send Return to the window and wait for the native state transition.
// Expected result: the window becomes full screen within the bounded timeout.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testReturnKeyEntersFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "return-fullscreen", Qt::darkBlue);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(!window.isFullScreen());

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    transitionTimer.restart();
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-02
// Test purpose: verify that the numeric keypad Enter key has the same entry behavior.
// Preconditions: an active, visible non-full-screen MainWindow with a loaded PNG.
// Input data: Qt::Key_Enter.
// Steps: send keypad Enter to the window and wait for the native state transition.
// Expected result: the window becomes full screen within the bounded timeout.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testKeypadEnterEntersFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "keypad-fullscreen", Qt::darkCyan);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(!window.isFullScreen());

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Enter);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    transitionTimer.restart();
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-03
// Test purpose: verify that Enter is an entry-only shortcut and is idempotent in full screen.
// Preconditions: an active, visible MainWindow already in full screen.
// Input data: Qt::Key_Return followed by Qt::Key_Enter.
// Steps: send both Enter variants while full screen and process the event loop.
// Expected result: the window remains full screen and is not toggled out.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testEnterDoesNotExitFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.showFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::qWait(2000);

    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_Enter);
    QTest::qWait(100);
    QVERIFY(window.isFullScreen());

    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testEscapeExitsFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.showFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::qWait(2000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    QVERIFY(window.isVisible());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-04
// Test purpose: verify the Esc regression path restores the loaded image and normal geometry.
// Preconditions: a visible non-full-screen MainWindow displaying a PNG.
// Input data: Return to enter full screen, then Escape to leave it.
// Steps: capture the normal geometry, enter full screen, send Escape, and wait for completion.
// Expected result: the image remains loaded, the window is visible and normal, and its
// geometry returns to the captured normal geometry without an extra state request.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testEscapeRestoresLoadedImageWithoutGeometryJump()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "escape-restore", Qt::darkGreen);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    window.setGeometry(QRect(200, 200, 640, 480));
    QCoreApplication::processEvents();
    const QRect normalGeometry = window.geometry();
    const QString loadedPath = window.getCurrentFileDetails().fileInfo.absoluteFilePath();

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    transitionTimer.restart();
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen() && window.geometry() == normalGeometry, 3000);
    QVERIFY(window.isVisible());
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), loadedPath);
    QCOMPARE(window.geometry(), normalGeometry);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testEscapeClosesWindow()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ApplicationEventTests::testFileOpenEventIsDeferredAndLoadsImage()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = createTestImage(dir, "launch-services", Qt::darkGreen);
    QVERIFY(!path.isEmpty());

    QSettings settings;
    const bool hadFirstLaunch = settings.contains("firstlaunch");
    const QVariant oldFirstLaunch = settings.value("firstlaunch");
    settings.setValue("firstlaunch", true);

    auto *window = QVApplication::newWindow();
    QCoreApplication::processEvents();

    QFileOpenEvent openEvent(QUrl::fromLocalFile(path));
    QVERIFY(qvApp->event(&openEvent));
    QVERIFY(qvApp->hasPendingFileOpenEvents());
    QVERIFY(!window->hasFileOrPendingLoad());

    QTRY_VERIFY_WITH_TIMEOUT(!qvApp->hasPendingFileOpenEvents(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(window->getIsPixmapLoaded(), 5000);
    QCOMPARE(window->getCurrentFileDetails().fileInfo.absoluteFilePath(), QFileInfo(path).absoluteFilePath());

    window->close();
    if (hadFirstLaunch)
        settings.setValue("firstlaunch", oldFirstLaunch);
    else
        settings.remove("firstlaunch");
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ApplicationEventTests::testFileOpenEventWithoutPathIsIgnored()
{
    QVERIFY(!qvApp->hasPendingFileOpenEvents());

    QFileOpenEvent openEvent(QString{});
    QVERIFY(qvApp->event(&openEvent));
    QVERIFY(!qvApp->hasPendingFileOpenEvents());
}

void ImageCoreAndMovieTests::testColorSpaceConversion()
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setColorSpace(QColorSpace::SRgb);

    TestableImageCore::handleColorSpaceConversion(image, QColorSpace::DisplayP3);

    QCOMPARE(image.colorSpace(), QColorSpace::DisplayP3);
}

void ImageCoreAndMovieTests::testMovieSpeedAndSingleFrameRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "single-frame", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVMovie movie(path);
    QVERIFY(movie.isValid());
    QCOMPARE(movie.frameCount(), 1);
    movie.setSpeed(50);
    QCOMPARE(movie.speed(), 50);
}

// TC-APNG-PLAY
// Test purpose: verify that the supplied APNG is decoded as an animation and
// that playback can advance beyond the first composited frame.
// Preconditions: macOS Image I/O animation decoding is available; an explicit
// FOVELLE_APNG_FIXTURE may optionally be provided.
// Input data: the configured external APNG, otherwise the embedded two-frame
// fixture used to keep CI independent from developer-machine paths.
// Steps: choose the deterministic fixture, construct QVMovie, inspect its
// frame/loop metadata, jump to frame 0 and the first later frame that differs,
// and compare image/delay metadata.
// Expected result: at least two frames are reported, the loop is infinite, a
// later frame is reachable and differs from frame 0, and its finite delay is
// retained; a supplied real sample is checked through the same multi-frame path.
// Postcondition: the movie and fallback temporary directory are destroyed
// without leaving an active timer.
void ImageCoreAndMovieTests::testAnimatedPngPlaysBeyondFirstFrame()
{
    const QString configuredPath = qEnvironmentVariable("FOVELLE_APNG_FIXTURE");
    QTemporaryDir fallbackDirectory;
    QString path = configuredPath;
    const bool usingEmbeddedFixture = path.isEmpty();
    if (path.isEmpty())
    {
        QVERIFY(fallbackDirectory.isValid());
        path = createBase64Image(fallbackDirectory, "embedded-animated", "png", tinyAnimatedPngBase64);
    }
    QVERIFY2(QFileInfo::exists(path), qPrintable(QStringLiteral("APNG fixture is missing: %1").arg(path)));
    const bool isReferenceSample = QFileInfo(path).fileName() == QStringLiteral("587991672-4ed6af9e-f29e-44d2-ba55-07423ba5b91b.png");

    QVMovie movie(path);
    movie.setCacheMode(QVMovie::CacheAll);
    QVERIFY(movie.isValid());
    QVERIFY(movie.frameCount() >= 2);
    if (isReferenceSample)
        QCOMPARE(movie.frameCount(), 686);
    else if (usingEmbeddedFixture)
        QCOMPARE(movie.frameCount(), 2);
    QCOMPARE(movie.loopCount(), -1);

    QVERIFY(movie.jumpToFrame(0));
    const QImage firstFrame = movie.currentImage();
    QVERIFY(!firstFrame.isNull());
    if (isReferenceSample)
        QCOMPARE(firstFrame.size(), QSize(240, 160));
    else if (usingEmbeddedFixture)
        QCOMPARE(firstFrame.size(), QSize(2, 2));

    int differingFrame = -1;
    QImage secondFrame;
    for (int frameNumber = 1; frameNumber < movie.frameCount(); ++frameNumber)
    {
        QVERIFY(movie.jumpToFrame(frameNumber));
        secondFrame = movie.currentImage();
        if (firstFrame != secondFrame)
        {
            differingFrame = frameNumber;
            break;
        }
    }
    QVERIFY(differingFrame > 0);
    QVERIFY(!secondFrame.isNull());
    if (isReferenceSample)
        QCOMPARE(secondFrame.size(), QSize(240, 160));
    else if (usingEmbeddedFixture)
        QCOMPARE(secondFrame.size(), QSize(2, 2));
    QCOMPARE(movie.currentFrameNumber(), differingFrame);
    QVERIFY(movie.nextFrameDelay() > 0);

    QVMovie playbackMovie(path);
    playbackMovie.setCacheMode(QVMovie::CacheAll);
    playbackMovie.setSpeed(1000);
    QSignalSpy frameChangedSpy(&playbackMovie, &QVMovie::frameChanged);
    playbackMovie.start();
    QTRY_VERIFY_WITH_TIMEOUT(frameChangedSpy.count() >= 2, 1000);
    bool observedLaterFrame = false;
    for (const auto &arguments : frameChangedSpy)
    {
        if (!arguments.isEmpty() && arguments.at(0).toInt() > 0)
        {
            observedLaterFrame = true;
            break;
        }
    }
    QVERIFY(observedLaterFrame);
    playbackMovie.stop();
}

// TC-TITLE-DEFAULT
// Test purpose: verify the default Settings → Window → Titlebar text value.
// Preconditions: SettingsManager has initialized its default-value library.
// Input data: the titlebarmode setting requested with defaults=true.
// Steps: read the default enum without mutating the user's stored setting.
// Expected result: the default is Qv::TitleBarText::Practical.
// Postcondition: no setting or window state is changed.
void WindowBehaviorTests::testDefaultTitlebarTextIsPractical()
{
    QCOMPARE(
        qvApp->getSettingsManager().getEnum<Qv::TitleBarText>("titlebarmode", true),
        Qv::TitleBarText::Practical);
}

// TC-FS-DEFAULT
// Test purpose: verify that the Full Screen action owns Enter as its default
// shortcut and that the value is exposed through the normal shortcut settings.
// Preconditions: the Qt application and ShortcutManager are initialized.
// Input data: the Full Screen action key and a Return key sequence.
// Steps: set the stored Full Screen shortcut to Return, refresh the manager,
// then inspect both the shortcut record and the QAction.
// Expected result: the default and active shortcut are Return, with no hidden
// second binding added by MainWindow.
// Postcondition: the original shortcut setting and action state are restored.
void WindowBehaviorTests::testFullscreenDefaultShortcutIsEnterAndConfigurable()
{
    const QString returnKey = QKeySequence(Qt::Key_Return).toString();
    ScopedShortcutValues shortcuts({{"fullscreen", QStringList {returnKey}}});

    bool foundFullscreenShortcut = false;
    for (const auto &shortcut : qvApp->getShortcutManager().getShortcutsList())
    {
        if (shortcut.name == QStringLiteral("fullscreen"))
        {
            foundFullscreenShortcut = true;
            QCOMPARE(shortcut.defaultShortcuts, QStringList {returnKey});
            QCOMPARE(shortcut.shortcuts, QStringList {returnKey});
            break;
        }
    }
    QVERIFY(foundFullscreenShortcut);

    const QAction *fullscreenAction = qvApp->getActionManager().getAction("fullscreen");
    QVERIFY(fullscreenAction);
    const QList<QKeySequence> expectedShortcuts {QKeySequence(Qt::Key_Return)};
    QCOMPARE(fullscreenAction->shortcuts(), expectedShortcuts);
}

// TC-FS-NO-BYPASS
// Test purpose: prove that removing Enter from Settings removes fullscreen
// entry behavior, including the keypad Enter variant.
// Preconditions: a visible non-fullscreen MainWindow; the Full Screen shortcut
// is explicitly saved as an empty list.
// Input data: Qt::Key_Return and Qt::Key_Enter.
// Steps: refresh shortcuts, send both key events, and inspect child shortcuts.
// Expected result: the window remains non-fullscreen and no QShortcut bypass
// remains for either Enter key.
// Postcondition: the window and shortcut setting are restored.
void WindowBehaviorTests::testEnterDoesNotBypassClearedFullscreenShortcut()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    ScopedShortcutValues shortcuts({{"fullscreen", QStringList {}}});

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);

    for (const auto *shortcut : window.findChildren<QShortcut *>())
    {
        QVERIFY(shortcut->key() != QKeySequence(Qt::Key_Return));
        QVERIFY(shortcut->key() != QKeySequence(Qt::Key_Enter));
    }

    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_Enter);
    QTest::qWait(100);
    QVERIFY(!window.isFullScreen());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-CONFIGURED
// Test purpose: verify that a user-specified Full Screen shortcut remains the
// only source of entry behavior after the hardcoded Enter shortcut is removed.
// Preconditions: a visible non-fullscreen MainWindow and a saved Space binding.
// Input data: Qt::Key_Space followed by Qt::Key_Escape.
// Steps: send Space, wait for fullscreen, then send Escape.
// Expected result: Space enters fullscreen and Escape exits it.
// Postcondition: the window and shortcut setting are restored.
void WindowBehaviorTests::testConfiguredFullscreenShortcutStillWorks()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    ScopedShortcutValues shortcuts({{"fullscreen", QStringList {QKeySequence(Qt::Key_Space).toString()}}});

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);

    QTest::keyClick(&window, Qt::Key_Space);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-TITLE-PRACTICAL
// Test purpose: verify Practical title text order and content.
// Preconditions: Titlebar text is Practical and a folder contains two images.
// Input data: 01-practical.png and 02-other.png, 64x48.
// Steps: open the first image and rebuild the title.
// Expected result: the title is "filename - index/count" with no leading zoom.
// Postcondition: the window and temporary image files are released.
void WindowBehaviorTests::testPracticalTitlebarTextUsesFilenameAndSequence()
{
    ScopedOptionValues options({
        {"titlebarmode", static_cast<int>(Qv::TitleBarText::Practical)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"sortmode", static_cast<int>(Qv::SortMode::Name)},
        {"sortdescending", false}
    });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString targetPath = createTestImage(dir, "01-practical", Qt::darkRed, QSize(64, 48));
    QVERIFY(!targetPath.isEmpty());
    QVERIFY(!createTestImage(dir, "02-other", Qt::darkBlue, QSize(64, 48)).isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(targetPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    window.buildWindowTitle();
    QCOMPARE(window.windowTitle(), QStringLiteral("01-practical.png - 1/2"));
    window.close();
}

// TC-TITLE-VERBOSE
// Test purpose: verify Verbose title text contains the requested fields in the
// specified order: filename, sequence, resolution, file size, zoom.
// Preconditions: Titlebar text is Verbose and a 64x48 image is loaded.
// Input data: two PNGs and an explicit 125% zoom.
// Steps: open the first image, set zoom to 125%, and rebuild the title.
// Expected result: all five fields appear exactly once and Fovelle is not
// appended as an unrelated suffix.
// Postcondition: the window and temporary image files are released.
void WindowBehaviorTests::testVerboseTitlebarTextUsesAllRequestedFields()
{
    ScopedOptionValues options({
        {"titlebarmode", static_cast<int>(Qv::TitleBarText::Verbose)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"sortmode", static_cast<int>(Qv::SortMode::Name)},
        {"sortdescending", false}
    });

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString targetPath = createTestImage(dir, "01-verbose", Qt::darkGreen, QSize(64, 48));
    QVERIFY(!targetPath.isEmpty());
    QVERIFY(!createTestImage(dir, "02-other", Qt::darkBlue, QSize(64, 48)).isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(targetPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.25, Qv::CalculateViewportCenterPos);
    window.buildWindowTitle();

    const QString expected = QStringLiteral("01-verbose.png - 1/2 - 64x48 - ") +
        QVInfoDialog::formatBytes(QFileInfo(targetPath).size()) + QStringLiteral(" - 125.0%");
    QCOMPARE(window.windowTitle(), expected);
    QVERIFY(!window.windowTitle().contains(QStringLiteral("Fovelle")));
    window.close();
}

// TC-THEME-SETTINGS
// Test purpose: verify Theme replaces both removed color controls and persists.
// Preconditions: Settings dialog can be constructed with Light Theme selected.
// Input data: Light Theme and Dark Theme combo-box entries.
// Steps: inspect the combo, confirm removed controls are absent, select Dark,
// and invoke Apply.
// Expected result: exactly two entries exist, Light is the default, and Dark is
// saved under the theme setting.
// Postcondition: the original theme setting is restored.
void WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls()
{
    ScopedOptionValues options({{"theme", static_cast<int>(Qv::Theme::Light)}});

    QVOptionsDialog dialog;
    auto *themeComboBox = dialog.findChild<QComboBox *>("themeComboBox");
    QVERIFY(themeComboBox);
    QCOMPARE(themeComboBox->count(), 2);
    QCOMPARE(themeComboBox->itemText(0), QStringLiteral("Light Theme"));
    QCOMPARE(themeComboBox->itemText(1), QStringLiteral("Dark Theme"));
    QCOMPARE(themeComboBox->itemData(0).toInt(), static_cast<int>(Qv::Theme::Light));
    QCOMPARE(themeComboBox->itemData(1).toInt(), static_cast<int>(Qv::Theme::Dark));
    QCOMPARE(themeComboBox->currentData().toInt(), static_cast<int>(Qv::Theme::Light));
    QVERIFY(!dialog.findChild<QCheckBox *>("bgColorCheckbox"));
    QVERIFY(!dialog.findChild<QPushButton *>("bgColorButton"));
    QVERIFY(!dialog.findChild<QCheckBox *>("darkTitlebarCheckbox"));

    themeComboBox->setCurrentIndex(1);
    auto *buttonBox = dialog.findChild<QDialogButtonBox *>("buttonBox");
    QVERIFY(buttonBox);
    auto *applyButton = buttonBox->button(QDialogButtonBox::Apply);
    QVERIFY(applyButton);
    QVERIFY(QMetaObject::invokeMethod(
        &dialog,
        "buttonBoxClicked",
        Qt::DirectConnection,
        Q_ARG(QAbstractButton *, applyButton)));
    QCOMPARE(qvApp->getSettingsManager().getEnum<Qv::Theme>("theme"), Qv::Theme::Dark);
}

// TC-THEME-COLORS
// Test purpose: verify Light/Dark Theme map to native Aqua/DarkAqua and to the
// Preview reference gray / former dark body colors.
// Preconditions: a visible MainWindow with no image loaded.
// Input data: Light Theme followed by Dark Theme.
// Steps: observe native appearance and a center viewport pixel for each theme.
// Expected result: Light is Aqua + #969696; Dark is DarkAqua + #212121.
// Postcondition: the original theme setting and window are restored.
void WindowBehaviorTests::testThemeAppliesNativeAppearanceAndViewportBackground()
{
    ScopedOptionValues options({{"theme", static_cast<int>(Qv::Theme::Light)}, {"checkerboardbackground", false}});

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);

    const auto viewportArea = [&window, view]() {
        return QRect(view->mapTo(&window, QPoint(0, 0)), view->size());
    };
    const auto viewportSnapshot = [&window]() { return window.grab().toImage(); };

    QTRY_COMPARE_WITH_TIMEOUT(QVCocoaFunctions::getWindowAppearanceName(window.windowHandle()), QStringLiteral("Aqua"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(containsColor(viewportSnapshot(), viewportArea(), QColor("#969696")), 2000);

    QSettings settings;
    settings.setValue("options/theme", static_cast<int>(Qv::Theme::Dark));
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
    QTRY_COMPARE_WITH_TIMEOUT(QVCocoaFunctions::getWindowAppearanceName(window.windowHandle()), QStringLiteral("DarkAqua"), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(containsColor(viewportSnapshot(), viewportArea(), QColor("#212121")), 2000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-THEME-CHECKERBOARD
// Test purpose: verify checkerboard takes precedence over either theme and
// disabling it restores the selected theme background.
// Preconditions: Dark Theme, checkerboard enabled, Original Size zoom, and a
// transparent 1x1 image are available.
// Input data: one transparent PNG, then checkerboard=false.
// Steps: load the transparent image, scan the viewport for both checker colors,
// disable the option, and scan again.
// Expected result: #ffffff and #cccccc are present while enabled; #212121 is
// present after disabling it.
// Postcondition: the window, fixture, and settings are restored.
void WindowBehaviorTests::testCheckerboardOverridesThemeAndRestoresBackground()
{
    ScopedOptionValues options({
        {"theme", static_cast<int>(Qv::Theme::Dark)},
        {"checkerboardbackground", true},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString transparentPath = createTransparentImage(dir, "transparent");
    QVERIFY(!transparentPath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(transparentPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    const QRect viewportArea(view->mapTo(&window, QPoint(0, 0)), view->size());

    QTRY_VERIFY_WITH_TIMEOUT(
        containsColor(window.grab().toImage(), viewportArea, QColorConstants::White) &&
            containsColor(window.grab().toImage(), viewportArea, QColor("#cccccc")),
        2000);

    QSettings settings;
    settings.setValue("options/checkerboardbackground", false);
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
    QTRY_VERIFY_WITH_TIMEOUT(containsColor(window.grab().toImage(), viewportArea, QColor("#212121")), 2000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-NAV-EDGE
// Test purpose: verify that navigation activation is limited to the left and
// right content edges and excludes the titlebar area.
// Preconditions: a valid main-window content rectangle and the configured
// activation width.
// Input data: points on both edge boundaries, the central area, and one point
// above the content rectangle.
// Steps: evaluate the pure edge predicate for each point.
// Expected result: only content-edge points activate navigation; the titlebar
// point and center point do not.
// Postcondition: no GUI or persistent state is changed.
void WindowBehaviorTests::testNavigationEdgeActivationExcludesTitlebar()
{
    const QRect contentRect(0, 100, 400, 300);
    const int activationWidth = MainWindow::navigationEdgeWidth(contentRect.width());
    QCOMPARE(activationWidth, 72);
    QVERIFY(MainWindow::isNavigationEdgeActive(QPoint(0, 200), contentRect));
    QVERIFY(MainWindow::isNavigationEdgeActive(QPoint(activationWidth - 1, 200), contentRect, activationWidth));
    QVERIFY(MainWindow::isNavigationEdgeActive(QPoint(399, 200), contentRect));
    QVERIFY(!MainWindow::isNavigationEdgeActive(QPoint(activationWidth, 200), contentRect, activationWidth));
    QVERIFY(MainWindow::isNavigationEdgeActive(QPoint(400 - activationWidth, 200), contentRect, activationWidth));
    QVERIFY(!MainWindow::isNavigationEdgeActive(QPoint(400 - activationWidth - 1, 200), contentRect, activationWidth));
    QVERIFY(!MainWindow::isNavigationEdgeActive(QPoint(200, 200), contentRect));
    QVERIFY(!MainWindow::isNavigationEdgeActive(QPoint(20, 99), contentRect));
}

// TC-NAV-SIZE
// Test purpose: verify the sensing-strip formula, the narrow-window cutoff,
// the absence of visibility-delay machinery, and the absence of tooltips.
// Preconditions: the navigation constants and MainWindow construction path.
// Input data: boundary widths 215/216 pt and representative widths around the
// 8% crossover, followed by a newly constructed main window.
// Steps: evaluate the pure width helpers and inspect the button/animation
// metadata and QObject tree.
// Expected result: widths below 216 pt are unsupported; the strip is the
// maximum of 72 pt and 8% (rounded upward to device points); no tooltip or
// visibility timer/delay property exists, and the opacity transition is 180 ms.
// Postcondition: the temporary window is closed; no settings are changed.
void WindowBehaviorTests::testNavigationButtonSizingAndNoDelay()
{
    QVERIFY(!MainWindow::areNavigationButtonsSupported(215));
    QVERIFY(MainWindow::areNavigationButtonsSupported(216));
    QCOMPARE(MainWindow::navigationEdgeWidth(800), 72);
    QCOMPARE(MainWindow::navigationEdgeWidth(899), 72);
    QCOMPARE(MainWindow::navigationEdgeWidth(900), 72);
    QCOMPARE(MainWindow::navigationEdgeWidth(901), 73);
    QCOMPARE(MainWindow::navigationEdgeWidth(1000), 80);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *previousButton = window.findChild<QPushButton *>("previousImageButton");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    QVERIFY(previousButton);
    QVERIFY(nextButton);
    QVERIFY(previousButton->toolTip().isEmpty());
    QVERIFY(nextButton->toolTip().isEmpty());
    QVERIFY(!previousButton->property("showDelayMs").isValid());
    QVERIFY(!previousButton->property("hideDelayMs").isValid());
    QVERIFY(!nextButton->property("showDelayMs").isValid());
    QVERIFY(!nextButton->property("hideDelayMs").isValid());
    QVERIFY(!window.findChild<QObject *>("previousImageButtonVisibilityTimer"));
    QVERIFY(!window.findChild<QObject *>("nextImageButtonVisibilityTimer"));
    QCOMPARE(previousButton->property("transitionDurationMs").toInt(), MainWindow::NavigationButtonAnimationDuration);
    QCOMPARE(nextButton->property("transitionDurationMs").toInt(), MainWindow::NavigationButtonAnimationDuration);
    window.close();
}

// TC-NAV-CONTRAST
// Test purpose: verify that each navigation button chooses its style from the
// pixels actually displayed beneath that button, independently per side.
// Preconditions: a visible two-file folder, a split white/black image, and the
// Settings Theme explicitly set to Dark to prove it is not the selector.
// Input data: a 1600x1000 image whose left half is white and right half black,
// plus a second image in the same folder.
// Steps: open the first image, move to the left edge and then right edge, and
// inspect the sampled brightness and contrastStyle properties.
// Expected result: the left button reports a light style and brightness > 0.5;
// the right button reports a dark style and brightness < 0.5.
// Postcondition: the window and temporary files are released; settings restore.
void WindowBehaviorTests::testNavigationButtonsUseActualContentContrast()
{
    ScopedOptionValues options({
        {"theme", static_cast<int>(Qv::Theme::Dark)},
        {"checkerboardbackground", false},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"sortmode", static_cast<int>(Qv::SortMode::Name)},
        {"sortdescending", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createSplitImage(dir, "01-navigation");
    const QString secondPath = createSplitImage(dir, "02-navigation");
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(800, 600);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(firstPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().folderFileInfoList.size() == 2, 5000);
    QTest::qWait(100);

    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    auto *previousButton = window.findChild<QPushButton *>("previousImageButton");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    QVERIFY(view);
    QVERIFY(previousButton);
    QVERIFY(nextButton);

    const int middleY = view->viewport()->height() / 2;
    sendMouseMove(view->viewport(), QPoint(1, middleY));
    QTRY_VERIFY_WITH_TIMEOUT(previousButton->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(previousButton->property("sampledContentBrightness").isValid(), 1000);
    QCOMPARE(previousButton->property("contrastStyle").toString(), QStringLiteral("light"));
    QVERIFY(previousButton->property("sampledContentBrightness").toDouble() > 0.5);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() - 1, middleY));
    QTRY_VERIFY_WITH_TIMEOUT(nextButton->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(nextButton->property("sampledContentBrightness").isValid(), 1000);
    QCOMPARE(nextButton->property("contrastStyle").toString(), QStringLiteral("dark"));
    QVERIFY(nextButton->property("sampledContentBrightness").toDouble() < 0.5);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-NAV-TRANSITION
// Test purpose: verify immediate navigation-button state changes with fade
// transitions and no appearance/disappearance delay.
// Preconditions: a visible two-file folder and a loaded image.
// Input data: two white 1600x1000 PNGs and mouse positions at the left edge,
// center, and right edge.
// Steps: move to the left edge and inspect the immediate show state and
// animation; move to the center and inspect the immediate hide animation;
// then move to the right edge and inspect the immediate show state.
// Expected result: entering/leaving the edge starts the corresponding 180 ms
// opacity animation in the same event turn; the button stays present while
// fading out, and both sides can be revealed.
// Postcondition: the window and temporary files are released; settings restore.
void WindowBehaviorTests::testNavigationButtonsFadeTransition()
{
    ScopedOptionValues options({
        {"theme", static_cast<int>(Qv::Theme::Light)},
        {"checkerboardbackground", false},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"sortmode", static_cast<int>(Qv::SortMode::Name)},
        {"sortdescending", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createTestImage(dir, "01-transition", Qt::white, QSize(1600, 1000));
    const QString secondPath = createTestImage(dir, "02-transition", Qt::white, QSize(1600, 1000));
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(800, 600);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(firstPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().folderFileInfoList.size() == 2, 5000);
    QTest::qWait(100);

    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    auto *previousButton = window.findChild<QPushButton *>("previousImageButton");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    auto *previousAnimation = window.findChild<QPropertyAnimation *>("previousImageButtonOpacityAnimation");
    auto *nextAnimation = window.findChild<QPropertyAnimation *>("nextImageButtonOpacityAnimation");
    QVERIFY(view);
    QVERIFY(previousButton);
    QVERIFY(nextButton);
    QVERIFY(previousAnimation);
    QVERIFY(nextAnimation);
    QCOMPARE(previousAnimation->duration(), MainWindow::NavigationButtonAnimationDuration);
    QCOMPARE(nextAnimation->duration(), MainWindow::NavigationButtonAnimationDuration);

    const int middleY = view->viewport()->height() / 2;
    sendMouseMove(view->viewport(), QPoint(1, middleY));
    QVERIFY(previousButton->isVisible());
    QCOMPARE(previousAnimation->state(), QAbstractAnimation::Running);
    QCOMPARE(previousAnimation->endValue().toReal(), 1.0);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() / 2, middleY));
    QVERIFY(previousButton->isVisible());
    QCOMPARE(previousAnimation->state(), QAbstractAnimation::Running);
    QCOMPARE(previousAnimation->endValue().toReal(), 0.0);
    QTest::qWait(100);
    QVERIFY(previousButton->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!previousButton->isVisible(), 1000);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() - 1, middleY));
    QVERIFY(nextButton->isVisible());
    QCOMPARE(nextAnimation->state(), QAbstractAnimation::Running);
    QCOMPARE(nextAnimation->endValue().toReal(), 1.0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-NAV-CLICK
// Test purpose: verify that clicking the visible Next button switches the
// current image to the adjacent file.
// Preconditions: a visible two-file folder, a loaded first image, and a Next
// button revealed by moving to the content area's right edge.
// Input data: two white 1600x1000 PNGs sorted by name.
// Steps: open the first file, send a deterministic right-edge mouse move, wait
// for the Next button, click it, and observe the loaded absolute path.
// Expected result: the second file becomes the current file within 5 seconds.
// Postcondition: the window and temporary files are released; settings restore.
void WindowBehaviorTests::testNavigationButtonsClickSwitchesFiles()
{
    ScopedOptionValues options({
        {"theme", static_cast<int>(Qv::Theme::Light)},
        {"checkerboardbackground", false},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"sortmode", static_cast<int>(Qv::SortMode::Name)},
        {"sortdescending", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createTestImage(dir, "01-click", Qt::white, QSize(1600, 1000));
    const QString secondPath = createTestImage(dir, "02-click", Qt::white, QSize(1600, 1000));
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(800, 600);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(firstPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window.getCurrentFileDetails().folderFileInfoList.size() == 2, 5000);
    QTest::qWait(100);

    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    QVERIFY(view);
    QVERIFY(nextButton);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() - 1, view->viewport()->height() / 2));
    QTRY_VERIFY_WITH_TIMEOUT(nextButton->isVisible(), 1000);
    QTest::mouseClick(nextButton, Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(
        window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
        QFileInfo(secondPath).absoluteFilePath(),
        5000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QGuiApplication::setApplicationDisplayName("Fovelle");
    QCoreApplication::setApplicationVersion("0.1.3");
    QVApplication app(argc, argv);
    qRegisterMetaType<QVImageLoader::Result>();

    ImageLoaderTests imageLoaderTests;
    FeatureTests featureTests;
    GraphicsViewTests graphicsViewTests;
    ApplicationEventTests applicationEventTests;
    ImageCoreAndMovieTests imageCoreAndMovieTests;
    WindowBehaviorTests windowBehaviorTests;
    const QByteArray selectedSuite = qgetenv("FOVELLE_TEST_SUITE");
    const auto runSuite = [&](const QByteArray &suiteName, QObject *suite) {
        return selectedSuite.isEmpty() || selectedSuite == suiteName ? QTest::qExec(suite, argc, argv) : 0;
    };
    int result = runSuite("ImageLoaderTests", &imageLoaderTests);
    result |= runSuite("FeatureTests", &featureTests);
    result |= runSuite("GraphicsViewTests", &graphicsViewTests);
    result |= runSuite("ApplicationEventTests", &applicationEventTests);
    result |= runSuite("ImageCoreAndMovieTests", &imageCoreAndMovieTests);
    result |= runSuite("WindowBehaviorTests", &windowBehaviorTests);
    return result;
}

#include "tst_qviewtests.moc"
