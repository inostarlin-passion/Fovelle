// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only
// Qt-Security score:critical reason:data-parser

#include "qvmovie.h"
#include "asyncwork.h"
#include <list>
#include <QThread>
#include "qvcocoafunctions.h"

#include "qelapsedtimer.h"
#include "qimage.h"
#include "qimagereader.h"
#include "qpixmap.h"
#include "qrect.h"
#include "qelapsedtimer.h"
#include "qtimer.h"
#include "qlist.h"
#include "qbuffer.h"
#include "qdir.h"

#include <chrono>
#include <map>
#include <memory>

#define QMOVIE_INVALID_DELAY -1

QT_BEGIN_NAMESPACE

class QFrameInfo
{
public:
    QImage image;
    int delay;
    bool endMark;
    inline QFrameInfo(bool endMark) : image(QImage()), delay(QMOVIE_INVALID_DELAY), endMark(endMark)
    {
    }

    inline QFrameInfo() : image(QImage()), delay(QMOVIE_INVALID_DELAY), endMark(false) { }

    inline QFrameInfo(QImage image, int delay)
        : image(std::move(image)), delay(delay), endMark(false)
    {
    }

    inline bool isValid() const
    {
        return endMark || !(image.isNull() && (delay == QMOVIE_INVALID_DELAY));
    }

    inline bool isEndMarker() { return endMark; }

    static inline QFrameInfo endMarker() { return QFrameInfo(true); }
};
Q_DECLARE_TYPEINFO(QFrameInfo, Q_RELOCATABLE_TYPE);

// This decoder is only touched by one read job at a time. It owns no GUI
// object and never borrows the synchronous reader used by manual frame jumps.
struct AsyncMovieDecoder
{
    QString path;
    QByteArray format;
    std::unique_ptr<QImageReader> reader;
    std::unique_ptr<QVCocoaFunctions::AnimatedImage> native;
    int lastFrame = -1;
    QFrameInfo read(int target)
    {
        Q_ASSERT(QThread::currentThread() != QCoreApplication::instance()->thread());
        if (!reader) {
            reader = std::make_unique<QImageReader>(path, format);
            reader->setAutoTransform(true);
            native = QVCocoaFunctions::createAnimatedImage(path);
        }
        if (native) {
            if (target >= native->frameCount())
                return QFrameInfo::endMarker();
            QImage image = native->frame(target);
            if (image.isNull())
                return { };
            return QFrameInfo(std::move(image), native->frameDelay(target));
        }
        const int count = reader->imageCount();
        if (count > 0 && target >= count)
            return QFrameInfo::endMarker();
        if (!reader->supportsAnimation()) {
            if (!reader->jumpToImage(target) && target != 0)
                return { };
            QImage image = reader->read();
            return image.isNull() ? QFrameInfo{ } : QFrameInfo(std::move(image), 1000);
        }
        if (target != lastFrame + 1) {
            reader = std::make_unique<QImageReader>(path, format);
            reader->setAutoTransform(true);
            lastFrame = -1;
        }
        while (lastFrame < target) {
            if (QvAsync::isCanceled())
                return { };
            if (!reader->canRead())
                return QFrameInfo::endMarker();
            QImage image = reader->read();
            if (image.isNull())
                return { };
            ++lastFrame;
            if (lastFrame == target)
                return QFrameInfo(std::move(image), reader->nextImageDelay());
        }
        return { };
    }
};

class QVMoviePrivate
{
    Q_DECLARE_PUBLIC(QVMovie)

public:
    QVMoviePrivate();

    void init(QVMovie *qq, std::unique_ptr<QImageReader> r);

    bool isDone();
    bool next();
    int speedAdjustedDelay(int delay) const;
    bool isValid() const;
    bool jumpToFrame(int frameNumber);
    int frameCount() const;
    bool jumpToNextFrame();
    QFrameInfo infoForFrame(int frameNumber);
    void reset();
    void cancelNextLoad();
    void configureNativeAnimation();
    void cacheFrame(int number, const QFrameInfo &info);
    void loadAsyncFrame();
    void publishFrame(const QFrameInfo &info);
    void finishPlayback();

    inline void enterState(QVMovie::MovieState newState)
    {
        movieState = newState;
        emit q_ptr->stateChanged(newState);
    }

    void _q_loadNextFrame();
    void _q_loadNextFrame(bool starting);

