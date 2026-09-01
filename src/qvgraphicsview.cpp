#include "qvgraphicsview.h"
#include "qvgraphicsimageitem.h"
#include "qvapplication.h"
#include "qvinfodialog.h"
#include "qvmovie.h"
#include "qvcocoafunctions.h"
#include <QWheelEvent>
#include <QGraphicsScene>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScopedValueRollback>
#include <QtMath>
#include <QGestureEvent>
#include <QScrollBar>
#include <QPainter>

#include <algorithm>
#include <cmath>

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);

    const auto scheduleScrollBarGeometryUpdate = [this](int, int) {
        scheduleVerticalScrollBarGeometry();
    };
    connect(horizontalScrollBar(), &QScrollBar::rangeChanged, this,
            scheduleScrollBarGeometryUpdate);
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this,
            scheduleScrollBarGeometryUpdate);
    QWidget *barGeometryWidget = verticalScrollBar()->parentWidget();
    if (!barGeometryWidget || barGeometryWidget == this
        || barGeometryWidget == viewport())
        barGeometryWidget = verticalScrollBar();
    barGeometryWidget->installEventFilter(this);

    // Scene setup
    auto *scene = new QGraphicsScene(this);
    // This view owns one image item.  Avoid maintaining/querying the BSP
    // index on every scroll; the linear walk is constant-time for this scene
    // and leaves the backing-store scroll path with less bookkeeping.
    scene->setItemIndexMethod(QGraphicsScene::NoIndex);
    setScene(scene);

    scrollHelper = new ScrollHelper(this,
        [this](ScrollHelper::Parameters &p)
        {
            p.contentRect = getContentRect();
            p.usableViewportRect = getUsableViewportRect();
            p.shouldConstrain = constrainImagePosition;
            p.shouldCenter = constrainToCenterWhenSmaller;
        });

    connect(&imageCore, &QVImageCore::animatedFrameChanged, this, &QVGraphicsView::animatedFrameChanged);
    connect(&imageCore, &QVImageCore::fileChanging, this, &QVGraphicsView::beforeLoad);
    connect(&imageCore, &QVImageCore::fileChanged, this, &QVGraphicsView::postLoad);
    connect(&imageCore, &QVImageCore::sortParametersChanged, this, [this]{emit sortParametersChanged();});

    expensiveScaleTimer = new QTimer(this);
    expensiveScaleTimer->setSingleShot(true);
    expensiveScaleTimer->setInterval(50);
    connect(expensiveScaleTimer, &QTimer::timeout, this, [this]{applyExpensiveScaling();});

    vectorRefineTimer = new QTimer(this);
    vectorRefineTimer->setSingleShot(true);
    vectorRefineTimer->setInterval(50);
    connect(vectorRefineTimer, &QTimer::timeout, this, [this]() {
        setVectorInteractionPresentation(false);
    });

    constrainBoundsTimer = new QTimer(this);
    constrainBoundsTimer->setSingleShot(true);
    connect(constrainBoundsTimer, &QTimer::timeout, this, [this]{
        QScopedValueRollback<bool> internalUpdateGuard(
                fullScreenPanInternalUpdate, true);
        scrollHelper->constrain();
        restoreFullScreenPanPreservation();
    });

    hideCursorTimer = new QTimer(this);
    hideCursorTimer->setSingleShot(true);
    hideCursorTimer->setInterval(1000);
    connect(hideCursorTimer, &QTimer::timeout, this, [this]{setCursorVisible(false);});

    loadedPixmapItem = new QVGraphicsImageItem();
    scene->addItem(loadedPixmapItem);

    hdrFrameRequestTimer = new QTimer(this);
    hdrFrameRequestTimer->setSingleShot(true);
    hdrFrameRequestTimer->setInterval(0);
    connect(hdrFrameRequestTimer, &QTimer::timeout, this,
            &QVGraphicsView::updateHDRRenderer);
    hdrPresentationTimer = new QTimer(this);
    hdrPresentationTimer->setInterval(16);
    connect(hdrPresentationTimer, &QTimer::timeout, this, [this]() {
        if (!hdrRendererActive || !hdrRenderer) {
            hdrPresentationTimer->stop();
            return;
        }
        const auto rendererState = hdrRenderer->diagnostics();
        if (rendererState.firstFramePresented
            && qEnvironmentVariableIsSet("FOVELLE_HDR_TRANSITION_LOG")) {
            qInfo().noquote() << "FOVELLE_HDR_TRANSITION"
                              << QJsonDocument(QJsonObject{
                                     { QStringLiteral("active_requested"),
                                       rendererState.presentationActiveRequested },
                                     { QStringLiteral("animation_in_flight"),
                                       rendererState.presentationAnimationInFlight },
                                     { QStringLiteral("fallback_visible"),
                                       loadedPixmapItem->isVisible() },
                                     { QStringLiteral("opacity"),
                                       rendererState.layerOpacity },
                                     { QStringLiteral("transition_count"),
                                       static_cast<qint64>(
                                               rendererState.presentationTransitionCount) },
                                     { QStringLiteral("wants_edr"),
                                       rendererState.wantsExtendedDynamicRangeContent },
                                 }).toJson(QJsonDocument::Compact);
        }
        const bool fullyVisible = hdrPresentationActive
                && rendererState.firstFramePresented
                && rendererState.firstVisibleFrameUsesFinalHeadroom
                && rendererState.presentationActiveRequested
                && !rendererState.presentationAnimationInFlight
                && rendererState.layerOpacity >= 0.999F;
        if (fullyVisible) {
            loadedPixmapItem->setVisible(false);
            hdrActivationCompleted = true;
            // Once the opaque native HDR surface is visible, repainting the
            // hidden QGraphicsScene only creates an additional Qt backing
            // store transaction for every scrollbar change.  The Metal layer
            // and its native navigation sublayers are now the sole viewport
            // presentation path, so leave the Qt pixels parked until the next
            // image load.
            setViewportUpdateMode(QGraphicsView::NoViewportUpdate);
            hdrPresentationTimer->stop();
            logHDRState("final-frame-visible");
            return;
        }
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        loadedPixmapItem->setVisible(true);
        hdrActivationCompleted = false;
        if (rendererState.firstFramePresented
            && !rendererState.presentationActiveRequested
            && !rendererState.presentationAnimationInFlight) {
            hdrPresentationTimer->stop();
            logHDRState("inactive-sdr-visible");
        }
    });
    hdrGeometryTimer = new QTimer(this);
    hdrGeometryTimer->setSingleShot(true);
    hdrGeometryTimer->setInterval(34);
    connect(hdrGeometryTimer, &QTimer::timeout, this,
            &QVGraphicsView::finishHDRGeometryStabilization);
    const auto viewportScrollChanged = [this]() {
        // Keep fullscreen preservation tied to user-visible viewport changes,
        // but do not infer user input from valueChanged alone: QAbstractScrollArea
        // emits the same signal while it lays out AsNeeded scrollbars after a
        // zoom. Those internal changes must not cancel the pending zoom anchor.
        const bool isExternalViewportChange =
            !fullScreenPanInternalUpdate && !isUpdatingSceneRect;
        // During a fullscreen transition, a scrollbar change outside an
        // explicitly guarded geometry update is the newest user-visible pan
        // state. Capture it before a later resize or scene-rect rebuild can
        // replay the anchor from the transition's request boundary. This also
        // covers direct QScrollBar::setValue() calls used by the regression
        // test; QAbstractSlider emits valueChanged for every changed value.
        if (fullScreenPanPreservationActive && isExternalViewportChange)
            captureFullScreenPanState();
        // Scrollbar values also change while opening/fitting a file. Treat
        // only changes after the native surface is fully active as an input
        // burst; otherwise cold open needlessly keeps the display link and
        // drawable pool busy for another 160 ms.
        if (hdrActivationCompleted)
            hdrScrollInteractionClock.restart();
        requestHDRRendererUpdate();
        if (getCurrentFileDetails().isVectorLoaded)
        {
            // Every pan source eventually changes a scrollbar: mouse drag,
            // wheel/trackpad, keyboard, scrollbar thumb, and constraint
            // animation.  Keep source rasterization off the GUI thread for
            // the whole burst, then request one exact terminal-density tile.
            setVectorInteractionPresentation(true);
        }
    };
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            viewportScrollChanged);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            viewportScrollChanged);
    connect(horizontalScrollBar(), &QScrollBar::sliderPressed, this,
            [this]() { cancelPendingZoomAnchor(); });
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, this,
            [this]() { cancelPendingZoomAnchor(); });
    connect(horizontalScrollBar(), &QScrollBar::actionTriggered, this,
            [this](int) { cancelPendingZoomAnchor(); });
    connect(verticalScrollBar(), &QScrollBar::actionTriggered, this,
            [this](int) { cancelPendingZoomAnchor(); });
    connect(qvApp, &QGuiApplication::applicationStateChanged, this,
            [this](const Qt::ApplicationState state) {
        const bool active = state == Qt::ApplicationActive
                && window() && window()->isActiveWindow();
        if (active != hdrPresentationActive)
            setHDRPresentationActive(active);
    });

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, [this]{settingsUpdated(false);});
    settingsUpdated(true);
}

// Events

void QVGraphicsView::scrollContentsBy(const int dx, const int dy)
{
    // QGraphicsView implements MinimalViewportUpdate by scrolling the
    // platform backing store and repainting only the newly exposed stripe.
    // That reuse is unsafe for an asynchronously refined transparent vector
    // tile on macOS: the old frame can remain visible until the worker result
    // arrives. Switch before the base implementation performs the scroll so
    // the first drag frame is a complete repaint.
    if ((dx != 0 || dy != 0) && getCurrentFileDetails().isVectorLoaded)
        setVectorInteractionPresentation(true);
    QGraphicsView::scrollContentsBy(dx, dy);
}

void QVGraphicsView::resizeEvent(QResizeEvent *event)
{
    if (const auto mainWindow = getMainWindow())
        if (mainWindow->getIsClosing())
            return;

    if (getCurrentFileDetails().isPixmapLoaded)
    {
        const bool shouldRestoreCalculatedZoom =
            !calculatedZoomMode.has_value() &&
            lastCalculatedZoomMode.has_value() &&
            lastCalculatedZoomLevel.has_value() &&
            zoomLevelsEquivalent(zoomLevel, lastCalculatedZoomLevel.value());

        // A resize normally keeps the scene focal point fixed by moving the
        // scroll position by half of the viewport delta.  That is correct for
        // an interior position, but it is wrong once the user has panned an
        // image to an edge: shrinking the viewport then pulls the selected
        // edge back into the middle of the image.  Remember the old scroll
        // endpoints so a full-screen round trip can preserve a deliberately
        // selected corner as well as an ordinary resize preserves its focus.
        const bool preserveManualPanEdges =
                !calculatedZoomMode.has_value() && !shouldRestoreCalculatedZoom;
        const QScrollBar *horizontalBar = horizontalScrollBar();
        const QScrollBar *verticalBar = verticalScrollBar();
        const ScrollEdge horizontalPanEdge = fullScreenHorizontalPanEdge != ScrollEdge::None
                ? fullScreenHorizontalPanEdge
                : preserveManualPanEdges ? getScrollEdge(horizontalBar)
                                         : ScrollEdge::None;
        const ScrollEdge verticalPanEdge = fullScreenVerticalPanEdge != ScrollEdge::None
                ? fullScreenVerticalPanEdge
                : preserveManualPanEdges ? getScrollEdge(verticalBar)
                                         : ScrollEdge::None;
        const bool wasAtHorizontalMinimum =
                preserveManualPanEdges && horizontalPanEdge == ScrollEdge::Minimum;
        const bool wasAtHorizontalMaximum =
                preserveManualPanEdges && horizontalPanEdge == ScrollEdge::Maximum;
        const bool wasAtVerticalMinimum =
                preserveManualPanEdges && verticalPanEdge == ScrollEdge::Minimum;
        const bool wasAtVerticalMaximum =
                preserveManualPanEdges && verticalPanEdge == ScrollEdge::Maximum;

        {
            QScopedValueRollback<bool> internalUpdateGuard(
                    fullScreenPanInternalUpdate, true);
            QGraphicsView::resizeEvent(event);
        }

        // setSceneRect() can synchronously resize the viewport when an
        // AsNeeded scrollbar changes state.  Do not start another fit pass
        // from that nested resize; Qt documents that this pattern can recurse
        // when automatic scrollbar state toggles during a resize.
        if (isUpdatingSceneRect)
        {
            restorePendingZoomAnchor();
            return;
        }

        // A zoom can make an AsNeeded scrollbar appear without changing the
        // outer view size.  QAbstractScrollArea then resizes the viewport and
        // QGraphicsView's normal half-delta compensation would move the image
        // a second time.  The zoom transaction already owns the focal point;
        // restore it after the scrollbar layout instead of treating this as a
        // user-initiated window resize.
        if (pendingZoomAnchorScene.has_value()
            && pendingZoomAnchorViewport.has_value())
        {
            restorePendingZoomAnchor();
            logViewportState("resize-zoom-anchor-restored");
            refreshVerticalScrollBarGeometry();
            return;
        }

        if (shouldRestoreCalculatedZoom)
        {
            calculatedZoomMode = lastCalculatedZoomMode;
            emit calculatedZoomModeChanged();
        }

        const QSize sizeDelta = event->size() - event->oldSize();
        const QPointF resizeDelta(
            (wasAtHorizontalMinimum || wasAtHorizontalMaximum)
                ? 0.0 : -sizeDelta.width() / 2.0,
            (wasAtVerticalMinimum || wasAtVerticalMaximum)
                ? 0.0 : -sizeDelta.height() / 2.0);
        {
            QScopedValueRollback<bool> internalUpdateGuard(
                    fullScreenPanInternalUpdate, true);
            scrollHelper->move(resizeDelta);
        }
        fitOrConstrainImage();

        // fitOrConstrainImage() may rebuild the explicit scene rect while the
        // titlebar inset changes during a native full-screen transition. Set
        // the endpoint after that rebuild so the new range, rather than the
        // old range, is used for the preserved image edge.
        if (preserveManualPanEdges)
        {
            QScopedValueRollback<bool> internalUpdateGuard(
                    fullScreenPanInternalUpdate, true);
            if (wasAtHorizontalMinimum)
                horizontalScrollBar()->setValue(horizontalScrollBar()->minimum());
            else if (wasAtHorizontalMaximum)
                horizontalScrollBar()->setValue(horizontalScrollBar()->maximum());

            if (wasAtVerticalMinimum)
                verticalScrollBar()->setValue(verticalScrollBar()->minimum());
            else if (wasAtVerticalMaximum)
                verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        }
        logViewportState("resize");
        refreshVerticalScrollBarGeometry();
    }
    else
    {
        QGraphicsView::resizeEvent(event);
        refreshVerticalScrollBarGeometry();
    }
}

