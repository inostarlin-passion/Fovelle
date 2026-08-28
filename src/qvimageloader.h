#ifndef QVIMAGELOADER_H
#define QVIMAGELOADER_H

#include "qvcocoafunctions.h"

#include <optional>
#include <memory>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QThreadPool>

class QVImageLoader : public QObject
{
    Q_OBJECT

public:
    struct ErrorData
    {
        int errorNum;
        QString errorString;
    };

    struct Result
    {
        QImage image;
        Qv::VectorImageData vectorImage;
        QVCocoaFunctions::SDRImagePtr sdrImage;
        QVCocoaFunctions::HDRImagePtr hdrImage;
        QVCocoaFunctions::HDRMetadata hdrMetadata;
        QString absoluteFilePath;
        qint64 fileSize = 0;
        QDateTime lastModified;
        bool isMultiFrameImage = false;
        QSize intrinsicSize;
        double decodeMilliseconds = 0.0;
        bool preloadRejected = false;
        quint64 estimatedDecodedBytes = 0;
        QString preloadRejectionReason;
        std::optional<ErrorData> errorData;
    };

    struct PreloadAdmission
    {
        bool allowed = true;
        qint64 fileSize = 0;
        QSize pixelSize;
        quint64 estimatedDecodedBytes = 0;
        QString reason;
    };

    struct DesiredImage
    {
        QString absoluteFilePath;
        int priority = 0;
    };

    explicit QVImageLoader(QObject *parent = nullptr);
    ~QVImageLoader() override;

    void setLargestDimension(int value);

    static PreloadAdmission preloadAdmissionForFile(
        const QString &absoluteFilePath);

    quint64 requestImage(const QString &absoluteFilePath, bool forceReload = false);
    void setDesiredImages(const QList<DesiredImage> &desiredImages);
    void clear();

signals:
    void imageReady(quint64 requestId, const QVImageLoader::Result &result);
    void loadStarted(const QString &absoluteFilePath, int priority);
    void preloadSkipped(const QString &absoluteFilePath, qint64 fileSize,
                        quint64 estimatedDecodedBytes, const QString &reason);

private:
    struct FileIdentity
    {
        qint64 fileSize = 0;
        QDateTime lastModified;

        bool operator==(const FileIdentity &other) const;
        bool operator!=(const FileIdentity &other) const { return !(*this == other); }
    };

    enum class State
    {
        Queued,
        Loading,
        Cached
    };

    struct Entry
    {
        int priority = 0;
        bool desired = false;
        bool reloadAfterFinish = false;
        State state = State::Queued;
        FileIdentity expectedIdentity;
        FileIdentity startedIdentity;
        quint64 generation = 0;
        std::optional<Result> result;
    };

    struct PendingRequest
    {
        quint64 id;
        QString absoluteFilePath;
    };

    static QString normalizePath(const QString &path);
    static FileIdentity getFileIdentity(const QString &absoluteFilePath);
    static FileIdentity getFileIdentity(const Result &result);
    static Result readFile(const QString &absoluteFilePath, int largestDimension,
                           bool isPreload);

    bool isWanted(const QString &absoluteFilePath, const Entry &entry) const;
    void queueCachedDelivery(quint64 requestId, const QString &absoluteFilePath);
    void deliverResult(quint64 requestId, const QString &absoluteFilePath);
    void startReadyJobs();
    void startJob(const QString &absoluteFilePath);
    void jobFinished(const QString &absoluteFilePath, quint64 generation, Result result);

    QHash<QString, Entry> entries;
    std::optional<PendingRequest> pendingRequest;
    std::shared_ptr<int> lifetimeToken = std::make_shared<int>(0);
    QThreadPool imageThreadPool;

    quint64 nextRequestId = 0;
    int largestDimension = 1920;
};

Q_DECLARE_METATYPE(QVImageLoader::Result)

#endif // QVIMAGELOADER_H
