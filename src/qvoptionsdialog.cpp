#include "qvoptionsdialog.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include "qvshortcutdialog.h"
#include "qvcocoafunctions.h"
#include "nativedialogs.h"

#include <QMessageBox>
#include <QSettings>
#include <QWindow>
#include <QSignalBlocker>
#include <QFormLayout>
#include <QTabBar>
#include <QHBoxLayout>
#include <QPushButton>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    // The association action is a full-width action, not a form-field value.
    // Put it in a spanning row so its center is the center of the page rather
    // than the center of the form's right-hand field column.
    ui->miscLayout->removeWidget(ui->associateFormatsButton);
    auto *associationRow = new QHBoxLayout;
    associationRow->setContentsMargins(0, 0, 0, 0);
    associationRow->addStretch();
    associationRow->addWidget(ui->associateFormatsButton);
    associationRow->addStretch();
    ui->miscLayout->setLayout(16, QFormLayout::SpanningRole, associationRow);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    resize(650, 550);
    ui->categoryTabs->setShape(QTabBar::RoundedNorth);
    // QTabBar remains the lightweight Qt page model used by tests and the
    // stacked widget connection.  AppKit supplies the visible Settings
    // toolbar once the native window exists.
    ui->categoryTabs->hide();

    connect(ui->categoryTabs, &QTabBar::currentChanged, this, [this](int currentIndex) {
        ui->stackedWidget->setCurrentIndex(currentIndex);
    });
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this, &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->cursorAutoHideFullscreenCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::cursorAutoHideFullscreenCheckboxCheckStateChanged);
    connect(ui->middleButtonModeClickRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->middleButtonModeDragRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->associateFormatsButton, &QPushButton::clicked, this, &QVOptionsDialog::associateSupportedFormats);

    QSettings settings;

    // The former Window and Image tabs occupied indexes 0 and 1. Map an old
    // saved selection once so the merged Display tab is the first page while
    // later selections retain their new four-tab indexes.
    int selectedTab = settings.value("optionstab", 0).toInt();
    if (settings.value("optionstabversion", 0).toInt() < 1)
    {
        selectedTab = selectedTab <= 1 ? 0 : selectedTab - 1;
        settings.setValue("optionstab", selectedTab);
        settings.setValue("optionstabversion", 1);
        settings.sync();
    }
    populateCategories(selectedTab);
    populateComboBoxes();
    populateLanguages();

    restoreGeometry(settings.value("optionsgeometry").toByteArray());

    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
        setWindowTitle(tr("Preferences"));

    ui->menubarCheckbox->hide();

    QString ctrlKeyName = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText).replace(QRegularExpression("\\+$"), "");
    ui->altDoubleClickLabel->setText(tr("%1 + Double Click:").arg(ctrlKeyName));
    ui->altDragLabel->setText(tr("%1 + Drag:").arg(ctrlKeyName));
    ui->altMiddleClickLabel->setText(tr("%1 + Middle Click:").arg(ctrlKeyName));
    ui->altMiddleDragLabel->setText(tr("%1 + Middle Drag:").arg(ctrlKeyName));
    ui->altVerticalScrollLabel->setText(tr("%1 + Vertical Scroll:").arg(ctrlKeyName));
    ui->altHorizontalScrollLabel->setText(tr("%1 + Horizontal Scroll:").arg(ctrlKeyName));

    syncSettings(false, true);
    syncShortcuts();

    connect(qvApp, &QVApplication::windowOnTopChanged, this, [this]() {
        if (windowHandle())
            windowHandle()->setFlag(Qt::WindowStaysOnTopHint,
                                    qvApp->foundOnTopWindow());
    });

    isInitialLoad = false;
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
}

void QVOptionsDialog::prepareForDisplay()
{
    ensurePolished();
    // QWidget::winId() creates the native NSWindow without ordering it front.
    // The toolbar therefore contributes to frameGeometry before centering.
    (void)winId();
    if (windowHandle())
        windowHandle()->setFlag(Qt::WindowStaysOnTopHint,
                                qvApp->foundOnTopWindow());
    NativeDialogs::applyTheme(this);
    QVCocoaFunctions::configureSettingsToolbar(windowHandle(), ui->categoryTabs);
}

