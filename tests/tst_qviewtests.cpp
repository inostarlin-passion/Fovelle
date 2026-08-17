#include <QtTest>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QImage>
#include <QFile>
#include <QElapsedTimer>
#include <QLabel>
#include <QSignalSpy>
#include <QSettings>
#include <QTextDocumentFragment>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QUrl>

#include "mainwindow.h"
#include "qvapplication.h"
#include "qvcocoafunctions.h"
#include "qvgraphicsview.h"
#include "qvimagecore.h"
#include "qvimageloader.h"
#include "qvmovie.h"

class ImageLoaderTests : public QObject
{
    Q_OBJECT

private slots:
    void testImageLoaderPriorities();
    void testImageLoaderCacheAndAttachment();
    void testImageLoaderRetainedDuringDelivery();
    void testImageLoaderForegroundRequestPreservesCache();
    void testImageLoaderSupersededForegroundDiscarded();
    void testImageLoaderDisabledRetention();
    void testImageLoaderCachedErrorRetry();
    void testImageLoaderDestructionDuringLoad();
    void testImageLoaderLoadsWebpWithImageIOFallback();
    void testImageLoaderLoadsAvifWithImageIOFallback();
};

class ActionManagerTests : public QObject
{
    Q_OBJECT

private slots:
    void testClonedActionsUntracked();
    void testApplicationIdentity();
    void testAboutDialogIdentity();
    void testWindowTitleIdentity();
    void testLastWindowClosedPolicy();
    void testReturnKeyEntersFullscreen();
    void testKeypadEnterEntersFullscreen();
    void testEnterDoesNotExitFullscreen();
    void testEscapeExitsFullscreen();
    void testEscapeRestoresLoadedImageWithoutGeometryJump();
    void testEscapeClosesWindow();
};

class FeatureTests : public QObject
{
    Q_OBJECT

private slots:
    void testWindowIconIsCleared();
    void testSettingsFormatsIncludeNativeImageFormats();
};

class GraphicsViewTests : public QObject
{
    Q_OBJECT

private slots:
    void testMouseWheelUsesOneDiscreteStep();
    void testTouchpadWheelCanUseFractionalSteps();
};

class ApplicationEventTests : public QObject
{
    Q_OBJECT

private slots:
    void testFileOpenEventIsDeferredAndLoadsImage();
    void testFileOpenEventWithoutPathIsIgnored();
};

class ImageCoreAndMovieTests : public QObject
{
    Q_OBJECT

private slots:
    void testColorSpaceConversion();
    void testMovieSpeedAndSingleFrameRead();
};

class TestableImageCore : public QVImageCore
{
public:
    using QVImageCore::handleColorSpaceConversion;
};

static QString createTestImage(const QTemporaryDir &dir, const QString &name, const QColor color)
{
    const QString path = dir.filePath(name + ".png");
    QImage image(32, 32, QImage::Format_RGB32);
    image.fill(color);
    if (!image.save(path))
        return {};
    return path;
}

static QString createBase64Image(const QTemporaryDir &dir, const QString &name, const QString &extension, const QByteArray &base64)
{
    const QString path = dir.filePath(name + "." + extension);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromBase64(base64)) <= 0)
        return {};
    return path;
}

static std::optional<QVImageLoader::Result> loadImage(const QString &path)
{
    QVImageLoader loader;
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const quint64 requestId = loader.requestImage(path);
    loader.setDesiredImages({{path, 0}});
    if (!readySpy.wait(5000) && readySpy.isEmpty())
        return {};
    if (readySpy.isEmpty() || readySpy.at(0).at(0).toULongLong() != requestId)
        return {};
    return qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
}

static const QByteArray tinyWebpBase64 =
    "UklGRlIAAABXRUJQVlA4WAoAAAAQAAAAAAAAAAAAQUxQSAIAAAAArlZQOCAqAAAAkAEAnQEqAQABAAIANCWgAnS6AAOYAP7wumv/BBbUemHHh/c1FbFtAAAA";

