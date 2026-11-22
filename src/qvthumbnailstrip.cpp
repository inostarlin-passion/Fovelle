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
#include <QHash>
#include <QImageReader>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>
#include <QMutex>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QToolTip>
#include <algorithm>
#include <cmath>
#include <QtMath>

#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>

namespace {
constexpr int PreviewCacheBytes = 24 * 1024 * 1024;
constexpr qint64 MaxQtSourceBytes = 64 * 1024 * 1024;
constexpr qint64 MaxSvgSourceBytes = 1024 * 1024;
constexpr qint64 MaxEstimatedBytes = 64 * 1024 * 1024;
constexpr int Gap = 8;
constexpr int Padding = 8;
constexpr int BottomMargin = 14;

QImage fitTransitionSurface(const QImage &image, const QSize &size, const qreal dpr)
{
    const QSize pixels(qCeil(size.width() * dpr), qCeil(size.height() * dpr));
    if (image.size() == pixels && image.devicePixelRatioF() == dpr)
        return image;

    QImage fitted(pixels, image.format());
    fitted.setDevicePixelRatio(dpr);
    fitted.setColorSpace(image.colorSpace());
    fitted.fill(Qt::transparent);
    const QSizeF logicalSize = QSizeF(image.size()) / image.devicePixelRatioF();
    QPainter painter(&fitted);
    painter.drawImage(
            QPointF((size.width() - logicalSize.width()) / 2, size.height() - logicalSize.height()),
            image);
    painter.end();
    return fitted;
}

using PreviewStatus = QVThumbnailStrip::PreviewStatus;

struct Preview
{
    QByteArray version;
    QImage image;
    PreviewStatus status = PreviewStatus::Unavailable;
    qint64 retryAfter = 0;
    bool needsSlowDecode = false;
};

struct PreviewCache
{
    QMutex mutex;
    QCache<QString, Preview> entries{ PreviewCacheBytes };
};

QImage readQtPreview(const QString &path, const QSize &size, const qint64 fileBytes, bool &limited)
{
    if (fileBytes > MaxQtSourceBytes) {
        limited = true;
        return { };
    }

    QImageReader reader(path);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return { };

    const auto format = reader.format();
    const bool vector = format == "svg" || format == "svgz";
    if (vector
        && (fileBytes > MaxSvgSourceBytes || !reader.supportsOption(QImageIOHandler::ScaledSize))) {
        limited = true;
        return { };
    }

    const QSize source = reader.size();
    if (source.isEmpty())
        return { };
    if (!vector && qint64(source.width()) * source.height() > MaxEstimatedBytes / 16) {
        limited = true;
        return { };
    }

    const bool rotated = reader.transformation().testFlag(QImageIOHandler::TransformationRotate90);
    const QSize bounds = rotated ? size.transposed() : size;
    reader.setScaledSize(source.scaled(bounds, Qt::KeepAspectRatio));
    return reader.read();
}

bool isQuickThumbnail(const QString &path, qint64 fileBytes)
{
    if (fileBytes > 8 * 1024 * 1024)
        return false;
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "png" && suffix != "jpg" && suffix != "jpeg")
        return false;
    QImageReader reader(path);
    const auto format = reader.format();
    if (format != "png" && format != "jpeg")
        return false;
    const QSize size = reader.size();
    return !size.isEmpty() && qint64(size.width()) * size.height() <= MaxEstimatedBytes / 16;
}

Preview readPreview(const QString &path, const QSize &size, PreviewCache &cache, bool slow)
{
    if (QvAsync::isCanceled())
        return { };
    const auto version = QVImageLoader::fileVersion(path);
    if (version.isEmpty())
        return { };
    const QString key = path + QLatin1Char('\n') + QString::fromLatin1(version) + QLatin1Char('\n')
            + QString::number(size.width()) + QLatin1Char('x') + QString::number(size.height());
    {
        QMutexLocker lock(&cache.mutex);
        if (const auto *entry = cache.entries.object(key)) {
            if (entry->retryAfter > QDateTime::currentMSecsSinceEpoch()
                || entry->status == PreviewStatus::Ready || entry->status == PreviewStatus::Limited)
                return *entry;
        }
    }

    Preview result;
    result.version = version;
    if (QVCocoaFunctions::thumbnailMayReadContent(path)) {
        const qint64 fileBytes = QFileInfo(path).size();
        if (!slow && fileBytes > 0 && !isQuickThumbnail(path, fileBytes)) {
            result.needsSlowDecode = true;
            return result;
        }
        bool limited = false;
        bool allowQtFallback = true;
        if (fileBytes > 0 && !QvAsync::isCanceled()) {
            result.image = QVCocoaFunctions::readThumbnail(path, qMax(size.width(), size.height()),
                                                           fileBytes, allowQtFallback, limited);
            if (result.image.isNull() && allowQtFallback && !QvAsync::isCanceled())
                result.image = readQtPreview(path, size, fileBytes, limited);
        }
        result.status = limited ? PreviewStatus::Limited : PreviewStatus::Failed;
    }

    auto &image = result.image;
    if (!image.isNull()) {
        if (image.width() > size.width() || image.height() > size.height())
            image = image.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        if (image.colorSpace().isValid())
            image = image.convertedToColorSpace(QColorSpace::SRgb);
        else
            image.setColorSpace(QColorSpace::SRgb);
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        result.status = PreviewStatus::Ready;
    }

    if (QvAsync::isCanceled() || QVImageLoader::fileVersion(path) != version)
        return { };
    result.retryAfter = QDateTime::currentMSecsSinceEpoch() + 3000;
    QMutexLocker lock(&cache.mutex);
    cache.entries.insert(key, new Preview(result), qMax<qsizetype>(256, image.sizeInBytes()));
    return result;
}

class PreviewQueue : public QObject
{
public:
    PreviewQueue() : QObject(qApp) { }

    using Callback = std::function<void(const Preview &)>;

