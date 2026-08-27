#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvrenamedialog.h"
#include "qvmenu.h"
#include "qvmovie.h"
#include "nativedialogs.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QString>
#include <QPixmap>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileSystemWatcher>
#include <QProcess>
#include <QDesktopServices>
#include <QContextMenuEvent>
#include <QImageWriter>
#include <QImage>
#include <QSettings>
#include <QStyle>
#include <QIcon>
#include <QMimeDatabase>
#include <QScreen>
#include <QCursor>
#include <QInputDialog>
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>
#include <QMenu>
#include <QWindow>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <QLabel>
#include <QPushButton>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QTimer>
#include <QMouseEvent>
#include <QPainterPath>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtMath>

namespace
{
class ImageNavigationButton : public QPushButton
{
public:
    ImageNavigationButton(const bool previous, QWidget *parent) :
        QPushButton(parent),
        previous(previous)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_NoSystemBackground);
        setAutoFillBackground(false);
        setMouseTracking(true);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFlat(true);
        setStyleSheet(QStringLiteral("QPushButton { background: transparent; border: none; padding: 0; }"));
        setProperty("paintOpacity", 0.0);
        setProperty("artworkComposition", QStringLiteral("single-composited-button"));
        setProperty("lightArtwork", QStringLiteral("transparent-chevron"));
        setProperty("darkArtwork", QStringLiteral("gray-tile-chevron"));
    }

    void setDarkBackground(const bool value)
    {
        if (darkBackground == value)
        {
            setProperty("contrastStyle", darkBackground ? QStringLiteral("dark") : QStringLiteral("light"));
            setProperty("navigationStyle", darkBackground
                        ? QStringLiteral("dark-tinted")
                        : QStringLiteral("light-transparent"));
            return;
        }

        darkBackground = value;
        setProperty("contrastStyle", darkBackground ? QStringLiteral("dark") : QStringLiteral("light"));
        setProperty("navigationStyle", darkBackground
                    ? QStringLiteral("dark-tinted")
                    : QStringLiteral("light-transparent"));
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const qreal paintOpacity = qBound(0.0, property("paintOpacity").toReal(), 1.0);
        if (paintOpacity <= 0.001)
            return;

        // Compose the translucent bottom and opaque chevron at full strength
        // before applying the animated opacity once. Applying opacity to each
        // primitive separately makes their overlap accumulate alpha and gives
        // the two elements visibly different fade curves.
        const qreal dpr = devicePixelRatioF();
        QImage artwork(
            QSize(qMax(1, qCeil(width() * dpr)),
                  qMax(1, qCeil(height() * dpr))),
            QImage::Format_ARGB32_Premultiplied);
        artwork.setDevicePixelRatio(dpr);
        artwork.fill(Qt::transparent);
        QPainter artworkPainter(&artwork);
        artworkPainter.setRenderHint(QPainter::Antialiasing);

        const bool isHovered = underMouse() || isDown();
        if (darkBackground)
        {
            QColor background(128, 128, 128, isHovered ? 235 : 220);
            if (!isEnabled())
                background.setAlpha(100);
            artworkPainter.setPen(Qt::NoPen);
            artworkPainter.setBrush(background);
            artworkPainter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 10, 10);
        }
        // The light-background artwork intentionally has no hover tile. The
        // reference style is a transparent plate with only the chevron; the
        // entire button remains one composited artwork surface.

        QColor foreground = darkBackground ? QColor(48, 48, 48) : QColor(96, 96, 96);
        if (!isEnabled())
            foreground.setAlpha(90);

        QPainterPath chevron;
        const qreal centerX = width() / 2.0;
        const qreal centerY = height() / 2.0;
        const qreal direction = previous ? -1.0 : 1.0;
        chevron.moveTo(centerX - direction * 5.0, centerY - 12.0);
        chevron.lineTo(centerX + direction * 7.0, centerY);
        chevron.lineTo(centerX - direction * 5.0, centerY + 12.0);

        QPen pen(foreground, 4.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        artworkPainter.setPen(pen);
        artworkPainter.setBrush(Qt::NoBrush);
        artworkPainter.drawPath(chevron);
        artworkPainter.end();

        // Fade only the already-composited artwork. The widget backing remains
        // transparent, avoiding the rectangular SDR surface over HDR content.
        QPainter painter(this);
        painter.setOpacity(paintOpacity);
        painter.drawImage(QPointF(0.0, 0.0), artwork);
    }

private:
    const bool previous;
    bool darkBackground {false};
};

std::optional<qreal> sampledContentBrightness(const QVGraphicsView *graphicsView, const QPushButton *button)
{
    if (!graphicsView || !button || !graphicsView->isVisible())
        return {};

    const QPoint viewportCenter = graphicsView->viewport()->mapFrom(
        graphicsView,
        button->geometry().center());
    // Sample the cached, bounded SDR proxy through the production view
    // transform. QWidget::grab() repaints the viewport, which used to submit
    // two additional full HDR frames on every edge entry.
    return graphicsView->sampleDisplayedImageBrightness(viewportCenter);
}

class TitlebarBubble : public QLabel
{
public:
    using QLabel::QLabel;

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().brush(QPalette::Base));
        painter.drawRoundedRect(rect(), 8, 8);
        painter.end();

        QLabel::paintEvent(event);
    }
};
}

