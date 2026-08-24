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
#include <QTabBar>

QVOptionsDialog::QVOptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);

    resize(650, 550);
    ui->categoryTabs->setShape(QTabBar::RoundedNorth);

    connect(ui->categoryTabs, &QTabBar::currentChanged, this, [this](int currentIndex) {
        ui->stackedWidget->setCurrentIndex(currentIndex);
    });
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this, &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->mainMenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("mainmenuicons", state); });
    connect(ui->contextMenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("contextmenuicons", state); });
    connect(ui->submenuIconsCheckbox, &QCheckBox::checkStateChanged, this, [this](Qt::CheckState state) { restartNotifyForCheckbox("submenuicons", state); });
    connect(ui->smoothScalingLimitCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::smoothScalingLimitCheckboxCheckStateChanged);
    connect(ui->fitZoomLimitCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::fitZoomLimitCheckboxCheckStateChanged);
    connect(ui->constrainImagePositionCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::constrainImagePositionCheckboxCheckStateChanged);
    connect(ui->cursorAutoHideFullscreenCheckbox, &QCheckBox::checkStateChanged, this, &QVOptionsDialog::cursorAutoHideFullscreenCheckboxCheckStateChanged);
    connect(ui->middleButtonModeClickRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->middleButtonModeDragRadioButton, &QRadioButton::clicked, this, &QVOptionsDialog::middleButtonModeChanged);
    connect(ui->smoothScalingComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::smoothScalingComboBoxCurrentIndexChanged);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->formatsTable, &QTableWidget::itemChanged, this, &QVOptionsDialog::formatsItemChanged);

    QSettings settings;

    populateCategories(settings.value("optionstab", 1).toInt());
    populateComboBoxes();
    populateLanguages();

    restoreGeometry(settings.value("optionsgeometry").toByteArray());

    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
        setWindowTitle(tr("Preferences"));

    ui->menubarCheckbox->hide();

    if (!QVApplication::supportsSessionPersistence())
        ui->persistSessionCheckbox->hide();

    QString ctrlKeyName = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText).replace(QRegularExpression("\\+$"), "");
    ui->altDoubleClickLabel->setText(tr("%1 + Double Click:").arg(ctrlKeyName));
    ui->altDragLabel->setText(tr("%1 + Drag:").arg(ctrlKeyName));
    ui->altMiddleClickLabel->setText(tr("%1 + Middle Click:").arg(ctrlKeyName));
    ui->altMiddleDragLabel->setText(tr("%1 + Middle Drag:").arg(ctrlKeyName));
    ui->altVerticalScrollLabel->setText(tr("%1 + Vertical Scroll:").arg(ctrlKeyName));
    ui->altHorizontalScrollLabel->setText(tr("%1 + Horizontal Scroll:").arg(ctrlKeyName));

    syncSettings(false, true);
    syncShortcuts();
    syncFormats();

    isInitialLoad = false;
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
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
    // On macOS, we don't make this dialog modal, so make sure it doesn't get covered by on top windows
    const auto updateWindowOnTop = [this]() {
        if (windowHandle())
            windowHandle()->setFlag(Qt::WindowStaysOnTopHint, qvApp->foundOnTopWindow());
    };
    updateWindowOnTop();
    connect(qvApp, &QVApplication::windowOnTopChanged, this, updateWindowOnTop);
    NativeDialogs::applyTheme(this);
    QDialog::showEvent(event);
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::PaletteChange)
    {
        populateCategories(ui->categoryTabs->currentIndex());
    }
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
    // windowresizemode
    syncComboBox(ui->windowResizeComboBox, "windowresizemode", defaults, makeConnections);
    // aftermatchingsize
    syncComboBox(ui->afterMatchingSizeComboBox, "aftermatchingsizemode", defaults, makeConnections);
    // minwindowresizedpercentage
    syncSpinBox(ui->minWindowResizeSpinBox, "minwindowresizedpercentage", defaults, makeConnections);
    // maxwindowresizedperecentage
    syncSpinBox(ui->maxWindowResizeSpinBox, "maxwindowresizedpercentage", defaults, makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // fullscreendetails
    syncCheckbox(ui->detailsInFullscreen, "fullscreendetails", defaults, makeConnections);
    // mainmenuicons
    syncCheckbox(ui->mainMenuIconsCheckbox, "mainmenuicons", defaults, makeConnections);
    // contextmenuicons
    syncCheckbox(ui->contextMenuIconsCheckbox, "contextmenuicons", defaults, makeConnections);
    // submenuicons
    syncCheckbox(ui->submenuIconsCheckbox, "submenuicons", defaults, makeConnections);
    // slideshowkeepswindowontop
    syncCheckbox(ui->slideshowKeepsWindowOnTopCheckbox, "slideshowkeepswindowontop", defaults, makeConnections);
    // reusewindow
    syncCheckbox(ui->reuseWindowCheckbox, "reusewindow", defaults, makeConnections);
    // persistsession
    syncCheckbox(ui->persistSessionCheckbox, "persistsession", defaults, makeConnections);
    // smoothscalingmode
    syncComboBox(ui->smoothScalingComboBox, "smoothscalingmode", defaults, makeConnections);
    // scalingtwoenabled
    syncCheckbox(ui->scalingTwoCheckbox, "scalingtwoenabled", defaults, makeConnections);
    // smoothscalinglimitenabled
    syncCheckbox(ui->smoothScalingLimitCheckbox, "smoothscalinglimitenabled", defaults, makeConnections);
    // smoothscalinglimitpercent
    syncSpinBox(ui->smoothScalingLimitSpinBox, "smoothscalinglimitpercent", defaults, makeConnections);
    // scalefactor
    syncSpinBox(ui->scaleFactorSpinBox, "scalefactor", defaults, makeConnections);
    // cursorzoom
    syncCheckbox(ui->cursorZoomCheckbox, "cursorzoom", defaults, makeConnections);
    // onetoonepixelsizing
    syncCheckbox(ui->oneToOnePixelSizingCheckbox, "onetoonepixelsizing", defaults, makeConnections);
    // smallimageoneone
    syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone", defaults, makeConnections);
    // calculatedzoommode
    syncComboBox(ui->zoomDefaultComboBox, "calculatedzoommode", defaults, makeConnections);
    // fitzoomlimitenabled
    syncCheckbox(ui->fitZoomLimitCheckbox, "fitzoomlimitenabled", defaults, makeConnections);
    fitZoomLimitCheckboxCheckStateChanged(ui->fitZoomLimitCheckbox->checkState());
    // fitzoomlimitpercent
    syncSpinBox(ui->fitZoomLimitSpinBox, "fitzoomlimitpercent", defaults, makeConnections);
    // fitoverscan
    syncSpinBox(ui->fitOverscanSpinBox, "fitoverscan", defaults, makeConnections);
    // navresetszoom
    syncCheckbox(ui->navResetsZoomCheckbox, "navresetszoom", defaults, makeConnections);
    // constrainimageposition
    syncCheckbox(ui->constrainImagePositionCheckbox, "constrainimageposition", defaults, makeConnections);
    constrainImagePositionCheckboxCheckStateChanged(ui->constrainImagePositionCheckbox->checkState());
    // constraincentersmallimage
    syncCheckbox(ui->constrainCentersSmallImageCheckbox, "constraincentersmallimage", defaults, makeConnections);
    // originalsizeastoggle
    syncCheckbox(ui->originalSizeAsToggleCheckbox, "originalsizeastoggle", defaults, makeConnections);
    // colorspaceconversion
    syncComboBox(ui->colorSpaceConversionComboBox, "colorspaceconversion", defaults, makeConnections);
    // language
    syncComboBox(ui->langComboBox, "language", defaults, makeConnections);
    // sortmode
    syncComboBox(ui->sortComboBox, "sortmode", defaults, makeConnections);
    // sortdescending
    syncRadioButtons({ui->descendingRadioButton0, ui->descendingRadioButton1}, "sortdescending", defaults, makeConnections);
    // preloadingmode
    syncComboBox(ui->preloadingComboBox, "preloadingmode", defaults, makeConnections);
    // navspeed
    syncSpinBox(ui->navSpeedSpinBox, "navspeed", defaults, makeConnections);
    // loopfolders
    syncCheckbox(ui->loopFoldersCheckbox, "loopfoldersenabled", defaults, makeConnections);
    // slideshowdirection
    syncComboBox(ui->slideshowDirectionComboBox, "slideshowdirection", defaults, makeConnections);
    // slideshowtimer
    syncDoubleSpinBox(ui->slideshowTimerSpinBox, "slideshowtimer", defaults, makeConnections);
    // afterdelete
    syncComboBox(ui->afterDeletionComboBox, "afterdelete", defaults, makeConnections);
    // askdelete
    syncCheckbox(ui->askDeleteCheckbox, "askdelete", defaults, makeConnections);
    // allowmimecontentdetection
    syncCheckbox(ui->mimeContentDetectionCheckbox, "allowmimecontentdetection", defaults, makeConnections);
    // skiphidden
    syncCheckbox(ui->skipHiddenCheckbox, "skiphidden", defaults, makeConnections);
    // saverecents
    syncCheckbox(ui->saveRecentsCheckbox, "saverecents", defaults, makeConnections);
    // updatenotifications
    syncCheckbox(ui->updateCheckbox, "updatenotifications", defaults, makeConnections);

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

void QVOptionsDialog::syncFormats(bool defaults)
{
    if (defaults)
        transientDisabledFileExtensions.clear();
    else
        transientDisabledFileExtensions = qvApp->getDisabledFileExtensions();

    const QStringList extensions = Qv::setToSortedList(qvApp->getAllFileExtensionList());

    isLoadingFormats = true;

    ui->formatsTable->setRowCount(extensions.count());

    int row = 0;
    for (const QString &extension : extensions)
    {
        const bool isDisabled = transientDisabledFileExtensions.contains(extension);

        auto *extensionItem = new QTableWidgetItem();
        extensionItem->setText(extension);
        ui->formatsTable->setItem(row, 0, extensionItem);

        auto *enabledItem = new QTableWidgetItem();
        enabledItem->setCheckState(isDisabled ? Qt::Unchecked : Qt::Checked);
        enabledItem->setData(Qt::UserRole, extension);
        ui->formatsTable->setItem(row, 1, enabledItem);

        row++;
    }

    isLoadingFormats = false;
}

void QVOptionsDialog::restartNotifyForCheckbox(const QString &key, const Qt::CheckState state)
{
    const bool savedValue = qvApp->getSettingsManager().getBoolean(key);
    if (static_cast<bool>(state) != savedValue)
        NativeDialogs::showMessage(QMessageBox::Information, tr("Restart Required"),
                                   tr("You must restart Fovelle for the setting change to take effect."),
                                   QMessageBox::Ok, this);
}

void QVOptionsDialog::smoothScalingComboBoxCurrentIndexChanged(int index)
{
    const auto value = static_cast<Qv::SmoothScalingMode>(ui->smoothScalingComboBox->itemData(index).toInt());
    ui->scalingTwoCheckbox->setEnabled(value == Qv::SmoothScalingMode::Expensive);
    ui->smoothScalingLimitCheckbox->setEnabled(value != Qv::SmoothScalingMode::Disabled);
    smoothScalingLimitCheckboxCheckStateChanged(ui->smoothScalingLimitCheckbox->checkState());
}

void QVOptionsDialog::smoothScalingLimitCheckboxCheckStateChanged(Qt::CheckState state)
{
    const bool selfEnabled = ui->smoothScalingLimitCheckbox->isEnabled();
    ui->smoothScalingLimitSpinBox->setEnabled(selfEnabled && static_cast<bool>(state));
}

void QVOptionsDialog::fitZoomLimitCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->fitZoomLimitSpinBox->setEnabled(static_cast<bool>(state));
}