    QVMovie *q_ptr = nullptr;
    std::unique_ptr<QImageReader> reader = nullptr;
    std::unique_ptr<QVCocoaFunctions::AnimatedImage> nativeAnimation;
    int speed = 100;

    QVMovie::MovieState movieState = QVMovie::NotRunning;
    QRect frameRect;
    QPixmap currentPixmap;
    int currentFrameNumber = -1;
    int nextFrameNumber = 0;
    int greatestFrameNumber = -1;
    int nextDelay = 0;
    std::optional<std::chrono::steady_clock::time_point> nextLoadTime = std::nullopt;
    int playCounter = -1;
    qint64 initialDevicePos = 0;
    QVMovie::CacheMode cacheMode = QVMovie::CacheNone;
    bool haveReadAll = false;
    bool isFirstIteration = true;
    std::map<int, QFrameInfo> frameMap;
    std::list<int> cacheOrder;
    quint64 cacheByteLimit = 64ULL * 1024 * 1024;
    quint64 cachedBytes = 0;
    bool asyncPlayback = false;
    quint64 playbackGeneration = 0;
    QvAsync::Ticket frameTicket;
    std::shared_ptr<AsyncMovieDecoder> asyncDecoder;
    std::optional<QFrameInfo> pendingFrame;
    QString absoluteFilePath;

    QTimer *nextImageTimer = nullptr;
};

QVMoviePrivate::QVMoviePrivate() { }

void QVMoviePrivate::init(QVMovie *qq, std::unique_ptr<QImageReader> r)
{
    q_ptr = qq;
    reader = std::move(r);
    nextImageTimer = new QTimer(qq);
    nextImageTimer->setTimerType(Qt::PreciseTimer);
    nextImageTimer->setSingleShot(true);
    QObject::connect(nextImageTimer, &QTimer::timeout, qq, [this]() { _q_loadNextFrame(); });
}

void QVMoviePrivate::reset()
{
    cancelNextLoad();
    if (reader->device())
        initialDevicePos = reader->device()->pos();
    currentFrameNumber = -1;
    nextFrameNumber = 0;
    greatestFrameNumber = -1;
    nextDelay = 0;
    playCounter = -1;
    haveReadAll = false;
    isFirstIteration = true;
    frameMap.clear();
    cacheOrder.clear();
    cachedBytes = 0;
    asyncDecoder.reset();
}

void QVMoviePrivate::configureNativeAnimation()
{
    nativeAnimation.reset();
    if (!absoluteFilePath.isEmpty())
        nativeAnimation = QVCocoaFunctions::createAnimatedImage(absoluteFilePath);
}

void QVMoviePrivate::cancelNextLoad()
{
    ++playbackGeneration;
    QvAsync::cancel(frameTicket);
    frameTicket.reset();
    asyncDecoder.reset();
    pendingFrame.reset();
    nextLoadTime = std::nullopt;
    nextImageTimer->stop();
}

bool QVMoviePrivate::isDone()
{
    return (playCounter == 0);
}

int QVMoviePrivate::speedAdjustedDelay(int delay) const
{
    return int((qint64(delay) * qint64(100)) / qint64(speed));
}

