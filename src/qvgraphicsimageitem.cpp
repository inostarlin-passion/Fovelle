#include "qvgraphicsimageitem.h"

#include "qvcocoafunctions.h"

#include <QColorSpace>
#include <QDebug>
#include <QFutureWatcher>
#include <QLineF>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QSvgRenderer>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>

namespace
{
constexpr quint64 MaxVectorTilePixels = 64ULL * 1024ULL * 1024ULL;
constexpr int MaxVectorTileDimension = 16384;
constexpr int VectorTilePanOverscanPixels = 128;
// During an active pan, QGraphicsView already scrolls the retained backing
// store and exposes only the newly uncovered strip. Keep a small seam guard
// for that strip instead of rerendering 128 device pixels of unused content
// on both sides of every worker request.
constexpr int VectorTileInteractionOverscanPixels = 16;
// A vector tile must never be rendered below the density at which it is
// displayed. Downsampling during a gesture throws away vector detail, and no
// later filtering can reconstruct it when that tile is magnified again.
constexpr qreal VectorTileRenderScale = 1.0;
constexpr qsizetype MaxMultipleVectorTileBytes = 96LL * 1024LL * 1024LL;
constexpr int MaxRetainedVectorTiles = 2;
constexpr qreal DevicePixelAlignmentTolerance = 1e-4;

bool scaleEquivalent(const qreal lhs, const qreal rhs)
{
    return qAbs(lhs - rhs) <= qMax(1.0, qMax(qAbs(lhs), qAbs(rhs))) * 1e-9;
}

bool devicePixelAligned(const qreal value)
{
    return qAbs(value - qRound(value)) <= DevicePixelAlignmentTolerance;
}

bool canUseNearestVectorTileSampling(const QTransform &deviceTransform,
                                     const QRectF &drawnSourceRect,
                                     const QRectF &tilePixelRect,
                                     const QSize &tileImageSize,
                                     const QRectF &tileSourceRect,
                                     const qreal tileDeviceScaleX,
                                     const qreal tileDeviceScaleY,
                                     const qreal deviceScaleX,
                                     const qreal deviceScaleY)
{
    // A nearest-neighbor copy is exact only for an axis-aligned, one-to-one
    // mapping.  The source tile has already been antialiased from vector
    // commands, so this skips only a second interpolation pass; it never
    // enables the undersampled interaction tile that caused blurry edges.
    if (!scaleEquivalent(tileDeviceScaleX, deviceScaleX)
        || !scaleEquivalent(tileDeviceScaleY, deviceScaleY)
        || !scaleEquivalent(tileImageSize.width() / tileSourceRect.width(),
                            deviceScaleX)
        || !scaleEquivalent(tileImageSize.height() / tileSourceRect.height(),
                            deviceScaleY))
    {
        return false;
    }

    const qreal transformTolerance = DevicePixelAlignmentTolerance;
    if (qAbs(deviceTransform.m12()) > transformTolerance
        || qAbs(deviceTransform.m21()) > transformTolerance
        || deviceTransform.m11() <= 0.0 || deviceTransform.m22() <= 0.0
        || !scaleEquivalent(deviceTransform.m11(), deviceScaleX)
        || !scaleEquivalent(deviceTransform.m22(), deviceScaleY))
    {
        return false;
    }

    const QPointF destinationTopLeft = deviceTransform.map(
        drawnSourceRect.topLeft());
    const QPointF destinationBottomRight = deviceTransform.map(
        drawnSourceRect.bottomRight());
    return devicePixelAligned(destinationTopLeft.x())
            && devicePixelAligned(destinationTopLeft.y())
            && devicePixelAligned(destinationBottomRight.x())
            && devicePixelAligned(destinationBottomRight.y())
            && devicePixelAligned(tilePixelRect.left())
            && devicePixelAligned(tilePixelRect.top())
            && devicePixelAligned(tilePixelRect.right())
            && devicePixelAligned(tilePixelRect.bottom());
}

struct SvgWorkerRenderCache
{
    QString sourcePath;
    QByteArray encodedData;
    QRectF originalViewBox;
    std::unique_ptr<QSvgRenderer> renderer;
};

SvgWorkerRenderCache &svgWorkerRenderCache()
{
    // QSvgRenderer is reentrant, but a QObject instance must not be moved
    // between the GUI and worker threads.  A QThreadPool thread-local cache
    // keeps one parsed renderer per worker thread and source instead.
    thread_local SvgWorkerRenderCache cache;
    return cache;
}

QSvgRenderer *cachedWorkerSvgRenderer(const Qv::VectorImageData &image)
{
    SvgWorkerRenderCache &cache = svgWorkerRenderCache();
    if (cache.renderer && cache.sourcePath == image.sourcePath
        && cache.encodedData == image.encodedData)
    {
        return cache.renderer.get();
    }

    cache.renderer.reset();
    cache.sourcePath = image.sourcePath;
    cache.encodedData = image.encodedData;
    cache.originalViewBox = {};
    auto renderer = std::make_unique<QSvgRenderer>();
    bool loaded = !image.sourcePath.isEmpty()
            && renderer->load(image.sourcePath);
    if (!loaded && !image.encodedData.isEmpty())
        loaded = renderer->load(image.encodedData);
    if (!loaded || !renderer->isValid())
        return nullptr;
    cache.originalViewBox = renderer->viewBoxF();
    cache.renderer = std::move(renderer);
    return cache.renderer.get();
}
}

