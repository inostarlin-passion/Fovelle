#include "qvrenamedialog.h"

#include <QDir>
#include <QMessageBox>
#include "nativedialogs.h"

QVRenameDialog::QVRenameDialog(QWidget *parent, QFileInfo fileInfo) :
    QInputDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    NativeDialogs::applyTheme(this);

    this->fileInfo = fileInfo;

    setWindowTitle(tr("Rename..."));
    setLabelText(tr("File name:"));
    setTextValue(fileInfo.fileName());
    resize(350, height());

    connect(this, &QInputDialog::finished, this, &QVRenameDialog::onFinished);
}

void QVRenameDialog::onFinished(int result)
{
    if (!fileInfo.isWritable())
    {
        NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"),
                                   tr("Could not rename %1:\nNo write permission or file is read-only.").arg(fileInfo.fileName()),
                                   QMessageBox::Ok, this);
        return;
    }

    if (result)
    {
        const auto newFileName = textValue();
        const auto newFilePath = QDir::cleanPath(fileInfo.absolutePath() + QDir::separator() + newFileName);

        emit readyToRenameFile();

        if (fileInfo.absoluteFilePath() != newFilePath)
        {
            if (QFile::rename(fileInfo.absoluteFilePath(), newFilePath))
            {
                emit newFileToOpen(newFilePath);
            }
            else
            {
                NativeDialogs::showMessage(QMessageBox::Critical, tr("Error"),
                                           tr("Could not rename %1:\n(Check that all characters are valid)").arg(fileInfo.fileName()),
                                           QMessageBox::Ok, this);
            }
        }
    }
}

void QVRenameDialog::showEvent(QShowEvent *event)
{
    QInputDialog::showEvent(event);
    NativeDialogs::applyTheme(this);

    QLineEdit *lineEdit = findChild<QLineEdit*>();
    const auto &lastDot = lineEdit->text().lastIndexOf(".");
    if (lastDot != -1)
        lineEdit->setSelection(0, lastDot);
}