void QVOptionsDialog::done(int r)
{
    // Save window geometry
    QSettings settings;
    settings.setValue("optionsgeometry", saveGeometry());
    settings.setValue("optionstab", ui->categoryTabs->currentIndex());

    QDialog::done(r);
}

void QVOptionsDialog::showEvent(QShowEvent *event)
{
    // Direct callers still receive the native toolbar and application theme;
    // QVApplication invokes this before positioning to prevent visible moves.
    prepareForDisplay();
    QDialog::showEvent(event);
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    // Native toolbar symbols and labels resolve against effectiveAppearance
    // automatically.  Rebuilding the page model on PaletteChange used to
    // cause a transient pane/title jump while switching themes.
    QDialog::changeEvent(event);
}

void QVOptionsDialog::modifySetting(QString key, QVariant value)
{
    QSettings settings;
    settings.beginGroup("options");
    settings.setValue(key, value);
    settings.endGroup();
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
    if (key == QStringLiteral("theme"))
        NativeDialogs::applyTheme(this);
}

void QVOptionsDialog::syncSettings(bool defaults, bool makeConnections)
{
    auto &settingsManager = qvApp->getSettingsManager();
    settingsManager.loadSettings();

    // theme
    syncComboBox(ui->themeComboBox, "theme", defaults, makeConnections);
    // checkerboardbackground
    syncCheckbox(ui->checkerboardBackgroundCheckbox, "checkerboardbackground", defaults, makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // reusewindow
    syncCheckbox(ui->reuseWindowCheckbox, "reusewindow", defaults, makeConnections);
    // smoothscalingmode
    syncComboBox(ui->smoothScalingComboBox, "smoothscalingmode", defaults, makeConnections);
    // smallimageoneone
    syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone", defaults, makeConnections);
    // language
    syncComboBox(ui->langComboBox, "language", defaults, makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, "sortmode", defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ui->descendingRadioButton0, ui->descendingRadioButton1}, "sortdescending", defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, "preloadingmode", defaults, makeConnections);
    // slideshowdirection
    syncComboBox(ui->slideshowDirectionComboBox, "slideshowdirection", defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, "slideshowtimer", defaults, makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, "afterdelete", defaults, makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, "askdelete", defaults, makeConnections);
    // updatecheckfrequency
    syncComboBox(ui->updateFrequencyComboBox, "updatecheckfrequency", defaults, makeConnections);

    // mouse actions
    syncCheckbox(ui->navigationRegionsCheckbox, "navigationregionsenabled", defaults, makeConnections);
    syncComboBox(ui->doubleClickComboBox, "viewportdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->altDoubleClickComboBox, "viewportaltdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->dragComboBox, "viewportdragaction", defaults, makeConnections);
    syncComboBox(ui->altDragComboBox, "viewportaltdragaction", defaults, makeConnections);
    syncRadioButtons({ui->middleButtonModeClickRadioButton, ui->middleButtonModeDragRadioButton}, "viewportmiddlebuttonmode", defaults, makeConnections);
    middleButtonModeChanged();
    syncComboBox(ui->middleClickComboBox, "viewportmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleClickComboBox, "viewportaltmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->middleDragComboBox, "viewportmiddledragaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleDragComboBox, "viewportaltmiddledragaction", defaults, makeConnections);
    syncComboBox(ui->verticalScrollComboBox, "viewportverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->horizontalScrollComboBox, "viewporthorizontalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altVerticalScrollComboBox, "viewportaltverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altHorizontalScrollComboBox, "viewportalthorizontalscrollaction", defaults, makeConnections);
    syncCheckbox(ui->scrollActionCooldownCheckbox, "scrollactioncooldown", defaults, makeConnections);
    syncCheckbox(ui->cursorAutoHideFullscreenCheckbox, "cursorautohidefullscreenenabled", defaults, makeConnections);
    cursorAutoHideFullscreenCheckboxCheckStateChanged(ui->cursorAutoHideFullscreenCheckbox->checkState());
    syncDoubleSpinBox(ui->cursorAutoHideFullscreenDelaySpinBox, "cursorautohidefullscreendelay", defaults, makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBoolean(key, defaults);
    const QSignalBlocker blocker(checkbox);
    checkbox->setChecked(val);

    if (makeConnection)
    {
        connect(checkbox, &QCheckBox::checkStateChanged, this, [this, key](Qt::CheckState state) {
            modifySetting(key, static_cast<bool>(state));
        });
    }
}

void QVOptionsDialog::syncRadioButtons(QList<QRadioButton *> buttons, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    const QSignalBlocker blocker(buttons.value(0));
    if (auto widget = buttons.value(val))
        widget->setChecked(true);

    if (makeConnection)
    {
        for (int i = 0; i < buttons.length(); i++)
        {
            connect(buttons.value(i), &QRadioButton::clicked, this, [this, key, i] {
                modifySetting(key, i);
            });
        }
    }
}

void QVOptionsDialog::syncComboBox(QComboBox *comboBox, const QString &key, bool defaults, bool makeConnection)
{
    const QVariant val = qvApp->getSettingsManager().getSetting(key, defaults);
    const QSignalBlocker blocker(comboBox);
    int index = comboBox->findData(val);
    if (index < 0)
        index = comboBox->findData(val.toInt());
    comboBox->setCurrentIndex(index);

    if (makeConnection)
    {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, key, comboBox](int index) {
            Q_UNUSED(index)
            modifySetting(key, comboBox->currentData());
        });
    }
}

void QVOptionsDialog::syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(val);

    if (makeConnection)
    {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, key](int i) {
            modifySetting(key, i);
        });
    }
}

