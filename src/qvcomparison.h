#ifndef QVCOMPARISON_H
#define QVCOMPARISON_H

#include "asyncwork.h"
#include "qvnamespace.h"

#include <QJsonObject>
#include <QPointF>
#include <QWidget>
#include <optional>

class QCheckBox;
class QLabel;
class QPushButton;
class QSplitter;
class QTimer;
class QVGraphicsView;

class QVComparison : public QWidget
{
    Q_OBJECT

public:
    explicit QVComparison(QVGraphicsView *reference, QWidget *parent);
    ~QVComparison() override;

    bool isActive() const { return candidate != nullptr; }
    bool canStart() const;
    bool canSwap() const;
    bool syncZoom() const;
    bool syncPan() const;
    void setSyncZoom(bool value);
    void setSyncPan(bool value);
    void enter();
    void leave();
    void cancelPending();
    void swap();
    void selectFile(const QString &path);
    void removeCandidate();

    QVGraphicsView *activeView() const { return active; }
    QVGraphicsView *candidateView() const { return candidate; }
    const QJsonObject &originalSession() const { return referenceSession; }

    void activate(QVGraphicsView *view);
    void pan(QVGraphicsView *source, QPointF delta);
    void relativeZoom(QVGraphicsView *source, qreal factor, std::optional<QPoint> anchor);
    void absoluteZoom(QVGraphicsView *source, qreal level);
    void calculatedZoom(QVGraphicsView *source, Qv::CalculatedZoomMode mode);
    QVGraphicsView *wheelTarget(QVGraphicsView *receiver, Qt::ScrollPhase phase);
    QVGraphicsView *gestureTarget(QVGraphicsView *receiver, Qt::NativeGestureType type);
    void cancelInput();
    void cancelDividerDrag();
    void scheduleLayout();

signals:
    void stateChanged();
    void exitRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QVGraphicsView *linkedView(QVGraphicsView *source, bool enabled) const;
    void chooseFile();
    void startPending();
    void refreshHeaders();
    void setStatus(const QString &text, bool announce = false);
    void restoreLayout();
    void resetEqualWidths();
    void rememberCenter(QVGraphicsView *view);

    struct InputSequence
    {
        QVGraphicsView *owner = nullptr;
        bool blocked = false;
        QVGraphicsView *target(QVGraphicsView *receiver, bool begin);
    };

    QVGraphicsView *const reference;
    QVGraphicsView *candidate = nullptr;
    QVGraphicsView *active;
    QWidget *referencePane;
    QWidget *candidatePane = nullptr;
    QWidget *toolbar;
    QLabel *referenceTitle;
    QLabel *candidateTitle = nullptr;
    QLabel *status = nullptr;
    QPushButton *choose = nullptr;
    QPushButton *remove = nullptr;
    QPushButton *swapButton;
    QCheckBox *zoomLink;
    QCheckBox *panLink;
    QSplitter *splitter;
    QTimer *layoutTimer;
    QJsonObject referenceSession;
    QPointF referenceInitialCenter;
    QPointF referenceCenter;
    QPointF candidateCenter;
    InputSequence wheel;
    InputSequence gesture;
    QString selectedPath;
    quint64 selection = 0;
    QvAsync::Ticket job;
};

#endif
