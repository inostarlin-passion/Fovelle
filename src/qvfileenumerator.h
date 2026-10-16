#ifndef QVFILEENUMERATOR_H
#define QVFILEENUMERATOR_H

#include "qvnamespace.h"
#include "asyncwork.h"
#include <QSet>
#include <QCollator>
#include <QList>
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
#include <QDirListing>
#endif
#include <QFileInfo>
#include <QObject>

class QVFileEnumerator : public QObject
{
    Q_OBJECT
public:
    struct CompatibleFile
    {
        QString absoluteFilePath;
        qint64 numericSortKey;
        QString stringSortKey;
    };

    class CompatibleFileList : public QList<CompatibleFile>
    {
    public:
        CompatibleFileList() = default;

        explicit CompatibleFileList(const QString &baseDir, const bool isRecursive) :
            baseDir(baseDir),
            isRecursive(isRecursive)
        {}

        QString getBaseDir() const { return baseDir; }

        bool getIsRecursive() const { return isRecursive; }

    private:
        QString baseDir;
        bool isRecursive {false};
    };

    explicit QVFileEnumerator(QObject *parent = nullptr);

    struct ScanOptions
    {
        QSet<QString> extensions;
        QSet<QString> mimeTypes;
        Qv::SortMode sortMode = Qv::SortMode::Name;
        bool sortDescending = false;
        bool allowMimeContentDetection = false;
        bool skipHiddenFiles = false;
        quint32 randomSeed = 0;
        QCollator collator;
    };
    ScanOptions scanOptions() const;
    static CompatibleFileList scan(const QString &dirPath, const ScanOptions &options);
    CompatibleFileList getCompatibleFiles(const QString &dirPath) const;
    QvAsync::Ticket scanAsync(const QString &dirPath, QObject *receiver,
                              std::function<void(CompatibleFileList)> completion) const;
    bool getIsLoopFoldersEnabled() const { return isLoopFoldersEnabled; }
    Qv::SortMode getSortMode() const { return sortMode; }
    void setSortMode(const Qv::SortMode mode);
    bool getSortDescending() const { return sortDescending; }
    void setSortDescending(const bool descending);
    void loadSettings(const bool isInitialLoad);

signals:
    void sortParametersChanged();

protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    static qint64 getFileTimeSortKey(const QDirListing::DirEntry &dirEntry,
                                     const QFileDevice::FileTime type);
#else
    static qint64 getFileTimeSortKey(const QFileInfo &fileInfo, const QFileDevice::FileTime type);
#endif
    qint64 getRandomSortKey(const QString &filePath) const;

private:
    const quint32 baseRandomSortSeed {static_cast<quint32>(std::chrono::system_clock::now().time_since_epoch().count())};

    QCollator collator;

    bool isLoopFoldersEnabled {true};
    Qv::SortMode sortMode {Qv::SortMode::Name};
    bool sortDescending {false};
    bool allowMimeContentDetection {false};
    bool skipHiddenFiles {false};
};

#endif // QVFILEENUMERATOR_H