void QVGraphicsView::paintEvent(QPaintEvent *event)
{
    static const bool sdrPerformanceLoggingEnabled =
            qEnvironmentVariableIsSet("FOVELLE_SDR_PERF_LOG");
    const bool logSDRPerformance =
            sdrPerformanceLoggingEnabled && !hdrRendererActive;
    QElapsedTimer paintTimer;
    if (logSDRPerformance)
        paintTimer.start();

    // This is the most reliable place to detect DPI changes. QWindow::screenChanged()
    // doesn't detect when the DPI is changed on the current monitor, for example.
    handleDpiAdjustmentChange();

    QGraphicsView::paintEvent(event);

    if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_PAINT_LOG")
        && getCurrentFileDetails().isVectorLoaded)
    {
        qint64 dirtyArea = 0;
        for (const QRect &rect : event->region())
            dirtyArea += static_cast<qint64>(rect.width()) * rect.height();
        const qint64 viewportArea = static_cast<qint64>(viewport()->width())
                * viewport()->height();
        const QString updateMode = viewportUpdateMode() == QGraphicsView::FullViewportUpdate
                ? QStringLiteral("full")
                : viewportUpdateMode() == QGraphicsView::MinimalViewportUpdate
                ? QStringLiteral("minimal") : QStringLiteral("other");
        qInfo().noquote() << "FOVELLE_VECTOR_PAINT"
                          << "update_mode=" << updateMode
                          << "dirty_area=" << dirtyArea
                          << "viewport_area=" << viewportArea
                          << "dirty_ratio="
                          << (viewportArea > 0
                                  ? static_cast<qreal>(dirtyArea) / viewportArea
                                  : 0.0);
    }

    if (logSDRPerformance)
    {
        qint64 dirtyArea = 0;
        for (const QRect &rect : event->region())
            dirtyArea += static_cast<qint64>(rect.width()) * rect.height();
        const qint64 viewportArea = static_cast<qint64>(viewport()->width())
                * viewport()->height();
        qInfo().noquote() << "FOVELLE_SDR_PAINT"
                          << "duration_ms=" << paintTimer.nsecsElapsed() / 1000000.0
                          << "dirty_rects=" << event->region().rectCount()
                          << "dirty_area=" << dirtyArea
                          << "viewport_area=" << viewportArea
                          << "dirty_ratio="
                          << (viewportArea > 0
                                  ? static_cast<qreal>(dirtyArea) / viewportArea
                                  : 0.0)
                          << "dpr=" << devicePixelRatioF()
                          << "viewport_opaque="
                          << viewport()->testAttribute(Qt::WA_OpaquePaintEvent)
                          << "zoom=" << zoomLevel
                          << "expensive_scale_zoom=" << appliedExpensiveScaleZoomLevel
                          << "pixmap_size=" << loadedPixmapItem->pixmap().size();
    }
    requestHDRRendererUpdate();
}

void QVGraphicsView::drawBackground(QPainter *painter, const QRectF &rect)
{
    Q_UNUSED(rect)
    if (!painter || !viewport()->testAttribute(Qt::WA_OpaquePaintEvent))
        return;

    painter->save();
    painter->resetTransform();
    const bool showCheckerboard = checkerboardBackground
            && getCurrentFileDetails().isPixmapLoaded;
    painter->fillRect(viewport()->rect(),
                      showCheckerboard ? checkerboardBackgroundBrush
                                       : viewportBackgroundBrush);
    painter->restore();
}

void QVGraphicsView::updateViewportOpacityContract()
{
    const auto &details = getCurrentFileDetails();
#ifdef Q_OS_MACOS
    // Do not opt SDR raster images into QWidget's accelerated backing-store
    // scroll on macOS. QCALayerBackingStore has to synchronize the unpainted
    // part of each IOSurface before presenting it, so repainting only the
    // newly exposed strip creates a hidden near-full-surface copy and can
    // exhaust the normal triple-buffer chain during a 120 Hz drag. Leaving
    // the raster viewport non-opaque makes Qt repaint the complete viewport,
    // which is the stable presentation path used by qView. Vector documents
    // retain an opaque background for idle/minimal paints; the interaction
    // presentation helper temporarily disables scroll reuse during gestures.
    const bool paintsOpaqueViewportBackground = details.isPixmapLoaded
            && !details.isNativeHDRLoaded && !details.isNativeSDRLoaded
            && details.isVectorLoaded;
#else
    const bool paintsOpaqueViewportBackground = details.isPixmapLoaded
            && !details.isNativeHDRLoaded && !details.isNativeSDRLoaded;
#endif
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent,
                             paintsOpaqueViewportBackground);
    if (details.isVectorLoaded)
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
}

void QVGraphicsView::setVectorInteractionPresentation(const bool active)
{
    if (!loadedPixmapItem)
        return;

    if (active)
    {
        if (!getCurrentFileDetails().isVectorLoaded)
            return;
        // FullViewportUpdate disables QGraphicsView's backing-store scroll
        // optimization. It is scoped to the vector interaction burst; the
        // idle path below restores MinimalViewportUpdate for bounded tile
        // refinement and ordinary static painting.
        setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
        loadedPixmapItem->setVectorInteractionActive(true);
        vectorRefineTimer->start();
        if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_PRESENTATION_LOG"))
            qInfo().noquote() << "FOVELLE_VECTOR_PRESENTATION active=true update_mode=full";
        return;
    }

    loadedPixmapItem->setVectorInteractionActive(false);
    if (getCurrentFileDetails().isVectorLoaded)
    {
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        // Publish one clean frame after the mode transition. This also
        // retires any pixels produced by a pre-fix partial update before the
        // exact terminal-density tile is painted.
        viewport()->update();
        if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_PRESENTATION_LOG"))
            qInfo().noquote() << "FOVELLE_VECTOR_PRESENTATION active=false update_mode=minimal";
    }
    else
    {
        setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
        if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_PRESENTATION_LOG"))
            qInfo().noquote() << "FOVELLE_VECTOR_PRESENTATION active=false update_mode=minimal";
    }
}

void QVGraphicsView::dropEvent(QDropEvent *event)
{
    QGraphicsView::dropEvent(event);
    loadMimeData(event->mimeData());
}

void QVGraphicsView::dragEnterEvent(QDragEnterEvent *event)
{
    QGraphicsView::dragEnterEvent(event);
    if (event->mimeData()->hasUrls())
    {
        event->acceptProposedAction();
    }
}

void QVGraphicsView::dragMoveEvent(QDragMoveEvent *event)
{
    QGraphicsView::dragMoveEvent(event);
    event->acceptProposedAction();
}

void QVGraphicsView::dragLeaveEvent(QDragLeaveEvent *event)
{
    QGraphicsView::dragLeaveEvent(event);
    event->accept();
}

void QVGraphicsView::mousePressEvent(QMouseEvent *event)
{
    const auto initializeDrag = [this, event](const Qv::ViewportDragAction action, const bool delayStart = false) {
        pressedMouseButton = event->button();
        mousePressModifiers = event->modifiers();
        isDelayingDrag = delayStart;
        isSystemWindowDragActive = false;
        isLastMousePosDubious = event->type() == QEvent::MouseButtonDblClick && QVApplication::isMouseEventSynthesized(event);
        lastMousePos = event->pos();
        setCursorVisible(true);
        if (!isDelayingDrag)
            startDragAction(action);
    };

    // If a drag is in progress, ignore other button presses if the original button is still
    // pressed, otherwise we missed the button release and can end the original drag
    if (pressedMouseButton != Qt::NoButton)
    {
        if (event->button() != pressedMouseButton && event->buttons().testFlag(pressedMouseButton))
            return;

        resetDragState();
    }

    if (event->button() == Qt::LeftButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        const Qv::ViewportDragAction action = isAltAction ? altDragAction : dragAction;
        const bool justGotFocus = lastFocusIn.isValid() && lastFocusIn.elapsed() < 100;
        const bool isNavRegionPress = !isAltAction && enableNavigationRegions && !justGotFocus && getNavigationRegion(event->pos()).has_value();
        if (action != Qv::ViewportDragAction::None || isNavRegionPress)
        {
            initializeDrag(action, isNavRegionPress);
        }
        return;
    }
    else if (event->button() == Qt::MouseButton::MiddleButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        if (middleButtonMode == Qv::ClickOrDrag::Click)
        {
            const Qv::ViewportClickAction action = isAltAction ? altMiddleClickAction : middleClickAction;
            executeClickAction(action, event->position().toPoint());
        }
        else if (middleButtonMode == Qv::ClickOrDrag::Drag)
        {
            const Qv::ViewportDragAction action = isAltAction ? altMiddleDragAction : middleDragAction;
            if (action != Qv::ViewportDragAction::None)
            {
                initializeDrag(action);
            }
        }
        return;
    }
    else if (event->button() == Qt::MouseButton::BackButton)
    {
        goToFile(Qv::GoToFileMode::Previous);
        return;
    }
    else if (event->button() == Qt::MouseButton::ForwardButton)
    {
        goToFile(Qv::GoToFileMode::Next);
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void QVGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
    if (pressedMouseButton != Qt::NoButton)
    {
        // If some other button initiated the drag, ignore this button release
        if (event->button() != pressedMouseButton)
            return;

        if (isDelayingDrag && pressedMouseButton == Qt::LeftButton)
        {
            const std::optional<Qv::GoToFileMode> navRegion = getNavigationRegion(lastMousePos);
            if (navRegion.has_value())
                goToFile(navRegion.value());
        }

        resetDragState();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void QVGraphicsView::mouseMoveEvent(QMouseEvent *event)
{
    setCursorVisible(true);

    if (pressedMouseButton != Qt::NoButton)
    {
        // If a drag is in progress and we missed the button release, cancel the drag
        if (!event->buttons().testFlag(pressedMouseButton))
        {
            resetDragState();
            return;
        }

        const QPoint delta = event->pos() - lastMousePos;
        const bool isAltAction = mousePressModifiers.testFlag(Qt::ControlModifier);
        const Qv::ViewportDragAction action =
            pressedMouseButton == Qt::LeftButton ? (isAltAction ? altDragAction : dragAction) :
            pressedMouseButton == Qt::MiddleButton ? (isAltAction ? altMiddleDragAction : middleDragAction) :
            Qv::ViewportDragAction::None;
        if (isDelayingDrag)
        {
            if (isLastMousePosDubious)
            {
                // On the second press of a double tap on a touch screen, the position may
                // have been copied from the first press, so we can't rely on it
                isLastMousePosDubious = false;
                lastMousePos = event->pos();
                return;
            }
            if (qMax(qAbs(delta.x()), qAbs(delta.y())) < startDragDistance)
                return;
            isDelayingDrag = false;
            startDragAction(action);
        }
        bool isMovingWindow = false;
        executeDragAction(action, delta, isMovingWindow);
        if (!isMovingWindow)
            logViewportState("mouse-drag");
        if (!isMovingWindow)
            lastMousePos = event->pos();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void QVGraphicsView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MouseButton::LeftButton)
    {
        const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
        const bool isInNavRegion = !isAltAction && enableNavigationRegions && getNavigationRegion(lastMousePos).has_value();
        if (!isInNavRegion)
        {
            executeClickAction(isAltAction ? altDoubleClickAction : doubleClickAction, event->position().toPoint());
            return;
        }
    }

    // Pass unhandled events to QWidget instead of QGraphicsView otherwise we won't
    // receive a press event for the second click of a double click
    QWidget::mouseDoubleClickEvent(event);
}

bool QVGraphicsView::viewportEvent(QEvent *event)
{
    if (event->type() == QEvent::NativeGesture)
        return handleNativeGestureEvent(static_cast<QNativeGestureEvent *>(event));

    return QGraphicsView::viewportEvent(event);
}

bool QVGraphicsView::event(QEvent *event)
{
    if (event->type() == QEvent::ShortcutOverride && !turboNavMode.has_value())
    {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        const ActionManager &actionManager = qvApp->getActionManager();
        if (actionManager.wouldTriggerAction(keyEvent, "previousfile") ||
            actionManager.wouldTriggerAction(keyEvent, "nextfile") ||
            actionManager.wouldTriggerAction(keyEvent, "randomfile"))
        {
            // Accept event to override shortcut and deliver as key press instead
            event->accept();
            return true;
        }
    }
    else if (event->type() == QEvent::KeyRelease && turboNavMode.has_value())
    {
        const QKeyEvent *keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat() &&
            (ActionManager::wouldTriggerAction(keyEvent, navPrevShortcuts) ||
             ActionManager::wouldTriggerAction(keyEvent, navNextShortcuts) ||
             ActionManager::wouldTriggerAction(keyEvent, navRandomShortcuts)))
        {
            cancelTurboNav();
        }
    }

    const bool result = QGraphicsView::event(event);
    if (event->type() == QEvent::LayoutRequest)
        scheduleVerticalScrollBarGeometry();
    return result;
}

bool QVGraphicsView::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *barGeometryWidget = verticalScrollBar()
        ? verticalScrollBar()->parentWidget() : nullptr;
    if (!barGeometryWidget || barGeometryWidget == this
        || barGeometryWidget == viewport())
        barGeometryWidget = verticalScrollBar();

    if (watched == barGeometryWidget
        && (event->type() == QEvent::LayoutRequest
            || event->type() == QEvent::Move
            || event->type() == QEvent::Resize
            || event->type() == QEvent::Show))
        scheduleVerticalScrollBarGeometry();

    return QGraphicsView::eventFilter(watched, event);
}

void QVGraphicsView::focusInEvent(QFocusEvent *event)
{
    lastFocusIn.start();

    QGraphicsView::focusInEvent(event);
}

void QVGraphicsView::focusOutEvent(QFocusEvent *event)
{
    cancelTurboNav();

    QGraphicsView::focusOutEvent(event);
}

void QVGraphicsView::wheelEvent(QWheelEvent *event)
{
    const QInputDevice *device = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    device = event->device();
#endif
    logViewportState("wheel-before");
    if (qEnvironmentVariableIsSet("FOVELLE_DIAGNOSTIC_LOG"))
    {
        qInfo().noquote() << "FOVELLE_WHEEL"
            << "deviceType=" << (device ? static_cast<int>(device->type()) : -1)
            << "pixelDelta=" << event->pixelDelta()
            << "angleDelta=" << event->angleDelta()
            << "phase=" << static_cast<int>(event->phase())
            << "modifiers=" << static_cast<int>(event->modifiers());
    }

    const bool isTouchpad = device != nullptr &&
        device->type() == QInputDevice::DeviceType::TouchPad;
    const bool isPhasedTouchpadScroll = isTouchpad &&
        event->phase() != Qt::NoScrollPhase &&
        event->modifiers() == Qt::NoModifier;
    if (isPhasedTouchpadScroll)
    {
        const QPointF trackpadDelta = !event->pixelDelta().isNull() ?
            QPointF(event->pixelDelta()) : QPointF(event->angleDelta()) / 2.0;
        if (!trackpadDelta.isNull())
        {
            cancelPendingZoomAnchor();
            scrollHelper->move(nativeGesturePanScrollDelta(trackpadDelta, isRightToLeft()));
            constrainBoundsTimer->start();
        }
        event->accept();
        logViewportState("wheel-trackpad-pan");
        return;
    }

    const QPoint eventPos = event->position().toPoint();
    const bool isAltAction = event->modifiers().testFlag(Qt::ControlModifier);
    const Qv::ViewportScrollAction horizontalAction = isAltAction ? altHorizontalScrollAction : horizontalScrollAction;
    const Qv::ViewportScrollAction verticalAction = isAltAction ? altVerticalScrollAction : verticalScrollAction;
    const bool hasHorizontalAction = horizontalAction != Qv::ViewportScrollAction::None;
    const bool hasVerticalAction = verticalAction != Qv::ViewportScrollAction::None;
    if (!hasHorizontalAction && !hasVerticalAction)
        return;
    const QPoint baseDelta =
        hasHorizontalAction && !hasVerticalAction ? QPoint(event->angleDelta().x(), 0) :
        !hasHorizontalAction && hasVerticalAction ? QPoint(0, event->angleDelta().y()) :
        event->angleDelta();
    const QPoint effectiveDelta =
        horizontalAction == verticalAction && Qv::scrollActionIsSelfCompatible(horizontalAction) ? baseDelta :
        scrollAxisLocker.filterMovement(baseDelta, event->phase(), hasHorizontalAction != hasVerticalAction);
    const Qv::ViewportScrollAction effectiveAction =
        effectiveDelta.x() != 0 ? horizontalAction :
        effectiveDelta.y() != 0 ? verticalAction :
        Qv::ViewportScrollAction::None;
    if (effectiveAction == Qv::ViewportScrollAction::None)
        return;
    const bool hasShiftModifier = event->modifiers().testFlag(Qt::ShiftModifier);

    bool useFractionalZoom = false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    useFractionalZoom = device != nullptr &&
        (device->type() == QInputDevice::DeviceType::TouchPad ||
         device->type() == QInputDevice::DeviceType::TouchScreen) &&
        event->phase() != Qt::NoScrollPhase;
#endif

    executeScrollAction(effectiveAction, effectiveDelta, eventPos, hasShiftModifier, useFractionalZoom);
    logViewportState("wheel-after");
}

void QVGraphicsView::keyPressEvent(QKeyEvent *event)
{
    if (turboNavMode.has_value())
    {
        if (ActionManager::wouldTriggerAction(event, navPrevShortcuts) ||
            ActionManager::wouldTriggerAction(event, navNextShortcuts) ||
            ActionManager::wouldTriggerAction(event, navRandomShortcuts))
        {
            lastTurboNavKeyPress.start();
            return;
        }
    }
    else
    {
        const ActionManager &actionManager = qvApp->getActionManager();
        const std::optional<Qv::GoToFileMode> triggeredNavMode =
            actionManager.wouldTriggerAction(event, "previousfile") ? Qv::GoToFileMode::Previous :
            actionManager.wouldTriggerAction(event, "nextfile") ? Qv::GoToFileMode::Next :
            actionManager.wouldTriggerAction(event, "randomfile") ? Qv::GoToFileMode::Random :
            std::optional<Qv::GoToFileMode>();
        if (triggeredNavMode.has_value())
        {
            if (event->isAutoRepeat())
            {
                turboNavMode = triggeredNavMode;
                lastTurboNav.start();
                lastTurboNavKeyPress.start();
                // Remove keyboard shortcuts while turbo navigation is in progress to eliminate any
                // potential overhead. Especially important on macOS which seems to enforce throttling
                // for menu invocations caused by key repeats, which blocks the UI thread (try setting
                // the key repeat rate to max without unbinding the shortcuts - it's really bad).
                navPrevShortcuts = actionManager.getAction("previousfile")->shortcuts();
                navNextShortcuts = actionManager.getAction("nextfile")->shortcuts();
                navRandomShortcuts = actionManager.getAction("randomfile")->shortcuts();
                actionManager.setActionShortcuts("previousfile", {});
                actionManager.setActionShortcuts("nextfile", {});
                actionManager.setActionShortcuts("randomfile", {});
            }
            goToFile(triggeredNavMode.value());
            return;
        }
    }

    // The base class has logic to scroll in response to certain key presses, but we'll
    // handle that ourselves here instead to ensure any bounds constraints are enforced.
    const int scrollXSmallSteps = event->key() == Qt::Key_Left ? -1 : event->key() == Qt::Key_Right ? 1 : 0;
    const int scrollYSmallSteps = event->key() == Qt::Key_Up ? -1 : event->key() == Qt::Key_Down ? 1 : 0;
    const int scrollYLargeSteps = event == QKeySequence::MoveToPreviousPage ? -1 : event == QKeySequence::MoveToNextPage ? 1 : 0;
    if (scrollXSmallSteps != 0 || scrollYSmallSteps != 0 || scrollYLargeSteps != 0)
    {
        const QPoint delta {
            (horizontalScrollBar()->singleStep() * scrollXSmallSteps) * getRtlFlip(),
            (verticalScrollBar()->singleStep() * scrollYSmallSteps) + (verticalScrollBar()->pageStep() * scrollYLargeSteps)
        };
        cancelPendingZoomAnchor();
        scrollHelper->move(delta);
        constrainBoundsTimer->start();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void QVGraphicsView::contextMenuEvent(QContextMenuEvent *event)
{
    // contextMenuEvent fires regardless of whether the original mouse event was already
    // handled, hence this special case to suppress it if a drag is in progress
    if (pressedMouseButton != Qt::NoButton && event->reason() == QContextMenuEvent::Mouse)
        return;

    QGraphicsView::contextMenuEvent(event);
}

// Functions

void QVGraphicsView::executeClickAction(const Qv::ViewportClickAction action, const QPoint mousePos)
{
    if (action == Qv::ViewportClickAction::ZoomToFit)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit);
    }
    else if (action == Qv::ViewportClickAction::FillWindow)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::FillWindow);
    }
    else if (action == Qv::ViewportClickAction::OriginalSize)
    {
        setCalculatedZoomMode(Qv::CalculatedZoomMode::OriginalSize, false, mousePos);
    }
    else if (action == Qv::ViewportClickAction::CenterImage)
    {
        centerImage();
    }
    else if (action == Qv::ViewportClickAction::ToggleFullScreen)
    {
        if (const auto mainWindow = getMainWindow())
            mainWindow->toggleFullScreen();
    }
    else if (action == Qv::ViewportClickAction::ToggleTitlebarHidden)
    {
        if (const auto mainWindow = getMainWindow())
            mainWindow->toggleTitlebarHidden();
    }
}

