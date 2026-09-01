#ifndef SCROLLHELPER_H
#define SCROLLHELPER_H

#include <QAbstractScrollArea>
#include <QScrollBar>

class ScrollHelper : public QObject
{
    Q_OBJECT
public:
    struct Parameters
    {
        QRect contentRect;
        QRect usableViewportRect;
        bool shouldConstrain;
        bool shouldCenter;
    };

    using GetParametersCallback = std::function<void(Parameters &)>;

    explicit ScrollHelper(QAbstractScrollArea *parent, GetParametersCallback getParametersCallback);

    void move(QPointF delta);

    void constrain();

private:
    static void calculateScrollRange(int contentDimension, int viewportDimension, int offset, bool shouldCenter, int &minValue, int &maxValue);

    static qreal calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta);

    QScrollBar *hScrollBar;
    QScrollBar *vScrollBar;
    GetParametersCallback getParametersCallback;
    QPointF lastMoveRoundingError;
};

#endif // SCROLLHELPER_H
