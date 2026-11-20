#ifndef QVTHUMBNAILSTRIP_H
#define QVTHUMBNAILSTRIP_H

#include "asyncwork.h"

#include <QColor>
#include <QImage>
#include <QSet>
#include <memory>
#include <QRectF>
#include <QTimer>
#include <QVariantAnimation>
#include <QWidget>

class MainWindow;
class QVGraphicsView;
class QPainter;
class ThumbnailWatch;
class QWheelEvent;
class QNativeGestureEvent;

class QVThumbnailStrip : public QWidget
{
    Q_OBJECT

public:
    enum class PreviewStatus { Loading, Ready, Limited, Unavailable, Failed };

    struct Cell
    {
        QRectF rect;
        int index = -1;
        QString path;
        QByteArray version;
        QImage preview;
        PreviewStatus status = PreviewStatus::Loading;
        QSize requestedPreviewSize;
        QImage tile;
        qint64 tileSource = 0;
        int tileStyle = -1;
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
    qreal browseIndex() const { return browsePosition; }
    qreal transitionProgress() const { return selectionProgress; }
    QRect panelRect() const { return panel; }
    void focusStrip();
    bool dismissFocus();
    void suppress(bool suppressed);
    void setSlideshowRunning(bool running);
    void setHoverEnabled(bool enabled);
    void refresh();

#ifdef FOVELLE_HDR_SURFACE_TESTING
    quint64 backgroundRasterCount() const { return backgroundRasters; }
    quint64 tileRasterCount() const { return tileRasters; }
    void setValidationReader(std::function<QByteArray(const QString &)> reader)
    {
        validationReader = std::move(reader);
    }
#endif

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
    bool rebuild();
    QRectF cellClip() const;
    void refreshVisible(bool imageCommitted);
    void finishTransition();
    QImage transitionSnapshot() const;
    bool canBrowseAt(const QPointF &position) const;
    bool handleWheel(QWheelEvent *event);
    bool handleNativeBrowse(QNativeGestureEvent *event);
    void scrollBy(qreal slotDelta);
    void requestPreviews();
    void updateRendering(bool contentChanged, const QRect &changedCell = { });
    void drawContent(QPainter &painter) const;
    QString previewStatusText(PreviewStatus status) const;
    void syncWatch();
    void filesChanged(bool rescan, const QSet<QString> &paths, bool all = false);
    void submitValidation();
    void syncAccessibleButtons();
    void moveFocus(int step);
    void activate(int slot);
    void cancelActivation();
    bool blocked() const;

    MainWindow *window;
    QVGraphicsView *view;
    QList<Cell> visibleCells;
    QRect panel;
    QImage background;
    QColor panelForeground;
    QColor panelTint;
    QColor panelFocus;
    QImage outgoingSurface;
    QImage animatedContent;
    QPoint lastPointer{ -1, -1 };
    QPoint pressedPosition;
    QTimer enterTimer;
    QTimer leaveTimer;
    QTimer gestureTimer;
    QTimer validationTimer;
    QTimer validationAdmission;
    QTimer directoryChangeTimer;
    std::unique_ptr<ThumbnailWatch> watch;
    QvAsync::Ticket validationTicket;
    std::function<void(const QByteArray &)> validationDone;
    QTimer wheelSequenceTimer;
    QVariantAnimation fade;
    QVariantAnimation selectionFade;
    qreal browsePosition = -1;
    qreal selectionProgress = 1;
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
    enum class ScrollOwner { None, Strip, Viewport };
    ScrollOwner wheelOwner = ScrollOwner::None;
    ScrollOwner nativeGestureOwner = ScrollOwner::None;
    bool nativeSurface = false;
    bool outgoingDirty = false;
    bool accessibilityActive = false;
    bool currentSourceChanged = false;
    bool watchNeedsCatchup = false;
#ifdef FOVELLE_HDR_SURFACE_TESTING
    quint64 backgroundRasters = 0;
    quint64 tileRasters = 0;
    std::function<QByteArray(const QString &)> validationReader;
#endif
    QString waitingPath;
    QList<QWidget *> accessibleButtons;
    QWidget *reportedAccessibleFocus = nullptr;
};

#endif