void QVGraphicsView::startDragAction(const Qv::ViewportDragAction action)
{
    if (action == Qv::ViewportDragAction::Pan)
    {
        viewport()->setCursor(Qt::ClosedHandCursor);
    }
    else if (action == Qv::ViewportDragAction::MoveWindow)
    {
        // Let the window manager handle the move if possible to get window snapping support etc.
        if (pressedMouseButton == Qt::LeftButton && !window()->windowState().testFlag(Qt::WindowFullScreen))
        {
            // Avoid QWindow::startSystemMove due to QTBUG-141220
            isSystemWindowDragActive = QVCocoaFunctions::startWindowDrag(window()->windowHandle());
        }
    }
}

void QVGraphicsView::resetDragState()
{
    pressedMouseButton = Qt::NoButton;
    mousePressModifiers = Qt::NoModifier;
    isDelayingDrag = false;
    isSystemWindowDragActive = false;
    viewport()->setCursor(Qt::ArrowCursor);
    setCursorVisible(true);
    scrollHelper->constrain();
    if (hdrRendererActive)
        requestHDRRendererUpdate();
}

void QVGraphicsView::executeDragAction(const Qv::ViewportDragAction action, const QPoint delta, bool &isMovingWindow)
{
    if (action == Qv::ViewportDragAction::Pan)
    {
        cancelPendingZoomAnchor();
        scrollHelper->move(QPointF(-delta.x() * getRtlFlip(), -delta.y()));
    }
    else if (action == Qv::ViewportDragAction::MoveWindow)
    {
        const auto windowState = window()->windowState();
        if (isSystemWindowDragActive || windowState.testFlag(Qt::WindowFullScreen))
            return;
        window()->move(window()->pos() + delta);
        isMovingWindow = true;
    }
}

qreal QVGraphicsView::wheelZoomFactor(const int wheelDelta, const qreal zoomMultiplier, const bool useFractionalSteps)
{
    if (wheelDelta == 0)
        return 1.0;

    const qreal wheelSteps = useFractionalSteps ? static_cast<qreal>(wheelDelta) / 120.0 : (wheelDelta > 0 ? 1.0 : -1.0);
    return qPow(zoomMultiplier, wheelSteps);
}

qreal QVGraphicsView::nativeGestureZoomFactor(const qreal value)
{
    return qMax(0.01, 1.0 + value);
}

qreal QVGraphicsView::boundedZoomLevel(const qreal requestedLevel)
{
    if (std::isnan(requestedLevel))
        return Qv::MinimumZoomLevel;
    return std::clamp(requestedLevel, Qv::MinimumZoomLevel,
                      Qv::MaximumZoomLevel);
}

bool QVGraphicsView::usesVectorRendering() const
{
    return loadedPixmapItem && loadedPixmapItem->hasVectorImage();
}

Qv::VectorImageFormat QVGraphicsView::vectorImageFormat() const
{
    return loadedPixmapItem ? loadedPixmapItem->vectorImageFormat()
                            : Qv::VectorImageFormat::None;
}

QSize QVGraphicsView::lastVectorRasterSize() const
{
    return loadedPixmapItem ? loadedPixmapItem->lastVectorRasterSize() : QSize();
}

quint64 QVGraphicsView::vectorRenderCount() const
{
    return loadedPixmapItem ? loadedPixmapItem->vectorRenderCount() : 0;
}

bool QVGraphicsView::hasPendingVectorRefinement() const
{
    return loadedPixmapItem
            && loadedPixmapItem->hasPendingVectorRefinement();
}

QPointF QVGraphicsView::nativeGesturePanScrollDelta(const QPointF &delta, const bool isRightToLeft)
{
    return QPointF(-delta.x() * (isRightToLeft ? -1.0 : 1.0), -delta.y());
}

QString QVGraphicsView::scrollBarStyleSheet(const Qv::Theme theme)
{
    const bool isDark = theme == Qv::Theme::Dark;
    const QString trackColor = isDark ? QStringLiteral("#2f2f2f") : QStringLiteral("#ededed");
    const QString handleColor = isDark ? QStringLiteral("#6b6b6b") : QStringLiteral("#b0b0b0");
    const QString handleHoverColor = isDark ? QStringLiteral("#858585") : QStringLiteral("#8f8f8f");

    return QStringLiteral(
        "QScrollBar:vertical, QScrollBar:horizontal { border: none; background: %1; margin: 0px; }"
        // Do not set a fixed thickness here.  QGraphicsView calculates its
        // viewport and scrollbar ranges from the platform style's
        // PM_ScrollBarExtent.  A stylesheet thickness would be applied by
        // QScrollBar while the view continues to use that native metric,
        // leaving a stale strip in the range whenever the two differ.
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: %2; border: none; border-radius: 5px; }"
        "QScrollBar::handle:vertical { min-height: 24px; margin: 0px 1px; }"
        "QScrollBar::handle:horizontal { min-width: 24px; margin: 1px 0px; }"
        "QScrollBar::handle:hover, QScrollBar::handle:pressed { background: %3; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical, QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: %1; }"
        "QScrollBar::add-line, QScrollBar::sub-line { background: transparent; border: none; width: 0px; height: 0px; }")
        .arg(trackColor, handleColor, handleHoverColor);
}

bool QVGraphicsView::zoomLevelsEquivalent(const qreal lhs, const qreal rhs)
{
    const qreal tolerance = qMax(1.0, qMax(qAbs(lhs), qAbs(rhs))) * 1e-9;
    return qAbs(lhs - rhs) <= tolerance;
}

bool QVGraphicsView::shouldDisplaySmallImageAtOneToOne(
    const QSizeF &imageSize,
    const QSize &viewportSize,
    const bool settingEnabled,
    const Qv::WindowResizeMode windowResizeMode)
{
    return settingEnabled &&
        windowResizeMode == Qv::WindowResizeMode::Never &&
        !imageSize.isEmpty() &&
        !viewportSize.isEmpty() &&
        imageSize.width() < viewportSize.width() &&
        imageSize.height() < viewportSize.height();
}

bool QVGraphicsView::hdrViewportGeometryEquivalent(
    const QSize &lhsViewportSize,
    const QPolygonF &lhsImageCorners,
    const QSize &rhsViewportSize,
    const QPolygonF &rhsImageCorners,
    const qreal tolerance)
{
    if (lhsViewportSize != rhsViewportSize
        || lhsImageCorners.size() != rhsImageCorners.size())
        return false;

    const qreal safeTolerance = qMax(0.0, tolerance);
    for (qsizetype index = 0; index < lhsImageCorners.size(); ++index) {
        const QPointF delta = lhsImageCorners.at(index) - rhsImageCorners.at(index);
        if (qAbs(delta.x()) > safeTolerance || qAbs(delta.y()) > safeTolerance)
            return false;
    }
    return true;
}

bool QVGraphicsView::canReuseHDRPresentation(const bool firstFramePresented,
                                             const bool hdrPrepared)
{
    return firstFramePresented && hdrPrepared;
}

std::optional<qreal> QVGraphicsView::sampleDisplayedImageBrightness(
    const QPoint &viewportPoint) const
{
    const QRectF pixmapRect = loadedPixmapItem ? loadedPixmapItem->boundingRect() : QRectF();
    if (navigationSamplingImage.isNull() || pixmapRect.isEmpty()
        || !viewport()->rect().contains(viewportPoint))
        return {};

    // Scene coordinates are device-independent. On a Retina host the sampling
    // preview can therefore have twice as many physical pixels as scene units.
    // Map through the actual item instead of dividing by the preview size.
    const QPointF itemPoint = loadedPixmapItem->mapFromScene(mapToScene(viewportPoint));
    if (!pixmapRect.contains(itemPoint))
        return {};

    const int centerX = qBound(
        0,
        qFloor((itemPoint.x() - pixmapRect.left()) * navigationSamplingImage.width()
               / pixmapRect.width()),
        navigationSamplingImage.width() - 1);
    const int centerY = qBound(
        0,
        qFloor((itemPoint.y() - pixmapRect.top()) * navigationSamplingImage.height()
               / pixmapRect.height()),
        navigationSamplingImage.height() - 1);
    qreal total = 0.0;
    int count = 0;
    for (int y = qMax(0, centerY - 1);
         y <= qMin(navigationSamplingImage.height() - 1, centerY + 1); ++y) {
        for (int x = qMax(0, centerX - 1);
             x <= qMin(navigationSamplingImage.width() - 1, centerX + 1); ++x) {
            total += Qv::getPerceivedBrightness(navigationSamplingImage.pixelColor(x, y));
            ++count;
        }
    }
    return count > 0 ? std::optional<qreal>(total / count) : std::nullopt;
}

bool QVGraphicsView::usesNativeHDRNavigationOverlay() const
{
    return hdrRendererActive && hdrRenderer && hdrRenderer->isAvailable();
}

bool QVGraphicsView::usesNativeSDRMetalRenderer() const
{
    return hdrRendererActive && getCurrentFileDetails().isNativeSDRLoaded
            && hdrRenderer && hdrRenderer->diagnostics().sdrImageActive;
}

QVCocoaFunctions::HDRRendererDiagnostics
QVGraphicsView::nativeMetalRendererDiagnostics() const
{
    return hdrRenderer ? hdrRenderer->diagnostics()
                       : QVCocoaFunctions::HDRRendererDiagnostics{};
}

void QVGraphicsView::setHDRPresentationActive(const bool active)
{
    if (hdrPresentationActive == active)
        return;
    hdrPresentationActive = active;
    const quint64 requestGeneration = ++hdrPresentationRequestGeneration;
    if (!hdrRendererActive || !hdrRenderer)
        return;

    // The SDR proxy must be committed behind the native HDR container before
    // a fade-out begins. Re-enable viewport updates now, then begin that fade
    // on the next display interval so there is no empty intermediate frame.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    loadedPixmapItem->setVisible(true);
    viewport()->update();
    hdrActivationCompleted = false;
    hdrPresentationTimer->start();
    QTimer::singleShot(active ? 0 : 16, this,
                       [this, active, requestGeneration]() {
        if (requestGeneration != hdrPresentationRequestGeneration
            || !hdrRendererActive || !hdrRenderer)
            return;
        hdrRenderer->setPresentationActive(active, true);
        logHDRState(active ? "window-activated" : "window-deactivated");
    });
}

void QVGraphicsView::setHDRNavigationOverlay(
        const int index, const QRectF &viewportRect, const qreal opacity,
        const bool previous, const bool darkBackground, const bool hovered,
        const bool pressed, const bool enabled)
{
    if (hdrRenderer)
        hdrRenderer->setNavigationOverlay(index, viewportRect, opacity, previous,
                                          darkBackground, hovered, pressed, enabled);
}

void QVGraphicsView::clearHDRNavigationOverlays()
{
    if (hdrRenderer)
        hdrRenderer->clearNavigationOverlays();
}

void QVGraphicsView::executeScrollAction(const Qv::ViewportScrollAction action, const QPoint delta, const QPoint mousePos, const bool hasShiftModifier, const bool useFractionalZoom)
{
    const int deltaPerWheelStep = 120;
    const int rtlFlip = getRtlFlip();

    const auto getUniAxisDelta = [delta, rtlFlip]() {
        return
            delta.x() != 0 && delta.y() == 0 ? delta.x() * rtlFlip :
            delta.x() == 0 && delta.y() != 0 ? delta.y() :
            0;
    };

    if (action == Qv::ViewportScrollAction::Pan)
    {
        const qreal scrollDivisor = 2.0; // To make scrolling less sensitive
        qreal scrollX = -delta.x() * rtlFlip / scrollDivisor;
        qreal scrollY = -delta.y() / scrollDivisor;

        if (hasShiftModifier)
            std::swap(scrollX, scrollY);

        cancelPendingZoomAnchor();
        scrollHelper->move(QPointF(scrollX, scrollY));
        constrainBoundsTimer->start();
    }
    else if (action == Qv::ViewportScrollAction::Zoom)
    {
        if (!getCurrentFileDetails().isPixmapLoaded)
            return;

        const qreal zoomFactor = wheelZoomFactor(getUniAxisDelta(), zoomMultiplier, useFractionalZoom);

        if (isCursorVisible)
            setCursorVisible(true);

        zoomRelative(zoomFactor, mousePos);
    }
    else if (action == Qv::ViewportScrollAction::Navigate)
    {
        SwipeData swipeData = scrollAxisLocker.getCustomData().value<SwipeData>();
        if (swipeData.triggeredAction && scrollActionCooldown)
            return;
        swipeData.totalDelta += getUniAxisDelta();
        if (qAbs(swipeData.totalDelta) >= deltaPerWheelStep)
        {
            if (swipeData.totalDelta < 0)
                goToFile(Qv::GoToFileMode::Next);
            else
                goToFile(Qv::GoToFileMode::Previous);
            swipeData.triggeredAction = true;
            swipeData.totalDelta %= deltaPerWheelStep;
        }
        scrollAxisLocker.setCustomData(QVariant::fromValue(swipeData));
    }
}