QVGraphicsImageItem::QVGraphicsImageItem(QGraphicsItem *parent)
    : QGraphicsObject(parent),
      asyncTileWatcher(std::make_unique<QFutureWatcher<AsyncTileResult>>())
{
    // Only the newest tile request can run at once. Keep that single worker
    // alive so successive SVG requests reuse its parsed renderer instead of
    // paying thread startup/teardown and parser allocation costs during a
    // drag.
    vectorThreadPool.setMaxThreadCount(1);
    vectorThreadPool.setExpiryTimeout(-1);
    // Qt's built-in item caches rasterize the whole item.  NoCache plus the
    // extended option lets this item keep its own strictly bounded,
    // terminal-density tiles around the actual exposed region.
    setCacheMode(QGraphicsItem::NoCache);
    setFlag(QGraphicsItem::ItemUsesExtendedStyleOption, true);
    QObject::connect(asyncTileWatcher.get(), &QFutureWatcher<AsyncTileResult>::finished,
                     this, [this]() { asyncVectorTileFinished(); });
}

QVGraphicsImageItem::~QVGraphicsImageItem()
{
    shutdownAsyncWork();
}

void QVGraphicsImageItem::shutdownAsyncWork()
{
    if (asyncWorkShutDown)
        return;

    asyncWorkShutDown = true;
    pendingAsyncRequest.reset();
    if (asyncTileWatcher && asyncTileWatcher->isRunning())
    {
        asyncTileWatcher->disconnect(this);
        asyncTileWatcher->cancel();
        asyncTileWatcher->waitForFinished();
    }

    vectorThreadPool.clear();
    vectorThreadPool.waitForDone();
}

void QVGraphicsImageItem::setPixmap(const QPixmap &pixmap)
{
    prepareGeometryChange();
    vectorImage = {};
    svgRenderer.reset();
    pdfDocument.reset();
    rasterPixmap = pixmap;
    vectorInteractionActive = false;
    ++vectorSourceGeneration;
    pendingAsyncRequest.reset();
    clearVectorTiles();
    lastVectorTilePixelSize = {};
    lastVectorTileSourceRect = {};
    completedVectorRenderCount = 0;
    completedVectorTileGenerationCount = 0;
    update();
}

void QVGraphicsImageItem::setTransformationMode(const Qt::TransformationMode mode)
{
    if (rasterTransformationMode == mode)
        return;
    rasterTransformationMode = mode;
    update();
}

void QVGraphicsImageItem::setVectorBackgroundBrush(const QBrush &brush)
{
    if (vectorBackgroundBrush == brush)
        return;
    vectorBackgroundBrush = brush;
    if (vectorImage.isValid())
        update();
}

