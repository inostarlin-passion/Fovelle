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
constexpr qreal InteractiveVectorRenderScale = 0.75;
constexpr qsizetype MaxMultipleVectorTileBytes = 96LL * 1024LL * 1024LL;
constexpr int MaxRetainedVectorTiles = 2;

bool scaleEquivalent(const qreal lhs, const qreal rhs)
{
    return qAbs(lhs - rhs) <= qMax(1.0, qMax(qAbs(lhs), qAbs(rhs))) * 1e-9;
}
}

QVGraphicsImageItem::QVGraphicsImageItem(QGraphicsItem *parent)
    : QGraphicsObject(parent),
      asyncTileWatcher(std::make_unique<QFutureWatcher<AsyncTileResult>>())
{
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
    if (asyncTileWatcher && asyncTileWatcher->isRunning())
    {
        asyncTileWatcher->disconnect(this);
        asyncTileWatcher->cancel();
        asyncTileWatcher->waitForFinished();
    }
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
    update();
}

void QVGraphicsImageItem::setTransformationMode(const Qt::TransformationMode mode)
{
    if (rasterTransformationMode == mode)
        return;
    rasterTransformationMode = mode;
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
        // The next paint only accepts an exact-scale tile.  If the asynchronous
        // interaction renderer has not already produced one, paint() performs
        // one final source render after the gesture is idle.
        pendingAsyncRequest.reset();
        update();
    }
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
    painter->setRenderHint(QPainter::SmoothPixmapTransform,
                           rasterTransformationMode == Qt::SmoothTransformation);
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

int QVGraphicsImageItem::bestInteractiveVectorTile(
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
            && lhs.sourceRect == rhs.sourceRect;
}

