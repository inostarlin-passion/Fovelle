#include "qvthumbnailstrip.h"

#include "mainwindow.h"
#include "qvapplication.h"
#include "qvgraphicsview.h"

#include <QAccessible>
#include <QApplication>
#include <QCache>
#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <QtMath>

namespace {
constexpr int MaxPreviewJobs = 2;
constexpr int PreviewCacheBytes = 24 * 1024 * 1024;
constexpr qint64 MaxThumbnailFileBytes = 256 * 1024 * 1024;
constexpr qint64 MaxQtSourceBytes = 64 * 1024 * 1024;
constexpr qint64 MaxSvgSourceBytes = 1024 * 1024;
constexpr qint64 MaxEstimatedBytes = 64 * 1024 * 1024;
constexpr int Gap = 8;
constexpr int Padding = 8;
constexpr int BottomMargin = 14;

struct Preview
{
    QByteArray version;
    QImage image;
    qint64 retryAfter = 0;
};

struct PreviewCache
{
    QMutex mutex;
    QCache<QString, Preview> entries{ PreviewCacheBytes };
    QMutex decodeMutex;
};

QImage readQtPreview(const QString &path, const QSize &size, const qint64 fileBytes)
{
    if (fileBytes > MaxQtSourceBytes)
        return { };

    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return { };

    const auto format = reader.format();
    const bool vector = format == "svg" || format == "svgz";
    // The SVG handler rasterizes directly to the scaled size. Apply the
    // encoded-size gate before asking the handler to parse its canvas.
    if (vector
        && (fileBytes > MaxSvgSourceBytes || !reader.supportsOption(QImageIOHandler::ScaledSize)))
        return { };

    const QSize source = reader.size();
    if (source.isEmpty())
        return { };
    if (!vector && qint64(source.width()) * source.height() > MaxEstimatedBytes / 16)
        return { };

    const bool rotated = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    const QSize bounds = rotated ? size.transposed() : size;
    reader.setScaledSize(source.scaled(bounds, Qt::KeepAspectRatio));
    return reader.read();
}

Preview readPreview(const QString &path, const QSize &size, PreviewCache &cache)
{
    const auto version = QVImageLoader::fileVersion(path);
    if (version.isEmpty())
        return { };
    const QString key = path + QLatin1Char('\n') + QString::fromLatin1(version) + QLatin1Char('\n')
            + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    {
        QMutexLocker lock(&cache.mutex);
        if (const auto *entry = cache.entries.object(key)) {
            if (!entry->image.isNull() || entry->retryAfter > QDateTime::currentMSecsSinceEpoch())
                return *entry;
        }
    }

    QImage image;
    if (!QvAsync::isCanceled() && QVCocoaFunctions::thumbnailMayReadContent(path)) {
        const qint64 fileBytes = QFileInfo(path).size();
        if (fileBytes > 0 && fileBytes <= MaxThumbnailFileBytes) {
            // Some codecs allocate at source resolution even for a scaled
            // request. Keep decoding serialized across all open windows.
            QMutexLocker decodeLock(&cache.decodeMutex);
            if (!QvAsync::isCanceled()) {
                bool allowQtFallback = true;
                image = QVCocoaFunctions::readThumbnail(path, qMax(size.width(), size.height()),
                                                        fileBytes, allowQtFallback);
                if (image.isNull() && allowQtFallback && !QvAsync::isCanceled())
                    image = readQtPreview(path, size, fileBytes);
            }
        }
    }

    if (!image.isNull()) {
        if (image.width() > size.width() || image.height() > size.height())
            image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (image.colorSpace().isValid())
            image = image.convertedToColorSpace(QColorSpace::SRgb);
        else
            image.setColorSpace(QColorSpace::SRgb);
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }

    if (QVImageLoader::fileVersion(path) != version)
        return { };
    Preview result{ version, image, QDateTime::currentMSecsSinceEpoch() + 3000 };
    QMutexLocker lock(&cache.mutex);
    cache.entries.insert(key, new Preview(result), qMax<qsizetype>(256, image.sizeInBytes()));
    return result;
}

class PreviewQueue : public QObject
{
public:
    PreviewQueue() : QObject(qApp) { }

    using Callback = std::function<void(const Preview &)>;
    struct Subscription
    {
        QPointer<QVThumbnailStrip> owner;
        Callback callback;
    };

    void request(QVThumbnailStrip *owner, const QString &path, const QSize &size, Callback callback)
    {
        for (auto &job : jobs) {
            if (job.path == path && job.size == size) {
                job.subscribers.append({ owner, std::move(callback) });
                return;
            }
        }
        jobs.append({ ++nextId, path, size, { { owner, std::move(callback) } }, false });
        start();
    }

