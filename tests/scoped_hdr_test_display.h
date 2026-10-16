#pragma once

#include <QByteArray>
#include <QtGlobal>

// Only the test executable includes this fixture. Hosted macOS displays can
// correctly report SDR headroom even though Metal supports the offscreen
// half-float/geometry oracle. The product's SDR policy remains authoritative.
class ScopedHDRTestDisplay
{
public:
    explicit ScopedHDRTestDisplay(float headroom = 4.0F)
    {
        Q_ASSERT(qIsFinite(headroom) && headroom > 1.0F);
        const QByteArray value = QByteArray::number(headroom);
        qputenv(potential.key, value);
        qputenv(current.key, value);
    }
    ~ScopedHDRTestDisplay()
    {
        current.restore();
        potential.restore();
    }

private:
    Q_DISABLE_COPY(ScopedHDRTestDisplay)
    struct SavedEnvironment
    {
        explicit SavedEnvironment(const char *name)
            : key(name), existed(qEnvironmentVariableIsSet(name)), value(qgetenv(name))
        {
        }
        void restore() const
        {
            if (existed)
                qputenv(key, value);
            else
                qunsetenv(key);
        }
        const char *key;
        bool existed;
        QByteArray value;
    };
    SavedEnvironment potential{ "FOVELLE_TEST_DISPLAY_HEADROOM" };
    SavedEnvironment current{ "FOVELLE_TEST_DISPLAY_CURRENT_HEADROOM" };
};