QMimeData *QVGraphicsView::getMimeData() const
{
    auto *mimeData = new QMimeData();
    if (!getCurrentFileDetails().isPixmapLoaded)
        return mimeData;

    mimeData->setUrls({QUrl::fromLocalFile(imageCore.getCurrentFileDetails().fileInfo.absoluteFilePath())});
    mimeData->setImageData(imageCore.getLoadedPixmap().toImage());
    return mimeData;
}

void QVGraphicsView::loadMimeData(const QMimeData *mimeData)
{
    if (mimeData == nullptr)
        return;

    if (!mimeData->hasUrls())
        return;

    const QList<QUrl> urlList = mimeData->urls();

    bool first = true;
    for (const auto &url : urlList)
    {
        if (first)
        {
            loadFile(url.toString());
            emit cancelSlideshow();
            first = false;
            continue;
        }
        QVApplication::openFile(url.toString());
    }
}

void QVGraphicsView::loadFile(const QString &fileName, const QString &baseDir)
{
    imageCore.loadFile(fileName, false, baseDir);
}

void QVGraphicsView::reloadFile()
{
    imageCore.markFolderInfoDirty();

    if (getCurrentFileDetails().isPixmapLoaded)
        imageCore.loadFile(getCurrentFileDetails().fileInfo.absoluteFilePath(), true);
}

void QVGraphicsView::shutdownAsyncWork()
{
    imageCore.shutdownAsyncWork();
    if (loadedPixmapItem)
        loadedPixmapItem->shutdownAsyncWork();
}

void QVGraphicsView::beforeLoad()
{
    cancelPendingZoomAnchor();

    // A native HDR presentation may have parked Qt viewport painting.  The
    // SDR proxy for the next file must be paintable while its renderer is
    // decoded and prepared.
    setViewportUpdateMode(QGraphicsView::MinimalViewportUpdate);
    hdrLayoutReady = false;
    hdrActivationCompleted = false;
    hdrPendingGeometryValid = false;
    hdrGeometryTimer->stop();
    hdrFrameRequestTimer->stop();
    hdrInteractionClock.invalidate();
    hdrScrollInteractionClock.invalidate();
    hdrInteractionZoomMilliseconds = 0.0;
    hdrInteractionStep = -1;
    navigationSamplingImage = {};
    navigationSamplingSourceSize = {};
    vectorRefineTimer->stop();
    setVectorInteractionPresentation(false);
    lastCalculatedZoomMode.reset();
    lastCalculatedZoomLevel.reset();

    // If a prior pixmap is still loaded, capture its content rect
    if (getCurrentFileDetails().isPixmapLoaded)
        lastImageContentRect = getContentRect();
}

void QVGraphicsView::ensureHDRRenderer()
{
    if (hdrRenderer)
        return;

    // The native layer tree and CAMetalDisplayLink are only useful for a
    // loaded image. Constructing them while an empty window is being created
    // promotes the Cocoa view hierarchy and adds needless launch latency.
    hdrRenderer = std::make_unique<QVCocoaFunctions::HDRRenderer>(viewport());
    hdrRenderer->setBackgroundColor(viewportBackgroundBrush.color());
    hdrRenderer->setCheckerboardBackground(checkerboardBackground);
}

