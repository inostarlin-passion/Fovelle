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
#include <QRectF>
#include <QSize>
#include <QStringList>
#include <QTransform>

#include <memory>

class QWidget;
class QTabBar;

class QVCocoaFunctions
{
public:
    class PDFVectorDocument
    {
    public:
        ~PDFVectorDocument();

        PDFVectorDocument(const PDFVectorDocument &) = delete;
        PDFVectorDocument &operator=(const PDFVectorDocument &) = delete;

        bool isValid() const;
        QImage renderTile(const QSizeF &logicalPageSize,
                          const QRectF &sourceRect,
                          const QSize &pixelSize,
                          QString *errorString = nullptr) const;

    private:
        friend class QVCocoaFunctions;
        explicit PDFVectorDocument(const QByteArray &pdfData);
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    using PDFVectorDocumentPtr = std::shared_ptr<const PDFVectorDocument>;

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

    class SDRImage
    {
    public:
        virtual ~SDRImage() = default;
        virtual QSize pixelSize() const = 0;
        virtual bool hasAlpha() const = 0;
    };

    using SDRImagePtr = std::shared_ptr<const SDRImage>;

    struct NativeImageReadResult
    {
        QImage image;
        Qv::VectorImageData vectorImage;
        SDRImagePtr sdrImage;
        HDRImagePtr hdrImage;
        HDRMetadata hdrMetadata;
        QSize intrinsicSize;
        QString typeIdentifier;
        QString errorString;
        bool isImageIOType {false};
        bool allowsQtFallback {true};
        bool isRaw {false};
        bool usedRawPreview {false};
    };

    struct HDRRendererDiagnostics
    {
        bool rendererAvailable{ false };
        bool imageActive{ false };
        bool sdrImageActive{ false };
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
        bool displayLinkPaused{ true };
        bool encodesMetalOffMainThread{ false };
        bool frameInFlight{ false };
        bool usesNativeNavigationOverlay{ false };
        bool firstVisibleFrameUsesFinalHeadroom{ false };
        bool usesDisplayLinkInteractionPacing{ false };
        bool usesPersistentHDRSurface{ false };
        bool persistentHDRSurfaceReady{ false };
        bool usesSDRPreview{ false };
        bool sdrFullResolutionRefinementPending{ false };
        bool presentationActiveRequested{ true };
        bool presentationAnimationInFlight{ false };
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
        quint64 displayLinkRebuildCount{ 0 };
        quint64 deferredDisplayLinkCallbackCount{ 0 };
        quint64 requestedRenderGeneration{ 0 };
        quint64 submittedRenderGeneration{ 0 };
        quint64 presentedRenderGeneration{ 0 };
        quint64 presentedFrameCount{ 0 };
        quint64 missedTargetDeadlineCount{ 0 };
        quint64 navigationOverlayUpdateCount{ 0 };
        quint64 displayLinkInteractiveSubmissionCount{ 0 };
        quint64 compositorGeometryUpdateCount{ 0 };
        quint64 presentationTransitionCount{ 0 };
        quint64 persistentHDRSurfaceBytes{ 0 };
        quint64 sdrPreviewPresentedFrameCount{ 0 };
        quint64 sdrFullResolutionPresentedFrameCount{ 0 };
        int framesInFlight{ 0 };
        int nativeWindowNumber{ 0 };
        int nativeWindowGlobalX{ 0 };
        int nativeWindowGlobalY{ 0 };
        int nativeNavigationVisibleCount{ 0 };
        quint64 renderCount{ 0 };
        double lastRenderMilliseconds{ 0.0 };
        double lastGPUExecutionMilliseconds{ 0.0 };
        double persistentHDRSurfacePreparationMilliseconds{ 0.0 };
        double lastPresentedIntervalMilliseconds{ 0.0 };
        double lastRequestToPresentationMilliseconds{ 0.0 };
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
        bool setSDRImage(const SDRImagePtr &image);
        void setBackgroundColor(const QColor &color);
        void setCheckerboardBackground(bool enabled);
        void setPresentationActive(bool active, bool animated = true);
        void invalidateGeometry();
        void clear();
        void render(const QSize &viewportSize, const QPolygonF &imageCorners,
                    qreal transitionProgress, bool interactive = false);
        void setNavigationOverlay(int index, const QRectF &viewportRect,
                                  qreal opacity, bool previous,
                                  bool darkBackground, bool hovered,
                                  bool pressed, bool enabled);
        void clearNavigationOverlays();
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

    static void showMenu(QMenu *menu);

    static void setUserDefaults();

    static void registerWillPowerOffObserver();

    // Ask AppKit to leave native full screen without publishing a premature
    // Qt window state. Requests made during entry are delivered once AppKit's
    // did-enter notification arrives; exit completion remains owned by the Qt
    // Cocoa platform plugin.
    static bool requestFullScreenExit(QWindow *window);

    static void setFullSizeContentView(QWidget *window, const bool enable);

    static bool getTitlebarHidden(const QWidget *window);

    static void setTitlebarHidden(QWidget *window, const bool hide);

