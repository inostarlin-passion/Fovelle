#include <QtTest>
#include <algorithm>
#include <numeric>
#include <QFileInfo>
#include <QDir>
#include <QFileOpenEvent>
#include <QImage>
#include <QLineF>
#include <QFile>
#include <QPainter>
#include <QPaintEvent>
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
#include <QStyleOptionGraphicsItem>
#include <QSvgRenderer>
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
#include "qvgraphicsimageitem.h"
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
    void testEPSFormatIsAdvertised();
    void testEPSPostScriptRender();
    void testImageLoaderLoadsEPS();
    void testImageLoaderLoadsSVGAsVectorDocument();
    void testEPSRenderSurvivesStaticMovieProbe();
    void testMalformedEPSFailsSafely();
    void testEPSMissingRendererFailsActionably();
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
    void testSettingsFormatsIncludeEPS();
    void testSmallImageOneToOneSettingIsExposedInImageOptions();
    void testOpenWithWorkerTeardownContract();
};

class HDRPolicyTests : public QObject
{
    Q_OBJECT

private slots:
    void testTransitionCurveIsBoundedAndMonotonic();
    void testFinalFrameRevealRejectsPartialHeadroom();
    void testHDRHeadroomIsClampedToContentAndDisplay();
    void testSDRDisplayForcesUnitHeadroom();
    void testDisplayHeadroomBootstrapsFromPotentialCapability();
    void testFinalFrameRevealRequiresMatchedGeometry();
    void testHDRViewportGeometryEquivalenceUsesCompleteContract();
    void testPersistentHDRLayerUsesStableQtViewportCoordinates();
    void testRawContentHeadroomUsesMeasuredPeakWhenUnknown();
    void testPreparedHDRPresentationCanBeReusedAcrossGeometry();
    void testViewportBackgroundColorsMatchTheme();
    void testRendererUsesFloatEDRColorManagedSurface();
    void testSDRImageStaysOnSDRPath();
    void testRequiredHDRFormatsAreAdvertised();
};

class HDRSampleTests : public QObject
{
    Q_OBJECT

private slots:
    void testGainMapJPEGCreatesNativeHDRGraph();
    void testGainMapJPEGHDRContainsAboveSDRValues();
    void testDNGCreatesProcessedGainMapHDRGraph();
    void testDNGProcessedGainMapContainsAboveSDRValues();
    void testDNGGainMapHeadroomMatchesMetadataContract();
    void testDNGProcessedGraphRepeatedFloatProbeIsStable();
    void testPlainDNGCreatesNativeRawEDRGraph();
    void testNEFCreatesNativeRawEDRGraph();
    void testNEFRawRepeatedFloatProbeIsStable();
};

class SDRSampleInteractionTests : public QObject
{
    Q_OBJECT

private slots:
    void testProvidedSamplesPanWithPartialRepaints();
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
    void testRasterPanRepaintsOnlyExposedStrip();
    void testZoomIsBoundedAt3200Percent();
    void testVectorFormatsUseDocumentSceneItem();
    void testVectorInteractionPaintPerformanceAt120Hz();
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
    void testNavigationBrightnessSamplingIsBounded();
    void testNavigationButtonUsesTransparentPaintOnlyFade();
    void testNavigationButtonsFadeTransition();
    void testNavigationButtonsClickSwitchesFiles();
};

class TestableImageCore : public QVImageCore
{
public:
    using QVImageCore::QVImageCore;
    using QVImageCore::handleColorSpaceConversion;
    using QVImageCore::loadPixmap;
};

class PaintRegionRecorder : public QObject
{
public:
    void clear() { areas.clear(); }
    const QVector<qint64> &recordedAreas() const { return areas; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        Q_UNUSED(watched)
        if (event->type() == QEvent::Paint)
        {
            const auto *paintEvent = static_cast<QPaintEvent *>(event);
            qint64 area = 0;
            for (const QRect &rect : paintEvent->region())
                area += static_cast<qint64>(rect.width()) * rect.height();
            areas.append(area);
        }
        return false;
    }

private:
    QVector<qint64> areas;
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

static QString createEPSVectorImage(const QTemporaryDir &dir, const QString &name)
{
    const QString path = dir.filePath(name + ".eps");
    const QByteArray content =
        "%!PS-Adobe-3.0 EPSF-3.0\n"
        "%%BoundingBox: 0 0 8 4\n"
        "%%HiResBoundingBox: 0 0 8 4\n"
        "%%Pages: 1\n"
        "%%EndComments\n"
        "0 setgray\n"
        "newpath 0 0 moveto 8 0 lineto 8 4 lineto 0 4 lineto closepath fill\n"
        "1 setgray\n"
        "newpath 1 1 moveto 3 1 lineto 3 3 lineto 1 3 lineto closepath fill\n"
        "newpath 5 1 moveto 7 1 lineto 7 3 lineto 5 3 lineto closepath fill\n"
        "showpage\n"
        "%%EOF\n";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
        return {};
    return path;
}

static QString createSVGVectorImage(const QTemporaryDir &dir, const QString &name)
{
    const QString path = dir.filePath(name + ".svg");
    const QByteArray content =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"120\" height=\"40\" viewBox=\"0 0 120 40\">"
        "<rect width=\"120\" height=\"40\" rx=\"8\" fill=\"#000\"/>"
        "<path d=\"M12 8 L32 20 L12 32 Z M108 8 L88 20 L108 32 Z\" fill=\"#fff\"/>"
        "<circle cx=\"60\" cy=\"20\" r=\"10\" fill=\"#fff\"/>"
        "</svg>";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size())
        return {};
    return path;
}

static QString epsSamplePath(const QTemporaryDir &fallbackDirectory, bool *usesExternalSample = nullptr)
{
    const QString configuredPath = QString::fromUtf8(qgetenv("FOVELLE_EPS_SAMPLE"));
    const QString suppliedPath = configuredPath.isEmpty()
        ? QStringLiteral("/Users/inostarlin/Downloads/Download-on-the-App-Store/US/Download_on_App_Store/Black_lockup/EPS/Download_on_the_App_Store_Badge_US-UK_blk_092917.eps")
        : configuredPath;
    if (QFileInfo::exists(suppliedPath))
    {
        if (usesExternalSample)
            *usesExternalSample = true;
        return suppliedPath;
    }

    if (usesExternalSample)
        *usesExternalSample = false;
    return createEPSVectorImage(fallbackDirectory, "fallback-vector");
}