    void request(QVThumbnailStrip *owner, const QString &path, const QSize &size,
                 quint64 generation, Callback callback)
    {
        for (auto &job : jobs) {
            if (job.path == path && job.size == size && !job.ticket->canceled.load()) {
                job.subscribers.insert(owner, { generation, std::move(callback) });
                return;
            }
        }
        jobs.append({ ++nextId,
                      path,
                      size,
                      { { owner, { generation, std::move(callback) } } },
                      std::make_shared<QvAsync::State>() });
    }

    void retain(QVThumbnailStrip *owner, quint64 generation)
    {
        for (auto &job : jobs) {
            const auto it = job.subscribers.find(owner);
            if (it != job.subscribers.end() && it->generation != generation)
                job.subscribers.erase(it);
        }
        prune();
        start();
    }

    void unsubscribe(QVThumbnailStrip *owner)
    {
        for (auto &job : jobs)
            job.subscribers.remove(owner);
        prune();
    }

    void invalidate(const QSet<QString> &paths)
    {
        QList<Job> replacements;
        for (auto &job : jobs) {
            if (paths.contains(job.path) && !job.subscribers.isEmpty()) {
                replacements.append({ ++nextId, job.path, job.size, std::move(job.subscribers),
                                      std::make_shared<QvAsync::State>() });
                job.subscribers.clear();
            }
        }
        prune();
        jobs.append(std::move(replacements));
    }

private:
    struct Subscription
    {
        quint64 generation;
        Callback callback;
    };

    struct Job
    {
        quint64 id;
        QString path;
        QSize size;
        QHash<QVThumbnailStrip *, Subscription> subscribers;
        QvAsync::Ticket ticket;
        bool slow = false;
        bool running = false;
    };

    void prune()
    {
        for (auto &job : jobs) {
            if (job.subscribers.isEmpty())
                QvAsync::cancel(job.ticket);
        }
        jobs.erase(std::remove_if(jobs.begin(), jobs.end(),
                                  [](const Job &job) {
                                      return !job.running && job.subscribers.isEmpty();
                                  }),
                   jobs.end());
    }

    void start()
    {
        start(false, fastRunning);
        start(true, slowRunning);
    }

    void start(bool slow, bool &running)
    {
        if (running)
            return;
        const auto job = std::find_if(jobs.begin(), jobs.end(), [slow](const Job &job) {
            return !job.running && job.slow == slow && !job.subscribers.isEmpty();
        });
        if (job == jobs.end())
            return;

        running = true;
        job->running = true;
        QvAsync::schedule(
                [owner = QPointer<PreviewQueue>(this), app = qApp, id = job->id, path = job->path,
                 size = job->size, ticket = job->ticket, cache = cache, slow] {
                    QvAsync::setCurrentTicket(ticket);
                    const auto clearTicket = qScopeGuard([] { QvAsync::setCurrentTicket({ }); });
                    auto preview = readPreview(path, size, *cache, slow);
                    // Even a canceled worker relinquishes its slot on completion.
                    QMetaObject::invokeMethod(
                            app,
                            [owner, id, preview = std::move(preview), slow] {
                                if (owner)
                                    owner->complete(id, preview, slow);
                            },
                            Qt::QueuedConnection);
                },
                0, QvAsync::Lane::ThumbnailDecode);
    }

    void complete(quint64 id, const Preview &preview, bool slow)
    {
        (slow ? slowRunning : fastRunning) = false;
        const auto job = std::find_if(jobs.begin(), jobs.end(),
                                      [id](const Job &candidate) { return candidate.id == id; });
        if (job != jobs.end()) {
            const bool canceled = job->ticket->canceled.load();
            if (!canceled && preview.needsSlowDecode) {
                job->slow = true;
                job->running = false;
            } else {
                auto subscribers = std::move(job->subscribers);
                jobs.erase(job);
                if (!canceled) {
                    // One callback can change another window's state, so take weak references
                    // before delivering any results.
                    QList<QPair<QPointer<QVThumbnailStrip>, Callback>> deliveries;
                    for (auto it = subscribers.begin(); it != subscribers.end(); ++it)
                        deliveries.append({ it.key(), std::move(it->callback) });
                    for (const auto &[owner, callback] : deliveries) {
                        if (owner)
                            callback(preview);
                    }
                }
            }
        }
        start();
    }

    QList<Job> jobs;
    std::shared_ptr<PreviewCache> cache = std::make_shared<PreviewCache>();
    quint64 nextId = 0;
    bool fastRunning = false;
    bool slowRunning = false;
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

class ThumbnailWatch
{
public:
    explicit ThumbnailWatch(std::function<void(bool, const QSet<QString> &, bool)> callback)
        : callback(std::move(callback))
    {
    }

    ~ThumbnailWatch() { stop(); }

    bool observe(const QString &path, const QString &physicalRoot, bool recurse)
    {
        recursive = recurse;
        // FSEvents reports the physical directory resolved by the worker.
        const auto prefix = physicalRoot.isEmpty() || physicalRoot.endsWith(QLatin1Char('/'))
                ? physicalRoot
                : physicalRoot + QLatin1Char('/');
        if (path == root && prefix == physicalPrefix)
            return false;
        stop();
        root = path;
        physicalPrefix = prefix;
        if (physicalRoot.isEmpty())
            return false;

        const auto directory = physicalRoot.toCFString();
        const void *directories[] = { directory };
        const auto paths = CFArrayCreate(nullptr, directories, 1, &kCFTypeArrayCallBacks);
        FSEventStreamContext context{ 0, this, nullptr, nullptr, nullptr };
        stream = FSEventStreamCreate(nullptr, &ThumbnailWatch::dispatch, &context, paths,
                                     kFSEventStreamEventIdSinceNow, 0.08,
                                     kFSEventStreamCreateFlagFileEvents
                                             | kFSEventStreamCreateFlagWatchRoot
                                             | kFSEventStreamCreateFlagNoDefer);
        CFRelease(paths);
        CFRelease(directory);
        if (stream) {
            FSEventStreamSetDispatchQueue(stream, dispatch_get_main_queue());
            if (!FSEventStreamStart(stream))
                stop();
        }
        return stream != nullptr;
    }

private:
    void stop()
    {
        if (!stream)
            return;
        FSEventStreamStop(stream);
        FSEventStreamInvalidate(stream);
        FSEventStreamRelease(stream);
        stream = nullptr;
    }

