#include "qvfileenumerator.h"
#include "qvapplication.h"
#if QT_VERSION < QT_VERSION_CHECK(6, 8, 0)
#include <QDirIterator>
#endif

QVFileEnumerator::QVFileEnumerator(QObject *parent)
    : QObject{parent}
{
    collator.setNumericMode(true);
    loadSettings(true);
}

QVFileEnumerator::CompatibleFileList QVFileEnumerator::getCompatibleFiles(const QString &dirPath) const
{
    const bool recurse = QFile::exists(QDir(dirPath).filePath("qv-recurse.txt"));
    CompatibleFileList fileList(dirPath, recurse);

    const QMimeDatabase mimeDb;
    const auto &extensions = qvApp->getFileExtensionSet();
    const auto &disabledExtensions = qvApp->getDisabledFileExtensions();
    const auto &mimeTypes = qvApp->getMimeTypeNameSet();
    const QMimeDatabase::MatchMode mimeMatchMode = allowMimeContentDetection ? QMimeDatabase::MatchDefault : QMimeDatabase::MatchExtension;

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Avoid the FilesOnly flag since it makes Qt check isSymLink which causes performance problems
    // accessing SMB shares from macOS. We'll check isFile later to include only files.
    const QDirListing::IteratorFlags flags =
        (!skipHiddenFiles ? QDirListing::IteratorFlag::IncludeHidden : QDirListing::IteratorFlags()) |
        (recurse ? QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::FollowDirSymlinks : QDirListing::IteratorFlags());
    for (const QDirListing::DirEntry &entry : QDirListing(dirPath, flags))
    {
        if (!entry.isFile())
            continue;
#else
    const QDir::Filters filters = QDir::Files | (!skipHiddenFiles ? QDir::Hidden : QDir::Filters());
    const QDirIterator::IteratorFlags flags = recurse ? QDirIterator::Subdirectories | QDirIterator::FollowSymlinks : QDirIterator::IteratorFlags();
    QDirIterator it(dirPath, filters, flags);
    while (it.hasNext())
    {
        it.next();
        const QFileInfo entry = it.fileInfo();
#endif
        const QString absoluteFilePath = entry.absoluteFilePath();
        const QString fileName = entry.fileName();
        const QString suffix = entry.suffix().toLower();
        bool matched = !suffix.isEmpty() && extensions.contains("." + suffix);
        QString mimeType;

        if (!matched || sortMode == Qv::SortMode::Type)
        {
            mimeType = mimeDb.mimeTypeForFile(absoluteFilePath, mimeMatchMode).name();
            matched |= mimeTypes.contains(mimeType) && (suffix.isEmpty() || !disabledExtensions.contains("." + suffix));
        }

        // ignore macOS ._ metadata files
        if (fileName.startsWith("._"))
        {
            matched = false;
        }

        if (matched)
        {
            qint64 numericSortKey = 0;
            QString stringSortKey;
            switch (sortMode)
            {
            case Qv::SortMode::DateModified:
                numericSortKey = getFileTimeSortKey(entry, QFileDevice::FileModificationTime);
                break;
            case Qv::SortMode::DateCreated:
                numericSortKey = getFileTimeSortKey(entry, QFileDevice::FileBirthTime);
                break;
            case Qv::SortMode::Size:
                numericSortKey = entry.size();
                break;
            case Qv::SortMode::Type:
                stringSortKey = mimeType;
                break;
            case Qv::SortMode::Random:
                numericSortKey = getRandomSortKey(absoluteFilePath);
                break;
            default:
                stringSortKey = fileName;
                break;
            }
            fileList.append({
                absoluteFilePath,
                numericSortKey,
                stringSortKey
            });
        }
    }

    std::sort(
        fileList.begin(),
        fileList.end(),
        [&](const CompatibleFile &file1, const CompatibleFile &file2) {
            int result =
                file1.numericSortKey < file2.numericSortKey ? -1 :
                file1.numericSortKey > file2.numericSortKey ? 1 :
                collator.compare(file1.stringSortKey, file2.stringSortKey);
            if (result == 0)
                result = collator.compare(file1.absoluteFilePath, file2.absoluteFilePath);
            return sortDescending ? (result > 0) : (result < 0);
        }
    );

    return fileList;
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
qint64 QVFileEnumerator::getFileTimeSortKey(const QDirListing::DirEntry &dirEntry, const QFileDevice::FileTime type) const
{
    return dirEntry.fileTime(type, QTimeZone::UTC).toMSecsSinceEpoch();
}
#else
qint64 QVFileEnumerator::getFileTimeSortKey(const QFileInfo &fileInfo, const QFileDevice::FileTime type) const
{
    return fileInfo.fileTime(type, QTimeZone::UTC).toMSecsSinceEpoch();
}
#endif

qint64 QVFileEnumerator::getRandomSortKey(const QString &filePath) const
{
    const QString seed = QString::number(baseRandomSortSeed, 16) + filePath;
    const QByteArray hash = QCryptographicHash::hash(seed.toUtf8(), QCryptographicHash::Md5);
    return static_cast<qint64>(hash.toHex().left(16).toULongLong(nullptr, 16));
}

void QVFileEnumerator::setSortMode(const Qv::SortMode mode)
{
    if (sortMode == mode)
        return;

    sortMode = mode;
    emit sortParametersChanged();
}

void QVFileEnumerator::setSortDescending(const bool descending)
{
    if (sortDescending == descending)
        return;

    sortDescending = descending;
    emit sortParametersChanged();
}

void QVFileEnumerator::loadSettings(const bool isInitialLoad)
{
    const auto &settingsManager = qvApp->getSettingsManager();

    //loop folders
    isLoopFoldersEnabled = settingsManager.getBoolean("loopfoldersenabled");

    if (isInitialLoad || globalSortMode != settingsManager.getEnum<Qv::SortMode>("sortmode") || globalSortDescending != settingsManager.getBoolean("sortdescending"))
    {
        //sort mode
        globalSortMode = settingsManager.getEnum<Qv::SortMode>("sortmode");
        setSortMode(globalSortMode);

        //sort ascending
        globalSortDescending = settingsManager.getBoolean("sortdescending");
        setSortDescending(globalSortDescending);
    }

    //allow mime content detection
    allowMimeContentDetection = settingsManager.getBoolean("allowmimecontentdetection");

    //skip hidden files
    skipHiddenFiles = settingsManager.getBoolean("skiphidden");
}
