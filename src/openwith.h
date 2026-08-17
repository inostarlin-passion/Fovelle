#ifndef OPENWITH_H
#define OPENWITH_H

#include <QIcon>

class OpenWith : public QObject
{
    Q_OBJECT
public:
    struct OpenWithItem {
        QIcon icon;
        QString iconName;
        QString name;
        QString exec;
        QStringList args;
        bool isDefault = false;
    };

    static QList<OpenWithItem> getOpenWithItems(const QString &filePath);

    static void showOpenWithDialog(QWidget *parent);

    static void openWithExecutable(const QString &executablePath, const QString &filePath);

    static void openWithExecutable(const QString &executablePath, const QStringList &args, const QString &filePath);

    static void openWith(const QString &filePath, const OpenWithItem &openWithItem);

};
Q_DECLARE_METATYPE(OpenWith::OpenWithItem);

#endif // OPENWITH_H