static QString svgSamplePath(const QTemporaryDir &fallbackDirectory,
                             bool *usesExternalSample = nullptr)
{
    const QString configuredPath = QString::fromUtf8(qgetenv("FOVELLE_SVG_SAMPLE"));
    const QString suppliedPath = configuredPath.isEmpty()
        ? QStringLiteral("/Users/inostarlin/Downloads/Download-on-the-App-Store/US/Download_on_App_Store/Black_lockup/SVG/Download_on_the_App_Store_Badge_US-UK_RGB_blk_092917.svg")
        : configuredPath;
    if (QFileInfo::exists(suppliedPath))
    {
        if (usesExternalSample)
            *usesExternalSample = true;
        return suppliedPath;
    }

    if (usesExternalSample)
        *usesExternalSample = false;
    return createSVGVectorImage(fallbackDirectory, "fallback-vector");
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

static double sampledChannelDifference(const QImage &actual,
                                       const QImage &expected)
{
    if (actual.size() != expected.size() || actual.isNull())
        return std::numeric_limits<double>::infinity();

    qint64 difference = 0;
    qint64 channelSamples = 0;
    for (int y = 0; y < actual.height(); y += 8)
    {
        for (int x = 0; x < actual.width(); x += 8)
        {
            const QColor lhs = actual.pixelColor(x, y);
            const QColor rhs = expected.pixelColor(x, y);
            difference += qAbs(lhs.red() - rhs.red())
                    + qAbs(lhs.green() - rhs.green())
                    + qAbs(lhs.blue() - rhs.blue())
                    + qAbs(lhs.alpha() - rhs.alpha());
            channelSamples += 4;
        }
    }
    return channelSamples > 0
            ? static_cast<double>(difference) / channelSamples
            : std::numeric_limits<double>::infinity();
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

class ScopedSettingPreserver
{
public:
    explicit ScopedSettingPreserver(QString key)
        : settingKey(std::move(key))
    {
        QSettings settings;
        existed = settings.contains(settingKey);
        savedValue = settings.value(settingKey);
    }

    ~ScopedSettingPreserver()
    {
        QSettings settings;
        if (existed)
            settings.setValue(settingKey, savedValue);
        else
            settings.remove(settingKey);
        settings.sync();
    }

private:
    QString settingKey;
    QVariant savedValue;
    bool existed {false};
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

// TC-EPS-UNIT-FORMAT
// Test purpose: verify that EPS, EPSF, and EPSI are advertised by the native
// format registry used by the application.
// Preconditions: the macOS test process has initialized QVCocoaFunctions.
// Input data: the three conventional Encapsulated PostScript extensions.
// Operation steps: query the additional-format list, support predicate, and
// application extension set for every alias.
// Expected result: every alias is advertised and accepted.
// Postconditions: no settings or files are changed.
void ImageLoaderTests::testEPSFormatIsAdvertised()
{
    const QList<QByteArray> formats{ "eps", "epsf", "epsi" };
    const auto advertisedFormats = QVCocoaFunctions::getAdditionalImageFormats();
    for (const QByteArray &format : formats)
    {
        QVERIFY(advertisedFormats.contains(format));
        QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat(format));
        QVERIFY(qvApp->getAllFileExtensionList().contains("." + QString::fromUtf8(format)));
    }
}

// TC-EPS-UNIT-RENDER
// Test purpose: prove that EPS uses its authoritative PostScript program rather
// than the low-resolution embedded placement preview.
// Preconditions: Ghostscript is installed; a readable sample is available, or
// a writable temporary directory can hold the deterministic vector fixture.
// Input data: the supplied DOS EPS (120x40 points with a 120x40 TIFF preview),
// or an 8x4 EPS containing two vector white squares on black.
// Operation steps: request a 2048px render and inspect UTI, logical page size,
// raster size, alpha/luminance distribution, and the sample's upright layout.
// Expected result: the result is a cropped, high-resolution PostScript render;
// for the supplied badge, its larger lower lettering remains below the caption.
// Postconditions: the child process and temporary conversion files are released.
void ImageLoaderTests::testEPSPostScriptRender()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    bool usesExternalSample = false;
    const QString path = epsSamplePath(dir, &usesExternalSample);
    QVERIFY(!path.isEmpty());

    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isImageIOType);
    QCOMPARE(result.typeIdentifier, QStringLiteral("com.adobe.encapsulated-postscript"));
    QVERIFY(!result.image.isNull());
    QVERIFY(!result.intrinsicSize.isEmpty());
    QVERIFY(result.vectorImage.isValid());
    QCOMPARE(result.vectorImage.format, Qv::VectorImageFormat::Pdf);
    QCOMPARE(result.vectorImage.logicalSize, QSizeF(result.intrinsicSize));
    QVERIFY(result.vectorImage.encodedData.startsWith("%PDF-"));
    QCOMPARE(qMax(result.image.width(), result.image.height()), 512);
    QVERIFY(result.image.size() != result.intrinsicSize);

    QString pdfError;
    const auto pdfDocument = QVCocoaFunctions::createPDFVectorDocument(
        result.vectorImage.encodedData, &pdfError);
    QVERIFY2(pdfDocument, qPrintable(pdfError));
    const QSize renderedSize = result.intrinsicSize.scaled(
        2048, 2048, Qt::KeepAspectRatio);
    const QImage renderedImage = pdfDocument->renderTile(
        result.vectorImage.logicalSize,
        QRectF(QPointF(), result.vectorImage.logicalSize),
        renderedSize, &pdfError);
    QVERIFY2(!renderedImage.isNull(), qPrintable(pdfError));
    QCOMPARE(qMax(renderedImage.width(), renderedImage.height()), 2048);

    QVGraphicsImageItem sceneItem;
    sceneItem.setPixmap(QPixmap::fromImage(result.image));
    QVERIFY(sceneItem.setVectorImage(result.vectorImage));
    QImage sceneRender(renderedImage.size(), QImage::Format_ARGB32_Premultiplied);
    sceneRender.fill(Qt::transparent);
    QPainter scenePainter(&sceneRender);
    scenePainter.scale(
        renderedImage.width() / result.vectorImage.logicalSize.width(),
        renderedImage.height() / result.vectorImage.logicalSize.height());
    QStyleOptionGraphicsItem sceneOption;
    sceneOption.exposedRect = sceneItem.boundingRect();
    sceneItem.paint(&scenePainter, &sceneOption);
    scenePainter.end();
    QVERIFY(sampledChannelDifference(sceneRender, renderedImage) < 3.0);

    const QRectF upperSourceRect(
        0, 0, result.vectorImage.logicalSize.width(),
        result.vectorImage.logicalSize.height() / 2.0);
    const QImage upperTile = pdfDocument->renderTile(
        result.vectorImage.logicalSize, upperSourceRect,
        QSize(renderedImage.width(), renderedImage.height() / 2), &pdfError);
    QVERIFY2(!upperTile.isNull(), qPrintable(pdfError));
    qint64 upperTileDifference = 0;
    qint64 upperTileSamples = 0;
    for (int y = 0; y < upperTile.height(); y += 8)
    {
        for (int x = 0; x < upperTile.width(); x += 8)
        {
            const QColor actual = upperTile.pixelColor(x, y);
            const QColor expected = renderedImage.pixelColor(x, y);
            upperTileDifference += qAbs(actual.red() - expected.red())
                    + qAbs(actual.green() - expected.green())
                    + qAbs(actual.blue() - expected.blue())
                    + qAbs(actual.alpha() - expected.alpha());
            upperTileSamples += 4;
        }
    }
    QVERIFY(upperTileSamples > 0);
    QVERIFY(static_cast<double>(upperTileDifference) / upperTileSamples < 3.0);

    if (usesExternalSample)
    {
        QCOMPARE(result.intrinsicSize, QSize(120, 40));
        QVERIFY(renderedImage.width() >= 2000);

        qint64 upperLightPixels = 0;
        qint64 lowerLightPixels = 0;
        qint64 opaqueDarkPixels = 0;
        for (int y = 0; y < renderedImage.height(); y += 2)
        {
            for (int x = 0; x < renderedImage.width(); x += 2)
            {
                const QColor pixel = renderedImage.pixelColor(x, y);
                if (pixel.alpha() < 220)
                    continue;
                const int luminance = pixel.lightness();
                if (luminance >= 220)
                {
                    if (y < renderedImage.height() / 2)
                        ++upperLightPixels;
                    else
                        ++lowerLightPixels;
                }
                else if (luminance <= 32)
                {
                    ++opaqueDarkPixels;
                }
            }
        }
        QVERIFY(opaqueDarkPixels > renderedImage.width() * renderedImage.height() / 16);
        QVERIFY(lowerLightPixels > upperLightPixels);
    }
    else
    {
        QCOMPARE(result.intrinsicSize, QSize(8, 4));
        const QColor background = renderedImage.pixelColor(renderedImage.width() / 2,
                                                            renderedImage.height() / 8);
        const QColor square = renderedImage.pixelColor(renderedImage.width() / 4,
                                                        renderedImage.height() / 2);
        QVERIFY(background.lightness() < 32);
        QVERIFY(square.lightness() > 220);
    }
}

// TC-EPS-UNIT-LOADER
// Test purpose: verify that EPS pixels travel through the production
// asynchronous QVImageLoader path with the same result contract as other images.
// Preconditions: Ghostscript and the EPS sample or deterministic vector
// fallback are readable.
// Input data: the path returned by epsSamplePath.
// Operation steps: request the image, wait for imageReady, and inspect the
// result, source identity, and error state.
// Expected result: one matching request completes with a retained PDF vector
// document, a bounded preview, no error data, and its logical EPS page size.
// Postconditions: the loader and temporary fixture are destroyed.
void ImageLoaderTests::testImageLoaderLoadsEPS()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = epsSamplePath(dir);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QVERIFY(!result->errorData.has_value());
    QVERIFY(!result->intrinsicSize.isEmpty());
    QVERIFY(result->vectorImage.isValid());
    QCOMPARE(result->vectorImage.format, Qv::VectorImageFormat::Pdf);
    QCOMPARE(qMax(result->image.width(), result->image.height()), 512);
    QVERIFY(result->image.size() != result->intrinsicSize);
    QCOMPARE(result->absoluteFilePath, QFileInfo(path).absoluteFilePath());
}

// TC-EPS-UNIT-STATIC-DOCUMENT
// Test purpose: ensure animation probing cannot replace the authoritative EPS
// render with an embedded placement preview after the initial load.
// Preconditions: the production loader has returned a valid EPS result.
// Input data: the same supplied or deterministic EPS fixture.
// Operation steps: feed the result into QVImageCore, process delayed movie
// callbacks, and compare the retained pixmap size and movie state.
// Expected result: the PDF vector document remains loaded and no movie runs.
// Postconditions: QVImageCore and any image-reader device are destroyed.
void ImageLoaderTests::testEPSRenderSurvivesStaticMovieProbe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = epsSamplePath(dir);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->errorData.has_value());
    const QSize renderedSize = result->image.size();
    QVERIFY(result->vectorImage.isValid());

    QWidget owner;
    owner.resize(32, 32);
    owner.show();
    QVERIFY(QTest::qWaitForWindowExposed(&owner));
    TestableImageCore imageCore(&owner);
    imageCore.loadPixmap(*result);
    QCOMPARE(imageCore.getLoadedMovie().state(), QVMovie::NotRunning);
    QVERIFY(imageCore.getLoadedVectorImage().isValid());
    QCOMPARE(imageCore.getLoadedPixmap().size(), renderedSize);
    QTest::qWait(1100);
    QCOMPARE(imageCore.getLoadedMovie().state(), QVMovie::NotRunning);
    QVERIFY(imageCore.getLoadedVectorImage().isValid());
    QCOMPARE(imageCore.getLoadedPixmap().size(), renderedSize);
}

void ImageLoaderTests::testImageLoaderLoadsSVGAsVectorDocument()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = svgSamplePath(dir);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY2(!result->errorData.has_value(),
             result->errorData.has_value()
                ? qPrintable(result->errorData->errorString) : "");
    QVERIFY(result->vectorImage.isValid());
    QCOMPARE(result->vectorImage.format, Qv::VectorImageFormat::Svg);
    QCOMPARE(result->vectorImage.sourcePath, QFileInfo(path).absoluteFilePath());
    QCOMPARE(result->vectorImage.logicalSize, QSizeF(result->intrinsicSize));
    QVERIFY(!result->image.isNull());
    QVERIFY(qMax(result->image.width(), result->image.height()) <= 512);

    const QSize renderedSize = result->intrinsicSize.scaled(
        2048, 2048, Qt::KeepAspectRatio);
    QImage reference(renderedSize, QImage::Format_ARGB32_Premultiplied);
    reference.fill(Qt::transparent);
    QSvgRenderer referenceRenderer(path);
    QVERIFY(referenceRenderer.isValid());
    QPainter referencePainter(&reference);
    referenceRenderer.render(
        &referencePainter, QRectF(QPointF(), QSizeF(renderedSize)));
    referencePainter.end();

    QVGraphicsImageItem sceneItem;
    sceneItem.setPixmap(QPixmap::fromImage(result->image));
    QVERIFY(sceneItem.setVectorImage(result->vectorImage));
    QImage sceneRender(renderedSize, QImage::Format_ARGB32_Premultiplied);
    sceneRender.fill(Qt::transparent);
    QPainter scenePainter(&sceneRender);
    scenePainter.scale(
        renderedSize.width() / result->vectorImage.logicalSize.width(),
        renderedSize.height() / result->vectorImage.logicalSize.height());
    QStyleOptionGraphicsItem sceneOption;
    sceneOption.exposedRect = sceneItem.boundingRect();
    sceneItem.paint(&scenePainter, &sceneOption);
    scenePainter.end();
    QVERIFY(sampledChannelDifference(sceneRender, reference) < 3.0);
}

