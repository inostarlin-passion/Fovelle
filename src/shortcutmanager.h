#ifndef SHORTCUTMANAGER_H
#define SHORTCUTMANAGER_H

#include <QObject>
#include <QKeySequence>

class ShortcutManager : public QObject
{
    Q_OBJECT
public:
    struct SShortcut {
        QString readableName;
        QString name;
        QStringList defaultShortcuts;
        QStringList shortcuts;
    };

    static QStringList keyBindingsToStringList(QKeySequence::StandardKey sequence)
    {
        const auto seqList = QKeySequence::keyBindings(sequence);
        QStringList strings;
        // QKeySequence::keyBindings() also returns a symbolic fallback such
        // as Qt::Key_Open. It is useful to Qt's standard-key machinery, but
        // it makes the settings table display the action name as if it were a
        // second shortcut. Keep the platform's primary binding only.
        if (!seqList.isEmpty())
            strings << seqList.constFirst().toString(QKeySequence::PortableText);
        return strings;
    }

    static QList<QKeySequence> stringListToKeySequenceList(const QStringList &stringList)
    {
        QList<QKeySequence> keySequences;
        for (const auto &string : stringList)
        {
            keySequences << QKeySequence::fromString(string);
        }
        return keySequences;
    }

    // Bare Escape is a window-lifecycle command: it exits full screen or
    // closes the current window. Configurable actions must not enter Qt's
    // shortcut map with the same prefix, where dispatch would be ambiguous.
    static bool beginsWithReservedEscape(const QKeySequence &sequence)
    {
        if (sequence.isEmpty())
            return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        return sequence[0].toCombined() == Qt::Key_Escape;
#else
        return sequence[0] == Qt::Key_Escape;
#endif
    }

    static QStringList withoutReservedEscape(const QStringList &shortcuts)
    {
        QStringList result;
        for (const QString &shortcut : shortcuts)
        {
            if (!beginsWithReservedEscape(QKeySequence::fromString(shortcut)))
                result.append(shortcut);
        }
        return result;
    }

    static QString stringListToReadableString(const QStringList &stringList)
    {
        QStringList readableStrings;
        for (const QString &shortcut : stringList)
        {
            const QKeySequence sequence =
                QKeySequence::fromString(shortcut, QKeySequence::PortableText);
            if (!sequence.isEmpty())
                readableStrings << sequence.toString(QKeySequence::NativeText);
        }
        return readableStrings.join(QStringLiteral(", "));
    }

    static QStringList readableStringToStringList(QString shortcutString)
    {
        return QKeySequence::fromString(shortcutString, QKeySequence::NativeText).toString().split(", ");
    }

    explicit ShortcutManager(QObject *parent = nullptr);

    void updateShortcuts();

    void hideShortcuts();

    void setShortcutHidden(const QString &shortcut);

    void setShortcutsHidden(const QStringList &shortcuts);

    const QList<SShortcut> &getShortcutsList() const { return shortcutsList; }

signals:
    void shortcutsUpdated();

protected:
    void initializeShortcutsList();

private:
    QList<SShortcut> shortcutsList;

    QStringList hiddenShortcuts;
};

#endif // SHORTCUTMANAGER_H