static const QByteArray tinyAvifBase64 =
    "AAAAIGZ0eXBhdmlmAAAAAGF2aWZtaWYxbWlhZk1BMUEAAAG7bWV0YQAAAAAAAAAhaGRscgAAAAAAAAAAcGljdAAAAAAAAAAAAAAAAAAAAAAOcGl0bQAAAAAAAQAAADppbG9jAAAAAEQAAAMAAQAAAAEAAAI9AAAAHwACAAAAAQAAAisAAAASAAMAAAABAAAB4wAAAEgAAABbaWluZgAAAAAAAwAAABppbmZlAgAAAAABAABhdjAxQ29sb3IAAAAAGmluZmUCAAAAAAIAAGF2MDFBbHBoYQAAAAAZaW5mZQIAAAAAAwAARXhpZkV4aWYAAAAAKGlyZWYAAAAAAAAADmF1eGwAAgABAAEAAAAOY2RzYwADAAEAAQAAAMNpcHJwAAAAnWlwY28AAAAUaXNwZQAAAAAAAAABAAAAAQAAABBwaXhpAAAAAAMICAgAAAAMYXYxQ4EgAAAAAAATY29scm5jbHgAAQANAAaAAAAADnBpeGkAAAAAAQgAAAAMYXYxQ4EAHAAAAAA4YXV4QwAAAAB1cm46bXBlZzptcGVnQjpjaWNwOnN5c3RlbXM6YXV4aWxpYXJ5OmFscGhhAAAAAB5pcG1hAAAAAAAAAAIAAQQBAoMEAAIEAQWGBwAAAIFtZGF0AAAAAE1NACoAAAAIAAGHaQAEAAAAAQAAABoAAAAAAAOgAQADAAAAAQABAACgAgAEAAAAAQAAAAGgAwAEAAAAAQAAAAEAAAAAEgAKBBgABhUyCBAATiImmSrQEgAKBzgABhAQ0GkyEhAAAE4dz4eZAFvClYOQUfU8Kg==";

static void reportFullscreenMetric(const QString &phase, qint64 elapsedMs)
{
    qInfo().noquote() << QStringLiteral("FS_METRIC %1_ms=%2").arg(phase).arg(elapsedMs);
}

void ImageLoaderTests::testImageLoaderPriorities()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString adjacentBefore = createTestImage(dir, "adjacent-before", Qt::green);
    const QString adjacentAfter = createTestImage(dir, "adjacent-after", Qt::blue);
    const QString extendedBefore = createTestImage(dir, "extended-before", Qt::cyan);
    const QString extendedAfter = createTestImage(dir, "extended-after", Qt::magenta);
    QVERIFY(!target.isEmpty());
    QVERIFY(!adjacentBefore.isEmpty());
    QVERIFY(!adjacentAfter.isEmpty());
    QVERIFY(!extendedBefore.isEmpty());
    QVERIFY(!extendedAfter.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    QVERIFY(startedSpy.isValid());
    QVERIFY(readySpy.isValid());

    const quint64 requestId = loader.requestImage(target);
    loader.setDesiredImages({
        {target, 0},
        {adjacentBefore, 1},
        {adjacentAfter, 1},
        {extendedBefore, 2},
        {extendedAfter, 2}
    });

    QCOMPARE(startedSpy.size(), 1);
    QCOMPARE(startedSpy.at(0).at(0).toString(), target);
    QCOMPARE(startedSpy.at(0).at(1).toInt(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), requestId);
    const auto result = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QCOMPARE(result.absoluteFilePath, target);
    QVERIFY(!result.image.isNull());

    QTRY_COMPARE_WITH_TIMEOUT(startedSpy.size(), 5, 5000);
    const QList<int> expectedPriorities {0, 1, 1, 2, 2};
    for (int i = 0; i < expectedPriorities.size(); ++i)
        QCOMPARE(startedSpy.at(i).at(1).toInt(), expectedPriorities.at(i));
}

void ImageLoaderTests::testImageLoaderCacheAndAttachment()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    const quint64 attachedRequestId = loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), attachedRequestId);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.setDesiredImages({});
    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
}

void ImageLoaderTests::testImageLoaderRetainedDuringDelivery()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    connect(&loader, &QVImageLoader::imageReady, this,
        [&loader, path](quint64, const QVImageLoader::Result &) {
            loader.setDesiredImages({{path, 0}});
        });

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(path);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 1);
}

