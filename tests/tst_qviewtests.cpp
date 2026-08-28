#include <QtTest>
#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <time.h>
#include <QFileInfo>
#include <QDir>
#include <QFileOpenEvent>
#include <QImage>
#include <QInputDialog>
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
#include <QDoubleSpinBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QPalette>
#include <QSignalSpy>
#include <QSettings>
#include <QStyleOptionGraphicsItem>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>
#include <QWheelEvent>
#include <QScrollBar>
#include <QScrollArea>
#include <QSet>
#include <QTabBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QPointer>
#include <QMenu>
#include <QStackedWidget>
#include <QTranslator>

#include "mainwindow.h"
#include "actionmanager.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvgraphicsimageitem.h"
#include "qvgraphicsview.h"
#include "qvimagecore.h"
#include "qvimageloader.h"
#include "qvmovie.h"
#include "qvoptionsdialog.h"
#include "qvshortcutdialog.h"
#include "settingsmanager.h"
#include "qvinfodialog.h"
#include "qvaboutdialog.h"
#include "nativedialogs.h"

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
    void testVectorInteractionPreservesTerminalDensity();
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
    void testViewMenuRemovesLegacyActions();
    void testSettingsFormatsIncludeNativeImageFormats();
    void testSettingsFormatsPaneIsRemoved();
    void testSettingsGeneralLanguageAndRemovedOptions();
    void testSettingsMouseCursorPanelIsRemoved();
    void testSettingsLanguageCatalogIsFixed();
    void testSettingsLanguageDefaultsToSystem();
    void testSystemLanguageMappingFallsBackToEnglish();
    void testAutoUpdateCheckLabelIsRenamed();
    void testSettingsRenamedLabelsAndRemovedMouseOptions();
    void testRemovedMouseSettingsMigrateToFixedDefaults();
    void testAssociateAllSupportedFormatsDryRun();
    void testPreferencesDefaultsAndRemovedControls();
    void testSettingsGeneralGroupsAndDefaults();
    void testSettingsCooldownOptionIsRemovedAndDefaultEnabled();
    void testUpdateCheckFrequencyPolicy();
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
    void testSDRTilePlacementConvertsTopRowsToCoreImageCoordinates();
    void testSDR180FPSPolicyRequiresCapableDisplay();
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
    void testNEFProxyOrientationMatchesNativeGraph();
};

class SDRSampleInteractionTests : public QObject
{
    Q_OBJECT

private slots:
    void testProvidedSamplesUseMacOSPanPresentationPolicy();
    void testProvidedRasterStaysAuthoritativeDuringInteraction();
    void testProvidedRaster120HzInteractionProbe();
    void testProvidedSamplesPerformanceProbe();
    void testLargeNeighborPreloadShutdownProbe();
    void testLargeRasterWindowCaptureHasNoBlackTileBlock();
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
    void testFullscreenAfterOverflowRemovesTitlebarScenePadding();
    void testManualZoomRemainsManualAcrossResize();
    void testSmallImageOneToOnePolicyUsesViewportAndWindowMode();
    void testSmallImageOneToOneAppliedWhenOpeningAndBrowsingImages();
    void testNativePinchZoomChangesScaleAtGesturePosition();
    void testNativePanChangesViewport();
    void testScrollBarsFollowImageOverflowAxes();
    void testScrollBarsMatchTheme();
    void testNativeGestureResponsePerformance();
    void testRasterPanUsesCompleteRepaintOnMacOS();
    void testZoomIsBoundedAt6400Percent();
    void testVectorPanRepaintsOnlyExposedStrip();
    void testVectorFormatsUseDocumentSceneItem();
    void testVectorInteractionPaintCpuBudgetFor120Hz();
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
    void testEscapeIsReservedForWindowLifecycle();
    void testPracticalTitlebarTextUsesFilenameAndSequence();
    void testDefaultTitlebarTextIsPractical();
    void testVerboseTitlebarTextUsesAllRequestedFields();
    void testThemeSettingsReplaceRemovedColorControls();
    void testSettingsDialogUsesNativeTabContractAndImmediatePersistence();
    void testSettingsDialogUsesFixedWidthAndTabHeights();
    void testSettingsTabTransitionAndMouseReopen();
    void testSettingsTabSwitchDoesNotFocusAppearance();
    void testLocalizedSettingsFormsUseSharedLabelColumns();
    void testSettingsDialogSizesFollowTranslations();
    void testMacMenuTranslationCatalogsAreComplete();
    void testFullscreenMenuIconsRespectMainMenuPolicy();
    void testExitFullscreenActionUsesEscapePath();
    void testOptionsDialogCentersOnMainWindow();
    void testSettingsDialogIsNativeChildAboveMainWindow();
    void testAssociateFormatsButtonIsCentered();
    void testTitlebarHiddenPersistsToNewWindow();
    void testSmoothScalingDefaultIsBilinear();
    void testSettingsFormsAlignLabelsAndValues();
    void testSettingsColonAlignmentSurvivesTranslations();
    void testSettingsSpacingUsesNativeStyle();
    void testSettingsAssociateButtonUsesNativeStyle();
    void testSettingsEveryTabFitsEveryLanguage();
    void testNewWindowStartsMaximized();
    void testSystemThemeResolvesFromControlledAppearance();
    void testHelpMenuContract();
    void testEditMenuRemovesMacOSServiceItems();
    void testNativeDialogsFollowSelectedTheme();
    void testOpenUrlDialogFollowsSelectedTheme();
    void testThemeAppliesNativeAppearanceAndViewportBackground();
    void testCheckerboardOverridesThemeAndRestoresBackground();
    void testNavigationEdgeActivationExcludesTitlebar();
    void testNavigationButtonSizingAndNoDelay();
    void testNavigationArtworkStylesAreSingleCompositedButtons();
    void testNavigationButtonsUseActualContentContrast();
    void testNavigationBrightnessSamplingIsBounded();
    void testNavigationButtonUsesTransparentPaintOnlyFade();
    void testNavigationButtonsFadeTransition();
    void testNavigationButtonsClickSwitchesFiles();
};

class ShortcutSettingsTests : public QObject
{
    Q_OBJECT

private slots:
    void testPrimaryStandardShortcutDoesNotExposeActionName();
    void testShortcutsColumnFillsRemainingWidth();
    void testDoubleClickOpensShortcutEditor();
    void testShortcutUpdateKeepsSettingsWidth();
    void testEscapeRejectsShortcutEditorLikeCancel();
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

class OptionsDialogPresentationRecorder : public QObject
{
public:
    bool wasShown() const { return shown; }
    QRect firstShownFrame() const { return shownFrame; }
    int moveEventsAfterShow() const { return movesAfterShow; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *dialog = qobject_cast<QVOptionsDialog *>(watched);
        if (!dialog)
            return false;

        if (event->type() == QEvent::Show && !shown)
        {
            shown = true;
            shownFrame = dialog->frameGeometry();
            qInfo() << "OPTIONS_FIRST_SHOW_FRAME" << shownFrame;
        }
        else if (event->type() == QEvent::Move && shown)
        {
            ++movesAfterShow;
        }
        return false;
    }

private:
    bool shown {false};
    QRect shownFrame;
    int movesAfterShow {0};
};

class FullScreenExitGeometryRecorder : public QObject
{
public:
    int geometryEventsAfterExit() const { return postExitGeometryEvents; }

    void reset()
    {
        exitObserved = false;
        postExitGeometryEvents = 0;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *window = qobject_cast<MainWindow *>(watched);
        if (!window)
            return false;

        if (event->type() == QEvent::WindowStateChange)
        {
            const auto *stateEvent = static_cast<QWindowStateChangeEvent *>(event);
            if (stateEvent->oldState().testFlag(Qt::WindowFullScreen)
                && !window->windowState().testFlag(Qt::WindowFullScreen))
                exitObserved = true;
        }
        else if (exitObserved
                 && (event->type() == QEvent::Move
                     || event->type() == QEvent::Resize))
        {
            ++postExitGeometryEvents;
        }
        return false;
    }

private:
    bool exitObserved {false};
    int postExitGeometryEvents {0};
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

class SourceLanguageTranslator final : public QTranslator
{
public:
    bool isEmpty() const override
    {
        return false;
    }

    QString translate(const char *context, const char *sourceText,
                      const char *disambiguation = nullptr,
                      int n = -1) const override
    {
        Q_UNUSED(context)
        Q_UNUSED(disambiguation)
        Q_UNUSED(n)
        return QString::fromUtf8(sourceText);
    }
};

static QString settingsLabelColumnFailure(QWidget *page)
{
    if (!page)
        return QStringLiteral("settings page is missing");

    int expectedRight = -1;
    QChar expectedPunctuation;
    int visibleLabelCount = 0;
    for (auto *label : page->findChildren<QLabel *>())
    {
        if (!label->isVisible())
            continue;

        const QString text = label->text().trimmed();
        if (text.isEmpty())
            continue;

        if (!text.endsWith(QLatin1Char(':'))
            && !text.endsWith(QChar(0xFF1A)))
            continue;

        ++visibleLabelCount;
        const QChar punctuation = text.back();
        if (punctuation != QLatin1Char(':'))
            return QStringLiteral("label %1 does not use the English ASCII U+003A colon")
                .arg(label->objectName());
        if (expectedPunctuation.isNull())
            expectedPunctuation = punctuation;
        else if (punctuation != expectedPunctuation)
            return QStringLiteral("labels use mixed terminal punctuation: %1 and %2")
                .arg(QString::number(expectedPunctuation.unicode(), 16))
                .arg(QString::number(punctuation.unicode(), 16));

        const int right = label->mapTo(page, QPoint(label->width(), 0)).x();
        if (expectedRight < 0)
            expectedRight = right;
        else if (right != expectedRight)
            return QStringLiteral("label %1 ends at x=%2, expected x=%3")
                .arg(label->objectName())
                .arg(right)
                .arg(expectedRight);

        const Qt::Alignment alignment = label->alignment();
        if (!(alignment & Qt::AlignRight) || !(alignment & Qt::AlignTrailing))
            return QStringLiteral("label %1 is not right/trailing aligned")
                .arg(label->objectName());
    }

    return visibleLabelCount > 0
        ? QString()
        : QStringLiteral("no visible colon-terminated settings labels found");
}

class ScopedEnvironmentValue
{
public:
    explicit ScopedEnvironmentValue(const QByteArray &name)
        : variableName(name), existed(qEnvironmentVariableIsSet(name.constData())), value(qgetenv(name.constData()))
    {
    }

    ~ScopedEnvironmentValue()
    {
        if (existed)
            qputenv(variableName, value);
        else
            qunsetenv(variableName);
    }

private:
    QByteArray variableName;
    bool existed;
    QByteArray value;
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

static std::optional<qint64> currentThreadCpuTimeNanoseconds()
{
#ifdef CLOCK_THREAD_CPUTIME_ID
    timespec value{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0)
        return std::nullopt;
    return static_cast<qint64>(value.tv_sec) * 1000000000LL + static_cast<qint64>(value.tv_nsec);
#else
    return std::nullopt;
#endif
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
    QStyleOptionGraphicsItem sceneOption;
    sceneOption.exposedRect = sceneItem.boundingRect();
    const auto paintSceneItem = [&]() {
        sceneRender.fill(Qt::transparent);
        QPainter scenePainter(&sceneRender);
        scenePainter.scale(
            renderedImage.width() / result.vectorImage.logicalSize.width(),
            renderedImage.height() / result.vectorImage.logicalSize.height());
        sceneItem.paint(&scenePainter, &sceneOption);
    };
    paintSceneItem();
    QElapsedTimer exactTileTimer;
    exactTileTimer.start();
    while (sceneItem.vectorRenderCount() == 0 && exactTileTimer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
        paintSceneItem();
    }
    QVERIFY2(sceneItem.vectorRenderCount() > 0,
             "the asynchronous exact PDF tile did not become paintable");
    QVERIFY(sampledChannelDifference(sceneRender, renderedImage) < 3.0);
    const quint64 pdfTileGenerationCount =
            sceneItem.vectorTileGenerationCount();
    for (int repaint = 0; repaint < 8; ++repaint)
        paintSceneItem();
    QTest::qWait(50);
    QCoreApplication::processEvents();
    QCOMPARE(sceneItem.vectorTileGenerationCount(), pdfTileGenerationCount);

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
    QStyleOptionGraphicsItem sceneOption;
    sceneOption.exposedRect = sceneItem.boundingRect();
    const auto paintSceneItem = [&]() {
        sceneRender.fill(Qt::transparent);
        QPainter scenePainter(&sceneRender);
        scenePainter.scale(
            renderedSize.width() / result->vectorImage.logicalSize.width(),
            renderedSize.height() / result->vectorImage.logicalSize.height());
        sceneItem.paint(&scenePainter, &sceneOption);
    };
    paintSceneItem();
    QElapsedTimer exactTileTimer;
    exactTileTimer.start();
    while (sceneItem.vectorRenderCount() == 0 && exactTileTimer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qWait(5);
        paintSceneItem();
    }
    QVERIFY2(sceneItem.vectorRenderCount() > 0,
             "the asynchronous exact SVG tile did not become paintable");
    QVERIFY(sampledChannelDifference(sceneRender, reference) < 3.0);
    const quint64 svgTileGenerationCount =
            sceneItem.vectorTileGenerationCount();
    for (int repaint = 0; repaint < 8; ++repaint)
        paintSceneItem();
    QTest::qWait(50);
    QCoreApplication::processEvents();
    QCOMPARE(sceneItem.vectorTileGenerationCount(), svgTileGenerationCount);
}

// TC-EPS-UNIT-INTERACTION-QUALITY
// Test purpose: ensure the temporary tile used while panning is not rendered
// below the display density, which would make EPS and SVG edges visibly
// pixelated until the gesture ends.
// Preconditions: the deterministic EPS/SVG fixtures (or configured samples)
// are readable and the asynchronous vector worker can run.
// Input data: one EPS and one SVG rendered at a 2048-pixel terminal size.
// Operation steps: enable vector interaction, paint until its worker tile is
// available, then compare it with a direct full-resolution vector reference.
// Expected result: the interaction tile is at least the target pixel size and
// its displayed pixels remain within the existing vector-render tolerance.
// Postconditions: all temporary vector documents and worker resources are released.
void ImageLoaderTests::testVectorInteractionPreservesTerminalDensity()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QList<QPair<QString, QString>> documents {
        {QStringLiteral("eps"), epsSamplePath(dir)},
        {QStringLiteral("svg"), svgSamplePath(dir)}
    };
    for (const auto &document : documents)
    {
        QVERIFY2(!document.second.isEmpty(), qPrintable(document.first));
        const auto result = loadImage(document.second);
        QVERIFY2(result.has_value(), qPrintable(document.first));
        QVERIFY2(result->vectorImage.isValid(), qPrintable(document.first));

        const QSize renderedSize = result->intrinsicSize.scaled(
            2048, 2048, Qt::KeepAspectRatio);
        QVERIFY(!renderedSize.isEmpty());

        QImage reference(renderedSize, QImage::Format_ARGB32_Premultiplied);
        reference.fill(Qt::transparent);
        if (result->vectorImage.format == Qv::VectorImageFormat::Pdf)
        {
            QString pdfError;
            const auto pdfDocument = QVCocoaFunctions::createPDFVectorDocument(
                result->vectorImage.encodedData, &pdfError);
            QVERIFY2(pdfDocument, qPrintable(pdfError));
            const QImage renderedReference = pdfDocument->renderTile(
                result->vectorImage.logicalSize,
                QRectF(QPointF(), result->vectorImage.logicalSize),
                renderedSize, &pdfError);
            QVERIFY2(!renderedReference.isNull(), qPrintable(pdfError));
            reference = renderedReference;
        }
        else
        {
            QSvgRenderer referenceRenderer(document.second);
            QVERIFY(referenceRenderer.isValid());
            QPainter referencePainter(&reference);
            referencePainter.setRenderHint(QPainter::Antialiasing, true);
            referencePainter.setRenderHint(QPainter::TextAntialiasing, true);
            referenceRenderer.render(
                &referencePainter, QRectF(QPointF(), QSizeF(renderedSize)));
        }

        QVGraphicsImageItem sceneItem;
        sceneItem.setPixmap(QPixmap::fromImage(result->image));
        QVERIFY(sceneItem.setVectorImage(result->vectorImage));
        sceneItem.setVectorInteractionActive(true);

        QImage actual(renderedSize, QImage::Format_ARGB32_Premultiplied);
        QStyleOptionGraphicsItem sceneOption;
        sceneOption.exposedRect = sceneItem.boundingRect();
        const auto paintSceneItem = [&]() {
            actual.fill(Qt::transparent);
            QPainter scenePainter(&actual);
            scenePainter.scale(
                static_cast<qreal>(renderedSize.width())
                    / sceneItem.boundingRect().width(),
                static_cast<qreal>(renderedSize.height())
                    / sceneItem.boundingRect().height());
            sceneItem.paint(&scenePainter, &sceneOption);
        };

        paintSceneItem();
        QElapsedTimer renderTimer;
        renderTimer.start();
        while (sceneItem.vectorRenderCount() == 0
               && renderTimer.elapsed() < 5000)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QTest::qWait(5);
            paintSceneItem();
        }
        QVERIFY2(sceneItem.vectorRenderCount() > 0,
                 qPrintable(document.first
                            + QStringLiteral(" interaction tile did not render")));
        QVERIFY2(sceneItem.lastVectorRasterSize().width() >= renderedSize.width()
                     && sceneItem.lastVectorRasterSize().height() >= renderedSize.height(),
                 qPrintable(document.first
                            + QStringLiteral(" interaction tile is undersampled: ")
                            + QStringLiteral("%1x%2")
                                  .arg(sceneItem.lastVectorRasterSize().width())
                                  .arg(sceneItem.lastVectorRasterSize().height())));
        QVERIFY2(sampledChannelDifference(actual, reference) < 3.0,
                 qPrintable(document.first
                            + QStringLiteral(" interaction tile differs from reference")));
    }
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
// retained as a full-resolution native graph while its Qt placeholder stays
// bounded.
// Preconditions: Image I/O supports PNG; a temporary directory is writable;
// the deterministic fixture is larger than the loader's 1920px default hint.
// Input data: a 2400x1600 one-pixel checkerboard PNG.
// Steps: decode through Image I/O with a small hint, then load through the
// asynchronous QVImageLoader using its production default.
// Expected result: the direct unbounded bridge can still return 2400x1600;
// production loading reports that intrinsic size through SDRImage while the
// immediately paintable Qt proxy is no larger than 1920 pixels.
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
    QVERIFY(nativeResult.sdrImage);
    QCOMPARE(nativeResult.sdrImage->pixelSize(), sourceSize);
    QCOMPARE(nativeResult.image.size(), sourceSize);
    QVERIFY(nativeResult.image.pixelColor(0, 0) != nativeResult.image.pixelColor(1, 0));