    static void setWindowCollectionBehaviorManaged(QWidget *window);

    // Resolve the user-facing System option to a deterministic color theme.
    // FOVELLE_SYSTEM_THEME is intentionally supported for repeatable tests;
    // production builds use the current AppKit effective appearance.
    static Qv::Theme resolvedTheme(Qv::Theme theme);

    // Apply the user-selected scheme at the application level.  Qt's Cocoa
    // platform theme builds QWidget palettes from NSApp.effectiveAppearance,
    // so a window-only override is insufficient for dialog contents.
    static void setApplicationTheme(Qv::Theme theme);

    static void setWindowTheme(Qv::Theme theme, QWindow *window);

    static QString getWindowAppearanceName(const QWindow *window);

    // Install the system Settings toolbar used to switch the hidden Qt page
    // model.  The AppKit toolbar supplies native layout, selection, vibrancy,
    // accessibility, and automatic Light/Dark rendering.
    static void configureSettingsToolbar(QWindow *window, QTabBar *categoryTabs);

    static bool hasNativeSettingsToolbar(const QWindow *window);

    static int getObscuredHeight(QWindow *window);

    static bool startWindowDrag(QWindow *window);

    static void setWindowMenu(QMenu *menu);

    // Resolve an AppKit Window-menu command using Fovelle's selected
    // application language rather than the macOS login language. The public
    // helper also gives localization tests the same path used by NSMenu.
    static QString localizedWindowMenuTitle(const QString &sourceTitle,
                                            bool submenuTitle = false);

    static void setAlternate(QMenu *menu, int index);

    static void setDockRecents(const QStringList &recentPathsList);

    static QList<OpenWith::OpenWithItem> getOpenWithItems(const QString &filePath, const bool loadIcons, const QString &defaultSuffix);

    struct FileAssociationResult
    {
        int requestedCount {0};
        int associatedCount {0};
        QStringList failedExtensions;
    };

    // Set Fovelle as the Launch Services viewer for every extension in the
    // application registry.  The dry-run switch keeps unit tests non-invasive
    // while the Preferences action uses the real Launch Services operation.
    static FileAssociationResult associateAllSupportedFormats(const QStringList &extensions,
                                                              bool dryRun = false);

    static QByteArray getIccProfileForWindow(const QWindow *window);

    static QList<QByteArray> getAdditionalImageFormats();

    static QList<QString> getAdditionalImageMimeTypes();

    static bool supportsAdditionalImageFormat(const QByteArray &format);

    // Reads only Image I/O properties; it never materializes the image pixels.
    static QSize imagePixelSize(const QString &filePath);

    // Image I/O identifies the file from its contents. The caller must not use
    // a filename extension to decide whether this is a RAW image. Native SDR
    // and HDR graphs preserve source detail while `image` is only the bounded
    // Qt compatibility proxy used until the native layer presents.
    static NativeImageReadResult readImageWithImageIO(const QString &filePath,
                                                      int fallbackLargestDimension = 0);

    // Rasterize only the requested PDF page region at the final device-pixel
    // size.  EPS uses this after its PostScript program has been normalized to
    // a vector PDF; callers never need a zoom-sized whole-document bitmap.
    static QImage renderPDFVectorTile(const QByteArray &pdfData,
                                      const QSizeF &logicalPageSize,
                                      const QRectF &sourceRect,
                                      const QSize &pixelSize,
                                      QString *errorString = nullptr);

    static PDFVectorDocumentPtr createPDFVectorDocument(
        const QByteArray &pdfData, QString *errorString = nullptr);

    // Pure headroom policy helpers are exposed so endpoint behavior can be
    // verified without depending on a particular physical display. Production
    // presentation submits the final endpoint; the curve remains useful for
    // deterministic policy compatibility and explicit callers.
    static qreal easedHDRTransition(qreal progress);

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

    // Only a final-headroom drawable whose physical geometry is complete may
    // replace the SDR proxy or prior HDR presentation.
    static bool isFinalHDRFrameReadyForReveal(bool drawableGeometryMatches,
                                              qreal transitionProgress);

    // The persistent surface is installed below Qt's flipped QNSView. Keep
    // the standalone Core Animation container unflipped and consume the
    // QGraphicsView viewport corners directly. The opening Metal render-target
    // path has a different coordinate contract and performs its own Y flip.
    static bool persistentHDRLayerGeometryFlipped();
    static QTransform persistentHDRLayerTransform(const QSizeF &sourceSize,
                                                  const QPolygonF &imageCorners);

    // Non-invasive test/diagnostic probe. It reduces each retained CI graph to
    // one floating-point maximum pixel without converting the source to an SDR
    // CGImage or NSImage.
    static HDRPixelStatistics probeHDRPixelStatistics(const HDRImagePtr &image);

    static QImage readAdditionalImage(const QString &filePath, QString *errorString = nullptr);

    static std::unique_ptr<AnimatedImage> createAnimatedImage(const QString &filePath);
};

#endif // QVCOCOAFUNCTIONS_H
