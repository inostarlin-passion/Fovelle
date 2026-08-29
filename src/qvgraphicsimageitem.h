#ifndef QVGRAPHICSIMAGEITEM_H
#define QVGRAPHICSIMAGEITEM_H

#include "qvnamespace.h"
#include "qvcocoafunctions.h"

#include <QGraphicsObject>
#include <QImage>
#include <QPixmap>
#include <QThreadPool>
#include <QVector>

#include <memory>
#include <optional>

class QSvgRenderer;
template <typename T> class QFutureWatcher;

// One scene item owns the application's raster and vector display contracts.
// Raster images behave like the former QGraphicsPixmapItem.  SVG and the PDF
// normalized from EPS keep their encoded source and are painted at the current
// view/device transform.  A bounded terminal-density tile can be reused while
// a worker catches up, but idle output is always regenerated at exact device
// scale rather than using a fixed source bitmap.
class QVGraphicsImageItem final : public QGraphicsObject
{
public:
    explicit QVGraphicsImageItem(QGraphicsItem *parent = nullptr);
    ~QVGraphicsImageItem() override;

    void setPixmap(const QPixmap &pixmap);
    const QPixmap &pixmap() const { return rasterPixmap; }

    void setTransformationMode(Qt::TransformationMode mode);
    Qt::TransformationMode transformationMode() const { return rasterTransformationMode; }

    bool setVectorImage(const Qv::VectorImageData &image);
    bool hasVectorImage() const { return vectorImage.isValid(); }
    Qv::VectorImageFormat vectorImageFormat() const { return vectorImage.format; }
    void setVectorInteractionActive(bool active);
    void shutdownAsyncWork();
    bool isVectorInteractionActive() const { return vectorInteractionActive; }
    // Includes work already running on the worker and the newest work queued
    // behind it, so callers never mistake an idle interaction timer for a
    // completed vector refinement.
    bool hasPendingVectorRefinement() const;

    // Non-invasive diagnostics used by deterministic tests.  The reported
    // image is a bounded device-space tile around the exposed region, never a
    // fixed whole-document raster.  Once interaction is idle, it is rendered
    // at the exact final device scale.
    QSize lastVectorRasterSize() const { return lastVectorTilePixelSize; }
    QRectF lastVectorSourceRect() const { return lastVectorTileSourceRect; }
    quint64 vectorRenderCount() const { return completedVectorRenderCount; }
    quint64 vectorTileGenerationCount() const
        { return completedVectorTileGenerationCount; }

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget = nullptr) override;

private:
    struct VectorTile
    {
        QImage image;
        QRectF sourceRect;
        qreal deviceScaleX {0.0};
        qreal deviceScaleY {0.0};
        quint64 lastUse {0};
    };

    struct AsyncTileRequest
    {
        Qv::VectorImageData vectorImage;
        QRectF sourceRect;
        QSize pixelSize;
        qreal deviceScaleX {0.0};
        qreal deviceScaleY {0.0};
        int svgFrame {0};
        quint64 generation {0};
    };

    struct AsyncTileResult
    {
        VectorTile tile;
        QString errorString;
        quint64 generation {0};
    };

    void paintRasterFallback(QPainter *painter) const;
    void clearVectorTiles();
    int matchingVectorTile(const QRectF &sourceRect, qreal deviceScaleX,
                           qreal deviceScaleY) const;
    int bestReusableVectorTile(const QRectF &sourceRect,
                               qreal deviceScaleX,
                               qreal deviceScaleY) const;
    void retainVectorTile(VectorTile tile) const;
    void requestAsyncVectorTile(const AsyncTileRequest &request);
    void startAsyncVectorTile(AsyncTileRequest request);
    void asyncVectorTileFinished();
    static bool sameAsyncRequest(const AsyncTileRequest &lhs,
                                 const AsyncTileRequest &rhs);
    static QSize boundedTileSize(const QSizeF &requestedSize);

    QPixmap rasterPixmap;
    Qt::TransformationMode rasterTransformationMode {Qt::FastTransformation};
    Qv::VectorImageData vectorImage;
    std::unique_ptr<QSvgRenderer> svgRenderer;
    QVCocoaFunctions::PDFVectorDocumentPtr pdfDocument;
    bool vectorInteractionActive {false};
    quint64 vectorSourceGeneration {0};
    std::unique_ptr<QFutureWatcher<AsyncTileResult>> asyncTileWatcher;
    QThreadPool vectorThreadPool;
    std::optional<AsyncTileRequest> activeAsyncRequest;
    std::optional<AsyncTileRequest> pendingAsyncRequest;
    bool asyncWorkShutDown {false};

    mutable QVector<VectorTile> vectorTiles;
    mutable quint64 vectorTileUseSerial {0};
    mutable QSize lastVectorTilePixelSize;
    mutable QRectF lastVectorTileSourceRect;
    mutable quint64 completedVectorRenderCount {0};
    quint64 completedVectorTileGenerationCount {0};
};

#endif // QVGRAPHICSIMAGEITEM_H