// TC-EPS-UNIT-MALFORMED
// Test purpose: verify that malformed DOS EPS input fails closed without a
// crash or false-positive image.
// Preconditions: a writable temporary directory is available.
// Input data: a truncated DOS EPS binary wrapper without PostScript content.
// Operation steps: write the malformed header and call the bounded renderer.
// Expected result: the result keeps the EPS type identifier, has no image, and
// reports a non-empty error string.
// Postconditions: the malformed fixture and decoder result are released.
void ImageLoaderTests::testMalformedEPSFailsSafely()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("malformed.eps");
    QByteArray header(32, '\0');
    header[0] = static_cast<char>(0xC5);
    header[1] = static_cast<char>(0xD0);
    header[2] = static_cast<char>(0xD3);
    header[3] = static_cast<char>(0xC6);
    header[20] = 32;
    header[24] = 100;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(header), header.size());
    file.close();

    const auto result = QVCocoaFunctions::readImageWithImageIO(path);
    QCOMPARE(result.typeIdentifier, QStringLiteral("com.adobe.encapsulated-postscript"));
    QVERIFY(result.image.isNull());
    QVERIFY(!result.errorString.isEmpty());
}

// TC-EPS-UNIT-DEPENDENCY
// Test purpose: ensure a missing Ghostscript executable produces an actionable
// error and cannot fall back to the EPS placement preview.
// Preconditions: a readable EPS fixture and writable process environment exist.
// Input data: the EPS path plus an explicitly invalid FOVELLE_GHOSTSCRIPT path.
// Operation steps: load through QVImageLoader, restore the environment, and
// inspect the final image/error result.
// Expected result: no image is returned and the error names Ghostscript and
// explains how to provide it.
// Postconditions: the original FOVELLE_GHOSTSCRIPT environment is restored.
void ImageLoaderTests::testEPSMissingRendererFailsActionably()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = epsSamplePath(dir);
    QVERIFY(!path.isEmpty());

    constexpr auto Variable = "FOVELLE_GHOSTSCRIPT";
    const bool variableWasSet = qEnvironmentVariableIsSet(Variable);
    const QByteArray previousValue = qgetenv(Variable);
    qputenv(Variable, "/path/that/does/not/contain/ghostscript");
    const auto result = loadImage(path);
    if (variableWasSet)
        qputenv(Variable, previousValue);
    else
        qunsetenv(Variable);

    QVERIFY(result.has_value());
    QVERIFY(result->image.isNull());
    QVERIFY(result->errorData.has_value());
    QVERIFY(result->errorData->errorString.contains(QStringLiteral("requires Ghostscript")));
    QVERIFY(result->errorData->errorString.contains(QStringLiteral("FOVELLE_GHOSTSCRIPT")));
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

// TC-HDR-UNIT-TRANSITION
// Test purpose: verify the Quick Look-like activation curve is bounded,
// deterministic, smooth at both endpoints, and monotonic.
// Preconditions: no display or image fixture is required.
// Input data: progress values below zero, from 0 through 1 in 1/100 steps,
// and above one.
// Steps: evaluate easedHDRTransition for every value and compare neighbors.
// Expected result: output is clamped to [0,1], starts at 0, ends at 1, and
// never decreases.
// Postcondition: no global or display state changes.
void HDRPolicyTests::testTransitionCurveIsBoundedAndMonotonic()
{
    QCOMPARE(QVCocoaFunctions::easedHDRTransition(-1.0), 0.0);
    QCOMPARE(QVCocoaFunctions::easedHDRTransition(0.0), 0.0);
    QCOMPARE(QVCocoaFunctions::easedHDRTransition(1.0), 1.0);
    QCOMPARE(QVCocoaFunctions::easedHDRTransition(2.0), 1.0);

    qreal previous = 0.0;
    for (int index = 1; index <= 100; ++index) {
        const qreal value = QVCocoaFunctions::easedHDRTransition(index / 100.0);
        QVERIFY(value >= previous);
        QVERIFY(value >= 0.0 && value <= 1.0);
        previous = value;
    }
}

// TC-HDR-UNIT-FIRST-VISIBLE-FINAL
// Test purpose: verify an app-generated partial-headroom frame can never be
// selected as the first visible Metal presentation.
// Preconditions: the pure reveal policy helper is available.
// Input data: matched drawable geometry and progress around the 0.999 final
// endpoint boundary.
// Steps: evaluate isFinalHDRFrameReadyForReveal for every input.
// Expected result: all partial endpoints are rejected; only final endpoints
// are eligible for reveal.
// Postcondition: no clocks, timers, images, or display state change.
void HDRPolicyTests::testFinalFrameRevealRejectsPartialHeadroom()
{
    QVERIFY(!QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, -1.0));
    QVERIFY(!QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 0.0));
    QVERIFY(!QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 0.5));
    QVERIFY(!QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 0.998));
    QVERIFY(QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 0.999));
    QVERIFY(QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 1.0));
    QVERIFY(QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 2.0));
}

// TC-HDR-UNIT-HEADROOM-CLAMP
// Test purpose: verify image highlights never request more headroom than both
// the content and current display permit.
// Preconditions: the pure policy helper is available.
// Input data: content/display pairs 4/6, 8/3, and unknown-content/3 at full
// transition, plus 4/4 at half transition.
// Steps: evaluate effectiveHDRHeadroom for each pair.
// Expected result: full results are 4, 3, and 3; the half result is strictly
// between 1 and 4.
// Postcondition: no global or display state changes.
void HDRPolicyTests::testHDRHeadroomIsClampedToContentAndDisplay()
{
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(4.0, 6.0, 1.0), 4.0);
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(8.0, 3.0, 1.0), 3.0);
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(0.0, 3.0, 1.0), 3.0);
    const qreal midpoint = QVCocoaFunctions::effectiveHDRHeadroom(4.0, 4.0, 0.5);
    QVERIFY(midpoint > 1.0 && midpoint < 4.0);
}

// TC-HDR-UNIT-SDR-FALLBACK
// Test purpose: verify an SDR-only display deterministically collapses HDR
// content to unit headroom at every transition phase.
// Preconditions: the pure policy helper is available.
// Input data: content headroom 4.9473, display headroom 1, and progress values
// 0, 0.5, and 1.
// Steps: evaluate effectiveHDRHeadroom for each progress value.
// Expected result: every result is exactly 1, selecting the SDR/tone-mapped
// representation without clipping through an SDR image-view path.
// Postcondition: no global or display state changes.
void HDRPolicyTests::testSDRDisplayForcesUnitHeadroom()
{
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(4.9473, 1.0, 0.0), 1.0);
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(4.9473, 1.0, 0.5), 1.0);
    QCOMPARE(QVCocoaFunctions::effectiveHDRHeadroom(4.9473, 1.0, 1.0), 1.0);
}

// TC-HDR-UNIT-EDR-BOOTSTRAP
// Test purpose: verify a potential EDR display can accept the first EDR frame
// even while NSScreen's dynamic current value still reports one.
// Preconditions: the pure rendering-headroom policy helper is available.
// Input data: SDR-only, clean-start XDR with known/unknown content headroom,
// and an already-active XDR current headroom.
// Steps: evaluate displayHeadroomForRendering for all four states.
// Expected result: SDR remains one; clean-start XDR uses bounded potential
// capability; once current rises, the dynamic current value is preferred.
// Postcondition: no display or process environment state changes.
void HDRPolicyTests::testDisplayHeadroomBootstrapsFromPotentialCapability()
{
    QCOMPARE(QVCocoaFunctions::displayHeadroomForRendering(1.0, 1.0, 5.0), 1.0);
    QCOMPARE(QVCocoaFunctions::displayHeadroomForRendering(1.0, 16.0, 4.9473), 4.9473);
    QCOMPARE(QVCocoaFunctions::displayHeadroomForRendering(1.0, 4.0, 0.0), 4.0);
    QCOMPARE(QVCocoaFunctions::displayHeadroomForRendering(3.5, 16.0, 5.0), 3.5);
}

// TC-HDR-UNIT-FIRST-VISIBLE-GEOMETRY
// Test purpose: verify a final-headroom frame is not revealed unless its
// physical drawable dimensions match the stable viewport contract.
// Preconditions: the pure reveal policy helper is available.
// Input data: final progress with matching and mismatching geometry.
// Steps: evaluate isFinalHDRFrameReadyForReveal twice.
// Expected result: mismatching geometry is rejected and matching geometry is
// accepted.
// Postcondition: no layer, image, or display state changes.
void HDRPolicyTests::testFinalFrameRevealRequiresMatchedGeometry()
{
    QVERIFY(!QVCocoaFunctions::isFinalHDRFrameReadyForReveal(false, 1.0));
    QVERIFY(QVCocoaFunctions::isFinalHDRFrameReadyForReveal(true, 1.0));
}

// TC-HDR-UNIT-GEOMETRY-EQUIVALENCE
// Test purpose: verify Metal geometry stabilization compares the complete
// viewport contract, not only zoom or drawable dimensions.
// Preconditions: the pure geometry comparator is available.
// Input data: identical four-corner geometry, sub/over-tolerance deltas,
// a different viewport, and a polygon with one missing corner.
// Steps: compare each candidate against the same reference contract.
// Expected result: only identical and 0.005-pixel-delta geometry are
// equivalent at the default 0.01-pixel tolerance.
// Postcondition: no widget or renderer state changes.
void HDRPolicyTests::testHDRViewportGeometryEquivalenceUsesCompleteContract()
{
    const QSize viewport(1200, 775);
    const QPolygonF reference{ QPointF(100.0, 20.0), QPointF(900.0, 20.0),
                               QPointF(900.0, 740.0), QPointF(100.0, 740.0) };
    QPolygonF withinTolerance = reference;
    withinTolerance[2] += QPointF(0.005, -0.005);
    QPolygonF outsideTolerance = reference;
    outsideTolerance[2] += QPointF(0.02, 0.0);
    QPolygonF missingCorner = reference;
    missingCorner.removeLast();

    QVERIFY(QVGraphicsView::hdrViewportGeometryEquivalent(
            viewport, reference, viewport, reference));
    QVERIFY(QVGraphicsView::hdrViewportGeometryEquivalent(
            viewport, reference, viewport, withinTolerance));
    QVERIFY(!QVGraphicsView::hdrViewportGeometryEquivalent(
            viewport, reference, viewport, outsideTolerance));
    QVERIFY(!QVGraphicsView::hdrViewportGeometryEquivalent(
            viewport, reference, QSize(1199, 775), reference));
    QVERIFY(!QVGraphicsView::hdrViewportGeometryEquivalent(
            viewport, reference, viewport, missingCorner));
}

