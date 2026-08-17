#include "mainwindow.h"
#include "openwith.h"
#include "qvcocoafunctions.h"

#include <QCollator>
#include <QDir>
#include <QFileDialog>
#include <QProcess>

const QList<OpenWith::OpenWithItem> OpenWith::getOpenWithItems(const QString &filePath)
{

    QList<OpenWithItem> listOfOpenWithItems;
    if (!filePath.isNull() && !QFileInfo::exists(filePath))
        return listOfOpenWithItems;

    listOfOpenWithItems = QVCocoaFunctions::getOpenWithItems(filePath);

    // Natural/alphabetic sort
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(
            listOfOpenWithItems.begin(), listOfOpenWithItems.end(),
            [&collator](const OpenWith::OpenWithItem &item0, const OpenWith::OpenWithItem &item1) {
                return collator.compare(item0.name, item1.name) < 0;
            });

    // Move default item to beginning
    for (int i = 0; i < listOfOpenWithItems.length(); i++) {
        const auto &item = listOfOpenWithItems.at(i);
        if (item.isDefault)
            listOfOpenWithItems.move(i, 0);
    }

    return listOfOpenWithItems;
}

void OpenWith::showOpenWithDialog(QWidget *parent)
{
    auto mainWindow = reinterpret_cast<MainWindow *>(parent);
    QString filePath = mainWindow->getCurrentFileDetails().fileInfo.absoluteFilePath();
    auto openWithDialog = new QFileDialog(parent);
    openWithDialog->setNameFilters({ QT_TR_NOOP("All Applications (*.app)") });
    openWithDialog->setDirectory("/Applications");
    openWithDialog->open();
    connect(openWithDialog, &QFileDialog::fileSelected, [filePath](const QString &executablePath) {
        openWithExecutable("open", { "-a", executablePath }, filePath);
    });
}

void OpenWith::openWithExecutable(const QString &executablePath, const QString &filePath)
{
    OpenWithItem item;
    item.exec = executablePath;
    openWith(filePath, item);
}

void OpenWith::openWithExecutable(const QString &executablePath, const QStringList &args,
                                  const QString &filePath)
{
    OpenWithItem item;
    item.exec = executablePath;
    item.args = args;
    openWith(filePath, item);
}

void OpenWith::openWith(const QString &filePath, const OpenWithItem &openWithItem)
{
    const QString &nativeFilePath = QDir::toNativeSeparators(filePath);
    const QString &exec = openWithItem.exec.trimmed();
    QStringList args = openWithItem.args;

    if (exec.isEmpty() || exec.isNull())
        return;

    args.append(nativeFilePath);
    QProcess::startDetached(exec, args);
}