    const auto loaderResult = loadImage(path);
    QVERIFY(loaderResult.has_value());
    QVERIFY(!loaderResult->errorData.has_value());
    QCOMPARE(loaderResult->intrinsicSize, sourceSize);
    QVERIFY(loaderResult->sdrImage);
    QCOMPARE(loaderResult->sdrImage->pixelSize(), sourceSize);
    QVERIFY(!loaderResult->image.isNull());
    QCOMPARE(qMax(loaderResult->image.width(), loaderResult->image.height()), 1920);
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

// TC-SDR-TILE-COORDINATES
// Test purpose: keep the CGImage top-row crop contract separate from Core
// Image's bottom-left composition contract for sources larger than one texture.
void HDRPolicyTests::testSDRTilePlacementConvertsTopRowsToCoreImageCoordinates()
{
    constexpr int sourceHeight = 17646;
    QCOMPARE(QVCocoaFunctions::coreImageTileRect(
                     QRect(0, 0, 2048, 2048), sourceHeight),
             QRect(0, 15598, 2048, 2048));
    QCOMPARE(QVCocoaFunctions::coreImageTileRect(
                     QRect(2048, 16384, 2048, 1262), sourceHeight),
             QRect(2048, 0, 2048, 1262));
    QCOMPARE(QVCocoaFunctions::coreImageTileRect(
                     QRect(2046, 2046, 2052, 2052), sourceHeight),
             QRect(2046, 13548, 2052, 2052));
    QVERIFY(QVCocoaFunctions::coreImageTileRect(
                    QRect(0, sourceHeight, 1, 1), sourceHeight).isEmpty());
}

// TC-SDR-180FPS-POLICY
// Test purpose: distinguish renderer frame budget from the physical display
// ceiling while unlocking 180...240 Hz presentation on capable screens.
void HDRPolicyTests::testSDR180FPSPolicyRequiresCapableDisplay()
{
    const auto builtIn120 = QVCocoaFunctions::sdrFrameRatePolicy(120.0);
    QCOMPARE(builtIn120.minimum, 120.0);
    QCOMPARE(builtIn120.maximum, 120.0);
    QCOMPARE(builtIn120.preferred, 120.0);
    QVERIFY(!builtIn120.displayCanPresent180FPS);

    const auto minimumCapable = QVCocoaFunctions::sdrFrameRatePolicy(180.0);
    QCOMPARE(minimumCapable.minimum, 180.0);
    QCOMPARE(minimumCapable.maximum, 180.0);
    QCOMPARE(minimumCapable.preferred, 180.0);
    QVERIFY(minimumCapable.displayCanPresent180FPS);

    const auto highRefresh = QVCocoaFunctions::sdrFrameRatePolicy(240.0);
    QCOMPARE(highRefresh.minimum, 180.0);
    QCOMPARE(highRefresh.maximum, 240.0);
    QCOMPARE(highRefresh.preferred, 240.0);
    QVERIFY(highRefresh.displayCanPresent180FPS);

    const auto boundedHighRefresh = QVCocoaFunctions::sdrFrameRatePolicy(360.0);
    QCOMPARE(boundedHighRefresh.minimum, 180.0);
    QCOMPARE(boundedHighRefresh.maximum, 240.0);
    QCOMPARE(boundedHighRefresh.preferred, 240.0);
    QVERIFY(boundedHighRefresh.displayCanPresent180FPS);
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

// TC-HDR-INT-RAW-NEF-ORIENTATION
// Test purpose: ensure the bounded cold-open proxy and the native RAW graph
// use the same EXIF-oriented coordinate space.
// Preconditions: FOVELLE_HDR_NEF_SAMPLE points to a readable NEF.
// Input data: one NEF and a 2048-pixel fallback limit.
// Steps: decode through the production path and compare proxy/native shape.
// Expected result: both representations are portrait or landscape together,
// with no 90-degree transposition.
// Postcondition: native graphs and proxy pixels are released.
void HDRSampleTests::testNEFProxyOrientationMatchesNativeGraph()
{
    const QString path = QString::fromUtf8(qgetenv("FOVELLE_HDR_NEF_SAMPLE"));
    QVERIFY2(!path.isEmpty() && QFileInfo::exists(path), qPrintable(path));
    const auto result = QVCocoaFunctions::readImageWithImageIO(path, 2048);
    QVERIFY2(result.errorString.isEmpty(), qPrintable(result.errorString));
    QVERIFY(result.isRaw);
    QVERIFY(result.hdrImage);
    QVERIFY(!result.image.isNull());
    QVERIFY(result.intrinsicSize.isValid());
    QVERIFY(result.intrinsicSize.width() > 0);
    QVERIFY(result.intrinsicSize.height() > 0);

    const bool proxyPortrait = result.image.height() > result.image.width();
    const bool nativePortrait = result.intrinsicSize.height()
            > result.intrinsicSize.width();
    QCOMPARE(proxyPortrait, nativePortrait);

    const double proxyAspect = static_cast<double>(result.image.width())
            / result.image.height();
    const double nativeAspect = static_cast<double>(result.intrinsicSize.width())
            / result.intrinsicSize.height();
    QVERIFY2(qAbs(proxyAspect - nativeAspect) < 0.02,
             qPrintable(QStringLiteral("proxy=%1 native=%2")
                                .arg(proxyAspect, 0, 'f', 6)
                                .arg(nativeAspect, 0, 'f', 6)));
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
    QCOMPARE(QCoreApplication::applicationVersion(), QString("1.0.1"));
}

// TC-APP-VERSION
// Test purpose: verify the application reports the released semantic version.
// Preconditions: the QVApplication has been constructed with the CMake version
// definitions.
// Input data: QCoreApplication::applicationVersion().
// Steps: read the runtime application version.
// Expected result: the value is exactly 1.0.1.
// Postcondition: no application or settings state changes.
void FeatureTests::testApplicationVersionIsCurrent()
{
    QCOMPARE(QCoreApplication::applicationVersion(), QString("1.0.1"));
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

// TC-VIEW-LEGACY-ACTIONS
// Test purpose: verify that the View menu no longer exposes the two removed
// image/window sizing actions while retaining Full Screen.
// Preconditions: ActionManager has built the production menu clones.
// Input data: every action in each non-context View menu clone.
// Steps: inspect action data keys and classify the main-menu View clone.
// Expected result: navresetszoom and matchimagesize are absent; fullscreen is
// still present in the main menu.
// Postcondition: no menu or settings state is changed.
void FeatureTests::testViewMenuRemovesLegacyActions()
{
    bool foundMainMenu = false;
    bool foundFullscreen = false;
    const auto viewMenus = qvApp->getActionManager().getAllClonesOfMenu("view");
    QVERIFY(!viewMenus.isEmpty());

    for (const auto *viewMenu : viewMenus)
    {
        if (viewMenu->property("isContextMenu").toBool())
            continue;

        foundMainMenu = true;
        for (const auto *action : viewMenu->actions())
        {
            const QString key = action->data().toStringList().value(0);
            QVERIFY(key != QStringLiteral("navresetszoom"));
            QVERIFY(key != QStringLiteral("matchimagesize"));
            if (key == QStringLiteral("fullscreen"))
                foundFullscreen = true;
        }
    }

    QVERIFY(foundMainMenu);
    QVERIFY(foundFullscreen);
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

// TC-PREFERENCES-FORMATS-REMOVED
// Test purpose: verify the Formats page and its old table are removed while
// the native image-extension registry remains available to the application.
// Preconditions: the application has initialized its Image I/O-backed
// extension registry and the options dialog can be constructed.
// Input data: the production QVOptionsDialog object tree and category model.
// Steps: construct the dialog, inspect its categories and search for the old
// Formats page/table object names.
// Expected result: exactly three categories exist; General replaces the former
// Display/Miscellaneous pair; Formats, formats, and
// formatsTable do not exist; native extensions remain advertised.
// Postcondition: the dialog is destroyed without changing user settings.
void FeatureTests::testSettingsFormatsPaneIsRemoved()
{
    QVERIFY(qvApp->getAllFileExtensionList().contains(".webp"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".avif"));

    QVOptionsDialog dialog;
    auto *tabs = dialog.findChild<QTabBar *>("categoryTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QStringList categoryTexts;
    for (int index = 0; index < tabs->count(); ++index)
        categoryTexts.append(tabs->tabText(index));
    QCOMPARE(categoryTexts, QStringList({QStringLiteral("General"), QStringLiteral("Shortcuts"),
                                         QStringLiteral("Mouse")}));
    QVERIFY(!categoryTexts.contains(QStringLiteral("Formats")));
    QVERIFY(!dialog.findChild<QWidget *>("formats"));
    QVERIFY(!dialog.findChild<QTableWidget *>("formatsTable"));
}

// TC-SETTINGS-GENERAL-CONTENTS
// Test purpose: verify the General page owns the former Display and
// Miscellaneous controls while the removed sorting/preloading controls are
// absent from the production object tree.
// Preconditions: the QVApplication and SettingsManager are initialized.
// Input data: a freshly constructed QVOptionsDialog.
// Steps: inspect the native category model, General controls, and removed
// object names.
// Expected result: the first category is General; Language is present; the
// sorting, ascending/descending, and preloading controls are absent; the
// preloading default remains Adjacent in SettingsManager.
// Postcondition: the dialog is destroyed without changing user settings.
void FeatureTests::testSettingsGeneralLanguageAndRemovedOptions()
{
    QVOptionsDialog dialog;
    auto *tabs = dialog.findChild<QTabBar *>("categoryTabs");
    auto *language = dialog.findChild<QComboBox *>("langComboBox");
    QVERIFY(tabs);
    QVERIFY(language);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("General"));
    QVERIFY(dialog.findChild<QLabel *>("langComboLabel"));
    QVERIFY(!dialog.findChild<QWidget *>("sortComboBox"));
    QVERIFY(!dialog.findChild<QWidget *>("descendingRadioButton0"));
    QVERIFY(!dialog.findChild<QWidget *>("descendingRadioButton1"));
    QVERIFY(!dialog.findChild<QWidget *>("preloadingComboBox"));
    QCOMPARE(qvApp->getSettingsManager().getEnum<Qv::PreloadMode>("preloadingmode"),
             Qv::PreloadMode::Adjacent);
}

// TC-SETTINGS-MOUSE-CURSOR-REMOVED
// Test purpose: verify the Mouse page no longer exposes the obsolete Cursor
// panel or either of its auto-hide controls.
// Preconditions: the production options dialog can be constructed.
// Input data: the Mouse page object tree.
// Steps: inspect the named group, controls, and form layout objects.
// Expected result: Cursor, cursorGroup, cursorLayout, and both auto-hide
// controls are absent from the Settings dialog.
// Postcondition: the dialog is destroyed without changing cursor behavior
// settings used by the graphics view.
void FeatureTests::testSettingsMouseCursorPanelIsRemoved()
{
    QVOptionsDialog dialog;
    QVERIFY(!dialog.findChild<QGroupBox *>(QStringLiteral("cursorGroup")));
    QVERIFY(!dialog.findChild<QFormLayout *>(QStringLiteral("cursorLayout")));
    QVERIFY(!dialog.findChild<QCheckBox *>(QStringLiteral("cursorAutoHideFullscreenCheckbox")));
    QVERIFY(!dialog.findChild<QDoubleSpinBox *>(QStringLiteral("cursorAutoHideFullscreenDelaySpinBox")));
}

// TC-SETTINGS-LANGUAGE-CATALOG
// Test purpose: verify the language selector exposes exactly the requested
// five languages plus System Language in the required order and labels.
// Preconditions: the production options dialog can be constructed.
// Input data: the langComboBox contents.
// Steps: enumerate every visible item and its persisted language code.
// Expected result: System Language is first, followed by English, Simplified
// Chinese, Traditional Chinese, Spanish, and Japanese.
// Postcondition: no settings are changed.
void FeatureTests::testSettingsLanguageCatalogIsFixed()
{
    ScopedOptionValues options({{"language", QStringLiteral("system")}});
    QVOptionsDialog dialog;
    auto *language = dialog.findChild<QComboBox *>("langComboBox");
    QVERIFY(language);
    QCOMPARE(language->count(), 6);
    const QStringList expectedLabels {QStringLiteral("System Language"),
                                      QStringLiteral("English"),
                                      QStringLiteral("简体中文"),
                                      QStringLiteral("繁體中文"),
                                      QStringLiteral("Español"),
                                      QStringLiteral("日本語")};
    for (int index = 0; index < expectedLabels.size(); ++index)
        QCOMPARE(language->itemText(index), expectedLabels.value(index));
    QCOMPARE(language->itemData(0).toString(), QStringLiteral("system"));
    QCOMPARE(language->itemData(1).toString(), QStringLiteral("en"));
    QCOMPARE(language->itemData(2).toString(), QStringLiteral("zh_Hans"));
    QCOMPARE(language->itemData(3).toString(), QStringLiteral("zh_Hant"));
    QCOMPARE(language->itemData(4).toString(), QStringLiteral("es"));
    QCOMPARE(language->itemData(5).toString(), QStringLiteral("ja"));
    QCOMPARE(language->currentData().toString(), QStringLiteral("system"));
}

// TC-SETTINGS-LANGUAGE-DEFAULT
// Test purpose: verify a new installation's Language default is System
// Language rather than a hard-coded English selection.
// Preconditions: SettingsManager has initialized its default-value library.
// Input data: the language setting requested with defaults=true.
// Steps: read the default value without consulting persisted user settings.
// Expected result: the default language code is system.
// Postcondition: no persistent setting is modified.
void FeatureTests::testSettingsLanguageDefaultsToSystem()
{
    QCOMPARE(qvApp->getSettingsManager().getString(QStringLiteral("language"), true),
             QStringLiteral("system"));
}

// TC-SETTINGS-SYSTEM-LANGUAGE-FALLBACK
// Test purpose: verify supported system locales map to the five allowed
// language codes and an unsupported locale deterministically maps to English.
// Preconditions: the pure SettingsManager locale mapper is available.
// Input data: representative English, Simplified Chinese, Traditional
// Chinese, Spanish, Japanese, and German locales.
// Steps: call languageCodeForLocale for each controlled locale.
// Expected result: supported locales map to their catalog code; German, which
// is outside the application list, maps to en.
// Postcondition: no process locale or persistent setting is changed.
void FeatureTests::testSystemLanguageMappingFallsBackToEnglish()
{
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("en-US"))), QStringLiteral("en"));
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("zh-CN"))), QStringLiteral("zh_Hans"));
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("zh-TW"))), QStringLiteral("zh_Hant"));
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("es-ES"))), QStringLiteral("es"));
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("ja-JP"))), QStringLiteral("ja"));
    QCOMPARE(SettingsManager::languageCodeForLocale(QLocale(QStringLiteral("de-DE"))), QStringLiteral("en"));
}

// TC-SETTINGS-AUTO-UPDATE-LABEL
// Test purpose: verify the update-frequency label uses the new concise
// English wording and no longer exposes the former phrase.
// Preconditions: the production options dialog can be constructed in English.
// Input data: the updateFrequencyLabel text from a fresh dialog.
// Steps: construct the dialog and inspect the label text.
// Expected result: the label is exactly "Auto update check:".
// Postcondition: the dialog is destroyed without changing update settings.
void FeatureTests::testAutoUpdateCheckLabelIsRenamed()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});
    QVOptionsDialog dialog;
    auto *label = dialog.findChild<QLabel *>(QStringLiteral("updateFrequencyLabel"));
    QVERIFY(label);
    QCOMPARE(label->text(), QStringLiteral("Auto update check:"));
    QVERIFY(!label->text().contains(QStringLiteral("Automatically check for updates")));
}

// TC-SETTINGS-LABELS-AND-MOUSE-OPTIONS
// Test purpose: verify the four requested General labels and the fixed Mouse
// page surface in the live Settings dialog.
// Preconditions: the production QVApplication and options dialog are ready.
// Input data: an English dialog, its General labels, the after-delete model,
// and the named Mouse controls.
// Steps: construct the dialog, inspect the exact renamed text and option data,
// then search for every removed Mouse control.
// Expected result: all four labels use the new wording; the DoNothing value is
// displayed as No Action; navigation and middle-button mode controls are gone;
// the fixed defaults are disabled navigation and Click mode.
// Postcondition: the dialog and temporary language setting are restored.
void FeatureTests::testSettingsRenamedLabelsAndRemovedMouseOptions()
{
    ScopedOptionValues options({{QStringLiteral("language"), QStringLiteral("en")}});
    SourceLanguageTranslator sourceTranslator;
    QVERIFY(QCoreApplication::installTranslator(&sourceTranslator));

    QVOptionsDialog dialog;
    auto *checkerboard = dialog.findChild<QCheckBox *>(QStringLiteral("checkerboardBackgroundCheckbox"));
    auto *reuseWindow = dialog.findChild<QCheckBox *>(QStringLiteral("reuseWindowCheckbox"));
    auto *afterDeleteLabel = dialog.findChild<QLabel *>(QStringLiteral("label_10"));
    auto *afterDelete = dialog.findChild<QComboBox *>(QStringLiteral("afterDeletionComboBox"));
    QVERIFY(checkerboard);
    QVERIFY(reuseWindow);
    QVERIFY(afterDeleteLabel);
    QVERIFY(afterDelete);
    QCOMPARE(checkerboard->text(), QStringLiteral("Use checkerboard background after opening image"));
    QCOMPARE(reuseWindow->text(), QStringLiteral("Open images in the same window"));
    QCOMPARE(afterDeleteLabel->text(), QStringLiteral("After deleting files:"));
    const int noActionIndex = afterDelete->findData(static_cast<int>(Qv::AfterDelete::DoNothing));
    QVERIFY(noActionIndex >= 0);
    QCOMPARE(afterDelete->itemText(noActionIndex), QStringLiteral("No Action"));

    QVERIFY(!dialog.findChild<QCheckBox *>(QStringLiteral("navigationRegionsCheckbox")));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("middleButtonModeLabel")));
    QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("middleButtonModeHost")));
    QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeClickRadioButton")));
    QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeDragRadioButton")));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("middleDragLabel")));
    QVERIFY(!dialog.findChild<QComboBox *>(QStringLiteral("middleDragComboBox")));
    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("altMiddleDragLabel")));
    QVERIFY(!dialog.findChild<QComboBox *>(QStringLiteral("altMiddleDragComboBox")));

    const auto &settings = qvApp->getSettingsManager();
    QCOMPARE(settings.getBoolean(QStringLiteral("navigationregionsenabled"), true), false);
    QCOMPARE(settings.getEnum<Qv::ClickOrDrag>(QStringLiteral("viewportmiddlebuttonmode"), true),
             Qv::ClickOrDrag::Click);
    QVERIFY(QCoreApplication::removeTranslator(&sourceTranslator));
}

// TC-SETTINGS-MOUSE-MIGRATION
// Test purpose: verify an existing profile cannot revive either removed Mouse
// option after migration.
// Preconditions: the application settings store is writable and firstlaunch
// is marked as an initialized profile so only the normal migration runs.
// Input data: legacy navigation=true and middle-button mode=Drag values.
// Steps: save the legacy values, run migrateOldSettings(), reload the manager,
// and inspect both persisted and in-memory values.
// Expected result: both values are deterministically rewritten to false and
// Click, respectively.
// Postcondition: the original profile values are restored.
void FeatureTests::testRemovedMouseSettingsMigrateToFixedDefaults()
{
    ScopedSettingPreserver firstLaunch(QStringLiteral("firstlaunch"));
    QSettings settings;
    settings.setValue(QStringLiteral("firstlaunch"), true);
    ScopedOptionValues options({
        {QStringLiteral("navigationregionsenabled"), true},
        {QStringLiteral("viewportmiddlebuttonmode"), static_cast<int>(Qv::ClickOrDrag::Drag)}
    });

    SettingsManager::migrateOldSettings();
    settings.sync();
    QCOMPARE(settings.value(QStringLiteral("options/navigationregionsenabled")).toBool(), false);
    QCOMPARE(settings.value(QStringLiteral("options/viewportmiddlebuttonmode")).toInt(),
             static_cast<int>(Qv::ClickOrDrag::Click));
    qvApp->getSettingsManager().loadSettings();
    QCOMPARE(qvApp->getSettingsManager().getBoolean(QStringLiteral("navigationregionsenabled")), false);
    QCOMPARE(qvApp->getSettingsManager().getEnum<Qv::ClickOrDrag>(
                 QStringLiteral("viewportmiddlebuttonmode")), Qv::ClickOrDrag::Click);
}

// TC-PREFERENCES-FORMATS-ASSOCIATE
// Test purpose: verify the file-association operation is deterministic and
// non-invasive in its unit-test dry-run mode.
// Preconditions: the Cocoa bridge is linked and Launch Services integration
// is available to the production implementation.
// Input data: duplicate, mixed-case, dotted and undotted extensions.
// Steps: invoke associateAllSupportedFormats with dryRun=true and inspect the
// returned counts and failure list.
// Expected result: extensions are normalized and de-duplicated; every valid
// extension is counted as associated and no real Launch Services registration
// occurs.
// Postcondition: the user's default application associations are unchanged.
void FeatureTests::testAssociateAllSupportedFormatsDryRun()
{
    const auto result = QVCocoaFunctions::associateAllSupportedFormats(
        {QStringLiteral(".JPG"), QStringLiteral("jpg"), QStringLiteral("PNG"),
         QStringLiteral(""), QStringLiteral(".")}, true);
    QCOMPARE(result.requestedCount, 2);
    QCOMPARE(result.associatedCount, 2);
    QVERIFY(result.failedExtensions.isEmpty());
}

// TC-PREFERENCES-DEFAULTS
// Test purpose: verify every fixed default requested by the new Preferences
// contract and verify that each removed option has no UI object.
// Preconditions: SettingsManager has initialized its default-value library.
// Input data: SettingsManager defaults and the production options dialog.
// Steps: read defaults with defaults=true, construct the dialog, and inspect
// the object names of all removed controls plus the new update controls.
// Expected result: fixed defaults match the specification; all removed
// controls are absent; update frequency has four choices and defaults Weekly.
// Postcondition: no persistent settings are changed.
void FeatureTests::testPreferencesDefaultsAndRemovedControls()
{
    const auto &settings = qvApp->getSettingsManager();
    QCOMPARE(settings.getBoolean("fullscreendetails", true), false);
    QCOMPARE(settings.getBoolean("mainmenuicons", true), false);
    QCOMPARE(settings.getBoolean("contextmenuicons", true), true);
    QCOMPARE(settings.getBoolean("submenuicons", true), true);
    QCOMPARE(settings.getBoolean("persistsession", true), false);
    QCOMPARE(settings.getBoolean("slideshowkeepswindowontop", true), false);
    QCOMPARE(settings.getBoolean("allowmimecontentdetection", true), true);
    QCOMPARE(settings.getBoolean("skiphidden", true), true);
    QCOMPARE(settings.getBoolean("saverecents", true), true);
    QCOMPARE(settings.getBoolean("scalingtwoenabled", true), true);
    QCOMPARE(settings.getBoolean("smoothscalinglimitenabled", true), false);
    QCOMPARE(settings.getBoolean("cursorzoom", true), true);
    QCOMPARE(settings.getBoolean("onetoonepixelsizing", true), false);
    QCOMPARE(settings.getEnum<Qv::CalculatedZoomMode>("calculatedzoommode", true), Qv::CalculatedZoomMode::ZoomToFit);
    QCOMPARE(settings.getBoolean("fitzoomlimitenabled", true), false);
    QCOMPARE(settings.getBoolean("navresetszoom", true), true);
    QCOMPARE(settings.getBoolean("constrainimageposition", true), true);
    QCOMPARE(settings.getBoolean("constraincentersmallimage", true), true);
    QCOMPARE(settings.getBoolean("originalsizeastoggle", true), false);
    QCOMPARE(settings.getEnum<Qv::ColorSpaceConversion>("colorspaceconversion", true), Qv::ColorSpaceConversion::AutoDetect);
    QCOMPARE(settings.getBoolean("navigationregionsenabled", true), false);
    QCOMPARE(settings.getEnum<Qv::ClickOrDrag>("viewportmiddlebuttonmode", true), Qv::ClickOrDrag::Click);
    QCOMPARE(settings.getInteger("navspeed", true), 50);
    QCOMPARE(settings.getBoolean("loopfoldersenabled", true), false);
    QCOMPARE(settings.getEnum<Qv::UpdateCheckFrequency>("updatecheckfrequency", true), Qv::UpdateCheckFrequency::Weekly);

    QVOptionsDialog dialog;
    const QStringList removedObjects {
        QStringLiteral("windowResizeComboBox"), QStringLiteral("afterMatchingSizeComboBox"),
        QStringLiteral("minWindowResizeSpinBox"), QStringLiteral("maxWindowResizeSpinBox"),
        QStringLiteral("detailsInFullscreen"), QStringLiteral("mainMenuIconsCheckbox"),
        QStringLiteral("contextMenuIconsCheckbox"), QStringLiteral("submenuIconsCheckbox"),
        QStringLiteral("persistSessionCheckbox"), QStringLiteral("slideshowKeepsWindowOnTopCheckbox"),
        QStringLiteral("mimeContentDetectionCheckbox"), QStringLiteral("skipHiddenCheckbox"),
        QStringLiteral("saveRecentsCheckbox"), QStringLiteral("scalingTwoCheckbox"),
        QStringLiteral("smoothScalingLimitCheckbox"), QStringLiteral("scaleFactorSpinBox"),
        QStringLiteral("cursorZoomCheckbox"), QStringLiteral("oneToOnePixelSizingCheckbox"),
        QStringLiteral("zoomDefaultComboBox"), QStringLiteral("fitZoomLimitCheckbox"),
        QStringLiteral("fitOverscanSpinBox"), QStringLiteral("navResetsZoomCheckbox"),
        QStringLiteral("constrainImagePositionCheckbox"),
        QStringLiteral("constrainCentersSmallImageCheckbox"),
        QStringLiteral("originalSizeAsToggleCheckbox"),
        QStringLiteral("colorSpaceConversionComboBox"), QStringLiteral("navSpeedSpinBox"),
        QStringLiteral("loopFoldersCheckbox"), QStringLiteral("updateCheckbox"),
        QStringLiteral("formatsTable"), QStringLiteral("navigationRegionsCheckbox"),
        QStringLiteral("middleButtonModeLabel"), QStringLiteral("middleButtonModeHost"),
        QStringLiteral("middleButtonModeClickRadioButton"),
        QStringLiteral("middleButtonModeDragRadioButton"),
        QStringLiteral("middleDragLabel"), QStringLiteral("middleDragComboBox"),
        QStringLiteral("altMiddleDragLabel"), QStringLiteral("altMiddleDragComboBox")
    };
    for (const auto &objectName : removedObjects)
        QVERIFY2(!dialog.findChild<QWidget *>(objectName), qPrintable(objectName));

    auto *theme = dialog.findChild<QComboBox *>("themeComboBox");
    QVERIFY(theme);
    auto *appearanceLabel = dialog.findChild<QLabel *>("appearanceLabel");
    QVERIFY(appearanceLabel);
    QCOMPARE(appearanceLabel->text(), QStringLiteral("Appearance:"));
    QCOMPARE(theme->itemText(0), QStringLiteral("Light"));
    QCOMPARE(theme->itemText(1), QStringLiteral("Dark"));
    auto *frequency = dialog.findChild<QComboBox *>("updateFrequencyComboBox");
    QVERIFY(frequency);
    QCOMPARE(frequency->count(), 4);
    QCOMPARE(frequency->itemText(0), QStringLiteral("Never"));
    QCOMPARE(frequency->itemText(1), QStringLiteral("Daily"));
    QCOMPARE(frequency->itemText(2), QStringLiteral("Weekly"));
    QCOMPARE(frequency->itemText(3), QStringLiteral("Monthly"));
    QCOMPARE(frequency->currentData().toInt(), static_cast<int>(Qv::UpdateCheckFrequency::Weekly));
    auto *associateButton = dialog.findChild<QPushButton *>("associateFormatsButton");
    QVERIFY(associateButton);
    QCOMPARE(associateButton->text(), QStringLiteral("Associate all supported formats"));
}