void QVOptionsDialog::constrainImagePositionCheckboxCheckStateChanged(Qt::CheckState state)
{
    ui->constrainCentersSmallImageCheckbox->setEnabled(static_cast<bool>(state));
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
    addItem(Qv::MaterialIcon::WebAsset, tr("Window"));
    addItem(Qv::MaterialIcon::Image, tr("Image"));
    addItem(Qv::MaterialIcon::Tune, tr("Miscellaneous"));
    addItem(Qv::MaterialIcon::Keyboard, tr("Shortcuts"));
    addItem(Qv::MaterialIcon::Mouse, tr("Mouse"));
    addItem(Qv::MaterialIcon::Extension, tr("Formats"));
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

void QVOptionsDialog::formatsItemChanged(QTableWidgetItem *item)
{
    if (isLoadingFormats)
        return;

    const QString extension = item->data(Qt::UserRole).toString();
    if (item->checkState() == Qt::Unchecked)
        transientDisabledFileExtensions.insert(extension);
    else
        transientDisabledFileExtensions.remove(extension);

    QSettings settings;
    settings.setValue(QStringLiteral("options/disabledfileextensions"),
                      Qv::setToList(transientDisabledFileExtensions).join(';'));
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
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

const Ui::ComboBoxItems<Qv::AfterMatchingSize> QVOptionsDialog::mapAfterMatchingSize() {
    return {
        { Qv::AfterMatchingSize::AvoidRepositioning, tr("Avoid repositioning") },
        { Qv::AfterMatchingSize::CenterOnPrevious, tr("Center relative to previous image") },
        { Qv::AfterMatchingSize::CenterOnScreen, tr("Center relative to screen") }
    };
}

const Ui::ComboBoxItems<Qv::CalculatedZoomMode> QVOptionsDialog::mapCalculatedZoomMode() {
    return {
        { Qv::CalculatedZoomMode::ZoomToFit, tr("Zoom to Fit") },
        { Qv::CalculatedZoomMode::FillWindow, tr("Fill Window") },
        { Qv::CalculatedZoomMode::OriginalSize, tr("Original Size") }
    };
}

const Ui::ComboBoxItems<Qv::ColorSpaceConversion> QVOptionsDialog::mapColorSpaceConversion() {
    return {
        { Qv::ColorSpaceConversion::Disabled, tr("Disabled") },
        { Qv::ColorSpaceConversion::AutoDetect, tr("Auto-detect") },
        { Qv::ColorSpaceConversion::SRgb, tr("sRGB") },
        { Qv::ColorSpaceConversion::DisplayP3, tr("Display P3") }
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
        { Qv::Theme::Light, tr("Light Theme") },
        { Qv::Theme::Dark, tr("Dark Theme") },
        { Qv::Theme::System, tr("System") }
    };
}

const Ui::ComboBoxItems<Qv::WindowResizeMode> QVOptionsDialog::mapWindowResizeMode() {
    return {
        { Qv::WindowResizeMode::Never, tr("Never") },
        { Qv::WindowResizeMode::WhenLaunching, tr("When launching") },
        { Qv::WindowResizeMode::WhenOpeningImages, tr("When opening images") }
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

    populateComboBox(ui->windowResizeComboBox, mapWindowResizeMode());

    populateComboBox(ui->afterMatchingSizeComboBox, mapAfterMatchingSize());

    populateComboBox(ui->smoothScalingComboBox, mapSmoothScalingMode());

    populateComboBox(ui->zoomDefaultComboBox, mapCalculatedZoomMode());

    populateComboBox(ui->colorSpaceConversionComboBox, mapColorSpaceConversion());

    populateComboBox(ui->sortComboBox, mapSortMode());

    populateComboBox(ui->preloadingComboBox, mapPreloadMode());

    populateComboBox(ui->slideshowDirectionComboBox, mapSlideshowDirection());

    populateComboBox(ui->afterDeletionComboBox, mapAfterDelete());

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