QFrameInfo QVMoviePrivate::infoForFrame(int frameNumber)
{
    Q_Q(QVMovie);

    if (frameNumber < 0)
        return QFrameInfo(); // Invalid

    if (haveReadAll && (frameNumber > greatestFrameNumber)) {
        if (frameNumber == greatestFrameNumber + 1)
            return QFrameInfo::endMarker();
        return QFrameInfo(); // Invalid
    }

    if (nativeAnimation) {
        if (frameNumber >= nativeAnimation->frameCount()) {
            haveReadAll = true;
            return QFrameInfo::endMarker();
        }

        if (cacheMode == QVMovie::CacheAll) {
            const auto cached = frameMap.find(frameNumber);
            if (cached != frameMap.end()) {
                cacheOrder.remove(frameNumber);
                cacheOrder.push_back(frameNumber);
                return cached->second;
            }
        }

        QImage image = nativeAnimation->frame(frameNumber);
        if (image.isNull())
            return QFrameInfo();

        QFrameInfo info(std::move(image), nativeAnimation->frameDelay(frameNumber));
        if (frameNumber > greatestFrameNumber)
            greatestFrameNumber = frameNumber;
        if (cacheMode == QVMovie::CacheAll)
            cacheFrame(frameNumber, info);
        return info;
    }

    // For an animated image format, the tradition is that QMovie calls read()
    // until canRead() == false, because the number of frames may not be known
    // in advance; but if we're abusing a multi-frame format as an animation,
    // canRead() may remain true, and we need to stop after reading the maximum
    // number of frames that the image provides.
    const bool supportsAnimation = reader->supportsOption(QImageIOHandler::Animation);
    const int stopAtFrame = supportsAnimation ? -1 : frameCount();

    // For an animated image format, QImageIOHandler::nextImageDelay() should
    // provide the time to wait until showing the next frame; but multi-frame
    // formats are not expected to provide this value, so use 1000 ms by default.
    const auto nextFrameDelay = [&]() {
        return supportsAnimation ? reader->nextImageDelay() : 1000;
    };

    if (cacheMode == QVMovie::CacheNone) {
        if (frameNumber != currentFrameNumber + 1) {
            // Non-sequential frame access
            if (!reader->jumpToImage(frameNumber)) {
                if (frameNumber == 0) {
                    // Special case: Attempt to "rewind" so we can loop
                    // ### This could be implemented as QImageReader::rewind()
                    if (reader->device()->isSequential())
                        return QFrameInfo(); // Invalid
                    QString fileName = reader->fileName();
                    QByteArray format = reader->format();
                    QIODevice *device = reader->device();
                    QColor bgColor = reader->backgroundColor();
                    QSize scaledSize = reader->scaledSize();
                    if (fileName.isEmpty())
                        reader = std::make_unique<QImageReader>(device, format);
                    else
                        reader = std::make_unique<QImageReader>(absoluteFilePath, format);
                    if (!reader->canRead()) // Provoke a device->open() call
                        emit q->error(reader->error());
                    reader->device()->seek(initialDevicePos);
                    reader->setBackgroundColor(bgColor);
                    reader->setScaledSize(scaledSize);
                } else {
                    return QFrameInfo(); // Invalid
                }
            }
        }
        if (stopAtFrame > 0 ? (frameNumber < stopAtFrame) : reader->canRead()) {
            // reader says we can read. Attempt to actually read image
            // But if it's a non-animated multi-frame format and we know the frame count, stop
            // there.
            if (stopAtFrame > 0)
                reader->jumpToImage(frameNumber);
            QImage anImage = reader->read();
            if (anImage.isNull()) {
                // Reading image failed.
                return QFrameInfo(); // Invalid
            }
            if (frameNumber > greatestFrameNumber)
                greatestFrameNumber = frameNumber;
            return QFrameInfo(std::move(anImage), nextFrameDelay());
        } else if (frameNumber != 0) {
            // We've read all frames now. Return an end marker
            haveReadAll = true;
            return QFrameInfo::endMarker();
        } else {
            // No readable frames
            haveReadAll = true;
            return QFrameInfo();
        }
    }

    // Eviction is independent of how far decoding has progressed. Rewind a
    // seekable file reader for an evicted target; do not fabricate a cache hit.
    if (frameMap.find(frameNumber) == frameMap.end() && frameNumber <= greatestFrameNumber) {
        reader = std::make_unique<QImageReader>(absoluteFilePath, reader->format());
        reader->setAutoTransform(true);
        greatestFrameNumber = -1;
        haveReadAll = false;
    }
    // CacheMode == CacheAll (bounded by actual decoded bytes).
    if (frameNumber > greatestFrameNumber) {
        // Frame hasn't been read from file yet. Try to do it
        for (int i = greatestFrameNumber + 1; i <= frameNumber; ++i) {
            if (stopAtFrame > 0 ? (frameNumber < stopAtFrame) : reader->canRead()) {
                // reader says we can read. Attempt to actually read image
                // But if it's a non-animated multi-frame format and we know the frame count, stop
                // there.
                if (stopAtFrame > 0)
                    reader->jumpToImage(frameNumber);
                QImage anImage = reader->read();
                if (anImage.isNull()) {
                    // Reading image failed.
                    return QFrameInfo(); // Invalid
                }
                greatestFrameNumber = i;
                QFrameInfo info(std::move(anImage), nextFrameDelay());
                // Cache it!
                cacheFrame(i, info);
                if (i == frameNumber) {
                    return info;
                }
            } else {
                // We've read all frames now. Return an end marker
                haveReadAll = true;
                return frameNumber == greatestFrameNumber + 1 ? QFrameInfo::endMarker()
                                                              : QFrameInfo();
            }
        }
    }
    // Return info for requested (cached) frame
    const auto it = frameMap.find(frameNumber);
    if (it == frameMap.cend())
        return QFrameInfo();
    cacheOrder.remove(frameNumber);
    cacheOrder.push_back(frameNumber);
    return it->second;
}