MainWindow::MainWindow(QWidget *parent, const QJsonObject &windowSessionState) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    // Keep the bundle icon for Finder and the Dock, but do not assign an icon to
    // the image window itself. On macOS a window icon is part of the document
    // proxy shown in the titlebar when a file path is associated with the window.
    setWindowIcon(QIcon());
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);

    // Allow the titlebar to overlap widgets with full size content view
    setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);
    centralWidget()->setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea, false);

    sessionStateToLoad = windowSessionState;
    lastActivated.start();

    // Initialize graphicsviewkDefaultBufferAlignment
    graphicsView = new QVGraphicsView(this);
    graphicsView->setObjectName(QStringLiteral("graphicsView"));
    centralWidget()->layout()->addWidget(graphicsView);

    initializeNavigationButtons();

    titlebarBubble = new TitlebarBubble(graphicsView);
    titlebarBubble->move(12, 4);
    titlebarBubble->setContentsMargins(8, 4, 8, 4);
    titlebarBubble->setForegroundRole(QPalette::Text);
    titlebarBubble->setAttribute(Qt::WA_TransparentForMouseEvents);
    titlebarBubbleOpacityEffect = new QGraphicsOpacityEffect(titlebarBubble);
    titlebarBubble->setGraphicsEffect(titlebarBubbleOpacityEffect);
    titlebarBubble->hide();

    titlebarBubbleHideTimer = new QTimer(this);
    titlebarBubbleHideTimer->setSingleShot(true);
    titlebarBubbleHideTimer->setInterval(3000);

    titlebarBubbleHideAnimation = new QPropertyAnimation(titlebarBubbleOpacityEffect, "opacity", this);
    titlebarBubbleHideAnimation->setDuration(250);
    titlebarBubbleHideAnimation->setStartValue(0.5);
    titlebarBubbleHideAnimation->setEndValue(0.0);

    connect(titlebarBubbleHideTimer, &QTimer::timeout, titlebarBubbleHideAnimation, [this]() {
        titlebarBubbleHideAnimation->start();
    });
    connect(titlebarBubbleHideAnimation, &QPropertyAnimation::finished, titlebarBubble, &QLabel::hide);

    // Hide fullscreen label by default
    ui->fullscreenLabel->hide();

    // Connect graphicsview signals
    connect(graphicsView, &QVGraphicsView::fileChanged, this, &MainWindow::fileChanged);
    connect(graphicsView, &QVGraphicsView::fileChanged, this, [this]() {
        if (!qEnvironmentVariableIsSet("FOVELLE_HDR_TEST_NAVIGATION"))
            return;
        // Opt-in system-test surface probe. Hold the real navigation widget at
        // a fractional paint opacity long enough for deterministic screen
        // capture, without moving the user's cursor or changing settings.
        const auto showNavigationProbe = [this]() {
            if (!nextImageButton)
                return;
            updateNavigationButtonGeometry();
            updateNavigationButtonAppearance();
            nextImageButtonAnimation->stop();
            setProperty("fovelleNavigationProbeActive", true);
            nextImageButton->setProperty("paintOpacity", 0.5);
            nextImageButtonRequestedVisible = true;
            if (graphicsView->usesNativeHDRNavigationOverlay()) {
                nextImageButton->hide();
                syncNavigationButtonOverlay(nextImageButton);
            } else {
                nextImageButton->show();
                nextImageButton->raise();
                nextImageButton->update();
            }
            const QPoint nativeOverlayOrigin = graphicsView->viewport()->mapFrom(
                    graphicsView, nextImageButton->geometry().topLeft());
            const QPoint origin = graphicsView->viewport()->mapToGlobal(
                    nativeOverlayOrigin);
            const QJsonObject event{
                { QStringLiteral("phase"), QStringLiteral("fractional-visible") },
                { QStringLiteral("global_x"), origin.x() },
                { QStringLiteral("global_y"), origin.y() },
                { QStringLiteral("viewport_x"), nativeOverlayOrigin.x() },
                { QStringLiteral("viewport_y"), nativeOverlayOrigin.y() },
                { QStringLiteral("width"), nextImageButton->width() },
                { QStringLiteral("height"), nextImageButton->height() },
                { QStringLiteral("paint_opacity"),
                  nextImageButton->property("paintOpacity").toDouble() },
                { QStringLiteral("has_graphics_effect"),
                  nextImageButton->graphicsEffect() != nullptr },
                { QStringLiteral("presentation_surface"),
                  graphicsView->usesNativeHDRNavigationOverlay()
                          ? QStringLiteral("metal-sublayer")
                          : QStringLiteral("qt-widget") },
                { QStringLiteral("qt_widget_visible"), nextImageButton->isVisible() },
            };
            qInfo().noquote() << "FOVELLE_NAV"
                              << QJsonDocument(event).toJson(QJsonDocument::Compact);
        };
        // Start only after all deterministic zoom/pan work and deferred
        // geometry have settled.
        QTimer::singleShot(8200, this, showNavigationProbe);
        QTimer::singleShot(9600, this, [this]() {
            if (!nextImageButton)
                return;
            nextImageButton->setProperty("paintOpacity", 1.0);
            syncNavigationButtonOverlay(nextImageButton);
            const QPoint nativeOverlayOrigin = graphicsView->viewport()->mapFrom(
                    graphicsView, nextImageButton->geometry().topLeft());
            const QPoint origin = graphicsView->viewport()->mapToGlobal(
                    nativeOverlayOrigin);
            qInfo().noquote() << "FOVELLE_NAV"
                              << QJsonDocument(QJsonObject{
                                     { QStringLiteral("phase"),
                                       QStringLiteral("fully-visible") },
                                     { QStringLiteral("global_x"), origin.x() },
                                     { QStringLiteral("global_y"), origin.y() },
                                     { QStringLiteral("viewport_x"), nativeOverlayOrigin.x() },
                                     { QStringLiteral("viewport_y"), nativeOverlayOrigin.y() },
                                     { QStringLiteral("width"), nextImageButton->width() },
                                     { QStringLiteral("height"), nextImageButton->height() },
                                     { QStringLiteral("paint_opacity"), 1.0 },
                                 }).toJson(QJsonDocument::Compact);
        });
        // Window-scoped HDR captures can take over a second on an XDR
        // desktop. Keep the probe visible well beyond the interaction settle
        // point so both comparison frames observe the same composition.
        QTimer::singleShot(10800, this, [this]() {
            if (!nextImageButton)
                return;
            setProperty("fovelleNavigationProbeActive", false);
            nextImageButtonRequestedVisible = false;
            nextImageButton->hide();
            nextImageButton->setProperty("paintOpacity", 0.0);
            syncNavigationButtonOverlay(nextImageButton);
            qInfo().noquote() << "FOVELLE_NAV"
                              << QJsonDocument(QJsonObject{
                                     { QStringLiteral("phase"),
                                       QStringLiteral("hidden") }
                                 }).toJson(QJsonDocument::Compact);
        });
    });
    connect(graphicsView, &QVGraphicsView::zoomLevelChanged, this, &MainWindow::zoomLevelChanged);
    connect(graphicsView, &QVGraphicsView::calculatedZoomModeChanged, this, &MainWindow::syncCalculatedZoomMode);
    connect(graphicsView, &QVGraphicsView::navigationResetsZoomChanged, this, &MainWindow::syncNavigationResetsZoom);
    connect(graphicsView, &QVGraphicsView::sortParametersChanged, this, &MainWindow::syncSortParameters);
    connect(graphicsView, &QVGraphicsView::cancelSlideshow, this, &MainWindow::cancelSlideshow);

    // Initialize escape shortcut
    escShortcut = new QShortcut(Qt::Key_Escape, this);
    connect(escShortcut, &QShortcut::activated, this, [this](){
        if (windowState().testFlag(Qt::WindowFullScreen))
            exitFullScreen();
        else
            close();
    });

    // Enable drag&dropping
    setAcceptDrops(true);

    // Make info dialog object
    info = new QVInfoDialog(this);

    // Timer for slideshow
    slideshowTimer = new QTimer(this);
    connect(slideshowTimer, &QTimer::timeout, this, &MainWindow::slideshowAction);

    // Timer for updating titlebar after zoom change
    zoomTitlebarUpdateTimer = new QTimer(this);
    zoomTitlebarUpdateTimer->setSingleShot(true);
    zoomTitlebarUpdateTimer->setInterval(50);
    connect(zoomTitlebarUpdateTimer, &QTimer::timeout, this, &MainWindow::buildWindowTitle);

    // Context menu
    auto &actionManager = qvApp->getActionManager();

    contextMenu = new QVMenu(this);
    contextMenu->setProperty("isContextMenu", true);

    actionManager.addCloneOfAction(contextMenu, "open");
    actionManager.addCloneOfAction(contextMenu, "openurl");
    contextMenu->addMenu(actionManager.buildRecentsMenu(contextMenu));
    contextMenu->addMenu(actionManager.buildOpenWithMenu(contextMenu));
    actionManager.addCloneOfAction(contextMenu, "openwithplaceholder");
    actionManager.addCloneOfAction(contextMenu, "opencontainingfolder");
    actionManager.addCloneOfAction(contextMenu, "showfileinfo");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "rename");
    actionManager.addCloneOfAction(contextMenu, "delete");
    contextMenu->addSeparator();
    actionManager.addCloneOfAction(contextMenu, "nextfile");
    actionManager.addCloneOfAction(contextMenu, "previousfile");
    contextMenu->addSeparator();
    contextMenu->addMenu(actionManager.buildSortMenu(contextMenu));
    contextMenu->addSeparator();
    contextMenu->addMenu(actionManager.buildViewMenu(contextMenu));
    contextMenu->addMenu(actionManager.buildToolsMenu(contextMenu));
    contextMenu->addMenu(actionManager.buildHelpMenu(contextMenu));

    connect(contextMenu, &QMenu::triggered, this, [this](QAction *triggeredAction){
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Initialize menubar
    setMenuBar(actionManager.buildMenuBar(this));
    // Stop actions conflicting with the window's actions
    const auto menubarActions = ActionManager::getAllNestedActions(menuBar()->actions());
    for (auto action : menubarActions)
    {
        action->setShortcutContext(Qt::WidgetShortcut);
    }
    connect(menuBar(), &QMenuBar::triggered, this, [this](QAction *triggeredAction){
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Add all actions to this window so keyboard shortcuts are always triggered
    // using virtual menu to hold them so i can connect to the triggered signal
    virtualMenu = new QMenu(this);
    const auto &actionKeys = actionManager.getActionLibrary().keys();
    for (const QString &key : actionKeys)
    {
        actionManager.addCloneOfAction(virtualMenu, key);
    }
    addActions(virtualMenu->actions());
    connect(virtualMenu, &QMenu::triggered, this, [this](QAction *triggeredAction){
        ActionManager::actionTriggered(triggeredAction, this);
    });

    // Enable actions related to having a window
    disableActions();

    // Connect functions to application components
    connect(&qvApp->getShortcutManager(), &ShortcutManager::shortcutsUpdated, this, &MainWindow::shortcutsUpdated);
    connect(&qvApp->getSettingsManager(), &SettingsManager::settingsUpdated, this, &MainWindow::settingsUpdated);
    settingsUpdated();
    shortcutsUpdated();

    // Timer for delayed-load Open With menu
    populateOpenWithTimer = new QTimer(this);
    populateOpenWithTimer->setSingleShot(true);
    populateOpenWithTimer->setInterval(250);
    connect(populateOpenWithTimer, &QTimer::timeout, this, &MainWindow::requestPopulateOpenWithMenu);

    // Connection for open with menu population futurewatcher
    connect(&openWithFutureWatcher, &QFutureWatcher<QList<OpenWith::OpenWithItem>>::finished, this, [this](){
        const QString completedFilePath = openWithFutureFilePath;
        openWithFutureFilePath.clear();

        if (!isClosing && completedFilePath == getCurrentFileDetails().fileInfo.absoluteFilePath())
            populateOpenWithMenu(openWithFutureWatcher.result());

        if (openWithPopulationPending && !isClosing)
        {
            openWithPopulationPending = false;
            QTimer::singleShot(0, this, &MainWindow::requestPopulateOpenWithMenu);
        }
    });

    QSettings settings;

    if (!sessionStateToLoad.isEmpty())
    {
        loadSessionState(sessionStateToLoad, true);
    }
    else
    {
        // Load window geometry
        restoreGeometry(settings.value("geometry").toByteArray());
    }

}

MainWindow::~MainWindow()
{
    populateOpenWithTimer->stop();
    openWithPopulationPending = false;

    // QtConcurrent::run() cannot be canceled. Wait for the Open With worker
    // before QApplication teardown so its QIcon/QPixmap work still has a live
    // QGuiApplication context.
    if (openWithFutureWatcher.isRunning())
        openWithFutureWatcher.waitForFinished();

    delete ui;
}

bool MainWindow::event(QEvent *event)
{
    const bool activated = event->type() == QEvent::WindowActivate
            || (event->type() == QEvent::ActivationChange && isActiveWindow());
    const bool deactivated = event->type() == QEvent::WindowDeactivate
            || (event->type() == QEvent::ActivationChange && !isActiveWindow());
    if (activated && !qvApp->getIsApplicationQuitting())
    {
        lastActivated.start();
        if (graphicsView)
            graphicsView->setHDRPresentationActive(true);
    }
    else if (deactivated && graphicsView)
        graphicsView->setHDRPresentationActive(false);
    return QMainWindow::event(event);
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    const bool isNavigationWidget =
        watched == graphicsView ||
        watched == graphicsView->viewport() ||
        watched == previousImageButton ||
        watched == nextImageButton;
    if (!isNavigationWidget)
        return QMainWindow::eventFilter(watched, event);

    QWidget *watchedWidget = qobject_cast<QWidget *>(watched);
    const auto navigationWindowRect = [this](QPushButton *button) {
        return QRect(graphicsView->mapTo(this, button->geometry().topLeft()), button->size());
    };
    const auto updateNativeHover = [this, &navigationWindowRect](const QPoint &position) {
        if (!graphicsView->usesNativeHDRNavigationOverlay())
            return;
        const bool previousHovered = previousImageButtonRequestedVisible
                && navigationWindowRect(previousImageButton).contains(position);
        const bool nextHovered = nextImageButtonRequestedVisible
                && navigationWindowRect(nextImageButton).contains(position);
        if (previousHovered == previousImageButtonHovered
            && nextHovered == nextImageButtonHovered)
            return;
        previousImageButtonHovered = previousHovered;
        nextImageButtonHovered = nextHovered;
        syncNavigationButtonOverlays();
    };
    const bool navigationProbeActive =
            property("fovelleNavigationProbeActive").toBool();
    if (event->type() == QEvent::MouseMove && watchedWidget)
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint watchedPosition = mouseEvent->position().toPoint();
#else
        const QPoint watchedPosition = mouseEvent->pos();
#endif
        const QPoint windowPosition = watchedWidget->mapTo(this, watchedPosition);
        if (!navigationProbeActive) {
            updateNavigationButtonVisibility(windowPosition);
            updateNativeHover(windowPosition);
        }
        if (pressedNavigationButton >= 0)
            return true;
    }
    else if (event->type() == QEvent::Enter)
    {
        if (!navigationProbeActive) {
            updateNavigationButtonVisibility(mapFromGlobal(QCursor::pos()));
            updateNativeHover(mapFromGlobal(QCursor::pos()));
        }
    }
    else if (event->type() == QEvent::Leave)
    {
        if (!navigationProbeActive) {
            updateNavigationButtonVisibility(mapFromGlobal(QCursor::pos()));
            updateNativeHover(mapFromGlobal(QCursor::pos()));
        }
    }
    else if ((event->type() == QEvent::MouseButtonPress
              || event->type() == QEvent::MouseButtonRelease)
             && watchedWidget && graphicsView->usesNativeHDRNavigationOverlay())
    {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const QPoint watchedPosition = mouseEvent->position().toPoint();
#else
        const QPoint watchedPosition = mouseEvent->pos();
#endif
        const QPoint windowPosition = watchedWidget->mapTo(this, watchedPosition);
        if (event->type() == QEvent::MouseButtonPress
            && mouseEvent->button() == Qt::LeftButton) {
            if (previousImageButtonRequestedVisible
                && navigationWindowRect(previousImageButton).contains(windowPosition))
                pressedNavigationButton = 0;
            else if (nextImageButtonRequestedVisible
                     && navigationWindowRect(nextImageButton).contains(windowPosition))
                pressedNavigationButton = 1;
            if (pressedNavigationButton >= 0) {
                updateNativeHover(windowPosition);
                syncNavigationButtonOverlays();
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease
                   && pressedNavigationButton >= 0) {
            const int releasedButton = pressedNavigationButton;
            const bool activate = mouseEvent->button() == Qt::LeftButton
                    && ((releasedButton == 0 && previousImageButtonRequestedVisible
                         && navigationWindowRect(previousImageButton).contains(windowPosition))
                        || (releasedButton == 1 && nextImageButtonRequestedVisible
                            && navigationWindowRect(nextImageButton).contains(windowPosition)));
            pressedNavigationButton = -1;
            updateNativeHover(windowPosition);
            syncNavigationButtonOverlays();
            if (activate) {
                if (releasedButton == 0)
                    previousFile();
                else
                    nextFile();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::isNavigationEdgeActive(const QPoint &windowPosition, const QRect &contentRect, const int edgeWidth)
{
    if (!contentRect.isValid() || !contentRect.contains(windowPosition))
        return false;

    const int boundedEdgeWidth = qBound(1, edgeWidth, qMax(1, contentRect.width() / 2));
    return windowPosition.x() < contentRect.left() + boundedEdgeWidth ||
        windowPosition.x() >= contentRect.right() - boundedEdgeWidth + 1;
}

int MainWindow::navigationEdgeWidth(const int windowWidth)
{
    return qMax(
        NavigationButtonActivationMinimumWidth,
        qCeil(windowWidth * NavigationButtonActivationPercentage / 100.0));
}

bool MainWindow::areNavigationButtonsSupported(const int windowWidth)
{
    return windowWidth >= NavigationButtonMinimumWindowWidth;
}

void MainWindow::initializeNavigationButtons()
{
    auto createButton = [this](const bool previous, const QString &objectName, const QString &accessibleName) {
        auto *button = new ImageNavigationButton(previous, graphicsView);
        button->setObjectName(objectName);
        button->setAccessibleName(accessibleName);
        button->setFixedSize(NavigationButtonSize, NavigationButtonSize);
        button->setProperty("transitionDurationMs", NavigationButtonAnimationDuration);
        button->hide();
        button->raise();
        button->installEventFilter(this);
        return button;
    };

    previousImageButton = createButton(true, QStringLiteral("previousImageButton"), tr("Previous File"));
    nextImageButton = createButton(false, QStringLiteral("nextImageButton"), tr("Next File"));

    connect(previousImageButton, &QPushButton::clicked, this, &MainWindow::previousFile);
    connect(nextImageButton, &QPushButton::clicked, this, &MainWindow::nextFile);

    auto createOpacityAnimation = [this](QPushButton *button, QPropertyAnimation **animation, const QString &objectName) {
        *animation = new QPropertyAnimation(button, "paintOpacity", this);
        (*animation)->setObjectName(objectName);
        (*animation)->setDuration(NavigationButtonAnimationDuration);
        connect(*animation, &QPropertyAnimation::valueChanged, button,
                [this, button]() {
                    button->update();
                    syncNavigationButtonOverlay(button);
                });
        connect(*animation, &QPropertyAnimation::finished, button, [this, button, animation]() {
            if ((*animation)->endValue().toReal() <= 0.0
                && button->property("paintOpacity").toReal() <= 0.001)
                button->hide();
            syncNavigationButtonOverlay(button);
        });
    };

    createOpacityAnimation(
        previousImageButton,
        &previousImageButtonAnimation,
        QStringLiteral("previousImageButtonOpacityAnimation"));
    createOpacityAnimation(
        nextImageButton,
        &nextImageButtonAnimation,
        QStringLiteral("nextImageButtonOpacityAnimation"));

    graphicsView->installEventFilter(this);
    graphicsView->viewport()->installEventFilter(this);
    updateNavigationButtonAppearance();
    updateNavigationButtonGeometry();
}

void MainWindow::updateNavigationButtonGeometry()
{
    if (!graphicsView || !previousImageButton || !nextImageButton)
        return;

    const ViewportPosition viewportPosition = getViewportPosition();
    const int contentTopInView = qBound(
        0,
        graphicsView->mapFrom(this, QPoint(0, viewportPosition.widgetY + viewportPosition.obscuredHeight)).y(),
        graphicsView->height());
    const int availableHeight = qMax(0, graphicsView->height() - contentTopInView);
    const int y = contentTopInView + qMax(0, (availableHeight - NavigationButtonSize) / 2);

    previousImageButton->setGeometry(NavigationButtonEdgeMargin, y, NavigationButtonSize, NavigationButtonSize);
    nextImageButton->setGeometry(
        graphicsView->width() - NavigationButtonEdgeMargin - NavigationButtonSize,
        y,
        NavigationButtonSize,
        NavigationButtonSize);
    previousImageButton->raise();
    nextImageButton->raise();
    syncNavigationButtonOverlays();
}

void MainWindow::updateNavigationButtonAppearance()
{
    const auto updateButton = [this](QPushButton *button) {
        const std::optional<qreal> brightness = sampledContentBrightness(graphicsView, button);
        const bool isDark = brightness.has_value() && brightness.value() < 0.5;
        button->setProperty("sampledContentBrightness", brightness.has_value() ? QVariant(brightness.value()) : QVariant());
        if (auto *navigationButton = dynamic_cast<ImageNavigationButton *>(button))
            navigationButton->setDarkBackground(isDark);
    };

    updateButton(previousImageButton);
    updateButton(nextImageButton);
    syncNavigationButtonOverlays();
}

void MainWindow::syncNavigationButtonOverlay(QPushButton *button)
{
    if (!graphicsView || !button)
        return;

    const int index = button == previousImageButton ? 0
            : button == nextImageButton ? 1 : -1;
    if (index < 0)
        return;

    const bool nativeOverlay = graphicsView->usesNativeHDRNavigationOverlay();
    const bool requestedVisible = index == 0
            ? previousImageButtonRequestedVisible
            : nextImageButtonRequestedVisible;
    if (!nativeOverlay) {
        if (requestedVisible && button->property("paintOpacity").toReal() > 0.001)
            button->show();
        button->update();
        return;
    }

    // Never leave an alien/raster QWidget above EDR pixels. Its transparent
    // corners resolve against Qt's SDR backing store instead of the sibling
    // CAMetalLayer. The shape-only overlay below is a CAMetalLayer sublayer.
    button->hide();
    const QPoint viewportOrigin = graphicsView->viewport()->mapFrom(
            graphicsView, button->geometry().topLeft());
    const QRectF viewportRect(viewportOrigin, button->size());
    const bool darkBackground =
            button->property("contrastStyle").toString() == QStringLiteral("dark");
    const bool hovered = index == 0
            ? previousImageButtonHovered : nextImageButtonHovered;
    graphicsView->setHDRNavigationOverlay(
            index, viewportRect,
            button->property("paintOpacity").toReal(),
            index == 0, darkBackground, hovered,
            pressedNavigationButton == index, button->isEnabled());
}

void MainWindow::syncNavigationButtonOverlays()
{
    if (!graphicsView)
        return;
    if (!graphicsView->usesNativeHDRNavigationOverlay()) {
        graphicsView->clearHDRNavigationOverlays();
        syncNavigationButtonOverlay(previousImageButton);
        syncNavigationButtonOverlay(nextImageButton);
        return;
    }
    syncNavigationButtonOverlay(previousImageButton);
    syncNavigationButtonOverlay(nextImageButton);
}

void MainWindow::setNavigationButtonVisible(
    QPushButton *button,
    QPropertyAnimation *animation,
    const bool visible)
{
    if (!button || !animation)
        return;

    bool &requestedVisible = button == previousImageButton
            ? previousImageButtonRequestedVisible
            : nextImageButtonRequestedVisible;
    if (requestedVisible == visible) {
        if (graphicsView->usesNativeHDRNavigationOverlay())
            button->hide();
        else if (visible && button->property("paintOpacity").toReal() > 0.001)
            button->show();
        syncNavigationButtonOverlay(button);
        return;
    }
    requestedVisible = visible;

    if (visible)
    {
        animation->stop();
        const bool nativeOverlay = graphicsView->usesNativeHDRNavigationOverlay();
        if (!nativeOverlay && button->property("paintOpacity").toReal() <= 0.001)
            button->show();
        if (nativeOverlay)
            button->hide();
        button->raise();
        animation->setStartValue(button->property("paintOpacity").toReal());
        animation->setEndValue(1.0);
        animation->start();
        return;
    }

    animation->stop();
    animation->setStartValue(button->property("paintOpacity").toReal());
    animation->setEndValue(0.0);
    animation->start();
}

void MainWindow::hideNavigationButtonsImmediately()
{
    if (!previousImageButton || !nextImageButton)
        return;

    previousImageButtonAnimation->stop();
    nextImageButtonAnimation->stop();
    previousImageButtonRequestedVisible = false;
    nextImageButtonRequestedVisible = false;
    previousImageButtonHovered = false;
    nextImageButtonHovered = false;
    pressedNavigationButton = -1;
    previousImageButton->setProperty("paintOpacity", 0.0);
    nextImageButton->setProperty("paintOpacity", 0.0);
    previousImageButton->update();
    nextImageButton->update();
    previousImageButton->hide();
    nextImageButton->hide();
    if (graphicsView)
        graphicsView->clearHDRNavigationOverlays();
}

void MainWindow::updateNavigationButtonVisibility(const QPoint &windowPosition)
{
    if (!areNavigationButtonsSupported(width()) ||
        !getIsPixmapLoaded() ||
        getCurrentFileDetails().folderFileInfoList.size() < 2)
    {
        hideNavigationButtonsImmediately();
        return;
    }

    updateNavigationButtonGeometry();
    const ViewportPosition viewportPosition = getViewportPosition();
    const int contentTop = viewportPosition.widgetY + viewportPosition.obscuredHeight;
    const QRect contentRect(0, contentTop, width(), qMax(0, height() - contentTop));
    const QRect previousWindowRect(
        graphicsView->mapTo(this, previousImageButton->geometry().topLeft()),
        previousImageButton->size());
    const QRect nextWindowRect(
        graphicsView->mapTo(this, nextImageButton->geometry().topLeft()),
        nextImageButton->size());
    const int activationWidth = navigationEdgeWidth(width());
    const bool leftVisible = contentRect.contains(windowPosition) &&
        (windowPosition.x() < contentRect.left() + activationWidth || previousWindowRect.contains(windowPosition));
    const bool rightVisible = contentRect.contains(windowPosition) &&
        (windowPosition.x() >= contentRect.right() - activationWidth + 1 || nextWindowRect.contains(windowPosition));

    if ((leftVisible && !previousImageButtonRequestedVisible)
        || (rightVisible && !nextImageButtonRequestedVisible))
        updateNavigationButtonAppearance();

    setNavigationButtonVisible(
        previousImageButton,
        previousImageButtonAnimation,
        leftVisible);
    setNavigationButtonVisible(
        nextImageButton,
        nextImageButtonAnimation,
        rightVisible);
}

void MainWindow::contextMenuEvent(QContextMenuEvent *event)
{
    // Qt is configured to send this after the real right-button release. The
    // Cocoa bridge can therefore start menu tracking without manufacturing a
    // second down/up pair or leaving Qt's pointer state pressed.
    event->accept();
    QVCocoaFunctions::showMenu(contextMenu);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QTimer::singleShot(0, this, [this]() {
        QVCocoaFunctions::setFullSizeContentView(this, true);
        QVCocoaFunctions::setWindowTheme(qvApp->getSettingsManager().getEnum<Qv::Theme>("theme"), windowHandle());
        if (!isFullScreen()
            && QSettings().value(QStringLiteral("options/titlebarhidden"), false).toBool())
            setTitlebarHidden(true, false);
    });

    if (!menuBar()->sizeHint().isEmpty())
    {
        ui->fullscreenLabel->setMargin(0);
        ui->fullscreenLabel->setMinimumHeight(menuBar()->sizeHint().height());
    }

    syncCalculatedZoomMode();
    syncNavigationResetsZoom();
    syncSortParameters();

    if (!sessionStateToLoad.isEmpty())
    {
        QTimer::singleShot(0, this, [this]() {
            loadSessionState(sessionStateToLoad, false);
            sessionStateToLoad = {};
        });
    }

    qvApp->addToActiveWindows(this);

    QMainWindow::showEvent(event);
    clearTitlebarIcons();
    updateNavigationButtonGeometry();
    updateNavigationButtonAppearance();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    isClosing = true;
    hideNavigationButtonsImmediately();

    QVCocoaFunctions::setFullSizeContentView(this, false);

    if (qvApp->getIsSessionStateSaveRequested())
        qvApp->addClosedWindowSessionState(getSessionState(), getLastActivatedTimestamp());

    QSettings settings;
    settings.setValue("geometry", saveGeometry());

    qvApp->deleteFromActiveWindows(this);
    qvApp->getActionManager().untrackClonedActions(contextMenu);
    qvApp->getActionManager().untrackClonedActions(menuBar());
    qvApp->getActionManager().untrackClonedActions(virtualMenu);

    QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        const auto *changeEvent = static_cast<QWindowStateChangeEvent*>(event);
        if (windowState().testFlag(Qt::WindowFullScreen) != changeEvent->oldState().testFlag(Qt::WindowFullScreen))
            fullscreenChanged();
    }
    else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)
    {
        // System Theme inherits the AppKit appearance. Recompute the
        // viewport and native titlebar immediately when macOS changes it.
        settingsUpdated();
    }

    QMainWindow::changeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateTitlebarBubbleText();
    updateNavigationButtonGeometry();
    updateNavigationButtonAppearance();
    if (!areNavigationButtonsSupported(width()))
        hideNavigationButtonsImmediately();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    static const bool sdrPerformanceLoggingEnabled =
            qEnvironmentVariableIsSet("FOVELLE_SDR_PERF_LOG");
    const bool logSDRPerformance = sdrPerformanceLoggingEnabled
            && !getCurrentFileDetails().isNativeHDRLoaded;
    QElapsedTimer paintTimer;
    if (logSDRPerformance)
        paintTimer.start();

    QPainter painter(this);

    const ViewportPosition viewportPos = getViewportPosition();
    const int adjustedViewportY = viewportPos.widgetY + viewportPos.obscuredHeight;
    const QRect headerRect = QRect(0, 0, width(), adjustedViewportY);
    const QRect viewportRect = rect().adjusted(0, adjustedViewportY, 0, 0);

    if (headerRect.isValid())
    {
        painter.eraseRect(headerRect);
    }

    if (viewportRect.isValid())
    {
        if (checkerboardBackground && getIsPixmapLoaded())
        {
            const int gridSize = 16;
            const QColor darkColor = QColor(204, 204, 204);
            const QColor lightColor = QColorConstants::White;
            const int numHorizontalSquares = (viewportRect.width() + (gridSize - 1)) / gridSize;
            const int numVerticalSquares = (viewportRect.height() + (gridSize - 1)) / gridSize;
            for (int iY = 0; iY < numVerticalSquares; iY++)
            {
                for (int iX = 0; iX < numHorizontalSquares; iX++)
                {
                    const bool isDarkSquare = (iX % 2) != (iY % 2);
                    painter.fillRect(
                        viewportRect.x() + (iX * gridSize),
                        viewportRect.y() + (iY * gridSize),
                        gridSize,
                        gridSize,
                        isDarkSquare ? darkColor : lightColor
                    );
                }
            }
        }
        else
        {
            const QColor &backgroundColor = customBackgroundColor.isValid() ? customBackgroundColor : painter.background().color();
            painter.fillRect(viewportRect, backgroundColor);

            if (getCurrentFileDetails().errorData.has_value())
            {
                const QVImageCore::ErrorData &errorData = getCurrentFileDetails().errorData.value();
                const QString errorMessage = tr("Error occurred opening\n%3\n%2 (Error %1)").arg(QString::number(errorData.errorNum), errorData.errorString, getCurrentFileDetails().fileInfo.fileName());
                painter.setFont(font());
                painter.setPen(Qv::getPerceivedBrightness(backgroundColor) > 0.5 ? QColorConstants::Black : QColorConstants::White);
                painter.drawText(viewportRect, errorMessage, QTextOption(Qt::AlignCenter));
            }
        }
    }

    if (logSDRPerformance)
    {
        qint64 dirtyArea = 0;
        for (const QRect &rect : event->region())
            dirtyArea += static_cast<qint64>(rect.width()) * rect.height();
        const qint64 windowArea = static_cast<qint64>(width()) * height();
        qInfo().noquote() << "FOVELLE_SDR_WINDOW_PAINT"
                          << "duration_ms=" << paintTimer.nsecsElapsed() / 1000000.0
                          << "dirty_rects=" << event->region().rectCount()
                          << "dirty_area=" << dirtyArea
                          << "window_area=" << windowArea
                          << "dirty_ratio="
                          << (windowArea > 0
                                  ? static_cast<qreal>(dirtyArea) / windowArea
                                  : 0.0)
                          << "dpr=" << devicePixelRatioF();
    }
}

void MainWindow::fullscreenChanged()
{
    const bool isFullscreen = windowState().testFlag(Qt::WindowFullScreen);

    if (isFullscreen)
        cancelFullScreenLayoutTransition();

    const auto fullscreenActions = qvApp->getActionManager().getAllClonesOfAction("fullscreen", this);
    for (const auto &fullscreenAction : fullscreenActions)
    {
        fullscreenAction->setText(isFullscreen ? tr("Exit F&ull Screen") : tr("Enter F&ull Screen"));
        const auto *menu = qobject_cast<const QMenu *>(fullscreenAction->parent());
        const bool isContextMenu = menu && menu->property("isContextMenu").toBool();
        const bool showIcon = isContextMenu
                ? qvApp->getShowContextMenuIcons()
                : qvApp->getShowMainMenuIcons();
        fullscreenAction->setIcon(showIcon
                ? qvApp->iconFromFont(isFullscreen ? Qv::MaterialIcon::FullscreenExit : Qv::MaterialIcon::Fullscreen)
                : QIcon());
        fullscreenAction->setIconVisibleInMenu(showIcon);
    }

    // The former "Show titlebar text in full screen" preference was removed;
    // fullscreen titlebar details are now always hidden.
    ui->fullscreenLabel->setVisible(false);

    if (!isFullscreen && storedTitlebarHidden)
    {
        setTitlebarHidden(true, false);
        storedTitlebarHidden = false;
    }

    if (!isFullscreen && activeFullScreenTitlebarOverlap >= 0)
    {
        activeFullScreenTitlebarOverlap =
            targetFullScreenTitlebarOverlap;
        graphicsView->fitOrConstrainImage();

        // AppKit has restored contentLayoutRect before Qt publishes the
        // WindowStateChange. Removing the override therefore keeps the same
        // effective inset and cannot expose a differently centered frame.
        activeFullScreenTitlebarOverlap = -1;
        graphicsView->fitOrConstrainImage();
    }

    updateMenuBarVisible();

    graphicsView->setCursorVisible(true);
}

void MainWindow::beginFullScreenLayoutTransition(
    const int titlebarOverlap, const int targetTitlebarOverlap)
{
    if (isClosing)
        return;

    activeFullScreenTitlebarOverlap = qMax(titlebarOverlap, 0);
    targetFullScreenTitlebarOverlap =
        qMax(targetTitlebarOverlap, 0);
}

void MainWindow::updateFullScreenLayoutTransition(const int titlebarOverlap)
{
    if (isClosing || activeFullScreenTitlebarOverlap < 0)
        return;

    const int boundedOverlap = qMax(titlebarOverlap, 0);
    if (boundedOverlap != activeFullScreenTitlebarOverlap)
    {
        activeFullScreenTitlebarOverlap = boundedOverlap;
        graphicsView->fitOrConstrainImage();
    }

    // Prepare the hidden real window before the proxy animation hands it back
    // to AppKit at the normal frame.
    graphicsView->viewport()->repaint();
    repaint();
}

void MainWindow::cancelFullScreenLayoutTransition()
{
    if (activeFullScreenTitlebarOverlap < 0)
        return;

    activeFullScreenTitlebarOverlap = -1;
    graphicsView->fitOrConstrainImage();
    update();
}

QRect MainWindow::fullScreenTransitionImageRect() const
{
    if (!getIsPixmapLoaded())
        return {};

    const QRect imageRect = graphicsView->fullScreenTransitionImageRect();
    if (imageRect.isEmpty())
        return {};

    return QRect(
        graphicsView->viewport()->mapTo(this, imageRect.topLeft()),
        imageRect.size());
}

QImage MainWindow::fullScreenTransitionImage() const
{
    return getIsPixmapLoaded()
        ? graphicsView->fullScreenTransitionImage() : QImage();
}

QColor MainWindow::fullScreenTransitionBackgroundColor() const
{
    return customBackgroundColor.isValid()
        ? customBackgroundColor : palette().color(QPalette::Window);
}

int MainWindow::fullScreenTransitionTitlebarOverlap() const
{
    return storedTitlebarHidden
        ? 0 : qMax(QVCocoaFunctions::getObscuredHeight(windowHandle()), 0);
}

void MainWindow::pauseChanged()
{
    const bool isPaused = getIsMovieLoaded() && graphicsView->getLoadedMovie().state() != QVMovie::Running;

    const auto pauseActions = qvApp->getActionManager().getAllClonesOfAction("pause", this);
    for (const auto &pauseAction : pauseActions)
    {
        pauseAction->setText(isPaused ? tr("Res&ume") : tr("Pa&use"));
        pauseAction->setIcon(qvApp->iconFromFont(isPaused ? Qv::MaterialIcon::PlayArrow : Qv::MaterialIcon::Pause));
    }
}

void MainWindow::openFile(const QString &fileName, const QString &baseDir)
{
    graphicsView->loadFile(fileName, baseDir);
    cancelSlideshow();
}

void MainWindow::settingsUpdated()
{
    auto &settingsManager = qvApp->getSettingsManager();

    buildWindowTitle();

    //theme
    const Qv::Theme theme = settingsManager.getEnum<Qv::Theme>("theme");
    customBackgroundColor = Qv::viewportBackgroundColor(QVCocoaFunctions::resolvedTheme(theme));

    //checkerboardbackground
    checkerboardBackground = settingsManager.getBoolean("checkerboardbackground");

    // menubarenabled
    menuBarEnabled = settingsManager.getBoolean("menubarenabled");

    // Apply the selected standard AppKit appearance to the native titlebar.
    QVCocoaFunctions::setWindowTheme(theme, windowHandle());

    //slideshow timer
    slideshowTimer->setInterval(static_cast<int>(settingsManager.getDouble("slideshowtimer")*1000));

    // The former "Show titlebar text in full screen" preference was removed;
    // fullscreen titlebar details are now always hidden.
    ui->fullscreenLabel->setVisible(false);

    updateMenuBarVisible();

    updateNavigationButtonGeometry();
    updateNavigationButtonAppearance();

    // repaint in case background color changed
    update();
}

void MainWindow::shortcutsUpdated()
{
    // Esc always exits fullscreen or closes this window.
    escShortcut->setKey(Qt::Key_Escape);
}

void MainWindow::openRecent(int i)
{
    const QString &filePath = qvApp->getActionManager().getRecentsList().value(i).filePath;
    if (!QFile::exists(filePath))
    {
        qvApp->getActionManager().auditRecentsList(true);
    }
    graphicsView->loadFile(filePath);
    cancelSlideshow();
}

void MainWindow::fileChanged(const bool isRestoringState)
{
    populateOpenWithTimer->start();
    disableActions();

    if (info->isVisible())
        refreshProperties();
    buildWindowTitle();
    clearTitlebarIcons();
    if (!isRestoringState)
        setWindowSize();
    pauseChanged();

    // full repaint to handle error message
    update();
    updateNavigationButtonGeometry();
    QTimer::singleShot(0, this, [this]() {
        updateNavigationButtonAppearance();
        updateNavigationButtonVisibility(mapFromGlobal(QCursor::pos()));
    });
}

void MainWindow::zoomLevelChanged()
{
    if (!zoomTitlebarUpdateTimer->isActive())
        zoomTitlebarUpdateTimer->start();
}

void MainWindow::syncCalculatedZoomMode()
{
    const bool isZoomToFit = graphicsView->getCalculatedZoomMode() == Qv::CalculatedZoomMode::ZoomToFit;
    const bool isFillWindow = graphicsView->getCalculatedZoomMode() == Qv::CalculatedZoomMode::FillWindow;
    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("zoomtofit", this))
        action->setChecked(isZoomToFit);
    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("fillwindow", this))
        action->setChecked(isFillWindow);
}

void MainWindow::syncNavigationResetsZoom()
{
    const bool value = graphicsView->getNavigationResetsZoom();
    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("navresetszoom", this))
        action->setChecked(value);
}

void MainWindow::syncSortParameters()
{
    const Qv::SortMode mode = graphicsView->getSortMode();
    const bool descending = graphicsView->getSortDescending();
    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("sortmode" + QString::number(static_cast<int>(mode)), this))
        action->setChecked(true);
    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("sortdirection" + QString::number(static_cast<int>(descending)), this))
        action->setChecked(true);
    buildWindowTitle();
}

