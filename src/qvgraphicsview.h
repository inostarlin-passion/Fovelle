#ifndef QVGRAPHICSVIEW_H
#define QVGRAPHICSVIEW_H

#include "qvnamespace.h"
#include "qvimagecore.h"
#include "axislocker.h"
#include "logicalpixelfitter.h"
#include "scrollhelper.h"
#include <memory>
#include <optional>
#include <QGraphicsView>
#include <QImageReader>
#include <QMimeData>
#include <QNativeGestureEvent>
#include <QPolygonF>
#include <QDir>
#include <QTimer>
#include <QFileInfo>
#include <QBrush>

class MainWindow;
class QVGraphicsImageItem;
class QPropertyAnimation;

class QVGraphicsView : public QGraphicsView
{
    Q_OBJECT
    Q_PROPERTY(qreal animatedZoomLevel READ animatedZoomLevel WRITE setAnimatedZoomLevel)

public:
    static constexpr int ZoomTransitionDurationMs = 200;
    static constexpr int ZoomAnchorSettleDelayMs = 150;

    QVGraphicsView(QWidget *parent = nullptr);

    struct SwipeData
    {
        int totalDelta;
        bool triggeredAction;
    };

    QMimeData* getMimeData() const;
    void loadMimeData(const QMimeData *mimeData);
    void loadFile(const QString &fileName, const QString &baseDir = "");

    void reloadFile();

    void shutdownAsyncWork();

    void zoomIn();

    void zoomOut();

    void zoomRelative(const qreal relativeLevel, const std::optional<QPoint> &mousePos = {});

    void zoomAbsolute(const qreal absoluteLevel,
                      const std::optional<QPoint> &targetPos = {},
                      const bool isApplyingCalculation = false,
                      const bool animateTransition = true);

    const std::optional<Qv::CalculatedZoomMode> &getCalculatedZoomMode() const;
    void setCalculatedZoomMode(const std::optional<Qv::CalculatedZoomMode> &value, const bool isNavigating = false, const std::optional<QPoint> &mousePos = {});

    bool getNavigationResetsZoom() const { return navigationResetsZoom; }
    void setNavigationResetsZoom(const bool value);

    Qv::SortMode getSortMode() const { return imageCore.getSortMode(); }
    void setSortMode(const Qv::SortMode mode) { imageCore.setSortMode(mode); }
    bool getSortDescending() const { return imageCore.getSortDescending(); }
    void setSortDescending(const bool descending) { imageCore.setSortDescending(descending); }

    void applyExpensiveScaling();
    void removeExpensiveScaling();

    void recalculateZoom(const bool animateTransition = true,
                         const std::optional<QPoint> &zoomAnchor = {});

    // Toggle is a view operation because its decision depends on the
    // displayed frame and on the cursor in the view, not merely on the
    // calculated-zoom enum stored by MainWindow.
    void toggleFitAnd100();

    bool isImageAtFit() const;

    void centerImage();

    void setCursorVisible(const bool visible);

    const QJsonObject getSessionState() const;

    void loadSessionState(const QJsonObject &state);

    void setLoadIsFromSessionRestore(const bool value);

    void goToFile(const Qv::GoToFileMode mode, const int index = 0);

    void settingsUpdated(const bool isInitialLoad);

    void closeImage(const bool stayInDir = false);
    void jumpToNextFrame();
    void jumpToPreviousFrame();
    void setPaused(const bool &desiredState);
    void setSpeed(const int &desiredSpeed);
    void rotateImage(const int relativeAngle);
    void mirrorImage();
    void flipImage();
    void resetTransformation();

    void fitOrConstrainImage();

    // Full-screen transitions can resize the viewport several times after
    // AppKit publishes the new window state. Preserve a manually selected
    // image edge for the whole transition, not just for one resize event.
    void beginFullScreenPanPreservation();
    void refreshFullScreenPanPreservation();
    void endFullScreenPanPreservation();