void QVGraphicsView::postLoad()
{
    hdrLayoutReady = false;
    hdrActivationCompleted = false;
    hdrPendingGeometryValid = false;
    hdrGeometryTimer->stop();
    // Set the pixmap to the new image and reset the transform's scale to a known value
    removeExpensiveScaling();
    if (imageCore.getLoadedHDRImage() || imageCore.getLoadedSDRImage())
        ensureHDRRenderer();

    if (imageCore.getLoadedHDRImage())
        hdrRendererActive = hdrRenderer
                && hdrRenderer->setImage(imageCore.getLoadedHDRImage());
    else
        hdrRendererActive = hdrRenderer
                && hdrRenderer->setSDRImage(imageCore.getLoadedSDRImage());
    updateViewportOpacityContract();
    // Keep the bounded SDR proxy visible until a correctly sized Metal frame
    // has actually reached the display. It is a seamless placeholder, not the
    // authoritative native SDR/HDR representation.
    loadedPixmapItem->setVisible(true);
    loadedPixmapItem->setTransform(QTransform());
    const QSize fallbackSize = imageCore.getLoadedPixmap().size();
    const QSize sourceSize = getCurrentFileDetails().loadedPixmapSize;
    navigationSamplingSourceSize = sourceSize.isEmpty() ? fallbackSize : sourceSize;
    navigationSamplingImage = imageCore.getLoadedPixmap().toImage();
    if (!navigationSamplingImage.isNull()
        && qMax(navigationSamplingImage.width(), navigationSamplingImage.height()) > 384) {
        navigationSamplingImage = navigationSamplingImage.scaled(
            384, 384, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if ((getCurrentFileDetails().isNativeHDRLoaded
         || getCurrentFileDetails().isNativeSDRLoaded)
        && !fallbackSize.isEmpty()
        && !sourceSize.isEmpty()) {
        loadedPixmapItem->setTransform(QTransform::fromScale(
                static_cast<qreal>(sourceSize.width()) / fallbackSize.width(),
                static_cast<qreal>(sourceSize.height()) / fallbackSize.height()));
    }
    hdrPresentationActive = window()->isActiveWindow();
    if (hdrRendererActive) {
        hdrRenderer->setPresentationActive(hdrPresentationActive, true);
        hdrPresentationTimer->start();
    } else {
        hdrPresentationTimer->stop();
        hdrGeometryTimer->stop();
        if (hdrRenderer)
            hdrRenderer->clear();
    }
    updateSceneRect();
    logViewportState("post-load-before-layout");

    // If we have a content rect for the prior pixmap, scroll the new pixmap to align their centers
    if (lastImageContentRect.isValid())
        matchContentCenter(lastImageContentRect);

    const auto &fileDetails = getCurrentFileDetails();
    if (!fileDetails.fileInfo.filePath().isEmpty() && !fileDetails.errorData.has_value())
        qvApp->getActionManager().addFileToRecentsList(fileDetails.fileInfo);

    emit fileChanged(loadIsFromSessionRestore);

    if (!loadIsFromSessionRestore)
    {
        if (navigationResetsZoom && calculatedZoomMode != defaultCalculatedZoomMode)
            setCalculatedZoomMode(defaultCalculatedZoomMode, true);
        else
            fitOrConstrainImage();
    }
    logViewportState("post-load-after-fit");
    // updateSceneRect, scrollbar policy and full-size titlebar layout can all
    // settle on later event-loop turns. Record the complete geometry now and
    // arm Metal only after it remains unchanged for two display intervals.
    requestHDRRendererUpdate();
    QTimer::singleShot(0, this, [this]() { logViewportState("post-load-next-turn"); });
    loadIsFromSessionRestore = false;

    expensiveScaleTimer->start();

    // Deterministic, opt-in system-test driver. It exercises the same public
    // zoom and scrollbar paths as user interaction without synthetic global
    // input or changes to persisted settings.
    const bool run120HzInteractionProbe =
            qEnvironmentVariableIsSet("FOVELLE_HDR_TEST_120HZ_INTERACTION");
    if (hdrRendererActive && !hdrInteractionTestScheduled
        && (qEnvironmentVariableIsSet("FOVELLE_HDR_TEST_INTERACTION")
            || run120HzInteractionProbe)) {
        hdrInteractionTestScheduled = true;
        QTimer::singleShot(2400, this, [this, run120HzInteractionProbe]() {
            if (!hdrRendererActive)
                return;
            hdrInteractionClock.start();
            QElapsedTimer zoomTimer;
            zoomTimer.start();
            zoomRelative(4.0, Qv::CalculateViewportCenterPos);
            fitOrConstrainImage();
            hdrInteractionZoomMilliseconds = zoomTimer.nsecsElapsed() / 1000000.0;
            hdrInteractionStep = 0;
            logHDRState("test-interaction-start");
            const auto probeStartDiagnostics = hdrRenderer->diagnostics();

            auto *panTimer = new QTimer(this);
            panTimer->setTimerType(Qt::PreciseTimer);
            panTimer->setInterval(run120HzInteractionProbe ? 8 : 12);
            panTimer->setProperty("step", 0);
            connect(panTimer, &QTimer::timeout, this,
                    [this, panTimer, run120HzInteractionProbe,
                     probeStartDiagnostics]() {
                const int step = panTimer->property("step").toInt();
                horizontalScrollBar()->setValue(horizontalScrollBar()->value() + 11);
                verticalScrollBar()->setValue(verticalScrollBar()->value() + 7);
                hdrInteractionStep = step;
                logHDRState("test-interaction-step");
                if (step >= 47) {
                    panTimer->stop();
                    panTimer->deleteLater();
                    hdrInteractionStep = 48;
                    requestHDRRendererUpdate();
                    logHDRState("test-interaction-finished");
                    const qint64 interactionElapsed = hdrInteractionClock.elapsed();
                    if (run120HzInteractionProbe) {
                        // The verbose diagnostic stream perturbs an 8 ms test
                        // interval. Emit one compact record after the final
                        // coalesced geometry request instead, so the probe can
                        // run with FOVELLE_HDR_DIAGNOSTIC_LOG unset.
                        QTimer::singleShot(50, this,
                                [this, interactionElapsed,
                                 probeStartDiagnostics]() {
                            if (!hdrRendererActive || !hdrRenderer)
                                return;
                            const auto finalDiagnostics =
                                    hdrRenderer->diagnostics();
                            const double updatesPerSecond = interactionElapsed > 0
                                    ? 48000.0 / interactionElapsed : 0.0;
                            const QJsonObject probe{
                                { QStringLiteral("sample_count"), 48 },
                                { QStringLiteral("elapsed_ms"),
                                  interactionElapsed },
                                { QStringLiteral("updates_per_second"),
                                  updatesPerSecond },
                                { QStringLiteral("persistent_surface_ready"),
                                  finalDiagnostics.persistentHDRSurfaceReady },
                                { QStringLiteral("compositor_updates"),
                                  static_cast<qint64>(
                                          finalDiagnostics.compositorGeometryUpdateCount
                                          - probeStartDiagnostics
                                                    .compositorGeometryUpdateCount) },
                                { QStringLiteral("presentation_updates"),
                                  static_cast<qint64>(
                                          finalDiagnostics.presentedFrameCount
                                          - probeStartDiagnostics
                                                    .presentedFrameCount) },
                                { QStringLiteral("last_geometry_update_ms"),
                                  finalDiagnostics.lastRenderMilliseconds },
                            };
                            qInfo().noquote()
                                    << "FOVELLE_HDR_120HZ"
                                    << QJsonDocument(probe).toJson(
                                               QJsonDocument::Compact);
                        });
                    }
                    QTimer::singleShot(250, this, [this]() {
                        if (hdrRendererActive)
                            logHDRState("test-interaction-settled");
                    });
                } else {
                    panTimer->setProperty("step", step + 1);
                }
            });
            panTimer->start();
        });
    }

    // Opt-in deterministic theme driver for system tests. It does not mutate
    // QSettings: both calls use the production renderer update path while the
    // opened image and its completed HDR activation remain intact.
    if (hdrRendererActive && !hdrThemeTestScheduled
        && qEnvironmentVariableIsSet("FOVELLE_HDR_TEST_THEME_SWITCH")) {
        hdrThemeTestScheduled = true;
        applyHDRViewportBackground(Qv::Theme::Light);
        QTimer::singleShot(3000, this, [this]() {
            if (!hdrRendererActive)
                return;
            applyHDRViewportBackground(Qv::Theme::Dark);
            logHDRState("test-theme-dark");
        });
    }

    // Opt-in focus-transition probe. It drives the same production state
    // method as WindowDeactivate/ApplicationStateChange while leaving the
    // window onscreen, so presentation-layer opacity can be sampled at 16 ms.
    if (hdrRendererActive && !hdrFocusTransitionTestScheduled
        && qEnvironmentVariableIsSet("FOVELLE_HDR_TEST_FOCUS_TRANSITION")) {
        hdrFocusTransitionTestScheduled = true;
        QTimer::singleShot(1400, this,
                           [this]() { setHDRPresentationActive(false); });
        QTimer::singleShot(2200, this,
                           [this]() { setHDRPresentationActive(true); });
    }

    if (turboNavMode.has_value())
    {
        const qint64 navDelay = qMax(turboNavInterval - lastTurboNav.elapsed(), 0LL);
        QTimer::singleShot(navDelay, this, [this]() {
            if (!turboNavMode.has_value())
                return;
            if (lastTurboNavKeyPress.elapsed() >= qMax(qvApp->keyboardAutoRepeatInterval() * 1.5, 250.0))
            {
                // Backup mechanism in case we somehow stop receiving key presses and aren't
                // notified of it in some other way (e.g. key release, lost focus), as can happen
                // in macOS if the menu bar gets clicked on while navigation is in progress.
                cancelTurboNav();
                return;
            }
            lastTurboNav.start();
            goToFile(turboNavMode.value());
        });
    }
}

void QVGraphicsView::zoomIn()
{
    zoomRelative(zoomMultiplier, Qv::CalculateViewportCenterPos);
    fitOrConstrainImage();
}

void QVGraphicsView::zoomOut()
{
    zoomRelative(qPow(zoomMultiplier, -1), Qv::CalculateViewportCenterPos);
    fitOrConstrainImage();
}

void QVGraphicsView::zoomRelative(const qreal relativeLevel, const std::optional<QPoint> &mousePos)
{
    const qreal absoluteLevel = boundedZoomLevel(zoomLevel * relativeLevel);
    const std::optional<QPoint> pos = !mousePos.has_value() ? std::nullopt : zoomToCursor && isCursorVisible ? mousePos : Qv::CalculateViewportCenterPos;
    zoomAbsolute(absoluteLevel, pos);
}

void QVGraphicsView::zoomAbsolute(const qreal absoluteLevel, const std::optional<QPoint> &targetPos, const bool isApplyingCalculation)
{
    const qreal requestedLevel = boundedZoomLevel(absoluteLevel);
    const bool keepsCalculatedZoomMode =
        isApplyingCalculation &&
        calculatedZoomMode.has_value() &&
        Qv::calculatedZoomModeIsSticky(calculatedZoomMode.value());
    const bool shouldRestoreCalculatedZoom =
        !isApplyingCalculation &&
        !calculatedZoomMode.has_value() &&
        lastCalculatedZoomMode.has_value() &&
        lastCalculatedZoomLevel.has_value() &&
        zoomLevelsEquivalent(requestedLevel, lastCalculatedZoomLevel.value());
    if (!keepsCalculatedZoomMode && calculatedZoomMode.has_value())
    {
        calculatedZoomMode.reset();
        emit calculatedZoomModeChanged();
    }

    const bool isChanging = requestedLevel != zoomLevel;
    if (isChanging && !isApplyingCalculation
        && getCurrentFileDetails().isVectorLoaded)
    {
        setVectorInteractionPresentation(true);
    }
    const std::optional<QPoint> pos = targetPos == Qv::CalculateViewportCenterPos ? getUsableViewportRect().center() : targetPos;
    if (pos != lastZoomEventPos)
    {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = pos.has_value() ? mapToScene(pos.value()) - lastZoomRoundingError : QPointF();

    if (isChanging && pos.has_value())
    {
        pendingZoomAnchorScene = scenePos;
        pendingZoomAnchorViewport = pos;
        pendingZoomAnchorFollowsViewportCenter = targetPos == Qv::CalculateViewportCenterPos;
        const quint64 anchorGeneration = ++pendingZoomAnchorGeneration;
        QTimer::singleShot(150, this, [this, anchorGeneration]() {
            if (anchorGeneration != pendingZoomAnchorGeneration)
                return;
            restorePendingZoomAnchor();
            pendingZoomAnchorScene.reset();
            pendingZoomAnchorViewport.reset();
            pendingZoomAnchorFollowsViewportCenter = false;
        });
    }
    else if (isChanging)
    {
        cancelPendingZoomAnchor();
    }

    if (appliedExpensiveScaleZoomLevel != 0.0)
    {
        const qreal baseTransformScale = 1.0 / devicePixelRatioF();
        const qreal relativeLevel = requestedLevel / appliedExpensiveScaleZoomLevel;
        setTransformScale(baseTransformScale * relativeLevel);
    }
    else
    {
        setTransformScale(requestedLevel * appliedDpiAdjustment);
    }
    zoomLevel = requestedLevel;
    updateSceneRect();

    if (shouldRestoreCalculatedZoom)
    {
        calculatedZoomMode = lastCalculatedZoomMode;
        emit calculatedZoomModeChanged();
    }

    if (pos.has_value())
    {
        const QPointF move = mapFromScene(scenePos) - pos.value();
        {
            // These scrollbar writes are part of the zoom transaction, not a
            // new user pan. Keep the delayed anchor alive until layout has
            // settled, while restorePendingZoomAnchor() itself remains
            // protected by the same internal-update guard.
            QScopedValueRollback<bool> internalUpdateGuard(
                    fullScreenPanInternalUpdate, true);
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (move.x() * getRtlFlip()));
            verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
            restorePendingZoomAnchor();
        }
        lastZoomRoundingError = mapToScene(pos.value()) - scenePos;
        constrainBoundsTimer->start();
    }
    else if (!loadIsFromSessionRestore)
    {
        centerImage();
    }

    if (isChanging)
    {
        handleSmoothScalingChange();

        emit zoomLevelChanged();
    }
}

const std::optional<Qv::CalculatedZoomMode> &QVGraphicsView::getCalculatedZoomMode() const
{
    return calculatedZoomMode;
}

void QVGraphicsView::setCalculatedZoomMode(const std::optional<Qv::CalculatedZoomMode> &value, const bool isNavigating, const std::optional<QPoint> &mousePos)
{
    if (!value.has_value() || value == Qv::CalculatedZoomMode::OriginalSize)
    {
        lastCalculatedZoomMode.reset();
        lastCalculatedZoomLevel.reset();
    }

    if (calculatedZoomMode == value)
    {
        if (calculatedZoomMode.has_value())
            centerImage();
        return;
    }

    if (value == Qv::CalculatedZoomMode::OriginalSize && zoomLevel == 1 && !isNavigating && qvApp->getSettingsManager().getBoolean("originalsizeastoggle"))
    {
        setCalculatedZoomMode(defaultCalculatedZoomMode != Qv::CalculatedZoomMode::OriginalSize ? defaultCalculatedZoomMode : Qv::CalculatedZoomMode::ZoomToFit);
        return;
    }

    calculatedZoomMode = value;
    if (calculatedZoomMode == Qv::CalculatedZoomMode::OriginalSize && zoomToCursor && mousePos.has_value())
    {
        zoomAbsolute(1.0, mousePos, true);
        fitOrConstrainImage();
    }
    else if (calculatedZoomMode.has_value())
    {
        recalculateZoom();
    }

    emit calculatedZoomModeChanged();
}

void QVGraphicsView::setNavigationResetsZoom(const bool value)
{
    if (navigationResetsZoom == value)
        return;

    navigationResetsZoom = value;

    emit navigationResetsZoomChanged();
}

void QVGraphicsView::applyExpensiveScaling()
{
    if (!isExpensiveScalingRequested())
        return;

    // Calculate scaled resolution
    const QPoint scrollPosition(horizontalScrollBar()->value(), verticalScrollBar()->value());
    const qreal dpiAdjustment = getDpiAdjustment();
    const QSizeF mappedSize = QSizeF(getCurrentFileDetails().loadedPixmapSize) * zoomLevel * dpiAdjustment * devicePixelRatioF();

    // Set image to scaled version
    loadedPixmapItem->setPixmap(imageCore.scaleExpensively(mappedSize));

    // Set appropriate scale factor
    const qreal newTransformScale = 1.0 / devicePixelRatioF();
    setTransformScale(newTransformScale);
    appliedDpiAdjustment = dpiAdjustment;
    appliedExpensiveScaleZoomLevel = zoomLevel;
    updateSceneRect(scrollPosition);
    logViewportState("expensive-scaling-applied");
}

void QVGraphicsView::removeExpensiveScaling()
{
    const bool wasExpensiveScalingApplied = appliedExpensiveScaleZoomLevel != 0.0;
    const QPoint scrollPosition(horizontalScrollBar()->value(), verticalScrollBar()->value());

    // Return to original size
    loadedPixmapItem->setPixmap(imageCore.getLoadedPixmap());
    if (getCurrentFileDetails().isVectorLoaded
        && !loadedPixmapItem->setVectorImage(imageCore.getLoadedVectorImage()))
    {
        qWarning() << "The validated vector document could not be attached to the scene";
    }

    // Set appropriate scale factor
    const qreal dpiAdjustment = getDpiAdjustment();
    const qreal newTransformScale = zoomLevel * dpiAdjustment;
    setTransformScale(newTransformScale);
    appliedDpiAdjustment = dpiAdjustment;
    appliedExpensiveScaleZoomLevel = 0.0;
    if (wasExpensiveScalingApplied)
    {
        updateSceneRect(scrollPosition);
        logViewportState("expensive-scaling-removed");
    }
}

void QVGraphicsView::animatedFrameChanged(QRect rect)
{
    Q_UNUSED(rect)

    if (isExpensiveScalingRequested())
    {
        applyExpensiveScaling();
    }
    else
    {
        loadedPixmapItem->setPixmap(imageCore.getLoadedPixmap());
        updateSceneRect();
    }
}

void QVGraphicsView::recalculateZoom()
{
    if (!getCurrentFileDetails().isPixmapLoaded || !calculatedZoomMode.has_value())
        return;

    const QSizeF imageSize = getEffectiveOriginalSize();
    const QSize viewSize = getUsableViewportRect(true).size();
    const QSize usableViewportSize = getUsableViewportRect().size();

    if (viewSize.isEmpty())
        return;

    const LogicalPixelFitter fitter = getPixelFitter();
    const qreal fitXRatio = fitter.unsnapWidth(viewSize.width()) / imageSize.width();
    const qreal fitYRatio = fitter.unsnapHeight(viewSize.height()) / imageSize.height();

    qreal targetRatio;
    const auto windowResizeMode = qvApp->getSettingsManager().getEnum<Qv::WindowResizeMode>("windowresizemode");
    const bool keepSmallImageAtOneToOne =
        calculatedZoomMode.has_value() &&
        calculatedZoomMode.value() != Qv::CalculatedZoomMode::OriginalSize &&
        shouldDisplaySmallImageAtOneToOne(imageSize, usableViewportSize, showSmallImagesAtOneToOne, windowResizeMode);

    // Each mode will check if the rounded image size already produces the desired fit,
    // in which case we can use exactly 1.0 to avoid unnecessary scaling
    const int imageOverflowX = fitter.snapWidth(imageSize.width()) - viewSize.width();
    const int imageOverflowY = fitter.snapHeight(imageSize.height()) - viewSize.height();

    if (keepSmallImageAtOneToOne)
    {
        targetRatio = 1.0;
    }
    else switch (calculatedZoomMode.value()) {
    case Qv::CalculatedZoomMode::ZoomToFit:
        // In rare cases, if the window sizing code just barely increased the size to enforce
        // the minimum and intends for a tiny upscale to occur (e.g. to 100.3%), that could get
        // misdetected as the special case for 1.0 here and leave an unintentional 1 pixel
        // border. So if we match on only one dimension, make sure the other dimension will have
        // at least a few pixels of border showing.
        if ((imageOverflowX == 0 && (imageOverflowY == 0 || imageOverflowY <= -2)) ||
            (imageOverflowY == 0 && (imageOverflowX == 0 || imageOverflowX <= -2)))
        {
            targetRatio = 1.0;
        }
        else
        {
            // If the fit ratios are extremely close, it's possible that both are sufficient to
            // contain the image, but one results in the opposing dimension getting rounded down
            // to just under the view size, so use the larger of the two ratios in that case.
            const bool isOverallFitToXRatio = fitter.snapHeight(imageSize.height() * fitXRatio) == viewSize.height();
            const bool isOverallFitToYRatio = fitter.snapWidth(imageSize.width() * fitYRatio) == viewSize.width();
            if (isOverallFitToXRatio || isOverallFitToYRatio)
                targetRatio = qMax(fitXRatio, fitYRatio);
            else
                targetRatio = qMin(fitXRatio, fitYRatio);
        }
        break;
    case Qv::CalculatedZoomMode::FillWindow:
        if ((imageOverflowX == 0 && imageOverflowY >= 0) ||
            (imageOverflowY == 0 && imageOverflowX >= 0))
        {
            targetRatio = 1.0;
        }
        else
        {
            targetRatio = qMax(fitXRatio, fitYRatio);
        }
        break;
    default:
        targetRatio = 1.0;
        break;
    }

    if (fitZoomLimit.has_value() && targetRatio > fitZoomLimit.value())
        targetRatio = fitZoomLimit.value();

    targetRatio = boundedZoomLevel(targetRatio);
    lastCalculatedZoomMode = calculatedZoomMode;
    lastCalculatedZoomLevel = targetRatio;

    zoomAbsolute(targetRatio, {}, true);
}

void QVGraphicsView::centerImage()
{
    logViewportState("center-before");
    const QRect viewRect = getUsableViewportRect();
    const QRect contentRect = getContentRect();
    const int hOffset = isRightToLeft() ?
        horizontalScrollBar()->minimum() + horizontalScrollBar()->maximum() - contentRect.left() :
        contentRect.left();
    const int vOffset = contentRect.top() - viewRect.top();
    const int hOverflow = contentRect.width() - viewRect.width();
    const int vOverflow = contentRect.height() - viewRect.height();

    horizontalScrollBar()->setValue(hOffset + (hOverflow / (isRightToLeft() ? -2 : 2)));
    verticalScrollBar()->setValue(vOffset + (vOverflow / 2));

    logViewportState("center-after");
}

void QVGraphicsView::setCursorVisible(const bool visible)
{
    const bool autoHideCursor = isCursorAutoHideFullscreenEnabled && window()->isFullScreen();
    if (visible)
    {
        if (autoHideCursor && pressedMouseButton == Qt::NoButton)
            hideCursorTimer->start();
        else
            hideCursorTimer->stop();

        if (isCursorVisible) return;

        window()->setCursor(Qt::ArrowCursor);
        viewport()->setCursor(Qt::ArrowCursor);
        isCursorVisible = true;
    }
    else
    {
        if (!isCursorVisible) return;

        window()->setCursor(Qt::BlankCursor);
        viewport()->setCursor(Qt::BlankCursor);
        isCursorVisible = false;
    }
}

const QJsonObject QVGraphicsView::getSessionState() const
{
    QJsonObject state;

    const QTransform transform = getUnspecializedTransform();
    const QJsonArray transformValues {
        static_cast<int>(transform.m11()),
        static_cast<int>(transform.m22()),
        static_cast<int>(transform.m21()),
        static_cast<int>(transform.m12())
    };
    state["transform"] = transformValues;

    state["zoomLevel"] = zoomLevel;

    state["hScroll"] = horizontalScrollBar()->value();
    state["vScroll"] = verticalScrollBar()->value();

    state["navResetsZoom"] = navigationResetsZoom;

    if (calculatedZoomMode.has_value())
        state["calcZoomMode"] = static_cast<int>(calculatedZoomMode.value());

    state["sortMode"] = static_cast<int>(getSortMode());
    state["sortDescending"] = getSortDescending();

    return state;
}

void QVGraphicsView::loadSessionState(const QJsonObject &state)
{
    lastCalculatedZoomMode.reset();
    lastCalculatedZoomLevel.reset();

    const QJsonArray transformValues = state["transform"].toArray();
    const QTransform transform {
        static_cast<double>(transformValues.at(0).toInt()),
        static_cast<double>(transformValues.at(3).toInt()),
        static_cast<double>(transformValues.at(2).toInt()),
        static_cast<double>(transformValues.at(1).toInt()),
        0,
        0
    };
    setTransform(transform);

    zoomAbsolute(state["zoomLevel"].toDouble());

    horizontalScrollBar()->setValue(state["hScroll"].toInt());
    verticalScrollBar()->setValue(state["vScroll"].toInt());

    setNavigationResetsZoom(state["navResetsZoom"].toBool());

    calculatedZoomMode = state.contains("calcZoomMode") ? std::optional(static_cast<Qv::CalculatedZoomMode>(state["calcZoomMode"].toInt())) : std::nullopt;
    if (calculatedZoomMode.has_value() && Qv::calculatedZoomModeIsSticky(calculatedZoomMode.value()))
    {
        lastCalculatedZoomMode = calculatedZoomMode;
        lastCalculatedZoomLevel = zoomLevel;
    }
    emit calculatedZoomModeChanged();

    if (state.contains("sortMode") && state.contains("sortDescending"))
    {
        imageCore.setSortMode(static_cast<Qv::SortMode>(state["sortMode"].toInt()));
        imageCore.setSortDescending(state["sortDescending"].toBool());
    }
}

void QVGraphicsView::setLoadIsFromSessionRestore(const bool value)
{
    loadIsFromSessionRestore = value;
}

void QVGraphicsView::goToFile(const Qv::GoToFileMode mode, const int index)
{
    const QVImageCore::GoToFileResult result = imageCore.goToFile(mode, index);

    if (result.reachedEnd)
        emit cancelSlideshow();
}

void QVGraphicsView::fitOrConstrainImage()
{
    QScopedValueRollback<bool> internalUpdateGuard(
            fullScreenPanInternalUpdate, true);
    // The explicit scene rect is also the scrollable range. Its titlebar
    // compensation depends on the current native contentLayoutRect, which
    // changes at the full-screen boundary even when the zoom transform does
    // not. Rebase it before every viewport fit/constraint pass so a padding
    // row from the previous window mode can never remain scrollable.
    updateSceneRect({}, true);

    if (calculatedZoomMode.has_value())
        recalculateZoom();
    else
        scrollHelper->constrain();

    restoreFullScreenPanPreservation();
}

void QVGraphicsView::beginFullScreenPanPreservation()
{
    // A delayed constraint can otherwise write an old scroll value after the
    // transition has already established its new range.
    constrainBoundsTimer->stop();
    cancelPendingZoomAnchor();

    // MainWindow starts preservation before asking Qt/AppKit to change the
    // window state. The native animation callback also calls this method when
    // it starts; that second call must not recapture an already-changing bar
    // value and erase the edge captured at the request boundary.
    if (fullScreenPanPreservationActive)
        return;

    fullScreenPanPreservationActive = true;
    fullScreenHorizontalPanEdge = ScrollEdge::None;
    fullScreenVerticalPanEdge = ScrollEdge::None;
    fullScreenPanAnchorScene.reset();

    if (calculatedZoomMode.has_value())
        return;

    captureFullScreenPanEdges();
    captureFullScreenPanAnchor();
}

void QVGraphicsView::refreshFullScreenPanPreservation()
{
    constrainBoundsTimer->stop();
    cancelPendingZoomAnchor();

    // Unlike begin(), this method is called at the exit request boundary. The
    // user may have dragged to a new edge while already in full screen, so the
    // current bars are the authoritative state even when preservation is
    // already active for the entry/exit animation.
    fullScreenPanPreservationActive = true;
    fullScreenHorizontalPanEdge = ScrollEdge::None;
    fullScreenVerticalPanEdge = ScrollEdge::None;
    fullScreenPanAnchorScene.reset();
    if (!calculatedZoomMode.has_value()) {
        captureFullScreenPanEdges();
        captureFullScreenPanAnchor();
    }
}

void QVGraphicsView::endFullScreenPanPreservation()
{
    constrainBoundsTimer->stop();
    restoreFullScreenPanPreservation();
    fullScreenPanPreservationActive = false;
    fullScreenHorizontalPanEdge = ScrollEdge::None;
    fullScreenVerticalPanEdge = ScrollEdge::None;
    fullScreenPanAnchorScene.reset();
}

void QVGraphicsView::restoreFullScreenPanPreservation()
{
    if (!fullScreenPanPreservationActive || calculatedZoomMode.has_value())
        return;

    QScopedValueRollback<bool> internalUpdateGuard(
            fullScreenPanInternalUpdate, true);

    const bool restoreHorizontalAnchor = fullScreenHorizontalPanEdge == ScrollEdge::None;
    const bool restoreVerticalAnchor = fullScreenVerticalPanEdge == ScrollEdge::None;

    if (fullScreenHorizontalPanEdge == ScrollEdge::Minimum)
        horizontalScrollBar()->setValue(horizontalScrollBar()->minimum());
    else if (fullScreenHorizontalPanEdge == ScrollEdge::Maximum)
        horizontalScrollBar()->setValue(horizontalScrollBar()->maximum());

    if (fullScreenVerticalPanEdge == ScrollEdge::Minimum)
        verticalScrollBar()->setValue(verticalScrollBar()->minimum());
    else if (fullScreenVerticalPanEdge == ScrollEdge::Maximum)
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());

    if (!fullScreenPanAnchorScene.has_value()
        || (!restoreHorizontalAnchor && !restoreVerticalAnchor))
        return;

    const QPoint anchorViewportPosition = getUsableViewportRect().center();
    const QPoint mappedAnchorPosition = mapFromScene(fullScreenPanAnchorScene.value());
    if (restoreHorizontalAnchor) {
        const int delta = (mappedAnchorPosition.x() - anchorViewportPosition.x()) * getRtlFlip();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + delta);
    }
    if (restoreVerticalAnchor) {
        const int delta = mappedAnchorPosition.y() - anchorViewportPosition.y();
        verticalScrollBar()->setValue(verticalScrollBar()->value() + delta);
    }
}