void QVOptionsDialog::syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key, bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getDouble(key, defaults);
    const QSignalBlocker blocker(doubleSpinBox);
    doubleSpinBox->setValue(val);

    if (makeConnection)
    {
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, key](double d) {
            modifySetting(key, d);
        });
    }
}

void QVOptionsDialog::syncShortcuts(bool defaults)
{
    qvApp->getShortcutManager().updateShortcuts();

    transientShortcuts.clear();
    const auto &shortcutsList = qvApp->getShortcutManager().getShortcutsList();
    ui->shortcutsTable->setRowCount(shortcutsList.length());

    for (int i = 0; i < shortcutsList.length(); i++)
    {
        const ShortcutManager::SShortcut &shortcut = shortcutsList.value(i);

        // Add shortcut to transient shortcut list
        if (defaults)
            transientShortcuts.append(shortcut.defaultShortcuts);
        else
            transientShortcuts.append(shortcut.shortcuts);

        // Add shortcut to table widget
        auto *nameItem = new QTableWidgetItem();
        nameItem->setText(shortcut.readableName);
        ui->shortcutsTable->setItem(i, 0, nameItem);

        auto *shortcutsItem = new QTableWidgetItem();
        shortcutsItem->setText(ShortcutManager::stringListToReadableString(
                                   transientShortcuts.value(i)));
        ui->shortcutsTable->setItem(i, 1, shortcutsItem);
    }
    updateShortcutsTable();
}

void QVOptionsDialog::updateShortcutsTable()
{
    for (int i = 0; i < transientShortcuts.length(); i++)
    {
        const QStringList &shortcuts = transientShortcuts.value(i);
        ui->shortcutsTable->item(i, 1)->setText(ShortcutManager::stringListToReadableString(shortcuts));
    }
}

void QVOptionsDialog::shortcutCellDoubleClicked(int row, int column)
{
    Q_UNUSED(column)
    auto getTransientShortcutCallback = [this](int index) {
        return transientShortcuts.value(index);
    };
    auto *shortcutDialog = new QVShortcutDialog(row, getTransientShortcutCallback, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this, [this](int index, const QStringList &stringListShortcuts) {
        transientShortcuts.replace(index, stringListShortcuts);
        const auto &shortcut = qvApp->getShortcutManager().getShortcutsList().value(index);
        QSettings settings;
        settings.setValue(QStringLiteral("shortcuts/") + shortcut.name, stringListShortcuts);
        settings.sync();
        qvApp->getShortcutManager().updateShortcuts();
        updateShortcutsTable();
    });
    shortcutDialog->open();
}