bool QVMoviePrivate::next()
{
    QFrameInfo info = infoForFrame(nextFrameNumber);
    if (!info.isValid())
        return false;
    if (info.isEndMarker()) {
        // We reached the end of the animation.
        if (isFirstIteration) {
            if (nextFrameNumber == 0) {
                // No frames could be read at all (error).
                return false;
            }
            // End of first iteration. Initialize play counter
            playCounter = nativeAnimation ? nativeAnimation->loopCount() : reader->loopCount();
            isFirstIteration = false;
        }
        // Loop as appropriate
        if (playCounter != 0) {
            if (playCounter != -1) // Infinite?
                playCounter--; // Nope
            nextFrameNumber = 0;
            return next();
        }
        // Loop no more. Done
        return false;
    }
    // Image and delay OK, update internal state
    currentFrameNumber = nextFrameNumber++;
    currentPixmap = QPixmap::fromImage(info.image);

    if (!speed)
        return true;

    nextDelay = speedAdjustedDelay(info.delay);
    return true;
}

void QVMoviePrivate::_q_loadNextFrame()
{
    _q_loadNextFrame(false);
}

void QVMoviePrivate::cacheFrame(int number, const QFrameInfo &info)
{
    if (cacheMode != QVMovie::CacheAll || !info.isValid() || info.endMark)
        return;
    auto existing = frameMap.find(number);
    if (existing != frameMap.end()) {
        cachedBytes -= quint64(existing->second.image.sizeInBytes());
        frameMap.erase(existing);
        cacheOrder.remove(number);
    }
    const quint64 bytes = quint64(info.image.sizeInBytes());
    if (bytes > cacheByteLimit)
        return;
    while (cachedBytes > cacheByteLimit - bytes && !cacheOrder.empty()) {
        const int victim = cacheOrder.front();
        cacheOrder.pop_front();
        cachedBytes -= quint64(frameMap.at(victim).image.sizeInBytes());
        frameMap.erase(victim);
    }
    frameMap[number] = info;
    cacheOrder.push_back(number);
    cachedBytes += bytes;
}

void QVMoviePrivate::finishPlayback()
{
    Q_Q(QVMovie);
    nextFrameNumber = 0;
    nextLoadTime.reset();
    isFirstIteration = true;
    playCounter = -1;
    enterState(QVMovie::NotRunning);
    emit q->finished();
}

void QVMoviePrivate::publishFrame(const QFrameInfo &info)
{
    Q_Q(QVMovie);
    const quint64 generation = playbackGeneration;
    currentFrameNumber = nextFrameNumber++;
    currentPixmap = QPixmap::fromImage(info.image);
    nextDelay = speed ? speedAdjustedDelay(qMax(1, info.delay)) : 0;
    if (frameRect.size() != currentPixmap.rect().size()) {
        frameRect = currentPixmap.rect();
        emit q->resized(frameRect.size());
    }
    if (generation != playbackGeneration)
        return;
    emit q->updated(frameRect);
    if (generation != playbackGeneration)
        return;
    emit q->frameChanged(currentFrameNumber);
    if (generation == playbackGeneration && speed && movieState == QVMovie::Running) {
        const auto now = std::chrono::steady_clock::now();
        nextLoadTime = nextLoadTime.value_or(now) + std::chrono::milliseconds(nextDelay);
        int remaining = int(
                std::chrono::duration_cast<std::chrono::milliseconds>(*nextLoadTime - now).count());
        if (remaining < -1000) {
            nextLoadTime.reset();
            remaining = 0;
        }
        nextImageTimer->start(qMax(0, remaining));
    }
}

