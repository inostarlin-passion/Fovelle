#include "qvimageloader.h"
#include "qvcocoafunctions.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMetaObject>
#include <QPainter>
#include <QSvgRenderer>
#include <QThreadPool>

#include <limits>

namespace
{
constexpr qint64 MaxVectorSourceBytes = 256LL * 1024LL * 1024LL;
constexpr int VectorPreviewLargestDimension = 512;
// Adjacent images are speculative. Bound both work visible in the file system
// and the minimum 8-bit RGBA footprint before entering a synchronous decoder,
// which exposes no cooperative cancellation point once it starts.
constexpr qint64 MaxPreloadSourceBytes = 128LL * 1024LL * 1024LL;
constexpr quint64 MaxPreloadDecodedBytes = 128ULL * 1024ULL * 1024ULL;
}

QVImageLoader::QVImageLoader(QObject *parent)
    : QObject(parent), imageThreadPool(this)
{
}

QVImageLoader::~QVImageLoader()
{
    lifetimeToken.reset();

    // Image-loader jobs are raw QRunnables, not QFuture-backed work. Isolate
    // them from QtConcurrent's global pool so queued speculative preloads can
    // be discarded here without leaving another component's future pending.
    imageThreadPool.clear();
    imageThreadPool.waitForDone();
}

void QVImageLoader::setLargestDimension(const int value)
{
    largestDimension = value;
}

quint64 QVImageLoader::requestImage(const QString &absoluteFilePath, const bool forceReload)
{
    const QString normalizedPath = normalizePath(absoluteFilePath);
    const FileIdentity identity = getFileIdentity(normalizedPath);

    auto targetEntryIt = entries.find(normalizedPath);
    if (targetEntryIt == entries.end())
    {
        Entry entry;
        entry.priority = 0;
        entry.expectedIdentity = identity;
        targetEntryIt = entries.insert(normalizedPath, std::move(entry));
    }
    else
    {
        targetEntryIt->priority = 0;
        targetEntryIt->expectedIdentity = identity;

        if (targetEntryIt->state == State::Cached &&
            getFileIdentity(targetEntryIt->result.value()) != identity)
        {
            targetEntryIt->state = State::Queued;
            targetEntryIt->result.reset();
        }
        else if (targetEntryIt->state == State::Loading &&
                 targetEntryIt->startedIdentity != identity)
        {
            targetEntryIt->reloadAfterFinish = true;
        }
    }

    Entry &targetEntry = targetEntryIt.value();
    const bool retryCachedError =
        targetEntry.state == State::Cached &&
        targetEntry.result.has_value() &&
        targetEntry.result->errorData.has_value();
    if (forceReload || retryCachedError)
    {
        if (targetEntry.state == State::Loading)
        {
            targetEntry.reloadAfterFinish = true;
        }
        else
        {
            targetEntry.state = State::Queued;
            targetEntry.result.reset();
        }
    }

    const quint64 requestId = ++nextRequestId;
    pendingRequest = PendingRequest {requestId, normalizedPath};

    if (targetEntry.state == State::Cached)
        queueCachedDelivery(requestId, normalizedPath);

    startReadyJobs();
    return requestId;
}

void QVImageLoader::clear()
{
    pendingRequest.reset();

    for (auto it = entries.begin(); it != entries.end();)
    {
        if (it->state == State::Loading)
        {
            it->desired = false;
            it->reloadAfterFinish = false;
            ++it;
        }
        else
        {
            it = entries.erase(it);
        }
    }
}

bool QVImageLoader::FileIdentity::operator==(const FileIdentity &other) const
{
    return fileSize == other.fileSize && lastModified == other.lastModified;
}