    QSizeF getEffectiveOriginalSize() const;

    QRect fullScreenTransitionImageRect() const;

    QImage fullScreenTransitionImage() const;

    LogicalPixelFitter getPixelFitter() const;

    const QVImageCore::FileDetails& getCurrentFileDetails() const { return imageCore.getCurrentFileDetails(); }
    const QVMovie& getLoadedMovie() const { return imageCore.getLoadedMovie(); }
    bool hasFileOrPendingLoad() const { return imageCore.hasFileOrPendingLoad(); }
    bool hasPreviousFile() { return imageCore.hasPreviousFile(); }
    bool hasNextFile() { return imageCore.hasNextFile(); }
    void refreshVerticalScrollBarGeometry();
    qreal getZoomLevel() const { return zoomLevel; }
    qreal animatedZoomLevel() const { return displayedZoomLevel; }
    void setAnimatedZoomLevel(qreal level);
    bool isZoomTransitionRunning() const;
    bool usesVectorRendering() const;
    Qv::VectorImageFormat vectorImageFormat() const;
    QSize lastVectorRasterSize() const;
    quint64 vectorRenderCount() const;
    bool hasPendingVectorRefinement() const;

    static qreal boundedZoomLevel(qreal requestedLevel);

    // Keep wheel-step calculation pure so mouse and touchpad behavior can be
    // verified without depending on platform event delivery.
    static qreal wheelZoomFactor(int wheelDelta, qreal zoomMultiplier, bool useFractionalSteps);

    // Keep calculated-zoom restoration tolerant to floating-point round trips.
    static bool zoomLevelsEquivalent(qreal lhs, qreal rhs);

    // Keep native gesture conversion pure so macOS event semantics can be
    // verified without depending on a physical trackpad.
    static qreal nativeGestureZoomFactor(qreal value);

    static QPointF nativeGesturePanScrollDelta(const QPointF &delta, bool isRightToLeft);

    // Project an explicit zoom request onto the displayed image rectangle.
    // Keeping this operation pure makes the inside-image and outside-image
    // anchor contract independently testable.
    static QPointF projectZoomAnchor(const QRectF &imageViewportRect,
                                     const QPointF &requestedViewportPoint);

    // Keep the Theme-to-scrollbar contract observable without rendering.
    static QString scrollBarStyleSheet(Qv::Theme theme);

    // Keep the small-image policy independent from widget state so its boundary
    // conditions can be tested deterministically.
    static bool shouldDisplaySmallImageAtOneToOne(
        const QSizeF &imageSize,
        const QSize &viewportSize,
        bool settingEnabled,
        Qv::WindowResizeMode windowResizeMode);

    // Compare the complete viewport contract used by the independent Metal
    // layer. A zoom value alone is insufficient because scroll offsets,
    // viewport resize and titlebar layout can change while zoom stays fixed.
    static bool hdrViewportGeometryEquivalent(
        const QSize &lhsViewportSize,
        const QPolygonF &lhsImageCorners,
        const QSize &rhsViewportSize,
        const QPolygonF &rhsImageCorners,
        qreal tolerance = 0.01);

    // Once a prepared Metal frame is visible, geometry changes must reuse it
    // instead of falling back to SDR and restarting the opening transition.
    static bool canReuseHDRPresentation(bool firstFramePresented, bool hdrPrepared);

    std::optional<qreal> sampleDisplayedImageBrightness(const QPoint &viewportPoint) const;

    bool usesNativeSDRMetalRenderer() const;
    QVCocoaFunctions::HDRRendererDiagnostics nativeMetalRendererDiagnostics() const;
    bool usesNativeHDRNavigationOverlay() const;
    void setHDRPresentationActive(bool active);
    void setHDRNavigationOverlay(int index, const QRectF &viewportRect,
                                 qreal opacity, bool previous,
                                 bool darkBackground, bool hovered,
                                 bool pressed, bool enabled);
    void clearHDRNavigationOverlays();