void QVMoviePrivate::loadAsyncFrame()
{
    Q_Q(QVMovie);
    if (frameTicket)
        return;
    if (!nextLoadTime)
        nextLoadTime = std::chrono::steady_clock::now();
    if (!asyncDecoder) {
#ifdef FOVELLE_MOVIE_TEST_METRICS
        q->setProperty("decoderCreations", q->property("decoderCreations").toInt() + 1);
#endif
        asyncDecoder = std::make_shared<AsyncMovieDecoder>();
        asyncDecoder->path = absoluteFilePath;
        asyncDecoder->format = reader->format();
    }
    const int target = nextFrameNumber;
    const quint64 generation = playbackGeneration;
    const auto decoder = asyncDecoder;
    const auto found = frameMap.find(target);
    std::optional<QFrameInfo> cached =
            found == frameMap.end() ? std::nullopt : std::optional<QFrameInfo>(found->second);
    if (found != frameMap.end()) {
        cacheOrder.remove(target);
        cacheOrder.push_back(target);
    }
    // A read may finish while paused. Consume that exact result without
    // asking the sequential decoder to read the same target a second time.
    if (pendingFrame) {
        cached = std::move(pendingFrame);
        pendingFrame.reset();
    }
    frameTicket = QvAsync::run(
            [decoder, target, cached] { return cached ? *cached : decoder->read(target); },
            [this, generation, target, q](QFrameInfo info) {
                if (generation != playbackGeneration)
                    return;
#ifdef FOVELLE_MOVIE_TEST_METRICS
                q->setProperty("readCompletions", q->property("readCompletions").toInt() + 1);
#endif
                frameTicket.reset();
                if (movieState == QVMovie::Paused) {
                    pendingFrame = std::move(info);
                    return;
                }
                if (movieState != QVMovie::Running)
                    return;
                if (info.endMark) {
                    if (target == 0) {
                        emit q->error(QImageReader::InvalidDataError);
                        if (generation == playbackGeneration)
                            finishPlayback();
                        return;
                    }
                    if (isFirstIteration) {
                        playCounter = q->loopCount();
                        isFirstIteration = false;
                    }
                    if (playCounter == 0) {
                        finishPlayback();
                        return;
                    }
                    if (playCounter > 0)
                        --playCounter;
                    nextFrameNumber = 0;
                    loadAsyncFrame();
                    return;
                }
                if (!info.isValid()) {
                    emit q->error(QImageReader::InvalidDataError);
                    if (generation == playbackGeneration)
                        finishPlayback();
                    return;
                }
                cacheFrame(target, info);
                publishFrame(info);
            });
}

void QVMoviePrivate::_q_loadNextFrame(bool starting)
{
    Q_Q(QVMovie);
    if (asyncPlayback) {
        if (starting && movieState == QVMovie::NotRunning) {
            enterState(QVMovie::Running);
            emit q->started();
        }
        loadAsyncFrame();
        return;
    }
    const auto loadStartTime = std::chrono::steady_clock::now();
    if (next()) {
        if (starting && movieState == QVMovie::NotRunning) {
            enterState(QVMovie::Running);
            emit q->started();
        }

        if (frameRect.size() != currentPixmap.rect().size()) {
            frameRect = currentPixmap.rect();
            emit q->resized(frameRect.size());
        }

        emit q->updated(frameRect);
        emit q->frameChanged(currentFrameNumber);

        if (speed && movieState == QVMovie::Running) {
            nextLoadTime = (nextLoadTime.has_value() ? nextLoadTime.value() : loadStartTime)
                    + std::chrono::milliseconds(nextDelay);
            const int adjustedNextDelay =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                            nextLoadTime.value() - std::chrono::steady_clock::now())
                            .count();
            if (adjustedNextDelay < -1000) {
                // If we get too far behind, don't try to catch up
                nextLoadTime = std::nullopt;
            }
            nextImageTimer->start(std::max(0, adjustedNextDelay));
        }
    } else {
        // Could not read another frame
        if (!isDone()) {
            emit q->error(reader->error());
        }

        // Graceful finish
        if (movieState != QVMovie::Paused) {
            nextFrameNumber = 0;
            nextLoadTime = std::nullopt;
            isFirstIteration = true;
            playCounter = -1;
            enterState(QVMovie::NotRunning);
            emit q->finished();
        }
    }
}

