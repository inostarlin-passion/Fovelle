#pragma once
#include <QRectF>
#include <QImage>
class QWindow;
struct NativeTitlebarSnapshot
{
    bool hidden;
    int entries;
    int exits;
};
NativeTitlebarSnapshot nativeTitlebarSnapshot(QWindow *window);

// Observe the public proxy lifecycle: presentation geometry while running,
// committed geometry after removal. Neither Qt state nor a production clock.
struct NativeFullScreenPresentation
{
    bool active {false};
    QRectF windowRect;
    QRectF imageRect;
    bool running {false};
};
NativeFullScreenPresentation nativeFullScreenPresentation(QWindow *window, bool refreshTransaction = true);

// Read actual proxy contents and rasterize its model-layer transform. This is
// an offscreen correctness oracle, not a physical-display frame measurement.
struct NativeFullScreenImageSnapshot
{
    bool active {false};
    QSize sourceSize;
    QSizeF orientedSize;
    QImage sourcePreview;
    QImage orientedPreview;
};
NativeFullScreenImageSnapshot nativeFullScreenImageSnapshot(QWindow *window);

bool nativeFullScreenUsesSystemAnimation(QWindow *window);
void nativeToggleFullScreen(QWindow *window);

#include <functional>
struct NativeSDRLayerGeometry {
    bool active {false};
    QRectF viewport;
    QRectF image;
    QRectF background;
    QImage bottomBand;
};
NativeSDRLayerGeometry nativeSDRLayerGeometry(QWindow *window);
void nativeSetResizeProbe(QWindow *window, std::function<void()> callback);

struct NativeHDRLayerGeometry {
    bool active {false};
    QRectF viewport;
    QRectF image;
    QRectF background;
    QImage bottomBand;
    QImage expectedBottomBand;
};
NativeHDRLayerGeometry nativeHDRLayerGeometry(QWindow *window, const QRectF &expectedImage);