void ImageLoaderTests::testImageLoaderForegroundRequestPreservesCache()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString cachedPath = createTestImage(dir, "cached", Qt::green);
    const QString foregroundPath = createTestImage(dir, "foreground", Qt::blue);
    QVERIFY(!cachedPath.isEmpty());
    QVERIFY(!foregroundPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(cachedPath);
    loader.setDesiredImages({{cachedPath, 0}});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);

    loader.requestImage(foregroundPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    QCOMPARE(startedSpy.size(), 2);

    loader.requestImage(cachedPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 3, 5000);
    QCOMPARE(startedSpy.size(), 2);
}

void ImageLoaderTests::testImageLoaderSupersededForegroundDiscarded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString firstPath = createTestImage(dir, "first", Qt::red);
    const QString secondPath = createTestImage(dir, "second", Qt::blue);
    QVERIFY(!firstPath.isEmpty());
    QVERIFY(!secondPath.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(firstPath);
    const quint64 secondRequestId = loader.requestImage(secondPath);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(readySpy.at(0).at(0).toULongLong(), secondRequestId);

    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();
    loader.requestImage(firstPath);
    QCOMPARE(startedSpy.size(), 3);
}

void ImageLoaderTests::testImageLoaderDisabledRetention()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "image", Qt::black);
    QVERIFY(!path.isEmpty());

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    QCOMPARE(startedSpy.size(), 1);

    loader.requestImage(path);
    loader.setDesiredImages({});
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
}

void ImageLoaderTests::testImageLoaderCachedErrorRetry()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = dir.filePath("invalid.png");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not an image"), 12);
    file.close();

    QVImageLoader loader;
    QSignalSpy startedSpy(&loader, &QVImageLoader::loadStarted);
    QSignalSpy readySpy(&loader, &QVImageLoader::imageReady);
    const QList<QVImageLoader::DesiredImage> desiredImages {{path, 0}};

    loader.requestImage(path);
    loader.setDesiredImages(desiredImages);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 1, 5000);
    const auto firstResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(0).at(1));
    QVERIFY(firstResult.errorData.has_value());

    loader.requestImage(path);
    QCOMPARE(startedSpy.size(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 5000);
    const auto secondResult = qvariant_cast<QVImageLoader::Result>(readySpy.at(1).at(1));
    QVERIFY(secondResult.errorData.has_value());
}

void ImageLoaderTests::testImageLoaderDestructionDuringLoad()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString target = createTestImage(dir, "target", Qt::red);
    const QString queuedPreload = createTestImage(dir, "queued-preload", Qt::blue);
    QVERIFY(!target.isEmpty());
    QVERIFY(!queuedPreload.isEmpty());

    QStringList startedPaths;
    auto *loader = new QVImageLoader;
    connect(loader, &QVImageLoader::loadStarted, this,
        [&startedPaths](const QString &path, int) { startedPaths.append(path); });

    loader->requestImage(target);
    loader->setDesiredImages({
        {target, 0},
        {queuedPreload, 1}
    });
    QCOMPARE(startedPaths, QStringList {target});

    delete loader;
    QThreadPool::globalInstance()->waitForDone();
    QCoreApplication::processEvents();

    QCOMPARE(startedPaths, QStringList {target});
}

void ImageLoaderTests::testImageLoaderLoadsWebpWithImageIOFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("webp"));
    const QString path = createBase64Image(dir, "native-webp", "webp", tinyWebpBase64);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QCOMPARE(result->image.size(), QSize(1, 1));
    QVERIFY(!result->errorData.has_value());
}

void ImageLoaderTests::testImageLoaderLoadsAvifWithImageIOFallback()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("avif"));
    const QString path = createBase64Image(dir, "native-avif", "avif", tinyAvifBase64);
    QVERIFY(!path.isEmpty());

    const auto result = loadImage(path);
    QVERIFY(result.has_value());
    QVERIFY(!result->image.isNull());
    QCOMPARE(result->image.size(), QSize(1, 1));
    QVERIFY(!result->errorData.has_value());
}

