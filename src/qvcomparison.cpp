#include "qvcomparison.h"

#include "qvgraphicsview.h"
#include "qvcocoafunctions.h"
#include "mainwindow.h"
#include "nativedialogs.h"
#include "qvapplication.h"
#include "qvimageloader.h"

#include <QAccessible>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
constexpr int ComparisonHandleWidth = 8;
constexpr qreal ComparisonStrokeWidth = 2;

class ComparisonDividerHandle final : public QSplitterHandle
{
public:
    explicit ComparisonDividerHandle(QSplitter *parent)
        : QSplitterHandle(Qt::Horizontal, parent) { }

    void cancelDrag()
    {
        if (!pointerOffset)
            return;
        pointerOffset.reset();
        guide.hide();
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));
        if (!pointerOffset) {
            const qreal x = (width() - ComparisonStrokeWidth) / 2;
            painter.fillRect(QRectF(x, 0, ComparisonStrokeWidth, height()),
                             palette().color(QPalette::Mid));
        }
    }

    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::UngrabMouse || event->type() == QEvent::Hide)
            cancelDrag();
        return QSplitterHandle::event(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        pointerOffset = event->pos().x();
        setFocus(Qt::MouseFocusReason);
        showGuide(x());
        update();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!pointerOffset || !event->buttons().testFlag(Qt::LeftButton)) {
            cancelDrag();
            event->ignore();
            return;
        }
        showGuide(dragPosition(event));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!pointerOffset || event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }
        const int position = dragPosition(event);
        cancelDrag();
        moveSplitter(position);
        event->accept();
    }

private:
    int dragPosition(const QMouseEvent *event)
    {
        const int pointerX = splitter()->mapFromGlobal(event->globalPosition().toPoint()).x();
        return closestLegalPosition(pointerX - *pointerOffset);
    }

    void showGuide(int handleLeft)
    {
        const QRect bounds = splitter()->contentsRect();
        const qreal x = handleLeft + (width() - ComparisonStrokeWidth) / 2;
        guide.show(splitter(), QRectF(x, bounds.y(), ComparisonStrokeWidth, bounds.height()),
                   palette().color(QPalette::Highlight));
    }

    std::optional<int> pointerOffset;
    QVCocoaFunctions::ComparisonDividerGuide guide;
};

class ComparisonSplitter final : public QSplitter
{
public:
    explicit ComparisonSplitter(QWidget *parent) : QSplitter(Qt::Horizontal, parent)
    {
        setHandleWidth(ComparisonHandleWidth);
    }

protected:
    QSplitterHandle *createHandle() override { return new ComparisonDividerHandle(this); }
};

QVBoxLayout *paneLayout(QWidget *pane)
{
    auto *layout = new QVBoxLayout(pane);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return layout;
}

bool hasImage(const QVGraphicsView *view)
{
    return view && view->getCurrentFileDetails().isPixmapLoaded;
}

QString zoomLabel(qreal factor)
{
    return QString::number(100 * factor, 'g', 4) + QLatin1Char('%');
}
} // namespace

