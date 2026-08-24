#include "nativedialogs.h"

#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QInputDialog>
#include <QTimer>

namespace NativeDialogs
{
Qv::Theme currentTheme()
{
    return qvApp ? qvApp->getSettingsManager().getEnum<Qv::Theme>("theme")
                 : Qv::Theme::Light;
}

void applyTheme(QWidget *dialog)
{
    if (!dialog)
        return;

    const Qv::Theme theme = currentTheme();
    const auto apply = [dialog, theme]() {
        if (dialog->windowHandle())
            QVCocoaFunctions::setWindowTheme(theme, dialog->windowHandle());
    };

    // Calling winId() makes the operation deterministic for dialogs that are
    // about to be executed synchronously, while the queued pass covers native
    // panels whose NSWindow is created by Cocoa during show().
    dialog->winId();
    apply();
    QTimer::singleShot(0, dialog, apply);
}

QMessageBox *createMessageBox(const QMessageBox::Icon icon,
                              const QString &title,
                              const QString &text,
                              const QMessageBox::StandardButtons buttons,
                              QWidget *parent)
{
    auto *messageBox = new QMessageBox(icon, title, text, buttons, parent);
    messageBox->setWindowModality(Qt::ApplicationModal);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    applyTheme(messageBox);
    return messageBox;
}

void showMessage(const QMessageBox::Icon icon,
                 const QString &title,
                 const QString &text,
                 const QMessageBox::StandardButtons buttons,
                 QWidget *parent)
{
    auto *messageBox = createMessageBox(icon, title, text, buttons, parent);
    messageBox->open();
    applyTheme(messageBox);
}

double getDouble(QWidget *parent,
                 const QString &title,
                 const QString &label,
                 const double value,
                 const double minimum,
                 const double maximum,
                 const int decimals,
                 bool *ok)
{
    QInputDialog dialog(parent);
    dialog.setWindowTitle(title);
    dialog.setLabelText(label);
    dialog.setInputMode(QInputDialog::DoubleInput);
    dialog.setDoubleValue(value);
    dialog.setDoubleMinimum(minimum);
    dialog.setDoubleMaximum(maximum);
    dialog.setDoubleDecimals(decimals);
    applyTheme(&dialog);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return dialog.doubleValue();
}
}