// TC-HDR-UNIT-PERSISTENT-LAYER-COORDINATES
// Test purpose: verify the persistent Core Animation HDR surface has one stable
// unflipped container contract and consumes QGraphicsView viewport coordinates
// without a second vertical conversion.
// Preconditions: no native view, display, or HDR fixture is required.
// Input data: fitted and rotated four-corner viewport geometries, plus a
// positive vertical translation.
// Steps: build the layer transform and map all four source-image corners.
// Expected result: the standalone layer is unflipped, every source corner lands
// on the corresponding Qt corner, and moving Qt geometry down moves the native
// geometry down by the same amount.
// Postcondition: no native layer or renderer state is created.
void HDRPolicyTests::testPersistentHDRLayerUsesStableQtViewportCoordinates()
{
    const QSizeF sourceSize(6048.0, 8064.0);
    QVERIFY(!QVCocoaFunctions::persistentHDRLayerGeometryFlipped());
    const auto verifyCornerMapping = [&](const QPolygonF &corners) {
        const QTransform transform = QVCocoaFunctions::persistentHDRLayerTransform(
                sourceSize, corners);
        const QPolygonF sourceCorners{
            QPointF(0.0, 0.0),
            QPointF(sourceSize.width(), 0.0),
            QPointF(sourceSize.width(), sourceSize.height()),
            QPointF(0.0, sourceSize.height())
        };
        for (qsizetype index = 0; index < sourceCorners.size(); ++index)
            QVERIFY(QLineF(transform.map(sourceCorners.at(index)),
                           corners.at(index)).length() < 1e-9);
    };

    const QPolygonF fitted{
        QPointF(505.0, 27.0), QPointF(1222.75, 27.0),
        QPointF(1222.75, 984.0), QPointF(505.0, 984.0)
    };
    verifyCornerMapping(fitted);

    QPolygonF translated = fitted;
    for (QPointF &corner : translated)
        corner += QPointF(0.0, 37.0);
    const QTransform fittedTransform = QVCocoaFunctions::persistentHDRLayerTransform(
            sourceSize, fitted);
    const QTransform translatedTransform = QVCocoaFunctions::persistentHDRLayerTransform(
            sourceSize, translated);
    QCOMPARE(translatedTransform.map(QPointF(0.0, 0.0)).y()
                     - fittedTransform.map(QPointF(0.0, 0.0)).y(),
             37.0);

    const QPolygonF rotated{
        QPointF(140.0, 80.0), QPointF(940.0, 280.0),
        QPointF(760.0, 1000.0), QPointF(-40.0, 800.0)
    };
    verifyCornerMapping(rotated);
}

// TC-HDR-UNIT-CONTENT-HEADROOM
// Test purpose: ensure unknown RAW metadata is resolved from the measured
// float endpoint rather than from unrelated display capability.
// Preconditions: the pure headroom resolver is available.
// Input data: reported, unknown+measured, and invalid/empty combinations.
// Steps: call resolvedHDRContentHeadroom for each combination.
// Expected result: a valid report wins; otherwise the measured peak is used;
// invalid input falls back to SDR headroom one.
// Postcondition: no image or display state changes.
void HDRPolicyTests::testRawContentHeadroomUsesMeasuredPeakWhenUnknown()
{
    QCOMPARE(QVCocoaFunctions::resolvedHDRContentHeadroom(4.0, 1.8), 4.0);
    QCOMPARE(QVCocoaFunctions::resolvedHDRContentHeadroom(0.0, 1.8321), 1.8321);
    QCOMPARE(QVCocoaFunctions::resolvedHDRContentHeadroom(0.0, 0.7), 1.0);
    QCOMPARE(QVCocoaFunctions::resolvedHDRContentHeadroom(-1.0, -2.0), 1.0);
}

// TC-HDR-UNIT-PRESENTATION-REUSE
// Test purpose: verify only a fully prepared and presented HDR endpoint can be
// reused during zoom, pan, or resize without an SDR fallback.
// Preconditions: the pure reuse policy is available.
// Input data: all four combinations of presented/prepared.
// Steps: call canReuseHDRPresentation for every combination.
// Expected result: only true/true permits reuse.
// Postcondition: no renderer generation or widget state changes.
void HDRPolicyTests::testPreparedHDRPresentationCanBeReusedAcrossGeometry()
{
    QVERIFY(!QVGraphicsView::canReuseHDRPresentation(false, false));
    QVERIFY(!QVGraphicsView::canReuseHDRPresentation(true, false));
    QVERIFY(!QVGraphicsView::canReuseHDRPresentation(false, true));
    QVERIFY(QVGraphicsView::canReuseHDRPresentation(true, true));
}

// TC-HDR-UNIT-THEME-BACKGROUND
// Test purpose: keep Qt and Metal viewport backgrounds on one exact contract.
// Preconditions: no native window is required.
// Input data: light and dark application themes.
// Steps: resolve each theme through the shared helper.
// Expected result: light is #969696 and dark is #212121.
// Postcondition: no palette or settings changes.
void HDRPolicyTests::testViewportBackgroundColorsMatchTheme()
{
    QCOMPARE(Qv::viewportBackgroundColor(Qv::Theme::Light), QColor("#969696"));
    QCOMPARE(Qv::viewportBackgroundColor(Qv::Theme::Dark), QColor("#212121"));
}

// TC-HDR-UNIT-RENDERER-CONTRACT
// Test purpose: verify the native renderer declares the precision, color
// management, and EDR surface required by the production pipeline.
// Preconditions: the Cocoa Qt platform and a Metal-capable target Mac are
// available.
// Input data: a 320x200 native QWidget viewport with no source image.
// Steps: create HDRRenderer and inspect its non-invasive diagnostics.
// Expected result: the renderer is available and reports RGBA16Float,
// extended-linear Display P3, ColorSync, and wants-EDR enabled.
// Postcondition: the temporary native layer and widget are destroyed.
void HDRPolicyTests::testRendererUsesFloatEDRColorManagedSurface()
{
    QWidget viewport;
    viewport.resize(320, 200);
    QVCocoaFunctions::HDRRenderer renderer(&viewport);
    QVERIFY(renderer.isAvailable());
    const auto diagnostics = renderer.diagnostics();
    QVERIFY(diagnostics.rendererAvailable);
    QVERIFY(diagnostics.usesRGBA16Float);
    QVERIFY(diagnostics.usesExtendedLinearDisplayP3);
    QVERIFY(diagnostics.usesColorSync);
    QVERIFY(diagnostics.wantsExtendedDynamicRangeContent);
    QVERIFY(diagnostics.clearsEntireDrawableOpaque);
    QVERIFY(diagnostics.usesCoreImageManagedIntermediates);
    QVERIFY(diagnostics.cachesIntermediates);
    QVERIFY(diagnostics.usesCAMetalDisplayLink);
    QVERIFY(diagnostics.encodesMetalOffMainThread);
    QCOMPARE(diagnostics.backgroundRed, 150);
    QCOMPARE(diagnostics.backgroundGreen, 150);
    QCOMPARE(diagnostics.backgroundBlue, 150);
    QVERIFY(!diagnostics.imageActive);
}

// TC-HDR-UNIT-SDR-CLASSIFICATION
// Test purpose: verify a conventional SDR file is not promoted to the native
// HDR graph merely because the renderer supports EDR.
// Preconditions: Image I/O supports PNG and a temporary directory is writable.
// Input data: a deterministic 32x32 sRGB PNG.
// Steps: decode the PNG through readImageWithImageIO and inspect both outputs.
// Expected result: the SDR QImage is valid, the HDR handle is null, and
// decodedToHDR is false.
// Postcondition: the temporary PNG and decoder resources are released.
void HDRPolicyTests::testSDRImageStaysOnSDRPath()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = createTestImage(directory, "sdr-contract", Qt::red);
    QVERIFY(!path.isEmpty());
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY(!result.image.isNull());
    QVERIFY(!result.hdrImage);
    QVERIFY(!result.hdrMetadata.decodedToHDR);
    QVERIFY(!result.isRaw);
}

// TC-HDR-UNIT-FORMAT-COVERAGE
// Test purpose: verify the native platform decoder advertises every requested
// RAW family and the principal metadata-bearing non-RAW HDR containers.
// Preconditions: tests run on the target macOS Image I/O installation.
// Input data: dng, nef, cr3, arw, raf, jpeg, heif, heic, and avif extensions.
// Steps: query supportsAdditionalImageFormat for each extension.
// Expected result: every extension is supported by Image I/O on the target.
// Postcondition: the cached Image I/O type list is released normally.
void HDRPolicyTests::testRequiredHDRFormatsAreAdvertised()
{
    const QList<QByteArray> formats{ "dng",  "nef",  "cr3",  "arw", "raf",
                                     "jpeg", "heif", "heic", "avif" };
    for (const QByteArray &format : formats)
        QVERIFY2(QVCocoaFunctions::supportsAdditionalImageFormat(format), format.constData());
}