    static void dispatch(ConstFSEventStreamRef, void *context, size_t count, void *eventPaths,
                         const FSEventStreamEventFlags flags[], const FSEventStreamEventId[])
    {
        auto &watch = *static_cast<ThumbnailWatch *>(context);
        const auto paths = static_cast<char **>(eventPaths);
        bool rescan = false;
        bool all = false;
        QSet<QString> changed;
        for (size_t i = 0; i < count; ++i) {
            const auto flag = flags[i];
            if (flag
                & (kFSEventStreamEventFlagMustScanSubDirs | kFSEventStreamEventFlagRootChanged)) {
                rescan = true;
                all = true;
                continue;
            }
            const QString path = QFile::decodeName(paths[i]);
            if (!path.startsWith(watch.physicalPrefix))
                continue;
            const auto relative = QStringView(path).mid(watch.physicalPrefix.size());
            if (!watch.recursive && relative.contains(QLatin1Char('/')))
                continue;
            if (flag
                & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved
                   | kFSEventStreamEventFlagItemRenamed))
                rescan = true;
            if (flag
                & (kFSEventStreamEventFlagItemCreated | kFSEventStreamEventFlagItemRemoved
                   | kFSEventStreamEventFlagItemRenamed | kFSEventStreamEventFlagItemModified
                   | kFSEventStreamEventFlagItemInodeMetaMod | kFSEventStreamEventFlagItemXattrMod))
                changed.insert(QDir(watch.root)
                                       .filePath(relative.toString())
                                       .normalized(QString::NormalizationForm_C));
        }
        if (rescan || !changed.isEmpty())
            watch.callback(rescan, changed, all);
    }

    std::function<void(bool, const QSet<QString> &, bool)> callback;
    QString root;
    QString physicalPrefix;
    FSEventStreamRef stream = nullptr;
    bool recursive = false;
};

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
    watch = std::make_unique<ThumbnailWatch>(
            [this](bool rescan, const QSet<QString> &paths, bool all) {
                filesChanged(rescan, paths, all);
            });
    directoryChangeTimer.setSingleShot(true);
    directoryChangeTimer.setInterval(100);
    connect(&directoryChangeTimer, &QTimer::timeout, this, [this] {
        if (!view->thumbnailImageCore().checkThumbnailListing())
            directoryChangeTimer.start();
    });
    validationAdmission.setSingleShot(true);
    validationAdmission.setInterval(25);
    connect(&validationAdmission, &QTimer::timeout, this, &QVThumbnailStrip::submitValidation);

    for (auto *timer :
         { &enterTimer, &leaveTimer, &gestureTimer, &validationTimer, &wheelSequenceTimer })
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

    connect(&wheelSequenceTimer, &QTimer::timeout, this,
            [this] { wheelOwner = ScrollOwner::None; });
    selectionFade.setDuration(180);
    selectionFade.setEasingCurve(QEasingCurve::InOutQuad);
    selectionFade.setStartValue(0.0);
    selectionFade.setEndValue(1.0);
    connect(&selectionFade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        if (outgoingSurface.isNull())
            return;
        selectionProgress = value.toReal();
        updateRendering(false);
    });
    connect(&selectionFade, &QVariantAnimation::finished, this,
            &QVThumbnailStrip::finishTransition);

    auto &core = view->thumbnailImageCore();
    connect(&core, &QVImageCore::thumbnailListingChanged, this, &QVThumbnailStrip::refresh);
    connect(&core, &QVImageCore::folderInfoChanged, this, &QVThumbnailStrip::refresh);
    connect(&core, &QVImageCore::imageRequestStarted, this, [this] {
        cancelActivation();
        updateRendering(true);
    });
    connect(view, &QVGraphicsView::fileChanged, this, [this] {
        currentSourceChanged = false;
        cancelActivation();
        refreshVisible(true);
    });
    connect(&core, &QVImageCore::thumbnailOpenRejected, this, [this] {
        const auto path = waitingPath;
        cancelActivation();
        window->showThumbnailMessage(tr("The image changed. Choose it again."));
        if (!path.isEmpty())
            filesChanged(false, { path.normalized(QString::NormalizationForm_C) });
    });
}

QVThumbnailStrip::~QVThumbnailStrip()
{
    QvAsync::cancel(validationTicket);
    previewQueue().unsubscribe(this);
}

void QVThumbnailStrip::syncWatch()
{
    const auto &details = view->getCurrentFileDetails();
    const auto &listing = details.folderFileInfoList;
    if (!details.isPixmapLoaded || view->thumbnailImageCore().getSourceLifetime()) {
        watch->observe({ }, { }, false);
        watchNeedsCatchup = false;
        return;
    }
    if (watch->observe(listing.getBaseDir(), listing.getPhysicalBaseDir(),
                       listing.getIsRecursive())) {
        watchNeedsCatchup = true;
        if (targetOpacity > 0) {
            watchNeedsCatchup = false;
            directoryChangeTimer.start();
        }
    }
}