    void unsubscribe(QVThumbnailStrip *owner)
    {
        for (auto &job : jobs) {
            job.subscribers.erase(std::remove_if(job.subscribers.begin(), job.subscribers.end(),
                                                 [owner](const Subscription &s) {
                                                     return !s.owner || s.owner == owner;
                                                 }),
                                  job.subscribers.end());
        }
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                  [](const Job &job) {
                                      return !job.running && job.subscribers.isEmpty();
                                  }),
                   jobs.end());
    }

    bool validate(const QString &path, std::function<void(QByteArray)> done)
    {
        if (validationRunning)
            return false;
        validationRunning = true;
        QvAsync::run([path] { return QVImageLoader::fileVersion(path); },
                     [owner = QPointer<PreviewQueue>(this), done = std::move(done)](QByteArray v) {
                         if (!owner)
                             return;
                         owner->validationRunning = false;
                         done(std::move(v));
                     },
                     1, QvAsync::Lane::Shared);
        return true;
    }

private:
    struct Job
    {
        quint64 id;
        QString path;
        QSize size;
        QList<Subscription> subscribers;
        bool running;
    };

    void start()
    {
        for (auto &job : jobs) {
            if (active >= MaxPreviewJobs)
                break;
            if (job.running)
                continue;
            job.running = true;
            ++active;
            QvAsync::run(
                    [path = job.path, size = job.size, cache = cache] {
                        return readPreview(path, size, *cache);
                    },
                    [owner = QPointer<PreviewQueue>(this), id = job.id](const Preview &preview) {
                        if (owner)
                            owner->complete(id, preview);
                    },
                    -100, QvAsync::Lane::Shared);
        }
    }

    void complete(quint64 id, const Preview &preview)
    {
        const auto it = std::find_if(jobs.begin(), jobs.end(),
                                     [id](const Job &job) { return job.id == id; });
        if (it == jobs.end())
            return;
        const auto subscribers = std::move(it->subscribers);
        jobs.erase(it);
        --active;
        for (const auto &subscription : subscribers) {
            if (subscription.owner)
                subscription.callback(preview);
        }
        start();
    }

    QList<Job> jobs;
    std::shared_ptr<PreviewCache> cache = std::make_shared<PreviewCache>();
    quint64 nextId = 0;
    int active = 0;
    bool validationRunning = false;
};

PreviewQueue &previewQueue()
{
    static QPointer<PreviewQueue> queue;
    if (!queue)
        queue = new PreviewQueue;
    return *queue;
}

class ThumbnailAccessibleButton : public QPushButton
{
public:
    explicit ThumbnailAccessibleButton(QWidget *parent) : QPushButton(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override { }
};
} // namespace

QList<QVThumbnailStrip::Cell> QVThumbnailStrip::layout(const QRectF &viewport, const int count,
                                                       const int current, const bool loop)
{
    if (count < 2 || current < 0 || current >= count || viewport.height() < 160)
        return { };

    const int maxRadius = qMin(4, count - 1);
    for (const QSize size : { QSize(144, 104), QSize(112, 80) }) {
        for (int radius = maxRadius; radius >= 1; --radius) {
            const int slotCount = 2 * radius + 1;
            if (slotCount * size.width() + 2 * radius * Gap + 2 * Padding + 24 > viewport.width())
                continue;

            QList<Cell> cells;
            const qreal pitch = size.width() + Gap;
            const qreal y = viewport.bottom() - BottomMargin - Padding - size.height();
            for (int offset = -radius; offset <= radius; ++offset) {
                const qreal x = viewport.center().x() + pitch * offset - size.width() / 2.0;
                cells.append({ QRectF(QPointF(x, y), size) });
            }
            cells[radius].index = current;
            QSet<int> used{ current };
            for (int distance = 1; distance <= radius; ++distance) {
                for (int offset : { distance, -distance }) {
                    int index = current + offset;
                    if (loop)
                        index = (index % count + count) % count;
                    if (index >= 0 && index < count && !used.contains(index)) {
                        used.insert(index);
                        cells[radius + offset].index = index;
                    }
                }
            }
            return cells;
        }
    }
    return { };
}

