#ifndef QVOPTIONSDIALOG_H
#define QVOPTIONSDIALOG_H

#include "qvnamespace.h"

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QSize>
#include <QSpinBox>

namespace Ui {
class QVOptionsDialog;

template <typename TEnum>
using ComboBoxItems = QVector<std::pair<TEnum, QString>>;
}

class QPropertyAnimation;

class QVOptionsDialog : public QDialog
{
    Q_OBJECT
    Q_PROPERTY(QSize settingsAnimatedSize READ settingsAnimatedSize WRITE setSettingsAnimatedSize)

public:

    explicit QVOptionsDialog(QWidget *parent = nullptr);
    ~QVOptionsDialog() override;

    QSize settingsAnimatedSize() const;
    void setSettingsAnimatedSize(const QSize &size);

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
    void syncComboBox(QComboBox *comboBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key, bool defaults = false, bool makeConnection = false);
    void syncShortcuts(bool defaults = false);
    void updateShortcutsTable();
    void configureGeneralPage();
    void updateNaturalPageSizes();
    void resizeForCategory(int categoryIndex);
    void finishCategoryTransition();
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

    void languageComboBoxCurrentIndexChanged(int index);

    void associateSupportedFormats();

private:
    static constexpr int ShortcutsVisibleRows = 16;
    static constexpr int SettingsCategoryTransitionDuration = 180;

    Ui::QVOptionsDialog *ui;

    QList<QStringList> transientShortcuts;

    bool isInitialLoad {true};

    bool languageRestartMessageShown {false};

    bool displayPrepared {false};

    bool pageMetricsReady {false};

    QPropertyAnimation *categorySizeAnimation {nullptr};
    int settingsDialogWidth {0};
    int categoryTargetHeight {0};
};

#endif // QVOPTIONSDIALOG_H
