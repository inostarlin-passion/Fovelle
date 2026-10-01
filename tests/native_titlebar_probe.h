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