void MainWindow::disableActions()
{
    const auto &actionLibrary = qvApp->getActionManager().getActionLibrary();
    for (const auto &action : actionLibrary)
    {
        const auto &data = action->data().toStringList();
        const auto &clonesOfAction = qvApp->getActionManager().getAllClonesOfAction(data.first(), this);

        // Enable this window's actions when a file is loaded
        if (data.last().contains("disable"))
        {
            for (const auto &clone : clonesOfAction)
            {
                const auto &cloneData = clone->data().toStringList();
                if (cloneData.last() == "disable")
                {
                    clone->setEnabled(getIsPixmapLoaded());
                }
                else if (cloneData.last() == "gifdisable")
                {
                    clone->setEnabled(getIsMovieLoaded());
                }
                else if (cloneData.last() == "undodisable")
                {
                    clone->setEnabled(!lastDeletedFiles.isEmpty() && !lastDeletedFiles.top().pathInTrash.isEmpty());
                }
                else if (cloneData.last() == "folderdisable")
                {
                    clone->setEnabled(!getCurrentFileDetails().folderFileInfoList.isEmpty());
                }
                else if (cloneData.last() == "windowdisable")
                {
                    clone->setEnabled(true);
                }
            }
        }
    }

    const auto &openWithMenus = qvApp->getActionManager().getAllClonesOfMenu("openwith", this);
    for (const auto &menu : openWithMenus)
    {
        menu->setEnabled(getIsPixmapLoaded());
        menu->menuAction()->setVisible(getIsPixmapLoaded());
    }

    const auto &openWithPlaceholderActions = qvApp->getActionManager().getAllClonesOfAction("openwithplaceholder", this);
    for (const auto &action : openWithPlaceholderActions)
    {
        action->setVisible(!getIsPixmapLoaded());
    }
}

