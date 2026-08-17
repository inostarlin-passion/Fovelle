#include "simplefonticonengine.h"
#include <QPainter>
#include <QPalette>
#include <QDebug>

SimpleFontIconEngine::SimpleFontIconEngine(const QChar iconChar, const QFont &iconFont)
    : m_iconChar(iconChar), m_iconFont(iconFont)
{
    Q_ASSERT_X(iconFont.styleStrategy() & QFont::NoFontMerging, "SimpleFontIconEngine", "Icon fonts must not use font merging");
}

SimpleFontIconEngine::~SimpleFontIconEngine() = default;

QIconEngine *SimpleFontIconEngine::clone() const
{
    return new SimpleFontIconEngine(m_iconChar, m_iconFont);
}

QString SimpleFontIconEngine::key() const
{
    return "SimpleFontIconEngine(" + m_iconFont.key() + ")";
}

QString SimpleFontIconEngine::iconName()
{
    return QString(m_iconChar);
}

QList<QSize> SimpleFontIconEngine::availableSizes(QIcon::Mode, QIcon::State)
{
    return {{16, 16}, {24, 24}, {32, 32}, {48, 48}, {64, 64}, {96, 96}, {128, 128}};
}

QPixmap SimpleFontIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    return scaledPixmap(size, mode, state, 1.0);
}

void SimpleFontIconEngine::virtual_hook(int id, void *data)
{
    if (id == QIconEngine::ScaledPixmapHook)
    {
        QIconEngine::ScaledPixmapArgument &arg = *reinterpret_cast<QIconEngine::ScaledPixmapArgument*>(data);
        arg.pixmap = scaledPixmapInternal(arg.size, arg.mode, arg.state, arg.scale);
        return;
    }

    QIconEngine::virtual_hook(id, data);
}

QPixmap SimpleFontIconEngine::scaledPixmapInternal(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale)
{
    const quint64 cacheKey = calculateCacheKey(mode, state);
    const QSize scaledSize = size * scale;
    if (cacheKey != m_pixmapCacheKey || scaledSize != m_pixmap.size() || scale != m_pixmap.devicePixelRatio())
    {
        m_pixmap = QPixmap(scaledSize);
        m_pixmap.fill(Qt::transparent);
        m_pixmap.setDevicePixelRatio(scale);

        if (!m_pixmap.isNull())
        {
            QPainter painter(&m_pixmap);
            paint(&painter, QRect(QPoint(), size), mode, state);
        }

        m_pixmapCacheKey = cacheKey;
    }
    return m_pixmap;
}

void SimpleFontIconEngine::paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State)
{
    const QPalette palette;
    const QColor color =
        mode == QIcon::Disabled ? palette.color(QPalette::Disabled, QPalette::Text) :
        palette.color(QPalette::Active, QPalette::Text);

    QFont renderFont(m_iconFont);
    renderFont.setPixelSize(qMin(rect.width(), rect.height()));

    painter->save();
    painter->setFont(renderFont);
    painter->setPen(color);
    painter->drawText(rect, Qt::AlignCenter, QString(m_iconChar));
    painter->restore();
}