// TC-SETTINGS-GROUPS-DEFAULTS
// Test purpose: verify the General page's eight semantic groups, their exact
// option membership/order, and the two requested default values.
// Preconditions: SettingsManager and the production QVOptionsDialog can be
// constructed.
// Input data: the General page object tree and the default setting library.
// Steps: read the defaults, enumerate the direct group hierarchy, inspect
// each group's form contract, and verify every named option belongs to its
// expected group.
// Expected result: groups 1 through 8 match the specification in order;
// every General form uses style-resolved vertical spacing (-1), has no extra
// bottom padding, and Appearance/Smooth scaling default to Dark/Bilinear.
// Postcondition: the dialog is destroyed and no persistent setting changes.
void FeatureTests::testSettingsGeneralGroupsAndDefaults()
{
    const auto &settings = qvApp->getSettingsManager();
    QCOMPARE(settings.getEnum<Qv::Theme>(QStringLiteral("theme"), true), Qv::Theme::Dark);
    QCOMPARE(settings.getEnum<Qv::SmoothScalingMode>(QStringLiteral("smoothscalingmode"), true),
             Qv::SmoothScalingMode::Bilinear);

    ScopedOptionValues options({
        {QStringLiteral("theme"), static_cast<int>(Qv::Theme::Dark)},
        {QStringLiteral("smoothscalingmode"), static_cast<int>(Qv::SmoothScalingMode::Bilinear)}
    });

    QVOptionsDialog dialog;
    auto *generalContent = dialog.findChild<QWidget *>(QStringLiteral("generalContent"));
    QVERIFY(generalContent);
    QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("general")));
    QVERIFY(!dialog.findChild<QWidget *>(QStringLiteral("misc")));
    auto *generalLayout = qobject_cast<QVBoxLayout *>(generalContent->layout());
    QVERIFY(generalLayout);
    QVERIFY(generalContent->property("settingsGroupSpacing").toInt() > 0);
    QCOMPARE(generalContent->property("settingsRowSpacing").toInt(), -1);
    QVERIFY(!generalContent->property("settingsGroupBottomPadding").isValid());
    QVERIFY(generalContent->property("settingsHasBottomStretch").toBool());
    QCOMPARE(generalLayout->count(), 9);
    QVERIFY(generalLayout->itemAt(8)->spacerItem());

    const QList<QStringList> expectedItems {
        {QStringLiteral("langComboBox")},
        {QStringLiteral("themeComboBox"), QStringLiteral("checkerboardBackgroundCheckbox")},
        {QStringLiteral("smoothScalingComboBox")},
        {QStringLiteral("reuseWindowCheckbox"), QStringLiteral("smallImagesOneToOneCheckbox")},
        {QStringLiteral("slideshowDirectionComboBox"), QStringLiteral("slideshowTimerSpinBox")},
        {QStringLiteral("afterDeletionComboBox"), QStringLiteral("askDeleteCheckbox")},
        {QStringLiteral("updateFrequencyComboBox")},
        {QStringLiteral("associateFormatsButton")}
    };

    const auto groups = generalContent->findChildren<QWidget *>();
    QList<QWidget *> orderedGroups;
    for (auto *widget : groups)
    {
        if (widget->property("settingsGroupIndex").isValid())
            orderedGroups.append(widget);
    }
    std::sort(orderedGroups.begin(), orderedGroups.end(), [](QWidget *left, QWidget *right) {
        return left->property("settingsGroupIndex").toInt()
            < right->property("settingsGroupIndex").toInt();
    });
        QCOMPARE(orderedGroups.size(), expectedItems.size());

    for (int index = 0; index < expectedItems.size(); ++index)
    {
        QWidget *group = orderedGroups.at(index);
        QCOMPARE(generalLayout->itemAt(index)->widget(), group);
        QCOMPARE(group->property("settingsGroupIndex").toInt(), index + 1);
        QCOMPARE(group->property("settingsItemObjectNames").toStringList(), expectedItems.at(index));
        QCOMPARE(group->parentWidget(), generalContent);
        QCOMPARE(group->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
        auto *layout = qobject_cast<QFormLayout *>(group->layout());
        QVERIFY(layout);
        QCOMPARE(layout->verticalSpacing(), -1);
        const QMargins margins = layout->contentsMargins();
        QCOMPARE(margins.top(), 0);
        QCOMPARE(margins.right(), 0);
        QCOMPARE(margins.bottom(), 0);
        if (index == 3)
            QCOMPARE(margins.left(), generalContent->property(
                         "settingsAlignedLabelColumnWidth").toInt());
        else
            QCOMPARE(margins.left(), 0);

        for (const QString &objectName : expectedItems.at(index))
        {
            auto *option = dialog.findChild<QWidget *>(objectName);
            QVERIFY(option);
            bool belongsToGroup = false;
            for (QWidget *ancestor = option->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
            {
                if (ancestor == group)
                {
                    belongsToGroup = true;
                    break;
                }
            }
            QVERIFY2(belongsToGroup, qPrintable(objectName));
        }
    }

    auto *theme = dialog.findChild<QComboBox *>(QStringLiteral("themeComboBox"));
    auto *smoothScaling = dialog.findChild<QComboBox *>(QStringLiteral("smoothScalingComboBox"));
    QVERIFY(theme);
    QVERIFY(smoothScaling);
    QCOMPARE(theme->currentData().toInt(), static_cast<int>(Qv::Theme::Dark));
    QCOMPARE(smoothScaling->currentData().toInt(), static_cast<int>(Qv::SmoothScalingMode::Bilinear));
}

// TC-SETTINGS-COOLDOWN-REMOVED
// Test purpose: verify the discrete-action cooldown is enabled by default
// while its obsolete user-facing checkbox is absent from Mouse.
// Preconditions: the application SettingsManager and QVOptionsDialog can be
// constructed.
// Input data: the scrollactioncooldown default value and the Mouse object tree.
// Steps: inspect the setting library, construct Settings, and search for the
// former checkbox and its visible text.
// Expected result: the default is true and no cooldown checkbox or label is
// exposed by the dialog.
// Postcondition: the dialog is destroyed without changing persistent settings.
void FeatureTests::testSettingsCooldownOptionIsRemovedAndDefaultEnabled()
{
    const auto setting = qvApp->getSettingsManager().getSettings().value(
        QStringLiteral("scrollactioncooldown"));
    QVERIFY(setting.defaultValue.isValid());
    QVERIFY(setting.defaultValue.toBool());

    QVOptionsDialog dialog;
    QVERIFY(!dialog.findChild<QCheckBox *>(
        QStringLiteral("scrollActionCooldownCheckbox")));
    for (auto *checkBox : dialog.findChildren<QCheckBox *>())
        QVERIFY(!checkBox->text().contains(QStringLiteral("Cooldown"),
                                           Qt::CaseInsensitive));
}

// TC-UPDATE-FREQUENCY-POLICY
// Test purpose: verify Never/Daily/Weekly/Monthly interval semantics without
// network access or wall-clock dependence.
// Preconditions: UpdateChecker's pure policy helper is available.
// Input data: fixed UTC timestamps and each frequency enum.
// Steps: evaluate before, at, and after each interval, plus an invalid last
// check timestamp.
// Expected result: Never never checks; a missing last check checks immediately;
// each other frequency checks exactly at its calendar interval.
// Postcondition: no network request or persistent setting is produced.
void FeatureTests::testUpdateCheckFrequencyPolicy()
{
    const QDateTime last(QDate(2026, 8, 1), QTime(12, 0), QTimeZone::UTC);
    QVERIFY(!UpdateChecker::shouldCheckAutomatically(last.addDays(30), last,
                                                      Qv::UpdateCheckFrequency::Never));
    QVERIFY(UpdateChecker::shouldCheckAutomatically(last, {}, Qv::UpdateCheckFrequency::Daily));
    QVERIFY(!UpdateChecker::shouldCheckAutomatically(last.addSecs(24 * 3600 - 1), last,
                                                       Qv::UpdateCheckFrequency::Daily));
    QVERIFY(UpdateChecker::shouldCheckAutomatically(last.addDays(1), last,
                                                    Qv::UpdateCheckFrequency::Daily));
    QVERIFY(!UpdateChecker::shouldCheckAutomatically(last.addDays(7).addSecs(-1), last,
                                                       Qv::UpdateCheckFrequency::Weekly));
    QVERIFY(UpdateChecker::shouldCheckAutomatically(last.addDays(7), last,
                                                    Qv::UpdateCheckFrequency::Weekly));
    QVERIFY(!UpdateChecker::shouldCheckAutomatically(last.addMonths(1).addSecs(-1), last,
                                                       Qv::UpdateCheckFrequency::Monthly));
    QVERIFY(UpdateChecker::shouldCheckAutomatically(last.addMonths(1), last,
                                                    Qv::UpdateCheckFrequency::Monthly));
}

// TC-IMG-SMALL-SETTING
// Test purpose: verify that the Image settings page exposes and persists the
// new small-image 1:1 option.
// Preconditions: a QVApplication exists and the settings store is writable.
// Input data: the option starts disabled, then the Image-page checkbox is enabled.
// Steps: construct QVOptionsDialog, locate the named checkbox, change it,
// and read the persisted SettingsManager value immediately.
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

    checkbox->setChecked(true);
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
// Test purpose: verify that returning to the fit ratio restores fit intent and
// that the macOS transition snapshot has the same image geometry before and
// after a fullscreen resize.
// Preconditions: a visible non-fullscreen MainWindow can load a writable 1600x900 PNG fixture.
// Input data: one discrete zoom-in step, one inverse zoom-out step, then a fullscreen enter/exit transition.
// Steps: load the fixture, force ZoomToFit, apply the inverse wheel-equivalent steps, and toggle fullscreen once.
// Expected result: ZoomToFit remains active; a changed fullscreen viewport
// receives a recalculated zoom level; the snapshot keeps the source aspect
// ratio; after exit, both the fit mode and original image rectangle return.
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

    const QRect normalTransitionRect = window.fullScreenTransitionImageRect();
    const QImage transitionImage = window.fullScreenTransitionImage();
    QVERIFY(!normalTransitionRect.isEmpty());
    QCOMPARE(normalTransitionRect.width() * 9,
             normalTransitionRect.height() * 16);
    QCOMPARE(transitionImage.size(), QSize(1600, 900));
    const QColor transitionCenter =
        transitionImage.pixelColor(transitionImage.rect().center());
    QCOMPARE(transitionCenter.alpha(), 255);
    QVERIFY(transitionCenter.blue() > transitionCenter.red());
    QVERIFY(transitionCenter.blue() > transitionCenter.green());

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
    const QRect fullscreenTransitionRect =
        window.fullScreenTransitionImageRect();
    QVERIFY(!fullscreenTransitionRect.isEmpty());
    QCOMPARE(fullscreenTransitionRect.width() * 9,
             fullscreenTransitionRect.height() * 16);

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
    QTRY_COMPARE_WITH_TIMEOUT(
        window.fullScreenTransitionImageRect(), normalTransitionRect, 5000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FULLSCREEN-SCROLL-TOP-EDGE
// Test purpose: reproduce the ordering-dependent blank band above a manually
// zoomed image after entering full screen.
// Preconditions: the normal window uses a full-size Cocoa content view with a
// visible titlebar; a tall SDR image overflows vertically at manual zoom 1.0.
// Input data: one 1200x2400 solid PNG and both orderings: zoom -> full screen
// and full screen -> zoom, each followed by vertical scrollbar minimum.
// Steps: verify the normal scene contains titlebar compensation, run the
// reported ordering, then repeat with the control ordering.
// Expected result: both full-screen scenes start at the image edge and that
// edge maps to the first viewport row; no stale padding remains scrollable.
// Postcondition: both windows exit full screen and all settings are restored.
void GraphicsViewTests::testFullscreenAfterOverflowRemovesTitlebarScenePadding()
{
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"onetoonepixelsizing", false},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)}
    });
    ScopedSettingPreserver titlebarSetting(QStringLiteral("options/titlebarhidden"));
    QSettings settings;
    settings.setValue(QStringLiteral("options/titlebarhidden"), false);
    settings.sync();

    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(
        dir, "fullscreen-scroll-top", Qt::darkBlue, QSize(1200, 2400));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.getViewportPosition().obscuredHeight > 0, 3000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);

    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    view->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(view->verticalScrollBar()->isVisible(), 2000);
    const QRectF imageSceneRect = view->scene()->itemsBoundingRect();
    QVERIFY(view->sceneRect().top() < imageSceneRect.top());

    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(window.getViewportPosition().obscuredHeight, 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(view->verticalScrollBar()->isVisible(), 2000);
    view->verticalScrollBar()->setValue(view->verticalScrollBar()->minimum());
    QCoreApplication::processEvents();
    const qreal zoomFirstSceneTop = view->sceneRect().top();
    const int zoomFirstImageTop = view->mapFromScene(imageSceneRect.topLeft()).y();
    const int zoomFirstViewportTop = view->viewport()->rect().top();

    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    window.close();

    MainWindow fullScreenFirstWindow;
    fullScreenFirstWindow.setAttribute(Qt::WA_DeleteOnClose, false);
    fullScreenFirstWindow.setWindowState(Qt::WindowNoState);
    fullScreenFirstWindow.resize(640, 480);
    fullScreenFirstWindow.show();
    QTRY_VERIFY_WITH_TIMEOUT(fullScreenFirstWindow.isVisible(), 1000);
    fullScreenFirstWindow.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(fullScreenFirstWindow.getIsPixmapLoaded(), 5000);
    auto *fullScreenFirstView =
        fullScreenFirstWindow.findChild<QVGraphicsView *>();
    QVERIFY(fullScreenFirstView);

    fullScreenFirstWindow.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(fullScreenFirstWindow.isFullScreen(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
        fullScreenFirstWindow.getViewportPosition().obscuredHeight, 0, 3000);
    fullScreenFirstView->zoomAbsolute(1.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        fullScreenFirstView->verticalScrollBar()->isVisible(), 2000);
    const QRectF fullScreenFirstImageRect =
        fullScreenFirstView->scene()->itemsBoundingRect();
    fullScreenFirstView->verticalScrollBar()->setValue(
        fullScreenFirstView->verticalScrollBar()->minimum());
    QCoreApplication::processEvents();
    const qreal fullScreenFirstSceneTop = fullScreenFirstView->sceneRect().top();
    const int fullScreenFirstImageTop =
        fullScreenFirstView->mapFromScene(fullScreenFirstImageRect.topLeft()).y();
    const int fullScreenFirstViewportTop =
        fullScreenFirstView->viewport()->rect().top();

    fullScreenFirstWindow.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!fullScreenFirstWindow.isFullScreen(), 5000);
    fullScreenFirstWindow.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);

    QCOMPARE(zoomFirstSceneTop, imageSceneRect.top());
    QCOMPARE(zoomFirstImageTop, zoomFirstViewportTop);
    QCOMPARE(fullScreenFirstSceneTop, fullScreenFirstImageRect.top());
    QCOMPARE(fullScreenFirstImageTop, fullScreenFirstViewportTop);
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