void QVGraphicsImageItem::requestAsyncVectorTile(
        const AsyncTileRequest &request)
{
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
    asyncTileWatcher->setFuture(QtConcurrent::run(
        [request = std::move(request)]() mutable {
            AsyncTileResult result;
            result.generation = request.generation;
            result.tile.sourceRect = request.sourceRect;
            result.tile.deviceScaleX = request.deviceScaleX;
            result.tile.deviceScaleY = request.deviceScaleY;

            if (request.vectorImage.format == Qv::VectorImageFormat::Pdf)
            {
                const auto document = QVCocoaFunctions::createPDFVectorDocument(
                    request.vectorImage.encodedData, &result.errorString);
                if (document)
                {
                    result.tile.image = document->renderTile(
                        request.vectorImage.logicalSize, request.sourceRect,
                        request.pixelSize, &result.errorString);
                }
            }
            else if (request.vectorImage.format == Qv::VectorImageFormat::Svg)
            {
                QSvgRenderer renderer;
                bool loaded = !request.vectorImage.sourcePath.isEmpty()
                        && renderer.load(request.vectorImage.sourcePath);
                if (!loaded && !request.vectorImage.encodedData.isEmpty())
                    loaded = renderer.load(request.vectorImage.encodedData);
                if (loaded && renderer.isValid())
                {
                    result.tile.image = QImage(
                        request.pixelSize, QImage::Format_ARGB32_Premultiplied);
                    if (!result.tile.image.isNull())
                    {
                        result.tile.image.fill(Qt::transparent);
                        QPainter tilePainter(&result.tile.image);
                        tilePainter.setRenderHint(QPainter::Antialiasing, true);
                        tilePainter.setRenderHint(QPainter::TextAntialiasing, true);
                        const qreal scaleX = request.pixelSize.width()
                                / request.sourceRect.width();
                        const qreal scaleY = request.pixelSize.height()
                                / request.sourceRect.height();
                        tilePainter.setWorldTransform(QTransform(
                            scaleX, 0.0, 0.0, scaleY,
                            -request.sourceRect.left() * scaleX,
                            -request.sourceRect.top() * scaleY));
                        renderer.render(
                            &tilePainter,
                            QRectF(QPointF(), request.vectorImage.logicalSize));
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
    if (vectorInteractionActive
        && result.generation == vectorSourceGeneration
        && !result.tile.image.isNull())
    {
        retainVectorTile(result.tile);
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
        if (vectorInteractionActive
            && pending.generation == vectorSourceGeneration)
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

    const bool isSvg = vectorImage.format == Qv::VectorImageFormat::Svg
            && svgRenderer;
    const bool isPdf = vectorImage.format == Qv::VectorImageFormat::Pdf
            && pdfDocument;
    if (!isSvg && !isPdf)
    {
        paintRasterFallback(painter);
        return;
    }

    QRectF exposedRect = option ? option->exposedRect : boundingRect();
    exposedRect = exposedRect.intersected(boundingRect());
    if (exposedRect.isEmpty())
        return;

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
    const auto interactionRequest = [this, sourceRect,
                                     deviceScaleX, deviceScaleY]() {
        const qreal overscanX = VectorTilePanOverscanPixels / deviceScaleX;
        const qreal overscanY = VectorTilePanOverscanPixels / deviceScaleY;
        const QRectF renderedSourceRect = sourceRect.adjusted(
            -overscanX, -overscanY, overscanX, overscanY)
            .intersected(boundingRect());
        const QSize tileSize = boundedTileSize(QSizeF(
            renderedSourceRect.width() * deviceScaleX
                * InteractiveVectorRenderScale,
            renderedSourceRect.height() * deviceScaleY
                * InteractiveVectorRenderScale));
        const qreal renderedScaleX = tileSize.isEmpty() ? 0.0
                : tileSize.width() / renderedSourceRect.width();
        const qreal renderedScaleY = tileSize.isEmpty() ? 0.0
                : tileSize.height() / renderedSourceRect.height();
        return AsyncTileRequest {
            vectorImage, renderedSourceRect, tileSize,
            renderedScaleX, renderedScaleY, vectorSourceGeneration
        };
    };
    int tileIndex = matchingVectorTile(sourceRect, deviceScaleX, deviceScaleY);
    const bool cacheHit = tileIndex >= 0;
    bool scaledInteractionTile = false;
    if (tileIndex < 0 && vectorInteractionActive)
    {
        requestAsyncVectorTile(interactionRequest());
        tileIndex = bestInteractiveVectorTile(
            sourceRect, deviceScaleX, deviceScaleY);
        scaledInteractionTile = tileIndex >= 0;
        if (tileIndex < 0)
        {
            // There is no source coverage to transform yet.  The bounded
            // preview keeps the gesture responsive; the idle refinement timer
            // will replace it from the vector source after 50 ms.
            paintRasterFallback(painter);
            return;
        }
    }
    if (tileIndex < 0)
    {
        const qreal overscanX = VectorTilePanOverscanPixels / deviceScaleX;
        const qreal overscanY = VectorTilePanOverscanPixels / deviceScaleY;
        const QRectF renderedSourceRect = sourceRect.adjusted(
            -overscanX, -overscanY, overscanX, overscanY)
            .intersected(boundingRect());
        const QSize tileSize = boundedTileSize(QSizeF(
            renderedSourceRect.width() * deviceScaleX,
            renderedSourceRect.height() * deviceScaleY));
        if (tileSize.isEmpty())
            return;

        QString errorString;
        QImage renderedTile;
        if (isPdf)
        {
            renderedTile = pdfDocument->renderTile(
                vectorImage.logicalSize, renderedSourceRect,
                tileSize, &errorString);
        }
        else
        {
            renderedTile = QImage(
                tileSize, QImage::Format_ARGB32_Premultiplied);
            if (!renderedTile.isNull())
            {
                renderedTile.fill(Qt::transparent);
                QPainter tilePainter(&renderedTile);
                tilePainter.setRenderHint(QPainter::Antialiasing, true);
                tilePainter.setRenderHint(QPainter::TextAntialiasing, true);
                const qreal renderScaleX = tileSize.width()
                        / renderedSourceRect.width();
                const qreal renderScaleY = tileSize.height()
                        / renderedSourceRect.height();
                tilePainter.setWorldTransform(QTransform(
                    renderScaleX, 0.0, 0.0, renderScaleY,
                    -renderedSourceRect.left() * renderScaleX,
                    -renderedSourceRect.top() * renderScaleY));
                svgRenderer->render(&tilePainter, boundingRect());
                tilePainter.end();
                renderedTile.setColorSpace(QColorSpace::SRgb);
            }
        }
        if (renderedTile.isNull())
        {
            if (qEnvironmentVariableIsSet("FOVELLE_VECTOR_RENDER_LOG"))
                qWarning().noquote() << "FOVELLE_VECTOR_RENDER error="
                                     << errorString;
            paintRasterFallback(painter);
            return;
        }
        retainVectorTile(VectorTile {
            std::move(renderedTile), renderedSourceRect,
            deviceScaleX, deviceScaleY, 0
        });
        tileIndex = matchingVectorTile(sourceRect, deviceScaleX, deviceScaleY);
        if (tileIndex < 0)
            return;
    }

    VectorTile &tile = vectorTiles[tileIndex];
    tile.lastUse = ++vectorTileUseSerial;
    QRectF drawnSourceRect = sourceRect;
    if (scaledInteractionTile
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
    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
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
                          : scaledInteractionTile ? QStringLiteral("scaled")
                                                  : QStringLiteral("miss"))
                         << "sourceRect=" << tile.sourceRect
                         << "tilePixels=" << tile.image.size();
    }
}
