#ifndef QVINFODIALOG_H
#define QVINFODIALOG_H

#include <QDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>

namespace Ui {
class QVInfoDialog;
}

class QVInfoDialog : public QDialog
{
    Q_OBJECT

public:
    explicit QVInfoDialog(QWidget *parent = nullptr);
    ~QVInfoDialog();

    void setInfo(const QFileInfo fileInfo, const QSize imageSize, const int frameCount);

    void updateInfo();

    static QString formatModifiedDateTime(const QDateTime &dateTime,
                                          const QString &languageCode);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Ui::QVInfoDialog *ui;

    QFileInfo fileInfo;
    QSize imageSize;
    int frameCount {0};

public:
    static QString formatBytes(qint64 bytes)
    {
        QLocale locale;
        return locale.formattedDataSize(bytes);
    }

protected:
    void showEvent(QShowEvent *event) override;
};

#endif // QVINFODIALOG_H