void MainWindow::requestPopulateOpenWithMenu()
{
    if (isClosing)
        return;

    if (openWithFutureWatcher.isRunning())
    {
        openWithPopulationPending = true;
        return;
    }

    const QString filePath = getCurrentFileDetails().fileInfo.absoluteFilePath();
    openWithFutureFilePath = filePath;
    openWithFutureWatcher.setFuture(QtConcurrent::run(
        [filePath]() -> QList<OpenWith::OpenWithItem> {
            if (filePath.isEmpty()) return {};
            return OpenWith::getOpenWithItems(filePath);
        }
    ));
}

void MainWindow::populateOpenWithMenu(const QList<OpenWith::OpenWithItem> &openWithItems)
{
    for (int i = 0; i < qvApp->getActionManager().getOpenWithMaxLength(); i++)
    {
        const auto clonedActions = qvApp->getActionManager().getAllClonesOfAction("openwith" + QString::number(i), this);
        for (const auto &action : clonedActions)
        {
            // If we are within the bounds of the open with list
            if (i < openWithItems.length())
            {
                auto openWithItem = openWithItems.value(i);

                action->setVisible(true);
                action->setIconVisibleInMenu(false); // Hide icon temporarily to speed up updates in certain cases
                action->setText(openWithItem.name);
                if (qvApp->getShowSubmenuIcons())
                {
                    if (!openWithItem.iconName.isEmpty())
                        action->setIcon(QIcon::fromTheme(openWithItem.iconName));
                    else
                        action->setIcon(openWithItem.icon);
                }
                auto data = action->data().toList();
                data.replace(1, QVariant::fromValue(openWithItem));
                action->setData(data);
                if (qvApp->getShowSubmenuIcons())
                    action->setIconVisibleInMenu(true);
            }
            else
            {
                action->setVisible(false);
            }
        }
    }
}