// TC-HDR-INT-GAINMAP-JPEG
// Test purpose: verify the supplied iPhone JPEG is reconstructed as a
// full-resolution adaptive HDR graph rather than flattened to an SDR bitmap.
// Preconditions: FOVELLE_HDR_JPEG_SAMPLE points to the supplied readable file
// and macOS 15+ Image I/O/Core Image APIs are available.
// Input data: IMG_1735.JPG and a 2048px-only SDR fallback budget.
// Steps: decode once, then inspect content UTI, gain-map metadata, headroom,
// intrinsic dimensions, native handle, and fallback dimensions.
// Expected result: public.jpeg is non-RAW; at least one gain map exists;
// decodedToHDR and the native handle are true; content headroom exceeds 1;
// the graph keeps full resolution while only its SDR fallback is bounded.
// Postcondition: all native image graphs and fallback pixels are released.
void HDRSampleTests::testGainMapJPEGCreatesNativeHDRGraph()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_JPEG_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isImageIOType);
    QCOMPARE(result.typeIdentifier, QStringLiteral("public.jpeg"));
    QVERIFY(!result.isRaw);
    QVERIFY(result.hdrImage);
    QVERIFY(result.hdrMetadata.decodedToHDR);
    QVERIFY(result.hdrMetadata.hasAppleGainMap || result.hdrMetadata.hasISOGainMap);
    QVERIFY(result.hdrMetadata.contentHeadroom > 1.0F);
    QCOMPARE(result.hdrMetadata.sourceKind, QStringLiteral("adaptive-hdr"));
    QVERIFY(result.intrinsicSize.width() > 2048 || result.intrinsicSize.height() > 2048);
    QVERIFY(!result.image.isNull());
    QVERIFY(qMax(result.image.width(), result.image.height()) <= 2048);
}

// TC-HDR-INT-GAINMAP-JPEG-PEAK
// Test purpose: prove the supplied JPEG's reconstructed adaptive-HDR graph
// contains extended-linear pixel values above its SDR representation.
// Preconditions: the sample is readable and Image I/O can expand its gain map.
// Input data: IMG_1735.JPG's retained SDR and HDR CI graphs.
// Steps: reduce both graphs with CIAreaMaximum in extended-linear Display P3
// and compare their largest RGB components.
// Expected result: HDR exceeds 1.05 and is at least 0.05 above SDR.
// Postcondition: the probe context and both graph references are released.
void HDRSampleTests::testGainMapJPEGHDRContainsAboveSDRValues()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_JPEG_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.hdrImage);
    const auto statistics = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    qInfo("FOVELLE_JPEG_PEAK sdr_max=%.9f hdr_max=%.9f",
          static_cast<double>(statistics.sdrMaximumComponent),
          static_cast<double>(statistics.hdrMaximumComponent));
    QVERIFY(statistics.valid);
    QVERIFY(statistics.hdrMaximumComponent > 1.05F);
    QVERIFY(statistics.hdrMaximumComponent > statistics.sdrMaximumComponent + 0.05F);
}

// TC-HDR-INT-RAW-DNG-PROCESSED-GAINMAP
// Test purpose: verify the supplied DNG follows the full-resolution processed
// preview plus its authored Adaptive-HDR gain map, matching Quick Look's public
// representation without flattening either endpoint.
// Preconditions: FOVELLE_HDR_RAW_SAMPLE points to the supplied readable DNG
// and the installed Apple RAW camera decoder supports it.
// Input data: IMG_8625.DNG and a 2048px-only SDR fallback budget.
// Steps: decode once, then inspect content UTI, RAW flags, precision, intrinsic
// dimensions, native handle, preview usage, and fallback dimensions.
// Expected result: the source remains camera RAW; a 16-bit-contract native
// processed/gain-map graph is present; full dimensions are retained while only
// the SDR Qt fallback is bounded.
// Postcondition: all native RAW graphs and fallback pixels are released.
void HDRSampleTests::testDNGCreatesProcessedGainMapHDRGraph()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_RAW_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isImageIOType);
    QVERIFY(result.isRaw);
    QVERIFY(result.typeIdentifier.contains(QStringLiteral("raw")));
    QVERIFY(result.hdrImage);
    QVERIFY(result.hdrMetadata.decodedToHDR);
    QVERIFY(!result.hdrMetadata.usesRawExtendedDynamicRange);
    QVERIFY(result.hdrMetadata.usesProcessedRawPreview);
    QVERIFY(result.usedRawPreview);
    QVERIFY(result.hdrMetadata.hasAppleGainMap || result.hdrMetadata.hasISOGainMap);
    QCOMPARE(result.hdrMetadata.sourceKind,
             QStringLiteral("camera-raw-processed-gain-map"));
    QCOMPARE(result.hdrMetadata.bitsPerComponent, 16);
    QVERIFY(result.intrinsicSize.width() > 2048 || result.intrinsicSize.height() > 2048);
    QVERIFY(!result.image.isNull());
    QVERIFY(qMax(result.image.width(), result.image.height()) <= 2048);
}

// TC-HDR-INT-RAW-DNG-GAINMAP-PEAK
// Test purpose: prove the supplied DNG's reconstructed gain-map graph contains real
// extended-linear values above SDR white, rather than only carrying HDR flags.
// Preconditions: the sample is readable and Apple CIRAWFilter supports it.
// Input data: IMG_8625.DNG processed preview and gain-map HDR endpoint.
// Steps: reduce both floating-point CI graphs with CIAreaMaximum and compare
// their largest RGB component in extended-linear Display P3.
// Expected result: SDR is at most 1.01; EDR is above 1.05 and exceeds SDR by
// at least 0.05.
// Postcondition: the one-pixel probes, CI context, and RAW graphs are released.
void HDRSampleTests::testDNGProcessedGainMapContainsAboveSDRValues()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_RAW_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.hdrImage);
    const auto statistics = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    qInfo("FOVELLE_RAW_PEAK sdr_max=%.9f hdr_max=%.9f",
          static_cast<double>(statistics.sdrMaximumComponent),
          static_cast<double>(statistics.hdrMaximumComponent));
    QVERIFY(statistics.valid);
    QVERIFY(statistics.sdrMaximumComponent <= 1.01F);
    QVERIFY(statistics.hdrMaximumComponent > 1.05F);
    QVERIFY(statistics.hdrMaximumComponent > statistics.sdrMaximumComponent + 0.05F);
}

// TC-HDR-INT-RAW-DNG-GAINMAP-HEADROOM
// Test purpose: verify gain-map metadata headroom is preserved as the content
// contract and safely bounds the measured half-float endpoint.
// Preconditions: the supplied DNG is readable by CIRAWFilter.
// Input data: decoder metadata plus a float CIAreaMaximum probe.
// Steps: decode once, probe the retained HDR graph, compare both values.
// Expected result: metadata headroom is above 1.5, is not lower than the
// measured half-float maximum, and differs by no more than quantization margin.
// Postcondition: decoder graphs and probe context are released.
void HDRSampleTests::testDNGGainMapHeadroomMatchesMetadataContract()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_RAW_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    const auto statistics = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    qInfo("FOVELLE_RAW_HEADROOM metadata=%.9f measured=%.9f",
          static_cast<double>(result.hdrMetadata.contentHeadroom),
          static_cast<double>(statistics.hdrMaximumComponent));
    QVERIFY(statistics.valid);
    QVERIFY(result.hdrMetadata.contentHeadroom > 1.5F);
    QVERIFY(result.hdrMetadata.contentHeadroom + 0.001F
            >= statistics.hdrMaximumComponent);
    QVERIFY(qAbs(result.hdrMetadata.contentHeadroom
                 - statistics.hdrMaximumComponent) <= 0.05F);
}

// TC-HDR-INT-RAW-DNG-REPEATABILITY
// Test purpose: prove repeated evaluation of the retained RAW endpoints does
// not produce timing-dependent partial or different pixel ranges.
// Preconditions: the supplied DNG has decoded into independent SDR/HDR graphs.
// Input data: two consecutive float endpoint probes of the same handle.
// Steps: call probeHDRPixelStatistics twice and compare both endpoints.
// Expected result: both probes are valid and each maximum agrees within 0.001.
// Postcondition: no filter properties are mutated between probes.
void HDRSampleTests::testDNGProcessedGraphRepeatedFloatProbeIsStable()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_RAW_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    const auto first = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    const auto second = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    QVERIFY(first.valid);
    QVERIFY(second.valid);
    QVERIFY(qAbs(first.sdrMaximumComponent - second.sdrMaximumComponent) <= 0.001F);
    QVERIFY(qAbs(first.hdrMaximumComponent - second.hdrMaximumComponent) <= 0.001F);
}

// TC-HDR-INT-RAW-PLAIN-DNG
// Test purpose: verify a DNG without the processed-preview gain-map contract
// follows the same immutable sensor-RAW EDR path used by other camera RAWs.
// Preconditions: FOVELLE_HDR_PLAIN_DNG_SAMPLE points to sample1.dng and the
// installed Apple RAW decoder supports its camera.
// Input data: sample1.dng and a 2048-pixel SDR fallback limit.
// Steps: decode with the production Image I/O bridge, inspect the retained
// graph contract, and compare SDR/HDR float peaks.
// Expected result: the native graph is camera-raw, uses RAW EDR rather than a
// processed preview, preserves 16-bit precision, and exceeds SDR white.
// Postcondition: native graphs, probe context, and fallback pixels are released.
void HDRSampleTests::testPlainDNGCreatesNativeRawEDRGraph()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_PLAIN_DNG_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isRaw);
    QVERIFY(result.hdrImage);
    QVERIFY(result.hdrMetadata.decodedToHDR);
    QVERIFY(result.hdrMetadata.usesRawExtendedDynamicRange);
    QVERIFY(!result.hdrMetadata.usesProcessedRawPreview);
    QVERIFY(!result.usedRawPreview);
    QCOMPARE(result.hdrMetadata.sourceKind, QStringLiteral("camera-raw"));
    QCOMPARE(result.hdrMetadata.bitsPerComponent, 16);
    QVERIFY(qMax(result.intrinsicSize.width(), result.intrinsicSize.height()) > 2048);
    QVERIFY(!result.image.isNull());
    QVERIFY(qMax(result.image.width(), result.image.height()) <= 2048);
    const auto statistics = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    qInfo("FOVELLE_PLAIN_DNG_PEAK sdr_max=%.9f hdr_max=%.9f",
          static_cast<double>(statistics.sdrMaximumComponent),
          static_cast<double>(statistics.hdrMaximumComponent));
    QVERIFY(statistics.valid);
    QVERIFY(statistics.hdrMaximumComponent > statistics.sdrMaximumComponent + 0.05F);
}