QVComparison::QVComparison(QVGraphicsView *view, QWidget *parent)
    : QWidget(parent), reference(view), active(view)
{
    auto *layout = paneLayout(this);
    toolbar = new QWidget(this);
    auto *controls = new QHBoxLayout(toolbar);
    controls->setContentsMargins(4, 2, 4, 2);
    zoomLink = new QCheckBox(tr("Sync zoom"), toolbar);
    zoomLink->setObjectName(QStringLiteral("comparisonSyncZoom"));
    panLink = new QCheckBox(tr("Sync pan"), toolbar);
    panLink->setObjectName(QStringLiteral("comparisonSyncPan"));
    swapButton = new QPushButton(tr("Swap sides"), toolbar);
    auto *exitButton = new QPushButton(tr("Exit compare"), toolbar);
    auto *linkControls = new QHBoxLayout;
    linkControls->addWidget(zoomLink);
    linkControls->addWidget(panLink);
    linkControls->addStretch();
    auto *commandControls = new QHBoxLayout;
    commandControls->addStretch();
    commandControls->addWidget(swapButton);
    commandControls->addWidget(exitButton);
    controls->addLayout(linkControls, 1);
    controls->addLayout(commandControls);
    layout->addWidget(toolbar);
    toolbar->hide();

    splitter = new ComparisonSplitter(this);
    splitter->setObjectName(QStringLiteral("comparisonSplitter"));
    splitter->setChildrenCollapsible(false);
    splitter->setOpaqueResize(false);
    splitter->setLayoutDirection(Qt::LeftToRight);
    layout->addWidget(splitter, 1);

    referencePane = new QWidget(splitter);
    auto *pane = paneLayout(referencePane);
    referenceTitle = new QLabel(referencePane);
    referenceTitle->setTextFormat(Qt::PlainText);
    referenceTitle->setIndent(6);
    referenceTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    referenceTitle->hide();
    pane->addWidget(referenceTitle);
    pane->addWidget(reference, 1);
    splitter->addWidget(referencePane);

    layoutTimer = new QTimer(this);
    layoutTimer->setSingleShot(true);
    connect(layoutTimer, &QTimer::timeout, this, &QVComparison::restoreLayout);
    connect(splitter, &QSplitter::splitterMoved, this, [this] { restoreLayout(); });
    connect(zoomLink, &QCheckBox::toggled, this, &QVComparison::stateChanged);
    connect(panLink, &QCheckBox::toggled, this, &QVComparison::stateChanged);
    connect(swapButton, &QPushButton::clicked, this, &QVComparison::swap);
    connect(exitButton, &QPushButton::clicked, this, &QVComparison::exitRequested);
    connect(reference, &QVGraphicsView::zoomLevelChanged, this, &QVComparison::refreshHeaders);
    reference->viewport()->installEventFilter(this);
    connect(&reference->thumbnailImageCore(), &QVImageCore::imageRequestStarted, this,
            &QVComparison::stateChanged);
}

QVComparison::~QVComparison()
{
    QvAsync::cancel(job);
    reference->setComparison(nullptr);
    candidate = nullptr;
    // Release pane owners before the reservation member releases its charge.
    delete candidatePane;
}

bool QVComparison::canStart() const
{
    const auto &details = reference->getCurrentFileDetails();
    return details.isPixmapLoaded && details.sourceIsFile && !details.isMovieLoaded
            && !reference->thumbnailImageCore().thumbnailNavigationBusy()
            && !reference->getSourceLifetime();
}

bool QVComparison::canSwap() const
{
    return hasImage(candidate);
}

bool QVComparison::syncZoom() const
{
    return zoomLink->isChecked();
}
bool QVComparison::syncPan() const
{
    return panLink->isChecked();
}
void QVComparison::setSyncZoom(bool value)
{
    zoomLink->setChecked(value);
}
void QVComparison::setSyncPan(bool value)
{
    panLink->setChecked(value);
}