bool QVGraphicsImageItem::setVectorImage(const Qv::VectorImageData &image)
{
    if (!image.isValid())
        return false;

    std::unique_ptr<QSvgRenderer> candidateRenderer;
    QVCocoaFunctions::PDFVectorDocumentPtr candidatePdfDocument;
    if (image.format == Qv::VectorImageFormat::Svg)
    {
        candidateRenderer = std::make_unique<QSvgRenderer>();
        bool loaded = !image.sourcePath.isEmpty()
                && candidateRenderer->load(image.sourcePath);
        if (!loaded && !image.encodedData.isEmpty())
            loaded = candidateRenderer->load(image.encodedData);
        if (!loaded || !candidateRenderer->isValid())
            return false;
    }
    else if (image.format == Qv::VectorImageFormat::Pdf)
    {
        candidatePdfDocument = QVCocoaFunctions::createPDFVectorDocument(
            image.encodedData);
        if (!candidatePdfDocument)
            return false;
    }

    prepareGeometryChange();
    vectorImage = image;
    svgRenderer = std::move(candidateRenderer);
    pdfDocument = std::move(candidatePdfDocument);
    vectorInteractionActive = false;
    ++vectorSourceGeneration;
    pendingAsyncRequest.reset();
    clearVectorTiles();
    lastVectorTilePixelSize = {};
    lastVectorTileSourceRect = {};
    completedVectorRenderCount = 0;
    completedVectorTileGenerationCount = 0;
    if (svgRenderer)
    {
        QObject::connect(svgRenderer.get(), &QSvgRenderer::repaintNeeded,
                         svgRenderer.get(), [this]() {
            ++vectorSourceGeneration;
            pendingAsyncRequest.reset();
            clearVectorTiles();
            update();
        });
    }
    update();
    return true;
}

void QVGraphicsImageItem::setVectorInteractionActive(const bool active)
{
    if (vectorInteractionActive == active)
        return;
    vectorInteractionActive = active;
    if (!active)
    {
        // The next paint only accepts an exact-scale tile and queues it on the
        // worker when needed.  Stale interaction work is replaced by that
        // latest idle request; source rendering never blocks the GUI thread.
        pendingAsyncRequest.reset();
        update();
    }
}

bool QVGraphicsImageItem::hasPendingVectorRefinement() const
{
    return vectorInteractionActive || activeAsyncRequest.has_value()
            || pendingAsyncRequest.has_value();
}

QRectF QVGraphicsImageItem::boundingRect() const
{
    if (vectorImage.isValid())
        return QRectF(QPointF(), vectorImage.logicalSize);
    if (rasterPixmap.isNull())
        return {};
    return QRectF(QPointF(), QSizeF(rasterPixmap.size())
                             / rasterPixmap.devicePixelRatio());
}

void QVGraphicsImageItem::paintRasterFallback(QPainter *painter) const
{
    if (!painter || rasterPixmap.isNull())
        return;
    // The vector preview is only a temporary fallback while its terminal-
    // density tile is being prepared.  It still needs filtered sampling when
    // it is enlarged during a drag; the vector tile path below supplies the
    // missing detail as soon as the worker completes.
    painter->setRenderHint(
        QPainter::SmoothPixmapTransform,
        vectorImage.isValid()
            || rasterTransformationMode == Qt::SmoothTransformation);
    if (vectorImage.isValid())
    {
        painter->drawPixmap(boundingRect(), rasterPixmap,
                            QRectF(QPointF(), QSizeF(rasterPixmap.size())));
    }
    else
    {
        painter->drawPixmap(QPointF(), rasterPixmap);
    }
}

void QVGraphicsImageItem::clearVectorTiles()
{
    vectorTiles.clear();
    vectorTileUseSerial = 0;
}

int QVGraphicsImageItem::matchingVectorTile(
        const QRectF &sourceRect, const qreal deviceScaleX,
        const qreal deviceScaleY) const
{
    constexpr qreal CoordinateTolerance = 1e-9;
    for (int index = 0; index < vectorTiles.size(); ++index)
    {
        const VectorTile &tile = vectorTiles.at(index);
        if (scaleEquivalent(tile.deviceScaleX, deviceScaleX)
            && scaleEquivalent(tile.deviceScaleY, deviceScaleY)
            && tile.sourceRect.adjusted(-CoordinateTolerance,
                                        -CoordinateTolerance,
                                        CoordinateTolerance,
                                        CoordinateTolerance).contains(sourceRect))
        {
            return index;
        }
    }
    return -1;
}