QVThumbnailStrip::QVThumbnailStrip(MainWindow *owner, QVGraphicsView *graphicsView)
    : QWidget(graphicsView->viewport()), window(owner), view(graphicsView)
{
    setObjectName(QStringLiteral("thumbnailStrip"));
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setAccessibleName(tr("Thumbnails"));
    setFocusPolicy(Qt::NoFocus);
    hide();
    qApp->installEventFilter(this);

    for (auto *timer : { &enterTimer, &leaveTimer, &gestureTimer, &validationTimer })
        timer->setSingleShot(true);

    connect(&enterTimer, &QTimer::timeout, this, [this] {
        if (!blocked() && hoverEnabled && !suppressHoverUntilExit
            && usableViewport().contains(lastPointer)
            && lastPointer.y() >= usableViewport().bottom() - 28) {
            view->thumbnailImageCore().refreshThumbnailListing();
            if (available())
                animateTo(true);
        }
    });
    connect(&leaveTimer, &QTimer::timeout, this, [this] { animateTo(false); });
    connect(&gestureTimer, &QTimer::timeout, this, [this] { updatePointer(lastPointer, true); });
    connect(&validationTimer, &QTimer::timeout, this, [this] {
        cancelActivation();
        window->showThumbnailMessage(tr("Could not verify the image. Try again."));
    });
    connect(&fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        opacityValue = value.toReal();
        updateRendering(false);
    });
    connect(&fade, &QVariantAnimation::finished, this, [this] {
        if (targetOpacity == 0)
            hideImmediately();
    });

    auto &core = view->thumbnailImageCore();
    connect(&core, &QVImageCore::thumbnailListingChanged, this, &QVThumbnailStrip::refresh);
    connect(&core, &QVImageCore::folderInfoChanged, this, &QVThumbnailStrip::refresh);
    connect(&core, &QVImageCore::imageRequestStarted, this, [this] {
        cancelActivation();
        updateRendering(true);
    });
    connect(view, &QVGraphicsView::fileChanged, this, [this] {
        cancelActivation();
        refresh();
    });
    connect(&core, &QVImageCore::thumbnailOpenRejected, this, [this] {
        cancelActivation();
        window->showThumbnailMessage(tr("The image changed. Choose it again."));
        if (targetOpacity > 0)
            requestPreviews();
    });
}

QVThumbnailStrip::~QVThumbnailStrip()
{
    previewQueue().unsubscribe(this);
}

QRectF QVThumbnailStrip::usableViewport() const
{
    QRectF area(view->viewport()->rect());
    area.setTop(qMax<qreal>(area.top(), window->getViewportPosition().obscuredHeight));
    for (auto *bar : { view->horizontalScrollBar(), view->verticalScrollBar() }) {
        if (!bar->isVisible())
            continue;
        const QRect barRect(bar->mapToGlobal(QPoint()), bar->size());
        const QRect viewportRect(view->viewport()->mapToGlobal(QPoint()), view->viewport()->size());
        const QRect intersection = barRect.intersected(viewportRect);
        if (intersection.isEmpty())
            continue;
        if (bar == view->horizontalScrollBar())
            area.setBottom(qMin(area.bottom(), qreal(intersection.top() - viewportRect.top())));
        else
            area.adjust(intersection.width(), 0, -intersection.width(), 0);
    }
    return area;
}

bool QVThumbnailStrip::available() const
{
    return view->thumbnailImageCore().thumbnailListingReady() && canOfferFocus();
}

bool QVThumbnailStrip::canOfferFocus() const
{
    const auto &core = view->thumbnailImageCore();
    const auto &details = core.getCurrentFileDetails();
    return details.isPixmapLoaded && !core.getSourceLifetime()
            && !layout(usableViewport(), details.folderFileInfoList.size(),
                       details.loadedIndexInFolder, core.thumbnailLoopEnabled())
                        .isEmpty();
}

bool QVThumbnailStrip::blocked() const
{
    return suppressed || slideshowRunning || dragActive || fileDragActive || gestureTimer.isActive()
            || qApp->property("fovelleNativeMenuTracking").toBool()
            || QApplication::activePopupWidget() || !window->isVisible() || window->isMinimized()
            || !window->isActiveWindow() || QApplication::activeModalWidget();
}

