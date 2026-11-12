#ifndef QVTHUMBNAILSTRIP_H
#define QVTHUMBNAILSTRIP_H

#include <QImage>
#include <QRectF>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class MainWindow;
class QVGraphicsView;

class QVThumbnailStrip : public QWidget
{
    Q_OBJECT

public:
    struct Cell
    {
        QRectF rect;
        int index = -1;
        QString path;
        QByteArray version;
        QImage preview;
        bool currentProxy = false;
    };

    QVThumbnailStrip(MainWindow *window, QVGraphicsView *view);
    ~QVThumbnailStrip() override;

    static QList<Cell> layout(const QRectF &viewport, int count, int current, bool loop);
    bool available() const;
    bool canOfferFocus() const;
    bool hitTest(const QPoint &pos) const;
    qreal opacity() const { return opacityValue; }
    const QList<Cell> &cells() const { return visibleCells; }
    int previewCount() const;
    QRect panelRect() const { return panel; }
    void focusStrip();
    bool dismissFocus();
    void suppress(bool suppressed);
    void setSlideshowRunning(bool running);
    void setHoverEnabled(bool enabled);
    void refresh();

signals:
    void overlayGeometryChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    QRectF usableViewport() const;
    int slotAt(const QPoint &pos) const;
    void updatePointer(const QPoint &pos, bool canEnter);
    void animateTo(bool visible);
    void hideImmediately();
    void rebuild();
    void requestPreviews();
    void updateRendering(bool contentChanged);
    void syncAccessibleButtons();
    void activate(int slot);
    void cancelActivation();
    bool blocked() const;

    MainWindow *window;
    QVGraphicsView *view;
    QList<Cell> visibleCells;
    QRect panel;
    QImage surface;
    QPoint lastPointer{ -1, -1 };
    QPoint pressedPosition;
    QTimer enterTimer;
    QTimer leaveTimer;
    QTimer gestureTimer;
    QTimer validationTimer;
    QVariantAnimation fade;
    qreal opacityValue = 0;
    qreal targetOpacity = 0;
    quint64 previewGeneration = 0;
    quint64 activationGeneration = 0;
    quint64 listVersion = 0;
    int currentIndex = -1;
    int hoverSlot = -1;
    int pressedSlot = -1;
    int focusSlot = -1;
    bool ownsPress = false;
    bool hoverEnabled = true;
    bool suppressHoverUntilExit = false;
    bool suppressed = false;
    bool dragActive = false;
    bool fileDragActive = false;
    bool slideshowRunning = false;
    bool focusRequested = false;
    bool nativeSurface = false;
    bool accessibilityActive = false;
    QString waitingPath;
    QList<QWidget *> accessibleButtons;
    QWidget *reportedAccessibleFocus = nullptr;
};

#endif