void QVThumbnailStrip::filesChanged(bool rescan, const QSet<QString> &paths, bool all)
{
    auto &core = view->thumbnailImageCore();
    if (rescan && !directoryChangeTimer.isActive())
        directoryChangeTimer.start();

    QSet<QString> invalidate;
    QRect affected;
    if (all
        || paths.contains(core.getCurrentFileDetails().fileInfo.absoluteFilePath().normalized(
                QString::NormalizationForm_C)))
        currentSourceChanged = true;
    for (int i = 0; i < visibleCells.size(); ++i) {
        auto &cell = visibleCells[i];
        if (cell.path.isEmpty()
            || (!all && !paths.contains(cell.path.normalized(QString::NormalizationForm_C))))
            continue;
        invalidate.insert(cell.path);
        affected |= cell.rect.toAlignedRect();
        if (pressedSlot == i)
            pressedSlot = -1;
        cell.preview = { };
        cell.version.clear();
        cell.currentProxy = false;
        cell.status = PreviewStatus::Loading;
    }
    if (invalidate.isEmpty())
        return;
    // Preserve the version captured by any already pending click. The loader
    // will reject that click if the file's identity changed.
    previewQueue().invalidate(invalidate);
    if (targetOpacity > 0) {
        requestPreviews();
        updateRendering(true, affected);
    }
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

QRectF QVThumbnailStrip::cellClip() const
{
    return QRectF(panel).adjusted(Padding, 0, -Padding, 0);
}

int QVThumbnailStrip::slotAt(const QPoint &pos) const
{
    if (!cellClip().contains(pos))
        return -1;
    for (int i = 0; i < visibleCells.size(); ++i) {
        if (visibleCells.at(i).index >= 0 && visibleCells.at(i).rect.contains(pos))
            return i;
    }
    return -1;
}

bool QVThumbnailStrip::hitTest(const QPoint &pos) const
{
    return canBrowseAt(pos) && outgoingSurface.isNull() && slotAt(pos) >= 0;
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
    finishTransition();
    browsePosition = view->getCurrentFileDetails().loadedIndexInFolder;
    rebuild();
    focusSlot = slotAt(panel.center());
    view->viewport()->setFocus(Qt::OtherFocusReason);
    if (targetOpacity > 0) {
        requestPreviews();
        updateRendering(true);
    } else {
        animateTo(true);
    }
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
    refreshVisible(false);
}

void QVThumbnailStrip::refreshVisible(const bool imageCommitted)
{
    syncWatch();
    const auto &core = view->thumbnailImageCore();
    if (!core.thumbnailListingReady()) {
        pressedSlot = -1;
        finishTransition();
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
        const int next = core.getCurrentFileDetails().loadedIndexInFolder;
        const bool changed = currentIndex != next;
        QImage previous;
        if (imageCommitted && changed && listVersion == core.thumbnailListVersion()
            && opacityValue > 0.001 && !QVCocoaFunctions::reduceThumbnailMotion()) {
            previous = transitionSnapshot();
        }
        if (imageCommitted && changed)
            browsePosition = next;
        rebuild();
        requestPreviews();
        const qreal dpr = view->viewport()->devicePixelRatioF();
        if (!previous.isNull()) {
            selectionFade.stop();
            outgoingSurface = fitTransitionSurface(previous, panel.size(), dpr);
            outgoingDirty = true;
            selectionProgress = 0;
            updateRendering(true);
            selectionFade.start();
        } else {
            if (imageCommitted && changed) {
                finishTransition();
            } else if (!outgoingSurface.isNull()) {
                const auto fitted = fitTransitionSurface(outgoingSurface, panel.size(), dpr);
                if (fitted.cacheKey() != outgoingSurface.cacheKey()) {
                    outgoingSurface = fitted;
                    outgoingDirty = true;
                }
            }
            updateRendering(true);
        }
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
    if (visible && watchNeedsCatchup) {
        watchNeedsCatchup = false;
        directoryChangeTimer.start();
    }
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
    selectionFade.stop();
    outgoingSurface = { };
    outgoingDirty = false;
    selectionProgress = 1;
    browsePosition = -1;
    ++previewGeneration;
    if (!visibleCells.isEmpty())
        previewQueue().unsubscribe(this);
    visibleCells.clear();
    const bool hadPanel = !panel.isEmpty();
    panel = { };
    background = { };
    animatedContent = { };
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

bool QVThumbnailStrip::rebuild()
{
    const auto &core = view->thumbnailImageCore();
    const auto &details = core.getCurrentFileDetails();
    const int nextCurrent = details.loadedIndexInFolder;
    const QString oldFocus = focusSlot >= 0 && focusSlot < visibleCells.size()
            ? visibleCells.at(focusSlot).path
            : QString();
    const bool newListing = listVersion != core.thumbnailListVersion();
    if (newListing || currentIndex != nextCurrent)
        pressedSlot = -1;
    listVersion = core.thumbnailListVersion();
    const auto oldCells = std::move(visibleCells);
    if (newListing || currentIndex != nextCurrent || browsePosition < 0)
        browsePosition = nextCurrent;

    const int count = details.folderFileInfoList.size();
    const bool loop = core.thumbnailLoopEnabled();
    // Anchor circular motion to the nearest slot, so a tiny move in either
    // direction keeps the same duplicate-free arrangement in a small folder.
    const int position = loop ? qRound(browsePosition) : qFloor(browsePosition);
    const int anchor = loop && count > 0 ? position % count : position;
    const qreal fraction = browsePosition - position;
    visibleCells = layout(usableViewport(), count, anchor, loop);
    if (visibleCells.isEmpty()) {
        panel = { };
        return !oldCells.isEmpty();
    }

    const QRect nextPanel = visibleCells.first()
                                    .rect.united(visibleCells.last().rect)
                                    .adjusted(-Padding, -Padding, Padding, Padding)
                                    .toAlignedRect();
    if (!qFuzzyIsNull(fraction)) {
        const int direction = fraction > 0 ? 1 : -1;
        int enteringIndex = anchor + direction * (visibleCells.size() / 2 + 1);
        if (loop)
            enteringIndex = (enteringIndex % count + count) % count;
        const qreal pitch = visibleCells.first().rect.width() + Gap;
        if (enteringIndex >= 0 && enteringIndex < count
            && std::none_of(
                    visibleCells.cbegin(), visibleCells.cend(),
                    [enteringIndex](const Cell &cell) { return cell.index == enteringIndex; })) {
            if (direction > 0)
                visibleCells.append(
                        { visibleCells.last().rect.translated(pitch, 0), enteringIndex });
            else
                visibleCells.prepend(
                        { visibleCells.first().rect.translated(-pitch, 0), enteringIndex });
        }
        for (auto &cell : visibleCells)
            cell.rect.translate(-fraction * pitch, 0);
    }
    const auto oldClip = cellClip();
    if (panel != nextPanel) {
        panel = nextPanel;
        emit overlayGeometryChanged();
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
                cell.status = old.status;
                cell.requestedPreviewSize = old.requestedPreviewSize;
                cell.tile = old.tile;
                cell.tileSource = old.tileSource;
                cell.tileStyle = old.tileStyle;
                break;
            }
        }
    }
    if (pressedSlot >= 0
        && (pressedSlot >= visibleCells.size()
            || visibleCells.at(pressedSlot).path != oldCells.at(pressedSlot).path))
        pressedSlot = -1;
    if (focusSlot >= 0) {
        focusSlot = -1;
        qreal nearest = panel.width();
        for (int i = 0; i < visibleCells.size(); ++i) {
            const auto &cell = visibleCells.at(i);
            if (cell.index < 0 || !cellClip().intersects(cell.rect))
                continue;
            if ((nextCurrent == currentIndex && cell.path == oldFocus)
                || (nextCurrent != currentIndex && cell.index == nextCurrent)) {
                focusSlot = i;
                break;
            }
            const qreal distance = qAbs(cell.rect.center().x() - panel.center().x());
            if (distance < nearest) {
                nearest = distance;
                focusSlot = i;
            }
        }
    }
    currentIndex = nextCurrent;
    const auto &proxy = core.getLoadedPixmap();
    if (!currentSourceChanged && !proxy.isNull() && proxy.width() <= 512 && proxy.height() <= 512) {
        for (auto &cell : visibleCells) {
            if (cell.index != nextCurrent || !cell.preview.isNull())
                continue;
            cell.preview = proxy.toImage();
            if (cell.preview.colorSpace().isValid())
                cell.preview = cell.preview.convertedToColorSpace(QColorSpace::SRgb);
            cell.currentProxy = true;
            cell.status = PreviewStatus::Ready;
            break;
        }
    }

    if (newListing || oldCells.size() != visibleCells.size())
        return true;
    const auto newClip = cellClip();
    for (int i = 0; i < oldCells.size(); ++i) {
        if (oldCells.at(i).path != visibleCells.at(i).path
            || oldCells.at(i).rect.size() != visibleCells.at(i).rect.size()
            || oldClip.intersects(oldCells.at(i).rect)
                    != newClip.intersects(visibleCells.at(i).rect))
            return true;
    }
    return false;
}

void QVThumbnailStrip::requestPreviews()
{
    const quint64 generation = ++previewGeneration;
    auto &queue = previewQueue();
    const qreal scale = view->viewport()->devicePixelRatioF();
    QList<int> order;
    for (int i = 0; i < visibleCells.size(); ++i) {
        if (visibleCells.at(i).index >= 0 && cellClip().intersects(visibleCells.at(i).rect))
            order.append(i);
    }
    std::stable_sort(order.begin(), order.end(), [this](int a, int b) {
        return qAbs(visibleCells.at(a).rect.center().x() - panel.center().x())
                < qAbs(visibleCells.at(b).rect.center().x() - panel.center().x());
    });
    for (const int i : order) {
        const auto &cell = visibleCells.at(i);
        const QSize size(qCeil(cell.rect.width() * scale), qCeil(cell.rect.height() * scale));
        if (!cell.currentProxy && !cell.version.isEmpty() && cell.requestedPreviewSize == size
            && (cell.status == PreviewStatus::Ready || cell.status == PreviewStatus::Limited))
            continue;
        queue.request(this, cell.path, size, generation,
                      [this, generation, path = cell.path, size](const Preview &preview) {
                          if (generation != previewGeneration || targetOpacity <= 0)
                              return;
                          const auto bound =
                                  std::find_if(visibleCells.begin(), visibleCells.end(),
                                               [&path](const Cell &c) { return c.path == path; });
                          if (bound == visibleCells.end())
                              return;
                          if (bound->preview.cacheKey() == preview.image.cacheKey()
                              && bound->status == preview.status
                              && bound->version == preview.version) {
                              bound->requestedPreviewSize = size;
                              return;
                          }
                          if (!preview.image.isNull() || !bound->currentProxy) {
                              bound->preview = preview.image;
                              bound->currentProxy = false;
                              bound->status = preview.status;
                          }
                          bound->version = preview.version;
                          bound->requestedPreviewSize = size;
                          updateRendering(true, bound->rect.toAlignedRect());
                      });
    }
    queue.retain(this, generation);
}

void QVThumbnailStrip::moveFocus(const int step)
{
    const auto &core = view->thumbnailImageCore();
    if (!core.thumbnailListingReady() || focusSlot < 0 || focusSlot >= visibleCells.size())
        return;

    const int count = core.getCurrentFileDetails().folderFileInfoList.size();
    const int focusedIndex = visibleCells.at(focusSlot).index;
    if (focusedIndex < 0 || focusedIndex >= count)
        return;

    int target = focusedIndex + step;
    if (target < 0 || target >= count) {
        if (!core.thumbnailLoopEnabled())
            return;
        target = target < 0 ? count - 1 : 0;
    }

    const auto slotForTarget = [this, target] {
        const auto clip = cellClip();
        for (int i = 0; i < visibleCells.size(); ++i) {
            const auto &cell = visibleCells.at(i);
            if (cell.index == target && clip.intersects(cell.rect))
                return i;
        }
        return -1;
    };
    int slot = slotForTarget();
    bool contentChanged = false;
    if (slot < 0) {
        finishTransition();
        pressedSlot = -1;
        browsePosition = target;
        contentChanged = rebuild();
        slot = slotForTarget();
        hoverSlot = slotAt(lastPointer);
    }
    focusSlot = slot;
    if (contentChanged)
        requestPreviews();
    updateRendering(true);
}

void QVThumbnailStrip::activate(const int slot)
{
    auto &core = view->thumbnailImageCore();
    if (slot < 0 || slot >= visibleCells.size() || !waitingPath.isEmpty()
        || !outgoingSurface.isNull() || core.thumbnailNavigationBusy()
        || !core.thumbnailListingReady())
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
            owner->filesChanged(false, { path.normalized(QString::NormalizationForm_C) });
        } else if (!core.openThumbnail(index, path, snapshot, verified)) {
            owner->cancelActivation();
        }
    };
    waitingPath = path;
    validationDone = done;
    validationTimer.start(800);
    submitValidation();
    updateRendering(true);
}