void QVComparison::enter()
{
    if (isActive() || !canStart())
        return;
    referenceSession = reference->getSessionState();
    referenceInitialCenter = reference->comparisonImageCenter();
    referenceCenter = referenceInitialCenter;
    candidateCenter = { 0.5, 0.5 };
    candidatePane = new QWidget(splitter);
    auto *column = paneLayout(candidatePane);
    auto *header = new QWidget(candidatePane);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(6, 0, 4, 0);
    candidateTitle = new QLabel(header);
    candidateTitle->setTextFormat(Qt::PlainText);
    candidateTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    choose = new QPushButton(tr("Choose comparison image…"), header);
    choose->setObjectName(QStringLiteral("comparisonChoose"));
    remove = new QPushButton(tr("Remove"), header);
    remove->setObjectName(QStringLiteral("comparisonRemove"));
    row->addWidget(candidateTitle, 1);
    row->addWidget(choose);
    row->addWidget(remove);
    column->addWidget(header);
    status = new QLabel(tr("Choose or drop an image in this pane."), candidatePane);
    status->setAccessibleName(tr("Comparison image status"));
    status->setObjectName(QStringLiteral("comparisonStatus"));
    status->setTextFormat(Qt::PlainText);
    status->setIndent(6);
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    column->addWidget(status);
    candidate = new QVGraphicsView(candidatePane);
    candidate->setObjectName(QStringLiteral("comparisonView"));
    candidate->setMinimumWidth(240);
    candidate->setComparison(this);
    candidate->viewport()->installEventFilter(this);
    column->addWidget(candidate, 1);
    splitter->addWidget(candidatePane);

    reference->setMinimumWidth(240);
    reference->setComparison(this);
    reference->thumbnailImageCore().setComparisonPreloadsSuspended(true);
    referenceTitle->setFixedHeight(header->sizeHint().height() + status->sizeHint().height());
    referenceTitle->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    referenceTitle->show();
    toolbar->show();
    zoomLink->setChecked(true);
    panLink->setChecked(true);
    active = reference;
    wheel = { };
    gesture = { };
    const int minimumPaneWidth = qMax(referencePane->minimumSizeHint().width(),
                                      candidatePane->minimumSizeHint().width());
    referencePane->setMinimumWidth(minimumPaneWidth);
    candidatePane->setMinimumWidth(minimumPaneWidth);
    layout()->activate();
    resetEqualWidths();
    auto *handle = splitter->handle(1);
    handle->setFocusPolicy(Qt::StrongFocus);
    handle->setAccessibleName(tr("Comparison divider"));
    handle->installEventFilter(this);
    connect(choose, &QPushButton::clicked, this, &QVComparison::chooseFile);
    connect(remove, &QPushButton::clicked, this, &QVComparison::removeCandidate);
    connect(candidate, &QVGraphicsView::zoomLevelChanged, this, &QVComparison::refreshHeaders);
    connect(candidate, &QVGraphicsView::calculatedZoomModeChanged, this,
            &QVComparison::stateChanged);
    reference->setFocus();
    refreshHeaders();
    restoreLayout();
    emit stateChanged();
}

void QVComparison::cancelPending()
{
    ++selection;
    selectedPath.clear();
    QvAsync::cancel(job);
    job.reset();
}

void QVComparison::leave()
{
    if (!isActive())
        return;
    cancelPending();
    cancelInput();
    cancelDividerDrag();
    auto *oldCandidate = candidate;
    candidate = nullptr;
    active = reference;
    oldCandidate->setComparison(nullptr);
    oldCandidate->shutdownAsyncWork();
    delete candidatePane;
    candidatePane = nullptr;
    reference->setComparison(nullptr);
    reference->setMinimumWidth(0);
    referencePane->setMinimumWidth(0);
    reference->setAccessibleName(QString());
    referenceTitle->hide();
    toolbar->hide();
    layout()->setContentsMargins(0, 0, 0, 0);
    layout()->activate();
    reference->loadSessionState(referenceSession);
    reference->fitOrConstrainImage();
    reference->restoreComparisonCenter(referenceInitialCenter);
    reference->refreshComparisonPresentation();
    reference->thumbnailImageCore().setComparisonPreloadsSuspended(false);
    reference->setFocus();
    emit stateChanged();
}

void QVComparison::swap()
{
    if (!canSwap())
        return;
    cancelInput();
    cancelDividerDrag();
    const QList<int> widths = splitter->sizes();
    splitter->insertWidget(0, splitter->widget(1));
    splitter->setSizes(widths);
    QWidget::setTabOrder(splitter->widget(0) == referencePane ? reference : candidate,
                         splitter->widget(0) == referencePane ? candidate : reference);
    restoreLayout();
    refreshHeaders();
    active->setFocus();
}

