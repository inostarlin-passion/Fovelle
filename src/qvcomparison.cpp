#include "qvcomparison.h"

#include "qvgraphicsview.h"
#include "mainwindow.h"
#include "nativedialogs.h"
#include "qvapplication.h"
#include "qvimageloader.h"

#include <QAccessible>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace {
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

    splitter = new QSplitter(Qt::Horizontal, this);
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
    previewTimer = new QTimer(this);
    previewTimer->setInterval(150);
    connect(previewTimer, &QTimer::timeout, this, &QVComparison::refreshPreview);
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
    splitter->setSizes({ 1, 1 });
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
    previewTimer->stop();
    auto *oldCandidate = candidate;
    candidate = nullptr;
    active = reference;
    oldCandidate->setComparison(nullptr);
    oldCandidate->shutdownAsyncWork();
    delete candidatePane;
    candidatePane = nullptr;
    committedBytes = 0;
    committedReservation.reset();
    reference->setComparison(nullptr);
    reference->setMinimumWidth(0);
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
    previewTimer->stop();
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
    const quint64 referenceBytes = QVImageLoader::comparisonImageBytes(
            reference->getCurrentFileDetails().baseImageSize, 8);
    const QSize drawable = size() * devicePixelRatioF();
    const quint64 drawableBytes = QVImageLoader::comparisonImageBytes(drawable, 24);
    const quint64 charged = referenceBytes + committedBytes + drawableBytes;
    const quint64 available = charged < QVImageLoader::ComparisonBudget
            ? QVImageLoader::ComparisonBudget - charged
            : 0;

    job = QvAsync::run(
            [path, available] { return QVImageLoader::readComparisonImage(path, available); },
            [this, request](QVImageLoader::Result result) {
                job.reset();
                if (request != selection) {
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
                const auto oldReservation = std::move(committedReservation);
                committedReservation = std::move(result.comparisonReservation);
                committedBytes = result.estimatedDecodedBytes;
                candidateCenter = { 0.5, 0.5 };
                candidate->thumbnailImageCore().commitComparisonImage(result);
                candidate->setCalculatedZoomMode(Qv::CalculatedZoomMode::ZoomToFit, true);
                candidate->fitOrConstrainImage();
                candidate->centerImage();
                refreshPreview();
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
    previewTimer->stop();
    candidate->closeImage();
    committedBytes = 0;
    committedReservation.reset();
    candidateCenter = { 0.5, 0.5 };
    setStatus(tr("Choose or drop an image in this pane."));
    refreshHeaders();
    choose->setFocus();
    emit stateChanged();
}

void QVComparison::refreshPreview()
{
    if (!isActive() || !selectedPath.isEmpty())
        return;
    const auto &details = candidate->getCurrentFileDetails();
    const auto diagnostics = candidate->nativeMetalRendererDiagnostics();
    const bool native = details.isNativeHDRLoaded || details.isNativeSDRLoaded;
    if (details.isComparisonPreview) {
        previewTimer->stop();
        setStatus(tr("SDR preview"));
    } else if (native && !diagnostics.firstFramePresented) {
        setStatus(details.isNativeHDRLoaded ? tr("SDR preview") : tr("Preview"));
        if (diagnostics.rendererAvailable)
            previewTimer->start();
    } else {
        previewTimer->stop();
        setStatus(QString());
    }
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
        if (event->type() == QEvent::MouseButtonDblClick) {
            splitter->setSizes({ 1, 1 });
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