void ActionManagerTests::testClonedActionsUntracked()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    // Get initial counts of certain actions
    int fullscreenCount = qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length();
    int openCount = qvApp->getActionManager().getAllInstancesOfAction("open").length();
    qDebug() << fullscreenCount;

    // Have window clone actions
    MainWindow window;
    window.show();
    // Make sure they were cloned
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length() != fullscreenCount);
    QVERIFY(qvApp->getActionManager().getAllInstancesOfAction("open").length() != openCount);
    // Untrack them
    window.close();

    // Make sure the count has not changed from the initial
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("fullscreen").length(), fullscreenCount);
    QCOMPARE(qvApp->getActionManager().getAllInstancesOfAction("open").length(), openCount);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testApplicationIdentity()
{
    QCOMPARE(QCoreApplication::organizationName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::organizationDomain(), QString("io.github.inostarlin-passion"));
    QCOMPARE(QCoreApplication::applicationName(), QString("Fovelle"));
    QCOMPARE(QGuiApplication::applicationDisplayName(), QString("Fovelle"));
    QCOMPARE(QCoreApplication::applicationVersion(), QString("0.1.0"));
}

void FeatureTests::testWindowIconIsCleared()
{
    QVERIFY(qvApp->windowIcon().isNull());
    MainWindow window;
    QVERIFY(window.windowIcon().isNull());
    window.close();
    QVERIFY(QFile::exists(":/icons/Fovelle.png"));
}

void FeatureTests::testSettingsFormatsIncludeNativeImageFormats()
{
    const auto additionalFormats = QVCocoaFunctions::getAdditionalImageFormats();
    QVERIFY(additionalFormats.contains("webp"));
    QVERIFY(additionalFormats.contains("avif"));
    QVERIFY(QVCocoaFunctions::supportsAdditionalImageFormat("avifs"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".webp"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".avif"));
    QVERIFY(qvApp->getAllFileExtensionList().contains(".avifs"));
}

void GraphicsViewTests::testMouseWheelUsesOneDiscreteStep()
{
    const qreal factor = QVGraphicsView::wheelZoomFactor(240, 1.25, false);
    QVERIFY(qFuzzyCompare(factor, 1.25));
    QVERIFY(qFuzzyCompare(QVGraphicsView::wheelZoomFactor(-240, 1.25, false), 0.8));
}

void GraphicsViewTests::testTouchpadWheelCanUseFractionalSteps()
{
    const qreal factor = QVGraphicsView::wheelZoomFactor(240, 1.25, true);
    QVERIFY(qFuzzyCompare(factor, 1.5625));
}

void ActionManagerTests::testAboutDialogIdentity()
{
    QVAboutDialog dialog;
    const auto *logoLabel = dialog.findChild<QLabel *>("logoLabel");
    const auto *subtitleLabel = dialog.findChild<QLabel *>("subtitleLabel");
    const auto *infoLabel = dialog.findChild<QLabel *>("infoLabel2");

    QVERIFY(logoLabel);
    QVERIFY(subtitleLabel);
    QVERIFY(infoLabel);
    QCOMPARE(dialog.windowTitle(), QString("About Fovelle"));
    QCOMPARE(logoLabel->text(), QString("Fovelle"));
    QCOMPARE(subtitleLabel->text(), QString("version 0.1.0"));

    const QString visibleText = QTextDocumentFragment::fromHtml(infoLabel->text()).toPlainText();
    const QString expectedText =
        "Based on qView\n"
        "Copyright © 2018–2025 jurplel and qView contributors\n"
        "Fovelle modifications © 2026 Fovelle contributors\n"
        "Includes portions of commits from jdpurcell/qView by jdpurcell\n\n"
        "Licensed under GPLv3";
    QCOMPARE(visibleText, expectedText);
    QVERIFY(infoLabel->text().contains("https://github.com/inostarlin-passion/Fovelle"));
    QVERIFY(infoLabel->text().contains("https://github.com/jdpurcell/qView"));
    QVERIFY(!infoLabel->text().contains("interversehq.com"));
}

void ActionManagerTests::testWindowTitleIdentity()
{
    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.buildWindowTitle();
    QCOMPARE(window.windowTitle(), QString("Fovelle"));
    window.close();
}

void ActionManagerTests::testLastWindowClosedPolicy()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    window.close();
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
    QVERIFY(qvApp->quitOnLastWindowClosed());
}