void QVThumbnailStrip::submitValidation()
{
    if (!validationTimer.isActive() || !validationDone)
        return;
    const auto ticket = std::make_shared<QvAsync::State>();
    std::function<QByteArray(const QString &)> reader = QVImageLoader::fileVersion;
#ifdef FOVELLE_HDR_SURFACE_TESTING
    if (validationReader)
        reader = validationReader;
#endif
    const bool admitted = QvAsync::trySchedule(
            [owner = QPointer<QVThumbnailStrip>(this), app = qApp, path = waitingPath, ticket,
             reader = std::move(reader)] {
                const auto verified = reader(path);
                if (ticket->canceled.load())
                    return;
                QMetaObject::invokeMethod(
                        app,
                        [owner, verified, ticket] {
                            if (!owner || owner->validationTicket != ticket
                                || ticket->canceled.load())
                                return;
                            owner->validationTicket.reset();
                            auto done = std::move(owner->validationDone);
                            done(verified);
                        },
                        Qt::QueuedConnection);
            },
            QvAsync::Lane::ThumbnailMetadata);
    if (admitted)
        validationTicket = ticket;
    else
        validationAdmission.start();
}

void QVThumbnailStrip::cancelActivation()
{
    ++activationGeneration;
    validationTimer.stop();
    validationAdmission.stop();
    QvAsync::cancel(validationTicket);
    validationTicket.reset();
    validationDone = { };
    if (waitingPath.isEmpty())
        return;
    waitingPath.clear();
    updateRendering(true);
}