int QVThumbnailStrip::slotAt(const QPoint &pos) const
{
    for (int i = 0; i < visibleCells.size(); ++i) {
        if (visibleCells.at(i).index >= 0 && visibleCells.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

bool QVThumbnailStrip::hitTest(const QPoint &pos) const
{
    return targetOpacity > 0 && opacityValue > 0.001 && !surface.isNull() && !blocked()
            && view->thumbnailImageCore().thumbnailListingReady() && slotAt(pos) >= 0;
}

int QVThumbnailStrip::previewCount() const
{
    return std::count_if(visibleCells.cbegin(), visibleCells.cend(),
                         [](const Cell &cell) { return !cell.preview.isNull(); });
}

void QVThumbnailStrip::setHoverEnabled(const bool enabled)
{
    if (hoverEnabled == enabled)
        return;
    hoverEnabled = enabled;
    if (!enabled && focusSlot < 0)
        hideImmediately();
    else if (enabled)
        updatePointer(lastPointer, true);
}

void QVThumbnailStrip::suppress(const bool value)
{
    suppressed = value;
    if (value) {
        cancelActivation();
        hideImmediately();
    } else {
        updatePointer(lastPointer, true);
    }
}

void QVThumbnailStrip::setSlideshowRunning(const bool running)
{
    slideshowRunning = running;
    if (running) {
        cancelActivation();
        hideImmediately();
    } else {
        updatePointer(lastPointer, true);
    }
}

void QVThumbnailStrip::focusStrip()
{
    if (qApp->property("fovelleNativeMenuTracking").toBool()) {
        focusRequested = true;
        return;
    }
    focusRequested = false;
    if (blocked() || !canOfferFocus())
        return;
    if (!available()) {
        focusRequested = true;
        view->thumbnailImageCore().refreshThumbnailListing();
        return;
    }
    rebuild();
    focusSlot = visibleCells.size() / 2;
    view->viewport()->setFocus(Qt::OtherFocusReason);
    animateTo(true);
    updateRendering(true);
}

bool QVThumbnailStrip::dismissFocus()
{
    if (focusSlot < 0)
        return false;
    suppressHoverUntilExit = true;
    cancelActivation();
    hideImmediately();
    view->setFocus(Qt::OtherFocusReason);
    return true;
}

void QVThumbnailStrip::refresh()
{
    const auto &core = view->thumbnailImageCore();
    if (!core.thumbnailListingReady()) {
        pressedSlot = -1;
        cancelActivation();
        if (core.getCurrentFileDetails().loadedIndexInFolder < 0)
            hideImmediately();
        else if (targetOpacity > 0) {
            updateRendering(true);
            QTimer::singleShot(0, this, [this] {
                if (!blocked() && targetOpacity > 0)
                    view->thumbnailImageCore().refreshThumbnailListing();
            });
        }
        return;
    }

    if (focusRequested && !blocked()) {
        focusRequested = false;
        if (available()) {
            focusStrip();
            return;
        }
    }
    if (targetOpacity > 0) {
        if (!available()) {
            hideImmediately();
            return;
        }
        rebuild();
        requestPreviews();
        updateRendering(true);
    }
    if (!blocked())
        updatePointer(lastPointer, true);
}

void QVThumbnailStrip::updatePointer(const QPoint &pos, const bool canEnter)
{
    lastPointer = pos;
    const QRectF area = usableViewport();
    const bool inBand = area.contains(pos) && pos.y() >= area.bottom() - 28;
    if (!inBand)
        suppressHoverUntilExit = false;
    if (blocked())
        return;

    const bool inPanel = !panel.isEmpty() && area.contains(pos)
            && QRectF(panel.left() - 8, panel.top(), panel.width() + 16,
                      area.bottom() - panel.top())
                       .contains(pos);
    const bool retained = inBand || inPanel;
    view->holdThumbnailCursor(retained && targetOpacity > 0);
    const int hovered = slotAt(pos);
    if (hoverSlot != hovered) {
        hoverSlot = hovered;
        if (targetOpacity > 0)
            updateRendering(true);
    }
    if (focusSlot >= 0)
        return;
    if (targetOpacity > 0) {
        if (retained)
            leaveTimer.stop();
        else if (!leaveTimer.isActive())
            leaveTimer.start(180);
    } else if (canEnter && hoverEnabled && !suppressHoverUntilExit) {
        if (retained && opacityValue > 0) {
            animateTo(true);
        } else if (inBand) {
            if (!enterTimer.isActive())
                enterTimer.start(70);
        } else {
            enterTimer.stop();
        }
    }
}

void QVThumbnailStrip::animateTo(const bool visible)
{
    const qreal target = visible ? 1.0 : 0.0;
    if (targetOpacity == target)
        return;
    if (visible) {
        if (!available())
            return;
        rebuild();
        requestPreviews();
    } else {
        pressedSlot = -1;
        view->holdThumbnailCursor(false);
    }
    targetOpacity = target;
    enterTimer.stop();
    leaveTimer.stop();
    fade.stop();
    updateRendering(true);
    if (QVCocoaFunctions::reduceThumbnailMotion()) {
        if (visible) {
            opacityValue = 1;
            updateRendering(false);
        } else {
            hideImmediately();
        }
        return;
    }
    fade.setStartValue(opacityValue);
    fade.setEndValue(target);
    fade.setDuration(visible ? 160 : 180);
    fade.setEasingCurve(visible ? QEasingCurve::OutQuad : QEasingCurve::InQuad);
    fade.start();
    if (visible)
        updatePointer(lastPointer, false);
}

void QVThumbnailStrip::hideImmediately()
{
    enterTimer.stop();
    leaveTimer.stop();
    fade.stop();
    ++previewGeneration;
    if (!visibleCells.isEmpty())
        previewQueue().unsubscribe(this);
    visibleCells.clear();
    const bool hadPanel = !panel.isEmpty();
    panel = { };
    surface = { };
    pressedSlot = -1;
    hoverSlot = -1;
    focusSlot = -1;
    targetOpacity = 0;
    opacityValue = 0;
    view->holdThumbnailCursor(false);
    view->clearNativeThumbnailSurface();
    nativeSurface = false;
    accessibilityActive = false;
    reportedAccessibleFocus = nullptr;
    hide();
    if (hadPanel)
        emit overlayGeometryChanged();
}

void QVThumbnailStrip::rebuild()
{
    const auto &core = view->thumbnailImageCore();
    const auto &details = core.getCurrentFileDetails();
    const int nextCurrent = details.loadedIndexInFolder;
    const QString oldFocus = focusSlot >= 0 && focusSlot < visibleCells.size()
            ? visibleCells.at(focusSlot).path
            : QString();
    if (listVersion != core.thumbnailListVersion() || currentIndex != nextCurrent)
        pressedSlot = -1;
    listVersion = core.thumbnailListVersion();
    const auto oldCells = std::move(visibleCells);
    visibleCells = layout(usableViewport(), details.folderFileInfoList.size(), nextCurrent,
                          core.thumbnailLoopEnabled());
    if (visibleCells.isEmpty()) {
        panel = { };
        return;
    }
    for (auto &cell : visibleCells) {
        if (cell.index < 0)
            continue;
        cell.path = details.folderFileInfoList.at(cell.index).absoluteFilePath;
        for (const auto &old : oldCells) {
            if (cell.path == old.path && (!old.currentProxy || cell.index == nextCurrent)) {
                cell.version = old.version;
                cell.preview = old.preview;
                cell.currentProxy = old.currentProxy;
                break;
            }
        }
    }
    if (pressedSlot >= 0
        && (pressedSlot >= visibleCells.size()
            || visibleCells.at(pressedSlot).path != oldCells.at(pressedSlot).path))
        pressedSlot = -1;
    if (focusSlot >= 0) {
        focusSlot = visibleCells.size() / 2;
        if (nextCurrent == currentIndex) {
            for (int i = 0; i < visibleCells.size(); ++i) {
                if (visibleCells.at(i).path == oldFocus)
                    focusSlot = i;
            }
        }
    }
    currentIndex = nextCurrent;
    auto &center = visibleCells[visibleCells.size() / 2];
    const auto &proxy = core.getLoadedPixmap();
    if (center.preview.isNull() && !proxy.isNull() && proxy.width() <= 512
        && proxy.height() <= 512) {
        center.preview = proxy.toImage();
        if (center.preview.colorSpace().isValid())
            center.preview = center.preview.convertedToColorSpace(QColorSpace::SRgb);
        center.currentProxy = true;
    }
    const QRect nextPanel = visibleCells.first()
                                    .rect.united(visibleCells.last().rect)
                                    .adjusted(-Padding, -Padding, Padding, Padding)
                                    .toAlignedRect();
    if (panel != nextPanel) {
        panel = nextPanel;
        emit overlayGeometryChanged();
    }
}

void QVThumbnailStrip::requestPreviews()
{
    const quint64 generation = ++previewGeneration;
    auto &queue = previewQueue();
    queue.unsubscribe(this);
    const int center = visibleCells.size() / 2;
    const qreal scale = view->viewport()->devicePixelRatioF();
    for (int distance = 0; distance <= center; ++distance) {
        for (int offset : { distance, -distance }) {
            const int i = center + offset;
            const auto &cell = visibleCells.at(i);
            if (cell.index >= 0) {
                const QSize size(qCeil(cell.rect.width() * scale),
                                 qCeil(cell.rect.height() * scale));
                queue.request(this, cell.path, size, [this, generation, i](const Preview &preview) {
                    if (generation != previewGeneration || targetOpacity <= 0)
                        return;
                    auto &bound = visibleCells[i];
                    if (!preview.image.isNull() || !bound.currentProxy) {
                        bound.preview = preview.image;
                        bound.currentProxy = false;
                    }
                    bound.version = preview.version;
                    updateRendering(true);
                });
            }
            if (distance == 0)
                break;
        }
    }
}

void QVThumbnailStrip::activate(const int slot)
{
    auto &core = view->thumbnailImageCore();
    if (slot < 0 || slot >= visibleCells.size() || !waitingPath.isEmpty()
        || core.thumbnailNavigationBusy() || !core.thumbnailListingReady())
        return;
    const auto &cell = visibleCells.at(slot);
    if (cell.index < 0 || cell.index == currentIndex)
        return;
    const QString path = cell.path;
    const int index = cell.index;
    const auto version = cell.version;
    const quint64 snapshot = listVersion;
    const quint64 request = ++activationGeneration;
    const auto done = [owner = QPointer<QVThumbnailStrip>(this), request, snapshot, path, index,
                       version](const QByteArray &verified) {
        if (!owner || owner->activationGeneration != request)
            return;
        owner->validationTimer.stop();
        auto &core = owner->view->thumbnailImageCore();
        if (verified.isEmpty()) {
            owner->cancelActivation();
            owner->window->showThumbnailMessage(tr("The image is no longer available."));
            core.markFolderInfoDirty();
        } else if (!version.isEmpty() && version != verified) {
            owner->cancelActivation();
            owner->window->showThumbnailMessage(tr("The image changed. Choose it again."));
            if (owner->targetOpacity > 0)
                owner->requestPreviews();
        } else if (!core.openThumbnail(index, path, snapshot, verified)) {
            owner->cancelActivation();
        }
    };
    if (!previewQueue().validate(path, done)) {
        window->showThumbnailMessage(tr("Could not verify the image. Try again."));
        return;
    }
    waitingPath = path;
    validationTimer.start(800);
    updateRendering(true);
}

void QVThumbnailStrip::cancelActivation()
{
    ++activationGeneration;
    validationTimer.stop();
    if (waitingPath.isEmpty())
        return;
    waitingPath.clear();
    updateRendering(true);
}

void QVThumbnailStrip::updateRendering(const bool contentChanged)
{
    if (panel.isEmpty())
        return;
    const bool native = view->usesNativeHDRNavigationOverlay();
    if (contentChanged || surface.isNull()) {
        const qreal scale = view->viewport()->devicePixelRatioF();
        surface = QImage(QSize(qCeil(panel.width() * scale), qCeil(panel.height() * scale)),
                         QImage::Format_ARGB32_Premultiplied);
        surface.setDevicePixelRatio(scale);
        surface.setColorSpace(QColorSpace::SRgb);
        surface.fill(Qt::transparent);
        QPainter painter(&surface);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.translate(-panel.topLeft());
        const auto theme = qvApp->getSettingsManager().getEnum<Qv::Theme>("theme");
        const bool dark = QVCocoaFunctions::resolvedTheme(theme) == Qv::Theme::Dark;
        const QColor background = dark ? QColor(22, 22, 24) : QColor(248, 248, 250);
        const QColor foreground = dark ? QColor(250, 250, 250) : QColor(24, 24, 28);
        QColor tint = background;
        tint.setAlpha(QVCocoaFunctions::reduceThumbnailTransparency() ? 255 : 226);
        painter.setPen(Qt::NoPen);
        painter.setBrush(tint);
        painter.drawRoundedRect(QRectF(panel), 10, 10);
        for (int i = 0; i < visibleCells.size(); ++i) {
            const auto &cell = visibleCells.at(i);
            if (cell.index < 0)
                continue;
            QColor tile = foreground;
            tile.setAlpha(i == hoverSlot ? 54 : 27);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tile);
            painter.drawRoundedRect(cell.rect, 4, 4);
            if (!cell.preview.isNull()) {
                const QSizeF size = QSizeF(cell.preview.size())
                                            .scaled(cell.rect.adjusted(3, 3, -3, -3).size(),
                                                    Qt::KeepAspectRatio);
                const QRectF dest(cell.rect.center() - QPointF(size.width(), size.height()) / 2,
                                  size);
                painter.drawImage(dest, cell.preview);
            } else {
                painter.setPen(foreground);
                const auto extension = QFileInfo(cell.path).suffix().toUpper().left(6);
                painter.drawText(cell.rect, Qt::AlignCenter, extension);
            }
            if (cell.index == currentIndex || i == focusSlot) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(
                        i == focusSlot ? palette().color(QPalette::Highlight) : foreground, 2));
                painter.drawRoundedRect(cell.rect.adjusted(1, 1, -1, -1), 4, 4);
            }
            if (cell.path == waitingPath) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(palette().color(QPalette::Highlight));
                painter.drawEllipse(cell.rect.topRight() + QPointF(-7, 7), 3, 3);
            }
        }
        painter.end();
    }
    if (contentChanged) {
        setGeometry(panel);
        if (opacityValue > 0 || targetOpacity > 0) {
            show();
            raise();
        }
    }
    if (native) {
        if (contentChanged || !nativeSurface)
            view->setNativeThumbnailSurface(panel, surface);
        view->setNativeThumbnailOpacity(opacityValue);
    } else if (nativeSurface) {
        view->clearNativeThumbnailSurface();
    }
    nativeSurface = native;
    const bool active = targetOpacity > 0 && opacityValue > 0.001;
    if (contentChanged || accessibilityActive != active) {
        accessibilityActive = active;
        syncAccessibleButtons();
    }
    if (!native)
        update();
}

