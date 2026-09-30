#pragma once
#include <QRectF>
class QWindow;
struct NativeTitlebarSnapshot
{
    bool hidden;
    int entries;
    int exits;
};
NativeTitlebarSnapshot nativeTitlebarSnapshot(QWindow *window);

// Read the presentation tree, not Qt state or the production animation clock.
struct NativeFullScreenPresentation
{
    bool active {false};
    QRectF windowRect;
    QRectF imageRect;
};
NativeFullScreenPresentation nativeFullScreenPresentation(QWindow *window);