void MainWindow::refreshProperties()
{
    const QVImageCore::FileDetails &fileDetails = getCurrentFileDetails();
    info->setInfo(
        fileDetails.fileInfo,
        fileDetails.baseImageSize,
        fileDetails.isMovieLoaded ? graphicsView->getLoadedMovie().frameCount() : 0
    );
}

void MainWindow::buildWindowTitle()
{
    QString newString = "Fovelle";
    if (getCurrentFileDetails().fileInfo.isFile())
    {
        const QVImageCore::FileDetails &fileDetails = getCurrentFileDetails();
        const bool hasError = fileDetails.errorData.has_value();
        auto getFileName = [&]() { return fileDetails.fileInfo.fileName(); };
        auto getZoomLevel = [&]() { return QString::number((hasError ? 1.0 : graphicsView->getZoomLevel()) * 100.0, 'f', 1) + "%"; };
        auto getImageIndex = [&]() { return QString::number(fileDetails.loadedIndexInFolder + 1); };
        auto getImageCount = [&]() { return QString::number(fileDetails.folderFileInfoList.count()); };
        auto getImageWidth = [&]() { return QString::number(hasError ? 0 : fileDetails.baseImageSize.width()); };
        auto getImageHeight = [&]() { return QString::number(hasError ? 0 : fileDetails.baseImageSize.height()); };
        auto getFileSize = [&]() { return QVInfoDialog::formatBytes(hasError ? 0 : fileDetails.fileInfo.size()); };
        switch (qvApp->getSettingsManager().getEnum<Qv::TitleBarText>("titlebarmode")) {
        case Qv::TitleBarText::Minimal:
        {
            newString = getFileName();
            break;
        }
        case Qv::TitleBarText::Practical:
        {
            newString = getFileName() + " - " + getImageIndex() + "/" + getImageCount();
            break;
        }
        case Qv::TitleBarText::Verbose:
        {
            newString = getFileName() + " - " + getImageIndex() + "/" + getImageCount() + " - " +
                        getImageWidth() + "x" + getImageHeight() + " - " + getFileSize() + " - " + getZoomLevel();
            break;
        }
        case Qv::TitleBarText::Custom:
        {
            newString = "";
            const QString customText = qvApp->getSettingsManager().getString("customtitlebartext");
            for (int i = 0; i < customText.length(); i++)
            {
                const QChar c = customText.at(i);
                if (c == '%')
                {
                    i++;
                    if (i >= customText.length()) break;
                    const QChar n = customText.at(i);
                    if (n == 'n') newString += getFileName();
                    else if (n == 'z') newString += getZoomLevel();
                    else if (n == 'i') newString += getImageIndex();
                    else if (n == 'c') newString += getImageCount();
                    else if (n == 'w') newString += getImageWidth();
                    else if (n == 'h') newString += getImageHeight();
                    else if (n == 's') newString += getFileSize();
                    else newString += n;
                }
                else newString += c;
            }
            break;
        }
        default:
            break;
        }
    }

    const bool titleChanged = newString != windowTitle();
    setWindowTitle(newString);

    // Update fullscreen label to titlebar text as well
    ui->fullscreenLabel->setText(newString);

    if (titleChanged)
        revealTitlebarBubble();
}