// TC-SDR-PAN-MACOS-PRESENTATION
// Test purpose: verify raster-image panning bypasses Qt's Cocoa backing store
// once the persistent native SDR tile surface is visible.
// Preconditions: a visible 640x480 Cocoa window contains a 1600x900 raster
// image at 2:1 and both scroll axes have room to move.
// Input data: one six-pixel horizontal scrollbar change after all opening
// paints have settled.
// Steps: wait for the authoritative native surface, record Qt paint events,
// pan once, and inspect native compositor submissions.
// Expected result: the tile transform advances without any Qt viewport paint.
// Postcondition: the recorder, window, fixture, and settings are released.
void GraphicsViewTests::testRasterPanUsesCompleteRepaintOnMacOS()
{
#ifndef Q_OS_MACOS
    QSKIP("The Cocoa backing-store presentation contract is macOS-specific.");
#endif
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
    QTRY_VERIFY_WITH_TIMEOUT(view->usesNativeSDRMetalRenderer(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics().firstFramePresented, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(
            view->viewportUpdateMode(), QGraphicsView::NoViewportUpdate, 5000);
    const auto beforePan = view->nativeMetalRendererDiagnostics();
    QVERIFY(beforePan.usesPersistentSDRTileSurface);
    QVERIFY(!view->viewport()->testAttribute(Qt::WA_NativeWindow));
    QVERIFY(!view->viewport()->windowHandle());
    QVERIFY(!view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    PaintRegionRecorder productionRecorder;
    view->viewport()->installEventFilter(&productionRecorder);
    bar->setValue(bar->value() + 6);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics().compositorGeometryUpdateCount
                    > beforePan.compositorGeometryUpdateCount,
            1000);
    QTest::qWait(50);
    view->viewport()->removeEventFilter(&productionRecorder);
    qInfo().noquote() << QStringLiteral(
        "SDR_PAN_PRESENTATION qt_paints=%1 compositor_updates=%2")
        .arg(productionRecorder.recordedAreas().size())
        .arg(view->nativeMetalRendererDiagnostics().compositorGeometryUpdateCount
             - beforePan.compositorGeometryUpdateCount);
    QVERIFY(productionRecorder.recordedAreas().isEmpty());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-INT-SAMPLE-PAN
// Test purpose: exercise real, externally supplied SDR files through the full
// decoder -> native Metal layer path and verify the macOS presentation policy
// selected for each supplied format.
// Preconditions: FOVELLE_SDR_SAMPLE_DIR names a readable directory containing
// one or more supported SDR image documents.
// Input data: every regular file in the supplied directory.
// Steps: load each image, wait for raster Metal presentation, force an
// overflowing zoom, pan by six pixels, and inspect Qt paint plus Metal frame
// counters. Expected result: raster images present from the independent native
// layer with Qt viewport painting parked; vector documents retain Qt's bounded
// tile path (covered independently by TC-EPS-VECTOR-PAN-PARTIAL-REPAINT).
// Postcondition: the recorder, window, samples, and settings are released.
void SDRSampleInteractionTests::testProvidedSamplesUseMacOSPanPresentationPolicy()
{
#ifndef Q_OS_MACOS
    QSKIP("The Cocoa backing-store presentation contract is macOS-specific.");
#endif
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
    window.raise();
    window.activateWindow();
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
        QCOMPARE(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent),
                 details.isVectorLoaded);
        if (!details.isVectorLoaded)
        {
            QVERIFY2(details.isNativeSDRLoaded, qPrintable(sample.fileName()));
            QTRY_VERIFY_WITH_TIMEOUT(view->usesNativeSDRMetalRenderer(), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(
                    view->nativeMetalRendererDiagnostics().firstFramePresented,
                    30000);
            QTRY_VERIFY_WITH_TIMEOUT(
                    view->viewportUpdateMode() == QGraphicsView::NoViewportUpdate,
                    3000);
        }

        // The supplied SVG logo and portrait EPS are much narrower than the
        // test viewport. 16:1 guarantees horizontal overflow for every sample
        // while remaining well below the common 64:1 zoom ceiling.
        view->zoomAbsolute(16.0, Qv::CalculateViewportCenterPos);
        QTRY_VERIFY_WITH_TIMEOUT(
            view->horizontalScrollBar()->maximum()
                > view->horizontalScrollBar()->minimum(),
            2000);
        QScrollBar *bar = view->horizontalScrollBar();
        const int centerValue = (bar->minimum() + bar->maximum()) / 2;
        bar->setValue(centerValue);
        QCoreApplication::processEvents();
        if (details.isVectorLoaded)
            view->viewport()->repaint();
        QCoreApplication::processEvents();
        // Exclude one-time scrollbar/layout work from the measured pan.
        bar->setValue(centerValue + 6);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        bar->setValue(centerValue);
        QCoreApplication::processEvents(QEventLoop::AllEvents);

        PaintRegionRecorder recorder;
        view->viewport()->installEventFilter(&recorder);
        const quint64 requestedBefore =
                view->nativeMetalRendererDiagnostics().requestedRenderGeneration;
        QElapsedTimer timer;
        timer.start();
        bar->setValue(bar->value() + 6);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        if (details.isVectorLoaded)
            QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000);
        else
            QTRY_VERIFY_WITH_TIMEOUT(
                    view->nativeMetalRendererDiagnostics().requestedRenderGeneration
                            > requestedBefore
                    && view->nativeMetalRendererDiagnostics().presentedRenderGeneration
                            >= view->nativeMetalRendererDiagnostics()
                                       .requestedRenderGeneration,
                    3000);
        const double elapsedMilliseconds = timer.nsecsElapsed() / 1000000.0;
        view->viewport()->removeEventFilter(&recorder);

        const qint64 viewportArea = static_cast<qint64>(view->viewport()->width())
                * view->viewport()->height();
        const qint64 maximumPaintArea = recorder.recordedAreas().isEmpty()
                ? 0
                : *std::max_element(recorder.recordedAreas().cbegin(),
                                    recorder.recordedAreas().cend());
        const qreal dirtyRatio = static_cast<qreal>(maximumPaintArea) / viewportArea;
        qInfo().noquote() << QStringLiteral(
            "SDR_SAMPLE_PAN file=%1 size=%2x%3 decode_ms=%4 "
            "pan_dispatch_and_present_ms=%5 dirty_ratio=%6 metal=%7")
            .arg(sample.fileName())
            .arg(details.loadedPixmapSize.width())
            .arg(details.loadedPixmapSize.height())
            .arg(details.decodeMilliseconds, 0, 'f', 3)
            .arg(elapsedMilliseconds, 0, 'f', 3)
            .arg(dirtyRatio, 0, 'f', 6)
            .arg(details.isNativeSDRLoaded ? QStringLiteral("true")
                                           : QStringLiteral("false"));
        if (!details.isVectorLoaded)
        {
            QCOMPARE(maximumPaintArea, 0);
            QVERIFY(view->nativeMetalRendererDiagnostics().sdrImageActive);
        }
    }

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-INT-AUTHORITATIVE-PIXELS
// Test purpose: prove zoom never presents a bounded proxy followed by a later
// refinement. Every SDR Metal frame must use the authoritative decoded tiles.
void SDRSampleInteractionTests::testProvidedRasterStaysAuthoritativeDuringInteraction()
{
#ifndef Q_OS_MACOS
    QSKIP("The native Metal refinement contract is macOS-specific.");
#endif
    const QDir directory(QString::fromUtf8(qgetenv("FOVELLE_SDR_SAMPLE_DIR")));
    const QString samplePath = directory.filePath(QStringLiteral("2.png"));
    QVERIFY2(QFileInfo::exists(samplePath), qPrintable(samplePath));

    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"preloadingmode", static_cast<int>(Qv::PreloadMode::Disabled)},
        {"checkerboardbackground", false},
        {"onetoonepixelsizing", false}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(1200, 800);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    window.openFile(samplePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesNativeSDRMetalRenderer(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics().firstFramePresented, 10000);

    const auto before = view->nativeMetalRendererDiagnostics();
    QVERIFY(before.usesMaterializedSDRTiles);
    QVERIFY(before.sdrTileCount > 0);
    QCOMPARE(before.sdrAuthoritativePresentedFrameCount,
             before.presentedFrameCount);
    QElapsedTimer responseTimer;
    responseTimer.start();
    view->zoomAbsolute(16.0, Qv::CalculateViewportCenterPos);
    while (view->nativeMetalRendererDiagnostics()
                   .sdrAuthoritativePresentedFrameCount
                   <= before.sdrAuthoritativePresentedFrameCount
           && responseTimer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QTest::qWait(1);
    }
    QVERIFY(view->nativeMetalRendererDiagnostics()
                    .sdrAuthoritativePresentedFrameCount
            > before.sdrAuthoritativePresentedFrameCount);
    const double responseMilliseconds =
            responseTimer.nsecsElapsed() / 1000000.0;
    const auto after = view->nativeMetalRendererDiagnostics();
    QCOMPARE(after.sdrAuthoritativePresentedFrameCount,
             after.presentedFrameCount);
    QVERIFY(after.usesSDRFullSingleImage);
    qInfo().noquote() << QStringLiteral(
            "SDR_AUTHORITATIVE_INTERACTION {\"response_ms\":%1,"
            "\"tiles_total\":%2,\"tiles_visible\":%3,"
            "\"authoritative_frames\":%4,\"full_single_image\":%5}")
            .arg(responseMilliseconds, 0, 'f', 3)
            .arg(after.sdrTileCount)
            .arg(after.sdrVisibleTileCount)
            .arg(after.sdrAuthoritativePresentedFrameCount)
            .arg(after.usesSDRFullSingleImage
                         ? QStringLiteral("true") : QStringLiteral("false"));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-SYS-120HZ
// Test purpose: drive 48 production scrollbar updates at 8 ms cadence and
// verify that persistent full-resolution SDR tiles consume them as bounded
// Core Animation transform submissions rather than Qt/CI rerasterizations.
// FOVELLE_SDR_BUDGET_SAMPLE can replace the default supplied 2.png so the same
// frame-budget contract covers an externally supplied oversized raster.
void SDRSampleInteractionTests::testProvidedRaster120HzInteractionProbe()
{
#ifndef Q_OS_MACOS
    QSKIP("The CAMetalDisplayLink cadence probe is macOS-specific.");
#endif
    ScopedEnvironmentValue interactionEnvironment(
            "FOVELLE_HDR_TEST_120HZ_INTERACTION");
    qputenv("FOVELLE_HDR_TEST_120HZ_INTERACTION", "1");
    const QDir directory(QString::fromUtf8(qgetenv("FOVELLE_SDR_SAMPLE_DIR")));
    const QString requestedBudgetSample = QString::fromUtf8(
            qgetenv("FOVELLE_SDR_BUDGET_SAMPLE"));
    const QString samplePath = requestedBudgetSample.isEmpty()
            ? directory.filePath(QStringLiteral("2.png"))
            : requestedBudgetSample;
    QVERIFY2(QFileInfo::exists(samplePath), qPrintable(samplePath));

    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"preloadingmode", static_cast<int>(Qv::PreloadMode::Disabled)},
        {"checkerboardbackground", false},
        {"onetoonepixelsizing", false}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(1200, 800);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    window.openFile(samplePath);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesNativeSDRMetalRenderer(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics().firstFramePresented, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics()
                    .compositorInteractiveSubmissionCount >= 30,
            7000);
    const auto diagnostics = view->nativeMetalRendererDiagnostics();
    qInfo().noquote() << QStringLiteral(
            "SDR_180FPS_BUDGET {\"interactive_submissions\":%1,"
            "\"presented_frames\":%2,\"last_interval_ms\":%3,"
            "\"missed_deadlines\":%4,\"callbacks\":%5,"
            "\"deferred_callbacks\":%6,\"gpu_ms\":%7,"
            "\"encode_ms\":%8,\"request_to_present_ms\":%9,"
            "\"max_interactive_gpu_ms\":%10,"
            "\"max_interactive_encode_ms\":%11,"
            "\"display_max_fps\":%12,\"requested_min_fps\":%13,"
            "\"requested_max_fps\":%14,\"display_can_present_180\":%15,"
            "\"persistent_tiles\":%16,\"file\":\"%17\"}")
            .arg(diagnostics.compositorInteractiveSubmissionCount)
            .arg(diagnostics.presentedFrameCount)
            .arg(diagnostics.lastPresentedIntervalMilliseconds, 0, 'f', 3)
            .arg(diagnostics.missedTargetDeadlineCount)
            .arg(diagnostics.displayLinkCallbackCount)
            .arg(diagnostics.deferredDisplayLinkCallbackCount)
            .arg(diagnostics.lastGPUExecutionMilliseconds, 0, 'f', 3)
            .arg(diagnostics.lastRenderMilliseconds, 0, 'f', 3)
            .arg(diagnostics.lastRequestToPresentationMilliseconds, 0, 'f', 3)
            .arg(diagnostics.maximumInteractiveGPUExecutionMilliseconds, 0, 'f', 3)
            .arg(diagnostics.maximumInteractiveRenderMilliseconds, 0, 'f', 3)
            .arg(diagnostics.displayMaximumFramesPerSecond)
            .arg(diagnostics.requestedFrameRateMinimum, 0, 'f', 0)
            .arg(diagnostics.requestedFrameRateMaximum, 0, 'f', 0)
            .arg(diagnostics.displayCanPresent180FPS
                         ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(diagnostics.usesPersistentSDRTileSurface
                         ? QStringLiteral("true") : QStringLiteral("false"))
            .arg(QFileInfo(samplePath).fileName());
    QVERIFY(diagnostics.usesCAMetalDisplayLink);
    QVERIFY(diagnostics.sdrImageActive);
    QVERIFY(diagnostics.usesPersistentSDRTileSurface);
    QCOMPARE(diagnostics.deferredDisplayLinkCallbackCount, 0);
    QCOMPARE(diagnostics.sdrAuthoritativePresentedFrameCount,
             diagnostics.presentedFrameCount);
    QVERIFY2(diagnostics.lastRenderMilliseconds < (1000.0 / 120.0),
             qPrintable(QStringLiteral("encode=%1ms")
                                .arg(diagnostics.lastRenderMilliseconds)));
    // The application submits no per-interaction Metal/CI work after the tile
    // surface is installed; WindowServer composites the retained layer tree.
    QCOMPARE(diagnostics.lastGPUExecutionMilliseconds, 0.0);
    constexpr double frameBudget180FPS = 1000.0 / 180.0;
    QVERIFY(diagnostics.maximumInteractiveRenderMilliseconds > 0.0);
    QVERIFY2(diagnostics.maximumInteractiveRenderMilliseconds < frameBudget180FPS,
             qPrintable(QStringLiteral("maximum interactive encode=%1ms")
                                .arg(diagnostics.maximumInteractiveRenderMilliseconds)));
    QCOMPARE(diagnostics.maximumInteractiveGPUExecutionMilliseconds, 0.0);
    const auto frameRatePolicy = QVCocoaFunctions::sdrFrameRatePolicy(
            diagnostics.displayMaximumFramesPerSecond);
    QCOMPARE(static_cast<qreal>(diagnostics.requestedFrameRateMinimum),
             frameRatePolicy.minimum);
    QCOMPARE(static_cast<qreal>(diagnostics.requestedFrameRateMaximum),
             frameRatePolicy.maximum);
    QCOMPARE(diagnostics.displayCanPresent180FPS,
             frameRatePolicy.displayCanPresent180FPS);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-SYS-PERFORMANCE
// Test purpose: record one backend-independent workload for comparing the Qt
// backing-store SDR path with the independent native Metal presentation path.
// Preconditions: FOVELLE_SDR_SAMPLE_DIR names the supplied SDR corpus; the
// optional FOVELLE_LARGE_SDR_SAMPLE names a readable large raster image.
// Input data: every supplied raster sample, followed by the optional large
// image, with preloading disabled so every open is a genuine cold decode.
// Steps: open one file, wait for its first settled viewport paint, zoom to 4x,
// then dispatch and settle 48 two-axis pan samples.
// Expected result: every supported sample loads and emits a single structured
// SDR_PERF record. Performance thresholds are evaluated by the comparison
// report rather than hard-coded across unlike Macs.
// Postcondition: the window, decoder cache, and temporary presentation state
// are released and all user settings are restored.
void SDRSampleInteractionTests::testProvidedSamplesPerformanceProbe()
{
#ifndef Q_OS_MACOS
    QSKIP("The native Metal comparison is macOS-specific.");
#endif
    const QString sampleDirectory =
            QString::fromUtf8(qgetenv("FOVELLE_SDR_SAMPLE_DIR"));
    const QDir directory(sampleDirectory);
    QVERIFY2(!sampleDirectory.isEmpty() && directory.exists(),
             qPrintable(sampleDirectory));

    QFileInfoList samples = directory.entryInfoList(
            QDir::Files | QDir::Readable, QDir::Name);
    const QString largeSample =
            QString::fromUtf8(qgetenv("FOVELLE_LARGE_SDR_SAMPLE"));
    if (!largeSample.isEmpty())
        samples.append(QFileInfo(largeSample));
    QVERIFY(!samples.isEmpty());

    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"preloadingmode", static_cast<int>(Qv::PreloadMode::Disabled)},
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
    const auto waitUntil = [](const auto &predicate, const int timeoutMilliseconds) {
        QElapsedTimer waitTimer;
        waitTimer.start();
        while (!predicate() && waitTimer.elapsed() < timeoutMilliseconds)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents);
            QTest::qWait(1);
        }
        return predicate();
    };

    for (const QFileInfo &sample : std::as_const(samples))
    {
        if (sample.fileName().startsWith("._"))
            continue;

        view->closeImage();
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QElapsedTimer coldTimer;
        coldTimer.start();
        window.openFile(sample.absoluteFilePath());
        QTRY_COMPARE_WITH_TIMEOUT(
            window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
            sample.absoluteFilePath(), 180000);
        QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 180000);

        if (window.getCurrentFileDetails().isVectorLoaded)
            continue;
        QVERIFY2(window.getCurrentFileDetails().isNativeSDRLoaded,
                 qPrintable(sample.absoluteFilePath()));
        QVERIFY2(waitUntil([&]() {
                     return view->usesNativeSDRMetalRenderer()
                             && view->nativeMetalRendererDiagnostics()
                                        .firstFramePresented
                             && view->viewportUpdateMode()
                                        == QGraphicsView::NoViewportUpdate;
                 }, 180000),
                 qPrintable(sample.absoluteFilePath()));
        const double coldMilliseconds = coldTimer.nsecsElapsed() / 1000000.0;

        const quint64 zoomRequestedBefore =
                view->nativeMetalRendererDiagnostics().requestedRenderGeneration;
        QElapsedTimer zoomTimer;
        zoomTimer.start();
        view->zoomAbsolute(4.0, Qv::CalculateViewportCenterPos);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        const double zoomDispatchMilliseconds =
                zoomTimer.nsecsElapsed() / 1000000.0;
        QVERIFY2(waitUntil([&]() {
                     const auto diagnostics =
                             view->nativeMetalRendererDiagnostics();
                     return diagnostics.requestedRenderGeneration
                                    > zoomRequestedBefore
                             && diagnostics.presentedRenderGeneration
                                    >= diagnostics.requestedRenderGeneration;
                 }, 30000),
                 qPrintable(sample.absoluteFilePath()));
        const double zoomMilliseconds = zoomTimer.nsecsElapsed() / 1000000.0;

        QScrollBar *horizontal = view->horizontalScrollBar();
        QScrollBar *vertical = view->verticalScrollBar();
        horizontal->setValue((horizontal->minimum() + horizontal->maximum()) / 2);
        vertical->setValue((vertical->minimum() + vertical->maximum()) / 2);
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QTest::qWait(50);

        const quint64 panRequestedBefore =
                view->nativeMetalRendererDiagnostics().requestedRenderGeneration;
        QElapsedTimer panTimer;
        panTimer.start();
        for (int step = 0; step < 48; ++step)
        {
            horizontal->setValue(horizontal->value() + (step % 2 == 0 ? 7 : -5));
            vertical->setValue(vertical->value() + (step % 2 == 0 ? 5 : -3));
            QCoreApplication::processEvents(QEventLoop::AllEvents);
        }
        const double panDispatchMilliseconds =
                panTimer.nsecsElapsed() / 1000000.0;
        QTest::qWait(40);
        QVERIFY2(waitUntil([&]() {
                     const auto diagnostics =
                             view->nativeMetalRendererDiagnostics();
                     return diagnostics.requestedRenderGeneration
                                    > panRequestedBefore
                             && diagnostics.presentedRenderGeneration
                                    >= diagnostics.requestedRenderGeneration;
                 }, 30000),
                 qPrintable(sample.absoluteFilePath()));
        const double panMilliseconds = panTimer.nsecsElapsed() / 1000000.0;

        const auto &details = window.getCurrentFileDetails();
        const auto renderer = view->nativeMetalRendererDiagnostics();
        qInfo().noquote() << QStringLiteral(
            "SDR_PERF {\"file\":\"%1\",\"width\":%2,\"height\":%3,"
            "\"cold_ms\":%4,\"zoom_ms\":%5,\"zoom_dispatch_ms\":%6,"
            "\"pan_48_ms\":%7,\"pan_dispatch_ms\":%8,\"decode_ms\":%9,"
            "\"native_metal\":true,\"gpu_ms\":%10,\"encode_ms\":%11,"
            "\"present_interval_ms\":%12,\"request_to_present_ms\":%13,"
            "\"presented_frames\":%14}")
            .arg(sample.fileName())
            .arg(details.loadedPixmapSize.width())
            .arg(details.loadedPixmapSize.height())
            .arg(coldMilliseconds, 0, 'f', 3)
            .arg(zoomMilliseconds, 0, 'f', 3)
            .arg(zoomDispatchMilliseconds, 0, 'f', 3)
            .arg(panMilliseconds, 0, 'f', 3)
            .arg(panDispatchMilliseconds, 0, 'f', 3)
            .arg(details.decodeMilliseconds, 0, 'f', 3)
            .arg(renderer.lastGPUExecutionMilliseconds, 0, 'f', 3)
            .arg(renderer.lastRenderMilliseconds, 0, 'f', 3)
            .arg(renderer.lastPresentedIntervalMilliseconds, 0, 'f', 3)
            .arg(renderer.lastRequestToPresentationMilliseconds, 0, 'f', 3)
            .arg(renderer.presentedFrameCount);
    }

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-PRELOAD-SYS-LARGE-NEIGHBOR-SHUTDOWN
// Test purpose: measure the shutdown tail left by an adjacent large-image
// preload without conflating it with the foreground image's decode.
// Preconditions: FOVELLE_LARGE_SDR_SAMPLE names the supplied readable PNG.
// Input data: a tiny current PNG followed by a link to the large PNG.
// Steps: inspect admission metadata, open the tiny image with adjacent
// preloading enabled, let the metadata-only worker finish, destroy the
// window/loader, then wait for the global pool. Expected result: the supplied
// neighbor is rejected before irreversible full-image decoding and shutdown
// remains below one second.
// Postcondition: all pool callbacks are drained and the temporary link is gone.
void SDRSampleInteractionTests::testLargeNeighborPreloadShutdownProbe()
{
    const QString largeSample =
            QString::fromUtf8(qgetenv("FOVELLE_LARGE_SDR_SAMPLE"));
    QVERIFY2(QFileInfo::exists(largeSample), qPrintable(largeSample));
    const auto admission = QVImageLoader::preloadAdmissionForFile(largeSample);
    QVERIFY(!admission.allowed);
    QCOMPARE(admission.reason, QStringLiteral("source-byte-limit"));

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString currentPath = createTestImage(
            directory, "01-current", Qt::red, QSize(64, 64));
    const QString neighborPath = directory.filePath("02-large.png");
    QVERIFY(!currentPath.isEmpty());
    QVERIFY2(QFile::link(largeSample, neighborPath), qPrintable(neighborPath));

    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"preloadingmode", static_cast<int>(Qv::PreloadMode::Adjacent)}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    auto window = std::make_unique<MainWindow>();
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    window->resize(800, 600);
    window->show();
    QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
    window->openFile(currentPath);
    QTRY_COMPARE_WITH_TIMEOUT(
        window->getCurrentFileDetails().fileInfo.absoluteFilePath(),
        currentPath, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(window->getIsPixmapLoaded(), 5000);
    QTest::qWait(750);
    QTRY_COMPARE_WITH_TIMEOUT(
            QThreadPool::globalInstance()->activeThreadCount(), 0, 3000);

    QElapsedTimer shutdownTimer;
    shutdownTimer.start();
    window->close();
    window.reset();
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents(QEventLoop::AllEvents);
    const double shutdownMilliseconds =
            shutdownTimer.nsecsElapsed() / 1000000.0;
    qInfo().noquote() << QStringLiteral(
        "PRELOAD_EXIT {\"file_bytes\":%1,\"shutdown_ms\":%2,"
        "\"admission\":\"%3\"}")
        .arg(QFileInfo(largeSample).size())
        .arg(shutdownMilliseconds, 0, 'f', 3)
        .arg(admission.reason);
    QVERIFY2(shutdownMilliseconds < 1000.0,
             qPrintable(QString::number(shutdownMilliseconds)));

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SDR-SYS-LARGE-BLACK-TILES
// Test purpose: reproduce the user-visible solid-black block caused by
// evaluating a source larger than one Metal texture as a monolithic CI graph.
void SDRSampleInteractionTests::testLargeRasterWindowCaptureHasNoBlackTileBlock()
{
#ifndef Q_OS_MACOS
    QSKIP("The native WindowServer/Metal capture contract is macOS-specific.");
#endif
    const QString samplePath = QString::fromUtf8(
            qgetenv("FOVELLE_SDR_BLACK_BLOCK_SAMPLE"));
    QVERIFY2(QFileInfo::exists(samplePath), qPrintable(samplePath));
    ScopedOptionValues options({
        {"windowresizemode", static_cast<int>(Qv::WindowResizeMode::Never)},
        {"calculatedzoommode", static_cast<int>(Qv::CalculatedZoomMode::ZoomToFit)},
        {"smoothscalingmode", static_cast<int>(Qv::SmoothScalingMode::Disabled)},
        {"preloadingmode", static_cast<int>(Qv::PreloadMode::Disabled)},
        {"checkerboardbackground", false},
        {"onetoonepixelsizing", false}
    });
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.resize(1200, 800);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    QElapsedTimer openTimer;
    openTimer.start();
    window.openFile(samplePath);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesNativeSDRMetalRenderer(), 120000);
    QTRY_VERIFY_WITH_TIMEOUT(
            view->nativeMetalRendererDiagnostics().firstFramePresented, 120000);
    const double openMilliseconds = openTimer.nsecsElapsed() / 1000000.0;
    const auto beforeZoom = view->nativeMetalRendererDiagnostics();
    QVERIFY(beforeZoom.usesMaterializedSDRTiles);
    QVERIFY(beforeZoom.sdrTileCount > 1);

    // Compare the real WindowServer presentation at fit zoom with the complete
    // bounded Qt proxy created from the same decoded CGImage. Grayscale Pearson
    // correlation tolerates ColorSync/display conversion but fails decisively
    // when 2048-row source bands are placed in reverse vertical order.
    QTest::qWait(150);
    const QPixmap fittedWindowPixmap = window.screen()->grabWindow(window.winId());
    const QImage fittedWindow = fittedWindowPixmap.toImage();
    QVERIFY(!fittedWindow.isNull());
    QRect fittedContentRect = view->mapFromScene(
            QRectF(QPointF(), QSizeF(window.getCurrentFileDetails().loadedPixmapSize)))
            .boundingRect().intersected(view->viewport()->rect());
    const QPoint fittedInWindow = view->viewport()->mapTo(
            &window, fittedContentRect.topLeft());
    const qreal fittedDpr = fittedWindowPixmap.devicePixelRatio();
    QRect fittedPixelRect(
            qRound(fittedInWindow.x() * fittedDpr),
            qRound(fittedInWindow.y() * fittedDpr),
            qRound(fittedContentRect.width() * fittedDpr),
            qRound(fittedContentRect.height() * fittedDpr));
    fittedPixelRect = fittedPixelRect.intersected(fittedWindow.rect()).adjusted(
            4, 4, -4, -4);
    QVERIFY(fittedPixelRect.width() > 400 && fittedPixelRect.height() > 300);
    const QImage fittedPresentation = fittedWindow.copy(fittedPixelRect);
    QVGraphicsImageItem *proxyItem = nullptr;
    for (QGraphicsItem *item : view->scene()->items()) {
        if (auto *candidate = dynamic_cast<QVGraphicsImageItem *>(item)) {
            proxyItem = candidate;
            break;
        }
    }
    QVERIFY(proxyItem);
    QVERIFY(!proxyItem->pixmap().isNull());
    const QSize correlationSize(256, 192);
    const QImage actualGray = fittedPresentation
            .scaled(correlationSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_Grayscale8);
    const QImage expectedGray = proxyItem->pixmap().toImage()
            .scaled(correlationSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_Grayscale8);
    double actualMean = 0.0;
    double expectedMean = 0.0;
    const int correlationPixels = correlationSize.width() * correlationSize.height();
    for (int y = 0; y < correlationSize.height(); ++y) {
        const uchar *actualRow = actualGray.constScanLine(y);
        const uchar *expectedRow = expectedGray.constScanLine(y);
        for (int x = 0; x < correlationSize.width(); ++x) {
            actualMean += actualRow[x];
            expectedMean += expectedRow[x];
        }
    }
    actualMean /= correlationPixels;
    expectedMean /= correlationPixels;
    double covariance = 0.0;
    double actualVariance = 0.0;
    double expectedVariance = 0.0;
    for (int y = 0; y < correlationSize.height(); ++y) {
        const uchar *actualRow = actualGray.constScanLine(y);
        const uchar *expectedRow = expectedGray.constScanLine(y);
        for (int x = 0; x < correlationSize.width(); ++x) {
            const double actualDelta = actualRow[x] - actualMean;
            const double expectedDelta = expectedRow[x] - expectedMean;
            covariance += actualDelta * expectedDelta;
            actualVariance += actualDelta * actualDelta;
            expectedVariance += expectedDelta * expectedDelta;
        }
    }
    const double fittedCorrelation = covariance
            / std::sqrt(actualVariance * expectedVariance);
    qInfo().noquote() << QStringLiteral(
            "SDR_LARGE_FIT_FIDELITY {\"correlation\":%1,"
            "\"tiles_total\":%2,\"display_width\":%3,\"display_height\":%4}")
            .arg(fittedCorrelation, 0, 'f', 6)
            .arg(beforeZoom.sdrTileCount)
            .arg(fittedPresentation.width())
            .arg(fittedPresentation.height());
    QVERIFY2(fittedCorrelation > 0.90,
             qPrintable(QString::number(fittedCorrelation)));

    const quint64 fullBeforeZoom = beforeZoom
            .sdrAuthoritativePresentedFrameCount;
    QElapsedTimer zoomTimer;
    zoomTimer.start();
    view->zoomAbsolute(4.0, Qv::CalculateViewportCenterPos);
    while (view->nativeMetalRendererDiagnostics()
                   .sdrAuthoritativePresentedFrameCount <= fullBeforeZoom
           && zoomTimer.elapsed() < 120000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents);
        QTest::qWait(1);
    }
    QVERIFY(view->nativeMetalRendererDiagnostics()
                    .sdrAuthoritativePresentedFrameCount > fullBeforeZoom);
    const double zoomMilliseconds = zoomTimer.nsecsElapsed() / 1000000.0;
    QTest::qWait(150);

    const QPixmap capturedPixmap = window.screen()->grabWindow(window.winId());
    const QImage capture = capturedPixmap.toImage().convertToFormat(
            QImage::Format_RGBA8888);
    QVERIFY(!capture.isNull());
    const qreal dpr = capturedPixmap.devicePixelRatio();
    const QSize sourceSize = window.getCurrentFileDetails().loadedPixmapSize;
    QRect contentRect = view->mapFromScene(
            QRectF(QPointF(), QSizeF(sourceSize))).boundingRect()
            .intersected(view->viewport()->rect());
    const QPoint inWindow = view->viewport()->mapTo(
            &window, contentRect.topLeft());
    QRect pixelRect(
            qRound(inWindow.x() * dpr), qRound(inWindow.y() * dpr),
            qRound(contentRect.width() * dpr),
            qRound(contentRect.height() * dpr));
    pixelRect = pixelRect.intersected(capture.rect());
    QVERIFY(pixelRect.width() > 100 && pixelRect.height() > 100);

    constexpr int CellSize = 32;
    int blackCells = 0;
    int sampledCells = 0;
    for (int y = pixelRect.top(); y + CellSize <= pixelRect.bottom(); y += CellSize)
    {
        for (int x = pixelRect.left(); x + CellSize <= pixelRect.right(); x += CellSize)
        {
            int nearlyBlack = 0;
            for (int py = y; py < y + CellSize; ++py)
            {
                const QRgb *row = reinterpret_cast<const QRgb *>(
                        capture.constScanLine(py));
                for (int px = x; px < x + CellSize; ++px)
                {
                    const QColor color = QColor::fromRgba(row[px]);
                    if (color.red() <= 2 && color.green() <= 2
                        && color.blue() <= 2)
                        ++nearlyBlack;
                }
            }
            ++sampledCells;
            if (nearlyBlack >= CellSize * CellSize * 99 / 100)
                ++blackCells;
        }
    }
    const double blackCellRatio = sampledCells > 0
            ? static_cast<double>(blackCells) / sampledCells : 1.0;
    qInfo().noquote() << QStringLiteral(
            "SDR_BLACK_TILE_CAPTURE {\"black_cell_ratio\":%1,"
            "\"black_cells\":%2,\"sampled_cells\":%3,"
            "\"capture_width\":%4,\"capture_height\":%5,"
            "\"open_ms\":%6,\"first_zoom_ms\":%7,"
            "\"tiles_total\":%8,\"tiles_visible\":%9,"
            "\"materialized_bytes\":%10}")
            .arg(blackCellRatio, 0, 'f', 6)
            .arg(blackCells)
            .arg(sampledCells)
            .arg(capture.width())
            .arg(capture.height())
            .arg(openMilliseconds, 0, 'f', 3)
            .arg(zoomMilliseconds, 0, 'f', 3)
            .arg(view->nativeMetalRendererDiagnostics().sdrTileCount)
            .arg(view->nativeMetalRendererDiagnostics().sdrVisibleTileCount)
            .arg(view->nativeMetalRendererDiagnostics().sdrMaterializedBytes);
    QVERIFY2(blackCellRatio < 0.01, qPrintable(QString::number(blackCellRatio)));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void GraphicsViewTests::testZoomIsBoundedAt6400Percent()
{
    QCOMPARE(QVGraphicsView::boundedZoomLevel(1.0), 1.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(32.0), 32.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(64.0), 64.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(64.0001), 64.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(100.0), 64.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(
        std::numeric_limits<qreal>::infinity()), 64.0);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(-1.0), 0.01);
    QCOMPARE(QVGraphicsView::boundedZoomLevel(
        std::numeric_limits<qreal>::quiet_NaN()), 0.01);
}

