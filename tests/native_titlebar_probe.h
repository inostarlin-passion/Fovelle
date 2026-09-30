#pragma once
class QWindow;
struct NativeTitlebarSnapshot
{
    bool hidden;
    int entries;
    int exits;
};
NativeTitlebarSnapshot nativeTitlebarSnapshot(QWindow *window);