bool QVMoviePrivate::isValid() const
{
    Q_Q(const QVMovie);

    if (greatestFrameNumber >= 0)
        return true; // have we seen valid data
    bool canRead = reader->canRead();
    if (!canRead) {
        // let the consumer know it's broken
        //
        // ### the const_cast here is ugly, but 'const' of this method is
        // technically wrong right now, since it may cause the underlying device
        // to open.
        emit const_cast<QVMovie *>(q)->error(reader->error());
    }
    return canRead;
}

bool QVMoviePrivate::jumpToFrame(int frameNumber)
{
    if (frameNumber < 0)
        return false;
    if (currentFrameNumber == frameNumber)
        return true;
    nextFrameNumber = frameNumber;
    // A paused movie can still own an in-flight read or a pending result.
    cancelNextLoad();
    // Explicit frame jumps remain synchronous and use an independent reader.
    const bool wasAsync = asyncPlayback;
    asyncPlayback = false;
    _q_loadNextFrame();
    asyncPlayback = wasAsync;
    return (nextFrameNumber == currentFrameNumber + 1);
}

int QVMoviePrivate::frameCount() const
{
    int result;
    if ((result = reader->imageCount()) != 0)
        return result;
    if (haveReadAll)
        return greatestFrameNumber + 1;
    return 0; // Don't know
}

bool QVMoviePrivate::jumpToNextFrame()
{
    return jumpToFrame(currentFrameNumber + 1);
}

QVMovie::QVMovie(QObject *parent) : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>());
}

QVMovie::QVMovie(QIODevice *device, const QByteArray &format, QObject *parent)
    : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>(device, format));
    d->initialDevicePos = device->pos();
}

QVMovie::QVMovie(const QString &fileName, const QByteArray &format, QObject *parent)
    : QObject(parent), d_ptr(new QVMoviePrivate)
{
    Q_D(QVMovie);
    d->init(this, std::make_unique<QImageReader>(fileName, format));
    d->absoluteFilePath = QDir(fileName).absolutePath();
    d->configureNativeAnimation();
    if (d->reader->device())
        d->initialDevicePos = d->reader->device()->pos();
}

QVMovie::~QVMovie()
{
    Q_D(QVMovie);
    d->cancelNextLoad();
    d->reader.reset();
}

void QVMovie::setDevice(QIODevice *device)
{
    stop();
    Q_D(QVMovie);
    d->nativeAnimation.reset();
    d->reader->setDevice(device);
    d->reset();
}

QIODevice *QVMovie::device() const
{
    Q_D(const QVMovie);
    return d->reader->device();
}

void QVMovie::setFileName(const QString &fileName)
{
    stop();
    Q_D(QVMovie);
    d->absoluteFilePath = QDir(fileName).absolutePath();
    d->reader->setFileName(fileName);
    d->configureNativeAnimation();
    d->reset();
}

QString QVMovie::fileName() const
{
    Q_D(const QVMovie);
    return d->reader->fileName();
}

void QVMovie::setFormat(const QByteArray &format)
{
    Q_D(QVMovie);
    d->reader->setFormat(format);
}

QByteArray QVMovie::format() const
{
    Q_D(const QVMovie);
    return d->reader->format();
}

void QVMovie::setBackgroundColor(const QColor &color)
{
    Q_D(QVMovie);
    d->reader->setBackgroundColor(color);
}

QColor QVMovie::backgroundColor() const
{
    Q_D(const QVMovie);
    return d->reader->backgroundColor();
}

QVMovie::MovieState QVMovie::state() const
{
    Q_D(const QVMovie);
    return d->movieState;
}

QRect QVMovie::frameRect() const
{
    Q_D(const QVMovie);
    return d->frameRect;
}

QPixmap QVMovie::currentPixmap() const
{
    Q_D(const QVMovie);
    return d->currentPixmap;
}

QImage QVMovie::currentImage() const
{
    Q_D(const QVMovie);
    return d->currentPixmap.toImage();
}

bool QVMovie::isValid() const
{
    Q_D(const QVMovie);
    if (d->nativeAnimation)
        return d->nativeAnimation->isValid();
    return d->isValid();
}

QImageReader::ImageReaderError QVMovie::lastError() const
{
    Q_D(const QVMovie);
    return d->reader->error();
}

QString QVMovie::lastErrorString() const
{
    Q_D(const QVMovie);
    return d->reader->errorString();
}

int QVMovie::frameCount() const
{
    Q_D(const QVMovie);
    if (d->nativeAnimation)
        return d->nativeAnimation->frameCount();
    return d->frameCount();
}

