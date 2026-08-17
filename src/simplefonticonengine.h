#ifndef SIMPLEFONTICONENGINE_H
#define SIMPLEFONTICONENGINE_H

#include <QFont>
#include <QIconEngine>

class SimpleFontIconEngine : public QIconEngine
{
public:
    SimpleFontIconEngine(const QChar iconChar, const QFont &iconFont);
    ~SimpleFontIconEngine() override;

    QString iconName() override;
    QString key() const override;
    QIconEngine *clone() const override;

    QList<QSize> availableSizes(QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override;
    void virtual_hook(int id, void *data) override;
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override;

private:
    QPixmap scaledPixmapInternal(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale);

    static constexpr quint64 calculateCacheKey(QIcon::Mode mode, QIcon::State state)
    {
        return (quint64(mode) << 32) | state;
    }

    const QChar m_iconChar;
    const QFont m_iconFont;
    mutable QPixmap m_pixmap;
    mutable quint64 m_pixmapCacheKey = {};
};

#endif // SIMPLEFONTICONENGINE_H