QString QVImageLoader::normalizePath(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QVImageLoader::PreloadAdmission
QVImageLoader::preloadAdmissionForFile(const QString &absoluteFilePath)
{
    PreloadAdmission admission;
    const QFileInfo fileInfo(absoluteFilePath);
    admission.fileSize = fileInfo.size();
    if (admission.fileSize < 0 || admission.fileSize > MaxPreloadSourceBytes)
    {
        admission.allowed = false;
        admission.reason = QStringLiteral("source-byte-limit");
        return admission;
    }

    QImageReader reader(absoluteFilePath);
    admission.pixelSize = reader.size();
    if (!admission.pixelSize.isValid())
        admission.pixelSize = QVCocoaFunctions::imagePixelSize(absoluteFilePath);

    if (!admission.pixelSize.isValid())
        return admission;

    const quint64 width = static_cast<quint64>(admission.pixelSize.width());
    const quint64 height = static_cast<quint64>(admission.pixelSize.height());
    if (width == 0 || height == 0
        || width > std::numeric_limits<quint64>::max() / height
        || width * height > std::numeric_limits<quint64>::max() / 4ULL)
    {
        admission.allowed = false;
        admission.estimatedDecodedBytes = std::numeric_limits<quint64>::max();
        admission.reason = QStringLiteral("decoded-byte-overflow");
        return admission;
    }

    // Qt documents a minimum 32-bit GUI allocation. Use that exact lower
    // bound here; HDR/vector-specific work may cost more and is therefore never
    // underestimated in a way that could admit the supplied multi-gigapixel
    // neighbor.
    admission.estimatedDecodedBytes = width * height * 4ULL;
    if (admission.estimatedDecodedBytes > MaxPreloadDecodedBytes)
    {
        admission.allowed = false;
        admission.reason = QStringLiteral("decoded-byte-limit");
    }
    return admission;
}

QVImageLoader::FileIdentity QVImageLoader::getFileIdentity(const QString &absoluteFilePath)
{
    const QFileInfo fileInfo(absoluteFilePath);
    return {fileInfo.size(), fileInfo.lastModified()};
}

QVImageLoader::FileIdentity QVImageLoader::getFileIdentity(const Result &result)
{
    return {result.fileSize, result.lastModified};
}

QVImageLoader::Result QVImageLoader::readFile(const QString &absoluteFilePath,
                                              const int largestDimension,
                                              const bool isPreload)
{
    QElapsedTimer decodeTimer;
    decodeTimer.start();

    if (isPreload)
    {
        const PreloadAdmission admission = preloadAdmissionForFile(absoluteFilePath);
        if (!admission.allowed)
        {
            const QFileInfo fileInfo(absoluteFilePath);
            Result rejected;
            rejected.absoluteFilePath = fileInfo.absoluteFilePath();
            rejected.fileSize = fileInfo.size();
            rejected.lastModified = fileInfo.lastModified();
            rejected.intrinsicSize = admission.pixelSize;
            rejected.preloadRejected = true;
            rejected.estimatedDecodedBytes = admission.estimatedDecodedBytes;
            rejected.preloadRejectionReason = admission.reason;
            rejected.decodeMilliseconds = decodeTimer.nsecsElapsed() / 1000000.0;
            return rejected;
        }
    }

    QImageReader imageReader(absoluteFilePath);
    imageReader.setAutoTransform(true);

    bool isMultiFrameImage = false;
    const QVCocoaFunctions::NativeImageReadResult nativeResult =
            QVCocoaFunctions::readImageWithImageIO(absoluteFilePath, qMin(largestDimension, 2048));
    const bool useNativeImageIO = nativeResult.isImageIOType;
    const bool useQtFallback =
            nativeResult.allowsQtFallback && nativeResult.image.isNull()
            && !nativeResult.vectorImage.isValid()
            && !nativeResult.isRaw && !nativeResult.sdrImage
            && !nativeResult.hdrImage;
    QSize intrinsicSize = nativeResult.intrinsicSize;
    QImage image = nativeResult.image;
    Qv::VectorImageData vectorImage = nativeResult.vectorImage;
    QString errorString;

    if (useQtFallback && (imageReader.format() == "svg" || imageReader.format() == "svgz") && !imageReader.size().isEmpty())
    {
        const QFileInfo vectorFileInfo(absoluteFilePath);
        QFile vectorFile(absoluteFilePath);
        if (vectorFileInfo.size() <= 0 || vectorFileInfo.size() > MaxVectorSourceBytes
            || !vectorFile.open(QIODevice::ReadOnly))
        {
            errorString = vectorFileInfo.size() > MaxVectorSourceBytes
                    ? QStringLiteral("SVG source exceeds the safety limit")
                    : QStringLiteral("SVG source could not be opened");
        }
        else
        {
            const QByteArray svgData = vectorFile.readAll();
            QSvgRenderer svgRenderer;
            const bool valid = svgRenderer.load(absoluteFilePath);
            QSize svgSize = svgRenderer.defaultSize();
            if (svgSize.isEmpty() && svgRenderer.viewBoxF().isValid())
                svgSize = svgRenderer.viewBoxF().size().toSize();
            if (!valid || svgSize.isEmpty())
            {
                errorString = QStringLiteral("SVG document is invalid or has no drawable size");
            }
            else
            {
                intrinsicSize = svgSize;
                vectorImage.format = Qv::VectorImageFormat::Svg;
                vectorImage.encodedData = svgData;
                vectorImage.sourcePath = vectorFileInfo.absoluteFilePath();
                vectorImage.logicalSize = QSizeF(intrinsicSize);

                const int previewLimit = qMax(1, qMin(largestDimension,
                                                       VectorPreviewLargestDimension));
                const QSize previewSize = intrinsicSize.scaled(
                    previewLimit, previewLimit, Qt::KeepAspectRatio);
                image = QImage(previewSize, QImage::Format_ARGB32_Premultiplied);
                if (!image.isNull())
                {
                    image.fill(Qt::transparent);
                    QPainter previewPainter(&image);
                    svgRenderer.render(&previewPainter, QRectF(QPointF(), QSizeF(previewSize)));
                }
            }
        }
    }
    else if (useQtFallback)
    {
        isMultiFrameImage = !imageReader.supportsOption(QImageIOHandler::Animation) && imageReader.imageCount() > 1;
        image = imageReader.read();
        errorString = imageReader.errorString();
    }

    if (image.isNull() && useNativeImageIO && !nativeResult.errorString.isEmpty())
    {
        errorString = nativeResult.errorString;
    }

    // Handle cases like icons containing multiple resolutions
    if (isMultiFrameImage)
    {
        qsizetype bestSize = image.sizeInBytes();
        while (imageReader.jumpToNextImage())
        {
            QImage candidateImage = imageReader.read();
            if (!candidateImage.isNull() && candidateImage.sizeInBytes() > bestSize)
            {
                bestSize = candidateImage.sizeInBytes();
                image = std::move(candidateImage);
            }
        }
    }

    const QFileInfo fileInfo(absoluteFilePath);

    Result result;
    result.image = std::move(image);
    result.vectorImage = std::move(vectorImage);
    result.sdrImage = nativeResult.sdrImage;
    result.hdrImage = nativeResult.hdrImage;
    result.hdrMetadata = nativeResult.hdrMetadata;
    result.absoluteFilePath = fileInfo.absoluteFilePath();
    result.fileSize = fileInfo.size();
    result.lastModified = fileInfo.lastModified();
    result.isMultiFrameImage = isMultiFrameImage;
    result.intrinsicSize = intrinsicSize;
    result.decodeMilliseconds = decodeTimer.nsecsElapsed() / 1000000.0;

    if (result.image.isNull() && !result.vectorImage.isValid()
        && !result.sdrImage && !result.hdrImage)
        result.errorData = ErrorData {imageReader.error(), errorString};

    return result;
}

bool QVImageLoader::isWanted(const QString &absoluteFilePath, const Entry &entry) const
{
    return entry.desired ||
        (pendingRequest.has_value() && pendingRequest->absoluteFilePath == absoluteFilePath);
}

void QVImageLoader::setDesiredImages(const QList<DesiredImage> &desiredImages)
{
    struct DesiredEntry
    {
        int priority;
        FileIdentity identity;
    };

    QHash<QString, DesiredEntry> desiredEntries;
    for (const DesiredImage &desiredImage : desiredImages)
    {
        const QString absoluteFilePath = normalizePath(desiredImage.absoluteFilePath);
        const FileIdentity identity = getFileIdentity(absoluteFilePath);
        auto desiredIt = desiredEntries.find(absoluteFilePath);
        if (desiredIt == desiredEntries.end())
        {
            desiredEntries.insert(absoluteFilePath, {desiredImage.priority, identity});
        }
        else
        {
            desiredIt->priority = qMin(desiredIt->priority, desiredImage.priority);
            desiredIt->identity = identity;
        }
    }

    for (auto it = entries.begin(); it != entries.end();)
    {
        const auto desiredIt = desiredEntries.constFind(it.key());
        if (desiredIt == desiredEntries.constEnd())
        {
            it->desired = false;
            const bool wanted = isWanted(it.key(), it.value());
            if (!wanted)
                it->reloadAfterFinish = false;

            if (it->state == State::Loading || wanted)
                ++it;
            else
                it = entries.erase(it);
            continue;
        }

        it->desired = true;
        it->priority = desiredIt->priority;
        it->expectedIdentity = desiredIt->identity;

        if (it->state == State::Cached && getFileIdentity(it->result.value()) != it->expectedIdentity)
        {
            it->state = State::Queued;
            it->result.reset();
        }
        else if (it->state == State::Loading && it->startedIdentity != it->expectedIdentity)
        {
            it->reloadAfterFinish = true;
        }

        desiredEntries.remove(it.key());
        ++it;
    }

    for (auto it = desiredEntries.constBegin(); it != desiredEntries.constEnd(); ++it)
    {
        Entry entry;
        entry.desired = true;
        entry.priority = it->priority;
        entry.expectedIdentity = it->identity;
        entries.insert(it.key(), std::move(entry));
    }

    startReadyJobs();
}

void QVImageLoader::queueCachedDelivery(const quint64 requestId, const QString &absoluteFilePath)
{
    QMetaObject::invokeMethod(
        this,
        [this, requestId, absoluteFilePath]() {
            deliverResult(requestId, absoluteFilePath);
        },
        Qt::QueuedConnection
    );
}

void QVImageLoader::deliverResult(const quint64 requestId, const QString &absoluteFilePath)
{
    if (!pendingRequest.has_value() ||
        pendingRequest->id != requestId ||
        pendingRequest->absoluteFilePath != absoluteFilePath)
    {
        return;
    }

    const auto entryIt = entries.constFind(absoluteFilePath);
    if (entryIt == entries.constEnd() || entryIt->state != State::Cached || !entryIt->result.has_value())
        return;

    const Result result = entryIt->result.value();
    pendingRequest.reset();
    emit imageReady(requestId, result);

    const auto currentEntryIt = entries.find(absoluteFilePath);
    if (currentEntryIt != entries.end() &&
        currentEntryIt->state == State::Cached &&
        !currentEntryIt->desired)
    {
        entries.erase(currentEntryIt);
    }
}

void QVImageLoader::startReadyJobs()
{
    std::optional<int> nextPriority;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (!isWanted(it.key(), it.value()) || it->state != State::Queued)
            continue;
        if (!nextPriority.has_value() || it->priority < nextPriority.value())
            nextPriority = it->priority;
    }

    if (!nextPriority.has_value())
        return;

    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (isWanted(it.key(), it.value()) &&
            it->state == State::Loading &&
            it->priority < nextPriority.value())
        {
            return;
        }
    }

    QStringList pathsToStart;
    for (auto it = entries.constBegin(); it != entries.constEnd(); ++it)
    {
        if (isWanted(it.key(), it.value()) &&
            it->state == State::Queued &&
            it->priority == nextPriority.value())
        {
            pathsToStart.append(it.key());
        }
    }

    for (const QString &absoluteFilePath : std::as_const(pathsToStart))
        startJob(absoluteFilePath);
}