void GraphicsViewTests::testVectorPanRepaintsOnlyExposedStrip()
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
    const QString path = svgSamplePath(dir);
    QVERIFY(!path.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(640, 480);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(path);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>();
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);
    view->zoomAbsolute(64.0, Qv::CalculateViewportCenterPos);
    QTRY_VERIFY_WITH_TIMEOUT(
        view->horizontalScrollBar()->maximum()
            > view->horizontalScrollBar()->minimum(),
        2000);
    QTest::qWait(100);
    view->viewport()->repaint();
    QCoreApplication::processEvents();

    QVERIFY(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    QScrollBar *bar = view->horizontalScrollBar();
    bar->setValue((bar->minimum() + bar->maximum()) / 2);
    QCoreApplication::processEvents();
    view->viewport()->repaint();
    QCoreApplication::processEvents();
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->vectorRenderCount() > 0, 5000);
    view->viewport()->repaint();
    QCoreApplication::processEvents();

    PaintRegionRecorder recorder;
    view->viewport()->installEventFilter(&recorder);
    bar->setValue(bar->value() + 6);
    QTRY_VERIFY_WITH_TIMEOUT(!recorder.recordedAreas().isEmpty(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);
    QCoreApplication::processEvents();
    view->viewport()->removeEventFilter(&recorder);

    const qint64 viewportArea = static_cast<qint64>(view->viewport()->width())
            * view->viewport()->height();
    // The first paint is the exposed strip produced by QGraphicsView's scroll
    // reuse. A fast worker can finish the vector tile while QTRY_VERIFY is
    // pumping events and schedule a later full-viewport refinement paint;
    // that distinct update must not be attributed to the scrollbar move.
    const qint64 scrollPaintArea = recorder.recordedAreas().constFirst();
    const qreal dirtyRatio = static_cast<qreal>(scrollPaintArea) / viewportArea;
    const qint64 maximumObservedPaintArea = *std::max_element(
        recorder.recordedAreas().cbegin(), recorder.recordedAreas().cend());
    const qreal maximumObservedDirtyRatio =
            static_cast<qreal>(maximumObservedPaintArea) / viewportArea;
    qInfo().noquote() << QStringLiteral(
        "VECTOR_PAN_REPAINT dirty_ratio=%1 max_observed_dirty_ratio=%2 "
        "paint_events=%3 viewport_area=%4")
        .arg(dirtyRatio, 0, 'f', 6)
        .arg(maximumObservedDirtyRatio, 0, 'f', 6)
        .arg(recorder.recordedAreas().size())
        .arg(viewportArea);
    QVERIFY2(dirtyRatio <= 0.05,
             qPrintable(QStringLiteral("unexpected full vector pan repaint ratio %1")
                        .arg(dirtyRatio, 0, 'f', 6)));

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
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

    // The interaction timer only describes the gesture state.  A vector tile
    // is produced by QFutureWatcher on a worker and becomes observable only
    // after the scene paints the completed result.  Poll that observable
    // output while processing events so slower GitHub runners do not inspect
    // an empty/stale diagnostic tile.
    const auto waitForRenderedVectorTile = [view]() {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 5000) {
            view->viewport()->repaint();
            if (view->vectorRenderCount() > 0
                && !view->lastVectorRasterSize().isEmpty())
                return true;
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            QTest::qWait(5);
        }
        view->viewport()->repaint();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        return view->vectorRenderCount() > 0
                && !view->lastVectorRasterSize().isEmpty();
    };

    window.openFile(epsPath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);
    QVERIFY(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    QCOMPARE(view->vectorImageFormat(), Qv::VectorImageFormat::Pdf);
    view->removeExpensiveScaling();
    QVERIFY(view->usesVectorRendering());
    view->zoomAbsolute(100.0, Qv::CalculateViewportCenterPos);
    QCOMPARE(view->getZoomLevel(), Qv::MaximumZoomLevel);
    QVERIFY(view->hasPendingVectorRefinement());
    view->viewport()->repaint();
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);
    if (!waitForRenderedVectorTile()) {
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        QFAIL("PDF vector refinement did not produce a painted tile within 5 seconds");
    }
    qInfo() << "VECTOR_TILE_READY format=pdf renders=" << view->vectorRenderCount()
            << "pixels=" << view->lastVectorRasterSize();
    QVERIFY(view->vectorRenderCount() > 0);
    // A scrollbar can appear after the first high-zoom request and shrink the
    // viewport while that exact tile is already in flight. Allow the bounded
    // pre-scroll tile plus a small exposure-rounding margin.
    const QSize maximumVisibleTile = view->viewport()->size()
            * view->viewport()->devicePixelRatioF() + QSize(296, 296);
    QVERIFY(view->lastVectorRasterSize().width() <= maximumVisibleTile.width());
    QVERIFY(view->lastVectorRasterSize().height() <= maximumVisibleTile.height());

    window.openFile(svgPath);
    QTRY_COMPARE_WITH_TIMEOUT(
        window.getCurrentFileDetails().fileInfo.absoluteFilePath(),
        QFileInfo(svgPath).absoluteFilePath(), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(view->usesVectorRendering(), 2000);
    QVERIFY(view->viewport()->testAttribute(Qt::WA_OpaquePaintEvent));
    QCOMPARE(view->vectorImageFormat(), Qv::VectorImageFormat::Svg);
    view->removeExpensiveScaling();
    QVERIFY(view->usesVectorRendering());
    view->zoomAbsolute(100.0, Qv::CalculateViewportCenterPos);
    QCOMPARE(view->getZoomLevel(), Qv::MaximumZoomLevel);
    QVERIFY(view->hasPendingVectorRefinement());
    view->viewport()->repaint();
    QTRY_VERIFY_WITH_TIMEOUT(!view->hasPendingVectorRefinement(), 5000);
    if (!waitForRenderedVectorTile()) {
        qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
        QFAIL("SVG vector refinement did not produce a painted tile within 5 seconds");
    }
    qInfo() << "VECTOR_TILE_READY format=svg renders=" << view->vectorRenderCount()
            << "pixels=" << view->lastVectorRasterSize();
    QVERIFY(view->vectorRenderCount() > 0);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void GraphicsViewTests::testVectorInteractionPaintCpuBudgetFor120Hz()
{
    // This is deliberately an application-side CPU paint budget probe. A
    // synchronous QWidget::repaint() cannot observe WindowServer submission,
    // VSync pacing, or photons on the panel, so no presented-FPS claim is made.
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
    QVERIFY(currentThreadCpuTimeNanoseconds().has_value());
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
        const double cpuCapacity = 1000.0 / average;
        qInfo().noquote() << QStringLiteral(
            "VECTOR_120HZ_CPU_BUDGET measurement=thread_cpu "
            "format=%1 interaction=%2 average_cpu_ms=%3 "
            "p99_cpu_ms=%4 max_cpu_ms=%5 cpu_capacity_fps=%6 count=%7")
            .arg(format, interaction)
            .arg(average, 0, 'f', 3)
            .arg(p99, 0, 'f', 3)
            .arg(maximum, 0, 'f', 3)
            .arg(cpuCapacity, 0, 'f', 3)
            .arg(samples.size());
        QVERIFY2(average <= FrameBudgetMilliseconds,
                 qPrintable(format + " " + interaction + " average"));
        QVERIFY2(p99 <= FrameBudgetMilliseconds,
                 qPrintable(format + " " + interaction + " p99"));
        // A synchronous repaint includes scheduler and WindowServer jitter;
        // retain the single-sample maximum as evidence without treating one
        // unrelated scheduling spike as a renderer regression.
        QVERIFY2(cpuCapacity >= 120.0,
                 qPrintable(format + " " + interaction + " CPU capacity"));
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
            view->zoomAbsolute(i % 2 == 0 ? 48.0 : 64.0,
                               Qv::CalculateViewportCenterPos);
            view->viewport()->repaint();
        }
        QVector<double> zoomSamples;
        zoomSamples.reserve(FrameCount);
        for (int i = 0; i < FrameCount; ++i)
        {
            const auto cpuStart = currentThreadCpuTimeNanoseconds();
            QVERIFY(cpuStart.has_value());
            const int triangularStep = i < FrameCount / 2
                    ? i : FrameCount - 1 - i;
            const qreal continuousZoom = 48.0
                    + triangularStep * 16.0 / (FrameCount / 2 - 1);
            view->zoomAbsolute(continuousZoom,
                               Qv::CalculateViewportCenterPos);
            view->viewport()->repaint();
            const auto cpuEnd = currentThreadCpuTimeNanoseconds();
            QVERIFY(cpuEnd.has_value());
            QVERIFY(*cpuEnd >= *cpuStart);
            zoomSamples.append(static_cast<double>(*cpuEnd - *cpuStart) / 1000000.0);
        }
        verifySamples(zoomSamples, document.first, QStringLiteral("zoom"));

        view->zoomAbsolute(64.0, Qv::CalculateViewportCenterPos);
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
            const auto cpuStart = currentThreadCpuTimeNanoseconds();
            QVERIFY(cpuStart.has_value());
            view->horizontalScrollBar()->setValue(
                view->horizontalScrollBar()->value() + (i % 2 == 0 ? 2 : -2));
            view->verticalScrollBar()->setValue(
                view->verticalScrollBar()->value() + (i % 3 == 0 ? 1 : -1));
            view->viewport()->repaint();
            const auto cpuEnd = currentThreadCpuTimeNanoseconds();
            QVERIFY(cpuEnd.has_value());
            QVERIFY(*cpuEnd >= *cpuStart);
            panSamples.append(static_cast<double>(*cpuEnd - *cpuStart) / 1000000.0);
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
    QCOMPARE(subtitleLabel->text(), QString("version 1.0.1"));

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
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-ESC-RESERVED
// Test purpose: prove a persisted image-action binding cannot compete with the
// window's bare Escape command.
// Preconditions: Zoom to Fit was previously saved as Escape; a zoomed image is
// visible in a normal window.
// Input data: the persisted Escape binding and one physical Escape key event.
// Steps: reload shortcuts, inspect the effective QAction map, zoom the image,
// then send Escape to the viewport.
// Expected result: the invalid binding is migrated away, no configurable action
// owns an Escape prefix, and Escape closes the window without changing zoom.
// Postcondition: the window and the user's original shortcut value are restored.
void WindowBehaviorTests::testEscapeIsReservedForWindowLifecycle()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);
    const QString escape = QKeySequence(Qt::Key_Escape).toString();
    ScopedShortcutValues shortcuts({{"zoomtofit", QStringList {escape}}});

    QVERIFY(ShortcutManager::beginsWithReservedEscape(
        QKeySequence(Qt::Key_Escape)));
    for (const auto &shortcut : qvApp->getShortcutManager().getShortcutsList())
    {
        for (const QKeySequence &sequence :
             ShortcutManager::stringListToKeySequenceList(shortcut.shortcuts))
            QVERIFY(!ShortcutManager::beginsWithReservedEscape(sequence));
    }
    const QAction *zoomToFitAction =
        qvApp->getActionManager().getAction("zoomtofit");
    QVERIFY(zoomToFitAction);
    for (const QKeySequence &sequence : zoomToFitAction->shortcuts())
        QVERIFY(!ShortcutManager::beginsWithReservedEscape(sequence));
    QSettings settings;
    QCOMPARE(
        settings.value(QStringLiteral("shortcuts/zoomtofit")).toStringList(),
        QStringList {});

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(
        dir, "reserved-escape", Qt::darkMagenta, QSize(1600, 1000));
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.resize(720, 500);
    window.show();
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&window);
    QT_WARNING_POP
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    auto *view = window.findChild<QVGraphicsView *>("graphicsView");
    QVERIFY(view);
    view->zoomIn();
    const qreal zoomedLevel = view->getZoomLevel();
    QVERIFY(!view->getCalculatedZoomMode().has_value());

    view->viewport()->setFocus();
    QTest::keyClick(view->viewport(), Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);
    QVERIFY(qFuzzyCompare(view->getZoomLevel(), zoomedLevel));

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

// AC-SHORTCUT-DISPLAY
// Test purpose: verify that a standard action exposes only its primary
// shortcut in the settings representation, not Qt's symbolic fallback name.
// Preconditions: the Qt GUI key-binding table is available on the current
// platform.
// Input data: QKeySequence::Open and its platform-provided bindings.
// Steps: convert the standard binding to the persisted list and then to the
// native display string.
// Expected result: exactly one binding is retained, it is the primary native
// shortcut (⌘O on macOS), and the display contains no "Open" alias.
// Postcondition: no settings, action, or widget state is changed.
void ShortcutSettingsTests::testPrimaryStandardShortcutDoesNotExposeActionName()
{
    const auto platformBindings = QKeySequence::keyBindings(QKeySequence::Open);
    QVERIFY(!platformBindings.isEmpty());

    const QStringList storedBindings =
        ShortcutManager::keyBindingsToStringList(QKeySequence::Open);
    QCOMPARE(storedBindings.size(), 1);
    QCOMPARE(storedBindings.constFirst(),
             platformBindings.constFirst().toString(QKeySequence::PortableText));

    const QString displayed = ShortcutManager::stringListToReadableString(storedBindings);
    QCOMPARE(displayed,
             platformBindings.constFirst().toString(QKeySequence::NativeText));
    QVERIFY(!displayed.contains(QStringLiteral("Open")));
}

// AC-SHORTCUT-COLUMN-WIDTH
// Test purpose: verify that the Shortcuts column consumes all remaining table
// width after the Action column has been sized.
// Preconditions: a visible Cocoa QVOptionsDialog has a populated Shortcuts
// table and its initial page metrics have been prepared.
// Input data: the two table columns and the current table viewport geometry.
// Steps: select Shortcuts and inspect header modes, section lengths, and
// horizontal scrolling.
// Expected result: the last column is stretched, the header exactly covers the
// viewport, both sections fit without a horizontal scrollbar, and the
// Shortcuts section has positive remaining width.
// Postcondition: the dialog is closed without changing settings.
void ShortcutSettingsTests::testShortcutsColumnFillsRemainingWidth()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});

    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.prepareForDisplay();
    dialog.show();

    auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
    QVERIFY(tabs);
    QVERIFY(table);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(table->isVisible(), 1000);

    auto *header = table->horizontalHeader();
    QVERIFY(header->stretchLastSection());
    QCOMPARE(header->sectionResizeMode(0), QHeaderView::Fixed);
    QCOMPARE(header->sectionResizeMode(1), QHeaderView::Stretch);
    QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(header->length(), table->viewport()->width());
    QCOMPARE(header->sectionSize(0) + header->sectionSize(1), header->length());
    QVERIFY(header->sectionSize(1) > 0);

    dialog.close();
}

static void sendShortcutCellDoubleClick(QTableWidget *table)
{
    const QRect cell = table->visualItemRect(table->item(0, 1));
    QVERIFY(cell.isValid());
    const QModelIndex index = table->indexAt(cell.center());
    QVERIFY(index.isValid());

    QSignalSpy doubleClickSpy(table, &QTableWidget::cellDoubleClicked);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, cell.center());
    if (doubleClickSpy.isEmpty())
    {
        // Cocoa's headless QTest backend can fail to deliver the native mouse
        // double-click to a visible table viewport. Invoke the signal produced
        // by that gesture so the test remains deterministic while exercising
        // the production signal-to-dialog connection.
        QVERIFY(QMetaObject::invokeMethod(table, "cellDoubleClicked",
                                          Qt::DirectConnection,
                                          Q_ARG(int, index.row()),
                                          Q_ARG(int, index.column())));
    }
}

// AC-SHORTCUT-DOUBLE-CLICK
// Test purpose: verify that double-clicking a shortcut cell opens its
// configuration dialog.
// Preconditions: a visible QVOptionsDialog is showing the populated
// Shortcuts table.
// Input data: row 0, column 1, delivered through QTest mouse double-click (with
// a deterministic signal fallback for the headless Cocoa backend).
// Steps: double-click the first cell in the Shortcuts column and observe the
// child QVShortcutDialog.
// Expected result: one visible configuration dialog is created for the row.
// Postcondition: the configuration dialog and settings dialog are closed
// without accepting a shortcut change.
void ShortcutSettingsTests::testDoubleClickOpensShortcutEditor()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});
    ScopedShortcutValues shortcuts({
        {QStringLiteral("open"), QStringList {
            QKeySequence(Qt::CTRL | Qt::Key_O).toString()
        }}
    });

    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.prepareForDisplay();
    dialog.show();
    auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
    QVERIFY(tabs);
    QVERIFY(table);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(table->isVisible(), 1000);
    sendShortcutCellDoubleClick(table);

    QTRY_VERIFY_WITH_TIMEOUT(!dialog.findChildren<QVShortcutDialog *>().isEmpty(), 1000);
    auto *editor = dialog.findChild<QVShortcutDialog *>();
    QVERIFY(editor);
    QTRY_VERIFY_WITH_TIMEOUT(editor->isVisible(), 1000);

    editor->reject();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.findChildren<QVShortcutDialog *>().isEmpty(), 1000);
    dialog.close();
}

// AC-SHORTCUT-WIDTH-STABILITY
// Test purpose: verify that accepting a shortcut update does not change the
// already presented Settings window width.
// Preconditions: a visible Shortcuts tab with a prepared fixed page width and
// a writable shortcut setting.
// Input data: one replacement shortcut, Ctrl+Alt+Shift+F12.
// Steps: double-click a shortcut cell, set the replacement, click OK, and
// compare the dialog frame and fixed-width contract before and after.
// Expected result: the new shortcut is displayed and persisted, while the
// Settings dialog width and fixed-width property remain identical.
// Postcondition: the dialog closes and ScopedShortcutValues restores the
// original shortcut setting.
void ShortcutSettingsTests::testShortcutUpdateKeepsSettingsWidth()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});
    ScopedShortcutValues shortcuts({
        {QStringLiteral("open"), QStringList {
            QKeySequence(Qt::CTRL | Qt::Key_O).toString()
        }}
    });

    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.prepareForDisplay();
    dialog.show();
    auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
    QVERIFY(tabs);
    QVERIFY(table);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(table->isVisible(), 1000);

    const int widthBefore = dialog.width();
    const int fixedWidthBefore = dialog.property("settingsFixedWidth").toInt();
    const QKeySequence replacement(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F12);

    sendShortcutCellDoubleClick(table);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.findChildren<QVShortcutDialog *>().isEmpty(), 1000);
    QPointer<QVShortcutDialog> editor = dialog.findChild<QVShortcutDialog *>();
    QVERIFY(editor);
    auto *keySequenceEdit = editor->findChild<QKeySequenceEdit *>
        (QStringLiteral("keySequenceEdit"));
    auto *buttonBox = editor->findChild<QDialogButtonBox *>(QStringLiteral("buttonBox"));
    QVERIFY(keySequenceEdit);
    QVERIFY(buttonBox);
    keySequenceEdit->setKeySequence(replacement);
    auto *okButton = buttonBox->button(QDialogButtonBox::Ok);
    QVERIFY(okButton);
    QTest::mouseClick(okButton, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(editor.isNull(), 1000);
    QCOMPARE(dialog.width(), widthBefore);
    QCOMPARE(dialog.property("settingsFixedWidth").toInt(), fixedWidthBefore);
    QCOMPARE(table->item(0, 1)->text(), replacement.toString(QKeySequence::NativeText));
    QCOMPARE(QSettings().value(QStringLiteral("shortcuts/open")).toStringList(),
             QStringList {replacement.toString(QKeySequence::PortableText)});

    dialog.close();
}

// AC-SHORTCUT-ESC-CANCEL
// Test purpose: verify that Esc in the shortcut editor has exactly the Cancel
// behavior and does not commit the edited value.
// Preconditions: a visible Shortcuts tab has opened the editor by double-click
// and the current shortcut is persisted.
// Input data: a replacement shortcut followed by a bare Escape key event.
// Steps: set the replacement, focus QKeySequenceEdit, send Esc, and observe
// the dialog result, rejection signal, shortcut signal, table, and settings.
// Expected result: the editor is rejected like Cancel, emits no accepted
// shortcut update, and leaves the table and persisted value unchanged.
// Postcondition: all dialogs close and ScopedShortcutValues restores state.
void ShortcutSettingsTests::testEscapeRejectsShortcutEditorLikeCancel()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});
    const QString originalShortcut = QKeySequence(Qt::CTRL | Qt::Key_O).toString();
    ScopedShortcutValues shortcuts({
        {QStringLiteral("open"), QStringList {originalShortcut}}
    });

    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.prepareForDisplay();
    dialog.show();
    auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
    QVERIFY(tabs);
    QVERIFY(table);
    tabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(table->isVisible(), 1000);
    const QString originalCell = table->item(0, 1)->text();

    sendShortcutCellDoubleClick(table);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.findChildren<QVShortcutDialog *>().isEmpty(), 1000);
    QPointer<QVShortcutDialog> editor = dialog.findChild<QVShortcutDialog *>();
    QVERIFY(editor);
    auto *keySequenceEdit = editor->findChild<QKeySequenceEdit *>
        (QStringLiteral("keySequenceEdit"));
    QVERIFY(keySequenceEdit);
    QSignalSpy changedSpy(editor, &QVShortcutDialog::shortcutsListChanged);
    QSignalSpy rejectedSpy(editor, &QDialog::rejected);

    keySequenceEdit->setKeySequence(
        QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F12));
    keySequenceEdit->setFocus();
    QTest::keyClick(keySequenceEdit, Qt::Key_Escape);

    QTRY_VERIFY_WITH_TIMEOUT(editor.isNull(), 1000);
    QCOMPARE(rejectedSpy.count(), 1);
    QCOMPARE(changedSpy.count(), 0);
    QCOMPARE(table->item(0, 1)->text(), originalCell);
    QCOMPARE(QSettings().value(QStringLiteral("shortcuts/open")).toStringList(),
             QStringList {originalShortcut});

    dialog.close();
}

// TC-THEME-SETTINGS
// Test purpose: verify Theme replaces both removed color controls and persists.
// Preconditions: Settings dialog can be constructed with a controlled Light
// Theme value.
// Input data: Light Theme and Dark Theme combo-box entries.
// Steps: inspect the combo, confirm removed controls are absent, and select
// Dark without using a dialog action button.
// Expected result: Light, Dark, and System entries exist in that order, the
// controlled Light value is shown, and Dark is saved immediately under the
// theme setting.
// Postcondition: the original theme setting is restored.
void WindowBehaviorTests::testThemeSettingsReplaceRemovedColorControls()
{
    ScopedOptionValues options({{"theme", static_cast<int>(Qv::Theme::Light)}});

    QVOptionsDialog dialog;
    auto *themeComboBox = dialog.findChild<QComboBox *>("themeComboBox");
    QVERIFY(themeComboBox);
    QCOMPARE(themeComboBox->count(), 3);
    QCOMPARE(themeComboBox->itemText(0), QStringLiteral("Light"));
    QCOMPARE(themeComboBox->itemText(1), QStringLiteral("Dark"));
    QCOMPARE(themeComboBox->itemData(0).toInt(), static_cast<int>(Qv::Theme::Light));
    QCOMPARE(themeComboBox->itemData(1).toInt(), static_cast<int>(Qv::Theme::Dark));
    QCOMPARE(themeComboBox->itemText(2), QStringLiteral("System"));
    QCOMPARE(themeComboBox->itemData(2).toInt(), static_cast<int>(Qv::Theme::System));
    QCOMPARE(themeComboBox->currentData().toInt(), static_cast<int>(Qv::Theme::Light));
    QVERIFY(!dialog.findChild<QCheckBox *>("bgColorCheckbox"));
    QVERIFY(!dialog.findChild<QPushButton *>("bgColorButton"));
    QVERIFY(!dialog.findChild<QCheckBox *>("darkTitlebarCheckbox"));

    themeComboBox->setCurrentIndex(1);
    QCOMPARE(qvApp->getSettingsManager().getEnum<Qv::Theme>("theme"), Qv::Theme::Dark);
}