void QVThumbnailStrip::syncAccessibleButtons()
{
    while (accessibleButtons.size() < visibleCells.size()) {
        const int index = accessibleButtons.size();
        auto *button = new ThumbnailAccessibleButton(this);
        connect(button, &QPushButton::clicked, this, [this, index] { activate(index); });
        accessibleButtons.append(button);
    }
    const bool enabled = targetOpacity > 0 && opacityValue > 0.001 && !blocked()
            && view->thumbnailImageCore().thumbnailListingReady();
    const auto &listing = view->getCurrentFileDetails().folderFileInfoList;
    for (int i = 0; i < accessibleButtons.size(); ++i) {
        auto *button = accessibleButtons.at(i);
        if (!enabled || i >= visibleCells.size() || visibleCells.at(i).index < 0) {
            button->hide();
            continue;
        }
        const auto &cell = visibleCells.at(i);
        const QString name = QDir(listing.getBaseDir()).relativeFilePath(cell.path);
        button->setAccessibleName(
                cell.index == currentIndex
                        ? tr("%1, current image, %2 of %3")
                                  .arg(name)
                                  .arg(cell.index + 1)
                                  .arg(listing.size())
                        : tr("%1, %2 of %3").arg(name).arg(cell.index + 1).arg(listing.size()));
        button->setGeometry(cell.rect.toAlignedRect().translated(-panel.topLeft()));
        button->setEnabled(waitingPath.isEmpty());
        button->show();
    }
    QWidget *focused = enabled && focusSlot >= 0 ? accessibleButtons.value(focusSlot) : nullptr;
    if (focused && focused != reportedAccessibleFocus) {
        QAccessibleEvent event(focused, QAccessible::Focus);
        QAccessible::updateAccessibility(&event);
    }
    reportedAccessibleFocus = focused;
}