int QVMovie::nextFrameDelay() const
{
    Q_D(const QVMovie);
    return d->nextDelay;
}

int QVMovie::currentFrameNumber() const
{
    Q_D(const QVMovie);
    return d->currentFrameNumber;
}

bool QVMovie::jumpToNextFrame()
{
    Q_D(QVMovie);
    return d->jumpToNextFrame();
}

bool QVMovie::jumpToFrame(int frameNumber)
{
    Q_D(QVMovie);
    return d->jumpToFrame(frameNumber);
}

int QVMovie::loopCount() const
{
    Q_D(const QVMovie);
    if (d->nativeAnimation)
        return d->nativeAnimation->loopCount();
    return d->reader->loopCount();
}

void QVMovie::setPaused(bool paused)
{
    Q_D(QVMovie);
    if (paused) {
        if (d->movieState == NotRunning || d->movieState == Paused)
            return;
        // Keep the decoder and its single in-flight read alive. Recreating a
        // GIF reader would replay every preceding frame on resume.
        d->nextImageTimer->stop();
        d->nextLoadTime.reset();
        d->enterState(Paused);
    } else {
        if (d->movieState == Running)
            return;
        d->nextLoadTime.reset();
        d->enterState(Running);
        d->_q_loadNextFrame();
    }
}

void QVMovie::setSpeed(int percentSpeed)
{
    Q_D(QVMovie);
    if (d->speed == percentSpeed || percentSpeed < 0)
        return;
    int oldSpeed = d->speed;
    d->speed = percentSpeed;
    if (d->movieState == Running) {
        if (!percentSpeed)
            d->cancelNextLoad();
        else if (!oldSpeed)
            d->_q_loadNextFrame();
    }
}

int QVMovie::speed() const
{
    Q_D(const QVMovie);
    return d->speed;
}

void QVMovie::start()
{
    Q_D(QVMovie);
    if (d->movieState == NotRunning) {
        d->_q_loadNextFrame(true);
    } else if (d->movieState == Paused) {
        setPaused(false);
    }
}

void QVMovie::stop()
{
    Q_D(QVMovie);
    d->cancelNextLoad();
    if (d->movieState == NotRunning)
        return;
    d->enterState(NotRunning);
    d->nextFrameNumber = 0;
}

QSize QVMovie::scaledSize()
{
    Q_D(QVMovie);
    return d->reader->scaledSize();
}

void QVMovie::setScaledSize(const QSize &size)
{
    Q_D(QVMovie);
    d->reader->setScaledSize(size);
}

QList<QByteArray> QVMovie::supportedFormats()
{
    QList<QByteArray> list = QImageReader::supportedImageFormats();

    QBuffer buffer;
    buffer.open(QIODevice::ReadOnly);

    const auto doesntSupportAnimation = [&buffer](const QByteArray &format) {
        return !QImageReader(&buffer, format).supportsOption(QImageIOHandler::Animation);
    };

    list.removeIf(doesntSupportAnimation);
    return list;
}

QVMovie::CacheMode QVMovie::cacheMode() const
{
    Q_D(const QVMovie);
    return d->cacheMode;
}

void QVMovie::setCacheMode(CacheMode cacheMode)
{
    Q_D(QVMovie);
    d->cacheMode = cacheMode;
    if (cacheMode == CacheNone) {
        d->frameMap.clear();
        d->cacheOrder.clear();
        d->cachedBytes = 0;
    }
}

void QVMovie::setCacheByteLimit(quint64 bytes)
{
    Q_D(QVMovie);
    d->cacheByteLimit = bytes;
    while (d->cachedBytes > bytes && !d->cacheOrder.empty()) {
        const int victim = d->cacheOrder.front();
        d->cacheOrder.pop_front();
        d->cachedBytes -= quint64(d->frameMap.at(victim).image.sizeInBytes());
        d->frameMap.erase(victim);
    }
}
quint64 QVMovie::cachedBytes() const
{
    Q_D(const QVMovie);
    return d->cachedBytes;
}
void QVMovie::setAsyncPlayback(bool enabled)
{
    Q_D(QVMovie);
    if (d->asyncPlayback == enabled)
        return;
    d->cancelNextLoad();
    d->asyncPlayback = enabled;
}
QT_END_NAMESPACE