bool QVGraphicsView::isSmoothScalingRequested() const
{
    return smoothScalingMode != Qv::SmoothScalingMode::Disabled &&
        (!smoothScalingLimit.has_value() || zoomLevel < smoothScalingLimit.value());
}

bool QVGraphicsView::isExpensiveScalingRequested() const
{
    if (getCurrentFileDetails().isNativeHDRLoaded
        || getCurrentFileDetails().isNativeSDRLoaded
        || getCurrentFileDetails().isVectorLoaded || !isSmoothScalingRequested()
        || smoothScalingMode != Qv::SmoothScalingMode::Expensive
        || !getCurrentFileDetails().isPixmapLoaded)
        return false;

    // Don't go over the maximum scaling size (a small tolerance is added to cover rounding errors)
    const QSize contentSize = getContentRect().size();
    const QSize maxSize = getUsableViewportRect(true).size() * (expensiveScalingAboveWindowSize ? 3 : 1) + QSize(2, 2);
    return contentSize.width() <= maxSize.width() && contentSize.height() <= maxSize.height();
}

QSizeF QVGraphicsView::getEffectiveOriginalSize() const
{
    return getUnspecializedTransform().mapRect(QRectF(QPoint(), getCurrentFileDetails().loadedPixmapSize)).size() * getDpiAdjustment();
}

QRect QVGraphicsView::fullScreenTransitionImageRect() const
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return {};

    const QRect mappedBounds = mapFromScene(
        scene()->itemsBoundingRect()).boundingRect();
    return QRect(mappedBounds.topLeft(), getContentRect().size());
}

QImage QVGraphicsView::fullScreenTransitionImage() const
{
    const QImage image = imageCore.getLoadedPixmap().toImage();
    return image.isNull()
        ? QImage() : image.transformed(getUnspecializedTransform());
}

LogicalPixelFitter QVGraphicsView::getPixelFitter() const
{
    const MainWindow::ViewportPosition viewportPos = getMainWindow()->getViewportPosition();
    return LogicalPixelFitter(devicePixelRatioF(), QPoint(0, viewportPos.widgetY + viewportPos.obscuredHeight));
}

void QVGraphicsView::matchContentCenter(const QRect target)
{
    const QPointF delta = QRectF(getContentRect()).center() - QRectF(target).center();
    scrollHelper->move(QPointF(delta.x() * getRtlFlip(), delta.y()));
}

std::optional<Qv::GoToFileMode> QVGraphicsView::getNavigationRegion(const QPoint mousePos) const
{
    const int regionWidth = qMin(width() / 3, 150);
    const int regionMinY = height() / 4;
    const int regionMaxY = regionMinY + (height() / 2);
    if (mousePos.y() >= regionMinY && mousePos.y() < regionMaxY)
    {
        if (mousePos.x() < regionWidth)
            return isRightToLeft() ? Qv::GoToFileMode::Next : Qv::GoToFileMode::Previous;
        if (mousePos.x() >= width() - regionWidth)
            return isRightToLeft() ? Qv::GoToFileMode::Previous : Qv::GoToFileMode::Next;
    }
    return {};
}

QRect QVGraphicsView::getContentRect() const
{
    if (!getCurrentFileDetails().isPixmapLoaded)
        return {};

    // Avoid using loadedPixmapItem and the active transform because the pixmap may have expensive scaling applied
    // which introduces a rounding error to begin with, and even worse, the error will be magnified if we're in the
    // the process of zooming in and haven't re-applied the expensive scaling yet. If that's the case, callers need
    // to know what the content rect will be once the dust settles rather than what's being temporarily displayed.
    const QSizeF pixmapSize = getCurrentFileDetails().loadedPixmapSize;
    const QRectF pixmapBoundingRect = QRectF(QPoint(), pixmapSize);
    const qreal pixmapScale = zoomLevel * appliedDpiAdjustment;
    const QTransform pixmapTransform = normalizeTransformOrigin(getUnspecializedTransform().scale(pixmapScale, pixmapScale), pixmapSize);
    const QRectF contentRect = pixmapTransform.mapRect(pixmapBoundingRect);
    return QRect(contentRect.topLeft().toPoint(), getPixelFitter().snapSize(contentRect.size()));
}

QRect QVGraphicsView::getUsableViewportRect(const bool addOverscan) const
{
    QRect rect = viewport()->rect();
    rect.setTop(getMainWindow()->getViewportPosition().obscuredHeight);
    if (addOverscan)
        rect.adjust(-fitOverscan, -fitOverscan, fitOverscan, fitOverscan);
    return rect;
}

void QVGraphicsView::setTransformScale(const qreal value)
{
    setTransformWithNormalization(getUnspecializedTransform().scale(value, value));
}

void QVGraphicsView::setTransformWithNormalization(const QTransform &matrix)
{
    const bool nativeMetalImage = getCurrentFileDetails().isNativeHDRLoaded
            || getCurrentFileDetails().isNativeSDRLoaded;
    const QSizeF normalizationSize = nativeMetalImage
            ? QSizeF(getCurrentFileDetails().loadedPixmapSize)
            : loadedPixmapItem->boundingRect().size();
    setTransform(normalizeTransformOrigin(matrix, normalizationSize));
}

void QVGraphicsView::logViewportState(const char *phase) const
{
    if (!qEnvironmentVariableIsSet("FOVELLE_DIAGNOSTIC_LOG"))
        return;

    qInfo().noquote() << "FOVELLE_VIEW"
                      << "phase=" << phase << "zoom=" << zoomLevel << "sceneRect=" << sceneRect()
                      << "itemRect="
                      << ((getCurrentFileDetails().isNativeHDRLoaded
                           || getCurrentFileDetails().isNativeSDRLoaded)
                                  ? QRectF(QPointF(), getCurrentFileDetails().loadedPixmapSize)
                                  : loadedPixmapItem->sceneBoundingRect())
                      << "contentRect=" << getContentRect() << "viewportRect=" << viewport()->rect()
                      << "usableViewportRect=" << getUsableViewportRect()
                      << "panAnchorScene=" << mapToScene(getUsableViewportRect().center())
                      << "sceneOriginInViewport=" << mapFromScene(QPointF(0, 0))
                      << "viewportOriginInScene=" << mapToScene(QPoint(0, 0))
                      << "transform=" << transform() << "hbar=" << horizontalScrollBar()->value()
                      << horizontalScrollBar()->minimum() << horizontalScrollBar()->maximum()
                      << "vbar=" << verticalScrollBar()->value() << verticalScrollBar()->minimum()
                      << verticalScrollBar()->maximum();
}

QPolygonF QVGraphicsView::getHDRViewportCorners() const
{
    const QSize sourceSize = getCurrentFileDetails().loadedPixmapSize;
    if (sourceSize.isEmpty() || viewport()->size().isEmpty())
        return {};

    const QPolygonF sourceCorners{ QPointF(0.0, 0.0), QPointF(sourceSize.width(), 0.0),
                                   QPointF(sourceSize.width(), sourceSize.height()),
                                   QPointF(0.0, sourceSize.height()) };
    return viewportTransform().map(sourceCorners);
}

void QVGraphicsView::stageHDRGeometry(const QSize &viewportSize,
                                      const QPolygonF &imageCorners)
{
    const auto rendererState = hdrRenderer
            ? hdrRenderer->diagnostics() : QVCocoaFunctions::HDRRendererDiagnostics{};
    const bool nativeSDR = getCurrentFileDetails().isNativeSDRLoaded;
    const bool reuseVisibleHDR = nativeSDR
            ? rendererState.firstFramePresented
            : canReuseHDRPresentation(
                    rendererState.firstFramePresented, rendererState.hdrPrepared);
    const bool invalidateUnpresentedGeometry = !nativeSDR
            && hdrLayoutReady && !reuseVisibleHDR;
    // SDR has no gain-map/EDR preparation phase. Publish its latest geometry
    // immediately and let CAMetalDisplayLink coalesce it; waiting two display
    // intervals (and resetting an already submitted drawable) only adds cold-
    // open latency without protecting any SDR invariant.
    hdrLayoutReady = nativeSDR || reuseVisibleHDR;
    hdrPendingGeometryValid = true;
    hdrPendingViewportSize = viewportSize;
    hdrPendingImageCorners = imageCorners;
    const bool presentationFullyVisible = hdrPresentationActive
            && rendererState.presentationActiveRequested
            && !rendererState.presentationAnimationInFlight
            && rendererState.layerOpacity >= 0.999F;
    loadedPixmapItem->setVisible(!reuseVisibleHDR || !presentationFullyVisible);

    if (invalidateUnpresentedGeometry && hdrRenderer)
        hdrRenderer->invalidateGeometry();

    // Before the first prepared HDR frame, keep the complete Qt proxy visible
    // while layout settles. Afterwards the source-space cached endpoints can
    // be transformed directly for every zoom/pan geometry. Keep the last HDR
    // drawable visible until its replacement is presented: returning to the
    // SDR proxy would cause the reported brightness dip and restart.
    if (nativeSDR)
        hdrGeometryTimer->stop();
    else
        hdrGeometryTimer->start();
    if (!hdrActivationCompleted)
        hdrPresentationTimer->start();
    // The last prepared Metal frame deliberately remains visible while an
    // interactive replacement is pending.  Repainting the hidden Qt proxy at
    // this point adds a second window surface update without changing any
    // visible pixel.
    if (!reuseVisibleHDR)
        viewport()->update();
    logHDRState(reuseVisibleHDR ? "geometry-reused" : "geometry-staged");
}

void QVGraphicsView::finishHDRGeometryStabilization()
{
    if (!hdrRendererActive || !hdrRenderer)
        return;

    const QSize viewportSize = viewport()->size();
    const QPolygonF imageCorners = getHDRViewportCorners();
    if (viewportSize.isEmpty() || imageCorners.size() < 4)
        return;

    if (!hdrPendingGeometryValid
        || !hdrViewportGeometryEquivalent(
                hdrPendingViewportSize, hdrPendingImageCorners,
                viewportSize, imageCorners)) {
        stageHDRGeometry(viewportSize, imageCorners);
        return;
    }

    hdrLayoutReady = true;
    requestHDRRendererUpdate();
    logHDRState("geometry-stable");
}

void QVGraphicsView::updateHDRRenderer()
{
    if (!hdrRendererActive || !hdrRenderer)
        return;

    const QSize viewportSize = viewport()->size();
    const QPolygonF viewportCorners = getHDRViewportCorners();
    if (viewportSize.isEmpty() || viewportCorners.size() < 4)
        return;

    if (!hdrPendingGeometryValid
        || !hdrViewportGeometryEquivalent(
                hdrPendingViewportSize, hdrPendingImageCorners,
                viewportSize, viewportCorners)) {
        stageHDRGeometry(viewportSize, viewportCorners);
        if (!hdrLayoutReady)
            return;
    }

    if (!hdrLayoutReady)
        return;

    const auto beforeRender = hdrRenderer->diagnostics();

    if (hdrPresentationActive
        && beforeRender.firstFramePresented && beforeRender.drawableGeometryMatches
        && beforeRender.presentationActiveRequested
        && !beforeRender.presentationAnimationInFlight
        && beforeRender.layerOpacity >= 0.999F)
        loadedPixmapItem->setVisible(false);

    // Submit only the final-headroom representation. The complete SDR proxy
    // (or prior HDR drawable during navigation) remains visible until that
    // frame is actually presented; the native presentation container then
    // crossfades that final endpoint without generating partial-HDR pixels.
    const bool interactive = pressedMouseButton != Qt::NoButton
            || (hdrScrollInteractionClock.isValid()
                && hdrScrollInteractionClock.elapsed() < 160)
            || (hdrInteractionClock.isValid() && hdrInteractionStep >= 0
                && hdrInteractionStep < 48);
    hdrRenderer->render(viewportSize, viewportCorners, 1.0, interactive);
    logHDRState("render");
}

void QVGraphicsView::requestHDRRendererUpdate()
{
    if (!hdrRendererActive || !hdrRenderer || hdrFrameRequestTimer->isActive())
        return;
    // All geometry and paint events publish only a dirty request. Qt collapses
    // them on the next event-loop turn. CAMetalDisplayLink consumes only the
    // newest state at a sustainable display cadence; Core Image encoding stays
    // off-main and no GPU-completion callback gates the next request.
    hdrFrameRequestTimer->start();
}