// TC-HDR-INT-RAW-NEF
// Test purpose: verify a conventional Nikon RAW retains independent full-size
// SDR/HDR CIRAWFilter recipes with camera defaults intact.
// Preconditions: FOVELLE_HDR_NEF_SAMPLE points to sample1.nef and the installed
// Apple RAW decoder supports the camera.
// Input data: sample1.nef and a 2048-pixel fallback limit.
// Steps: decode once and inspect RAW classification, source recipe, flags,
// native handle, precision, full size, and bounded fallback.
// Expected result: sourceKind is camera-raw, extendedDynamicRangeAmount is the
// HDR strategy, no processed preview is primary, and the graph exceeds SDR.
// Postcondition: native graphs and fallback pixels are released.
void HDRSampleTests::testNEFCreatesNativeRawEDRGraph()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_NEF_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isRaw);
    QVERIFY(result.hdrImage);
    QVERIFY(result.hdrMetadata.decodedToHDR);
    QVERIFY(result.hdrMetadata.usesRawExtendedDynamicRange);
    QVERIFY(!result.hdrMetadata.usesProcessedRawPreview);
    QVERIFY(!result.usedRawPreview);
    QCOMPARE(result.hdrMetadata.sourceKind, QStringLiteral("camera-raw"));
    QCOMPARE(result.hdrMetadata.bitsPerComponent, 16);
    QVERIFY(qMax(result.intrinsicSize.width(), result.intrinsicSize.height()) > 2048);
    QVERIFY(!result.image.isNull());
    QVERIFY(qMax(result.image.width(), result.image.height()) <= 2048);
    const auto statistics = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    qInfo("FOVELLE_NEF_PEAK sdr_max=%.9f hdr_max=%.9f",
          static_cast<double>(statistics.sdrMaximumComponent),
          static_cast<double>(statistics.hdrMaximumComponent));
    QVERIFY(statistics.valid);
    QVERIFY(statistics.hdrMaximumComponent > statistics.sdrMaximumComponent + 0.05F);
}

// TC-HDR-INT-RAW-NEF-REPEATABILITY
// Test purpose: ensure the immutable Nikon RAW endpoints evaluate identically
// across consecutive probes, independent of later zoom/pan geometry.
// Preconditions: sample1.nef decoded to two independent CIRAWFilter graphs.
// Input data: two consecutive full-graph float reductions.
// Steps: probe both endpoints twice without mutating filter properties.
// Expected result: both probes are valid and agree within 0.001.
// Postcondition: no renderer or source state is changed.
void HDRSampleTests::testNEFRawRepeatedFloatProbeIsStable()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_NEF_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    const auto first = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    const auto second = QVCocoaFunctions::probeHDRPixelStatistics(result.hdrImage);
    QVERIFY(first.valid);
    QVERIFY(second.valid);
    QVERIFY(qAbs(first.sdrMaximumComponent - second.sdrMaximumComponent) <= 0.001F);
    QVERIFY(qAbs(first.hdrMaximumComponent - second.hdrMaximumComponent) <= 0.001F);
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
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.4"));
}

// TC-APP-VERSION
// Test purpose: verify the application reports the released semantic version.
// Preconditions: the QVApplication has been constructed with the CMake version
// definitions.
// Input data: QCoreApplication::applicationVersion().
// Steps: read the runtime application version.
// Expected result: the value is exactly 0.1.4.
// Postcondition: no application or settings state changes.
void FeatureTests::testApplicationVersionIsCurrent()
{
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.4"));
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

// TC-EPS-INT-SETTINGS
// Test purpose: verify that the Settings → Formats table exposes the EPS
// aliases produced by the same registry used by folder enumeration.
// Preconditions: QVApplication has initialized its format registry and the
// options dialog can be constructed under Cocoa.
// Input data: .eps, .epsf, and .epsi extension entries.
// Operation steps: construct QVOptionsDialog and collect the first-column
// extension values from formatsTable.
// Expected result: all three EPS aliases are present and enabled by default.
// Postconditions: the dialog is destroyed without persisting settings.
void FeatureTests::testSettingsFormatsIncludeEPS()
{
    QVOptionsDialog dialog;
    const auto *formatsTable = dialog.findChild<QTableWidget *>("formatsTable");
    QVERIFY(formatsTable);

    QSet<QString> tableExtensions;
    QSet<QString> enabledExtensions;
    for (int row = 0; row < formatsTable->rowCount(); ++row)
    {
        tableExtensions.insert(formatsTable->item(row, 0)->text());
        if (formatsTable->item(row, 1)->checkState() == Qt::Checked)
            enabledExtensions.insert(formatsTable->item(row, 0)->text());
    }

    for (const QString &extension : {QStringLiteral(".eps"),
                                     QStringLiteral(".epsf"),
                                     QStringLiteral(".epsi")})
    {
        QVERIFY(tableExtensions.contains(extension));
        QVERIFY(enabledExtensions.contains(extension));
    }
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
// center of the usable viewport.
// Preconditions: a visible Cocoa window uses OriginalSize and loads an image
// sized to 90% of its current viewport, so the initial image fits and the next
// 1.25x step introduces overflow on both axes.
// Input data: one center-anchored zoom-in step.
// Steps: record the normalized image coordinate at the usable viewport center, zoom
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

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->viewport()->width() >= 200 && view->viewport()->height() >= 200, 1000);
    QRect usableViewport = view->viewport()->rect();
    usableViewport.setTop(window.getViewportPosition().obscuredHeight);
    if (usableViewport.width() < 200 || usableViewport.height() < 200)
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }

    const QSize imageSize(usableViewport.width() * 9 / 10, usableViewport.height() * 9 / 10);
    const QString imagePath = createTestImage(dir, "zoom-scrollbar-threshold", Qt::darkYellow, imageSize);
    QVERIFY(!imagePath.isEmpty());
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    if (!window.getIsPixmapLoaded())
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }

    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    const auto noScrollBars = [view]() {
        return !view->horizontalScrollBar()->isVisible() && !view->verticalScrollBar()->isVisible();
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        noScrollBars(),
        2000);
    if (!noScrollBars())
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }
    const auto normalizedImageCoordinateAtUsableViewportCenter = [&window, view]() {
        const QRectF imageRect = view->scene()->itemsBoundingRect();
        QRect currentUsableViewport = view->viewport()->rect();
        currentUsableViewport.setTop(window.getViewportPosition().obscuredHeight);
        const QPointF scenePoint = view->mapToScene(currentUsableViewport.center());
        return QPointF(
            (scenePoint.x() - imageRect.left()) / imageRect.width(),
            (scenePoint.y() - imageRect.top()) / imageRect.height());
    };
    const QPointF imageCoordinateBefore = normalizedImageCoordinateAtUsableViewportCenter();

    view->zoomIn();
    const auto bothScrollBars = [view]() {
        return view->horizontalScrollBar()->isVisible() && view->verticalScrollBar()->isVisible();
    };
    QTRY_VERIFY_WITH_TIMEOUT(
        bothScrollBars(),
        2000);
    if (!bothScrollBars())
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }
    QCoreApplication::processEvents();

    const QPointF imageCoordinateAfterLayout = normalizedImageCoordinateAtUsableViewportCenter();
    const bool layoutStable = QLineF(imageCoordinateBefore, imageCoordinateAfterLayout).length() <= 0.005;
    QVERIFY(layoutStable);
    if (!layoutStable)
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }
    QTest::qWait(150);
    QCoreApplication::processEvents();
    const QPointF imageCoordinateAfterScaling = normalizedImageCoordinateAtUsableViewportCenter();
    const bool scalingStable = QLineF(imageCoordinateAfterLayout, imageCoordinateAfterScaling).length() <= 0.005;
    QVERIFY(scalingStable);
    if (!scalingStable)
    {
        window.close();
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        return;
    }

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

// TC-SDR-PAN-PARTIAL-REPAINT
// Test purpose: verify raster-image panning keeps Qt's backing-store scroll
// acceleration instead of repainting the complete Retina viewport per input
// event.
// Preconditions: a visible 640x480 Cocoa window contains a 1600x900 raster
// image at 2:1 and both scroll axes have room to move.
// Input data: one six-pixel horizontal scrollbar change after all opening
// paints have settled.
// Steps: first clear WA_OpaquePaintEvent and measure a six-pixel pan, then
// restore it, repeat the same pan, and compare both paint regions.
// Expected result: the transparent control repaints nearly the whole viewport;
// the opaque production path repaints at most five percent (the edge strip).
// Postcondition: the recorder, window, fixture, and settings are released.
void GraphicsViewTests::testRasterPanRepaintsOnlyExposedStrip()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"checkerboardbackground", false},
        {"onetoonepixelsizing", false}
    });

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(
        dir, "raster-pan-partial-repaint", Qt::darkCyan, QSize(1600, 900));
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
    // Force a non-sticky zoom well above the viewport size. This also makes
    // the test independent of the previous session's window geometry and the
    // timing of the initial calculated-zoom restoration.
    view->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->maximum()
            > view->horizontalScrollBar()->minimum(),
        2000);
    view->horizontalScrollBar()->setValue(
        (view->horizontalScrollBar()->minimum()
         + view->horizontalScrollBar()->maximum()) / 2);
    QCoreApplication::processEvents();
    view->viewport()->repaint();
    QCoreApplication::processEvents();

    QScrollBar *bar = view->horizontalScrollBar();
    view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, false);
    PaintRegionRecorder transparentRecorder;
    view->viewport()->installEventFilter(&transparentRecorder);
    bar->setValue(bar->value() + 6);
    QTRY_VERIFY_WITH_TIMEOUT(!transparentRecorder.recordedAreas().isEmpty(), 1000);
    view->viewport()->removeEventFilter(&transparentRecorder);
    const qint64 viewportArea = static_cast<qint64>(view->viewport()->width())
            * view->viewport()->height();
    const qint64 transparentPaintArea = *std::max_element(
        transparentRecorder.recordedAreas().cbegin(),
        transparentRecorder.recordedAreas().cend());
    const qreal transparentDirtyRatio =
            static_cast<qreal>(transparentPaintArea) / viewportArea;

    view->viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    view->viewport()->repaint();
    QCoreApplication::processEvents();
    PaintRegionRecorder recorder;
    view->viewport()->installEventFilter(&recorder);
    bar->setValue(bar->value() + 6);
    QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000);
    view->viewport()->removeEventFilter(&recorder);

    const qint64 maximumPaintArea = *std::max_element(
        recorder.recordedAreas().cbegin(), recorder.recordedAreas().cend());
    const qreal dirtyRatio = static_cast<qreal>(maximumPaintArea) / viewportArea;
    qInfo().noquote() << QStringLiteral(
        "SDR_PAN_REPAINT transparent_dirty_ratio=%1 "
        "opaque_dirty_ratio=%2 viewport_area=%3")
        .arg(transparentDirtyRatio, 0, 'f', 6)
        .arg(dirtyRatio, 0, 'f', 6)
        .arg(viewportArea);
    QVERIFY2(transparentDirtyRatio >= 0.95,
             qPrintable(QStringLiteral("unexpected transparent pan ratio %1")
                        .arg(transparentDirtyRatio, 0, 'f', 6)));
    QVERIFY2(dirtyRatio <= 0.05,
             qPrintable(QStringLiteral("unexpected full pan repaint ratio %1")
                        .arg(dirtyRatio, 0, 'f', 6)));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-INT-SAMPLE-PAN