QImage QVThumbnailStrip::transitionSnapshot() const
{
    if (background.isNull())
        return { };

    QImage snapshot(background.size(), background.format());
    snapshot.setDevicePixelRatio(background.devicePixelRatioF());
    snapshot.setColorSpace(background.colorSpace());
    snapshot.fill(Qt::transparent);
    QPainter painter(&snapshot);
    if (!outgoingSurface.isNull()) {
        painter.setOpacity(1 - selectionProgress);
        painter.drawImage(QPoint(), outgoingSurface);
    }
    painter.setOpacity(selectionProgress);
    if (animatedContent.isNull())
        drawContent(painter);
    else
        painter.drawImage(QPoint(), animatedContent);
    painter.end();
    return snapshot;
}

void QVThumbnailStrip::finishTransition()
{
    if (outgoingSurface.isNull())
        return;
    selectionFade.stop();
    outgoingSurface = { };
    outgoingDirty = false;
    selectionProgress = 1;
    updateRendering(false);
}

bool QVThumbnailStrip::canBrowseAt(const QPointF &position) const
{
    return targetOpacity > 0 && opacityValue > 0.001 && !background.isNull() && !blocked()
            && QRectF(panel).contains(position)
            && view->thumbnailImageCore().thumbnailListingReady();
}

bool QVThumbnailStrip::handleWheel(QWheelEvent *event)
{
    const auto phase = event->phase();
    const bool inside = canBrowseAt(event->position());
    bool ownsEvent = inside;
    if (phase != Qt::NoScrollPhase) {
        if (phase == Qt::ScrollBegin || wheelOwner == ScrollOwner::None)
            wheelOwner = inside ? ScrollOwner::Strip : ScrollOwner::Viewport;
        if (phase == Qt::ScrollEnd || phase == Qt::ScrollMomentum)
            wheelSequenceTimer.start(350);
        else
            wheelSequenceTimer.stop();
        ownsEvent = wheelOwner == ScrollOwner::Strip;
    }
    if (!ownsEvent)
        return false;

    // Keep consuming a sequence that began here even if the strip closes or
    // the cursor moves away before macOS delivers its final momentum events.
    if (targetOpacity > 0 && !blocked() && view->thumbnailImageCore().thumbnailListingReady()
        && !visibleCells.isEmpty()) {
        if (inside)
            leaveTimer.stop();
        const bool pixel = !event->pixelDelta().isNull();
        const QPoint delta = pixel ? event->pixelDelta() : event->angleDelta();
        const qreal x = delta.x();
        const qreal y = delta.y();
        const qreal amount = qAbs(x) >= qAbs(y) ? x : y;
        const qreal divisor = pixel ? visibleCells.first().rect.width() + Gap : 120.0;
        scrollBy(-amount / divisor);
    }
    event->accept();
    return true;
}

bool QVThumbnailStrip::handleNativeBrowse(QNativeGestureEvent *event)
{
    const auto type = event->gestureType();
    const bool inside = canBrowseAt(event->position());
    if (type == Qt::BeginNativeGesture) {
        nativeGestureOwner = inside ? ScrollOwner::Strip : ScrollOwner::Viewport;
    } else if (type == Qt::EndNativeGesture) {
        const bool owned = nativeGestureOwner == ScrollOwner::Strip;
        nativeGestureOwner = ScrollOwner::None;
        if (owned)
            event->accept();
        return owned;
    } else if (type != Qt::PanNativeGesture && type != Qt::SwipeNativeGesture) {
        if (nativeGestureOwner != ScrollOwner::None)
            nativeGestureOwner = ScrollOwner::Viewport;
        return false;
    }

    const bool ownsEvent = nativeGestureOwner == ScrollOwner::Strip
            || (nativeGestureOwner == ScrollOwner::None && inside);
    if (!ownsEvent)
        return false;
    if (targetOpacity > 0 && !blocked() && view->thumbnailImageCore().thumbnailListingReady()
        && !visibleCells.isEmpty()) {
        if (inside)
            leaveTimer.stop();
        if (type == Qt::PanNativeGesture)
            scrollBy(-event->delta().x() / (visibleCells.first().rect.width() + Gap));
        else if (type == Qt::SwipeNativeGesture) {
            // Cocoa reports a left swipe at 180 degrees and a right swipe at 0.
            if (event->value() == 180)
                scrollBy(1);
            else if (event->value() == 0)
                scrollBy(-1);
        }
    }
    event->accept();
    return true;
}