// TC-SETTINGS-NATIVE-TABS
// Test purpose: verify that Settings uses the Preview-style horizontal tab
// contract and persists non-restart settings in the same event turn.
// Preconditions: SettingsManager is initialized and Theme is Light.
// Input data: the Settings dialog and the Theme combo-box selection Dark.
// Steps: inspect the tab bar and removed controls, then change Theme.
// Expected result: General replaces Display/Miscellaneous, tabs are horizontal, Settings has no global action button
// box or Titlebar text controls, and the manager immediately reports Dark.
// Postcondition: ScopedOptionValues restores the original Theme.
void WindowBehaviorTests::testSettingsDialogUsesNativeTabContractAndImmediatePersistence()
{
    ScopedOptionValues options({{"theme", static_cast<int>(Qv::Theme::Light)}});

    QVOptionsDialog dialog;
    auto *tabs = dialog.findChild<QTabBar *>("categoryTabs");
    QVERIFY(tabs);
    QCOMPARE(tabs->count(), 3);
    QCOMPARE(tabs->tabText(0), QStringLiteral("General"));
    QVERIFY(tabs->shape() == QTabBar::RoundedNorth || tabs->shape() == QTabBar::TriangularNorth);
    QVERIFY(tabs->isHidden());
    QVERIFY(!dialog.findChild<QDialogButtonBox *>("buttonBox"));
    QVERIFY(!dialog.findChild<QComboBox *>("titlebarComboBox"));
    QVERIFY(!dialog.findChild<QLineEdit *>("customTitlebarLineEdit"));

    auto *themeComboBox = dialog.findChild<QComboBox *>("themeComboBox");
    QVERIFY(themeComboBox);
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.show();
    QTRY_VERIFY_WITH_TIMEOUT(
        QVCocoaFunctions::hasNativeSettingsToolbar(dialog.windowHandle()),
        2000);
    QTRY_COMPARE_WITH_TIMEOUT(dialog.windowTitle(), tabs->tabText(tabs->currentIndex()), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(
        QVCocoaFunctions::getWindowAppearanceName(dialog.windowHandle()),
        QStringLiteral("Aqua"),
        2000);
    themeComboBox->setCurrentIndex(themeComboBox->findData(static_cast<int>(Qv::Theme::Dark)));
    QCOMPARE(qvApp->getSettingsManager().getEnum<Qv::Theme>("theme"), Qv::Theme::Dark);
    QTRY_COMPARE_WITH_TIMEOUT(
        QVCocoaFunctions::getWindowAppearanceName(dialog.windowHandle()),
        QStringLiteral("DarkAqua"),
        2000);
    dialog.close();
}

// TC-SETTINGS-CONTENT-GEOMETRY
// Test purpose: verify the shared content-derived Settings width and per-tab
// minimum heights, including the 16-row Shortcuts viewport.
// Preconditions: the production QVOptionsDialog and its populated shortcut
// table are available.
// Input data: category indexes 0 (General), 1 (Shortcuts), and 2 (Mouse).
// Steps: show the dialog, switch through all categories, and inspect window
// constraints, scroll ranges, natural content sizes, and the last visible row.
// Expected result: every tab has the same minimum no-horizontal-scroll width;
// General and Mouse have exact no-vertical-scroll heights; Shortcuts shows
// exactly 16 complete data rows and row 16 is Random File.
// Postcondition: the dialog is closed and no settings are modified.
void WindowBehaviorTests::testSettingsDialogUsesFixedWidthAndTabHeights()
{
    ScopedOptionValues options({{"language", QStringLiteral("en")}});

    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *tabs = dialog.findChild<QTabBar *>("categoryTabs");
    auto *generalScrollArea = dialog.findChild<QScrollArea *>("generalScrollArea");
    auto *mouseScrollArea = dialog.findChild<QScrollArea *>("mouseScrollArea");
    auto *table = dialog.findChild<QTableWidget *>("shortcutsTable");
    auto *stack = dialog.findChild<QStackedWidget *>("stackedWidget");
    QVERIFY(tabs);
    QVERIFY(generalScrollArea);
    QVERIFY(mouseScrollArea);
    QVERIFY(table);
    QVERIFY(stack);
    dialog.prepareForDisplay();
    const int fixedWidth = dialog.property("settingsFixedWidth").toInt();
    const int naturalPageWidth = dialog.property("settingsNaturalPageWidth").toInt();
    QVERIFY(fixedWidth > 0);
    QVERIFY(naturalPageWidth > 0);
    QCOMPARE(dialog.minimumWidth(), fixedWidth);
    QCOMPARE(dialog.maximumWidth(), fixedWidth);
    QCOMPARE(stack->width(), naturalPageWidth);
    QCOMPARE(naturalPageWidth,
             qMax(dialog.property("settingsGeneralNaturalWidth").toInt(),
                  qMax(dialog.property("settingsMouseNaturalWidth").toInt(),
                       dialog.property("settingsShortcutsNaturalWidth").toInt())));
    QVERIFY(!dialog.isSizeGripEnabled());
    QCOMPARE(table->rowCount() >= 16, true);
    QCOMPARE(table->item(15, 0)->text(), QStringLiteral("Random File"));

            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
            tabs->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    QCOMPARE(dialog.width(), fixedWidth);
    QCOMPARE(generalScrollArea->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(generalScrollArea->verticalScrollBar()->maximum(), 0);
    QVERIFY(!generalScrollArea->verticalScrollBar()->isVisible());
    QCOMPARE(generalScrollArea->widget()->minimumHeight(),
             generalScrollArea->viewport()->height());
    if (dialog.property("settingsGeneralNaturalWidth").toInt() == naturalPageWidth)
        QCOMPARE(generalScrollArea->widget()->minimumWidth(),
                 generalScrollArea->viewport()->width());
    const int generalHeight = dialog.height();
    QVERIFY(generalHeight > 0);

    tabs->setCurrentIndex(1);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    int visibleRowsHeight = 0;
    for (int row = 0; row < 16; ++row)
        visibleRowsHeight += table->rowHeight(row);
    const int expectedTableHeight = table->horizontalHeader()->height()
            + visibleRowsHeight + 2 * table->frameWidth();
    QCOMPARE(table->height(), expectedTableHeight);
    QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(table->viewport()->height(), visibleRowsHeight);
    if (dialog.property("settingsShortcutsNaturalWidth").toInt() == naturalPageWidth)
        QCOMPARE(table->viewport()->width(), table->horizontalHeader()->length());
    QVERIFY(table->visualItemRect(table->item(15, 0)).bottom()
            <= table->viewport()->rect().bottom());
    if (table->rowCount() > 16)
        QVERIFY(table->visualItemRect(table->item(16, 0)).bottom()
                > table->viewport()->rect().bottom());
    const int shortcutsHeight = dialog.height();
    QVERIFY(shortcutsHeight > 0);

    tabs->setCurrentIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    QCOMPARE(dialog.width(), fixedWidth);
    QCOMPARE(mouseScrollArea->horizontalScrollBar()->maximum(), 0);
    QCOMPARE(mouseScrollArea->verticalScrollBar()->maximum(), 0);
    QVERIFY(!mouseScrollArea->verticalScrollBar()->isVisible());
    QCOMPARE(mouseScrollArea->widget()->minimumHeight(),
             mouseScrollArea->viewport()->height());
    if (dialog.property("settingsMouseNaturalWidth").toInt() == naturalPageWidth)
        QCOMPARE(mouseScrollArea->widget()->minimumWidth(),
                 mouseScrollArea->viewport()->width());
    QVERIFY(dialog.height() > 0);
    QVERIFY(dialog.height() != shortcutsHeight || dialog.height() != generalHeight);

    dialog.close();
}

// TC-SETTINGS-TAB-TRANSITION
// Test purpose: verify that changing a Settings category animates the
// different fixed window heights instead of jumping directly between frames.
// Preconditions: a visible production Settings dialog with General,
// Shortcuts, and Mouse categories.
// Input data: category indexes 0 and 2, plus the production animation object.
// Steps: show General, switch to Mouse, observe the transition state, and
// wait for the animation to settle.
// Expected result: the animation is configured for 180 ms, becomes active for
// the size change, and ends at the Mouse natural height.
// Postcondition: the dialog is closed and tab/geometry settings are restored.
void WindowBehaviorTests::testSettingsTabTransitionAndMouseReopen()
{
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    ScopedSettingPreserver geometrySetting(QStringLiteral("optionsgeometry"));

    QSize mouseSize;
    {
        QVOptionsDialog dialog;
        dialog.setAttribute(Qt::WA_DeleteOnClose, false);
        auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
        auto *animation = dialog.findChild<QPropertyAnimation *>(QStringLiteral("settingsCategorySizeAnimation"));
        QVERIFY(tabs);
        QVERIFY(animation);
        QCOMPARE(animation->duration(), 180);

        dialog.show();
        QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
        tabs->setCurrentIndex(0);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
        const int generalHeight = dialog.height();

        tabs->setCurrentIndex(2);
        QVERIFY(dialog.property("settingsCategoryTransitionActive").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
        mouseSize = dialog.size();
        QVERIFY(mouseSize.height() > 0);
        QVERIFY(mouseSize.height() != generalHeight);
        dialog.close();
        QVERIFY(!dialog.isVisible());
    }

    // QVApplication creates a fresh WA_DeleteOnClose dialog on every open.
    // A deliberately unrelated legacy geometry must not compete with the
    // content-derived size of the persisted Mouse category.
    QDialog unrelatedGeometry;
    unrelatedGeometry.setGeometry(40, 80, 420, 420);
    QSettings settings;
    settings.setValue(QStringLiteral("optionsgeometry"), unrelatedGeometry.saveGeometry());
    settings.sync();

    QVOptionsDialog reopened;
    reopened.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *reopenedTabs = reopened.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    QVERIFY(reopenedTabs);
    QCOMPARE(reopenedTabs->currentIndex(), 2);
    reopened.show();
    QTRY_VERIFY_WITH_TIMEOUT(reopened.isVisible(), 1000);
    QCOMPARE(reopened.size(), mouseSize);
    QVERIFY(!reopened.property("settingsCategoryTransitionActive").toBool());
    reopened.close();
    QVERIFY(!QSettings().contains(QStringLiteral("optionsgeometry")));
}

// TC-SETTINGS-TAB-FOCUS
// Test purpose: verify returning to General never transfers keyboard focus to
// the Appearance combo box as a side effect of native toolbar navigation.
// Preconditions: a visible production Settings dialog and the General theme
// combo box.
// Input data: a focus request on the Appearance combo followed by Mouse →
// General category changes.
// Steps: focus Appearance, leave General, return to General, and process the
// native/Qt event turn that settles first-responder state.
// Expected result: Appearance is not focused after the return transition.
// Postcondition: the dialog is closed without changing the theme value.
void WindowBehaviorTests::testSettingsTabSwitchDoesNotFocusAppearance()
{
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
    auto *appearance = dialog.findChild<QComboBox *>(QStringLiteral("themeComboBox"));
    QVERIFY(tabs);
    QVERIFY(appearance);

    dialog.show();
    dialog.raise();
    dialog.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isActiveWindow(), 1000);
    tabs->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    appearance->setFocus(Qt::OtherFocusReason);
    QTRY_VERIFY_WITH_TIMEOUT(appearance->hasFocus(), 1000);

    tabs->setCurrentIndex(2);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    tabs->setCurrentIndex(0);
    QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(!appearance->hasFocus(), 500);

    dialog.close();
}

// TC-SETTINGS-LOCALIZED-ALIGNMENT
// Test purpose: verify all translated General and Mouse form layouts share a
// label column and do not center independent form size hints.
// Preconditions: the production Settings dialog can be constructed.
// Input data: QFormLayout objects under General and Mouse content pages.
// Steps: enumerate each page's form layouts and compare column minimum widths,
// wrap policy, and horizontal form alignment.
// Expected result: each page has one shared label-column width, no row wrapping,
// both pages use the same label-column width, and left-aligned form origins keep
// localized labels from shifting fields.
// Postcondition: the dialog is destroyed without changing settings.
void WindowBehaviorTests::testLocalizedSettingsFormsUseSharedLabelColumns()
{
    QVOptionsDialog dialog;
    auto *general = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
    auto *mouse = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
    QVERIFY(general);
    QVERIFY(mouse);

    const auto verifyPage = [](QWidget *page) {
        const auto layouts = page->findChildren<QFormLayout *>();
        QVERIFY(!layouts.isEmpty());
        const int expectedWidth = page->property("settingsAlignedLabelColumnWidth").toInt();
        QVERIFY(expectedWidth > 0);
        for (auto *layout : layouts)
        {
            QCOMPARE(layout->rowWrapPolicy(), QFormLayout::DontWrapRows);
            QVERIFY(!(layout->formAlignment() & Qt::AlignHCenter));
            for (int row = 0; row < layout->rowCount(); ++row)
            {
                auto *item = layout->itemAt(row, QFormLayout::LabelRole);
                if (item && item->widget())
                    QCOMPARE(item->widget()->minimumWidth(), expectedWidth);
            }
        }
    };

    verifyPage(general->widget());
    verifyPage(mouse->widget());
    QCOMPARE(general->widget()->property("settingsAlignedLabelColumnWidth").toInt(),
             mouse->widget()->property("settingsAlignedLabelColumnWidth").toInt());
}

// TC-SETTINGS-PER-LANGUAGE-GEOMETRY
// Test purpose: verify geometry is re-derived from each installed language
// catalog while retaining the same no-scroll / 16-row constraints.
void WindowBehaviorTests::testSettingsDialogSizesFollowTranslations()
{
#ifndef FOVELLE_TRANSLATIONS_DIR
    QSKIP("Translation catalogs were disabled for this build");
#else
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    ScopedSettingPreserver geometrySetting(QStringLiteral("optionsgeometry"));
    const QList<QString> languages {
        QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("ja"),
        QStringLiteral("zh_Hans"), QStringLiteral("zh_Hant")
    };
    QHash<QString, QList<QSize>> sizesByLanguage;

    for (const QString &language : languages)
    {
        SourceLanguageTranslator sourceTranslator;
        QTranslator catalogTranslator;
        QTranslator *translator = &sourceTranslator;
        if (language != QStringLiteral("en"))
        {
            const QString path = QStringLiteral(FOVELLE_TRANSLATIONS_DIR "/qview_%1.qm")
                    .arg(language);
            QVERIFY2(catalogTranslator.load(path), qPrintable(path));
            QVERIFY(!catalogTranslator.translate("ShortcutManager", "Random File").isNull());
            translator = &catalogTranslator;
        }
        QVERIFY(QCoreApplication::installTranslator(translator));

        {
            ScopedOptionValues options({{"language", language}});
            QSettings settings;
            settings.setValue(QStringLiteral("optionstab"), 0);
            settings.setValue(QStringLiteral("optionstabversion"), 2);
            settings.sync();

            QVOptionsDialog dialog;
            dialog.setAttribute(Qt::WA_DeleteOnClose, false);
            auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
            auto *general = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
            auto *mouse = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
            auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
            QVERIFY(tabs);
            QVERIFY(general);
            QVERIFY(mouse);
            QVERIFY(table);

            // ShortcutManager is constructed once before this test installs
            // its per-row translator. Mirror a real restart by translating
            // those startup-owned readable names before the dialog's final
            // native-toolbar measurement pass.
            const auto &shortcutSources = qvApp->getShortcutManager().getShortcutsList();
            for (int row = 0; row < table->rowCount() && row < shortcutSources.size(); ++row)
            {
                const QByteArray source = shortcutSources.at(row).readableName.toUtf8();
                table->item(row, 0)->setText(
                    QCoreApplication::translate("ShortcutManager", source.constData()));
            }
            dialog.prepareForDisplay();

            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
            QList<QSize> categorySizes;
            for (int category = 0; category < tabs->count(); ++category)
            {
                tabs->setCurrentIndex(category);
                QTRY_VERIFY_WITH_TIMEOUT(
                    !dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
                categorySizes.append(dialog.size());
                QCOMPARE(dialog.width(), dialog.property("settingsFixedWidth").toInt());
            }

            QCOMPARE(categorySizes.at(0).width(), categorySizes.at(1).width());
            QCOMPARE(categorySizes.at(1).width(), categorySizes.at(2).width());
            tabs->setCurrentIndex(0);
            QTRY_VERIFY_WITH_TIMEOUT(
                !dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            QCOMPARE(general->horizontalScrollBar()->maximum(), 0);
            QCOMPARE(general->verticalScrollBar()->maximum(), 0);
            QCOMPARE(general->viewport()->height(), general->widget()->minimumHeight());
            tabs->setCurrentIndex(2);
            QTRY_VERIFY_WITH_TIMEOUT(
                !dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            QCOMPARE(mouse->horizontalScrollBar()->maximum(), 0);
            QCOMPARE(mouse->verticalScrollBar()->maximum(), 0);
            QCOMPARE(mouse->viewport()->height(), mouse->widget()->minimumHeight());
            QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
            QCOMPARE(table->item(15, 0)->text(),
                     QCoreApplication::translate("ShortcutManager", "Random File"));

            int visibleRowsHeight = 0;
            for (int row = 0; row < 16; ++row)
                visibleRowsHeight += table->rowHeight(row);
            QCOMPARE(table->viewport()->height(), visibleRowsHeight);
            sizesByLanguage.insert(language, categorySizes);
            dialog.close();
        }

        QVERIFY(QCoreApplication::removeTranslator(translator));
    }

    QSet<int> languageWidths;
    for (const QString &language : languages)
        languageWidths.insert(sizesByLanguage.value(language).constFirst().width());
    QVERIFY2(languageWidths.size() > 1,
             "Translated content must produce content-derived, not globally fixed, geometry");
#endif
}

// TC-SETTINGS-FORM-ALIGNMENT
// Test purpose: verify every General and Mouse form uses a shared right-aligned
// label column and a left-aligned value column for every supported language.
// Preconditions: the Cocoa Qt test application, all five supported catalogs,
// and a writable isolated settings store are available.
// Input data: en, es, ja, zh_Hans, zh_Hant; every QFormLayout under General and
// Mouse, including combo boxes, spin boxes, checkboxes, radio-button layouts,
// and the centered association action.
// Steps: install one language catalog, show Settings, activate every form,
// inspect form and item alignments, then compare the runtime label/value
// column origins; repeat for all supported languages.
// Expected result: labels end at one shared right-aligned column, values begin
// at the following shared left-aligned column, no form is horizontally centered,
// and the action row remains intentionally centered.
// Postcondition: every dialog, translator, and temporary setting is restored.
void WindowBehaviorTests::testSettingsFormsAlignLabelsAndValues()
{
#ifndef FOVELLE_TRANSLATIONS_DIR
    QSKIP("Translation catalogs were disabled for this build");
#else
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    const QList<QString> languages {
        QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("ja"),
        QStringLiteral("zh_Hans"), QStringLiteral("zh_Hant")
    };
    const Qt::Alignment expectedLabelAlignment =
        Qt::AlignRight | Qt::AlignTrailing;
    const Qt::Alignment expectedLabelContentAlignment =
        expectedLabelAlignment | Qt::AlignVCenter;
    const Qt::Alignment expectedValueAlignment = Qt::AlignLeft | Qt::AlignTop;
    const QStringList valueOnlyNames {
        QStringLiteral("checkerboardBackgroundCheckbox"),
        QStringLiteral("reuseWindowCheckbox"),
        QStringLiteral("smallImagesOneToOneCheckbox"),
        QStringLiteral("askDeleteCheckbox")
    };

    const auto alignmentHas = [](const Qt::Alignment actual,
                                 const Qt::Alignment expected) {
        return (actual & expected) == expected;
    };

    for (const QString &language : languages)
    {
        SourceLanguageTranslator sourceTranslator;
        QTranslator catalogTranslator;
        QTranslator *translator = &sourceTranslator;
        if (language != QStringLiteral("en"))
        {
            const QString path = QStringLiteral(FOVELLE_TRANSLATIONS_DIR "/qview_%1.qm")
                    .arg(language);
            QVERIFY2(catalogTranslator.load(path), qPrintable(path));
            translator = &catalogTranslator;
        }
        QVERIFY(QCoreApplication::installTranslator(translator));

        {
            ScopedOptionValues options({
                {QStringLiteral("language"), language},
                {QStringLiteral("theme"), static_cast<int>(Qv::Theme::Dark)},
                {QStringLiteral("viewportmiddlebuttonmode"), static_cast<int>(Qv::ClickOrDrag::Click)}
            });
            QSettings settings;
            settings.setValue(QStringLiteral("optionstab"), 0);
            settings.setValue(QStringLiteral("optionstabversion"), 2);
            settings.sync();

            QVOptionsDialog dialog;
            dialog.setAttribute(Qt::WA_DeleteOnClose, false);
            auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
            auto *general = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
            auto *mouse = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
            QVERIFY(tabs);
            QVERIFY(general);
            QVERIFY(mouse);
            dialog.prepareForDisplay();
            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);

            const auto verifyPage = [&](QScrollArea *scrollArea) {
                QWidget *page = scrollArea->widget();
                QVERIFY(page);
                const int expectedLabelWidth =
                    page->property("settingsAlignedLabelColumnWidth").toInt();
                QVERIFY(expectedLabelWidth > 0);
                QCOMPARE(page->property("settingsLabelAlignment").toInt(),
                         int(expectedLabelAlignment));
                QCOMPARE(page->property("settingsValueAlignment").toInt(),
                         int(expectedValueAlignment));

                const auto layouts = page->findChildren<QFormLayout *>();
                QVERIFY(!layouts.isEmpty());
                const auto itemIsVisible = [](QLayoutItem *item) {
                    if (!item)
                        return false;
                    if (item->widget())
                        return item->widget()->isVisible();
                    if (item->layout())
                    {
                        for (int index = 0; index < item->layout()->count(); ++index)
                        {
                            auto *child = item->layout()->itemAt(index);
                            if (child && child->widget() && child->widget()->isVisible())
                                return true;
                        }
                    }
                    return false;
                };
                for (auto *layout : layouts)
                {
                    QCOMPARE(layout->labelAlignment(), expectedLabelAlignment);
                    QVERIFY(alignmentHas(layout->formAlignment(),
                                         Qt::AlignLeft | Qt::AlignTop));
                    QVERIFY(!(layout->formAlignment() & Qt::AlignHCenter));
                    QCOMPARE(layout->rowWrapPolicy(), QFormLayout::DontWrapRows);

                    int labelColumnRight = -1;
                    int fieldColumnLeft = -1;
                    for (int row = 0; row < layout->rowCount(); ++row)
                    {
                        auto *labelItem = layout->itemAt(row, QFormLayout::LabelRole);
                        auto *fieldItem = layout->itemAt(row, QFormLayout::FieldRole);
                        auto *spanningItem = layout->itemAt(row, QFormLayout::SpanningRole);
                        if (spanningItem && spanningItem->widget())
                        {
                            QVERIFY2(!valueOnlyNames.contains(spanningItem->widget()->objectName()),
                                     qPrintable(language + QStringLiteral(": value-only option must not span ")
                                                + spanningItem->widget()->objectName()));
                        }
                        if (fieldItem && fieldItem->widget()
                            && valueOnlyNames.contains(fieldItem->widget()->objectName()))
                        {
                            QVERIFY2(!spanningItem,
                                     qPrintable(language + QStringLiteral(": value-only option has both roles ")
                                                + fieldItem->widget()->objectName()));
                        }
                        if (!itemIsVisible(labelItem) && !itemIsVisible(fieldItem)
                            && !itemIsVisible(spanningItem))
                            continue;

                        if (labelItem && labelItem->widget())
                        {
                            auto *label = qobject_cast<QLabel *>(labelItem->widget());
                            QVERIFY(label);
                            if (!label->isVisible())
                                continue;
                            QCOMPARE(label->minimumWidth(), expectedLabelWidth);
                            const int thisLabelRight = label->geometry().x() + label->width();
                            if (labelColumnRight < 0)
                                labelColumnRight = thisLabelRight;
                            else
                            {
                                QVERIFY2(thisLabelRight == labelColumnRight,
                                         qPrintable(language + QStringLiteral(": label column ")
                                                    + layout->objectName() + QStringLiteral("/")
                                                    + label->objectName()));
                            }
                            QCOMPARE(label->alignment(), expectedLabelContentAlignment);
                        }

                        if (fieldItem)
                        {
                            if (!itemIsVisible(fieldItem))
                                continue;
                            const QString fieldName = fieldItem->widget()
                                ? fieldItem->widget()->objectName()
                                : fieldItem->layout() ? fieldItem->layout()->objectName()
                                                       : QStringLiteral("anonymous");
                            const bool isAssociationButton =
                                fieldName == QStringLiteral("associateFormatsButton");
                            if (isAssociationButton)
                            {
                                QVERIFY2(alignmentHas(fieldItem->alignment(),
                                                      Qt::AlignHCenter | Qt::AlignVCenter),
                                         qPrintable(language + QStringLiteral(": association alignment")));
                            }
                            else
                            {
                                QVERIFY2(alignmentHas(fieldItem->alignment(),
                                                      expectedValueAlignment),
                                         qPrintable(language + QStringLiteral(": value alignment ")
                                                    + fieldName));
                            }
                            auto *fieldWidget = fieldItem->widget();
                            QVERIFY(fieldWidget);
                            const QRect fieldGeometry = fieldItem->geometry();
                            QVERIFY(fieldGeometry.isValid());
                            if (!isAssociationButton)
                            {
                                if (fieldColumnLeft < 0)
                                    fieldColumnLeft = fieldGeometry.x();
                                else
                                    QCOMPARE(fieldGeometry.x(), fieldColumnLeft);

                                if (labelItem && labelItem->widget())
                                {
                                    auto *label = qobject_cast<QLabel *>(labelItem->widget());
                                    QVERIFY(label);
                                    const QRect labelGeometry = labelItem->geometry();
                                    const int expectedFieldX = labelGeometry.x()
                                        + labelGeometry.width() + layout->horizontalSpacing();
                                    QVERIFY2(fieldGeometry.x() >= expectedFieldX,
                                             qPrintable(language + QStringLiteral(": label/value horizontal gap ")
                                                        + layout->objectName() + QStringLiteral("/")
                                                        + label->objectName() + QStringLiteral("/")
                                                        + fieldName + QStringLiteral(" label=")
                                                        + QString::number(labelGeometry.x()) + QStringLiteral(",")
                                                        + QString::number(labelGeometry.width())
                                                        + QStringLiteral(" field=")
                                                        + QString::number(fieldGeometry.x()) + QStringLiteral(",")
                                                        + QString::number(fieldGeometry.width())
                                                        + QStringLiteral(" expectedX=")
                                                        + QString::number(expectedFieldX)));
                                    QCOMPARE(labelGeometry.top(), fieldGeometry.top());
                                    QCOMPARE(label->height(), fieldWidget->height());
                                }
                            }
                        }

                        if (spanningItem && spanningItem->widget()
                            && spanningItem->widget()->objectName()
                                == QStringLiteral("associateFormatsButton"))
                        {
                            QVERIFY(alignmentHas(spanningItem->alignment(),
                                                 Qt::AlignHCenter | Qt::AlignVCenter));
                        }
                        else if (spanningItem)
                        {
                            QVERIFY(alignmentHas(spanningItem->alignment(),
                                                 expectedValueAlignment));
                            const QRect spanningGeometry = spanningItem->geometry();
                            QVERIFY(spanningGeometry.isValid());
                            if (fieldColumnLeft < 0)
                                fieldColumnLeft = spanningGeometry.x();
                            else
                                QCOMPARE(spanningGeometry.x(), fieldColumnLeft);
                        }
                    }
                }
            };

            tabs->setCurrentIndex(0);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            verifyPage(general);
            tabs->setCurrentIndex(2);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            verifyPage(mouse);
            QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("middleButtonModeLabel")));
            QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeClickRadioButton")));
            QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeDragRadioButton")));
            dialog.close();
        }

        QVERIFY(QCoreApplication::removeTranslator(translator));
    }
#endif
}

// TC-SETTINGS-COLON-ALIGNMENT
// Test purpose: verify that every visible colon-terminated option name in the
// General and Mouse pages ends at one shared right edge in every language.
// Preconditions: all five translation catalogs, the Cocoa Qt test application,
// and an isolated writable settings store are available.
// Input data: en, es, ja, zh_Hans, zh_Hant; the visible General and Mouse
// settings pages after final polish and category activation.
// Steps: install one language, construct and show Settings, activate General
// and Mouse, and compare each visible label's mapped right edge and alignment.
// Expected result: every label ending in ':' or '：' uses one terminal colon
// glyph per language, is right/trailing aligned, and all such labels on the
// same page share exactly one right edge. A mixed ASCII/full-width set would
// make the visible punctuation edges differ even when widget rectangles match.
// Postcondition: the dialog, translator, and temporary settings are restored.
void WindowBehaviorTests::testSettingsColonAlignmentSurvivesTranslations()
{
#ifndef FOVELLE_TRANSLATIONS_DIR
    QSKIP("Translation catalogs were disabled for this build");
#else
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    const QList<QString> languages {
        QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("ja"),
        QStringLiteral("zh_Hans"), QStringLiteral("zh_Hant")
    };

    for (const QString &language : languages)
    {
        SourceLanguageTranslator sourceTranslator;
        QTranslator catalogTranslator;
        QTranslator *translator = &sourceTranslator;
        if (language != QStringLiteral("en"))
        {
            const QString path = QStringLiteral(FOVELLE_TRANSLATIONS_DIR "/qview_%1.qm")
                    .arg(language);
            QVERIFY2(catalogTranslator.load(path), qPrintable(path));
            translator = &catalogTranslator;
        }
        QVERIFY(QCoreApplication::installTranslator(translator));

        {
            ScopedOptionValues options({{QStringLiteral("language"), language}});
            QSettings settings;
            settings.setValue(QStringLiteral("optionstab"), 0);
            settings.setValue(QStringLiteral("optionstabversion"), 2);
            settings.sync();

            QVOptionsDialog dialog;
            dialog.setAttribute(Qt::WA_DeleteOnClose, false);
            auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
            auto *general = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
            auto *mouse = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
            QVERIFY(tabs);
            QVERIFY(general);
            QVERIFY(mouse);

            dialog.prepareForDisplay();
            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);

            const auto verifyPage = [&](QScrollArea *scrollArea, const int tabIndex) {
                tabs->setCurrentIndex(tabIndex);
                QTRY_VERIFY_WITH_TIMEOUT(
                    !dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
                QCoreApplication::processEvents();
                const QString failure = settingsLabelColumnFailure(scrollArea->widget());
                QVERIFY2(failure.isEmpty(),
                         qPrintable(language + QStringLiteral(" tab=")
                                    + QString::number(tabIndex) + QStringLiteral(": ")
                                    + failure));
            };

            verifyPage(general, 0);
            verifyPage(mouse, 2);
            dialog.close();
        }

        QVERIFY(QCoreApplication::removeTranslator(translator));
    }
#endif
}

// TC-SETTINGS-SPACING
// Test purpose: verify that General and Mouse use the same native form
// spacing and that every group gap is strictly larger than every row gap.
// Preconditions: the production Settings dialog and Cocoa widget style are
// available; the dialog can be shown without invoking any action.
// Input data: every General/Mouse QFormLayout, direct group geometry, style
// spacing properties.
// Steps: show Settings, inspect both pages and all forms, and compare direct
// group gaps.
// Expected result: all forms report verticalSpacing=-1, both pages expose the
// same computed group spacing, every measured intra-group gap is smaller, and
// extra height is represented only by the final stretch item.
// Postcondition: the dialog is closed and settings are unchanged.
void WindowBehaviorTests::testSettingsSpacingUsesNativeStyle()
{
    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.show();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);

    auto *generalScroll = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
    auto *mouseScroll = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
    QVERIFY(generalScroll);
    QVERIFY(mouseScroll);
    auto *generalContent = generalScroll->widget();
    auto *mouseContent = mouseScroll->widget();
    QVERIFY(generalContent);
    QVERIFY(mouseContent);

    const int groupSpacing = generalContent->property("settingsGroupSpacing").toInt();
    const int intraSpacing = generalContent->property("settingsIntraGroupMaxSpacing").toInt();
    QVERIFY(groupSpacing > 0);
    QVERIFY(intraSpacing >= 0);
    QVERIFY(groupSpacing > intraSpacing);
    QCOMPARE(mouseContent->property("settingsGroupSpacing").toInt(), groupSpacing);
    QCOMPARE(mouseContent->property("settingsIntraGroupMaxSpacing").toInt(), intraSpacing);

    const auto verifyForms = [&](QWidget *page) {
        const auto forms = page->findChildren<QFormLayout *>();
        QVERIFY(!forms.isEmpty());
        for (auto *form : forms)
        {
            QCOMPARE(form->verticalSpacing(), -1);
            for (int row = 1; row < form->rowCount(); ++row)
            {
                auto *previous = form->itemAt(row - 1, QFormLayout::FieldRole);
                auto *current = form->itemAt(row, QFormLayout::FieldRole);
                if (!previous || !current || !previous->widget() || !current->widget()
                    || previous->widget()->isHidden() || current->widget()->isHidden())
                    continue;
                const int gap = current->widget()->geometry().top()
                    - (previous->widget()->geometry().top()
                       + previous->widget()->geometry().height());
                QVERIFY2(gap < groupSpacing,
                         qPrintable(form->objectName() + QStringLiteral(" gap=")
                                    + QString::number(gap)));
            }
        }
    };

    const auto verifyGroups = [&](QWidget *page, const int expectedCount) {
        auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
        QVERIFY(layout);
        QVERIFY(layout->count() >= expectedCount + 1);
        QVERIFY(layout->itemAt(layout->count() - 1)->spacerItem());
        QList<QLayoutItem *> groups;
        for (int index = 0; index < layout->count(); ++index)
        {
            if (layout->itemAt(index)->widget())
                groups.append(layout->itemAt(index));
        }
        QCOMPARE(groups.size(), expectedCount);
        for (int index = 1; index < groups.size(); ++index)
        {
            const QRect previous = groups.at(index - 1)->geometry();
            const QRect current = groups.at(index)->geometry();
            const int gap = current.top() - (previous.top() + previous.height());
            QCOMPARE(gap, groupSpacing);
        }
    };

    QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("middleButtonModeLabel")));
    QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeClickRadioButton")));
    QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeDragRadioButton")));
    verifyForms(generalContent);
    verifyForms(mouseContent);
    verifyGroups(generalContent, 8);
    verifyGroups(mouseContent, 3);
    dialog.close();
}