void QVImageLoader::startJob(const QString &absoluteFilePath)
{
    auto entryIt = entries.find(absoluteFilePath);
    if (entryIt == entries.end() ||
        !isWanted(absoluteFilePath, entryIt.value()) ||
        entryIt->state != State::Queued)
    {
        return;
    }

    entryIt->state = State::Loading;
    entryIt->startedIdentity = entryIt->expectedIdentity;
    entryIt->reloadAfterFinish = false;
    const quint64 generation = ++entryIt->generation;
    const int priority = entryIt->priority;
    const bool isPreload = priority > 0;
    const int targetLargestDimension = largestDimension;
    emit loadStarted(absoluteFilePath, priority);

    QVImageLoader *loader = this;
    const std::weak_ptr<int> weakLifetime = lifetimeToken;
    QObject *dispatchContext = QCoreApplication::instance();
    imageThreadPool.start(
        [
            loader,
            weakLifetime,
            dispatchContext,
            absoluteFilePath,
            generation,
            targetLargestDimension,
            isPreload
        ]() {
            Result result = readFile(
                absoluteFilePath, targetLargestDimension, isPreload);
            QMetaObject::invokeMethod(
                dispatchContext,
                [
                    loader,
                    weakLifetime,
                    absoluteFilePath,
                    generation,
                    result = std::move(result)
                ]() mutable {
                    if (!weakLifetime.lock())
                        return;
                    loader->jobFinished(absoluteFilePath, generation, std::move(result));
                },
                Qt::QueuedConnection
            );
        },
        -priority
    );
}