void QVComparison::chooseFile()
{
    if (!isActive())
        return;
    auto *dialog = new QFileDialog(this, tr("Choose comparison image"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setFileMode(QFileDialog::ExistingFile);
    dialog->setNameFilters(qvApp->getNameFilterList());
    NativeDialogs::applyTheme(dialog);
    if (hasImage(candidate))
        dialog->setDirectory(candidate->getCurrentFileDetails().fileInfo.absolutePath());
    else
        dialog->setDirectory(reference->getCurrentFileDetails().fileInfo.absolutePath());
    const auto owner = QPointer<QVGraphicsView>(candidate);
    connect(dialog, &QFileDialog::fileSelected, this, [this, owner](const QString &path) {
        if (owner && candidate == owner.data())
            selectFile(path);
    });
    dialog->open();
}

void QVComparison::selectFile(const QString &path)
{
    if (!isActive() || path.isEmpty())
        return;
    selectedPath = path;
    ++selection;
    setStatus(tr("Loading %1…").arg(QFileInfo(path).fileName()));
    refreshHeaders();
    if (!job)
        startPending();
}

void QVComparison::startPending()
{
    if (!isActive() || selectedPath.isEmpty())
        return;
    const QString path = selectedPath;
    const quint64 request = selection;

    job = QvAsync::run([path] { return QVImageLoader::readComparisonImage(path); },
                       [this, request](QVImageLoader::Result result) {
                           job.reset();
                           if (request != selection) {
                               result = { };
                               startPending();
                               return;
                           }
                           if (!isActive())
                               return;
                           selectedPath.clear();
                           if (result.errorData) {
                               setStatus(result.errorData->errorString, true);
                               refreshHeaders();
                               return;
                           }
                           cancelInput();
                           candidateCenter = { 0.5, 0.5 };
                           candidate->thumbnailImageCore().commitComparisonImage(result);
                           candidate->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit,
                                                            true);
                           candidate->fitOrConstrainImage();
                           candidate->centerImage();
                           setStatus(QString());
                           refreshHeaders();
                           QAccessibleEvent loaded(candidate, QAccessible::NameChanged);
                           QAccessible::updateAccessibility(&loaded);
                           emit stateChanged();
                       },
                       0, QvAsync::Lane::Comparison);
}

void QVComparison::removeCandidate()
{
    if (!isActive())
        return;
    cancelPending();
    cancelInput();
    candidate->closeImage();
    candidateCenter = { 0.5, 0.5 };
    setStatus(tr("Choose or drop an image in this pane."));
    refreshHeaders();
    choose->setFocus();
    emit stateChanged();
}

void QVComparison::setStatus(const QString &message, bool announce)
{
    if (status->text() == message)
        return;
    status->setText(message);
    status->setToolTip(message);
    status->setAccessibleName(message.isEmpty() ? tr("Comparison image status") : message);
    if (announce && status->isVisible()) {
        QAccessibleEvent event(status, QAccessible::Alert);
        QAccessible::updateAccessibility(&event);
    }
}

void QVComparison::refreshHeaders()
{
    if (!isActive())
        return;
    const bool referenceOnLeft = splitter->indexOf(referencePane) == 0;
    const auto updateTitle = [this](QVGraphicsView *view, QLabel *label, QString role, bool left) {
        const auto &file = view->getCurrentFileDetails().fileInfo;
        const QString text = hasImage(view)
                ? tr("%1 · %2 · %3").arg(role, file.fileName(), zoomLabel(view->getZoomLevel()))
                : role;
        const QString visible = view == active ? tr("● %1").arg(text) : text;
        label->setText(label->fontMetrics().elidedText(visible, Qt::ElideMiddle,
                                                       qMax(0, label->width() - 12)));
        label->setToolTip(file.absoluteFilePath());
        view->setAccessibleName(left ? tr("Left pane, %1").arg(text)
                                     : tr("Right pane, %1").arg(text));
    };
    updateTitle(reference, referenceTitle, tr("Reference"), referenceOnLeft);
    updateTitle(candidate, candidateTitle, tr("Comparison"), !referenceOnLeft);
    const bool ready = hasImage(candidate);
    choose->setText(ready ? tr("Replace…") : tr("Choose comparison image…"));
    remove->setEnabled(ready || !selectedPath.isEmpty());
    swapButton->setEnabled(ready);
}

void QVComparison::activate(QVGraphicsView *view)
{
    if (!isActive() || (view != reference && view != candidate) || active == view)
        return;
    active = view;
    refreshHeaders();
    emit stateChanged();
}

QVGraphicsView *QVComparison::linkedView(QVGraphicsView *source, bool enabled) const
{
    if (!enabled || !hasImage(source))
        return nullptr;
    auto *other = source == reference ? candidate : reference;
    return hasImage(other) ? other : nullptr;
}

void QVComparison::rememberCenter(QVGraphicsView *view)
{
    (view == reference ? referenceCenter : candidateCenter) = view->comparisonImageCenter();
}

void QVComparison::pan(QVGraphicsView *source, QPointF delta)
{
    if (!hasImage(source) || delta.isNull())
        return;
    activate(source);
    const QSizeF sourceSize = source->imageViewportRect().size();
    const QPointF used = source->scrollHelper->move(delta);
    if (used.isNull())
        return;
    rememberCenter(source);
    auto *peer = linkedView(source, syncPan());
    if (!peer || sourceSize.isEmpty())
        return;
    const QSizeF peerSize = peer->imageViewportRect().size();
    const QPointF mapped(used.x() * peerSize.width() / sourceSize.width() * source->getRtlFlip()
                                 * peer->getRtlFlip(),
                         used.y() * peerSize.height() / sourceSize.height());
    if (!peer->scrollHelper->move(mapped).isNull())
        rememberCenter(peer);
}

void QVComparison::relativeZoom(QVGraphicsView *source, qreal factor, std::optional<QPoint> anchor)
{
    if (!hasImage(source) || !std::isfinite(factor) || factor <= 0)
        return;
    activate(source);
    auto *peer = linkedView(source, syncZoom());
    qreal minimum = source->comparisonMinimumZoom() / source->getZoomLevel();
    qreal maximum = Qv::MaximumZoomLevel / source->getZoomLevel();
    if (peer) {
        minimum = qMax(minimum, peer->comparisonMinimumZoom() / peer->getZoomLevel());
        maximum = qMin(maximum, Qv::MaximumZoomLevel / peer->getZoomLevel());
    }
    const qreal common = qBound(minimum, factor, maximum);
    if (QVGraphicsView::zoomLevelsEquivalent(common, 1.0))
        return;
    const QRectF own = source->getUsableViewportRect();
    QPointF relative(0.5, 0.5);
    if (anchor && *anchor != Qv::CalculateViewportCenterPos && source->zoomToCursor
        && source->isCursorVisible && !own.isEmpty()) {
        relative = { qBound(0.0, (anchor->x() - own.left()) / own.width(), 1.0),
                     qBound(0.0, (anchor->y() - own.top()) / own.height(), 1.0) };
    }
    const auto apply = [this, common, relative](QVGraphicsView *view) {
        const QRectF rect = view->getUsableViewportRect();
        const QPointF position =
                rect.topLeft() + QPointF(relative.x() * rect.width(), relative.y() * rect.height());
        view->zoomAbsolute(common * view->getZoomLevel(), position.toPoint());
        rememberCenter(view);
    };
    apply(source);
    if (peer)
        apply(peer);
}

void QVComparison::absoluteZoom(QVGraphicsView *source, qreal level)
{
    if (!hasImage(source))
        return;
    auto *peer = linkedView(source, syncZoom());
    const qreal minimum = peer
            ? qMax(source->comparisonMinimumZoom(), peer->comparisonMinimumZoom())
            : source->comparisonMinimumZoom();
    level = qBound(minimum, level, Qv::MaximumZoomLevel);
    source->zoomAbsolute(level, Qv::CalculateViewportCenterPos);
    rememberCenter(source);
    if (peer) {
        peer->zoomAbsolute(level, Qv::CalculateViewportCenterPos);
        rememberCenter(peer);
    }
}

void QVComparison::calculatedZoom(QVGraphicsView *source, Qv::CalculatedZoomMode mode)
{
    if (!hasImage(source))
        return;
    if (mode == Qv::CalculatedZoomMode::OriginalSize) {
        absoluteZoom(source, 1.0);
        return;
    }
    const auto apply = [this, mode](QVGraphicsView *view) {
        view->setCalculatedZoomMode(mode, true, Qv::CalculateViewportCenterPos);
        if (mode == Qv::CalculatedZoomMode::ZoomToFit)
            view->centerImage();
        rememberCenter(view);
    };
    apply(source);
    if (auto *peer = linkedView(source, syncZoom()))
        apply(peer);
}

QVGraphicsView *QVComparison::InputSequence::target(QVGraphicsView *receiver, bool begin)
{
    if (begin || (!owner && !blocked)) {
        owner = receiver;
        blocked = false;
    }
    return blocked ? nullptr : owner;
}

QVGraphicsView *QVComparison::wheelTarget(QVGraphicsView *receiver, Qt::ScrollPhase phase)
{
    if (phase == Qt::NoScrollPhase || phase == Qt::ScrollBegin)
        activate(receiver);
    return phase == Qt::NoScrollPhase ? receiver : wheel.target(receiver, phase == Qt::ScrollBegin);
}

QVGraphicsView *QVComparison::gestureTarget(QVGraphicsView *receiver, Qt::NativeGestureType type)
{
    if (type == Qt::BeginNativeGesture)
        activate(receiver);
    auto *view = gesture.target(receiver, type == Qt::BeginNativeGesture);
    if (type == Qt::EndNativeGesture)
        gesture = { };
    return view;
}

void QVComparison::cancelInput()
{
    if (!isActive())
        return;
    wheel.blocked = wheel.owner != nullptr;
    gesture.blocked = gesture.owner != nullptr;
    reference->cancelComparisonInput();
    candidate->cancelComparisonInput();
}

void QVComparison::cancelDividerDrag()
{
    if (auto *handle = splitter->handle(1))
        static_cast<ComparisonDividerHandle *>(handle)->cancelDrag();
}

void QVComparison::resetEqualWidths()
{
    const int available = splitter->contentsRect().width() - splitter->handle(1)->width();
    const int widthPerPane = qMax(referencePane->minimumWidth(), (available + 1) / 2);
    splitter->setSizes({ widthPerPane, widthPerPane });
}

void QVComparison::scheduleLayout()
{
    if (isActive() && !layoutTimer->isActive())
        layoutTimer->start(0);
}

void QVComparison::restoreLayout()
{
    if (!isActive())
        return;
    auto *controls = static_cast<QHBoxLayout *>(toolbar->layout());
    const int controlsWidth = controls->itemAt(0)->layout()->sizeHint().width()
            + controls->itemAt(1)->layout()->sizeHint().width() + 16;
    controls->setDirection(width() < controlsWidth ? QBoxLayout::TopToBottom
                                                   : QBoxLayout::LeftToRight);
    if (const auto *owner = qobject_cast<const MainWindow *>(window())) {
        const int overlap = qMax(0, owner->getTitlebarOverlap() - mapTo(owner, QPoint()).y());
        if (layout()->contentsMargins().top() != overlap) {
            layout()->setContentsMargins(0, overlap, 0, 0);
            layout()->activate();
        }
    }
    reference->restoreComparisonCenter(referenceCenter);
    if (hasImage(candidate))
        candidate->restoreComparisonCenter(candidateCenter);
    reference->refreshComparisonPresentation();
    candidate->refreshComparisonPresentation();
    refreshHeaders();
}

bool QVComparison::eventFilter(QObject *watched, QEvent *event)
{
    if (!isActive())
        return QWidget::eventFilter(watched, event);
    if (watched == splitter->handle(1)) {
        if (event->type() == QEvent::MouseButtonDblClick
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            cancelDividerDrag();
            resetEqualWidths();
            restoreLayout();
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            const auto *key = static_cast<QKeyEvent *>(event);
            if (key->key() == Qt::Key_Left || key->key() == Qt::Key_Right) {
                auto widths = splitter->sizes();
                const int delta = key->key() == Qt::Key_Left ? -16 : 16;
                widths[0] += delta;
                widths[1] -= delta;
                splitter->setSizes(widths);
                restoreLayout();
                return true;
            }
        }
    } else if (event->type() == QEvent::Resize || event->type() == QEvent::Move) {
        scheduleLayout();
    }
    return QWidget::eventFilter(watched, event);
}