void QVThumbnailStrip::scrollBy(const qreal slotDelta)
{
    const int lastIndex = view->getCurrentFileDetails().folderFileInfoList.size() - 1;
    if (lastIndex < 1 || qFuzzyIsNull(slotDelta))
        return;
    qreal next = browsePosition + slotDelta;
    if (view->thumbnailImageCore().thumbnailLoopEnabled()) {
        const int count = lastIndex + 1;
        next = std::fmod(next, count);
        if (next < 0)
            next += count;
    } else {
        next = qBound(0.0, next, qreal(lastIndex));
    }
    if (next == browsePosition)
        return;

    finishTransition();
    pressedSlot = -1;
    browsePosition = next;
    if (rebuild())
        requestPreviews();
    hoverSlot = slotAt(lastPointer);
    updateRendering(true, cellClip().toAlignedRect());
}

QString QVThumbnailStrip::previewStatusText(PreviewStatus status) const
{
    switch (status) {
    case PreviewStatus::Loading:
        return tr("Loading preview");
    case PreviewStatus::Limited:
        return tr("Preview exceeds limits");
    case PreviewStatus::Unavailable:
        return tr("Preview unavailable");
    case PreviewStatus::Failed:
        return tr("Preview failed");
    case PreviewStatus::Ready:
        return { };
    }
    Q_UNREACHABLE();
}

void QVThumbnailStrip::drawContent(QPainter &painter) const
{
    painter.drawImage(QPoint(), background);
    painter.save();
    painter.setClipRect(cellClip().translated(-panel.topLeft()), Qt::IntersectClip);
    for (const auto &cell : visibleCells) {
        if (!cell.tile.isNull() && cellClip().intersects(cell.rect))
            painter.drawImage(cell.rect.translated(-panel.topLeft()), cell.tile);
    }
    painter.restore();
}