// Test purpose: exercise real, externally supplied SDR files through the full
// decoder -> QPixmap -> QGraphicsView path and verify that format-specific
// loading does not regress backing-store scroll acceleration.
// Preconditions: FOVELLE_SDR_SAMPLE_DIR names a readable directory containing
// one or more supported raster images.
// Input data: every regular file in the supplied directory.
// Steps: load each image, assert the SDR path, force an overflowing zoom,
// settle the full-frame paint, pan by six pixels, and inspect the paint region.
// Expected result: each image loads at intrinsic resolution and its pan repaint
// covers at most five percent of the viewport.
// Postcondition: the recorder, window, samples, and settings are released.
void SDRSampleInteractionTests::testProvidedSamplesPanWithPartialRepaints()
{
    const QString sampleDirectory =
            QString::fromUtf8(qgetenv("FOVELLE_SDR_SAMPLE_DIR"));
    const QDir directory(sampleDirectory);
    QVERIFY2(!sampleDirectory.isEmpty() && directory.exists(),
             qPrintable(sampleDirectory));
    const QFileInfoList samples = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name);
    QVERIFY2(!samples.isEmpty(), qPrintable(sampleDirectory));

    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"checkerboardbackground", false},
        {"onetoonepixelsizing", false}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(1200, 800);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);

    for (const QFileInfo &sample : samples)
    {
        window.openFile(sample.absoluteFilePath());
        QTRY_COMPARE_WITH_TIMEOUT(
            window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
            sample.absoluteFilePath(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 10000);
        const auto &details = window.getCurrentFileDetails();
        QVERIFY2(!details.isNativeHDRLoaded, qPrintable(sample.fileName()));
        QVERIFY2(!details.loadedPixmapSize.isEmpty(), qPrintable(sample.fileName()));
        QVERIFY(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));

        view->zoomAbsolute(2.0, Qv::CalculateViewportCenterPos);
        QTRY_VERIFY_WITH_TIMEOUT(
            view->horizontalScrollBar()->maximum()
                > view->horizontalScrollBar()->minimum(),
            2000);
        QScrollBar *bar = view->horizontalScrollBar();
        const int centerValue = (bar->minimum() + bar->maximum()) / 2;
        bar->setValue(centerValue);
        QCoreApplication::processEvents();
        view->viewport()->repaint();
        QCoreApplication::processEvents();
        // Exclude one-time scrollbar/layout work from the measured pan.
        bar->setValue(centerValue + 6);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        bar->setValue(centerValue);
        QCoreApplication::processEvents(QEventLoop::AllEvents);

        PaintRegionRecorder recorder;
        view->viewport()->installEventFilter(&recorder);
        QElapsedTimer timer;
        timer.start();
        bar->setValue(bar->value() + 6);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        const double elapsedMilliseconds = timer.nsecsElapsed() / 1000000.0;
        QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000);
        view->viewport()->removeEventFilter(&recorder);

        const qint64 viewportArea = static_cast<qint64>(view->viewport()->width())
                * view->viewport()->height();
        const qint64 maximumPaintArea = *std::max_element(
            recorder.recordedAreas().cbegin(), recorder.recordedAreas().cend());
        const qreal dirtyRatio = static_cast<qreal>(maximumPaintArea) / viewportArea;
        qInfo().noquote() << QStringLiteral(
            "SDR_SAMPLE_PAN file=%1 size=%2x%3 decode_ms=%4 "
            "pan_dispatch_and_paint_ms=%5 dirty_ratio=%6")
            .arg(sample.fileName())
            .arg(details.loadedPixmapSize.width())
            .arg(details.loadedPixmapSize.height())
            .arg(details.decodeMilliseconds, 0, 'f', 3)
            .arg(elapsedMilliseconds, 0, 'f', 3)
            .arg(dirtyRatio, 0, 'f', 6);
        QVERIFY2(dirtyRatio <= 0.05,
                 qPrintable(QStringLiteral("%1 full pan repaint ratio %2")
                            .arg(sample.fileName()).arg(dirtyRatio, 0, 'f', 6)));
    }

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void GraphicsViewTests::testZoomIsBoundedAt3200Percent()
{
    QCOMPARE(QVGraphicsView::boundedZoomLevel(1.0), 1.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(32.0), 32.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(32.0001), 32.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(100.0), 32.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(
        std::numeric_limits<qreal>::infinity()), 32.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(-1.0), 0.01);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(
        std::numeric_limits<qreal>::quiet_NaN()), 0.01);
}