void QVImageLoader::jobFinished(const QString &absoluteFilePath, const quint64 generation, Result result)
{
    auto entryIt = entries.find(absoluteFilePath);
    if (entryIt == entries.end() || entryIt->state != State::Loading || entryIt->generation != generation)
        return;

    if (!isWanted(absoluteFilePath, entryIt.value()))
    {
        entries.erase(entryIt);
        startReadyJobs();
        return;
    }


    if (result.preloadRejected)
    {
        emit preloadSkipped(absoluteFilePath, result.fileSize,
                            result.estimatedDecodedBytes,
                            result.preloadRejectionReason);
        const bool foregroundRequested = pendingRequest.has_value()
                && pendingRequest->absoluteFilePath == absoluteFilePath;
        if (foregroundRequested)
        {
            entryIt->state = State::Queued;
            entryIt->priority = 0;
            entryIt->reloadAfterFinish = false;
            entryIt->result.reset();
        }
        else
        {
            entries.erase(entryIt);
        }
        startReadyJobs();
        return;
    }

    const FileIdentity currentIdentity = getFileIdentity(absoluteFilePath);
    if (entryIt->reloadAfterFinish || getFileIdentity(result) != currentIdentity)
    {
        entryIt->state = State::Queued;
        entryIt->reloadAfterFinish = false;
        entryIt->expectedIdentity = currentIdentity;
        entryIt->result.reset();
        startReadyJobs();
        return;
    }

    entryIt->expectedIdentity = currentIdentity;
    entryIt->state = State::Cached;
    entryIt->result = std::move(result);

    if (pendingRequest.has_value() && pendingRequest->absoluteFilePath == absoluteFilePath)
        deliverResult(pendingRequest->id, absoluteFilePath);

    startReadyJobs();
}