void MainWindow::clearTitlebarIcons()
{
    if (auto *handle = windowHandle())
    {
        // QWindow::setFilePath() maps to NSWindow.representedURL on macOS and
        // causes AppKit to display a document proxy icon in the titlebar.
        handle->setIcon(QIcon());
        handle->setFilePath(QString());
    }
}

void MainWindow::updateMenuBarVisible()
{
    menuBar()->setVisible(true);
}

void MainWindow::updateTitlebarBubbleText()
{
    const int horizontalMargin = titlebarBubble->pos().x() * 2;
    const int horizontalPadding = titlebarBubble->contentsMargins().left() + titlebarBubble->contentsMargins().right();
    const int availableTextWidth = qMax(graphicsView->width() - horizontalMargin - horizontalPadding, 0);
    titlebarBubble->setText(titlebarBubble->fontMetrics().elidedText(windowTitle(), Qt::ElideRight, availableTextWidth));
    titlebarBubble->adjustSize();
}

void MainWindow::revealTitlebarBubble()
{
    titlebarBubbleHideTimer->stop();
    titlebarBubbleHideAnimation->stop();

    const bool shouldShow = !windowTitle().isEmpty() && !slideshowTimer->isActive() && getTitlebarHidden();
    if (!shouldShow)
    {
        titlebarBubble->hide();
        return;
    }

    updateTitlebarBubbleText();
    titlebarBubbleOpacityEffect->setOpacity(titlebarBubbleHideAnimation->startValue().toDouble());
    titlebarBubble->show();
    titlebarBubbleHideTimer->start();
}

bool MainWindow::getWindowOnTop() const
{
    const QWindow *winHandle = windowHandle();
    return winHandle && winHandle->flags().testFlag(Qt::WindowStaysOnTopHint);
}

bool MainWindow::getTitlebarHidden() const
{
    if (!windowHandle())
        return false;

    return QVCocoaFunctions::getTitlebarHidden(this);
}

void MainWindow::setTitlebarHidden(const bool shouldHide, const bool persistPreference)
{
    if (!windowHandle())
        return;

    const auto customizeWindowFlags = [this](const Qt::WindowFlags flagsToChange, const bool on) {
        Qv::alterWindowFlags(this, [&](Qt::WindowFlags f) { return (on ? (f | flagsToChange) : (f & ~flagsToChange)) | Qt::CustomizeWindowHint; });
    };

    QVCocoaFunctions::setTitlebarHidden(this, shouldHide);
    customizeWindowFlags(Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint | Qt::WindowFullscreenButtonHint, !shouldHide);

    if (persistPreference)
    {
        QSettings settings;
        settings.setValue(QStringLiteral("options/titlebarhidden"), shouldHide);
        settings.sync();
    }

    const auto toggleTitlebarActions = qvApp->getActionManager().getAllClonesOfAction("toggletitlebar", this);
    for (const auto &toggleTitlebarAction : toggleTitlebarActions)
    {
        toggleTitlebarAction->setText(shouldHide ? tr("Show Title&bar") : tr("Hide Title&bar"));
    }

    clearTitlebarIcons();
    updateMenuBarVisible();
    revealTitlebarBubble();
    update();
    graphicsView->fitOrConstrainImage();
    updateNavigationButtonGeometry();
    updateNavigationButtonAppearance();
}

void MainWindow::setWindowSize(const bool isReapplying, const bool isExplicitRequest)
{
    if (!getIsPixmapLoaded())
        return;

    //check if the program is configured to resize the window
    const auto windowResizeMode = qvApp->getSettingsManager().getEnum<Qv::WindowResizeMode>("windowresizemode");
    const bool shouldResize =
        isExplicitRequest ||
        windowResizeMode == Qv::WindowResizeMode::WhenOpeningImages ||
        (windowResizeMode == Qv::WindowResizeMode::WhenLaunching && justLaunchedWithImage);
    if (!shouldResize)
        return;

    justLaunchedWithImage = false;

    //check if window is maximized or fullscreened
    if (windowState().testFlag(Qt::WindowMaximized) || windowState().testFlag(Qt::WindowFullScreen))
        return;

    const qreal minWindowResizedPercentage = qvApp->getSettingsManager().getInteger("minwindowresizedpercentage")/100.0;
    const qreal maxWindowResizedPercentage = qvApp->getSettingsManager().getInteger("maxwindowresizedpercentage")/100.0;

    // Try to grab the current screen
    QScreen *currentScreen = screenContaining(frameGeometry());
    // If completely offscreen, use first screen as fallback
    if (!currentScreen)
        currentScreen = QGuiApplication::screens().at(0);

    QSize extraWidgetsSize { 0, 0 };

    if (menuBar()->isVisible())
        extraWidgetsSize.rheight() += menuBar()->height();

    const int titlebarOverlap = getTitlebarOverlap();
    if (titlebarOverlap != 0)
        extraWidgetsSize.rheight() += titlebarOverlap;

    const QSize windowFrameSize = frameGeometry().size() - geometry().size();
    const QSize hardLimitSize = currentScreen->availableSize() - windowFrameSize - extraWidgetsSize;
    const QSize screenSize = currentScreen->size();
    const QSize minWindowSize = (screenSize * minWindowResizedPercentage).boundedTo(hardLimitSize);
    const QSize maxWindowSize = (screenSize * qMax(maxWindowResizedPercentage, minWindowResizedPercentage)).boundedTo(hardLimitSize);
    const bool isZoomFixed = (!graphicsView->getNavigationResetsZoom() || isReapplying) && !graphicsView->getCalculatedZoomMode().has_value();
    const QSizeF imageSize = graphicsView->getEffectiveOriginalSize() * (isZoomFixed ? graphicsView->getZoomLevel() : 1.0);
    const int fitOverscan = graphicsView->getFitOverscan();
    const QSize fitOverscanSize = QSize(fitOverscan * 2, fitOverscan * 2);
    const LogicalPixelFitter fitter = graphicsView->getPixelFitter();
    const bool enforceMinSizeBothDimensions = false;

    QSize targetSize = fitter.snapSize(imageSize) - fitOverscanSize;

    const bool limitToMin = targetSize.width() < minWindowSize.width() && targetSize.height() < minWindowSize.height();
    const bool limitToMax = targetSize.width() > maxWindowSize.width() || targetSize.height() > maxWindowSize.height();
    if (limitToMin || limitToMax)
    {
        const QSizeF enforcedSize = fitter.unsnapSize(limitToMin ? minWindowSize : maxWindowSize) + fitOverscanSize;
        const qreal fitRatio = qMin(enforcedSize.width() / imageSize.width(), enforcedSize.height() / imageSize.height());
        targetSize = fitter.snapSize(imageSize * fitRatio) - fitOverscanSize;
    }

    if (enforceMinSizeBothDimensions)
        targetSize = targetSize.expandedTo(minWindowSize);

    const bool recenterImage = isZoomFixed && geometry().size() != targetSize + extraWidgetsSize;

    const auto afterMatchingSizeMode = qvApp->getSettingsManager().getEnum<Qv::AfterMatchingSize>("aftermatchingsizemode");
    const QPoint referenceCenter =
        afterMatchingSizeMode == Qv::AfterMatchingSize::CenterOnPrevious ? geometry().center() :
        afterMatchingSizeMode == Qv::AfterMatchingSize::CenterOnScreen ? currentScreen->availableGeometry().center() :
        QPoint();

    // Resize window first, reposition later
    // This is smoother than a single geometry set for some reason
    resize(targetSize + extraWidgetsSize);
    QRect newRect = geometry();

    if (afterMatchingSizeMode != Qv::AfterMatchingSize::AvoidRepositioning)
        newRect.moveCenter(referenceCenter);

    // Ensure titlebar is not above or below the available screen area
    const QRect availableScreenRect = currentScreen->availableGeometry();
    const int topFrameHeight = geometry().top() - frameGeometry().top();
    const int windowMinY = availableScreenRect.top() + topFrameHeight;
    const int windowMaxY = availableScreenRect.top() + availableScreenRect.height() - titlebarOverlap;
    if (newRect.top() < windowMinY)
        newRect.moveTop(windowMinY);
    if (newRect.top() > windowMaxY)
        newRect.moveTop(windowMaxY);

    // Reposition window
    setGeometry(newRect);

    if (recenterImage)
        graphicsView->centerImage();
}

