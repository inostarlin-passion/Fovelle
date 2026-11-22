#include "qvrenamedialog.h"
#include "fileversion.h"

#include <QDir>
#include <QMessageBox>
#include "nativedialogs.h"

QVRenameDialog::QVRenameDialog(QWidget *parent, QFileInfo source)
    : QInputDialog(parent),
      fileInfo(std::move(source)),
      originalVersion(
              QvFile::version(fileInfo.absoluteFilePath(), QvFile::LinkMode::DirectoryEntry))
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    NativeDialogs::applyTheme(this);

    setWindowTitle(tr("Rename..."));
    setLabelText(tr("File name:"));
    setTextValue(fileInfo.fileName());
    resize(350, height());

    connect(this, &QInputDialog::finished, this, &QVRenameDialog::onFinished);
}

void QVRenameDialog::onFinished(int result)
{
    if (result != QDialog::Accepted)
        return;

    const auto source = fileInfo.absoluteFilePath();
    const auto destination =
            QDir::cleanPath(fileInfo.absolutePath() + QDir::separator() + textValue());
    if (source == destination)
        return;

    const auto sameEntry = [&] {
        if (!originalVersion.isEmpty()
            && originalVersion == QvFile::version(source, QvFile::LinkMode::DirectoryEntry))
            return true;
        NativeDialogs::showMessage(QMessageBox::Critical,
                                   tr("Could not rename %1 because the original file changed or is "
                                      "no longer available.")
                                           .arg(fileInfo.fileName()),
                                   { }, QMessageBox::Ok, parentWidget());
        return false;
    };
    if (!sameEntry())
        return;
    if (!QFileInfo(source).isWritable()) {
        NativeDialogs::showMessage(
                QMessageBox::Critical,
                tr("Could not rename %1:\nNo write permission or file is read-only.")
                        .arg(fileInfo.fileName()),
                { }, QMessageBox::Ok, parentWidget());
        return;
    }

    emit readyToRenameFile();
    if (!sameEntry())
        return;
    if (QFile::rename(source, destination)) {
        emit newFileToOpen(destination);
    } else {
        NativeDialogs::showMessage(QMessageBox::Critical,
                                   tr("Could not rename %1:\n(Check that all characters are valid)")
                                           .arg(fileInfo.fileName()),
                                   { }, QMessageBox::Ok, parentWidget());
    }
}

void QVRenameDialog::showEvent(QShowEvent *event)
{
    QInputDialog::showEvent(event);
    NativeDialogs::applyTheme(this);

    QLineEdit *lineEdit = findChild<QLineEdit *>();
    const auto &lastDot = lineEdit->text().lastIndexOf(".");
    if (lastDot != -1)
        lineEdit->setSelection(0, lastDot);
}
