#include "qvshortcutdialog.h"
#include "ui_qvshortcutdialog.h"
#include "qvapplication.h"
#include "nativedialogs.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMessageBox>

#include <QDebug>

QVShortcutDialog::QVShortcutDialog(int index, GetTransientShortcutCallback getTransientShortcutCallback, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVShortcutDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    NativeDialogs::applyTheme(this);

    // QKeySequenceEdit treats Escape as a recordable key and accepts the
    // event before QDialog can apply its usual reject-on-Escape behavior.
    // Observe the application event stream so this also covers the editor's
    // internal line edit, then use the exact same reject path as Cancel.
    if (auto *application = QApplication::instance())
        application->installEventFilter(this);

    connect(ui->buttonBox, &QDialogButtonBox::clicked, this, &QVShortcutDialog::buttonBoxClicked);

    shortcutObject = qvApp->getShortcutManager().getShortcutsList().value(index);
    this->index = index;
    this->getTransientShortcutCallback = getTransientShortcutCallback;
    ui->keySequenceEdit->setKeySequence(getTransientShortcutCallback(index).join(", "));
    ui->keySequenceEdit->setClearButtonEnabled(true);
}

QVShortcutDialog::~QVShortcutDialog()
{
    if (auto *application = QApplication::instance())
        application->removeEventFilter(this);
    delete ui;
}

bool QVShortcutDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress && isVisible())
    {
        auto *widget = qobject_cast<QWidget *>(watched);
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (widget && widget->window() == this
            && keyEvent->key() == Qt::Key_Escape
            && keyEvent->modifiers() == Qt::NoModifier)
        {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QVShortcutDialog::done(int r)
{
    if (r == QDialog::Accepted)
    {
        return;
    }

    QDialog::done(r);
}

void QVShortcutDialog::buttonBoxClicked(QAbstractButton *button)
{
    if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::AcceptRole)
    {
        QStringList shortcutsStringList = ui->keySequenceEdit->keySequence().toString().split(", ");
        const auto sequenceList = ShortcutManager::stringListToKeySequenceList(shortcutsStringList);

        for (const auto &sequence : sequenceList)
        {
            auto conflictingShortcut = shortcutAlreadyBound(sequence, shortcutObject.name);
            if (!conflictingShortcut.isEmpty())
            {
                QString nativeShortcutString = sequence.toString(QKeySequence::NativeText);
                NativeDialogs::showMessage(QMessageBox::Warning, tr("Shortcut Already Used"),
                                           tr("\"%1\" is already bound to \"%2\"").arg(nativeShortcutString, conflictingShortcut),
                                           QMessageBox::Ok, this);
                return;
            }
        }

        acceptValidated();

        emit shortcutsListChanged(index, shortcutsStringList);
    }
    else if (ui->buttonBox->buttonRole(button) == QDialogButtonBox::ResetRole)
    {
        ui->keySequenceEdit->setKeySequence(shortcutObject.defaultShortcuts.join(", "));
    }
}

QString QVShortcutDialog::shortcutAlreadyBound(const QKeySequence &chosenSequence, const QString &exemptShortcut)
{
    if (chosenSequence.isEmpty())
        return "";

    if (ShortcutManager::beginsWithReservedEscape(chosenSequence))
        return QCoreApplication::translate("ShortcutManager", "Close Window");

    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    for (int i = 0; i < shortcutsList.length(); i++)
    {
        const auto &shortcut = shortcutsList.value(i);
        const auto sequenceList = ShortcutManager::stringListToKeySequenceList(getTransientShortcutCallback(i));

        if (sequenceList.contains(chosenSequence) && shortcut.name != exemptShortcut)
            return shortcut.readableName;
    }
    return "";
}

void QVShortcutDialog::acceptValidated()
{
    QDialog::done(1);
}
