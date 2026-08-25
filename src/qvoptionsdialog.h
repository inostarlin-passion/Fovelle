#ifndef QVOPTIONSDIALOG_H
#define QVOPTIONSDIALOG_H

#include "qvnamespace.h"

#include <QDialog>
#include <QCheckBox>
#include <QRadioButton>
#include <QComboBox>
#include <QSpinBox>

namespace Ui {
class QVOptionsDialog;

template <typename TEnum>
using ComboBoxItems = QVector<std::pair<TEnum, QString>>;
}

class QVOptionsDialog : public QDialog
{
    Q_OBJECT

public:

    explicit QVOptionsDialog(QWidget *parent = nullptr);
    ~QVOptionsDialog() override;

    // Create and finish configuring the hidden native Settings window so its
    // final frame can be positioned before the first visible presentation.
    void prepareForDisplay();

protected:
    void done(int r) override;

    void showEvent(QShowEvent *event) override;

    void changeEvent(QEvent *event) override;

    void modifySetting(QString key, QVariant value);
    void syncSettings(bool defaults = false, bool makeConnections = false);
    void syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncRadioButtons(QList<QRadioButton*> buttons, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncComboBox(QComboBox *comboBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncShortcuts(bool defaults = false);
    void updateShortcutsTable();
    void configureGeneralPage();
    void resizeForCategory(int categoryIndex);
    void populateCategories(int selectedRow);
    void populateLanguages();
    void populateComboBoxes();

    const Ui::ComboBoxItems<Qv::AfterDelete> mapAfterDelete();
    const Ui::ComboBoxItems<Qv::SlideshowDirection> mapSlideshowDirection();
    const Ui::ComboBoxItems<Qv::SmoothScalingMode> mapSmoothScalingMode();
    const Ui::ComboBoxItems<Qv::Theme> mapTheme();
    const Ui::ComboBoxItems<Qv::UpdateCheckFrequency> mapUpdateCheckFrequency();
    const Ui::ComboBoxItems<Qv::ViewportClickAction> mapViewportClickAction();
    const Ui::ComboBoxItems<Qv::ViewportDragAction> mapViewportDragAction();
    const Ui::ComboBoxItems<Qv::ViewportScrollAction> mapViewportScrollAction();

private slots:
    void shortcutCellDoubleClicked(int row, int column);

    void cursorAutoHideFullscreenCheckboxCheckStateChanged(Qt::CheckState state);

    void languageComboBoxCurrentIndexChanged(int index);

    void associateSupportedFormats();

    void middleButtonModeChanged();

private:
    static constexpr int SettingsDialogWidth = 600;
    static constexpr int ShortcutsVisibleRows = 16;

    Ui::QVOptionsDialog *ui;

    QList<QStringList> transientShortcuts;

    bool isInitialLoad {true};

    bool languageRestartMessageShown {false};

    bool displayPrepared {false};
};

#endif // QVOPTIONSDIALOG_H