// TC-SETTINGS-ASSOCIATE-NATIVE-STYLE
// Test purpose: verify the association action restores the direct native
// QPushButton state used by the pre-regression macOS Settings page.
// Preconditions: the production Settings dialog and Cocoa widget style are
// available; no real file-association action is invoked.
// Input data: associateFormatsButton role/alignment, stylesheet, size policy,
// native default properties, QStyleOptionButton features, and final geometry.
// Steps: show Settings, inspect the action row and native style option, then
// compare the final geometry with the native size hint.
// Expected result: the button is a direct SpanningRole widget, centered,
// non-flat, auto-default, default, stylesheet-free, and sized by sizeHint.
// Postcondition: the dialog is closed without clicking the button.
void WindowBehaviorTests::testSettingsAssociateButtonUsesNativeStyle()
{
    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.show();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);

    auto *button = dialog.findChild<QPushButton *>(QStringLiteral("associateFormatsButton"));
    auto *group8 = dialog.findChild<QWidget *>(QStringLiteral("settingsGroup8"));
    QVERIFY(button);
    QVERIFY(group8);
    auto *layout = qobject_cast<QFormLayout *>(group8->layout());
    QVERIFY(layout);

    auto *item = layout->itemAt(0, QFormLayout::SpanningRole);
    QVERIFY(item);
    QCOMPARE(item->widget(), static_cast<QWidget *>(button));
    QVERIFY((item->alignment() & (Qt::AlignHCenter | Qt::AlignVCenter))
            == (Qt::AlignHCenter | Qt::AlignVCenter));
    QVERIFY(button->style());
    QVERIFY(button->styleSheet().isEmpty());
    QVERIFY(!button->isFlat());
    QVERIFY(button->autoDefault());
    QVERIFY(button->isDefault());
    QCOMPARE(button->minimumWidth(), 0);
    QVERIFY(button->minimumHeight() <= button->sizeHint().height());

    QVERIFY(item->geometry().size().expandedTo(item->sizeHint())
            == item->geometry().size());

    const QImage rendered = button->grab().toImage();
    QVERIFY(!rendered.isNull());
    if (button->style()->objectName().contains(QStringLiteral("mac"), Qt::CaseInsensitive)
        && rendered.height() > 2)
    {
        int maximumVerticalDelta = 0;
        for (int y = 1; y < rendered.height(); ++y)
        {
            const QColor above = rendered.pixelColor(rendered.width() / 2, y - 1);
            const QColor current = rendered.pixelColor(rendered.width() / 2, y);
            maximumVerticalDelta = qMax(maximumVerticalDelta,
                                        qAbs(above.red() - current.red())
                                        + qAbs(above.green() - current.green())
                                        + qAbs(above.blue() - current.blue()));
        }
        QVERIFY(maximumVerticalDelta > 0);
    }

    dialog.close();
}

// TC-SETTINGS-ALL-LANGUAGES-TABS
// Test purpose: verify every visible option on every Settings tab remains
// inside its viewport for every supported application language.
// Preconditions: the Cocoa Qt test application, all five supported catalogs,
// and a writable isolated settings store are available.
// Input data: en, es, ja, zh_Hans, zh_Hant; General, Shortcuts, and Mouse;
// the fixed Click middle-button behavior.
// Steps: install one language catalog, construct and show Settings, switch to
// every tab, and inspect each visible control's natural width and mapped
// geometry.
// Expected result: the renamed same-window option and every other visible
// control fit without horizontal scrolling, insufficient height, or clipped
// geometry in every language and tab.
// Postcondition: every dialog, translator, and temporary setting is restored.
void WindowBehaviorTests::testSettingsEveryTabFitsEveryLanguage()
{
#ifndef FOVELLE_TRANSLATIONS_DIR
    QSKIP("Translation catalogs were disabled for this build");
#else
    ScopedSettingPreserver tabSetting(QStringLiteral("optionstab"));
    ScopedSettingPreserver tabVersionSetting(QStringLiteral("optionstabversion"));
    const QList<QString> languages {
        QStringLiteral("en"), QStringLiteral("es"), QStringLiteral("ja"),
        QStringLiteral("zh_Hans"), QStringLiteral("zh_Hant")
    };

    const auto isInspectable = [](QWidget *widget) {
        return qobject_cast<QLabel *>(widget)
            || qobject_cast<QAbstractButton *>(widget)
            || qobject_cast<QComboBox *>(widget)
            || qobject_cast<QAbstractSpinBox *>(widget);
    };
    const auto findLayoutItemForWidget = [](QLayout *layout,
                                            QWidget *target,
                                            const auto &self) -> QLayoutItem * {
        if (!layout)
            return nullptr;
        for (int index = 0; index < layout->count(); ++index)
        {
            auto *item = layout->itemAt(index);
            if (!item)
                continue;
            if (item->widget() == target)
                return item;
            if (auto *nestedLayout = item->layout())
            {
                if (auto *nestedItem = self(nestedLayout, target, self))
                    return nestedItem;
            }
            if (auto *nestedWidget = item->widget())
            {
                if (auto *nestedLayout = nestedWidget->layout())
                {
                    if (auto *nestedItem = self(nestedLayout, target, self))
                        return nestedItem;
                }
            }
        }
        return nullptr;
    };
    const auto verifyScrollPage = [&](QScrollArea *scrollArea, const QString &language) {
        QVERIFY2(scrollArea, qPrintable(language));
        QCOMPARE(scrollArea->horizontalScrollBar()->maximum(), 0);
        auto *viewport = scrollArea->viewport();
        auto *pageLayout = scrollArea->widget()->layout();
        QVERIFY2(pageLayout, qPrintable(language + QStringLiteral(": page layout")));
        for (auto *widget : scrollArea->widget()->findChildren<QWidget *>())
        {
            if (widget->objectName().isEmpty() || widget->isHidden() || !isInspectable(widget))
                continue;

            const QRect mapped(widget->mapTo(viewport, QPoint(0, 0)), widget->size());
            QVERIFY2(viewport->rect().contains(mapped),
                     qPrintable(language + QStringLiteral(": ") + widget->objectName()));
            auto *item = findLayoutItemForWidget(pageLayout, widget, findLayoutItemForWidget);
            QVERIFY2(item, qPrintable(language + QStringLiteral(": layout item ")
                                      + widget->objectName()));
            QVERIFY2(item->geometry().width() >= item->sizeHint().width(),
                     qPrintable(language + QStringLiteral(": natural width ") + widget->objectName()));
            QVERIFY2(widget->size().expandedTo(widget->minimumSizeHint()) == widget->size(),
                     qPrintable(language + QStringLiteral(": minimum height ") + widget->objectName()
                                + QStringLiteral(" actual=") + QString::number(widget->height())
                                + QStringLiteral(" hint=") + QString::number(widget->minimumSizeHint().height())));
        }
    };

    for (const QString &language : languages)
    {
        SourceLanguageTranslator sourceTranslator;
        QTranslator catalogTranslator;
        QTranslator *translator = &sourceTranslator;
        if (language != QStringLiteral("en"))
        {
            const QString path = QStringLiteral(FOVELLE_TRANSLATIONS_DIR "/qview_%1.qm")
                    .arg(language);
            QVERIFY2(catalogTranslator.load(path), qPrintable(path));
            translator = &catalogTranslator;
        }
        QVERIFY(QCoreApplication::installTranslator(translator));

        {
            ScopedOptionValues options({
                {QStringLiteral("language"), language},
                {QStringLiteral("theme"), static_cast<int>(Qv::Theme::Dark)},
                {QStringLiteral("checkerboardbackground"), false},
                {QStringLiteral("viewportmiddlebuttonmode"), static_cast<int>(Qv::ClickOrDrag::Click)}
            });
            QSettings settings;
            settings.setValue(QStringLiteral("optionstab"), 0);
            settings.setValue(QStringLiteral("optionstabversion"), 2);
            settings.sync();

            QVOptionsDialog dialog;
            dialog.setAttribute(Qt::WA_DeleteOnClose, false);
            auto *tabs = dialog.findChild<QTabBar *>(QStringLiteral("categoryTabs"));
            auto *general = dialog.findChild<QScrollArea *>(QStringLiteral("generalScrollArea"));
            auto *mouse = dialog.findChild<QScrollArea *>(QStringLiteral("mouseScrollArea"));
            auto *table = dialog.findChild<QTableWidget *>(QStringLiteral("shortcutsTable"));
            QVERIFY(tabs);
            QVERIFY(general);
            QVERIFY(mouse);
            QVERIFY(table);
            QVERIFY(!dialog.findChild<QCheckBox *>(QStringLiteral("navigationRegionsCheckbox")));
            QVERIFY(!dialog.findChild<QLabel *>(QStringLiteral("middleButtonModeLabel")));
            QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeClickRadioButton")));
            QVERIFY(!dialog.findChild<QRadioButton *>(QStringLiteral("middleButtonModeDragRadioButton")));

            dialog.prepareForDisplay();
            dialog.show();
            QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
            tabs->setCurrentIndex(0);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            verifyScrollPage(general, language);
            auto *reuse = dialog.findChild<QCheckBox *>(QStringLiteral("reuseWindowCheckbox"));
            QVERIFY(reuse);
            QVERIFY(reuse->text().contains(QStringLiteral("Open images")) || language != QStringLiteral("en"));
            auto *reuseItem = findLayoutItemForWidget(general->widget()->layout(),
                                                      reuse,
                                                      findLayoutItemForWidget);
            QVERIFY(reuseItem);
            QVERIFY(reuse->size().expandedTo(reuse->minimumSizeHint()) == reuse->size());

            tabs->setCurrentIndex(1);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            QCOMPARE(table->horizontalScrollBar()->maximum(), 0);
            for (int row = 0; row < table->rowCount(); ++row)
            {
                for (int column = 0; column < table->columnCount(); ++column)
                {
                    auto *item = table->item(row, column);
                    QVERIFY(item);
                    const QRect itemRect = table->visualItemRect(item);
                    if (itemRect.isValid() && itemRect.top() < table->viewport()->height())
                        QVERIFY2(table->viewport()->rect().contains(itemRect),
                                 qPrintable(language + QStringLiteral(": shortcut cell")));
                }
            }

            tabs->setCurrentIndex(2);
            QTRY_VERIFY_WITH_TIMEOUT(!dialog.property("settingsCategoryTransitionActive").toBool(), 1000);
            verifyScrollPage(mouse, language);

            dialog.close();
        }
        QVERIFY(QCoreApplication::removeTranslator(translator));
    }
#endif
}

// TC-MAC-MENU-LOCALIZATION
// Test purpose: verify every supported non-English catalog contains Qt's
// native application-menu contract and the AppKit Window-menu fallback.
void WindowBehaviorTests::testMacMenuTranslationCatalogsAreComplete()
{
#ifndef FOVELLE_TRANSLATIONS_DIR
    QSKIP("Translation catalogs were disabled for this build");
#else
    const QList<QByteArray> applicationSources {
        "About %1", "Preferences...", "Services", "Hide %1",
        "Hide Others", "Show All", "Quit %1"
    };
    const QList<QByteArray> windowSources {
        "Minimize", "Minimize All", "Zoom", "Zoom All", "Fill", "Center",
        "Move & Resize", "Full Screen Tile", "Remove Window from Set", "Halves",
        "Left", "Right", "Top", "Bottom", "Quarters", "Top Left", "Top Right",
        "Bottom Left", "Bottom Right", "Arrange", "Left & Right",
        "Left & Quarters", "Right & Left", "Right & Quarters", "Top & Bottom",
        "Top & Quarters", "Bottom & Top", "Bottom & Quarters",
        "Return to Previous Size", "Left of Screen", "Right of Screen",
        "Bring All to Front", "Arrange in Front", "Enter Full Screen",
        "Exit Full Screen", "Make Window Full Screen",
        "Tile Window to Left of Screen", "Tile Window to Right of Screen",
        "Move Window to Left Side of Screen", "Move Window to Right Side of Screen",
        "Cycle Through Windows"
    };
    const QList<QString> languages {
        QStringLiteral("es"), QStringLiteral("ja"),
        QStringLiteral("zh_Hans"), QStringLiteral("zh_Hant")
    };

    for (const QString &language : languages)
    {
        QTranslator translator;
        const QString path = QStringLiteral(FOVELLE_TRANSLATIONS_DIR "/qview_%1.qm")
                .arg(language);
        QVERIFY2(translator.load(path), qPrintable(path));
        for (const QByteArray &source : applicationSources)
        {
            QVERIFY2(!translator.translate("MAC_APPLICATION_MENU", source.constData()).isNull(),
                     qPrintable(language + QStringLiteral(": ") + QString::fromUtf8(source)));
        }
        for (const QByteArray &source : windowSources)
        {
            QVERIFY2(!translator.translate("MAC_WINDOW_MENU", source.constData()).isNull(),
                     qPrintable(language + QStringLiteral(": ") + QString::fromUtf8(source)));
        }

        QVERIFY(QCoreApplication::installTranslator(&translator));
        {
            ScopedOptionValues options({{"language", language}});
            QCOMPARE(QCoreApplication::translate("MAC_APPLICATION_MENU", "Services"),
                     translator.translate("MAC_APPLICATION_MENU", "Services"));
            QCOMPARE(QCoreApplication::translate("MAC_APPLICATION_MENU", "Quit %1"),
                     translator.translate("MAC_APPLICATION_MENU", "Quit %1"));
            QCOMPARE(QVCocoaFunctions::localizedWindowMenuTitle(QStringLiteral("Minimize")),
                     translator.translate("MAC_WINDOW_MENU", "Minimize"));
            QCOMPARE(QVCocoaFunctions::localizedWindowMenuTitle(QStringLiteral("Move & Resize"), true),
                     translator.translate("MAC_WINDOW_MENU", "Move & Resize"));
            QCOMPARE(QVCocoaFunctions::localizedWindowMenuTitle(QStringLiteral("Quarters"), true),
                     translator.translate("MAC_WINDOW_MENU", "Quarters"));
            QCOMPARE(QVCocoaFunctions::localizedWindowMenuTitle(QStringLiteral("A Dynamic Window Title")),
                     QStringLiteral("A Dynamic Window Title"));
        }
        QVERIFY(QCoreApplication::removeTranslator(&translator));
    }
#endif
}

// TC-VIEW-FULLSCREEN-MAIN-ICON
// Test purpose: verify both full-screen labels never acquire an icon in the
// main View menu when main-menu icons are disabled.
// Preconditions: a visible MainWindow and the production View menu clones.
// Input data: the Full Screen action before entering and after leaving full
// screen.
// Steps: enter and leave full screen through the window state path, then read
// the main-menu Full Screen action icon after each state transition.
// Expected result: the action text changes state, but its icon remains null in
// both Enter and Exit states.
// Postcondition: the test window is closed and the quit policy is restored.
void WindowBehaviorTests::testFullscreenMenuIconsRespectMainMenuPolicy()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    const auto findMainFullscreenAction = [&]() -> QAction * {
        const auto viewMenus = qvApp->getActionManager().getAllClonesOfMenu("view");
        for (auto *viewMenu : viewMenus)
        {
            if (viewMenu->property("isContextMenu").toBool())
                continue;
            for (auto *action : viewMenu->actions())
            {
                if (action->data().toStringList().value(0) == QStringLiteral("fullscreen"))
                    return action;
            }
        }
        return nullptr;
    };

    QAction *fullscreenAction = findMainFullscreenAction();
    QVERIFY(fullscreenAction);
    QVERIFY(fullscreenAction->icon().isNull());

    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    fullscreenAction = findMainFullscreenAction();
    QVERIFY(fullscreenAction);
    QVERIFY(fullscreenAction->text().contains(QStringLiteral("Exit")));
    QVERIFY(fullscreenAction->icon().isNull());

    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    fullscreenAction = findMainFullscreenAction();
    QVERIFY(fullscreenAction);
    QVERIFY(fullscreenAction->text().contains(QStringLiteral("Enter")));
    QVERIFY(fullscreenAction->icon().isNull());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-VIEW-EXIT-FULLSCREEN
// Test purpose: verify the View → Exit Full Screen action uses the same restore
// request as the Escape shortcut.
// Preconditions: a visible normal MainWindow with a stable normal geometry.
// Input data: the production Full Screen QAction clone while the window is in
// full screen.
// Steps: enter full screen, dispatch the Exit action directly, inspect the
// immediate Qt state, and wait for the native transition to finish.
// Expected result: the Qt state stays full screen until AppKit reports native
// completion; the restored geometry is then stable without a second write.
// Postcondition: the test window is closed and the quit policy is restored.
void WindowBehaviorTests::testExitFullscreenActionUsesEscapePath()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.setGeometry(QRect(220, 180, 720, 500));
    QSettings settings;
    const QVariant originalTitlebarPreference =
        settings.value(QStringLiteral("options/titlebarhidden"));
    settings.setValue(QStringLiteral("options/titlebarhidden"), false);
    settings.sync();
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    // Let the deferred full-size-content-view setup establish the steady
    // platform-normalized client geometry before capturing the pre-full-screen
    // frame. Do not reapply the requested client rect here: AppKit restores the
    // native normal frame, whose client geometry can differ by titlebar inset
    // between macOS versions.
    QTest::qWait(250);
    QCoreApplication::processEvents();
    const QRect normalGeometry = window.geometry();
    FullScreenExitGeometryRecorder geometryRecorder;
    window.installEventFilter(&geometryRecorder);

    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);

    QAction *exitAction = nullptr;
    for (auto *action : qvApp->getActionManager().getAllClonesOfAction("fullscreen", &window))
    {
        if (action->text().contains(QStringLiteral("Exit")))
        {
            exitAction = action;
            break;
        }
    }
    QVERIFY(exitAction);
    ActionManager::actionTriggered(exitAction, &window);

    // A native Escape exit does not publish the requested Qt state before the
    // asynchronous AppKit transition has completed. The View action must use
    // that same native boundary instead of calling QWidget::setWindowState().
    QCOMPARE(window.windowHandle()->windowState(), Qt::WindowFullScreen);

    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(window.geometry(), normalGeometry, 3000);
    const QRect settledGeometry = window.geometry();
    QTest::qWait(250);
    QCOMPARE(window.geometry(), settledGeometry);
    QCOMPARE(geometryRecorder.geometryEventsAfterExit(), 0);

    // Exercise the physical Escape binding as a second cycle. Both inputs
    // must retain the native asynchronous state boundary and produce no
    // post-exit resize/move.
    geometryRecorder.reset();
    window.toggleFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::keyClick(&window, Qt::Key_Escape);
    QCOMPARE(window.windowHandle()->windowState(), Qt::WindowFullScreen);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(window.geometry(), normalGeometry, 3000);
    QTest::qWait(250);
    QCOMPARE(geometryRecorder.geometryEventsAfterExit(), 0);

    window.removeEventFilter(&geometryRecorder);
    window.close();
    if (originalTitlebarPreference.isValid())
        settings.setValue(QStringLiteral("options/titlebarhidden"), originalTitlebarPreference);
    else
        settings.remove(QStringLiteral("options/titlebarhidden"));
    settings.sync();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SETTINGS-CENTER