    int getFitOverscan() const { return fitOverscan; }

signals:
    void cancelSlideshow();

    void fileChanged(const bool isRestoringState);

    void zoomLevelChanged();

    void calculatedZoomModeChanged();

    void navigationResetsZoomChanged();

    void sortParametersChanged();

protected:
    void resizeEvent(QResizeEvent *event) override;

    void scrollContentsBy(int dx, int dy) override;

    void paintEvent(QPaintEvent *event) override;

    void drawBackground(QPainter *painter, const QRectF &rect) override;

    void dropEvent(QDropEvent *event) override;

    void dragEnterEvent(QDragEnterEvent *event) override;

    void dragMoveEvent(QDragMoveEvent *event) override;

    void dragLeaveEvent(QDragLeaveEvent *event) override;

    void mousePressEvent(QMouseEvent *event) override;

    void mouseReleaseEvent(QMouseEvent *event) override;

    void mouseMoveEvent(QMouseEvent *event) override;

    void mouseDoubleClickEvent(QMouseEvent *event) override;

    bool viewportEvent(QEvent *event) override;

    bool event(QEvent *event) override;

    bool eventFilter(QObject *watched, QEvent *event) override;

    void focusInEvent(QFocusEvent *event) override;

    void focusOutEvent(QFocusEvent *event) override;

    void wheelEvent(QWheelEvent *event) override;

    void keyPressEvent(QKeyEvent *event) override;

    void contextMenuEvent(QContextMenuEvent *event) override;

    void executeClickAction(const Qv::ViewportClickAction action, const QPoint mousePos);

    void startDragAction(const Qv::ViewportDragAction action);

    void resetDragState();

    void executeDragAction(const Qv::ViewportDragAction action, const QPoint delta, bool &isMovingWindow);

    void executeScrollAction(const Qv::ViewportScrollAction action, const QPoint delta, const QPoint mousePos, const bool hasShiftModifier, const bool useFractionalZoom);

    bool isSmoothScalingRequested() const;

    bool isExpensiveScalingRequested() const;

    void matchContentCenter(const QRect target);

    std::optional<Qv::GoToFileMode> getNavigationRegion(const QPoint mousePos) const;

    QRect getContentRect() const;

    QRect getUsableViewportRect(const bool addOverscan = false) const;

    // Fit is a no-scrollbar destination.  Derive its size from the viewport
    // available after AsNeeded bars disappear so the animation has one stable
    // endpoint instead of requiring a visible terminal correction.
    QSize getFitViewportSize(const bool addOverscan = false) const;

    void setTransformScale(const qreal absoluteScale);

    void logViewportState(const char *phase) const;

    void updateHDRRenderer();

    void requestHDRRendererUpdate();

    QPolygonF getHDRViewportCorners() const;

    void stageHDRGeometry(const QSize &viewportSize, const QPolygonF &imageCorners);

    void finishHDRGeometryStabilization();

    void logHDRState(const char *phase) const;

    void setTransformWithNormalization(const QTransform &matrix);

    void updateViewportOpacityContract();

    void setVectorInteractionPresentation(bool active);

    QTransform getUnspecializedTransform() const;

    QTransform normalizeTransformOrigin(const QTransform &matrix, const QSizeF &pixmapSize) const;

    qreal getDpiAdjustment() const;

    void handleDpiAdjustmentChange();

    void handleSmoothScalingChange();

    int getRtlFlip() const;

    qreal calculateZoomLevelForMode(Qv::CalculatedZoomMode mode) const;

    std::optional<QPoint> getCursorViewportPosition() const;

    QRect getContentRectForZoomLevel(qreal level) const;

    QRect getDisplayedContentRect() const;

    QRect getScrollContentRect() const;

    QPoint zoomAnchorViewportPoint(const QPoint &requestedPoint) const;

    void finishZoomTransition();

    void stopZoomTransition();

    void settlePendingZoomAnchor();

    void cancelTurboNav();