int QVGraphicsImageItem::bestReusableVectorTile(
        const QRectF &sourceRect, const qreal deviceScaleX,
        const qreal deviceScaleY) const
{
    int bestIndex = -1;
    qreal bestScore = 0.0;
    for (int index = 0; index < vectorTiles.size(); ++index)
    {
        const VectorTile &tile = vectorTiles.at(index);
        const QRectF overlap = tile.sourceRect.intersected(sourceRect);
        if (overlap.isEmpty())
            continue;
        const qreal overlapArea = overlap.width() * overlap.height();
        const qreal scalePenalty = 1.0
                + qAbs(std::log(deviceScaleX / tile.deviceScaleX))
                + qAbs(std::log(deviceScaleY / tile.deviceScaleY));
        const qreal score = overlapArea / scalePenalty;
        if (score > bestScore)
        {
            bestScore = score;
            bestIndex = index;
        }
    }
    return bestIndex;
}

void QVGraphicsImageItem::retainVectorTile(VectorTile tile) const
{
    tile.lastUse = ++vectorTileUseSerial;
    vectorTiles.append(std::move(tile));

    const auto retainedBytes = [this]() {
        qsizetype total = 0;
        for (const VectorTile &candidate : vectorTiles)
            total += candidate.image.sizeInBytes();
        return total;
    };
    while (vectorTiles.size() > MaxRetainedVectorTiles
           || (vectorTiles.size() > 1
               && retainedBytes() > MaxMultipleVectorTileBytes))
    {
        int oldestIndex = 0;
        for (int index = 1; index < vectorTiles.size(); ++index)
        {
            if (vectorTiles.at(index).lastUse
                < vectorTiles.at(oldestIndex).lastUse)
            {
                oldestIndex = index;
            }
        }
        vectorTiles.removeAt(oldestIndex);
    }
}

bool QVGraphicsImageItem::sameAsyncRequest(const AsyncTileRequest &lhs,
                                           const AsyncTileRequest &rhs)
{
    return lhs.generation == rhs.generation
            && lhs.pixelSize == rhs.pixelSize
            && scaleEquivalent(lhs.deviceScaleX, rhs.deviceScaleX)
            && scaleEquivalent(lhs.deviceScaleY, rhs.deviceScaleY)
            && lhs.svgFrame == rhs.svgFrame
            && lhs.sourceRect == rhs.sourceRect;
}

void QVGraphicsImageItem::requestAsyncVectorTile(
        const AsyncTileRequest &request)
{
    if (asyncWorkShutDown)
        return;
    if (request.generation != vectorSourceGeneration
        || request.pixelSize.isEmpty() || request.sourceRect.isEmpty())
    {
        return;
    }
    if (activeAsyncRequest.has_value())
    {
        if (sameAsyncRequest(activeAsyncRequest.value(), request))
            pendingAsyncRequest.reset();
        else
            pendingAsyncRequest = request;
        return;
    }
    startAsyncVectorTile(request);
}

