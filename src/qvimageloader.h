#ifndef QVIMAGELOADER_H
#define QVIMAGELOADER_H

#include "qvcocoafunctions.h"
#include "colorinformation.h"
#include "asyncwork.h"
#include <functional>

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
        std::shared_ptr<void> sourceLifetime;
        QImage image;
        QvColor::SourceInfo sourceColor;
        Qv::VectorImageData vectorImage;
        QVCocoaFunctions::SDRImagePtr sdrImage;
        QVCocoaFunctions::HDRImagePtr hdrImage;
        QVCocoaFunctions::HDRMetadata hdrMetadata;
        QString cameraModel;
        QString absoluteFilePath;
        qint64 fileSize = 0;
        QDateTime lastModified;
        bool isMultiFrameImage = false;
        QSize intrinsicSize;
        double decodeMilliseconds = 0.0;
        QByteArray fileVersionStamp;
        bool versionMismatch = false;
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

    using ReadFunction = std::function<Result(const QString &, int, bool)>;
    explicit QVImageLoader(QObject *parent = nullptr, ReadFunction read = { });
    ~QVImageLoader() override;

    void setLargestDimension(int value);

    static PreloadAdmission preloadAdmissionForFile(const QString &absoluteFilePath);

    quint64 requestImage(const QString &absoluteFilePath, bool forceReload = false,
                         std::shared_ptr<void> sourceLifetime = { });
    static QByteArray fileVersion(const QString &path);
    quint64 requestVersionedImage(const QString &path, const QByteArray &version);
    void setDesiredImages(const QList<DesiredImage> &desiredImages);
    void clear();

    // Stop speculative and foreground decode work while the application is
    // still alive, so queued callbacks cannot outlive the GUI teardown.
    void shutdownAsyncWork();

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

    enum class State { Queued, Loading, Cached };

    struct Entry
    {
        std::shared_ptr<void> sourceLifetime;
        int priority = 0;
        bool desired = false;
        bool reloadAfterFinish = false;
        State state = State::Queued;
        FileIdentity expectedIdentity;
        FileIdentity startedIdentity;
        quint64 generation = 0;
        std::optional<Result> result;
        QvAsync::Ticket ticket;
    };

    struct PendingRequest
    {
        quint64 id;
        QString absoluteFilePath;
    };

    static QString normalizePath(const QString &path);
    static FileIdentity getFileIdentity(const QString &absoluteFilePath);
    static FileIdentity getFileIdentity(const Result &result);
    static Result readFile(const QString &absoluteFilePath, int largestDimension, bool isPreload);

    bool isWanted(const QString &absoluteFilePath, const Entry &entry) const;
    void queueCachedDelivery(quint64 requestId, const QString &absoluteFilePath);
    void deliverResult(quint64 requestId, const QString &absoluteFilePath);
    void cancelUnwantedJobs();
    void startReadyJobs();
    void startJob(const QString &absoluteFilePath);
    void jobFinished(const QString &absoluteFilePath, quint64 generation, Result result);

    QvAsync::Ticket versionedTicket;
    QHash<QString, Entry> entries;
    std::optional<PendingRequest> pendingRequest;
    ReadFunction readFunction;
    bool asyncWorkShutDown{ false };

    quint64 nextRequestId = 0;
    quint64 nextJobGeneration = 0;
    int largestDimension = 1920;
};

Q_DECLARE_METATYPE(QVImageLoader::Result)

#endif // QVIMAGELOADER_H