    MainWindow* getMainWindow() const;

    bool handleNativeGestureEvent(QNativeGestureEvent *event);

    void updateSceneRect(const std::optional<QPoint> &restoreScrollPosition = {},
                         bool preserveScrollEdges = false);

    QRectF getSceneRectForViewport() const;

    void applyScrollBarTheme(Qv::Theme theme);

    void applyHDRViewportBackground(Qv::Theme theme);

private slots:
    void animatedFrameChanged(QRect rect);

    void beforeLoad();

    void postLoad();

private:
    enum class ScrollEdge { None, Minimum, Maximum };

    void ensureHDRRenderer();

    ScrollEdge getScrollEdge(const QScrollBar *scrollBar) const;
    void captureFullScreenPanEdges();
    void captureFullScreenPanAnchor();
    void captureFullScreenPanState();
    void restoreFullScreenPanPreservation();
    void cancelPendingZoomAnchor();
    void restorePendingZoomAnchor();
    void restoreSettledZoomAnchor();
    void scheduleVerticalScrollBarGeometry();

    QVGraphicsImageItem *loadedPixmapItem {nullptr};
    std::unique_ptr<QVCocoaFunctions::HDRRenderer> hdrRenderer;

    Qv::SmoothScalingMode smoothScalingMode {Qv::SmoothScalingMode::Disabled};
    std::optional<qreal> smoothScalingLimit;
    bool expensiveScalingAboveWindowSize {false};
    std::optional<qreal> fitZoomLimit;
    int fitOverscan {0};
    bool zoomToCursor {true};
    bool useOneToOnePixelSizing {true};
    bool showSmallImagesAtOneToOne {false};
    bool constrainImagePosition {true};
    bool constrainToCenterWhenSmaller {true};
    bool disableDelayedConstraint {false};
    bool checkerboardBackground {false};
    QBrush viewportBackgroundBrush;
    QBrush checkerboardBackgroundBrush;
    Qv::CalculatedZoomMode defaultCalculatedZoomMode {Qv::CalculatedZoomMode::ZoomToFit};
    qreal zoomMultiplier {1.25};

    bool enableNavigationRegions {false};
    Qv::ViewportClickAction doubleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportClickAction altDoubleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportDragAction dragAction {Qv::ViewportDragAction::None};
    Qv::ViewportDragAction altDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportClickAction middleClickAction {Qv::ViewportClickAction::None};
    Qv::ViewportClickAction altMiddleClickAction {Qv::ViewportClickAction::None};
    Qv::ClickOrDrag middleButtonMode {Qv::ClickOrDrag::Click};
    Qv::ViewportDragAction middleDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportDragAction altMiddleDragAction {Qv::ViewportDragAction::None};
    Qv::ViewportScrollAction verticalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction horizontalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction altVerticalScrollAction {Qv::ViewportScrollAction::None};
    Qv::ViewportScrollAction altHorizontalScrollAction {Qv::ViewportScrollAction::None};
    bool scrollActionCooldown {false};