// Test purpose: verify Preferences opens centered on the invoking main window.
// Preconditions: a visible main window and no stale Preferences dialog.
// Input data: an off-center saved Preferences geometry, explicit main-window
// geometry, and QVApplication::openOptionsDialog.
// Steps: record the first Show frame and every later Move event while opening.
// Expected result: the very first visible frame is centered; the tall native
// preference frame may receive one AppKit normalization, then stays stable and
// remains horizontally centered.
// Postcondition: Preferences and the test main window are closed.
void WindowBehaviorTests::testOptionsDialogCentersOnMainWindow()
{
    ScopedSettingPreserver geometrySetting(QStringLiteral("optionsgeometry"));
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    // Reproduce the reported precondition: the previous Settings window was
    // closed at a visibly different location.
    QVOptionsDialog rememberedDialog;
    rememberedDialog.setGeometry(QRect(40, 80, 650, 550));
    QSettings settings;
    settings.setValue(QStringLiteral("optionsgeometry"), rememberedDialog.saveGeometry());
    settings.sync();

    MainWindow mainWindow;
    mainWindow.setAttribute(Qt::WA_DeleteOnClose, false);
    mainWindow.setWindowState(Qt::WindowNoState);
    mainWindow.setGeometry(QRect(180, 160, 760, 520));
    mainWindow.show();
    QTRY_VERIFY_WITH_TIMEOUT(mainWindow.isVisible(), 1000);

    OptionsDialogPresentationRecorder presentationRecorder;
    qvApp->installEventFilter(&presentationRecorder);
    qvApp->openOptionsDialog(&mainWindow);
    QVOptionsDialog *dialog = nullptr;
    const auto findVisibleOptionsDialog = []() -> QVOptionsDialog * {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *candidate = qobject_cast<QVOptionsDialog *>(widget); candidate && candidate->isVisible())
                return candidate;
        }
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT((dialog = findVisibleOptionsDialog()) != nullptr, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(presentationRecorder.wasShown(), 1000);
    QCOMPARE(presentationRecorder.firstShownFrame().center(),
             mainWindow.frameGeometry().center());
    QTest::qWait(250);
    // A tall fixed Settings frame receives one AppKit placement normalization
    // after ordering when the native preference toolbar is attached. It must
    // not keep moving, and its horizontal center remains anchored to the main
    // window.
    QVERIFY(presentationRecorder.moveEventsAfterShow() <= 1);
    QCOMPARE(dialog->frameGeometry().center().x(), mainWindow.frameGeometry().center().x());

    qvApp->removeEventFilter(&presentationRecorder);
    dialog->close();
    mainWindow.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SETTINGS-NATIVE-CHILD-ORDER
// Test purpose: verify that the modeless Preferences window is an AppKit
// child ordered above the invoking Fovelle main window.
// Preconditions: a visible main window and a QVApplication with no required
// user interaction for opening Preferences.
// Input data: an invoking MainWindow, the production Preferences action, and
// an independent tool panel representing an external preview panel entering
// the window-ordering sequence.
// Steps: open Preferences from the main window, inspect its native relation
// and modality, order the independent panel, then reopen Preferences.
// Expected result: Preferences remains modeless and its native NSWindow stays
// a child of, and ordered above, the invoking main NSWindow; no global
// always-on-top flag is required.
// Postcondition: all test windows are closed and application settings restore.
void WindowBehaviorTests::testSettingsDialogIsNativeChildAboveMainWindow()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow mainWindow;
    mainWindow.setAttribute(Qt::WA_DeleteOnClose, false);
    mainWindow.setWindowState(Qt::WindowNoState);
    mainWindow.setGeometry(QRect(220, 180, 760, 520));
    mainWindow.show();
    QTRY_VERIFY_WITH_TIMEOUT(mainWindow.isVisible(), 1000);

    qvApp->openOptionsDialog(&mainWindow);
    QVOptionsDialog *dialog = nullptr;
    const auto findVisibleOptionsDialog = []() -> QVOptionsDialog * {
        for (QWidget *widget : QApplication::topLevelWidgets())
        {
            if (auto *candidate = qobject_cast<QVOptionsDialog *>(widget);
                candidate && candidate->isVisible())
                return candidate;
        }
        return nullptr;
    };
    QTRY_VERIFY_WITH_TIMEOUT((dialog = findVisibleOptionsDialog()) != nullptr, 3000);
    QVERIFY(dialog->windowHandle());
    QVERIFY(mainWindow.windowHandle());
    QVERIFY(!dialog->isModal());
    QCOMPARE(dialog->windowModality(), Qt::NonModal);
    QVERIFY(!dialog->windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint));
    QTRY_VERIFY_WITH_TIMEOUT(
        QVCocoaFunctions::isWindowChildOf(dialog->windowHandle(),
                                           mainWindow.windowHandle()),
        2000);
    QVERIFY(dialog->property("settingsAttachedToMainWindow").toBool());

    QDialog externalPreviewPanel;
    externalPreviewPanel.setWindowFlag(Qt::Tool, true);
    externalPreviewPanel.setAttribute(Qt::WA_DeleteOnClose, false);
    externalPreviewPanel.show();
    QTRY_VERIFY_WITH_TIMEOUT(externalPreviewPanel.isVisible(), 1000);
    externalPreviewPanel.raise();
    QCoreApplication::processEvents();

    qvApp->openOptionsDialog(&mainWindow);
    QTRY_VERIFY_WITH_TIMEOUT(
        QVCocoaFunctions::isWindowChildOf(dialog->windowHandle(),
                                           mainWindow.windowHandle()),
        2000);
    QVERIFY(dialog->property("settingsAttachedToMainWindow").toBool());

    externalPreviewPanel.close();
    dialog->close();
    mainWindow.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SETTINGS-ASSOCIATE-CENTER
// Test purpose: verify the file-association button is centered across the
// General content area rather than aligned to the form's field column.
// Preconditions: the production options dialog can be constructed and shown.
// Input data: the button and General page geometries after layout.
// Steps: show the dialog, map both centers to global coordinates, and compare
// their horizontal coordinates.
// Expected result: the button center is within one pixel of the page center.
// Postcondition: the dialog is closed without invoking the association action.
void WindowBehaviorTests::testAssociateFormatsButtonIsCentered()
{
    QVOptionsDialog dialog;
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    dialog.show();
    QTRY_VERIFY_WITH_TIMEOUT(dialog.isVisible(), 1000);
    auto *categoryTabs = dialog.findChild<QTabBar *>("categoryTabs");
    QVERIFY(categoryTabs);
    categoryTabs->setCurrentIndex(0);

    auto *button = dialog.findChild<QPushButton *>("associateFormatsButton");
    auto *generalContent = dialog.findChild<QWidget *>("generalContent");
    auto *generalScrollArea = dialog.findChild<QScrollArea *>("generalScrollArea");
    QVERIFY(button);
    QVERIFY(generalContent);
    QVERIFY(generalScrollArea);
    QTRY_VERIFY_WITH_TIMEOUT(button->width() > 0 && generalContent->width() > 0, 1000);

    const int buttonCenterX = button->mapToGlobal(button->rect().center()).x();
    const int pageCenterX = generalScrollArea->viewport()->mapToGlobal(
        generalScrollArea->viewport()->rect().center()).x();
    QVERIFY(qAbs(buttonCenterX - pageCenterX) <= 1);

    dialog.close();
}

// TC-TITLEBAR-PERSISTENCE
// Test purpose: verify a manually hidden titlebar becomes the default for a
// newly opened window.
// Preconditions: a visible main window and writable QSettings.
// Input data: the titlebarhidden preference and QVApplication::newWindow().
// Steps: set the first window's titlebar hidden, create a second production
// window, and inspect its native titlebar state.
// Expected result: the second window is also titlebar-hidden.
// Postcondition: both windows close and the original persistent value returns.
void WindowBehaviorTests::testTitlebarHiddenPersistsToNewWindow()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QSettings settings;
    const bool originalPreference = settings.value(QStringLiteral("options/titlebarhidden"), false).toBool();
    settings.setValue(QStringLiteral("options/titlebarhidden"), false);
    settings.sync();

    MainWindow firstWindow;
    firstWindow.setAttribute(Qt::WA_DeleteOnClose, false);
    firstWindow.show();
    QTRY_VERIFY_WITH_TIMEOUT(firstWindow.isVisible(), 1000);
    firstWindow.setTitlebarHidden(true);
    QVERIFY(firstWindow.getTitlebarHidden());

    MainWindow *secondWindow = qvApp->newWindow();
    QVERIFY(secondWindow);
    QTRY_VERIFY_WITH_TIMEOUT(secondWindow->isVisible(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(secondWindow->getTitlebarHidden(), 2000);

    secondWindow->close();
    firstWindow.close();
    settings.setValue(QStringLiteral("options/titlebarhidden"), originalPreference);
    settings.sync();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SETTINGS-SMOOTH-DEFAULT
// Test purpose: verify the Image → Smooth scaling default.
// Preconditions: SettingsManager has initialized its default-value library.
// Input data: the smoothscalingmode setting read with defaults=true.
// Steps: read the default without changing a stored user value.
// Expected result: the default is Bilinear.
// Postcondition: no persistent setting is changed.
void WindowBehaviorTests::testSmoothScalingDefaultIsBilinear()
{
    QCOMPARE(
        qvApp->getSettingsManager().getEnum<Qv::SmoothScalingMode>("smoothscalingmode", true),
        Qv::SmoothScalingMode::Bilinear);
}

// TC-WINDOW-MAXIMIZED
// Test purpose: verify every application-created image window is shown
// maximized, independent of the removed image-size matching preferences.
// Preconditions: the Cocoa application is running and last-window quit is
// temporarily disabled so the test-owned window can be inspected.
// Input data: an empty QVApplication::newWindow() request.
// Steps: create the production window, wait for the native show operation,
// and inspect its Qt window state.
// Expected result: WindowMaximized is set before the function returns to the
// event loop.
// Postcondition: the test-owned window is closed and deleted; app policy is restored.
void WindowBehaviorTests::testNewWindowStartsMaximized()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow *window = QVApplication::newWindow();
    QVERIFY(window);
    window->setAttribute(Qt::WA_DeleteOnClose, false);
    QTRY_VERIFY_WITH_TIMEOUT(window->isVisible(), 1000);
    QVERIFY(window->windowState().testFlag(Qt::WindowMaximized));
    window->close();
    delete window;

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-SETTINGS-SYSTEM-THEME
// Test purpose: verify System is the terminal Theme option and resolves to the
// system appearance deterministically for both appearance branches.
// Preconditions: the native appearance resolver is available.
// Input data: FOVELLE_SYSTEM_THEME=light and =dark test overrides.
// Steps: resolve Qv::Theme::System under each controlled override.
// Expected result: light maps to Light and dark maps to Dark.
// Postcondition: the process environment is restored.
void WindowBehaviorTests::testSystemThemeResolvesFromControlledAppearance()
{
    ScopedEnvironmentValue environment("FOVELLE_SYSTEM_THEME");
    qputenv("FOVELLE_SYSTEM_THEME", "light");
    QCOMPARE(QVCocoaFunctions::resolvedTheme(Qv::Theme::System), Qv::Theme::Light);
    qputenv("FOVELLE_SYSTEM_THEME", "dark");
    QCOMPARE(QVCocoaFunctions::resolvedTheme(Qv::Theme::System), Qv::Theme::Dark);
}

// TC-HELP-MENU
// Test purpose: verify Help removes Welcome and exposes the two requested
// actions.
// Preconditions: the application ActionManager has built the application menu.
// Input data: the Help menu action library.
// Steps: inspect the menu's action texts and action-library keys.
// Expected result: Project Homepage and Check for Updates exist; Welcome does
// not exist.
// Postcondition: the temporary menu is released with its parent.
void WindowBehaviorTests::testHelpMenuContract()
{
    QMenu *helpMenu = nullptr;
    for (QAction *menuAction : qvApp->getMenuBar()->actions())
    {
        if (menuAction->text().contains(QStringLiteral("Help"), Qt::CaseInsensitive))
        {
            helpMenu = menuAction->menu();
            break;
        }
    }
    QVERIFY(helpMenu);
    QVERIFY(qvApp->getActionManager().getAction("projecthomepage"));
    QVERIFY(qvApp->getActionManager().getAction("checkupdates"));
    QVERIFY(!qvApp->getActionManager().getAction("welcome"));

    const QStringList actionTexts = std::accumulate(
        helpMenu->actions().cbegin(), helpMenu->actions().cend(), QStringList {},
        [](QStringList texts, QAction *action) {
            texts.append(action->text());
            return texts;
        });
    QVERIFY(actionTexts.join(QStringLiteral("\n")).contains(QStringLiteral("Project Homepage")));
    QVERIFY(actionTexts.join(QStringLiteral("\n")).contains(QStringLiteral("Check for Updates")));
    QVERIFY(!actionTexts.join(QStringLiteral("\n")).contains(QStringLiteral("Welcome")));
}

// TC-EDIT-MACOS-SERVICES
// Test purpose: verify the Edit menu does not expose macOS text-service items
// requested for removal.
// Preconditions: the application menu bar has been initialized.
// Input data: all nested Edit-menu actions.
// Steps: inspect action text case-insensitively.
// Expected result: AutoFill, Start Dictation, Emoji & Symbols, and Emoji and
// Symbols are absent.
// Postcondition: no menu state is changed.
void WindowBehaviorTests::testEditMenuRemovesMacOSServiceItems()
{
    const auto allActions = ActionManager::getAllNestedActions(qvApp->getMenuBar()->actions());
    const QStringList forbidden {
        QStringLiteral("AutoFill"), QStringLiteral("Start Dictation"),
        QStringLiteral("Emoji & Symbols"), QStringLiteral("Emoji and Symbols")
    };
    for (const QAction *action : allActions)
    {
        for (const QString &text : forbidden)
            QVERIFY2(action->text().compare(text, Qt::CaseInsensitive) != 0, qPrintable(action->text()));
    }
}

// TC-DIALOGS-THEME
// Test purpose: verify the shared native-dialog adapter applies both selected
// appearances to Settings, About, image information, and message boxes.
// Preconditions: the Cocoa test application is running.
// Input data: Light Theme and Dark Theme settings.
// Steps: create and show each production dialog under each theme, then read
// its AppKit window appearance.
// Expected result: Light uses Aqua and Dark uses DarkAqua for every dialog.
// Postcondition: dialogs are closed and the original theme is restored.
void WindowBehaviorTests::testNativeDialogsFollowSelectedTheme()
{
    for (const auto theme : {Qv::Theme::Light, Qv::Theme::Dark})
    {
        ScopedOptionValues options({{"theme", static_cast<int>(theme)}});
        const QString expectedAppearance = theme == Qv::Theme::Dark
            ? QStringLiteral("DarkAqua")
            : QStringLiteral("Aqua");
        const bool expectsDarkPalette = theme == Qv::Theme::Dark;

        const auto assertAppearance = [&expectedAppearance, expectsDarkPalette](QWidget *dialog) {
            dialog->setAttribute(Qt::WA_DeleteOnClose, false);
            dialog->show();
            QTRY_COMPARE_WITH_TIMEOUT(
                QVCocoaFunctions::getWindowAppearanceName(dialog->windowHandle()),
                expectedAppearance,
                2000);
            QTRY_VERIFY_WITH_TIMEOUT(
                (dialog->palette().color(QPalette::Window).lightness() < 128)
                    == expectsDarkPalette,
                2000);
            QTRY_VERIFY_WITH_TIMEOUT(
                (dialog->palette().color(QPalette::WindowText).lightness() > 128)
                    == expectsDarkPalette,
                2000);
            dialog->close();
            delete dialog;
        };

        assertAppearance(new QVOptionsDialog());
        assertAppearance(new QVAboutDialog());
        assertAppearance(new QVInfoDialog());

        QMessageBox *messageBox = NativeDialogs::createMessageBox(
            QMessageBox::Information,
            QStringLiteral("Theme test"),
            QStringLiteral("Native dialog"),
            QMessageBox::Ok);
        assertAppearance(messageBox);
    }
}

// TC-OPEN-URL-THEME
// Test purpose: verify that File -> Open URL changes both its native Cocoa
// appearance and its Qt widget palette under Light and Dark Theme.
// Preconditions: a visible MainWindow and the production pickUrl path.
// Input data: Light Theme followed by Dark Theme.
// Steps: open the production QInputDialog, inspect its window, label/edit
// palette, then reject it without starting a network request.
// Expected result: Aqua uses a light Window/Base with dark text; DarkAqua uses
// a dark Window/Base with light text, and the two rendered palettes differ.
// Postcondition: dialogs/windows close and the original Theme is restored.
void WindowBehaviorTests::testOpenUrlDialogFollowsSelectedTheme()
{
    QColor lightWindow;
    QColor lightBase;
    QColor darkWindow;
    QColor darkBase;

    for (const auto theme : {Qv::Theme::Light, Qv::Theme::Dark})
    {
        ScopedOptionValues options({{"theme", static_cast<int>(theme)}});
        MainWindow window;
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.resize(640, 480);
        window.show();
        QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

        window.pickUrl();
        auto *dialog = window.findChild<QInputDialog *>();
        QTRY_VERIFY_WITH_TIMEOUT(dialog && dialog->isVisible(), 2000);
        auto *lineEdit = dialog->findChild<QLineEdit *>();
        QVERIFY(lineEdit);

        const bool isDark = theme == Qv::Theme::Dark;
        const QString expectedAppearance = isDark
            ? QStringLiteral("DarkAqua") : QStringLiteral("Aqua");
        QTRY_COMPARE_WITH_TIMEOUT(
            QVCocoaFunctions::getWindowAppearanceName(dialog->windowHandle()),
            expectedAppearance,
            2000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (dialog->palette().color(QPalette::Window).lightness() < 128) == isDark,
            2000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (lineEdit->palette().color(QPalette::Base).lightness() < 128) == isDark,
            2000);
        QTRY_VERIFY_WITH_TIMEOUT(
            (lineEdit->palette().color(QPalette::Text).lightness() > 128) == isDark,
            2000);

        if (isDark)
        {
            darkWindow = dialog->palette().color(QPalette::Window);
            darkBase = lineEdit->palette().color(QPalette::Base);
        }
        else
        {
            lightWindow = dialog->palette().color(QPalette::Window);
            lightBase = lineEdit->palette().color(QPalette::Base);
        }

        dialog->reject();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        window.close();
    }

    QVERIFY(lightWindow != darkWindow);
    QVERIFY(lightBase != darkBase);
    QVERIFY(lightWindow.lightness() > darkWindow.lightness());
    QVERIFY(lightBase.lightness() > darkBase.lightness());
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
    const bool nativeOverlay = view->usesNativeHDRNavigationOverlay();

    const int middleY = view->viewport()->height() / 2;
    sendMouseMove(view->viewport(), QPoint(1, middleY));
    if (nativeOverlay)
        QTRY_VERIFY_WITH_TIMEOUT(
                view->nativeMetalRendererDiagnostics().nativeNavigationVisibleCount > 0,
                1000);
    else
        QTRY_VERIFY_WITH_TIMEOUT(previousButton->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(previousButton->property("sampledContentBrightness").isValid(), 1000);
    QCOMPARE(previousButton->property("contrastStyle").toString(), QStringLiteral("light"));
    QVERIFY(previousButton->property("sampledContentBrightness").toDouble() > 0.5);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() - 1, middleY));
    if (!nativeOverlay)
        QTRY_VERIFY_WITH_TIMEOUT(nextButton->isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(nextButton->property("sampledContentBrightness").isValid(), 1000);
    QCOMPARE(nextButton->property("contrastStyle").toString(), QStringLiteral("dark"));
    QVERIFY(nextButton->property("sampledContentBrightness").toDouble() < 0.5);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-NAV-ARTWORK-STYLES
// Test purpose: verify the two navigation visuals are single composited
// buttons with explicit transparent-light and tinted-dark artwork contracts.
// Preconditions: MainWindow has created both navigation buttons.
// Input data: the previous and next button object properties.
// Steps: inspect composition metadata, absence of widget effects, and the
// style names used by the contrast renderer.
// Expected result: both buttons share one composited-artwork contract and
// expose the two required style variants without separate child controls.
// Postcondition: the temporary window is closed.
void WindowBehaviorTests::testNavigationArtworkStylesAreSingleCompositedButtons()
{
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    const auto buttons = window.findChildren<QPushButton *>();
    auto *previousButton = window.findChild<QPushButton *>("previousImageButton");
    auto *nextButton = window.findChild<QPushButton *>("nextImageButton");
    QVERIFY(previousButton);
    QVERIFY(nextButton);
    for (QPushButton *button : {previousButton, nextButton})
    {
        QCOMPARE(button->property("artworkComposition").toString(),
                 QStringLiteral("single-composited-button"));
        QCOMPARE(button->property("lightArtwork").toString(),
                 QStringLiteral("transparent-chevron"));
        QCOMPARE(button->property("darkArtwork").toString(),
                 QStringLiteral("gray-tile-chevron"));
        QVERIFY(button->graphicsEffect() == nullptr);
    }
    QVERIFY(buttons.size() >= 2);
    window.close();
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
// Test purpose: verify navigation fades only its composited artwork pixels and
// never allocates the rectangular QGraphicsOpacityEffect surface seen over HDR.
// Preconditions: MainWindow has created both navigation buttons/animations.
// Input data: both buttons at full and 50% paintOpacity rendered onto transparent ARGB.
// Steps: inspect attributes/effects/animation targets and compare every artwork
// pixel between the full and half-opacity renderings.
// Expected result: effects are null, transparent/no-system-background flags are
// set, animations target paintOpacity, corners stay transparent, and every
// already-composited bottom/chevron pixel receives the same 50% multiplier.
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
        // The light reference artwork has no tile even when pressed. The dark
        // variant supplies its gray tile as part of the same artwork surface.
        button->setDown(true);
        const auto renderAtOpacity = [button](const qreal opacity) {
            button->setProperty("paintOpacity", opacity);
            QImage rendered(button->size(), QImage::Format_ARGB32_Premultiplied);
            rendered.fill(Qt::transparent);
            button->render(&rendered);
            return rendered;
        };
        const QImage full = renderAtOpacity(1.0);
        const QImage half = renderAtOpacity(0.5);
        QCOMPARE(half.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(half.pixelColor(half.width() - 1, 0).alpha(), 0);
        QCOMPARE(half.pixelColor(0, half.height() - 1).alpha(), 0);
        QCOMPARE(half.pixelColor(half.width() - 1,
                                 half.height() - 1).alpha(), 0);

        int artworkPixelCount = 0;
        int opaqueChevronPixelCount = 0;
        for (int y = 0; y < full.height(); ++y)
        {
            for (int x = 0; x < full.width(); ++x)
            {
                const int fullAlpha = full.pixelColor(x, y).alpha();
                const int halfAlpha = half.pixelColor(x, y).alpha();
                if (fullAlpha == 0)
                    continue;
                ++artworkPixelCount;
                opaqueChevronPixelCount += fullAlpha == 255;
                QVERIFY2(qAbs(halfAlpha * 2 - fullAlpha) <= 3,
                         qPrintable(QStringLiteral("pixel=(%1,%2) full=%3 half=%4")
                             .arg(x).arg(y).arg(fullAlpha).arg(halfAlpha)));
            }
        }
        QVERIFY(artworkPixelCount > 0);
        QCOMPARE(full.pixelColor(2, full.height() / 2).alpha(), 0);
        QVERIFY(opaqueChevronPixelCount > 0);
        button->setDown(false);
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
    const bool nativeOverlay = view->usesNativeHDRNavigationOverlay();

    const int middleY = view->viewport()->height() / 2;
    sendMouseMove(view->viewport(), QPoint(1, middleY));
    QCOMPARE(previousButton->isVisible(), !nativeOverlay);
    QCOMPARE(previousAnimation->state(), QAbstractAnimation::Running);
    QCOMPARE(previousAnimation->endValue().toReal(), 1.0);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() / 2, middleY));
    QCOMPARE(previousButton->isVisible(), !nativeOverlay);
    QCOMPARE(previousAnimation->state(), QAbstractAnimation::Running);
    QCOMPARE(previousAnimation->endValue().toReal(), 0.0);
    // The immediate visibility assertion above is the deterministic
    // mid-transition contract. Do not assume that a wall-clock 100 ms wait
    // is shorter than the animation on every hosted macOS display backend.
    QTRY_VERIFY_WITH_TIMEOUT(
            previousButton->property("paintOpacity").toReal() <= 0.001,
            1000);

    sendMouseMove(view->viewport(), QPoint(view->viewport()->width() - 1, middleY));
    QCOMPARE(nextButton->isVisible(), !nativeOverlay);
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
    if (view->usesNativeHDRNavigationOverlay())
    {
        QTRY_VERIFY_WITH_TIMEOUT(
                view->nativeMetalRendererDiagnostics().nativeNavigationVisibleCount > 0,
                1000);
        const QPoint nativeButtonCenter = view->viewport()->mapFrom(
                view, nextButton->geometry().center());
        QTest::mouseClick(
                view->viewport(), Qt::LeftButton, Qt::NoModifier,
                nativeButtonCenter);
    }
    else
    {
        QTRY_VERIFY_WITH_TIMEOUT(nextButton->isVisible(), 1000);
        QTest::mouseClick(nextButton, Qt::LeftButton);
    }
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
    QCoreApplication::setApplicationVersion("1.0.1");
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
    ShortcutSettingsTests shortcutSettingsTests;
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
    if (selectedSuite == "ShortcutSettingsTests")
        result |= QTest::qExec(&shortcutSettingsTests, argc, argv);
    return result;
}

#include "tst_qviewtests.moc"