// TC-FS-01
// Test purpose: verify that the Return/Enter key enters full screen from a loaded image.
// Preconditions: an active, visible non-full-screen MainWindow with a loaded PNG.
// Input data: Qt::Key_Return.
// Steps: send Return to the window and wait for the native state transition.
// Expected result: the window becomes full screen within the bounded timeout.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testReturnKeyEntersFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "return-fullscreen", Qt::darkBlue);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(!window.isFullScreen());

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    transitionTimer.restart();
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-02
// Test purpose: verify that the numeric keypad Enter key has the same entry behavior.
// Preconditions: an active, visible non-full-screen MainWindow with a loaded PNG.
// Input data: Qt::Key_Enter.
// Steps: send keypad Enter to the window and wait for the native state transition.
// Expected result: the window becomes full screen within the bounded timeout.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testKeypadEnterEntersFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "keypad-fullscreen", Qt::darkCyan);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    QVERIFY(!window.isFullScreen());

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Enter);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    transitionTimer.restart();
    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-03
// Test purpose: verify that Enter is an entry-only shortcut and is idempotent in full screen.
// Preconditions: an active, visible MainWindow already in full screen.
// Input data: Qt::Key_Return followed by Qt::Key_Enter.
// Steps: send both Enter variants while full screen and process the event loop.
// Expected result: the window remains full screen and is not toggled out.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testEnterDoesNotExitFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.showFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::qWait(2000);

    QTest::keyClick(&window, Qt::Key_Return);
    QTest::keyClick(&window, Qt::Key_Enter);
    QTest::qWait(100);
    QVERIFY(window.isFullScreen());

    QTest::keyClick(&window, Qt::Key_Escape);
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testEscapeExitsFullscreen()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.showFullScreen();
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    QTest::qWait(2000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    QVERIFY(window.isVisible());

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

// TC-FS-04
// Test purpose: verify the Esc regression path restores the loaded image and normal geometry.
// Preconditions: a visible non-full-screen MainWindow displaying a PNG.
// Input data: Return to enter full screen, then Escape to leave it.
// Steps: capture the normal geometry, enter full screen, send Escape, and wait for completion.
// Expected result: the image remains loaded, the window is visible and normal, and its
// geometry returns to the captured normal geometry without an extra state request.
// Postcondition: the test closes the window and restores the application quit policy.
void ActionManagerTests::testEscapeRestoresLoadedImageWithoutGeometryJump()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString imagePath = createTestImage(dir, "escape-restore", Qt::darkGreen);
    QVERIFY(!imagePath.isEmpty());

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.setWindowState(Qt::WindowNoState);
    window.show();
    window.raise();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);
    QTRY_VERIFY_WITH_TIMEOUT(window.isActiveWindow(), 1000);
    window.openFile(imagePath);
    QTRY_VERIFY_WITH_TIMEOUT(window.getIsPixmapLoaded(), 5000);
    window.setGeometry(QRect(200, 200, 640, 480));
    QCoreApplication::processEvents();
    const QRect normalGeometry = window.geometry();
    const QString loadedPath = window.getCurrentFileDetails().fileInfo.absoluteFilePath();

    QElapsedTimer transitionTimer;
    transitionTimer.start();
    QTest::keyClick(&window, Qt::Key_Return);
    QTRY_VERIFY_WITH_TIMEOUT(window.isFullScreen(), 2000);
    reportFullscreenMetric("enter", transitionTimer.elapsed());
    QTest::qWait(2000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    transitionTimer.restart();
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen(), 2000);
    reportFullscreenMetric("exit", transitionTimer.elapsed());
    QTRY_VERIFY_WITH_TIMEOUT(!window.isFullScreen() && window.geometry() == normalGeometry, 3000);
    QVERIFY(window.isVisible());
    QCOMPARE(window.getCurrentFileDetails().fileInfo.absoluteFilePath(), loadedPath);
    QCOMPARE(window.geometry(), normalGeometry);

    window.close();
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ActionManagerTests::testEscapeClosesWindow()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    MainWindow window;
    window.setAttribute(Qt::WA_DeleteOnClose, false);
    window.show();
    window.activateWindow();
    QTRY_VERIFY_WITH_TIMEOUT(window.isVisible(), 1000);

    auto *escapeShortcut = window.findChild<QShortcut *>();
    QVERIFY(escapeShortcut);
    QCOMPARE(escapeShortcut->key(), QKeySequence(Qt::Key_Escape));
    QVERIFY(QMetaObject::invokeMethod(escapeShortcut, "activated", Qt::DirectConnection));
    QTRY_VERIFY_WITH_TIMEOUT(!window.isVisible(), 1000);

    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ApplicationEventTests::testFileOpenEventIsDeferredAndLoadsImage()
{
    const bool originalQuitOnLastWindowClosed = qvApp->quitOnLastWindowClosed();
    qvApp->setQuitOnLastWindowClosed(false);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString path = createTestImage(dir, "launch-services", Qt::darkGreen);
    QVERIFY(!path.isEmpty());

    QSettings settings;
    const bool hadFirstLaunch = settings.contains("firstlaunch");
    const QVariant oldFirstLaunch = settings.value("firstlaunch");
    settings.setValue("firstlaunch", true);

    auto *window = QVApplication::newWindow();
    QCoreApplication::processEvents();

    QFileOpenEvent openEvent(QUrl::fromLocalFile(path));
    QVERIFY(qvApp->event(&openEvent));
    QVERIFY(qvApp->hasPendingFileOpenEvents());
    QVERIFY(!window->hasFileOrPendingLoad());

    QTRY_VERIFY_WITH_TIMEOUT(!qvApp->hasPendingFileOpenEvents(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(window->getIsPixmapLoaded(), 5000);
    QCOMPARE(window->getCurrentFileDetails().fileInfo.absoluteFilePath(), QFileInfo(path).absoluteFilePath());

    window->close();
    if (hadFirstLaunch)
        settings.setValue("firstlaunch", oldFirstLaunch);
    else
        settings.remove("firstlaunch");
    qvApp->setQuitOnLastWindowClosed(originalQuitOnLastWindowClosed);
}

void ApplicationEventTests::testFileOpenEventWithoutPathIsIgnored()
{
    QVERIFY(!qvApp->hasPendingFileOpenEvents());

    QFileOpenEvent openEvent(QString{});
    QVERIFY(qvApp->event(&openEvent));
    QVERIFY(!qvApp->hasPendingFileOpenEvents());
}

void ImageCoreAndMovieTests::testColorSpaceConversion()
{
    QImage image(8, 8, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setColorSpace(QColorSpace::SRgb);

    TestableImageCore::handleColorSpaceConversion(image, QColorSpace::DisplayP3);

    QCOMPARE(image.colorSpace(), QColorSpace::DisplayP3);
}

void ImageCoreAndMovieTests::testMovieSpeedAndSingleFrameRead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = createTestImage(dir, "single-frame", Qt::yellow);
    QVERIFY(!path.isEmpty());

    QVMovie movie(path);
    QVERIFY(movie.isValid());
    QCOMPARE(movie.frameCount(), 1);
    movie.setSpeed(50);
    QCOMPARE(movie.speed(), 50);
}

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName("Fovelle");
    QCoreApplication::setOrganizationDomain("io.github.inostarlin-passion");
    QCoreApplication::setApplicationName("Fovelle");
    QGuiApplication::setApplicationDisplayName("Fovelle");
    QCoreApplication::setApplicationVersion("0.1.0");
    QVApplication app(argc, argv);
    qRegisterMetaType<QVImageLoader::Result>();

    ImageLoaderTests imageLoaderTests;
    FeatureTests featureTests;
    GraphicsViewTests graphicsViewTests;
    ApplicationEventTests applicationEventTests;
    ImageCoreAndMovieTests imageCoreAndMovieTests;
    int result = QTest::qExec(&imageLoaderTests, argc, argv);
    result |= QTest::qExec(&featureTests, argc, argv);
    result |= QTest::qExec(&graphicsViewTests, argc, argv);
    result |= QTest::qExec(&applicationEventTests, argc, argv);
    result |= QTest::qExec(&imageCoreAndMovieTests, argc, argv);
    return result;
}

#include "tst_qviewtests.moc"