void GraphicsViewTests::testVectorFormatsUseDocumentSceneItem()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString epsPath = epsSamplePath(dir);
    const QString svgPath = svgSamplePath(dir);
    QVERIFY(!epsPath.isEmpty());
    QVERIFY(!svgPath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(800, 600);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);

    window.openFile(epsPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);
    QVERIFY(!view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    QCOMPARE(view->vectorImageFormat(), Qv::VectorImageFormat::Pdf);
    view->removeExpensiveScaling();
    QVERIFY(view->usesVectorRendering());
    view->zoomAbsolute(100.0, Qv::CalculateViewportCenterPos);
    QCOMPARE(view->getZoomLevel(), Qv::MaximumZoomLevel);
    QVERIFY(view->hasPendingVectorRefinement());
    view->viewport()->repaint();
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 500);
    view->viewport()->repaint();
    QVERIFY(!view->lastVectorRasterSize().isEmpty());
    QVERIFY(qMax(view->lastVectorRasterSize().width(),
                 view->lastVectorRasterSize().height()) > 512);
    const QSize maximumVisibleTile = view->viewport()->size()
            * view->viewport()->devicePixelRatioF() + QSize(264, 264);
    QVERIFY(view->lastVectorRasterSize().width() <= maximumVisibleTile.width());
    QVERIFY(view->lastVectorRasterSize().height() <= maximumVisibleTile.height());

    window.openFile(svgPath);
    QTRY_COMPARE_WITH_TIMEOUT(
        window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
        QFileInfo(svgPath).absoluteFilePath(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);
    QVERIFY(!view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    QCOMPARE(view->vectorImageFormat(), Qv::VectorImageFormat::Svg);
    view->removeExpensiveScaling();
    QVERIFY(view->usesVectorRendering());
    view->zoomAbsolute(100.0, Qv::CalculateViewportCenterPos);
    QCOMPARE(view->getZoomLevel(), Qv::MaximumZoomLevel);
    QVERIFY(view->hasPendingVectorRefinement());
    view->viewport()->repaint();
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 500);
    view->viewport()->repaint();
    QVERIFY(!view->lastVectorRasterSize().isEmpty());
    QVERIFY(qMax(view->lastVectorRasterSize().width(),
                 view->lastVectorRasterSize().height()) > 512);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void GraphicsViewTests::testVectorInteractionPaintPerformanceAt120Hz()
{
    ScopedSettingPreserver geometrySetting(QStringLiteral("geometry"));
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::OriginalSize)},
        {"onetoonepixelsizing", false},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QList<QPair<QString, QString>> documents {
        {QStringLiteral("eps"), epsSamplePath(dir)},
        {QStringLiteral("svg"), svgSamplePath(dir)}
    };
    for (const auto &document : documents)
        QVERIFY2(!document.second.isEmpty(), qPrintable(document.first));

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(QGuiApplication::primaryScreen()->availableGeometry().size());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    qInfo().noquote() << QStringLiteral(
        "VECTOR_120HZ_DISPLAY refresh_hz=%1 viewport=%2x%3 dpr=%4")
        .arg(window.screen() ? window.screen()->refreshRate() : 0.0, 0, 'f', 1)
        .arg(view->viewport()->width())
        .arg(view->viewport()->height())
        .arg(view->viewport()->devicePixelRatioF(), 0, 'f', 1);

    constexpr int FrameCount = 120;
    constexpr double FrameBudgetMilliseconds = 1000.0 / 120.0;
    const auto verifySamples = [&](QVector<double> samples,
                                   const QString &format,
                                   const QString &interaction) {
        std::sort(samples.begin(), samples.end());
        const double average = std::accumulate(
            samples.cbegin(), samples.cend(), 0.0) / samples.size();
        const int p99Index = qMax(
            0, static_cast<int>(qCeil(samples.size() * 0.99)) - 1);
        const double p99 = samples.at(p99Index);
        const double maximum = samples.constLast();
        const double throughput = 1000.0 / average;
        qInfo().noquote() << QStringLiteral(
            "VECTOR_120HZ format=%1 interaction=%2 average_ms=%3 p99_ms=%4 max_ms=%5 throughput_fps=%6 count=%7")
            .arg(format, interaction)
            .arg(average, 0, 'f', 3)
            .arg(p99, 0, 'f', 3)
            .arg(maximum, 0, 'f', 3)
            .arg(throughput, 0, 'f', 3)
            .arg(samples.size());
        QVERIFY2(average <= FrameBudgetMilliseconds,
                 qPrintable(format + " " + interaction + " average"));
        QVERIFY2(p99 <= FrameBudgetMilliseconds,
                 qPrintable(format + " " + interaction + " p99"));
        QVERIFY2(maximum <= FrameBudgetMilliseconds,
                 qPrintable(format + " " + interaction + " maximum"));
        QVERIFY2(throughput >= 120.0,
                 qPrintable(format + " " + interaction + " throughput"));
    };

    for (const auto &document : documents)
    {
        window.openFile(document.second);
        QTRY_COMPARE_WITH_TIMEOUT(
            window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
            QFileInfo(document.second).absoluteFilePath(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);

        for (int i = 0; i < 12; ++i)
        {
            view->zoomAbsolute(i % 2 == 0 ? 24.0 : 32.0,
                               Qv::CalculateViewportCenterPos);
            view->viewport()->repaint();
        }
        QVector<double> zoomSamples;
        zoomSamples.reserve(FrameCount);
        for (int i = 0; i < FrameCount; ++i)
        {
            QElapsedTimer frameTimer;
            frameTimer.start();
            const int triangularStep = i < FrameCount / 2
                    ? i : FrameCount - 1 - i;
            const qreal continuousZoom = 24.0
                    + triangularStep * 8.0 / (FrameCount / 2 - 1);
            view->zoomAbsolute(continuousZoom,
                               Qv::CalculateViewportCenterPos);
            view->viewport()->repaint();
            zoomSamples.append(frameTimer.nsecsElapsed() / 1000000.0);
        }
        verifySamples(zoomSamples, document.first, QStringLiteral("zoom"));

        view->zoomAbsolute(32.0, Qv::CalculateViewportCenterPos);
        view->horizontalScrollBar()->setValue(
            (view->horizontalScrollBar()->minimum()
             + view->horizontalScrollBar()->maximum()) / 2);
        view->verticalScrollBar()->setValue(
            (view->verticalScrollBar()->minimum()
             + view->verticalScrollBar()->maximum()) / 2);
        view->viewport()->repaint();
        QVector<double> panSamples;
        panSamples.reserve(FrameCount);
        for (int i = 0; i < FrameCount; ++i)
        {
            QElapsedTimer frameTimer;
            frameTimer.start();
            view->horizontalScrollBar()->setValue(
                view->horizontalScrollBar()->value() + (i % 2 == 0 ? 2 : -2));
            view->verticalScrollBar()->setValue(
                view->verticalScrollBar()->value() + (i % 3 == 0 ? 1 : -1));
            view->viewport()->repaint();
            panSamples.append(frameTimer.nsecsElapsed() / 1000000.0);
        }
        verifySamples(panSamples, document.first, QStringLiteral("pan"));
    }

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
    QCOMPARE(subtitleLabel->text(), QString("version 0.1.4"));

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
    // QTest delivers the synthetic key directly to this widget. Do not make
    // the deterministic shortcut contract depend on whether macOS lets the
    // test process steal foreground ownership from the test runner.
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

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
// Input data: a configured Space shortcut action followed by the Escape
// shortcut.
// Steps: dispatch the configured action, wait for fullscreen, then invoke the
// Escape shortcut through the same QShortcut object used by the window.
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
    // QWidget shortcuts require an active top-level. The public test hook is
    // deprecated in favor of activateWindow(), but the latter is deliberately
    // subject to macOS foreground-stealing policy and is nondeterministic in a
    // desktop test runner. Confine the compatibility hook to this test.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&window);
    QT_WARNING_POP
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);

    const auto fullscreenActions = qvApp->getActionManager().getAllClonesOfAction("fullscreen", &window);
    QVERIFY(!fullscreenActions.isEmpty());
    QAction *fullscreenAction = fullscreenActions.constFirst();
    QVERIFY(fullscreenAction);
    QCOMPARE(fullscreenAction->shortcuts(), QList<QKeySequence> {QKeySequence(Qt::Key_Space)});
    // Trigger the configured QAction directly. Qt's macOS test backend can
    // deliver synthetic Space key events to the application-wide menu rather
    // than the active QWidget, even after QApplication::setActiveWindow().
    ActionManager::actionTriggered(fullscreenAction, &window);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QShortcut *escapeShortcut = nullptr;
    for (auto *shortcut : window.findChildren<QShortcut *>()) {
        if (shortcut->key() == QKeySequence(Qt::Key_Escape)) {
            escapeShortcut = shortcut;
            break;
        }
    }
    QVERIFY(escapeShortcut);
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
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
    window.setWindowState(Qt::WindowNoState);
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

// TC-HDR-UNIT-NAV-SAMPLING-LATENCY
// Test purpose: prove hover contrast uses the bounded decoded proxy instead of
// synchronously capturing/repainting the HDR viewport.
// Preconditions: a visible split image has populated the navigation sample.
// Input data: 10,000 brightness queries at the displayed image center.
// Steps: call sampleDisplayedImageBrightness repeatedly and time the batch.
// Expected result: every query returns a value and the batch completes within
// 250 ms (at least 40,000 queries/s on the target machine).
// Postcondition: the view geometry and renderer state remain unchanged.
void WindowBehaviorTests::testNavigationBrightnessSamplingIsBounded()
{
    ScopedOptionValues options({
        {"checkerboardbackground", false},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createSplitImage(dir, "bounded-navigation-sample");
    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(800, 600);
    window.show();
    window.openFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    const QPoint samplePoint = view->viewport()->rect().center();
    QVERIFY(view->sampleDisplayedImageBrightness(samplePoint).has_value());

    QElapsedTimer timer;
    timer.start();
    int validCount = 0;
    constexpr int Iterations = 10000;
    for (int index = 0; index < Iterations; ++index)
        validCount += view->sampleDisplayedImageBrightness(samplePoint).has_value();
    const qreal elapsedMilliseconds = timer.nsecsElapsed() / 1000000.0;
    qInfo("FOVELLE_NAV_SAMPLE iterations=%d elapsed_ms=%.6f", Iterations,
          static_cast<double>(elapsedMilliseconds));
    QCOMPARE(validCount, Iterations);
    QVERIFY2(elapsedMilliseconds <= 250.0,
             qPrintable(QString::number(elapsedMilliseconds, 'f', 3)));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-HDR-UNIT-NAV-TRANSPARENT-FADE
// Test purpose: verify navigation fades only its rounded/chevron pixels and
// never allocates the rectangular QGraphicsOpacityEffect surface seen over HDR.
// Preconditions: MainWindow has created both navigation buttons/animations.
// Input data: both buttons at 50% paintOpacity rendered onto transparent ARGB.
// Steps: inspect attributes/effects/animation targets and rendered corner alpha.
// Expected result: effects are null, transparent/no-system-background flags are
// set, animations target paintOpacity, corners stay transparent, and artwork
// contains partially opaque pixels.
// Postcondition: temporary widgets/images are destroyed without settings writes.
void WindowBehaviorTests::testNavigationButtonUsesTransparentPaintOnlyFade()
{
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *previousButton = window.findChild<QPushButton *>("previousImageButton");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    auto *previousAnimation =
            window.findChild<QPropertyAnimation *>("previousImageButtonOpacityAnimation");
    auto *nextAnimation =
            window.findChild<QPropertyAnimation *>("nextImageButtonOpacityAnimation");
    QVERIFY(previousButton);
    QVERIFY(nextButton);
    QVERIFY(previousAnimation);
    QVERIFY(nextAnimation);

    for (QPushButton *button : { previousButton, nextButton }) {
        QVERIFY(button->graphicsEffect() == nullptr);
        QVERIFY(button->testAttribute(Qt::WA_TranslucentBackground));
        QVERIFY(button->testAttribute(Qt::WA_NoSystemBackground));
        QVERIFY(!button->autoFillBackground());
        button->setProperty("paintOpacity", 0.5);
        QImage rendered(button->size(), QImage::Format_ARGB32_Premultiplied);
        rendered.fill(Qt::transparent);
        button->render(&rendered);
        QCOMPARE(rendered.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(rendered.pixelColor(rendered.width() - 1, 0).alpha(), 0);
        QCOMPARE(rendered.pixelColor(0, rendered.height() - 1).alpha(), 0);
        QCOMPARE(rendered.pixelColor(rendered.width() - 1,
                                     rendered.height() - 1).alpha(), 0);
        int partialPixelCount = 0;
        for (int y = 0; y < rendered.height(); ++y)
            for (int x = 0; x < rendered.width(); ++x) {
                const int alpha = rendered.pixelColor(x, y).alpha();
                partialPixelCount += alpha > 0 && alpha < 255;
            }
        QVERIFY(partialPixelCount > 0);
    }
    QCOMPARE(previousAnimation->propertyName(), QByteArray("paintOpacity"));
    QCOMPARE(nextAnimation->propertyName(), QByteArray("paintOpacity"));
    window.close();
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
    // The immediate visibility assertion above is the deterministic
    // mid-transition contract. Do not assume that a wall-clock 100 ms wait
    // is shorter than the animation on every hosted macOS display backend.
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
    QCoreApplication::setApplicationVersion("0.1.4");
    QVApplication app(argc, argv);
    qRegisterMetaType<QVImageLoader::Result>();

    ImageLoaderTests imageLoaderTests;
    FeatureTests featureTests;
    HDRPolicyTests hdrPolicyTests;
    HDRSampleTests hdrSampleTests;
    SDRSampleInteractionTests sdrSampleInteractionTests;
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
    result |= runSuite("HDRPolicyTests", &hdrPolicyTests);
    result |= runSuite("GraphicsViewTests", &graphicsViewTests);
    result |= runSuite("ApplicationEventTests", &applicationEventTests);
    result |= runSuite("ImageCoreAndMovieTests", &imageCoreAndMovieTests);
    result |= runSuite("WindowBehaviorTests", &windowBehaviorTests);
    if (selectedSuite == "HDRSampleTests")
        result |= QTest::qExec(&hdrSampleTests, argc, argv);
    if (selectedSuite == "SDRSampleInteractionTests")
        result |= QTest::qExec(&sdrSampleInteractionTests, argc, argv);
    return result;
}

#include "tst_qviewtests.moc"