void QVGraphicsView::logHDRState(const char *phase) const
{
    if (!qEnvironmentVariableIsSet("FOVELLE_HDR_DIAGNOSTIC_LOG") || !hdrRenderer)
        return;

    const auto &fileDetails = getCurrentFileDetails();
    const auto &metadata = fileDetails.hdrMetadata;
    const auto renderer = hdrRenderer->diagnostics();
    const QPoint viewportGlobalOrigin = viewport()->mapToGlobal(QPoint(0, 0));
    QJsonArray imageCorners;
    for (const QPointF &corner : getHDRViewportCorners())
        imageCorners.append(QJsonArray{ corner.x(), corner.y() });
    QJsonObject object{
        { QStringLiteral("phase"), QString::fromLatin1(phase) },
        { QStringLiteral("path"), fileDetails.fileInfo.absoluteFilePath() },
        { QStringLiteral("source_kind"), metadata.sourceKind },
        { QStringLiteral("type_identifier"), metadata.typeIdentifier },
        { QStringLiteral("color_space"), metadata.colorSpaceName },
        { QStringLiteral("transfer_function"), metadata.transferFunction },
        { QStringLiteral("pixel_width"), metadata.pixelSize.width() },
        { QStringLiteral("pixel_height"), metadata.pixelSize.height() },
        { QStringLiteral("bits_per_component"), metadata.bitsPerComponent },
        { QStringLiteral("is_raw"), metadata.isRaw },
        { QStringLiteral("has_apple_gain_map"), metadata.hasAppleGainMap },
        { QStringLiteral("has_iso_gain_map"), metadata.hasISOGainMap },
        { QStringLiteral("decoded_to_hdr"), metadata.decodedToHDR },
        { QStringLiteral("uses_raw_extended_dynamic_range"), metadata.usesRawExtendedDynamicRange },
        { QStringLiteral("used_raw_preview"), metadata.usedRawPreview },
        { QStringLiteral("uses_processed_raw_preview"), metadata.usesProcessedRawPreview },
        { QStringLiteral("content_headroom"), renderer.contentHeadroom },
        { QStringLiteral("decode_ms"), fileDetails.decodeMilliseconds },
        { QStringLiteral("renderer_available"), renderer.rendererAvailable },
        { QStringLiteral("image_active"), renderer.imageActive },
        { QStringLiteral("rgba16_float"), renderer.usesRGBA16Float },
        { QStringLiteral("extended_linear_display_p3"), renderer.usesExtendedLinearDisplayP3 },
        { QStringLiteral("color_sync"), renderer.usesColorSync },
        { QStringLiteral("wants_edr"), renderer.wantsExtendedDynamicRangeContent },
        { QStringLiteral("opaque_drawable_clear"), renderer.clearsEntireDrawableOpaque },
        { QStringLiteral("display_headroom_overridden"), renderer.displayHeadroomOverridden },
        { QStringLiteral("display_current_headroom_overridden"),
          renderer.displayCurrentHeadroomOverridden },
        { QStringLiteral("layout_ready"), hdrLayoutReady },
        { QStringLiteral("geometry_pending"), hdrPendingGeometryValid && !hdrLayoutReady },
        { QStringLiteral("fallback_visible"), loadedPixmapItem->isVisible() },
        { QStringLiteral("zoom_level"), zoomLevel },
        { QStringLiteral("viewport_global_x"), viewportGlobalOrigin.x() },
        { QStringLiteral("viewport_global_y"), viewportGlobalOrigin.y() },
        { QStringLiteral("window_global_x"), renderer.nativeWindowGlobalX },
        { QStringLiteral("window_global_y"), renderer.nativeWindowGlobalY },
        { QStringLiteral("native_window_number"), renderer.nativeWindowNumber },
        { QStringLiteral("viewport_logical_width"), viewport()->width() },
        { QStringLiteral("viewport_logical_height"), viewport()->height() },
        { QStringLiteral("viewport_device_pixel_ratio"), viewport()->devicePixelRatioF() },
        { QStringLiteral("first_frame_submitted"), renderer.firstFrameSubmitted },
        { QStringLiteral("first_frame_presented"), renderer.firstFramePresented },
        { QStringLiteral("hdr_preparation_in_flight"), renderer.hdrPreparationInFlight },
        { QStringLiteral("hdr_prepared"), renderer.hdrPrepared },
        { QStringLiteral("core_image_managed_intermediates"),
          renderer.usesCoreImageManagedIntermediates },
        { QStringLiteral("core_image_cache_intermediates"), renderer.cachesIntermediates },
        { QStringLiteral("prepared_geometry_active"), renderer.preparedGeometryActive },
        { QStringLiteral("bootstrapping_edr"), renderer.bootstrappingEDR },
        { QStringLiteral("hdr_activation_completed"), hdrActivationCompleted },
        { QStringLiteral("layer_opacity"), renderer.layerOpacity },
        { QStringLiteral("display_current_headroom"), renderer.displayCurrentHeadroom },
        { QStringLiteral("display_potential_headroom"), renderer.displayPotentialHeadroom },
        { QStringLiteral("display_rendering_headroom"), renderer.displayRenderingHeadroom },
        { QStringLiteral("target_headroom"), renderer.targetHeadroom },
        { QStringLiteral("layer_contents_headroom"), renderer.layerContentsHeadroom },
        { QStringLiteral("layer_contents_headroom_tag_supported"),
          renderer.usesLayerContentsHeadroomTag },
        { QStringLiteral("uses_metal_display_link"), renderer.usesCAMetalDisplayLink },
        { QStringLiteral("display_link_paused"), renderer.displayLinkPaused },
        { QStringLiteral("encodes_metal_off_main_thread"),
          renderer.encodesMetalOffMainThread },
        { QStringLiteral("display_link_interaction_pacing"),
          renderer.usesDisplayLinkInteractionPacing },
        { QStringLiteral("uses_persistent_hdr_surface"),
          renderer.usesPersistentHDRSurface },
        { QStringLiteral("persistent_hdr_surface_ready"),
          renderer.persistentHDRSurfaceReady },
        { QStringLiteral("presentation_active_requested"),
          renderer.presentationActiveRequested },
        { QStringLiteral("presentation_animation_in_flight"),
          renderer.presentationAnimationInFlight },
        { QStringLiteral("presentation_transition_count"),
          static_cast<qint64>(renderer.presentationTransitionCount) },
        { QStringLiteral("persistent_hdr_surface_bytes"),
          static_cast<qint64>(renderer.persistentHDRSurfaceBytes) },
        { QStringLiteral("persistent_hdr_surface_preparation_ms"),
          renderer.persistentHDRSurfacePreparationMilliseconds },
        { QStringLiteral("compositor_geometry_update_count"),
          static_cast<qint64>(renderer.compositorGeometryUpdateCount) },
        { QStringLiteral("display_link_interactive_submission_count"),
          static_cast<qint64>(renderer.displayLinkInteractiveSubmissionCount) },
        { QStringLiteral("frame_in_flight"), renderer.frameInFlight },
        { QStringLiteral("viewport_background_red"), renderer.backgroundRed },
        { QStringLiteral("viewport_background_green"), renderer.backgroundGreen },
        { QStringLiteral("viewport_background_blue"), renderer.backgroundBlue },
        { QStringLiteral("background_update_count"),
          static_cast<qint64>(renderer.backgroundUpdateCount) },
        { QStringLiteral("image_corners"), imageCorners },
        { QStringLiteral("transition_progress"), renderer.transitionProgress },
        { QStringLiteral("requested_drawable_width"), renderer.requestedDrawableWidth },
        { QStringLiteral("requested_drawable_height"), renderer.requestedDrawableHeight },
        { QStringLiteral("actual_texture_width"), renderer.actualTextureWidth },
        { QStringLiteral("actual_texture_height"), renderer.actualTextureHeight },
        { QStringLiteral("drawable_geometry_matches"), renderer.drawableGeometryMatches },
        { QStringLiteral("geometry_generation"), static_cast<qint64>(renderer.geometryGeneration) },
        { QStringLiteral("geometry_reset_count"), static_cast<qint64>(renderer.geometryResetCount) },
        { QStringLiteral("render_request_count"), static_cast<qint64>(renderer.renderRequestCount) },
        { QStringLiteral("coalesced_render_request_count"),
          static_cast<qint64>(renderer.coalescedRenderRequestCount) },
        { QStringLiteral("display_link_callback_count"),
          static_cast<qint64>(renderer.displayLinkCallbackCount) },
        { QStringLiteral("display_link_rebuild_count"),
          static_cast<qint64>(renderer.displayLinkRebuildCount) },
        { QStringLiteral("deferred_display_link_callback_count"),
          static_cast<qint64>(renderer.deferredDisplayLinkCallbackCount) },
        { QStringLiteral("requested_render_generation"),
          static_cast<qint64>(renderer.requestedRenderGeneration) },
        { QStringLiteral("submitted_render_generation"),
          static_cast<qint64>(renderer.submittedRenderGeneration) },
        { QStringLiteral("render_count"), static_cast<qint64>(renderer.renderCount) },
        { QStringLiteral("last_render_ms"), renderer.lastRenderMilliseconds },
        { QStringLiteral("last_gpu_execution_ms"),
          renderer.lastGPUExecutionMilliseconds },
        { QStringLiteral("interaction_step"), hdrInteractionStep },
        { QStringLiteral("interaction_elapsed_ms"),
          hdrInteractionClock.isValid() ? hdrInteractionClock.elapsed() : -1 },
        { QStringLiteral("interaction_zoom_ms"), hdrInteractionZoomMilliseconds },
        { QStringLiteral("first_visible_final_headroom"),
          renderer.firstVisibleFrameUsesFinalHeadroom },
        { QStringLiteral("presented_frame_count"),
          static_cast<qint64>(renderer.presentedFrameCount) },
        { QStringLiteral("frames_in_flight"), renderer.framesInFlight },
        { QStringLiteral("last_presented_interval_ms"),
          renderer.lastPresentedIntervalMilliseconds },
        { QStringLiteral("last_request_to_present_ms"),
          renderer.lastRequestToPresentationMilliseconds },
        { QStringLiteral("missed_target_deadline_count"),
          static_cast<qint64>(renderer.missedTargetDeadlineCount) },
        { QStringLiteral("native_navigation_overlay"),
          renderer.usesNativeNavigationOverlay },
        { QStringLiteral("native_navigation_visible_count"),
          renderer.nativeNavigationVisibleCount },
        { QStringLiteral("navigation_overlay_update_count"),
          static_cast<qint64>(renderer.navigationOverlayUpdateCount) }
    };
    qInfo().noquote() << "FOVELLE_HDR" << QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QTransform QVGraphicsView::getUnspecializedTransform() const
{
    // Returns a transform that represents the currently applied mirroring, flipping, and rotation
    // (only in increments of 90 degrees) operations, but with no scaling or translation.
    const QTransform t = transform();
    if (t.type() == QTransform::TxRotate)
        return { 0, t.m12() < 0 ? -1.0 : 1.0, t.m21() < 0 ? -1.0 : 1.0, 0, 0, 0 };
    else
        return { t.m11() < 0 ? -1.0 : 1.0, 0, 0, t.m22() < 0 ? -1.0 : 1.0, 0, 0 };
}

QTransform QVGraphicsView::normalizeTransformOrigin(const QTransform &matrix, const QSizeF &pixmapSize) const
{
    // This applies translation to compensate for mirroring, flipping, and rotation to ensure that
    // a pixmap will have its resulting top left at 0, 0. In theory this shouldn't matter, but it
    // works around a glitch where Qt sometimes won't paint the last pixel on the right of the
    // viewport if an image is rotated 90 degrees and just touching the right edge.
    const int horizontalFactor = matrix.m11() < 0 ? -1 * getRtlFlip() : matrix.m12() < 0 ? -1 : 0;
    const int verticalFactor = matrix.m22() < 0 ? -1 : matrix.m21() < 0 ? -1 * getRtlFlip() : 0;
    QTransform t { matrix.m11(), matrix.m12(), matrix.m21(), matrix.m22(), 0, 0 };
    return t.translate(pixmapSize.width() * horizontalFactor, pixmapSize.height() * verticalFactor);
}

qreal QVGraphicsView::getDpiAdjustment() const
{
    // Although inverting this potentially introduces a rounding error, it is inevitable. For
    // example with 1:1 pixel sizing @ 100% zoom, the transform's scale must be set to the
    // inverted value. Pre-inverting it here helps keep things consistent, e.g. so that the
    // content rect calculation has the same error that will happen during painting.
    return useOneToOnePixelSizing ? 1.0 / devicePixelRatioF() : 1.0;
}

void QVGraphicsView::handleDpiAdjustmentChange()
{
    if (appliedDpiAdjustment == getDpiAdjustment())
        return;

    removeExpensiveScaling();

    fitOrConstrainImage();

    expensiveScaleTimer->start();
}

void QVGraphicsView::handleSmoothScalingChange()
{
    loadedPixmapItem->setTransformationMode(isSmoothScalingRequested() ? Qt::SmoothTransformation : Qt::FastTransformation);

    if (isExpensiveScalingRequested())
        expensiveScaleTimer->start();
    else if (appliedExpensiveScaleZoomLevel != 0.0)
        removeExpensiveScaling();
}

int QVGraphicsView::getRtlFlip() const
{
    return isRightToLeft() ? -1 : 1;
}

bool QVGraphicsView::handleNativeGestureEvent(QNativeGestureEvent *event)
{
    const auto getGesturePosition = [](const QNativeGestureEvent *gestureEvent) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return gestureEvent->position().toPoint();
#else
        return gestureEvent->localPos().toPoint();
#endif
    };

    switch (event->gestureType())
    {
    case Qt::BeginNativeGesture:
        if (getCurrentFileDetails().isVectorLoaded)
            setVectorInteractionPresentation(true);
        break;
    case Qt::EndNativeGesture:
        vectorRefineTimer->stop();
        setVectorInteractionPresentation(false);
        constrainBoundsTimer->start();
        break;
    case Qt::ZoomNativeGesture:
    {
        if (getCurrentFileDetails().isPixmapLoaded)
        {
            const qreal relativeFactor = nativeGestureZoomFactor(event->value());
            if (!qFuzzyCompare(relativeFactor, 1.0))
                zoomAbsolute(zoomLevel * relativeFactor, getGesturePosition(event));
        }
        break;
    }
    case Qt::PanNativeGesture:
    {
        if (getCurrentFileDetails().isPixmapLoaded && !event->delta().isNull())
        {
            cancelPendingZoomAnchor();
            scrollHelper->move(nativeGesturePanScrollDelta(event->delta(), isRightToLeft()));
            constrainBoundsTimer->start();
        }
        break;
    }
    default:
        return false;
    }

    event->accept();
    return true;
}

void QVGraphicsView::cancelTurboNav()
{
    if (!turboNavMode.has_value())
        return;

    const ActionManager &actionManager = qvApp->getActionManager();
    turboNavMode = {};
    actionManager.setActionShortcuts("previousfile", navPrevShortcuts);
    actionManager.setActionShortcuts("nextfile", navNextShortcuts);
    actionManager.setActionShortcuts("randomfile", navRandomShortcuts);
    navPrevShortcuts = {};
    navNextShortcuts = {};
    navRandomShortcuts = {};
}

MainWindow* QVGraphicsView::getMainWindow() const
{
    return qobject_cast<MainWindow*>(window());
}

void QVGraphicsView::scheduleVerticalScrollBarGeometry()
{
    if (verticalScrollBarGeometryUpdatePending)
        return;

    verticalScrollBarGeometryUpdatePending = true;
    QTimer::singleShot(0, this, [this]() {
        verticalScrollBarGeometryUpdatePending = false;
        refreshVerticalScrollBarGeometry();
    });
}

void QVGraphicsView::refreshVerticalScrollBarGeometry()
{
    if (isUpdatingVerticalScrollBarGeometry || !verticalScrollBar())
        return;

    // QAbstractScrollArea lays out the QScrollBar inside a private container.
    // Its layout knows about the viewport but not Fovelle's macOS full-size
    // titlebar overlap. Move that container's top edge after Qt lays it out;
    // the bottom edge remains owned by Qt so horizontal-bar corner handling is
    // unchanged. On Qt versions without a separate container, adjust the bar
    // itself instead.
    QWidget *barGeometryWidget = verticalScrollBar()->parentWidget();
    if (!barGeometryWidget || barGeometryWidget == this
        || barGeometryWidget == viewport())
        barGeometryWidget = verticalScrollBar();

    const QRect geometry = barGeometryWidget->geometry();
    if (geometry.width() <= 0 || geometry.height() <= 0)
        return;

    const int obscuredHeight = getMainWindow()
        ? qMax(0, getMainWindow()->getViewportPosition().obscuredHeight)
        : 0;
    const int adjustedTop = qMin(obscuredHeight, geometry.bottom() + 1);
    if (geometry.top() == adjustedTop)
        return;

    QScopedValueRollback<bool> updateGuard(
        isUpdatingVerticalScrollBarGeometry, true);
    QRect adjustedGeometry = geometry;
    adjustedGeometry.setTop(adjustedTop);
    barGeometryWidget->setGeometry(adjustedGeometry);
}