// Initially copied from Qt source code (QGuiApplication::screenAt) and then customized
QScreen *MainWindow::screenContaining(const QRect &rect)
{
    QScreen *bestScreen = nullptr;
    int bestScreenArea = 0;
    QVarLengthArray<const QScreen *, 8> visitedScreens;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *screen : screens) {
        if (visitedScreens.contains(screen))
            continue;
        // The virtual siblings include the screen itself, so iterate directly
        const auto siblings = screen->virtualSiblings();
        for (QScreen *sibling : siblings) {
            const QRect intersect = sibling->geometry().intersected(rect);
            const int area = intersect.width() * intersect.height();
            if (area > bestScreenArea) {
                bestScreen = sibling;
                bestScreenArea = area;
            }
            visitedScreens.append(sibling);
        }
    }
    return bestScreen;
}

const QJsonObject MainWindow::getSessionState() const
{
    QJsonObject state;

    state["geometry"] = QString(saveGeometry().toBase64());

    state["windowOnTop"] = getWindowOnTop();

    state["titlebarHidden"] = getTitlebarHidden();

    if (getIsPixmapLoaded())
    {
        state["path"] = getCurrentFileDetails().fileInfo.absoluteFilePath();

        if (getCurrentFileDetails().folderFileInfoList.getIsRecursive())
            state["baseDir"] = getCurrentFileDetails().folderFileInfoList.getBaseDir();
    }

    state["graphicsView"] = graphicsView->getSessionState();

    return state;
}

void MainWindow::loadSessionState(const QJsonObject &state, const bool isInitialPhase)
{
    if (isInitialPhase)
    {
        restoreGeometry(QByteArray::fromBase64(state["geometry"].toString().toUtf8()));

        graphicsView->loadSessionState(state["graphicsView"].toObject());

        return;
    }

    if (state["windowOnTop"].toBool() != getWindowOnTop())
        toggleWindowOnTop();

    if (state["titlebarHidden"].toBool() != getTitlebarHidden())
        toggleTitlebarHidden();

    const QString path = state["path"].toString();
    const QString baseDir = state.contains("baseDir") ? state["baseDir"].toString() : "";
    if (!path.isEmpty())
    {
        graphicsView->setLoadIsFromSessionRestore(true);
        openFile(path, baseDir);
    }
}

void MainWindow::setJustLaunchedWithImage(bool value)
{
    justLaunchedWithImage = value;
}

void MainWindow::openUrl(const QUrl &url)
{
    if (!url.isValid()) {
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Error: URL is invalid"), QMessageBox::Ok, this);
        return;
    }

    auto request = QNetworkRequest(url);
    auto *reply = networkAccessManager.get(request);
    auto *progressDialog = new QProgressDialog(tr("Downloading image..."), tr("Cancel"), 0, 100);
    progressDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    progressDialog->setAutoClose(false);
    progressDialog->setAutoReset(false);
    progressDialog->setWindowTitle(tr("Open URL..."));
    NativeDialogs::applyTheme(progressDialog);
    progressDialog->open();

    connect(progressDialog, &QProgressDialog::canceled, reply, [reply]{
        reply->abort();
    });

    connect(reply, &QNetworkReply::downloadProgress, progressDialog, [progressDialog](qreal bytesReceived, qreal bytesTotal){
        auto percent = (bytesReceived/bytesTotal)*100;
        progressDialog->setValue(qRound(percent));
    });

    connect(reply, &QNetworkReply::finished, progressDialog, [progressDialog, reply, this]{
        if (reply->error())
        {
            progressDialog->close();
            NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Error ") + QString::number(reply->error()) + ": " + reply->errorString(), QMessageBox::Ok, this);

            progressDialog->deleteLater();
            return;
        }

        progressDialog->setMaximum(0);

        auto *tempFile = new QTemporaryFile(this);
        tempFile->setFileTemplate(QDir::tempPath() + "/" + qvApp->applicationName() + ".XXXXXX.png");

        auto *saveFutureWatcher = new QFutureWatcher<bool>();
        connect(saveFutureWatcher, &QFutureWatcher<bool>::finished, this, [progressDialog, tempFile, saveFutureWatcher, this](){
            progressDialog->close();
            if (saveFutureWatcher->result())
            {
                if (tempFile->open())
                {
                    openFile(tempFile->fileName());
                }
            }
            else
            {
                NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Error: Invalid image"), QMessageBox::Ok, this);
                tempFile->deleteLater();
            }
            progressDialog->deleteLater();
            saveFutureWatcher->deleteLater();
        });

        saveFutureWatcher->setFuture(QtConcurrent::run([reply, tempFile]{
            return QImage::fromData(reply->readAll()).save(tempFile, "png");
        }));
    });
}

void MainWindow::pickUrl()
{
    auto inputDialog = new QInputDialog(this);
    inputDialog->setWindowTitle(tr("Open URL..."));
    inputDialog->setLabelText(tr("URL of a supported image file:"));
    inputDialog->resize(350, inputDialog->height());
    inputDialog->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    NativeDialogs::applyTheme(inputDialog);
    connect(inputDialog, &QInputDialog::finished, this, [inputDialog, this](int result) {
        if (result)
        {
            const auto url = QUrl(inputDialog->textValue());
            openUrl(url);
        }
        inputDialog->deleteLater();
    });
    inputDialog->open();
}

void MainWindow::reloadFile()
{
    graphicsView->reloadFile();
}

void MainWindow::openWith(const OpenWith::OpenWithItem &openWithItem)
{
    OpenWith::openWith(getCurrentFileDetails().fileInfo.absoluteFilePath(), openWithItem);
}

void MainWindow::openContainingFolder()
{
    if (!getIsPixmapLoaded())
        return;

    const QFileInfo selectedFileInfo = getCurrentFileDetails().fileInfo;

    QProcess::execute("open", QStringList() << "-R" << selectedFileInfo.absoluteFilePath());
}

void MainWindow::showFileInfo()
{
    refreshProperties();
    info->show();
    info->raise();
    info->activateWindow();
}

void MainWindow::askDeleteFile(bool permanent)
{
    if (!permanent && !qvApp->getSettingsManager().getBoolean("askdelete"))
    {
        deleteFile(permanent);
        return;
    }

    const QFileInfo &fileInfo = getCurrentFileDetails().fileInfo;
    const QString fileName = getCurrentFileDetails().fileInfo.fileName();

    if (!fileInfo.isWritable())
    {
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Can't delete %1:\nNo write permission or file is read-only.").arg(fileName), QMessageBox::Ok, this);
        return;
    }

    QString messageText;
    if (permanent)
    {
        messageText = tr("Are you sure you want to permanently delete %1? This can't be undone.").arg(fileName);
    }
    else
    {
        messageText = tr("Are you sure you want to move %1 to the Trash?").arg(fileName);
    }

    auto *msgBox = NativeDialogs::createMessageBox(QMessageBox::Question, tr("Delete"), messageText,
                       QMessageBox::Yes | QMessageBox::No, this);
    if (!permanent)
        msgBox->setCheckBox(new QCheckBox(tr("Do not ask again")));

    connect(msgBox, &QMessageBox::finished, this, [this, msgBox, permanent](int result){
        if (result != QMessageBox::Yes)
            return;

        if (!permanent)
        {
            QSettings settings;
            settings.beginGroup("options");
            settings.setValue("askdelete", !msgBox->checkBox()->isChecked());
            qvApp->getSettingsManager().loadSettings();
        }
        this->deleteFile(permanent);
    });

    msgBox->open();
}

void MainWindow::deleteFile(bool permanent)
{
    const QFileInfo &fileInfo = getCurrentFileDetails().fileInfo;
    const QString filePath = fileInfo.absoluteFilePath();
    const QString fileName = fileInfo.fileName();

    graphicsView->closeImage(true);

    bool success;
    QString trashFilePath;
    if (permanent)
    {
        success = QFile::remove(filePath);
    }
    else
    {
        QFile file(filePath);
        success = file.moveToTrash();
        if (success)
            trashFilePath = file.fileName();
    }

    if (!success || QFile::exists(filePath))
    {
        openFile(filePath);
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Can't delete %1.").arg(fileName), QMessageBox::Ok, this);
        return;
    }

    qvApp->getActionManager().auditRecentsList(true);
    qvApp->invalidateFolderListings();

    auto afterDelete = qvApp->getSettingsManager().getEnum<Qv::AfterDelete>("afterdelete");
    if (afterDelete == Qv::AfterDelete::MoveForward)
        nextFile();
    else if (afterDelete == Qv::AfterDelete::MoveBack)
        previousFile();

    if (!trashFilePath.isEmpty())
        lastDeletedFiles.push({trashFilePath, filePath});

    disableActions();
}

void MainWindow::undoDelete()
{
    if (lastDeletedFiles.isEmpty())
        return;

    const DeletedPaths lastDeletedFile = lastDeletedFiles.pop();
    // Update the Restore from Trash action now that its history has changed
    disableActions();

    if (lastDeletedFile.pathInTrash.isEmpty() || lastDeletedFile.previousPath.isEmpty())
        return;

    const QFileInfo fileInfo(lastDeletedFile.pathInTrash);
    if (!fileInfo.isWritable())
    {
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Can't undo deletion of %1:\n"
                                                    "No write permission or file is read-only.").arg(fileInfo.fileName()), QMessageBox::Ok, this);
        return;
    }

    bool success = QFile::rename(lastDeletedFile.pathInTrash, lastDeletedFile.previousPath);
    if (!success)
    {
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"), tr("Failed undoing deletion of %1.").arg(fileInfo.fileName()), QMessageBox::Ok, this);
        return;
    }

    qvApp->invalidateFolderListings();
    openFile(lastDeletedFile.previousPath);
}

