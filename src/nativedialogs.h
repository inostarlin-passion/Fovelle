#ifndef FOVELLE_NATIVEDIALOGS_H
#define FOVELLE_NATIVEDIALOGS_H

#include "qvnamespace.h"

#include <QMessageBox>

class QWidget;

namespace NativeDialogs
{
    // The setting is deliberately read at the point a dialog is created. This
    // keeps dialogs opened after a live settings change in the same appearance
    // as the main window without introducing a second theme store.
    Qv::Theme currentTheme();

    // Apply the selected AppKit appearance to a Qt dialog. The native window
    // may not exist until show()/open()/exec(), so the helper applies once now
    // and once on the next event-loop turn.
    void applyTheme(QWidget *dialog);

    QMessageBox *createMessageBox(QMessageBox::Icon icon,
                                  const QString &title,
                                  const QString &text,
                                  QMessageBox::StandardButtons buttons,
                                  QWidget *parent = nullptr);

    void showMessage(QMessageBox::Icon icon,
                     const QString &title,
                     const QString &text,
                     QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                     QWidget *parent = nullptr);

    double getDouble(QWidget *parent,
                     const QString &title,
                     const QString &label,
                     double value,
                     double minimum,
                     double maximum,
                     int decimals,
                     bool *ok);
}

#endif // FOVELLE_NATIVEDIALOGS_H