void QVGraphicsImageItem::startAsyncVectorTile(AsyncTileRequest request)
{
    activeAsyncRequest = request;
    asyncTileWatcher->setFuture(QtConcurrent::run(&vectorThreadPool,
        [request = std::move(request)]() mutable {
            AsyncTileResult result;
            result.generation = request.generation;
            result.tile.sourceRect = request.sourceRect;
            result.tile.deviceScaleX = request.deviceScaleX;
            result.tile.deviceScaleY = request.deviceScaleY;

            if (request.vectorImage.format == Qv::VectorImageFormat::Pdf)
            {
                // The GUI item constructs this immutable document once.  A
                // tile request retains it so PDF parsing is not repeated for
                // every pan exposure.  The fallback keeps this helper robust
                // for any future request producer that omits the pointer.
                const auto document = request.pdfDocument
                    ? request.pdfDocument
                    : QVCocoaFunctions::createPDFVectorDocument(
                          request.vectorImage.encodedData,
                          &result.errorString);
                if (document)
                {
                    result.tile.image = document->renderTile(
                        request.vectorImage.logicalSize, request.sourceRect,
                        request.pixelSize, &result.errorString);
                }
            }
            else if (request.vectorImage.format == Qv::VectorImageFormat::Svg)
            {
                QSvgRenderer *renderer = cachedWorkerSvgRenderer(
                    request.vectorImage);
                if (renderer)
                {
                    // The GUI-thread renderer owns the animation clock.  A
                    // worker-local renderer is reentrant, and selecting the
                    // captured frame keeps animated SVG refinement coherent.
                    renderer->setCurrentFrame(request.svgFrame);
                    result.tile.image = QImage(
                        request.pixelSize, QImage::Format_ARGB32_Premultiplied);
                    if (!result.tile.image.isNull())
                    {
                        result.tile.image.fill(Qt::transparent);
                        QPainter tilePainter(&result.tile.image);
                        tilePainter.setRenderHint(QPainter::Antialiasing, true);
                        tilePainter.setRenderHint(QPainter::TextAntialiasing, true);
                        // Crop in SVG user space before rasterization instead
                        // of transforming the full document far outside this
                        // bitmap. Qt's raster paint engine only guarantees
                        // coordinates within roughly +/- 2^15; a 6400% view
                        // of a large document can otherwise cross that range
                        // even though the requested tile itself is bounded.
                        // setViewBox() is mutable. Restore the source viewBox
                        // captured when this worker-local renderer was parsed
                        // before calculating every independent tile, or a
                        // cached renderer would crop the next tile relative
                        // to the previous tile.
                        const QRectF declaredViewBox =
                                svgWorkerRenderCache().originalViewBox;
                        renderer->setViewBox(declaredViewBox);
                        const QSizeF logicalSize = request.vectorImage.logicalSize;
                        // Width/height-only SVG documents are valid as well.
                        // Keep their historical full-document mapping instead
                        // of publishing a transparent tile when no explicit
                        // viewBox was declared.
                        const QRectF originalViewBox =
                            declaredViewBox.isValid() && !declaredViewBox.isEmpty()
                                ? declaredViewBox
                                : QRectF(QPointF(), logicalSize);
                        const QRectF tileViewBox(
                            originalViewBox.left()
                                + request.sourceRect.left()
                                    * originalViewBox.width() / logicalSize.width(),
                            originalViewBox.top()
                                + request.sourceRect.top()
                                    * originalViewBox.height() / logicalSize.height(),
                            request.sourceRect.width()
                                * originalViewBox.width() / logicalSize.width(),
                            request.sourceRect.height()
                                * originalViewBox.height() / logicalSize.height());
                        if (tileViewBox.isValid() && !tileViewBox.isEmpty())
                        {
                            renderer->setViewBox(tileViewBox);
                            renderer->render(
                                &tilePainter,
                                QRectF(QPointF(), QSizeF(request.pixelSize)));
                        }
                        tilePainter.end();
                        result.tile.image.setColorSpace(QColorSpace::SRgb);
                    }
                }
                else
                {
                    result.errorString = QStringLiteral(
                        "The asynchronous SVG document is invalid");
                }
            }
            return result;
        }));
}

void QVGraphicsImageItem::asyncVectorTileFinished()
{
    const AsyncTileResult result = asyncTileWatcher->result();
    activeAsyncRequest.reset();
    if (result.generation == vectorSourceGeneration
        && !result.tile.image.isNull())
    {
        // Retain both transient interaction tiles and the exact tile requested
        // after the gesture becomes idle.  Vector parsing/rasterization never
        // needs to fall back to the GUI thread.
        retainVectorTile(result.tile);
        ++completedVectorTileGenerationCount;
        update();
    }
    else if (!result.errorString.isEmpty()
             && qEnvironmentVariableIsSet("FOVELLE_VECTOR_RENDER_LOG"))
    {
        qWarning().noquote() << "FOVELLE_VECTOR_RENDER async_error="
                             << result.errorString;
    }

    if (pendingAsyncRequest.has_value())
    {
        AsyncTileRequest pending = std::move(pendingAsyncRequest.value());
        pendingAsyncRequest.reset();
        if (pending.generation == vectorSourceGeneration)
        {
            startAsyncVectorTile(std::move(pending));
        }
    }
}

