#include "scrollhelper.h"

ScrollHelper::ScrollHelper(QAbstractScrollArea *parent, GetParametersCallback getParametersCallback) : QObject(parent)
{
    hScrollBar = parent->horizontalScrollBar();
    vScrollBar = parent->verticalScrollBar();
    this->getParametersCallback = getParametersCallback;
}

void ScrollHelper::move(QPointF delta)
{
    Parameters p;
    getParametersCallback(p);
    if (!p.contentRect.isValid() || !p.usableViewportRect.isValid())
        return;

    bool isRightToLeft = hScrollBar->isRightToLeft();
    int hMin, hMax, vMin, vMax;
    calculateScrollRange(
        p.contentRect.width(),
        p.usableViewportRect.width(),
        isRightToLeft ?
            hScrollBar->minimum() + hScrollBar->maximum() + p.usableViewportRect.width() - p.contentRect.left() - p.contentRect.width() :
            p.contentRect.left(),
        p.shouldCenter,
        hMin,
        hMax
    );
    calculateScrollRange(
        p.contentRect.height(),
        p.usableViewportRect.height(),
        p.contentRect.top() - p.usableViewportRect.top(),
        p.shouldCenter,
        vMin,
        vMax
    );
    QPointF scrollLocation = QPointF(hScrollBar->value(), vScrollBar->value()) + lastMoveRoundingError;
    qreal scrollDeltaX = delta.x();
    qreal scrollDeltaY = delta.y();
    if (p.shouldConstrain)
    {
        scrollDeltaX = calculateScrollDelta(scrollLocation.x(), hMin, hMax, scrollDeltaX);
        scrollDeltaY = calculateScrollDelta(scrollLocation.y(), vMin, vMax, scrollDeltaY);
    }
    scrollLocation += QPointF(scrollDeltaX, scrollDeltaY);
    int scrollValueX = qAbs(scrollLocation.x()) == 0.5 ? 0 : qRound(scrollLocation.x());
    int scrollValueY = qAbs(scrollLocation.y()) == 0.5 ? 0 : qRound(scrollLocation.y());
    lastMoveRoundingError = QPointF(scrollLocation.x() - scrollValueX, scrollLocation.y() - scrollValueY);
    hScrollBar->setValue(scrollValueX);
    vScrollBar->setValue(scrollValueY);
}

void ScrollHelper::constrain()
{
    // Re-evaluate the range after a scene or viewport change. move() now
    // clamps directly, so constraining never needs a rebound animation.
    move(QPointF());
}

void ScrollHelper::calculateScrollRange(int contentDimension, int viewportDimension, int offset, bool shouldCenter, int &minValue, int &maxValue)
{
    int overflow = contentDimension - viewportDimension;
    if (overflow >= 0)
    {
        minValue = offset;
        maxValue = overflow + offset;
    }
    else if (shouldCenter)
    {
        minValue = overflow / 2 + offset;
        maxValue = minValue;
    }
    else
    {
        minValue = overflow + offset;
        maxValue = offset;
    }
}

qreal ScrollHelper::calculateScrollDelta(qreal currentValue, int minValue, int maxValue, qreal proposedDelta)
{
    // A constrained image must stop at its edge. The previous implementation
    // deliberately returned a fraction of an out-of-range delta and animated
    // it back later, which produced the rubber-band effect during a drag.
    return qBound(static_cast<qreal>(minValue) - currentValue,
                  proposedDelta,
                  static_cast<qreal>(maxValue) - currentValue);
}