    std::optional<Qv::CalculatedZoomMode> calculatedZoomMode;
    std::optional<Qv::CalculatedZoomMode> lastCalculatedZoomMode;
    std::optional<qreal> lastCalculatedZoomLevel;
    bool globalNavigationResetsZoom {true};
    bool navigationResetsZoom {true};
    bool loadIsFromSessionRestore {false};
    qreal zoomLevel {1.0};
    qreal displayedZoomLevel {1.0};
    qreal appliedDpiAdjustment {1.0};
    qreal appliedExpensiveScaleZoomLevel {0.0};
    bool zoomTransitionCentersImage {false};
    bool isUpdatingSceneRect {false};
    bool verticalScrollBarGeometryUpdatePending {false};
    bool isUpdatingVerticalScrollBarGeometry {false};
    bool fullScreenPanInternalUpdate {false};
    bool fullScreenPanPreservationActive {false};
    ScrollEdge fullScreenHorizontalPanEdge {ScrollEdge::None};
    ScrollEdge fullScreenVerticalPanEdge {ScrollEdge::None};
    // A manual scrollbar drag can cancel a zoom while the animated frame is
    // still growing. Keep an explicitly selected endpoint stable as the
    // scrollbar range expands during that transition.
    ScrollEdge zoomTransitionHorizontalPanEdge {ScrollEdge::None};
    ScrollEdge zoomTransitionVerticalPanEdge {ScrollEdge::None};
    std::optional<QPointF> fullScreenPanAnchorScene;
    std::optional<QPointF> pendingZoomAnchorScene;
    std::optional<QPoint> pendingZoomAnchorViewport;
    std::optional<QPointF> settledZoomAnchorScene;
    std::optional<QPoint> settledZoomAnchorViewport;
    bool pendingZoomAnchorFollowsViewportCenter {false};
    quint64 pendingZoomAnchorGeneration {0};
    quint64 zoomAnchorSettleGeneration {0};
    std::optional<QPoint> lastZoomEventPos;
    QPointF lastZoomRoundingError;
    bool isCursorAutoHideFullscreenEnabled {true};
    bool isCursorVisible {true};
    QRect lastImageContentRect;

    QVImageCore imageCore {this};

    QTimer *expensiveScaleTimer;
    QPropertyAnimation *zoomAnimation;
    QTimer *vectorRefineTimer;
    QTimer *constrainBoundsTimer;
    QTimer *zoomAnchorSettleTimer;
    QTimer *zoomAnchorPostLayoutTimer;
    QTimer *verticalScrollBarGeometryTimer;
    QTimer *hideCursorTimer;
    QTimer *hdrPresentationTimer;
    QTimer *hdrGeometryTimer;
    QTimer *hdrFrameRequestTimer;
    QElapsedTimer hdrInteractionClock;
    QElapsedTimer hdrScrollInteractionClock;
    qreal hdrInteractionZoomMilliseconds{ 0.0 };
    int hdrInteractionStep{ -1 };
    bool hdrActivationCompleted{ false };
    bool hdrPresentationActive{ true };
    quint64 hdrPresentationRequestGeneration{ 0 };
    bool hdrRendererActive{ false };
    bool hdrLayoutReady{ false };
    bool hdrPendingGeometryValid{ false };
    bool hdrInteractionTestScheduled{ false };
    bool hdrThemeTestScheduled{ false };
    bool hdrFocusTransitionTestScheduled{ false };
    QSize hdrPendingViewportSize;
    QPolygonF hdrPendingImageCorners;
    QImage navigationSamplingImage;
    QSize navigationSamplingSourceSize;

    ScrollHelper *scrollHelper;
    AxisLocker scrollAxisLocker;
    Qt::MouseButton pressedMouseButton {Qt::MouseButton::NoButton};
    Qt::KeyboardModifiers mousePressModifiers {Qt::KeyboardModifier::NoModifier};
    bool isDelayingDrag {false};
    bool isLastMousePosDubious {false};
    bool isSystemWindowDragActive {false};
    QPoint lastMousePos;
    // QAction::triggered() does not carry a viewport position.  Keep the last
    // delivered mouse position so keyboard/menu zooms use the same anchor as
    // the pointer without depending on a platform global-cursor query.
    std::optional<QPoint> lastMouseViewportPosition;
    QElapsedTimer lastFocusIn;

    std::optional<Qv::GoToFileMode> turboNavMode;
    QList<QKeySequence> navPrevShortcuts;
    QList<QKeySequence> navNextShortcuts;
    QList<QKeySequence> navRandomShortcuts;
    QElapsedTimer lastTurboNav;
    QElapsedTimer lastTurboNavKeyPress;
    int turboNavInterval {0};

    const int startDragDistance {3};
};
Q_DECLARE_METATYPE(QVGraphicsView::SwipeData)
#endif // QVGRAPHICSVIEW_H
