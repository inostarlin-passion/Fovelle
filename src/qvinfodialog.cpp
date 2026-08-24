#include "qvinfodialog.h"
#include "ui_qvinfodialog.h"
#include "qvapplication.h"
#include "nativedialogs.h"
#include <QDateTime>
#include <QMimeDatabase>
#include <QTimer>
#include <QShowEvent>

static int getGcd (int a, int b) {
    return (b == 0) ? a : getGcd(b, a % b);
}

QVInfoDialog::QVInfoDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVInfoDialog)
{
    ui->setupUi(this);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setFixedSize(0, 0);
}

QVInfoDialog::~QVInfoDialog()
{
    delete ui;
}

void QVInfoDialog::showEvent(QShowEvent *event)
{
    NativeDialogs::applyTheme(this);
    QDialog::showEvent(event);
}

void QVInfoDialog::setInfo(const QFileInfo fileInfo, const QSize imageSize, const int frameCount)
{
    this->fileInfo = fileInfo;
    this->imageSize = imageSize;
    this->frameCount = frameCount;

    // If the dialog is visible, defer updateInfo through the event queue so the font is ready before
    // first use and the main window can repaint first, giving the appearance of better
    // responsiveness. If the dialog is not visible, however, it means we're preparing to display for an
    // image already opened. In this case there is no urgency to repaint the main window, and we need to
    // process the updates here synchronously to avoid the caller showing the dialog before it's ready
    // (i.e. to avoid showing outdated info or placeholder text).
    if (isVisible())
        QTimer::singleShot(0, this, &QVInfoDialog::updateInfo);
    else
        updateInfo();
}

void QVInfoDialog::updateInfo()
{
    const QLocale locale = QLocale::system();
    const QMimeDatabase mimeDb;
    const QMimeType mime = mimeDb.mimeTypeForFile(fileInfo.absoluteFilePath(), QMimeDatabase::MatchContent);
    const int width = imageSize.width();
    const int height = imageSize.height();
    const qreal megapixels = (width * height) / 1000000.0;
    const int gcd = getGcd(width, height);
    ui->nameLabel->setText(fileInfo.fileName());
    ui->typeLabel->setText(mime.name());
    ui->locationLabel->setText(fileInfo.path());
    ui->sizeLabel->setText(tr("%1 (%2 bytes)").arg(formatBytes(fileInfo.size()), locale.toString(fileInfo.size())));
    ui->modifiedLabel->setText(fileInfo.lastModified().toString(locale.dateTimeFormat()));
    ui->dimensionsLabel->setText(tr("%1 x %2 (%3 MP)").arg(QString::number(width), QString::number(height), QString::number(megapixels, 'f', 1)));
    if (gcd != 0)
        ui->ratioLabel->setText(QString::number(width / gcd) + ":" + QString::number(height / gcd));
    if (frameCount != 0)
    {
        ui->framesLabel2->show();
        ui->framesLabel->show();
        ui->framesLabel->setText(QString::number(frameCount));
    }
    else
    {
        ui->framesLabel2->hide();
        ui->framesLabel->hide();
    }
    window()->adjustSize();
}

void QVInfoDialog::keyPressEvent(QKeyEvent *event)
{
    if (qvApp->getActionManager().wouldTriggerAction(event, "showfileinfo"))
    {
        close();
        return;
    }

    QDialog::keyPressEvent(event);
}
