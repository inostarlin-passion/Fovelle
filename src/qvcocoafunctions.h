#ifndef QVCOCOAFUNCTIONS_H
#define QVCOCOAFUNCTIONS_H

#include "openwith.h"
#include "qvnamespace.h"

#include <QWindow>
#include <QMenu>
#include <QImage>
#include <QByteArray>
#include <QList>
#include <QPolygonF>
#include <QSize>

#include <memory>

class QWidget;

class QVCocoaFunctions
{
public:
    struct HDRMetadata
    {
        QString sourceKind;
        QString typeIdentifier;
        QString colorSpaceName;
        QString transferFunction;
        QSize pixelSize;
        float contentHeadroom{ 1.0F };
        int bitsPerComponent{ 0 };
        bool isRaw{ false };
        bool hasAppleGainMap{ false };
        bool hasISOGainMap{ false };
        bool decodedToHDR{ false };
        bool usesRawExtendedDynamicRange{ false };
        bool usedRawPreview{ false };
        bool usesProcessedRawPreview{ false };
    };

    class HDRImage
    {
    public:
        virtual ~HDRImage() = default;
        virtual const HDRMetadata &metadata() const = 0;
    };

    using HDRImagePtr = std::shared_ptr<const HDRImage>;

    struct NativeImageReadResult
    {
        QImage image;
        HDRImagePtr hdrImage;
        HDRMetadata hdrMetadata;
        QSize intrinsicSize;
        QString typeIdentifier;
        QString errorString;
        bool isImageIOType {false};
        bool isRaw {false};
        bool usedRawPreview {false};
    };

    struct HDRRendererDiagnostics
    {
        bool rendererAvailable{ false };
        bool imageActive{ false };
        bool isRaw{ false };
        bool hasGainMap{ false };
        bool usesRGBA16Float{ false };
        bool usesExtendedLinearDisplayP3{ false };
        bool usesColorSync{ false };
        bool wantsExtendedDynamicRangeContent{ false };
        bool clearsEntireDrawableOpaque{ false };
        bool displayHeadroomOverridden{ false };
        bool displayCurrentHeadroomOverridden{ false };
        bool firstFrameSubmitted{ false };
        bool firstFramePresented{ false };
        bool hdrPreparationInFlight{ false };
        bool hdrPrepared{ false };
        bool usesCoreImageManagedIntermediates{ false };
        bool preparedGeometryActive{ false };
        bool bootstrappingEDR{ false };
        bool cachesIntermediates{ false };
        bool usesLayerContentsHeadroomTag{ false };
        bool usesCAMetalDisplayLink{ false };
        bool encodesMetalOffMainThread{ false };
        bool frameInFlight{ false };
        float contentHeadroom{ 1.0F };
        float displayCurrentHeadroom{ 1.0F };
        float displayPotentialHeadroom{ 1.0F };
        float displayRenderingHeadroom{ 1.0F };
        float targetHeadroom{ 1.0F };
        float layerContentsHeadroom{ 0.0F };
        int backgroundRed{ 0 };
        int backgroundGreen{ 0 };
        int backgroundBlue{ 0 };
        quint64 backgroundUpdateCount{ 0 };
        float transitionProgress{ 0.0F };
        int requestedDrawableWidth{ 0 };
        int requestedDrawableHeight{ 0 };
        int actualTextureWidth{ 0 };
        int actualTextureHeight{ 0 };
        bool drawableGeometryMatches{ false };
        float layerOpacity{ 0.0F };
        quint64 geometryGeneration{ 0 };
        quint64 geometryResetCount{ 0 };
        quint64 renderRequestCount{ 0 };
        quint64 coalescedRenderRequestCount{ 0 };
        quint64 displayLinkCallbackCount{ 0 };
        quint64 deferredDisplayLinkCallbackCount{ 0 };
        quint64 requestedRenderGeneration{ 0 };
        quint64 submittedRenderGeneration{ 0 };
        quint64 renderCount{ 0 };
        double lastRenderMilliseconds{ 0.0 };
    };

    struct HDRPixelStatistics
    {
        bool valid{ false };
        float sdrMaximumComponent{ 0.0F };
        float hdrMaximumComponent{ 0.0F };
    };

    class HDRRenderer
    {
    public:
        explicit HDRRenderer(QWidget *viewport);
        ~HDRRenderer();

        HDRRenderer(const HDRRenderer &) = delete;
        HDRRenderer &operator=(const HDRRenderer &) = delete;

        bool isAvailable() const;
        bool setImage(const HDRImagePtr &image);
        void setBackgroundColor(const QColor &color);
        void invalidateGeometry();
        void clear();
        void render(const QSize &viewportSize, const QPolygonF &imageCorners,
                    qreal transitionProgress);
        HDRRendererDiagnostics diagnostics() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
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
    static NativeImageReadResult readImageWithImageIO(const QString &filePath,
                                                      int fallbackLargestDimension = 0);

    // Pure transition policy helpers are exposed so headroom behavior can be
    // verified without depending on a particular physical display.
    static qreal easedHDRTransition(qreal progress);

    // Advance a presentation-driven ramp without allowing a delayed GPU frame
    // to turn elapsed wall time into a visible luminance jump.
    static qreal pacedHDRTransitionProgress(qreal previousProgress,
                                            qreal desiredProgress,
                                            qreal maximumStep);

    static qreal effectiveHDRHeadroom(qreal contentHeadroom, qreal displayHeadroom,
                                      qreal transitionProgress);

    // Some filter graphs (notably CIRAWFilter) report zero when their content
    // headroom is unknown. A measured float peak is then the correct tag for
    // Core Animation; it must never be replaced by display capability.
    static qreal resolvedHDRContentHeadroom(qreal reportedHeadroom,
                                            qreal measuredMaximumComponent);

    // The current EDR value can remain one until the first EDR frame is
    // onscreen. Use potential capability to break that bootstrap cycle while
    // still preferring the dynamic current value once WindowServer exposes it.
    static qreal displayHeadroomForRendering(qreal currentHeadroom,
                                             qreal potentialHeadroom,
                                             qreal contentHeadroom);

    // A transition may begin only after the final view geometry is known, an
    // SDR Metal frame is actually visible, and the expensive HDR graph has
    // completed its first offscreen evaluation.
    static bool shouldStartHDRTransition(bool layoutReady, bool firstFramePresented,
                                         bool hdrPrepared);

    // Non-invasive test/diagnostic probe. It reduces each retained CI graph to
    // one floating-point maximum pixel without converting the source to an SDR
    // CGImage or NSImage.
    static HDRPixelStatistics probeHDRPixelStatistics(const HDRImagePtr &image);

    static QImage readAdditionalImage(const QString &filePath, QString *errorString = nullptr);

    static std::unique_ptr<AnimatedImage> createAnimatedImage(const QString &filePath);
};

#endif // QVCOCOAFUNCTIONS_H