QSize QVGraphicsImageItem::boundedTileSize(const QSizeF &requestedSize)
{
    if (!requestedSize.isValid() || requestedSize.isEmpty())
        return {};

    qreal width = std::ceil(requestedSize.width());
    qreal height = std::ceil(requestedSize.height());
    const qreal largestDimension = qMax(width, height);
    if (largestDimension > MaxVectorTileDimension)
    {
        const qreal scale = MaxVectorTileDimension / largestDimension;
        width *= scale;
        height *= scale;
    }
    const qreal pixelCount = width * height;
    if (pixelCount > static_cast<qreal>(MaxVectorTilePixels))
    {
        const qreal scale = std::sqrt(static_cast<qreal>(MaxVectorTilePixels)
                                      / pixelCount);
        width *= scale;
        height *= scale;
    }
    QSize result(qBound(1, qCeil(width), MaxVectorTileDimension),
                 qBound(1, qCeil(height), MaxVectorTileDimension));
    while (static_cast<quint64>(result.width())
               * static_cast<quint64>(result.height()) > MaxVectorTilePixels)
    {
        if (result.width() >= result.height() && result.width() > 1)
            result.rwidth() -= 1;
        else if (result.height() > 1)
            result.rheight() -= 1;
        else
            break;
    }
    return result;
}