void QVOptionsDialog::cursorAutoHideFullscreenCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->cursorAutoHideFullscreenDelaySpinBox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::populateCategories(int selectedRow)
{
    const int iconSize = 20;
    const auto addItem = [&](const Qv::MaterialIcon iconName, const QString &text) {
        ui->categoryTabs->addTab(qvApp->iconFromFont(iconName), text);
    };
    ui->categoryTabs->setIconSize(QSize(iconSize, iconSize));
    while (ui->categoryTabs->count() > 0)
        ui->categoryTabs->removeTab(0);
    addItem(Qv::MaterialIcon::WebAsset, tr("Display"));
    addItem(Qv::MaterialIcon::Tune, tr("Miscellaneous"));
    addItem(Qv::MaterialIcon::Keyboard, tr("Shortcuts"));
    addItem(Qv::MaterialIcon::Mouse, tr("Mouse"));
    const int safeRow = qBound(0, selectedRow, ui->categoryTabs->count() - 1);
    ui->categoryTabs->setCurrentIndex(safeRow);
    ui->stackedWidget->setCurrentIndex(safeRow);
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    // Put english at the top seperately because it has no file
    ui->langComboBox->addItem("English (en)", "en");

    const auto entries = QDir(":/i18n/").entryList();
    for (auto entry : entries)
    {
        entry.remove(0, 6);
        entry.remove(entry.length()-3, 3);
        QLocale locale(entry);

        const QString langString = locale.nativeLanguageName() + " (" + entry + ")";

        ui->langComboBox->addItem(langString, entry);
    }
}

void QVOptionsDialog::languageComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!isInitialLoad && !languageRestartMessageShown)
    {
        NativeDialogs::showMessage(QMessageBox::Information, tr("Restart Required"),
                                   tr("You must restart Fovelle for the language change to take effect."),
                                   QMessageBox::Ok, this);
        languageRestartMessageShown = true;
    }
}

void QVOptionsDialog::associateSupportedFormats()
{
    const auto extensions = Qv::setToSortedList(qvApp->getAllFileExtensionList());
    const auto result = QVCocoaFunctions::associateAllSupportedFormats(extensions);

    const QMessageBox::Icon icon = result.failedExtensions.isEmpty()
            ? QMessageBox::Information : QMessageBox::Warning;
    QString message = tr("Associated %1 supported formats with Fovelle.")
            .arg(result.associatedCount);
    if (!result.failedExtensions.isEmpty())
        message += tr("\nUnable to associate: %1.")
                .arg(result.failedExtensions.join(", "));
    NativeDialogs::showMessage(icon, tr("File Associations"), message,
                               QMessageBox::Ok, this);
}

void QVOptionsDialog::middleButtonModeChanged()
{
    const bool isClick = ui->middleButtonModeClickRadioButton->isChecked();
    const bool isDrag = ui->middleButtonModeDragRadioButton->isChecked();
    ui->middleClickLabel->setVisible(isClick);
    ui->middleClickComboBox->setVisible(isClick);
    ui->altMiddleClickLabel->setVisible(isClick);
    ui->altMiddleClickComboBox->setVisible(isClick);
    ui->middleDragLabel->setVisible(isDrag);
    ui->middleDragComboBox->setVisible(isDrag);
    ui->altMiddleDragLabel->setVisible(isDrag);
    ui->altMiddleDragComboBox->setVisible(isDrag);
}

const Ui::ComboBoxItems<Qv::AfterDelete> QVOptionsDialog::mapAfterDelete() {
    return {
        { Qv::AfterDelete::MoveBack, tr("Move Back") },
        { Qv::AfterDelete::DoNothing, tr("Do Nothing") },
        { Qv::AfterDelete::MoveForward, tr("Move Forward") }
    };
}

const Ui::ComboBoxItems<Qv::PreloadMode> QVOptionsDialog::mapPreloadMode() {
    return {
        { Qv::PreloadMode::Disabled, tr("Disabled") },
        { Qv::PreloadMode::Adjacent, tr("Adjacent") },
        { Qv::PreloadMode::Extended, tr("Extended") }
    };
}

const Ui::ComboBoxItems<Qv::SlideshowDirection> QVOptionsDialog::mapSlideshowDirection() {
    return {
        { Qv::SlideshowDirection::Forward, tr("Forward") },
        { Qv::SlideshowDirection::Backward, tr("Backward") },
        { Qv::SlideshowDirection::Random, tr("Random") }
    };
}

const Ui::ComboBoxItems<Qv::SmoothScalingMode> QVOptionsDialog::mapSmoothScalingMode() {
    return {
        { Qv::SmoothScalingMode::Disabled, tr("Disabled") },
        { Qv::SmoothScalingMode::Bilinear, tr("Bilinear") },
        { Qv::SmoothScalingMode::Expensive, tr("Expensive") }
    };
}