void QVThumbnailStrip::updateRendering(const bool contentChanged, const QRect &changedCell)
{
    if (panel.isEmpty())
        return;
    const bool native = view->usesNativeHDRNavigationOverlay();
    bool redrawAll = changedCell.isNull();
    if (contentChanged || background.isNull()) {
        const qreal scale = view->viewport()->devicePixelRatioF();
        const QSize pixels(qCeil(panel.width() * scale), qCeil(panel.height() * scale));
        const auto theme = qvApp->getSettingsManager().getEnum<Qv::Theme>("theme");
        const bool dark = QVCocoaFunctions::resolvedTheme(theme) == Qv::Theme::Dark;
        const QColor foreground = dark ? QColor(250, 250, 250) : QColor(24, 24, 28);
        const auto focus = palette().color(QPalette::Highlight);
        QColor tint = dark ? QColor(22, 22, 24) : QColor(248, 248, 250);
        tint.setAlpha(QVCocoaFunctions::reduceThumbnailTransparency() ? 255 : 226);
        const bool textColorChanged = panelForeground != foreground || panelFocus != focus;
        if (background.isNull() || background.size() != pixels
            || background.devicePixelRatioF() != scale || panelTint != tint) {
            background = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
            background.setDevicePixelRatio(scale);
            background.setColorSpace(QColorSpace::SRgb);
            background.fill(Qt::transparent);
            QPainter painter(&background);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tint);
            painter.drawRoundedRect(QRectF(QPointF(), panel.size()), 10, 10);
            panelTint = tint;
            redrawAll = true;
#ifdef FOVELLE_HDR_SURFACE_TESTING
            ++backgroundRasters;
#endif
        }
        panelForeground = foreground;
        panelFocus = focus;
        redrawAll |= textColorChanged;

        const QRectF clip = cellClip();
        for (int i = 0; i < visibleCells.size(); ++i) {
            auto &cell = visibleCells[i];
            if (cell.index < 0 || !clip.intersects(cell.rect))
                continue;
            const bool hovered = i == hoverSlot;
            const bool focused = i == focusSlot;
            const bool current = cell.index == currentIndex;
            const bool waiting = cell.path == waitingPath;
            const int style = int(cell.status) * 16 + int(hovered) + 2 * int(focused)
                    + 4 * int(current) + 8 * int(waiting);
            const auto source = cell.preview.cacheKey();
            const QSize tilePixels(qCeil(cell.rect.width() * scale),
                                   qCeil(cell.rect.height() * scale));
            if (!textColorChanged && cell.tileSource == source && cell.tileStyle == style
                && cell.tile.size() == tilePixels && cell.tile.devicePixelRatioF() == scale)
                continue;

            cell.tile = QImage(tilePixels, QImage::Format_ARGB32_Premultiplied);
            cell.tile.setDevicePixelRatio(scale);
            cell.tile.setColorSpace(QColorSpace::SRgb);
            cell.tile.fill(Qt::transparent);
            cell.tileStyle = style;
            cell.tileSource = source;
#ifdef FOVELLE_HDR_SURFACE_TESTING
            ++tileRasters;
#endif
            QPainter painter(&cell.tile);
            painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            const QRectF rect(QPointF(), cell.rect.size());
            QColor tile = foreground;
            tile.setAlpha(hovered ? 54 : 27);
            painter.setPen(Qt::NoPen);
            painter.setBrush(tile);
            painter.drawRoundedRect(rect, 4, 4);

            if (!cell.preview.isNull()) {
                const auto size =
                        QSizeF(cell.preview.size())
                                .scaled(rect.adjusted(3, 3, -3, -3).size(), Qt::KeepAspectRatio);
                const QRectF dest(rect.center() - QPointF(size.width(), size.height()) / 2, size);
                painter.drawImage(dest, cell.preview);
            } else {
                painter.setPen(foreground);
                const auto extension = QFileInfo(cell.path).suffix().toUpper().left(6);
                const auto label = rect.adjusted(4, 3, -4, -3);
                painter.drawText(label, Qt::AlignCenter | Qt::TextWordWrap,
                                 extension + QLatin1Char('\n') + previewStatusText(cell.status));
            }
            if (current || focused) {
                painter.setBrush(Qt::NoBrush);
                painter.setPen(QPen(focused ? focus : foreground, 2));
                painter.drawRoundedRect(rect.adjusted(1, 1, -1, -1), 4, 4);
            }
            if (waiting) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(focus);
                painter.drawEllipse(rect.topRight() + QPointF(-7, 7), 3, 3);
            }
        }
    }

    if (contentChanged && geometry() != panel)
        setGeometry(panel);
    if ((opacityValue > 0 || targetOpacity > 0) && isHidden()) {
        show();
        raise();
    }
    if (native) {
        if (contentChanged || !nativeSurface) {
            const auto clip = cellClip();
            QList<QVCocoaFunctions::ThumbnailTile> tiles;
            for (const auto &cell : visibleCells) {
                if (!cell.tile.isNull() && clip.intersects(cell.rect))
                    tiles.append({ cell.rect, cell.tile });
            }
            view->setNativeThumbnailContent(panel, background, clip, tiles);
        }
        if (!outgoingSurface.isNull() && (outgoingDirty || !nativeSurface)) {
            view->setNativeThumbnailTransitionSurface(panel, outgoingSurface);
            outgoingDirty = false;
        }
        view->setNativeThumbnailTransitionProgress(selectionProgress);
        view->setNativeThumbnailOpacity(opacityValue);
    } else if (nativeSurface) {
        view->clearNativeThumbnailSurface();
    }
    nativeSurface = native;
    const bool active = targetOpacity > 0 && opacityValue > 0.001 && outgoingSurface.isNull();
    if (contentChanged || accessibilityActive != active) {
        accessibilityActive = active;
        syncAccessibleButtons();
    }
    if (!native) {
        const QRect dirty = redrawAll
                ? rect()
                : changedCell.intersected(cellClip().toAlignedRect()).translated(-panel.topLeft());
        // Qt painting applies opacity to each draw. Flatten the overlapping
        // pieces only while a group opacity is needed and patch dirty pixels
        // when a tile changes during that animation.
        if (opacityValue >= 1 && outgoingSurface.isNull()) {
            animatedContent = { };
        } else {
            const bool allocate = animatedContent.size() != background.size()
                    || animatedContent.devicePixelRatioF() != background.devicePixelRatioF();
            if (allocate) {
                animatedContent = QImage(background.size(), background.format());
                animatedContent.setDevicePixelRatio(background.devicePixelRatioF());
                animatedContent.setColorSpace(background.colorSpace());
                animatedContent.fill(Qt::transparent);
            }
            if (allocate || contentChanged) {
                QPainter painter(&animatedContent);
                if (!allocate)
                    painter.setClipRect(dirty);
                painter.setCompositionMode(QPainter::CompositionMode_Source);
                painter.fillRect(rect(), Qt::transparent);
                painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
                drawContent(painter);
            }
        }
        update(dirty);
    } else {
        animatedContent = { };
    }
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
            && outgoingSurface.isNull() && view->thumbnailImageCore().thumbnailListingReady();
    const auto &listing = view->getCurrentFileDetails().folderFileInfoList;
    for (int i = 0; i < accessibleButtons.size(); ++i) {
        auto *button = accessibleButtons.at(i);
        if (!enabled || i >= visibleCells.size() || visibleCells.at(i).index < 0
            || !cellClip().intersects(visibleCells.at(i).rect)) {
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
        button->setAccessibleDescription(previewStatusText(cell.status));
        button->setGeometry(
                cell.rect.intersected(cellClip()).toAlignedRect().translated(-panel.topLeft()));
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
    if (view->usesNativeHDRNavigationOverlay() || background.isNull())
        return;
    QPainter painter(this);
    if (!outgoingSurface.isNull()) {
        painter.setOpacity(opacityValue * (1 - selectionProgress));
        painter.drawImage(QPoint(), outgoingSurface);
    }
    painter.setOpacity(opacityValue * selectionProgress);
    if (animatedContent.isNull())
        drawContent(painter);
    else
        painter.drawImage(QPoint(), animatedContent);
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
        wheelSequenceTimer.stop();
        wheelOwner = ScrollOwner::None;
        nativeGestureOwner = ScrollOwner::None;
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
            if (QVCocoaFunctions::reduceThumbnailMotion()) {
                finishTransition();
                if (fade.state() == QAbstractAnimation::Running) {
                    fade.stop();
                    if (targetOpacity == 0)
                        hideImmediately();
                    else
                        opacityValue = targetOpacity;
                }
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
            if (handleNativeBrowse(static_cast<QNativeGestureEvent *>(event)))
                return true;
            gestureTimer.start(250);
            hideImmediately();
        }
        if (type == QEvent::Wheel) {
            auto *wheel = static_cast<QWheelEvent *>(event);
            if (handleWheel(wheel))
                return true;
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
                QString tooltip = QDir(listing.getBaseDir()).relativeFilePath(cell.path);
                const auto status = previewStatusText(cell.status);
                if (!status.isEmpty())
                    tooltip += QLatin1Char('\n') + status;
                QToolTip::showText(help->globalPos(), tooltip, this);
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
                && canBrowseAt(pos) && (slotAt(pos) >= 0 || !outgoingSurface.isNull())) {
                ownsPress = true;
                pressedSlot = type == QEvent::MouseButtonPress && hitTest(pos) ? slotAt(pos) : -1;
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
                else
                    moveFocus(code == Qt::Key_Left ? -1 : 1);
            }
            return true;
        }
        if (type == QEvent::KeyPress && (code == Qt::Key_Tab || code == Qt::Key_Backtab))
            dismissFocus();
    }
    return false;
}