void MainWindow::copy()
{
    auto *mimeData = graphicsView->getMimeData();
    if (!mimeData->hasImage() || !mimeData->hasUrls())
    {
        mimeData->deleteLater();
        return;
    }

    QApplication::clipboard()->setMimeData(mimeData);
}

void MainWindow::paste()
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    if (mimeData == nullptr)
        return;

    if (mimeData->hasText())
    {
        auto url = QUrl(mimeData->text());

        if (url.isValid() && (url.scheme() == "http" || url.scheme() == "https"))
        {
            openUrl(url);
            return;
        }
    }

    graphicsView->loadMimeData(mimeData);
}

void MainWindow::rename()
{
    if (!getIsPixmapLoaded())
        return;

    auto *renameDialog = new QVRenameDialog(this, getCurrentFileDetails().fileInfo);
    connect(renameDialog, &QVRenameDialog::newFileToOpen, this, [this](const QString &filePath) {
        qvApp->invalidateFolderListings();
        openFile(filePath);
    });
    connect(renameDialog, &QVRenameDialog::readyToRenameFile, this, [this]() {
        if (auto device = graphicsView->getLoadedMovie().device()) {
            device->close();
        }
    });

    renameDialog->open();
}

void MainWindow::zoomIn()
{
    graphicsView->zoomIn();
}

void MainWindow::zoomOut()
{
    graphicsView->zoomOut();
}

void MainWindow::zoomCustom()
{
    bool ok;
    const double oldValue = graphicsView->getZoomLevel() * 100.0;
    const double newValue = NativeDialogs::getDouble(
        this, tr("Set Zoom Level"), tr("Zoom Level (%):"), oldValue,
        Qv::MinimumZoomLevel * 100.0, Qv::MaximumZoomLevel * 100.0, 1, &ok);
    if (!ok) return;
    graphicsView->zoomAbsolute(newValue / 100.0, Qv::CalculateViewportCenterPos);
    graphicsView->fitOrConstrainImage();
}

void MainWindow::originalSize()
{
    graphicsView->setCalculatedZoomMode(Qv::CalculatedZoomMode::OriginalSize);
}

void MainWindow::setZoomToFit(const bool value)
{
    graphicsView->setCalculatedZoomMode(value ? std::optional(Qv::CalculatedZoomMode::ZoomToFit) : std::nullopt);
}

void MainWindow::setFillWindow(const bool value)
{
    graphicsView->setCalculatedZoomMode(value ? std::optional(Qv::CalculatedZoomMode::FillWindow) : std::nullopt);
}

void MainWindow::setNavigationResetsZoom(const bool value)
{
    graphicsView->setNavigationResetsZoom(value);
}

void MainWindow::setSortMode(const Qv::SortMode mode)
{
    graphicsView->setSortMode(mode);
}

void MainWindow::setSortDescending(const bool descending)
{
    graphicsView->setSortDescending(descending);
}

void MainWindow::rotateRight()
{
    graphicsView->rotateImage(90);
    graphicsView->fitOrConstrainImage();
    setWindowSize(true);
}

void MainWindow::rotateLeft()
{
    graphicsView->rotateImage(-90);
    graphicsView->fitOrConstrainImage();
    setWindowSize(true);
}

void MainWindow::mirror()
{
    graphicsView->mirrorImage();
    graphicsView->fitOrConstrainImage();
}

void MainWindow::flip()
{
    graphicsView->flipImage();
    graphicsView->fitOrConstrainImage();
}

void MainWindow::resetTransformation()
{
    graphicsView->resetTransformation();
    graphicsView->fitOrConstrainImage();
    setWindowSize(true);
}

void MainWindow::firstFile()
{
    graphicsView->goToFile(Qv::GoToFileMode::First);
}

void MainWindow::previousFile()
{
    graphicsView->goToFile(Qv::GoToFileMode::Previous);
}

void MainWindow::nextFile()
{
    graphicsView->goToFile(Qv::GoToFileMode::Next);
}

void MainWindow::lastFile()
{
    graphicsView->goToFile(Qv::GoToFileMode::Last);
}

void MainWindow::randomFile()
{
    graphicsView->goToFile(Qv::GoToFileMode::Random);
}

void MainWindow::saveFrameAs()
{
    QSettings settings;
    settings.beginGroup("recents");
    if (!getIsMovieLoaded())
        return;

    if (graphicsView->getLoadedMovie().state() == QVMovie::Running)
    {
        pause();
    }
    QFileDialog *saveDialog = new QFileDialog(this, tr("Save Frame As..."));
    saveDialog->setDirectory(settings.value("lastFileDialogDir", QDir::homePath()).toString());
    saveDialog->setNameFilters(qvApp->getNameFilterList());
    saveDialog->selectFile(getCurrentFileDetails().fileInfo.baseName() + "-" + QString::number(graphicsView->getLoadedMovie().currentFrameNumber()) + ".png");
    saveDialog->setDefaultSuffix("png");
    saveDialog->setAcceptMode(QFileDialog::AcceptSave);
    NativeDialogs::applyTheme(saveDialog);
    saveDialog->open();
    connect(saveDialog, &QFileDialog::fileSelected, this, [this](const QString &fileName){
        if (!graphicsView->getLoadedMovie().currentImage().save(fileName, nullptr, 100))
            return;
        qvApp->invalidateFolderListings();
    });
}

void MainWindow::pause()
{
    if (!getIsMovieLoaded())
        return;

    const bool isPausing = graphicsView->getLoadedMovie().state() == QVMovie::Running;
    graphicsView->setPaused(isPausing);
    pauseChanged();
}

void MainWindow::nextFrame()
{
    graphicsView->jumpToNextFrame();
    pauseChanged();
}

void MainWindow::previousFrame()
{
    graphicsView->jumpToPreviousFrame();
    pauseChanged();
}

void MainWindow::toggleSlideshow()
{
    const bool isStarting = !slideshowTimer->isActive();
    if (isStarting)
        slideshowTimer->start();
    else
        slideshowTimer->stop();
    const auto slideshowActions = qvApp->getActionManager().getAllClonesOfAction("slideshow", this);
    for (const auto &slideshowAction : slideshowActions)
    {
        slideshowAction->setText(isStarting ? tr("Stop S&lideshow") : tr("Start S&lideshow"));
        slideshowAction->setIcon(qvApp->iconFromFont(isStarting ? Qv::MaterialIcon::CancelPresentation : Qv::MaterialIcon::Slideshow));
    }
}

void MainWindow::cancelSlideshow()
{
    if (slideshowTimer->isActive())
        toggleSlideshow();
}

void MainWindow::slideshowAction()
{
    switch (qvApp->getSettingsManager().getEnum<Qv::SlideshowDirection>("slideshowdirection"))
    {
    case Qv::SlideshowDirection::Forward:
        nextFile();
        break;
    case Qv::SlideshowDirection::Backward:
        previousFile();
        break;
    case Qv::SlideshowDirection::Random:
        randomFile();
        break;
    }
}

void MainWindow::decreaseSpeed()
{
    if (!getIsMovieLoaded())
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed()-25);
}

void MainWindow::resetSpeed()
{
    if (!getIsMovieLoaded())
        return;

    graphicsView->setSpeed(100);
}

void MainWindow::increaseSpeed()
{
    if (!getIsMovieLoaded())
        return;

    graphicsView->setSpeed(graphicsView->getLoadedMovie().speed()+25);
}

void MainWindow::exitFullScreen()
{
    if (!windowState().testFlag(Qt::WindowFullScreen))
        return;

    // Escape is handled by AppKit's native full-screen action. Route the View
    // command through the same asynchronous action and let Qt publish the
    // resulting state only after NSWindowDidExitFullScreenNotification.
    QVCocoaFunctions::requestFullScreenExit(windowHandle());
}

void MainWindow::toggleFullScreen()
{
    if (windowState().testFlag(Qt::WindowFullScreen))
    {
        exitFullScreen();
    }
    else
    {
        // Restore the titlebar before entering fullscreen because macOS may apply special titlebar handling.
        storedTitlebarHidden = getTitlebarHidden();
        if (storedTitlebarHidden)
            setTitlebarHidden(false, false);

        showFullScreen();
    }
}

void MainWindow::toggleWindowOnTop()
{
    if (!windowHandle())
        return;

    const bool targetValue = !getWindowOnTop();

    Qv::alterWindowFlags(this, [&](Qt::WindowFlags f) { return f.setFlag(Qt::WindowStaysOnTopHint, targetValue); });

    if (info->windowHandle())
        Qv::alterWindowFlags(info, [&](Qt::WindowFlags f) { return f.setFlag(Qt::WindowStaysOnTopHint, targetValue); });

    for (const auto &action : qvApp->getActionManager().getAllClonesOfAction("windowontop", this))
        action->setChecked(targetValue);

    // Make sure window still participates in Mission Control
    QVCocoaFunctions::setWindowCollectionBehaviorManaged(this);

    emit qvApp->windowOnTopChanged();
}

void MainWindow::toggleTitlebarHidden()
{
    if (windowState().testFlag(Qt::WindowFullScreen))
        return;

    setTitlebarHidden(!getTitlebarHidden());
}

int MainWindow::getTitlebarOverlap() const
{
    if (activeFullScreenTitlebarOverlap >= 0)
        return activeFullScreenTitlebarOverlap;

    // To account for fullsizecontentview on mac
    return QVCocoaFunctions::getObscuredHeight(window()->windowHandle());
}

MainWindow::ViewportPosition MainWindow::getViewportPosition() const
{
    ViewportPosition result;
    // This accounts for anything that may be above the viewport such as the menu bar (if it's inside
    // the window) and/or the label that displays titlebar text in full screen mode.
    result.widgetY = windowHandle() ? graphicsView->mapTo(this, QPoint()).y() : 0;
    // On macOS, part of the viewport may be additionally covered with the window's translucent
    // titlebar due to full size content view.
    result.obscuredHeight = qMax(getTitlebarOverlap() - result.widgetY, 0);
    return result;
}