const Ui::ComboBoxItems<Qv::SortMode> QVOptionsDialog::mapSortMode() {
    return {
        { Qv::SortMode::Name, tr("Name") },
        { Qv::SortMode::DateModified, tr("Date Modified") },
        { Qv::SortMode::DateCreated, tr("Date Created") },
        { Qv::SortMode::Size, tr("Size") },
        { Qv::SortMode::Type, tr("Type") },
        { Qv::SortMode::Random, tr("Random") }
    };
}

const Ui::ComboBoxItems<Qv::Theme> QVOptionsDialog::mapTheme() {
    return {
        { Qv::Theme::Light, tr("Light") },
        { Qv::Theme::Dark, tr("Dark") },
        { Qv::Theme::System, tr("System") }
    };
}

const Ui::ComboBoxItems<Qv::UpdateCheckFrequency> QVOptionsDialog::mapUpdateCheckFrequency() {
    return {
        { Qv::UpdateCheckFrequency::Never, tr("Never") },
        { Qv::UpdateCheckFrequency::Daily, tr("Daily") },
        { Qv::UpdateCheckFrequency::Weekly, tr("Weekly") },
        { Qv::UpdateCheckFrequency::Monthly, tr("Monthly") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportClickAction> QVOptionsDialog::mapViewportClickAction() {
    return {
        { Qv::ViewportClickAction::None, tr("None") },
        { Qv::ViewportClickAction::ZoomToFit, tr("Zoom to Fit") },
        { Qv::ViewportClickAction::FillWindow, tr("Fill Window") },
        { Qv::ViewportClickAction::OriginalSize, tr("Original Size") },
        { Qv::ViewportClickAction::CenterImage, tr("Center Image") },
        { Qv::ViewportClickAction::ToggleFullScreen, tr("Toggle Full Screen") },
        { Qv::ViewportClickAction::ToggleTitlebarHidden, tr("Toggle Titlebar Hidden") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportDragAction> QVOptionsDialog::mapViewportDragAction() {
    return {
        { Qv::ViewportDragAction::None, tr("None") },
        { Qv::ViewportDragAction::Pan, tr("Pan") },
        { Qv::ViewportDragAction::MoveWindow, tr("Move Window") }
    };
}

const Ui::ComboBoxItems<Qv::ViewportScrollAction> QVOptionsDialog::mapViewportScrollAction() {
    return {
        { Qv::ViewportScrollAction::None, tr("None") },
        { Qv::ViewportScrollAction::Zoom, tr("Zoom") },
        { Qv::ViewportScrollAction::Navigate, tr("Navigate") },
        { Qv::ViewportScrollAction::Pan, tr("Pan") }
    };
}

template <typename TEnum>
static void populateComboBox(QComboBox *comboBox, const Ui::ComboBoxItems<TEnum> &items)
{
    comboBox->clear();
    for (const auto &item : items)
    {
        comboBox->addItem(item.second, static_cast<int>(item.first));
    }
}

void QVOptionsDialog::populateComboBoxes()
{
    populateComboBox(ui->themeComboBox, mapTheme());

    populateComboBox(ui->smoothScalingComboBox, mapSmoothScalingMode());

    populateComboBox(ui->sortComboBox, mapSortMode());

    populateComboBox(ui->preloadingComboBox, mapPreloadMode());

    populateComboBox(ui->slideshowDirectionComboBox, mapSlideshowDirection());

    populateComboBox(ui->afterDeletionComboBox, mapAfterDelete());

    populateComboBox(ui->updateFrequencyComboBox, mapUpdateCheckFrequency());

    populateComboBox(ui->doubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altDoubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->middleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altMiddleClickComboBox, mapViewportClickAction());

    populateComboBox(ui->dragComboBox, mapViewportDragAction());
    populateComboBox(ui->altDragComboBox, mapViewportDragAction());
    populateComboBox(ui->middleDragComboBox, mapViewportDragAction());
    populateComboBox(ui->altMiddleDragComboBox, mapViewportDragAction());

    populateComboBox(ui->verticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->horizontalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altVerticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altHorizontalScrollComboBox, mapViewportScrollAction());
}
