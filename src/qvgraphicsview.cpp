#include "qvgraphicsview.h"
#include "qvapplication.h"
#include "qvinfodialog.h"
#include "qvmovie.h"
#include "qvcocoafunctions.h"
#include <QWheelEvent>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QSettings>
#include <QMessageBox>
#include <QDebug>
#include <QtMath>
#include <QGestureEvent>
#include <QScrollBar>

QVGraphicsView::QVGraphicsView(QWidget *parent) : QGraphicsView(parent)
{
    // GraphicsView setup
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setFrameShape(QFrame::NoFrame);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    viewport()->setAutoFillBackground(false);
    viewport()->setMouseTracking(true);

    // Scene setup
    auto *scene = new QGraphicsScene(this);
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

    constrainBoundsTimer = new QTimer(this);
    constrainBoundsTimer->setSingleShot(true);
    connect(constrainBoundsTimer, &QTimer::timeout, this, [this]{scrollHelper->constrain(disableDelayedConstraint);});

    hideCursorTimer = new QTimer(this);
    hideCursorTimer->setSingleShot(true);
    hideCursorTimer->setInterval(1000);
    connect(hideCursorTimer, &QTimer::timeout, this, [this]{setCursorVisible(false);});

    loadedPixmapItem = new QGraphicsPixmapItem();
    scene->addItem(loadedPixmapItem);

    // Connect to settings signal
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, [this]{settingsUpdated(false);});
    settingsUpdated(true);
}

// Events

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

        QGraphicsView::resizeEvent(event);

        if (shouldRestoreCalculatedZoom)
        {
            calculatedZoomMode = lastCalculatedZoomMode;
            emit calculatedZoomModeChanged();
        }

        const QSize sizeDelta = event->size() - event->oldSize();
        scrollHelper->move(QPointF(sizeDelta.width(), sizeDelta.height()) / -2.0);
        fitOrConstrainImage();
        logViewportState("resize");
    }
    else
    {
        QGraphicsView::resizeEvent(event);
    }
}

void QVGraphicsView::paintEvent(QPaintEvent *event)
{
    // This is the most reliable place to detect DPI changes. QWindow::screenChanged()
    // doesn't detect when the DPI is changed on the current monitor, for example.
    handleDpiAdjustmentChange();

    QGraphicsView::paintEvent(event);
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

    return QGraphicsView::event(event);
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
}

