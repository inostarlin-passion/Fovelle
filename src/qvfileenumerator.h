#ifndef QVFILEENUMERATOR_H
#define QVFILEENUMERATOR_H

#include "qvnamespace.h"
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

    CompatibleFileList getCompatibleFiles(const QString &dirPath) const;
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
    qint64 getFileTimeSortKey(const QDirListing::DirEntry &dirEntry, const QFileDevice::FileTime type) const;
#else
    qint64 getFileTimeSortKey(const QFileInfo &fileInfo, const QFileDevice::FileTime type) const;
#endif
    qint64 getRandomSortKey(const QString &filePath) const;

private:
    const quint32 baseRandomSortSeed {static_cast<quint32>(std::chrono::system_clock::now().time_since_epoch().count())};

    QCollator collator;

    bool isLoopFoldersEnabled {true};
    Qv::SortMode globalSortMode {Qv::SortMode::Name};
    Qv::SortMode sortMode {Qv::SortMode::Name};
    bool globalSortDescending {false};
    bool sortDescending {false};
    bool allowMimeContentDetection {false};
    bool skipHiddenFiles {false};
};

#endif // QVFILEENUMERATOR_H