void QVThumbnailStrip::paintEvent(QPaintEvent *)
{
    if (view->usesNativeHDRNavigationOverlay() || surface.isNull())
        return;
    QPainter painter(this);
    painter.setOpacity(opacityValue);
    painter.drawImage(QPoint(0, 0), surface);
}

bool QVThumbnailStrip::eventFilter(QObject *watched, QEvent *event)
{
    const auto type = event->type();
    if (watched == window
        && (type == QEvent::WindowDeactivate || type == QEvent::Hide || type == QEvent::Close)) {
        lastPointer = { -1, -1 };
        focusRequested = false;
        dragActive = false;
        fileDragActive = false;
        ownsPress = false;
        cancelActivation();
        hideImmediately();
    }
    if (watched == qApp && type == QEvent::DynamicPropertyChange) {
        const auto *change = static_cast<QDynamicPropertyChangeEvent *>(event);
        if (change->propertyName() == "fovelleNativeMenuTracking") {
            if (qApp->property("fovelleNativeMenuTracking").toBool()) {
                hideImmediately();
            } else if (focusRequested) {
                QTimer::singleShot(0, this, &QVThumbnailStrip::focusStrip);
            } else {
                updatePointer(lastPointer, true);
            }
        } else if (change->propertyName() == "fovelleAccessibilityDisplayRevision") {
            if (QVCocoaFunctions::reduceThumbnailMotion()
                && fade.state() == QAbstractAnimation::Running) {
                fade.stop();
                if (targetOpacity == 0)
                    hideImmediately();
                else
                    opacityValue = targetOpacity;
            }
            updateRendering(true);
        }
    }
    if (watched == view->horizontalScrollBar() || watched == view->verticalScrollBar()) {
        if (type == QEvent::MouseButtonPress) {
            dragActive = true;
            hideImmediately();
        } else if (type == QEvent::MouseButtonRelease || type == QEvent::UngrabMouse) {
            dragActive = false;
        }
    }
    if (watched == view->viewport()) {
        if (type == QEvent::Resize || type == QEvent::PaletteChange
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            || type == QEvent::DevicePixelRatioChange
#endif
        )
            QTimer::singleShot(0, this, &QVThumbnailStrip::refresh);
        if (type == QEvent::Leave)
            updatePointer({ -1, -1 }, false);
        if (type == QEvent::UngrabMouse) {
            pressedSlot = -1;
            dragActive = false;
        }
        if (type == QEvent::DragEnter || type == QEvent::DragMove) {
            fileDragActive = true;
            hideImmediately();
        } else if (type == QEvent::DragLeave || type == QEvent::Drop) {
            fileDragActive = false;
        }
        if (type == QEvent::NativeGesture) {
            gestureTimer.start(250);
            hideImmediately();
        }
        if (type == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (wheel->phase() != Qt::NoScrollPhase) {
                gestureTimer.start(wheel->phase() == Qt::ScrollEnd ? 80 : 250);
                hideImmediately();
            }
        }
        if (type == QEvent::ToolTip) {
            auto *help = static_cast<QHelpEvent *>(event);
            if (hitTest(help->pos())) {
                const auto &cell = visibleCells.at(slotAt(help->pos()));
                const auto &listing = view->getCurrentFileDetails().folderFileInfoList;
                QToolTip::showText(help->globalPos(),
                                   QDir(listing.getBaseDir()).relativeFilePath(cell.path), this);
                return true;
            }
        }
    }

    const bool viewportInput = watched == view->viewport() || watched == view;
    if (viewportInput
        && (type == QEvent::MouseMove || type == QEvent::MouseButtonPress
            || type == QEvent::MouseButtonRelease || type == QEvent::MouseButtonDblClick)) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        const auto *widget = static_cast<QWidget *>(watched);
        const QPoint pos =
                view->viewport()->mapFromGlobal(widget->mapToGlobal(mouse->position().toPoint()));
        if (type == QEvent::MouseMove) {
            if (ownsPress) {
                if ((pos - pressedPosition).manhattanLength() > QApplication::startDragDistance()
                    || pressedSlot != slotAt(pos))
                    pressedSlot = -1;
                if (mouse->buttons().testFlag(Qt::LeftButton))
                    return true;
                ownsPress = false;
            }
            if (dragActive && mouse->buttons() == Qt::NoButton)
                dragActive = false;
            updatePointer(pos, mouse->buttons() == Qt::NoButton);
        } else if (mouse->button() == Qt::LeftButton) {
            if (type == QEvent::MouseButtonRelease && ownsPress) {
                ownsPress = false;
                const int slot = pressedSlot;
                pressedSlot = -1;
                if (hitTest(pos) && slot == slotAt(pos))
                    activate(slot);
                return true;
            }
            if (type != QEvent::MouseButtonRelease && mouse->modifiers() == Qt::NoModifier
                && hitTest(pos)) {
                ownsPress = true;
                pressedSlot = type == QEvent::MouseButtonPress ? slotAt(pos) : -1;
                pressedPosition = pos;
                return true;
            }
            if (type != QEvent::MouseButtonRelease) {
                dragActive = true;
                hideImmediately();
            } else {
                dragActive = false;
                updatePointer(pos, true);
            }
        }
    }

    if (focusSlot >= 0 && (type == QEvent::ShortcutOverride || type == QEvent::KeyPress)) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget->window() != window)
            return false;
        auto *key = static_cast<QKeyEvent *>(event);
        const int code = key->key();
        if (code == Qt::Key_Left || code == Qt::Key_Right || code == Qt::Key_Return
            || code == Qt::Key_Enter || code == Qt::Key_Space || code == Qt::Key_Escape) {
            event->accept();
            if (type == QEvent::KeyPress) {
                if (code == Qt::Key_Escape)
                    dismissFocus();
                else if (code == Qt::Key_Return || code == Qt::Key_Enter || code == Qt::Key_Space)
                    activate(focusSlot);
                else {
                    const int step = code == Qt::Key_Left ? -1 : 1;
                    for (int i = focusSlot + step; i >= 0 && i < visibleCells.size(); i += step) {
                        if (visibleCells.at(i).index >= 0) {
                            focusSlot = i;
                            updateRendering(true);
                            break;
                        }
                    }
                }
            }
            return true;
        }
        if (type == QEvent::KeyPress && (code == Qt::Key_Tab || code == Qt::Key_Backtab))
            dismissFocus();
    }
    return false;
}