void QVGraphicsView::executeDragAction(const Qv::ViewportDragAction action, const QPoint delta, bool &isMovingWindow)
{
    if (action == Qv::ViewportDragAction::Pan)
    {
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
        "QScrollBar:vertical { width: 12px; }"
        "QScrollBar:horizontal { height: 12px; }"
        "QScrollBar::handle:vertical, QScrollBar::handle:horizontal { background: %2; border: none; border-radius: 5px; }"
        "QScrollBar::handle:vertical { min-height: 24px; margin: 2px 1px; }"
        "QScrollBar::handle:horizontal { min-width: 24px; margin: 1px 2px; }"
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

void QVGraphicsView::beforeLoad()
{
    lastCalculatedZoomMode.reset();
    lastCalculatedZoomLevel.reset();

    // If a prior pixmap is still loaded, capture its content rect
    if (getCurrentFileDetails().isPixmapLoaded)
        lastImageContentRect = getContentRect();
}

void QVGraphicsView::postLoad()
{
    scrollHelper->cancelAnimation();

    // Set the pixmap to the new image and reset the transform's scale to a known value
    removeExpensiveScaling();
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
    QTimer::singleShot(0, this, [this]() { logViewportState("post-load-next-turn"); });
    loadIsFromSessionRestore = false;

    expensiveScaleTimer->start();

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
    const qreal absoluteLevel = std::clamp(zoomLevel * relativeLevel, 0.01, 100.0);
    const std::optional<QPoint> pos = !mousePos.has_value() ? std::nullopt : zoomToCursor && isCursorVisible ? mousePos : Qv::CalculateViewportCenterPos;
    zoomAbsolute(absoluteLevel, pos);
}

void QVGraphicsView::zoomAbsolute(const qreal absoluteLevel, const std::optional<QPoint> &targetPos, const bool isApplyingCalculation)
{
    const bool keepsCalculatedZoomMode =
        isApplyingCalculation &&
        calculatedZoomMode.has_value() &&
        Qv::calculatedZoomModeIsSticky(calculatedZoomMode.value());
    const bool shouldRestoreCalculatedZoom =
        !isApplyingCalculation &&
        !calculatedZoomMode.has_value() &&
        lastCalculatedZoomMode.has_value() &&
        lastCalculatedZoomLevel.has_value() &&
        zoomLevelsEquivalent(absoluteLevel, lastCalculatedZoomLevel.value());
    if (!keepsCalculatedZoomMode && calculatedZoomMode.has_value())
    {
        calculatedZoomMode.reset();
        emit calculatedZoomModeChanged();
    }

    const bool isChanging = absoluteLevel != zoomLevel;
    const std::optional<QPoint> pos = targetPos == Qv::CalculateViewportCenterPos ? getUsableViewportRect().center() : targetPos;
    if (pos != lastZoomEventPos)
    {
        lastZoomEventPos = pos;
        lastZoomRoundingError = QPointF();
    }
    const QPointF scenePos = pos.has_value() ? mapToScene(pos.value()) - lastZoomRoundingError : QPointF();

    if (appliedExpensiveScaleZoomLevel != 0.0)
    {
        const qreal baseTransformScale = 1.0 / devicePixelRatioF();
        const qreal relativeLevel = absoluteLevel / appliedExpensiveScaleZoomLevel;
        setTransformScale(baseTransformScale * relativeLevel);
    }
    else
    {
        setTransformScale(absoluteLevel * appliedDpiAdjustment);
    }
    zoomLevel = absoluteLevel;

    if (shouldRestoreCalculatedZoom)
    {
        calculatedZoomMode = lastCalculatedZoomMode;
        emit calculatedZoomModeChanged();
    }

    scrollHelper->cancelAnimation();

    if (pos.has_value())
    {
        const QPointF move = mapFromScene(scenePos) - pos.value();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() + (move.x() * getRtlFlip()));
        verticalScrollBar()->setValue(verticalScrollBar()->value() + move.y());
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

    lastCalculatedZoomMode = calculatedZoomMode;
    lastCalculatedZoomLevel = targetRatio;

    zoomAbsolute(targetRatio, {}, true);
}

void QVGraphicsView::centerImage()
{
    ++sceneRectRestoreGeneration;
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

    scrollHelper->cancelAnimation();
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
    if (calculatedZoomMode.has_value())
        recalculateZoom();
    else
        scrollHelper->constrain(true);
}

bool QVGraphicsView::isSmoothScalingRequested() const
{
    return smoothScalingMode != Qv::SmoothScalingMode::Disabled &&
        (!smoothScalingLimit.has_value() || zoomLevel < smoothScalingLimit.value());
}

bool QVGraphicsView::isExpensiveScalingRequested() const
{
    if (!isSmoothScalingRequested() || smoothScalingMode != Qv::SmoothScalingMode::Expensive || !getCurrentFileDetails().isPixmapLoaded)
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
    setTransform(normalizeTransformOrigin(matrix, loadedPixmapItem->boundingRect().size()));
}

void QVGraphicsView::logViewportState(const char *phase) const
{
    if (!qEnvironmentVariableIsSet("FOVELLE_DIAGNOSTIC_LOG"))
        return;

    qInfo().noquote() << "FOVELLE_VIEW"
        << "phase=" << phase
        << "zoom=" << zoomLevel
        << "sceneRect=" << sceneRect()
        << "itemRect=" << (loadedPixmapItem ? loadedPixmapItem->sceneBoundingRect() : QRectF())
        << "contentRect=" << getContentRect()
        << "viewportRect=" << viewport()->rect()
        << "usableViewportRect=" << getUsableViewportRect()
        << "sceneOriginInViewport=" << mapFromScene(QPointF(0, 0))
        << "viewportOriginInScene=" << mapToScene(QPoint(0, 0))
        << "transform=" << transform()
        << "hbar=" << horizontalScrollBar()->value() << horizontalScrollBar()->minimum() << horizontalScrollBar()->maximum()
        << "vbar=" << verticalScrollBar()->value() << verticalScrollBar()->minimum() << verticalScrollBar()->maximum();
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
        break;
    case Qt::EndNativeGesture:
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

void QVGraphicsView::updateSceneRect(const std::optional<QPoint> &restoreScrollPosition)
{
    const auto &fileDetails = getCurrentFileDetails();
    if (!fileDetails.isPixmapLoaded || fileDetails.loadedPixmapSize.isEmpty())
    {
        ++sceneRectRestoreGeneration;
        setSceneRect(QRectF());
        return;
    }

    // The pixmap can be temporarily rendered at a higher backing resolution
    // while the view transform is reduced by the matching factor. The scene
    // rectangle must follow that backing pixmap, otherwise QGraphicsView
    // centers the smaller logical scene while painting the larger item.
    const bool preserveViewport = restoreScrollPosition.has_value() ||
        (sceneRect().isValid() && !sceneRect().isEmpty());
    const QPoint scrollPosition = restoreScrollPosition.value_or(
        QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value()));
    const int horizontalValue = scrollPosition.x();
    const int verticalValue = scrollPosition.y();
    const quint64 restoreGeneration = ++sceneRectRestoreGeneration;
    setSceneRect(loadedPixmapItem->boundingRect());
    // setSceneRect() may reset the bars while it recalculates their ranges.
    // Restore the visual viewport after that queued layout pass; Qt clamps
    // values if the new range is genuinely smaller.
    if (preserveViewport)
    {
        QTimer::singleShot(0, this, [this, horizontalValue, verticalValue, restoreGeneration]() {
            if (restoreGeneration != sceneRectRestoreGeneration)
                return;
            horizontalScrollBar()->setValue(horizontalValue);
            verticalScrollBar()->setValue(verticalValue);
            logViewportState("scene-rect-viewport-restored");
        });
    }
}

void QVGraphicsView::applyScrollBarTheme(const Qv::Theme theme)
{
    const QString style = scrollBarStyleSheet(theme);
    for (QScrollBar *scrollBar : {horizontalScrollBar(), verticalScrollBar()})
    {
        scrollBar->setStyleSheet(style);
        scrollBar->setProperty("scrollBarTheme", static_cast<int>(theme));
    }
}

void QVGraphicsView::settingsUpdated(const bool isInitialLoad)
{
    auto &settingsManager = qvApp->getSettingsManager();

    applyScrollBarTheme(settingsManager.getEnum<Qv::Theme>("theme"));

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
    matchContentCenter(oldRect);
}

void QVGraphicsView::mirrorImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(-1 * rotateCorrection, 1 * rotateCorrection));
    matchContentCenter(oldRect);
}

void QVGraphicsView::flipImage()
{
    const QRect oldRect = getContentRect();
    const int rotateCorrection = transform().isRotating() ? -1 : 1;
    setTransformWithNormalization(transform().scale(1 * rotateCorrection, -1 * rotateCorrection));
    matchContentCenter(oldRect);
}

void QVGraphicsView::resetTransformation()
{
    const QRect oldRect = getContentRect();
    const QTransform t = transform();
    const qreal scale = qFabs(t.isRotating() ? t.m21() : t.m11());
    setTransformWithNormalization(QTransform::fromScale(scale, scale));
    matchContentCenter(oldRect);
}
