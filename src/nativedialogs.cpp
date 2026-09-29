#include "nativedialogs.h"

#include "qvapplication.h"
#include "qvcocoafunctions.h"

#include <QInputDialog>
#include <QTextDocument>
#include <QTimer>

namespace
{
QString plainAlertText(const QString &text)
{
    if (!Qt::mightBeRichText(text))
        return text;

    // NSAlert accepts plain strings, not Qt rich text. Convert rich input at
    // the shared boundary so Qt's Cocoa helper never falls back to a QWidget
    // dialog for this reason.
    QTextDocument document;
    document.setHtml(text);
    return document.toPlainText();
}
}

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

    const auto apply = [dialog]() {
        if (dialog->windowHandle())
            QVCocoaFunctions::setWindowTheme(currentTheme(), dialog->windowHandle());
    };

    // Do not force winId() before QDialog::open()/exec(): on Cocoa that can
    // create and expose an NSWindow before Qt begins its own visibility
    // transition.  The application scheme already supplies the correct Qt
    // palette; these passes only pin the native window once it exists.
    if (dialog->windowHandle())
        apply();
    QTimer::singleShot(0, dialog, apply);
}

QMessageBox *createMessageBox(const QMessageBox::Icon severity,
                              const QString &messageText,
                              const QString &informativeText,
                              const QMessageBox::StandardButtons buttons,
                              QWidget *parent)
{
    auto *messageBox = new QMessageBox(parent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    // Keep Qt's Cocoa native-message-dialog path enabled. Set this before
    // assigning message content, as required by QMessageBox::setOption().
    messageBox->setOption(QMessageBox::Option::DontUseNativeDialog, false);
#endif
    messageBox->setIcon(severity);
    messageBox->setText(plainAlertText(messageText));
    messageBox->setInformativeText(plainAlertText(informativeText));
    messageBox->setTextFormat(Qt::PlainText);
    messageBox->setStandardButtons(buttons);
    // Keep every alert on QCocoaMessageDialog's NSAlert path. In particular,
    // Cocoa falls back to QWidget for window-modal alerts on macOS Tahoe.
    // App-modal NSAlert inherits NSApp's current Appearance and system layout.
    messageBox->setWindowModality(Qt::ApplicationModal);
    messageBox->setAttribute(Qt::WA_DeleteOnClose);
    return messageBox;
}

void showMessage(const QMessageBox::Icon severity,
                 const QString &messageText,
                 const QString &informativeText,
                 const QMessageBox::StandardButtons buttons,
                 QWidget *parent)
{
    auto *messageBox = createMessageBox(severity, messageText, informativeText, buttons, parent);
    // open() changes QMessageBox to window-modal on macOS. show() preserves
    // the explicitly selected application modality and still returns
    // immediately while AppKit runs the native modal alert.
    messageBox->show();
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
    dialog.setDoubleDecimals(decimals);
    dialog.setDoubleMinimum(minimum);
    dialog.setDoubleMaximum(maximum);
    // QDoubleSpinBox clamps its value to the current range. Install the
    // caller's range before the initial value so values above the spin box's
    // default 99.99 maximum are not silently reduced before the wider range
    // takes effect.
    dialog.setDoubleValue(value);
    applyTheme(&dialog);
    const bool accepted = dialog.exec() == QDialog::Accepted;
    if (ok)
        *ok = accepted;
    return dialog.doubleValue();
}
}