QVGraphicsView::ScrollEdge QVGraphicsView::getScrollEdge(const QScrollBar *scrollBar) const
{
    // ScrollHelper calculates the image-space range from QRect dimensions while
    // QGraphicsView exposes an inclusive integer scrollbar range.  Releasing a
    // drag at the visual edge can therefore leave a short (observed: two-unit)
    // animated constraint tail before the next fullscreen request captures it.
    constexpr int ScrollEdgeTolerance = 3;
    if (scrollBar->minimum() >= scrollBar->maximum())
        return ScrollEdge::None;
    if (scrollBar->value() <= scrollBar->minimum() + ScrollEdgeTolerance)
        return ScrollEdge::Minimum;
    if (scrollBar->value() >= scrollBar->maximum() - ScrollEdgeTolerance)
        return ScrollEdge::Maximum;
    return ScrollEdge::None;
}

void QVGraphicsView::captureFullScreenPanEdges()
{
    fullScreenHorizontalPanEdge = getScrollEdge(horizontalScrollBar());
    fullScreenVerticalPanEdge = getScrollEdge(verticalScrollBar());
}

void QVGraphicsView::captureFullScreenPanAnchor()
{
    const QRect usableViewport = getUsableViewportRect();
    if (!getCurrentFileDetails().isPixmapLoaded || usableViewport.isEmpty())
        return;

    fullScreenPanAnchorScene = mapToScene(usableViewport.center());
}

void QVGraphicsView::captureFullScreenPanState()
{
    fullScreenHorizontalPanEdge = ScrollEdge::None;
    fullScreenVerticalPanEdge = ScrollEdge::None;
    fullScreenPanAnchorScene.reset();
    captureFullScreenPanEdges();
    captureFullScreenPanAnchor();
}

void QVGraphicsView::updateSceneRect(
    const std::optional<QPoint> &restoreScrollPosition,
    const bool preserveScrollEdges)
{
    if (isUpdatingSceneRect)
        return;

    const auto &fileDetails = getCurrentFileDetails();
    if (!fileDetails.isPixmapLoaded || fileDetails.loadedPixmapSize.isEmpty())
    {
        setSceneRect(QRectF());
        refreshVerticalScrollBarGeometry();
        return;
    }

    // The pixmap can be temporarily rendered at a higher backing resolution
    // while the view transform is reduced by the matching factor. The scene
    // rectangle must follow that backing pixmap, otherwise QGraphicsView
    // centers the smaller logical scene while painting the larger item.
    const bool preserveViewport = restoreScrollPosition.has_value() ||
        (sceneRect().isValid() && !sceneRect().isEmpty());
    const bool preserveManualPanEdges = preserveScrollEdges
        && !restoreScrollPosition.has_value()
        && !calculatedZoomMode.has_value();
    const QPoint scrollPosition = restoreScrollPosition.value_or(
        QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()));
    const int horizontalValue = scrollPosition.x();
    const int verticalValue = scrollPosition.y();
    const ScrollEdge horizontalPanEdge =
        fullScreenHorizontalPanEdge != ScrollEdge::None
            ? fullScreenHorizontalPanEdge
            : preserveManualPanEdges ? getScrollEdge(horizontalScrollBar())
                                     : ScrollEdge::None;
    const ScrollEdge verticalPanEdge =
        fullScreenVerticalPanEdge != ScrollEdge::None
            ? fullScreenVerticalPanEdge
            : preserveManualPanEdges ? getScrollEdge(verticalScrollBar())
                                     : ScrollEdge::None;
    const QRectF desiredSceneRect = getSceneRectForViewport();
    QScopedValueRollback<bool> sceneRectUpdateGuard(isUpdatingSceneRect, true);
    if (sceneRect() != desiredSceneRect)
        setSceneRect(desiredSceneRect);
    // setSceneRect() resets the bars while it recalculates their ranges.
    // Restore the visual viewport before the next paint; Qt clamps values if
    // the new range is genuinely smaller. During a native fullscreen transition,
    // restore the scene anchor here instead of replaying the old integer value;
    // deferring this to a queued callback would expose an observable frame at the
    // origin during zooming or fullscreen exit.
    const bool restoreFullScreenPanAnchor = preserveViewport
            && fullScreenPanPreservationActive && fullScreenPanAnchorScene.has_value()
            && !calculatedZoomMode.has_value();
    if (restoreFullScreenPanAnchor)
        restoreFullScreenPanPreservation();

    if (preserveViewport && !restoreFullScreenPanAnchor)
    {
        if (preserveManualPanEdges && horizontalPanEdge != ScrollEdge::None)
        {
            horizontalScrollBar()->setValue(
                horizontalPanEdge == ScrollEdge::Minimum
                    ? horizontalScrollBar()->minimum()
                    : horizontalScrollBar()->maximum());
        }
        else
        {
            horizontalScrollBar()->setValue(horizontalValue);
        }

        if (preserveManualPanEdges && verticalPanEdge != ScrollEdge::None)
        {
            verticalScrollBar()->setValue(
                verticalPanEdge == ScrollEdge::Minimum
                    ? verticalScrollBar()->minimum()
                    : verticalScrollBar()->maximum());
        }
        else
        {
            verticalScrollBar()->setValue(verticalValue);
        }
        restorePendingZoomAnchor();
        logViewportState("scene-rect-viewport-restored");
    }
    refreshVerticalScrollBarGeometry();
}

void QVGraphicsView::restorePendingZoomAnchor()
{
    if (!pendingZoomAnchorScene.has_value()
        || !pendingZoomAnchorViewport.has_value())
        return;

    QScopedValueRollback<bool> internalUpdateGuard(
            fullScreenPanInternalUpdate, true);
    const QPointF mappedAnchor = mapFromScene(pendingZoomAnchorScene.value());
    const QPoint anchorViewport = pendingZoomAnchorFollowsViewportCenter
            ? getUsableViewportRect().center()
            : pendingZoomAnchorViewport.value();
    const QPointF delta = mappedAnchor - anchorViewport;
    horizontalScrollBar()->setValue(
        horizontalScrollBar()->value() + qRound(delta.x() * getRtlFlip()));
    verticalScrollBar()->setValue(
        verticalScrollBar()->value() + qRound(delta.y()));
}

void QVGraphicsView::cancelPendingZoomAnchor()
{
    if (!pendingZoomAnchorScene.has_value()
        && !pendingZoomAnchorViewport.has_value())
        return;

    ++pendingZoomAnchorGeneration;
    pendingZoomAnchorScene.reset();
    pendingZoomAnchorViewport.reset();
    pendingZoomAnchorFollowsViewportCenter = false;
}

QRectF QVGraphicsView::getSceneRectForViewport() const
{
    QRectF sceneRect = (getCurrentFileDetails().isNativeHDRLoaded
                        || getCurrentFileDetails().isNativeSDRLoaded)
            ? QRectF(QPointF(), getCurrentFileDetails().loadedPixmapSize)
            : loadedPixmapItem->boundingRect();
    const MainWindow *mainWindow = getMainWindow();
    if (!mainWindow)
        return sceneRect;

    const int obscuredHeight = mainWindow->getViewportPosition().obscuredHeight;
    if (obscuredHeight <= 0)
        return sceneRect;

    // The fit calculation uses the portion of the viewport not covered by the
    // macOS full-size titlebar. QGraphicsView's default AlignCenter, however,
    // centers a scene that fits in the entire viewport. Add equivalent empty
    // scene space on the view's top side so both calculations use the same
    // coordinate system. The scene padding is expressed in scene units because
    // QGraphicsView applies the current transform when calculating its indents.
    const QTransform currentTransform = transform();
    const qreal verticalScale = qMax(qAbs(currentTransform.m22()), qAbs(currentTransform.m12()));
    if (qFuzzyIsNull(verticalScale))
        return sceneRect;

    const qreal scenePadding = obscuredHeight / verticalScale;
    if (qAbs(currentTransform.m22()) >= qAbs(currentTransform.m12()))
    {
        if (currentTransform.m22() >= 0)
            sceneRect.adjust(0, -scenePadding, 0, 0);
        else
            sceneRect.adjust(0, 0, 0, scenePadding);
    }
    else if (currentTransform.m12() >= 0)
    {
        sceneRect.adjust(-scenePadding, 0, 0, 0);
    }
    else
    {
        sceneRect.adjust(0, 0, scenePadding, 0);
    }

    return sceneRect;
}

void QVGraphicsView::applyScrollBarTheme(const Qv::Theme theme)
{
    const Qv::Theme resolvedTheme = QVCocoaFunctions::resolvedTheme(theme);
    const QString style = scrollBarStyleSheet(resolvedTheme);
    for (QScrollBar *scrollBar : {horizontalScrollBar(), verticalScrollBar()})
    {
        scrollBar->setStyleSheet(style);
        scrollBar->setProperty("scrollBarTheme", static_cast<int>(resolvedTheme));
    }
}

void QVGraphicsView::applyHDRViewportBackground(const Qv::Theme theme)
{
    if (!hdrRenderer)
        return;
    hdrRenderer->setBackgroundColor(Qv::viewportBackgroundColor(QVCocoaFunctions::resolvedTheme(theme)));
    if (hdrRendererActive)
        requestHDRRendererUpdate();
}

void QVGraphicsView::settingsUpdated(const bool isInitialLoad)
{
    auto &settingsManager = qvApp->getSettingsManager();

    const Qv::Theme theme = settingsManager.getEnum<Qv::Theme>("theme");
    const Qv::Theme resolvedTheme = QVCocoaFunctions::resolvedTheme(theme);
    viewportBackgroundBrush = QBrush(Qv::viewportBackgroundColor(resolvedTheme));
    checkerboardBackground = settingsManager.getBoolean("checkerboardbackground");
    if (hdrRenderer)
        hdrRenderer->setCheckerboardBackground(checkerboardBackground);
    if (checkerboardBackground)
    {
        constexpr int checkerSize = 16;
        constexpr int tileSize = checkerSize * 2;
        const qreal dpr = qMax<qreal>(1.0, viewport()->devicePixelRatioF());
        QPixmap checkerboardTile(qRound(tileSize * dpr),
                                 qRound(tileSize * dpr));
        checkerboardTile.setDevicePixelRatio(dpr);
        checkerboardTile.fill(QColorConstants::White);
        QPainter tilePainter(&checkerboardTile);
        tilePainter.fillRect(0, 0, checkerSize, checkerSize,
                             QColor(204, 204, 204));
        tilePainter.fillRect(checkerSize, checkerSize,
                             checkerSize, checkerSize,
                             QColor(204, 204, 204));
        checkerboardBackgroundBrush = QBrush(checkerboardTile);
    }
    loadedPixmapItem->setVectorBackgroundBrush(
        checkerboardBackground ? checkerboardBackgroundBrush : viewportBackgroundBrush);
    viewport()->update();
    applyScrollBarTheme(resolvedTheme);
    applyHDRViewportBackground(theme);

    if (isInitialLoad || globalNavigationResetsZoom != settingsManager.getBoolean("navresetszoom"))
    {
        //nav resets zoom
        globalNavigationResetsZoom = settingsManager.getBoolean("navresetszoom");
        setNavigationResetsZoom(globalNavigationResetsZoom);
    }

    //smooth scaling
    smoothScalingMode = settingsManager.getEnum<Qv::SmoothScalingMode>("smoothscalingmode");

    //scaling two
    expensiveScalingAboveWindowSize = settingsManager.getBoolean("scalingtwoenabled");

    //smooth scaling limit
    smoothScalingLimit = settingsManager.getBoolean("smoothscalinglimitenabled") ? std::optional(settingsManager.getInteger("smoothscalinglimitpercent") / 100.0) : std::nullopt;

    //calculated zoom mode
    defaultCalculatedZoomMode = settingsManager.getEnum<Qv::CalculatedZoomMode>("calculatedzoommode");

    //scale factor
    zoomMultiplier = 1.0 + (settingsManager.getInteger("scalefactor") / 100.0);

    //fit zoom limit
    fitZoomLimit = settingsManager.getBoolean("fitzoomlimitenabled") ? std::optional(settingsManager.getInteger("fitzoomlimitpercent") / 100.0) : std::nullopt;

    //fit overscan
    fitOverscan = settingsManager.getInteger("fitoverscan");

    //cursor zoom
    zoomToCursor = settingsManager.getBoolean("cursorzoom");

    //one-to-one pixel sizing
    useOneToOnePixelSizing = settingsManager.getBoolean("onetoonepixelsizing");

    //small images at one-to-one
    showSmallImagesAtOneToOne = settingsManager.getBoolean("smallimageoneone");

    //constrained positioning
    constrainImagePosition = settingsManager.getBoolean("constrainimageposition");

    //constrained small centering
    constrainToCenterWhenSmaller = settingsManager.getBoolean("constraincentersmallimage");

    //disable delayed constraint
    disableDelayedConstraint = settingsManager.getBoolean("disabledelayedconstraint");
    constrainBoundsTimer->setInterval(disableDelayedConstraint ? 0 : 500);

    //nav speed
    turboNavInterval = settingsManager.getInteger("navspeed");

    //mouse actions
    enableNavigationRegions = settingsManager.getBoolean("navigationregionsenabled");
    doubleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportdoubleclickaction");
    altDoubleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportaltdoubleclickaction");
    dragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportdragaction");
    altDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportaltdragaction");
    middleButtonMode = settingsManager.getEnum<Qv::ClickOrDrag>("viewportmiddlebuttonmode");
    middleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportmiddleclickaction");
    altMiddleClickAction = settingsManager.getEnum<Qv::ViewportClickAction>("viewportaltmiddleclickaction");
    middleDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportmiddledragaction");
    altMiddleDragAction = settingsManager.getEnum<Qv::ViewportDragAction>("viewportaltmiddledragaction");
    verticalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportverticalscrollaction");
    horizontalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewporthorizontalscrollaction");
    altVerticalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportaltverticalscrollaction");
    altHorizontalScrollAction = settingsManager.getEnum<Qv::ViewportScrollAction>("viewportalthorizontalscrollaction");
    scrollActionCooldown = settingsManager.getBoolean("scrollactioncooldown");

    //cursor auto-hiding
    isCursorAutoHideFullscreenEnabled = settingsManager.getBoolean("cursorautohidefullscreenenabled");
    hideCursorTimer->setInterval(settingsManager.getDouble("cursorautohidefullscreendelay") * 1000.0);

    // End of settings variables

    if (isInitialLoad)
    {
        setCalculatedZoomMode(defaultCalculatedZoomMode);
    }

    handleSmoothScalingChange();

    handleDpiAdjustmentChange();

    fitOrConstrainImage();

    setCursorVisible(true);
}

void QVGraphicsView::closeImage(const bool stayInDir)
{
    imageCore.closeImage(stayInDir);
}

void QVGraphicsView::jumpToNextFrame()
{
    imageCore.jumpToNextFrame();
}

void QVGraphicsView::jumpToPreviousFrame()
{
    imageCore.jumpToPreviousFrame();
}

void QVGraphicsView::setPaused(const bool &desiredState)
{
    imageCore.setPaused(desiredState);
}

void QVGraphicsView::setSpeed(const int &desiredSpeed)
{
    imageCore.setSpeed(desiredSpeed);
}

void QVGraphicsView::rotateImage(const int relativeAngle)
{
    const QRect oldRect = getContentRect();
    const QTransform t = transform();
    const bool isMirroredOrFlipped = t.isRotating() ? ((t.m12() < 0) == (t.m21() < 0)) : ((t.m11() < 0) != (t.m22() < 0));
    setTransformWithNormalization(transform().rotate(relativeAngle * (isMirroredOrFlipped ? -1 : 1)));
    updateSceneRect();
    matchContentCenter(oldRect);
}

void QVGraphicsView::mirrorImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(-1 * rotateCorrection, 1 * rotateCorrection));
    updateSceneRect();
    matchContentCenter(oldRect);
}

void QVGraphicsView::flipImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(1 * rotateCorrection, -1 * rotateCorrection));
    updateSceneRect();
    matchContentCenter(oldRect);
}

void QVGraphicsView::resetTransformation()
{
    const QRect oldRect = getContentRect();
    const QTransform t = transform();
    const qreal scale = qFabs(t.isRotating() ? t.m21() : t.m11());
    setTransformWithNormalization(QTransform::fromScale(scale, scale));
    updateSceneRect();
    matchContentCenter(oldRect);
}