void QVGraphicsImageItem::paint(QPainter *painter,
                                const QStyleOptionGraphicsItem *option,
                                QWidget *widget)
{
    Q_UNUSED(widget)
    if (!painter)
        return;
    if (!vectorImage.isValid())
    {
        paintRasterFallback(painter);
        return;
    }

    QRectF exposedRect = option ? option->exposedRect : boundingRect();
    exposedRect = exposedRect.intersected(boundingRect());
    if (exposedRect.isEmpty())
        return;

    // A transparent fallback/tile is valid vector output: the artwork may
    // intentionally contain transparent pixels.  During a partial viewport
    // update, however, SourceOver cannot replace stale pixels left in the
    // newly exposed backing-store stripe.  Clear the exact exposed item area
    // first so an incomplete asynchronous tile can never reveal the previous
    // frame.  The view supplies either the solid theme brush or its
    // checkerboard brush.
    painter->save();
    painter->setCompositionMode(QPainter::CompositionMode_Source);
    painter->fillRect(exposedRect, vectorBackgroundBrush);
    painter->restore();

    const bool isSvg = vectorImage.format == Qv::VectorImageFormat::Svg
            && svgRenderer;
    const bool isPdf = vectorImage.format == Qv::VectorImageFormat::Pdf
            && pdfDocument;
    if (!isSvg && !isPdf)
    {
        paintRasterFallback(painter);
        return;
    }

    const QTransform deviceTransform = painter->deviceTransform();
    const QPointF deviceOrigin = deviceTransform.map(QPointF());
    const qreal deviceScaleX = QLineF(deviceOrigin,
        deviceTransform.map(QPointF(1.0, 0.0))).length();
    const qreal deviceScaleY = QLineF(deviceOrigin,
        deviceTransform.map(QPointF(0.0, 1.0))).length();
    if (!(deviceScaleX > 0.0) || !(deviceScaleY > 0.0)
        || !std::isfinite(deviceScaleX) || !std::isfinite(deviceScaleY))
    {
        return;
    }

    // One device-pixel overlap prevents antialias seams.  A small extra border
    // lets successive pan frames reuse a terminal-density tile; it is bounded
    // in device pixels and never grows with the document or zoom percentage.
    const qreal paddingX = 1.0 / deviceScaleX;
    const qreal paddingY = 1.0 / deviceScaleY;
    const QRectF sourceRect = exposedRect.adjusted(
        -paddingX, -paddingY, paddingX, paddingY).intersected(boundingRect());
    const auto tileRequest = [this, sourceRect, deviceScaleX, deviceScaleY]() {
        const int overscanPixels = vectorInteractionActive
                ? VectorTileInteractionOverscanPixels
                : VectorTilePanOverscanPixels;
        const qreal overscanX = overscanPixels / deviceScaleX;
        const qreal overscanY = overscanPixels / deviceScaleY;
        const QRectF renderedSourceRect = sourceRect.adjusted(
            -overscanX, -overscanY, overscanX, overscanY)
            .intersected(boundingRect());
        const qreal requestedScaleX = deviceScaleX * VectorTileRenderScale;
        const qreal requestedScaleY = deviceScaleY * VectorTileRenderScale;
        const QSize tileSize = boundedTileSize(QSizeF(
            renderedSourceRect.width() * requestedScaleX,
            renderedSourceRect.height() * requestedScaleY));
        return AsyncTileRequest {
            vectorImage, pdfDocument, renderedSourceRect, tileSize,
            requestedScaleX, requestedScaleY,
            svgRenderer ? svgRenderer->currentFrame() : 0,
            vectorSourceGeneration
        };
    };
    int tileIndex = matchingVectorTile(sourceRect, deviceScaleX, deviceScaleY);
    const bool cacheHit = tileIndex >= 0;
    bool reusedVectorTile = false;
    if (tileIndex < 0)
    {
        const AsyncTileRequest request = tileRequest();
        // A tile already generated for the current device density can cover
        // the visible rect while a newly exposed tile is being produced. Do
        // not continuously regenerate that same request; its device-pixel
        // overscan determines when a pan really needs another request.
        if (matchingVectorTile(sourceRect,
                               request.deviceScaleX,
                               request.deviceScaleY) < 0)
        {
            requestAsyncVectorTile(request);
        }
        tileIndex = bestReusableVectorTile(
            sourceRect, deviceScaleX, deviceScaleY);
        reusedVectorTile = tileIndex >= 0;
        if (tileIndex < 0)
        {
            // There is no source coverage to transform yet.  The bounded
            // preview keeps the UI responsive until the worker publishes the
            // requested interaction or exact terminal-density tile.
            paintRasterFallback(painter);
            return;
        }
    }

    VectorTile &tile = vectorTiles[tileIndex];
    tile.lastUse = ++vectorTileUseSerial;
    QRectF drawnSourceRect = sourceRect;
    if (reusedVectorTile
        && !tile.sourceRect.adjusted(-1e-9, -1e-9, 1e-9, 1e-9)
                .contains(sourceRect))
    {
        paintRasterFallback(painter);
        drawnSourceRect = sourceRect.intersected(tile.sourceRect);
        if (drawnSourceRect.isEmpty())
            return;
    }
    const qreal sourcePixelScaleX = tile.image.width()
            / tile.sourceRect.width();
    const qreal sourcePixelScaleY = tile.image.height()
            / tile.sourceRect.height();
    const QRectF tilePixelRect(
        (drawnSourceRect.left() - tile.sourceRect.left()) * sourcePixelScaleX,
        (drawnSourceRect.top() - tile.sourceRect.top()) * sourcePixelScaleY,
        drawnSourceRect.width() * sourcePixelScaleX,
        drawnSourceRect.height() * sourcePixelScaleY);
    // Tiles are rendered at terminal device density even during interaction.
    // For an exact integer-pixel translation, the tile is already antialiased
    // vector output and a nearest copy avoids a second interpolation pass.
    // Fractional scrolls, reused tiles, and transformed mappings retain
    // filtering so their edges stay clear.
    const bool nearestSampling =
            !qEnvironmentVariableIsSet("FOVELLE_VECTOR_FORCE_SMOOTH")
            && !reusedVectorTile
            && canUseNearestVectorTileSampling(
                deviceTransform, drawnSourceRect, tilePixelRect, tile.image.size(),
                tile.sourceRect, tile.deviceScaleX, tile.deviceScaleY,
                deviceScaleX, deviceScaleY);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, !nearestSampling);
    painter->drawImage(drawnSourceRect, tile.image, tilePixelRect);
    lastVectorTilePixelSize = tile.image.size();
    lastVectorTileSourceRect = tile.sourceRect;
    ++completedVectorRenderCount;
    if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_RENDER_LOG"))
    {
        qInfo().noquote() << QStringLiteral(
            "FOVELLE_VECTOR_RENDER format=%1 source=vector cache=%2")
            .arg(isPdf ? QStringLiteral("pdf") : QStringLiteral("svg"),
                 cacheHit ? QStringLiteral("hit")
                         : reusedVectorTile ? QStringLiteral("reused")
                                                  : QStringLiteral("miss"))
                         << "sampling="
                         << (nearestSampling ? QStringLiteral("nearest")
                                              : QStringLiteral("smooth"))
                         << "deviceScale=" << deviceScaleX << deviceScaleY
                         << "exposedRect=" << exposedRect
                         << "sourceRect=" << tile.sourceRect
                         << "tilePixels=" << tile.image.size();
    }
}
