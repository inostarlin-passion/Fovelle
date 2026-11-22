#ifndef FILEVERSION_H
#define FILEVERSION_H

#include <QFile>
#include <sys/stat.h>

namespace QvFile {
enum class LinkMode { Follow, DirectoryEntry };

inline QByteArray version(const QString &path, LinkMode links = LinkMode::Follow)
{
    struct stat info;
    const auto encoded = QFile::encodeName(path);
    const int status = links == LinkMode::Follow ? ::stat(encoded.constData(), &info)
                                                 : ::lstat(encoded.constData(), &info);
    if (status != 0 || (links == LinkMode::Follow && !S_ISREG(info.st_mode)))
        return { };

    return QByteArray::number(qulonglong(info.st_dev)) + ':'
            + QByteArray::number(qulonglong(info.st_ino)) + ':'
            + QByteArray::number(qlonglong(info.st_size)) + ':'
#ifdef Q_OS_MACOS
            + QByteArray::number(qlonglong(info.st_mtimespec.tv_sec)) + ':'
            + QByteArray::number(qlonglong(info.st_mtimespec.tv_nsec)) + ':'
            + QByteArray::number(qlonglong(info.st_ctimespec.tv_sec)) + ':'
            + QByteArray::number(qlonglong(info.st_ctimespec.tv_nsec));
#else
            + QByteArray::number(qlonglong(info.st_mtime)) + ':'
            + QByteArray::number(qlonglong(info.st_ctime));
#endif
}
} // namespace QvFile

#endif // FILEVERSION_H
