#include "qvoptionsdialog.h"
#include "qvsettingswidgets.h"
#include "ui_qvoptionsdialog.h"
#include "qvapplication.h"
#include "qvshortcutdialog.h"
#include "qvcocoafunctions.h"
#include "nativedialogs.h"

#include <QMessageBox>
#include <QApplication>
#include <QSettings>
#include <QWindow>
#include <QSignalBlocker>
#include <QTimer>
#include <QGridLayout>
#include <QTabBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QLabel>
#include <QScreen>
#include <QDynamicPropertyChangeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QShortcut>
#include <QStyleOptionComboBox>
#include <QStyleOptionSpinBox>

namespace {
constexpr int SettingsWidth = 500;
constexpr int SettingsMinimumWidth = 460;
constexpr int SettingsRowHeight = 32;

void configureSeparator(QFrame *separator)
{
    separator->setFrameShape(QFrame::NoFrame);
    separator->setBackgroundRole(QPalette::Window);
    separator->setAutoFillBackground(true);
    separator->setFixedHeight(1);
}

QColor settingsTint(const QPalette &palette, const qreal weight)
{
    const QColor base = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    return QColor::fromRgbF(base.redF() + (text.redF() - base.redF()) * weight,
                            base.greenF() + (text.greenF() - base.greenF()) * weight,
                            base.blueF() + (text.blueF() - base.blueF()) * weight);
}

void configureForms(QWidget *page)
{
    for (auto *form : page->findChildren<QGridLayout *>()) {
        for (int row = 0; row < form->rowCount(); ++row) {
            auto *labelItem = form->itemAtPosition(row, 0);
            auto *fieldItem = form->itemAtPosition(row, 1);
            if (!labelItem || !fieldItem || labelItem == fieldItem)
                continue;

            form->setRowMinimumHeight(row, SettingsRowHeight);
            auto *label = qobject_cast<QLabel *>(labelItem->widget());
            auto *field = fieldItem->widget();
            label->setAttribute(Qt::WA_LayoutUsesWidgetRect);
            field->setAttribute(Qt::WA_LayoutUsesWidgetRect);
            QWidget *editor = field;
            if (auto *spin = field->findChild<QDoubleSpinBox *>())
                editor = spin;
            else if (auto *button = field->findChild<QPushButton *>())
                editor = button;
            label->setBuddy(editor);
            label->setToolTip(editor->toolTip());
            if (qobject_cast<QCheckBox *>(editor)) {
                editor->setFixedSize(editor->sizeHint());
                editor->setAccessibleName(label->text().remove(QLatin1Char('&')));
            }
            if (auto *popup = qobject_cast<QComboBox *>(field)) {
                popup->setSizeAdjustPolicy(QComboBox::AdjustToContents);
                popup->setAttribute(Qt::WA_Hover);
                popup->setCursor(Qt::PointingHandCursor);
            }
        }
    }
    for (auto *line : page->findChildren<QFrame *>()) {
        if (line->objectName().endsWith(QStringLiteral("Separator")))
            configureSeparator(line);
    }
}

void measureForm(QScrollArea *scroll, int pageWidth)
{
    auto *content = scroll->widget();
    auto *layout = content->layout();
    for (auto *popup : content->findChildren<QComboBox *>()) {
        const QSize hint = popup->sizeHint().expandedTo(popup->minimumSizeHint());
        popup->setFixedSize(hint.width(), qMax(24, hint.height()));
    }
    const int scrollbarWidth = scroll->style()->pixelMetric(QStyle::PM_ScrollBarExtent);
    const int formWidth = pageWidth - qMax(0, scrollbarWidth);
    layout->setAlignment(Qt::AlignTop);
    content->setMinimumHeight(
            qMax(layout->minimumSize().height(), layout->heightForWidth(formWidth)));
}

int shortcutIndex(const QTableWidget *table, const int row)
{
    const auto *item = table->item(row, 0);
    if (!item || !item->data(Qt::UserRole).isValid())
        return -1;
    return item->data(Qt::UserRole).toInt();
}
} // namespace

void QVSettingsGroup::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool contrast = QVCocoaFunctions::increaseSettingsContrast();
    painter.setBrush(settingsTint(palette(), 0.055));
    painter.setPen(contrast ? QPen(settingsTint(palette(), 0.45)) : Qt::NoPen);
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9, 9);
}

void QVSettingsPopup::paintEvent(QPaintEvent *)
{
    QStyleOptionComboBox option;
    initStyleOption(&option);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool contrast = QVCocoaFunctions::increaseSettingsContrast();
    const bool hovered = isEnabled() && (underMouse() || popupOpen);
    if (hovered || hasFocus()) {
        painter.setBrush(hovered ? settingsTint(palette(), popupOpen ? 0.17 : 0.1)
                                 : QColor(Qt::transparent));
        painter.setPen(hasFocus() ? QPen(option.palette.color(QPalette::Highlight), 2) : Qt::NoPen);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 2, -1, -2), 6, 6);
    }
    const QColor text = isEnabled() ? settingsTint(palette(), contrast ? 0.92 : 0.72)
                                    : option.palette.color(QPalette::Disabled, QPalette::Text);
    painter.setPen(text);
    const QRect textRect = rect().adjusted(7, 0, -22, 0);
    painter.drawText(
            textRect, Qt::AlignRight | Qt::AlignVCenter,
            fontMetrics().elidedText(option.currentText, Qt::ElideRight, textRect.width()));
    const qreal x = width() - 10;
    const qreal y = height() / 2.0;
    QPainterPath chevron;
    chevron.moveTo(x - 3, y - 1);
    chevron.lineTo(x, y - 4);
    chevron.lineTo(x + 3, y - 1);
    chevron.moveTo(x - 3, y + 1);
    chevron.lineTo(x, y + 4);
    chevron.lineTo(x + 3, y + 1);
    painter.setPen(QPen(text, contrast ? 1.6 : 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(chevron);
}

void QVSettingsPopup::showPopup()
{
    popupOpen = true;
    update();
    QComboBox::showPopup();
    popupOpen = view() && view()->isVisible();
    update();
}

void QVSettingsPopup::hidePopup()
{
    popupOpen = false;
    QComboBox::hidePopup();
    update();
}

QVSettingsSwitch::QVSettingsSwitch(QWidget *parent) : QCheckBox(parent)
{
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize QVSettingsSwitch::sizeHint() const
{
    return { 42, SettingsRowHeight };
}

QSize QVSettingsSwitch::minimumSizeHint() const
{
    return sizeHint();
}

bool QVSettingsSwitch::hitButton(const QPoint &position) const
{
    return rect().contains(position);
}

void QVSettingsSwitch::paintEvent(QPaintEvent *event)
{
    const QImage image = QVCocoaFunctions::settingsSwitchImage(
            window()->windowHandle(), isChecked(), isEnabled(), devicePixelRatioF());
    if (image.isNull()) {
        QCheckBox::paintEvent(event);
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    if (hasFocus()) {
        painter.setPen(QPen(palette().color(QPalette::Highlight), 2));
        painter.drawRoundedRect(QRectF(rect()).adjusted(2, 4, -2, -4), 7, 7);
    }
    if (isDown())
        painter.setOpacity(0.8);
    const QSizeF size = QSizeF(image.size()) / image.devicePixelRatio();
    painter.drawImage(QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), image);
}

void QVSettingsNumber::paintEvent(QPaintEvent *event)
{
    QDoubleSpinBox::paintEvent(event);
    QStyleOptionSpinBox option;
    initStyleOption(&option);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto drawArrow = [&](QStyle::SubControl control, StepEnabledFlag enabled, int direction) {
        const QRect button = style()->subControlRect(QStyle::CC_SpinBox, &option, control, this);
        if (button.isEmpty())
            return;
        const auto group = isEnabled() && option.stepEnabled.testFlag(enabled) ? QPalette::Current
                                                                               : QPalette::Disabled;
        painter.setPen(QPen(palette().color(group, QPalette::WindowText), 1.2, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        const QPointF center = QRectF(button).center();
        const QPointF points[] = { center + QPointF(-3, -direction), center + QPointF(0, direction),
                                   center + QPointF(3, -direction) };
        painter.drawPolyline(points, 3);
    };
    drawArrow(QStyle::SC_SpinBoxUp, StepUpEnabled, -1);
    drawArrow(QStyle::SC_SpinBoxDown, StepDownEnabled, 1);
}

QVOptionsDialog::QVOptionsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    ui->stackedWidget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    for (auto *scroll : { ui->generalScrollArea, ui->mouseScrollArea }) {
        scroll->viewport()->setBackgroundRole(QPalette::Window);
        scroll->widget()->setAutoFillBackground(false);
        configureForms(scroll->widget());
    }

    ui->associateFormatsButton->setAutoDefault(false);
    ui->associateFormatsButton->setAttribute(Qt::WA_LayoutUsesWidgetRect);
    ui->associationDefaultIndicator->hide();
    ui->associationFeedback->hide();
    ui->associationFeedback->setTextFormat(Qt::PlainText);
    ui->slideshowTimerSpinBox->setAttribute(Qt::WA_MacSmallSize);
    ui->slideshowTimerSpinBox->setFrame(false);
    ui->slideshowTimerSpinBox->setMinimumWidth(76);
    ui->associateFormatsButton->setAttribute(Qt::WA_MacSmallSize);
    ui->editShortcutButton->setAutoDefault(false);
    ui->editShortcutButton->setEnabled(false);
    ui->editShortcutButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    ui->shortcutsFooter->setStretch(0, 1);
    configureSeparator(ui->shortcutFooterSeparator);

    auto *table = ui->shortcutsTable;
    table->setWordWrap(false);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setSectionsClickable(false);
    table->horizontalHeader()->setStyleSheet(QStringLiteral(
            "QHeaderView::section { background: palette(window); border: 0; padding: 4px; }"));
    ui->shortcutSearch->setAttribute(Qt::WA_MacSmallSize);
    const QString inputStyle =
            QStringLiteral("QLineEdit, QDoubleSpinBox { border: 1px solid palette(mid);"
                           " border-radius: 6px;"
                           " background: palette(base); padding: 2px 6px; }"
                           " QLineEdit:focus, QDoubleSpinBox:focus {"
                           " border-color: palette(highlight); }");
    ui->shortcutSearch->setStyleSheet(inputStyle);
    ui->slideshowTimerSpinBox->setStyleSheet(
            inputStyle
            + QStringLiteral("QDoubleSpinBox QLineEdit { border: 0;"
                             " background: transparent; padding: 0 3px; }"
                             " QDoubleSpinBox::up-arrow, QDoubleSpinBox::down-arrow"
                             " { image: none; }"));
    if (auto *editor = ui->slideshowTimerSpinBox->findChild<QLineEdit *>())
        editor->setAlignment(Qt::AlignRight);
    auto *closeShortcut = new QShortcut(QKeySequence::Close, this);
    connect(closeShortcut, &QShortcut::activated, this, &QDialog::close);
    connect(ui->shortcutSearch, &QLineEdit::textChanged, this, &QVOptionsDialog::filterShortcuts);
    table->setTabKeyNavigation(false);
    table->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->horizontalHeaderItem(1)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table->setTextElideMode(Qt::ElideRight);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(false);
    setProperty("settingsVisibleShortcutRows", ShortcutsVisibleRows);
    setProperty("settingsCategoryTransitionDuration", SettingsCategoryTransitionDuration);
    setProperty("settingsCategoryTransitionActive", false);

    categorySizeAnimation = new QPropertyAnimation(this, "settingsAnimatedSize", this);
    categorySizeAnimation->setObjectName(QStringLiteral("settingsCategorySizeAnimation"));
    categorySizeAnimation->setDuration(SettingsCategoryTransitionDuration);
    categorySizeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(categorySizeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (categoryTargetHeight > 0)
            setFixedSize(settingsWidth, categoryTargetHeight);
        setProperty("settingsCategoryTransitionActive", false);
    });

    ui->categoryTabs->setShape(QTabBar::RoundedNorth);
    // QTabBar remains the lightweight Qt page model used by tests and the
    // stacked widget connection.  AppKit supplies the visible Settings
    // toolbar once the native window exists.
    ui->categoryTabs->hide();

    connect(ui->categoryTabs, &QTabBar::currentChanged, this, [this](int currentIndex) {
        ui->stackedWidget->setCurrentIndex(currentIndex);
        resizeForCategory(currentIndex);

        // AppKit may move first-responder status back to the first focusable
        // Qt control after the native Settings toolbar changes panes.  The
        // toolbar is the navigation control, so a pane change must not make
        // General's Appearance combo look selected or steal keyboard input.
        QTimer::singleShot(0, this, [this]() {
            if (auto *focused = QApplication::focusWidget(); focused && focused->window() == this)
                focused->clearFocus();
        });
    });
    connect(table, &QTableWidget::cellDoubleClicked, this,
            [this](int row, int) { editShortcut(row); });
    connect(table, &QTableWidget::currentCellChanged, this, [this, table](int row, int, int, int) {
        ui->editShortcutButton->setEnabled(shortcutIndex(table, row) >= 0);
    });
    connect(ui->editShortcutButton, &QPushButton::clicked, this,
            [this, table] { editShortcut(table->currentRow()); });
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->associateFormatsButton, &QPushButton::clicked, this,
            &QVOptionsDialog::associateSupportedFormats);

    qApp->installEventFilter(this);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state == Qt::ApplicationActive && isVisible())
                    refreshFileAssociations();
            });

    QSettings settings;

    // The former Display and Miscellaneous tabs occupied indexes 0 and 1.
    // Map both to General, and shift Shortcuts/Mouse down by one.
    int selectedTab = settings.value("optionstab", 0).toInt();
    if (settings.value("optionstabversion", 0).toInt() < 2) {
        selectedTab = selectedTab <= 1 ? 0 : selectedTab - 1;
        settings.setValue("optionstab", selectedTab);
        settings.setValue("optionstabversion", 2);
        settings.sync();
    }
    populateCategories(selectedTab);
    populateComboBoxes();
    populateLanguages();

    if (QOperatingSystemVersion::current()
        < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
        setWindowTitle(tr("Preferences"));

    ui->menubarCheckbox->hide();

    QString ctrlKeyName = QKeySequence(Qt::CTRL)
                                  .toString(QKeySequence::NativeText)
                                  .replace(QRegularExpression("\\+$"), "");
    ui->altDoubleClickLabel->setText(tr("%1 + Double Click").arg(ctrlKeyName));
    ui->altDragLabel->setText(tr("%1 + Drag").arg(ctrlKeyName));
    ui->altMiddleClickLabel->setText(tr("%1 + Middle Click").arg(ctrlKeyName));
    ui->altVerticalScrollLabel->setText(tr("%1 + Vertical Scroll").arg(ctrlKeyName));
    ui->altHorizontalScrollLabel->setText(tr("%1 + Horizontal Scroll").arg(ctrlKeyName));

    syncSettings(false, true);
    syncShortcuts();
    updateNaturalPageSizes();
    resizeForCategory(ui->categoryTabs->currentIndex());

    connect(qvApp, &QVApplication::windowOnTopChanged, this, [this]() {
        if (windowHandle())
            windowHandle()->setFlag(Qt::WindowStaysOnTopHint, qvApp->foundOnTopWindow());
    });

    isInitialLoad = false;
}

QVOptionsDialog::~QVOptionsDialog()
{
    delete ui;
}

void QVOptionsDialog::updateNaturalPageSizes()
{
    const bool contrast = QVCocoaFunctions::increaseSettingsContrast();
    QPalette inputPalette = palette();
    inputPalette.setColor(QPalette::Mid, settingsTint(palette(), contrast ? 0.55 : 0.20));
    ui->shortcutSearch->setPalette(inputPalette);
    ui->slideshowTimerSpinBox->setPalette(inputPalette);
    const QColor secondaryColor = settingsTint(palette(), contrast ? 0.95 : 0.68);
    for (auto *label : ui->stackedWidget->findChildren<QLabel *>()) {
        if (!label->objectName().endsWith(QStringLiteral("Heading"))
            && label != ui->shortcutEditHint && label != ui->associationScope
            && label != ui->associationStatus && label != ui->associationFeedback)
            continue;
        QFont font = this->font();
        font.setPointSizeF(qMax(11.0, font.pointSizeF() - 2.0));
        if (label->objectName().endsWith(QStringLiteral("Heading")))
            font.setWeight(QFont::Medium);
        label->setFont(font);
        QPalette labelPalette = palette();
        labelPalette.setColor(QPalette::WindowText, secondaryColor);
        label->setPalette(labelPalette);
    }

    auto *shortcutTable = ui->shortcutsTable;
    QPalette tablePalette = palette();
    tablePalette.setColor(QPalette::Base, palette().color(QPalette::Window));
    tablePalette.setColor(QPalette::AlternateBase, settingsTint(palette(), 0.035));
    shortcutTable->setPalette(tablePalette);
    for (int row = 0; row < shortcutTable->rowCount(); ++row) {
        if (shortcutIndex(shortcutTable, row) < 0)
            shortcutTable->item(row, 0)->setForeground(secondaryColor);
    }
    const QColor separatorColor = settingsTint(palette(), contrast ? 0.42 : 0.13);
    QPalette separatorPalette = palette();
    separatorPalette.setColor(QPalette::Window, separatorColor);
    for (auto *separator : ui->stackedWidget->findChildren<QFrame *>()) {
        if (separator->objectName().endsWith(QStringLiteral("Separator")))
            separator->setPalette(separatorPalette);
    }

    const QMargins margins = ui->verticalLayout->contentsMargins();
    if (const auto *display = screen()) {
        settingsWidth = qBound(SettingsMinimumWidth, display->availableGeometry().width() - 24,
                               SettingsWidth);
    }
    const int pageWidth = settingsWidth - margins.left() - margins.right();
    measureForm(ui->generalScrollArea, pageWidth);
    measureForm(ui->mouseScrollArea, pageWidth);

    auto *table = ui->shortcutsTable;
    const int rowHeight = qMax(26, table->fontMetrics().height() + 6);
    table->verticalHeader()->setDefaultSectionSize(rowHeight);
    const int headerHeight = table->horizontalHeader()->sizeHint().height();
    const int tableHeight =
            headerHeight + rowHeight * qMin(ShortcutsVisibleRows, table->rowCount());
    const int footerHeight = ui->shortcutsFooter->sizeHint().height()
            + ui->shortcutFooterSeparator->height() + ui->shortcutSearch->sizeHint().height()
            + 3 * ui->shortcutsLayout->spacing();
    pageHeights = { ui->generalContent->minimumHeight(), tableHeight + footerHeight,
                    ui->mouse->minimumHeight() };

    setProperty("settingsTabWidths", QVariantList{ pageWidth, pageWidth, pageWidth });
    setProperty("settingsFixedWidth", settingsWidth);
    setProperty("settingsTabWidth", pageWidth);
    pageMetricsReady = true;
}

void QVOptionsDialog::resizeForCategory(const int categoryIndex)
{
    if (!pageMetricsReady || categoryIndex < 0 || categoryIndex >= pageHeights.size())
        return;

    const QMargins margins = ui->verticalLayout->contentsMargins();
    const int contentHeight = pageHeights.at(categoryIndex);
    int targetHeight = contentHeight + margins.top() + margins.bottom();
    if (const auto *display = screen()) {
        const int nativeChrome = qMax(0, frameGeometry().height() - height());
        const int availableHeight = display->availableGeometry().height() - nativeChrome;
        targetHeight = qMin(targetHeight, qMax(1, availableHeight - 20));
    }
    categoryTargetHeight = targetHeight;

    if (isVisible() && height() != targetHeight && !QVCocoaFunctions::reduceThumbnailMotion()) {
        categorySizeAnimation->stop();
        setProperty("settingsCategoryTransitionActive", true);
        categorySizeAnimation->setStartValue(QSize(settingsWidth, height()));
        categorySizeAnimation->setEndValue(QSize(settingsWidth, targetHeight));
        categorySizeAnimation->start();
    } else {
        finishCategoryTransition();
    }
    setProperty("settingsCategoryIndex", categoryIndex);
    setProperty("settingsCategoryTargetWidth", settingsWidth);
    setProperty("settingsCategoryContentHeight", contentHeight);
}

QSize QVOptionsDialog::settingsAnimatedSize() const
{
    return size();
}

void QVOptionsDialog::setSettingsAnimatedSize(const QSize &size)
{
    setFixedSize(qMax(1, size.width()), qMax(1, size.height()));
}

void QVOptionsDialog::finishCategoryTransition()
{
    if (categorySizeAnimation)
        categorySizeAnimation->stop();
    if (categoryTargetHeight > 0)
        setFixedSize(settingsWidth, categoryTargetHeight);
    setProperty("settingsCategoryTransitionActive", false);
}

void QVOptionsDialog::prepareForDisplay()
{
    if (displayPrepared)
        return;

    ensurePolished();
    // QWidget::winId() creates the native NSWindow without ordering it front.
    // The toolbar therefore contributes to frameGeometry before centering.
    (void)winId();
    if (windowHandle())
        windowHandle()->setFlag(Qt::WindowStaysOnTopHint, qvApp->foundOnTopWindow());
    NativeDialogs::applyTheme(this);
    QVCocoaFunctions::configureSettingsToolbar(windowHandle(), ui->categoryTabs);
    refreshFileAssociations();
    // Measure after AppKit installs the toolbar so the first frame includes
    // its chrome when applying the display's available-height constraint.
    updateNaturalPageSizes();
    resizeForCategory(ui->categoryTabs->currentIndex());
    displayPrepared = true;
}

void QVOptionsDialog::done(int r)
{
    // Never persist an interpolated frame. Settings size is derived from the
    // selected language and page contents; only the selected category is user
    // state. A saved size would introduce a competing source of truth when a
    // fresh dialog instance is opened.
    finishCategoryTransition();

    QSettings settings;
    settings.remove("optionsgeometry");
    settings.setValue("optionstab", ui->categoryTabs->currentIndex());
    settings.setValue("optionstabversion", 2);

    QDialog::done(r);
}

void QVOptionsDialog::showEvent(QShowEvent *event)
{
    // Direct callers still receive the native toolbar and application theme;
    // QVApplication invokes this before positioning to prevent visible moves.
    prepareForDisplay();
    QDialog::showEvent(event);
    // Cocoa finalizes the preference-toolbar content border while dispatching
    // the first show event. Reapply the already-measured fixed frame inside
    // that event so a dialog that starts on Mouse has the same client size as
    // one switched to Mouse after presentation.
    finishCategoryTransition();
    QTimer::singleShot(0, this, [this]() {
        if (!ui || !pageMetricsReady || !isVisible())
            return;
        // Cocoa can finalize native control metrics only after the first
        // show event. Re-run the same post-polish normalization once those
        // metrics are observable, so no row is fixed below its final hint.
        pageMetricsReady = false;
        updateNaturalPageSizes();
        resizeForCategory(ui->categoryTabs->currentIndex());
        finishCategoryTransition();
    });
}

void QVOptionsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange)
        ui->retranslateUi(this);
    if (event->type() == QEvent::LanguageChange || event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange) {
        QTimer::singleShot(0, this, [this]() {
            if (!ui || !pageMetricsReady)
                return;
            pageMetricsReady = false;
            updateNaturalPageSizes();
            resizeForCategory(ui->categoryTabs->currentIndex());
            finishCategoryTransition();
        });
    }
}

void QVOptionsDialog::modifySetting(QString key, QVariant value)
{
    QSettings settings;
    settings.beginGroup("options");
    settings.setValue(key, value);
    settings.endGroup();
    settings.sync();
    qvApp->getSettingsManager().loadSettings();
    if (key == QStringLiteral("theme")) {
        NativeDialogs::applyTheme(this);
    }
}

void QVOptionsDialog::syncSettings(bool defaults, bool makeConnections)
{
    auto &settingsManager = qvApp->getSettingsManager();
    settingsManager.loadSettings();

    // theme
    syncComboBox(ui->themeComboBox, "theme", defaults, makeConnections);
    // checkerboardbackground
    syncCheckbox(ui->checkerboardBackgroundCheckbox, "checkerboardbackground", defaults,
                 makeConnections);
    // menubarenabled
    syncCheckbox(ui->menubarCheckbox, "menubarenabled", defaults, makeConnections);
    // reusewindow
    syncCheckbox(ui->reuseWindowCheckbox, "reusewindow", defaults, makeConnections);
    // Preserve the historical inverse key and its default for existing users.
    {
        const QSignalBlocker blocker(ui->keepZoomCheckbox);
        ui->keepZoomCheckbox->setChecked(!settingsManager.getBoolean("navresetszoom", defaults));
    }
    if (makeConnections)
        connect(ui->keepZoomCheckbox, &QCheckBox::toggled, this,
                [this](bool checked) { modifySetting(QStringLiteral("navresetszoom"), !checked); });
    // smoothscalingmode
    syncComboBox(ui->smoothScalingComboBox, "smoothscalingmode", defaults, makeConnections);
    // smallimageoneone
    syncCheckbox(ui->smallImagesOneToOneCheckbox, "smallimageoneone", defaults, makeConnections);
    // language
    syncComboBox(ui->langComboBox, "language", defaults, makeConnections);
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
    syncComboBox(ui->doubleClickComboBox, "viewportdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->altDoubleClickComboBox, "viewportaltdoubleclickaction", defaults,
                 makeConnections);
    syncComboBox(ui->dragComboBox, "viewportdragaction", defaults, makeConnections);
    syncComboBox(ui->altDragComboBox, "viewportaltdragaction", defaults, makeConnections);
    syncComboBox(ui->middleClickComboBox, "viewportmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleClickComboBox, "viewportaltmiddleclickaction", defaults,
                 makeConnections);
    syncComboBox(ui->verticalScrollComboBox, "viewportverticalscrollaction", defaults,
                 makeConnections);
    syncComboBox(ui->horizontalScrollComboBox, "viewporthorizontalscrollaction", defaults,
                 makeConnections);
    syncComboBox(ui->altVerticalScrollComboBox, "viewportaltverticalscrollaction", defaults,
                 makeConnections);
    syncComboBox(ui->altHorizontalScrollComboBox, "viewportalthorizontalscrollaction", defaults,
                 makeConnections);
}

void QVOptionsDialog::syncCheckbox(QCheckBox *checkbox, const QString &key, bool defaults,
                                   bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getBoolean(key, defaults);
    const QSignalBlocker blocker(checkbox);
    checkbox->setChecked(val);

    if (makeConnection) {
        connect(checkbox, &QCheckBox::checkStateChanged, this, [this, key](Qt::CheckState state) {
            modifySetting(key, static_cast<bool>(state));
        });
    }
}

void QVOptionsDialog::syncComboBox(QComboBox *comboBox, const QString &key, bool defaults,
                                   bool makeConnection)
{
    const QVariant val = qvApp->getSettingsManager().getSetting(key, defaults);
    const QSignalBlocker blocker(comboBox);
    int index = comboBox->findData(val);
    if (index < 0)
        index = comboBox->findData(val.toInt());
    comboBox->setCurrentIndex(index);

    if (makeConnection) {
        connect(comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, key, comboBox](int index) {
                    Q_UNUSED(index)
                    modifySetting(key, comboBox->currentData());
                });
    }
}

void QVOptionsDialog::syncSpinBox(QSpinBox *spinBox, const QString &key, bool defaults,
                                  bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getInteger(key, defaults);
    const QSignalBlocker blocker(spinBox);
    spinBox->setValue(val);

    if (makeConnection) {
        connect(spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this, key](int i) { modifySetting(key, i); });
    }
}

void QVOptionsDialog::syncDoubleSpinBox(QDoubleSpinBox *doubleSpinBox, const QString &key,
                                        bool defaults, bool makeConnection)
{
    auto val = qvApp->getSettingsManager().getDouble(key, defaults);
    const QSignalBlocker blocker(doubleSpinBox);
    doubleSpinBox->setValue(val);

    if (makeConnection) {
        connect(doubleSpinBox, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, key](double d) { modifySetting(key, d); });
    }
}

void QVOptionsDialog::syncShortcuts()
{
    auto &manager = qvApp->getShortcutManager();
    manager.updateShortcuts();

    const QHash<QString, QString> categoryStarts{
        { QStringLiteral("open"), tr("Files & Clipboard") },
        { QStringLiteral("firstfile"), tr("Navigation") },
        { QStringLiteral("zoomin"), tr("View & Comparison") },
        { QStringLiteral("saveframeas"), tr("Animation & Slideshow") },
        { QStringLiteral("options"), tr("App & Windows") },
    };

    auto *table = ui->shortcutsTable;
    table->clearSpans();
    table->setRowCount(0);
    transientShortcuts.clear();
    const auto &shortcuts = manager.getShortcutsList();
    for (int index = 0; index < shortcuts.size(); ++index) {
        const auto &shortcut = shortcuts.at(index);
        if (const QString category = categoryStarts.value(shortcut.name); !category.isEmpty()) {
            const int row = table->rowCount();
            table->insertRow(row);
            auto *heading = new QTableWidgetItem(category);
            heading->setFlags(Qt::ItemIsEnabled);
            QFont font = table->font();
            font.setWeight(QFont::Medium);
            font.setPointSizeF(qMax(11.0, font.pointSizeF() - 1.0));
            heading->setFont(font);
            table->setItem(row, 0, heading);
            table->setSpan(row, 0, 1, 2);
        }

        const int row = table->rowCount();
        table->insertRow(row);
        auto *name = new QTableWidgetItem(shortcut.readableName);
        name->setData(Qt::UserRole, index);
        name->setToolTip(shortcut.readableName);
        table->setItem(row, 0, name);
        auto *keys = new QTableWidgetItem;
        keys->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, 1, keys);
        transientShortcuts.append(shortcut.shortcuts);
    }
    updateShortcutsTable();
}

void QVOptionsDialog::updateShortcutsTable()
{
    auto *table = ui->shortcutsTable;
    for (int row = 0; row < table->rowCount(); ++row) {
        const int index = shortcutIndex(table, row);
        if (index < 0)
            continue;
        const QString text =
                ShortcutManager::stringListToReadableString(transientShortcuts.at(index));
        auto *item = table->item(row, 1);
        item->setText(text.isEmpty() ? QStringLiteral("—") : text);
        item->setToolTip(text);
    }
    filterShortcuts();
}

void QVOptionsDialog::filterShortcuts()
{
    auto *table = ui->shortcutsTable;
    const QString query = ui->shortcutSearch->text().trimmed();
    int heading = -1;
    bool sectionHasMatch = false;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (shortcutIndex(table, row) < 0) {
            if (heading >= 0)
                table->setRowHidden(heading, !sectionHasMatch);
            heading = row;
            sectionHasMatch = false;
            continue;
        }
        const bool matches = table->item(row, 0)->text().contains(query, Qt::CaseInsensitive)
                || table->item(row, 1)->text().contains(query, Qt::CaseInsensitive);
        table->setRowHidden(row, !matches);
        sectionHasMatch |= matches;
    }
    if (heading >= 0)
        table->setRowHidden(heading, !sectionHasMatch);
    if (table->currentRow() >= 0 && table->isRowHidden(table->currentRow())) {
        table->clearSelection();
        table->setCurrentItem(nullptr);
    }
    ui->editShortcutButton->setEnabled(shortcutIndex(table, table->currentRow()) >= 0);
}

void QVOptionsDialog::editShortcut(int row)
{
    const int index = shortcutIndex(ui->shortcutsTable, row);
    if (index < 0 || ui->shortcutsTable->isRowHidden(row))
        return;

    const auto getTransientShortcut = [this](int shortcutIndex) {
        return transientShortcuts.value(shortcutIndex);
    };
    auto *shortcutDialog = new QVShortcutDialog(index, getTransientShortcut, this);
    connect(shortcutDialog, &QVShortcutDialog::shortcutsListChanged, this,
            [this](int index, const QStringList &values) {
                transientShortcuts.replace(index, values);
                const auto &shortcut = qvApp->getShortcutManager().getShortcutsList().at(index);
                QSettings settings;
                settings.setValue(QStringLiteral("shortcuts/") + shortcut.name, values);
                settings.sync();
                qvApp->getShortcutManager().updateShortcuts();
                updateShortcutsTable();
            });
    shortcutDialog->open();
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
    addItem(Qv::MaterialIcon::Tune, tr("General"));
    addItem(Qv::MaterialIcon::Keyboard, tr("Shortcuts"));
    addItem(Qv::MaterialIcon::Mouse, tr("Mouse & Trackpad"));
    const int safeRow = qBound(0, selectedRow, ui->categoryTabs->count() - 1);
    ui->categoryTabs->setCurrentIndex(safeRow);
    ui->stackedWidget->setCurrentIndex(safeRow);
}

void QVOptionsDialog::populateLanguages()
{
    ui->langComboBox->clear();

    ui->langComboBox->addItem(tr("System Language"), "system");

    ui->langComboBox->addItem(QStringLiteral("English"), QStringLiteral("en"));
    ui->langComboBox->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_Hans"));
    ui->langComboBox->addItem(QStringLiteral("繁體中文"), QStringLiteral("zh_Hant"));
    ui->langComboBox->addItem(QStringLiteral("Español"), QStringLiteral("es"));
    ui->langComboBox->addItem(QStringLiteral("日本語"), QStringLiteral("ja"));
}

void QVOptionsDialog::languageComboBoxCurrentIndexChanged(int index)
{
    Q_UNUSED(index)
    if (!isInitialLoad && !languageRestartMessageShown) {
        NativeDialogs::showMessage(
                QMessageBox::Information, tr("Restart Required"),
                tr("You must restart Fovelle for the language change to take effect."),
                QMessageBox::Ok, this);
        languageRestartMessageShown = true;
    }
}

bool QVOptionsDialog::eventFilter(QObject *object, QEvent *event)
{
    if (object == qApp && event->type() == QEvent::DynamicPropertyChange) {
        const auto name = static_cast<QDynamicPropertyChangeEvent *>(event)->propertyName();
        if (name == "fovelleFileAssociationInProgress" && isVisible())
            refreshFileAssociations();
        if (name == "fovelleAccessibilityDisplayRevision") {
            if (pageMetricsReady) {
                updateNaturalPageSizes();
                resizeForCategory(ui->categoryTabs->currentIndex());
            }
            for (auto *control : ui->stackedWidget->findChildren<QWidget *>())
                control->update();
        }
    }
    return QDialog::eventFilter(object, event);
}

void QVOptionsDialog::refreshFileAssociations()
{
    using State = QVCocoaFunctions::FileAssociationState;
    const bool busy = QVCocoaFunctions::isChangingFileAssociations();
    State state = State::Unknown;
    if (!busy) {
        const auto extensions = Qv::setToSortedList(qvApp->getAllFileExtensionList());
        state = QVCocoaFunctions::readFileAssociations(extensions).state();
    }

    QString status;
    switch (state) {
    case State::None:
        status = tr("Not set as default");
        break;
    case State::Partial:
        status = tr("Default for some formats");
        break;
    case State::All:
        status = tr("Default for all supported formats");
        break;
    case State::Unknown:
        status = tr("Default status unavailable");
        break;
    }
    ui->associationStatus->setText(busy ? tr("Updating file associations…") : status);
    ui->associateFormatsButton->setText(state == State::None ? tr("Set as Default")
                                                             : tr("Associate All"));
    ui->associateFormatsButton->setFixedSize(ui->associateFormatsButton->sizeHint());
    ui->associateFormatsButton->setDisabled(busy);
    ui->associateFormatsButton->setVisible(state != State::All);
    ui->associationDefaultIndicator->setText(tr("✓ Default"));
    ui->associationDefaultIndicator->setVisible(state == State::All);
    ui->associationActions->layout()->activate();
    ui->associationActions->updateGeometry();
    if (pageMetricsReady && isVisible()) {
        updateNaturalPageSizes();
        resizeForCategory(ui->categoryTabs->currentIndex());
    }
}

void QVOptionsDialog::associateSupportedFormats()
{
    if (QVCocoaFunctions::isChangingFileAssociations())
        return;

    ui->associationFeedback->hide();
    const QPointer<QVOptionsDialog> dialog(this);
    const auto extensions = Qv::setToSortedList(qvApp->getAllFileExtensionList());
    QVCocoaFunctions::associateAllSupportedFormats(
            extensions, [dialog](QVCocoaFunctions::FileAssociationAttempt result) {
                if (!dialog)
                    return;
                QString feedback;
                if (result.cancelled)
                    feedback = tr(
                            "The system request was cancelled. Earlier changes may still apply.");
                if (!result.failures.isEmpty()) {
                    if (!feedback.isEmpty())
                        feedback += QLatin1Char('\n');
                    feedback += tr("Some formats could not be updated. %1")
                                        .arg(result.failures.join(QLatin1Char('\n')));
                }
                dialog->refreshFileAssociations();
                if (feedback.isEmpty() && dialog->ui->associationDefaultIndicator->isHidden())
                    feedback = tr("The system has not confirmed the change for every format.");
                dialog->ui->associationFeedback->setText(feedback);
                dialog->ui->associationFeedback->setVisible(!feedback.isEmpty());
                dialog->updateNaturalPageSizes();
                dialog->resizeForCategory(dialog->ui->categoryTabs->currentIndex());
            });
}

const Ui::ComboBoxItems<Qv::AfterDelete> QVOptionsDialog::mapAfterDelete()
{
    return { { Qv::AfterDelete::MoveBack, tr("Move Back") },
             { Qv::AfterDelete::DoNothing, tr("No Action") },
             { Qv::AfterDelete::MoveForward, tr("Move Forward") } };
}

const Ui::ComboBoxItems<Qv::SlideshowDirection> QVOptionsDialog::mapSlideshowDirection()
{
    return { { Qv::SlideshowDirection::Forward, tr("Forward") },
             { Qv::SlideshowDirection::Backward, tr("Backward") },
             { Qv::SlideshowDirection::Random, tr("Random") } };
}

const Ui::ComboBoxItems<Qv::SmoothScalingMode> QVOptionsDialog::mapSmoothScalingMode()
{
    return { { Qv::SmoothScalingMode::Disabled, tr("Disabled") },
             { Qv::SmoothScalingMode::Bilinear, tr("Bilinear") },
             { Qv::SmoothScalingMode::Expensive, tr("High Quality") } };
}

const Ui::ComboBoxItems<Qv::Theme> QVOptionsDialog::mapTheme()
{
    return { { Qv::Theme::Light, tr("Light") },
             { Qv::Theme::Dark, tr("Dark") },
             { Qv::Theme::System, tr("System") } };
}

const Ui::ComboBoxItems<Qv::UpdateCheckFrequency> QVOptionsDialog::mapUpdateCheckFrequency()
{
    return { { Qv::UpdateCheckFrequency::Never, tr("Never") },
             { Qv::UpdateCheckFrequency::Daily, tr("Daily") },
             { Qv::UpdateCheckFrequency::Weekly, tr("Weekly") },
             { Qv::UpdateCheckFrequency::Monthly, tr("Monthly") } };
}

const Ui::ComboBoxItems<Qv::ViewportClickAction> QVOptionsDialog::mapViewportClickAction()
{
    return { { Qv::ViewportClickAction::None, tr("None") },
             { Qv::ViewportClickAction::ZoomToFit, tr("Zoom to Fit") },
             { Qv::ViewportClickAction::FillWindow, tr("Fill Window") },
             { Qv::ViewportClickAction::OriginalSize, tr("Original Size") },
             { Qv::ViewportClickAction::CenterImage, tr("Center Image") },
             { Qv::ViewportClickAction::ToggleFullScreen, tr("Toggle Full Screen") },
             { Qv::ViewportClickAction::ToggleTitlebarHidden, tr("Toggle Titlebar Hidden") } };
}

const Ui::ComboBoxItems<Qv::ViewportDragAction> QVOptionsDialog::mapViewportDragAction()
{
    return { { Qv::ViewportDragAction::None, tr("None") },
             { Qv::ViewportDragAction::Pan, tr("Pan") },
             { Qv::ViewportDragAction::MoveWindow, tr("Move Window") } };
}

const Ui::ComboBoxItems<Qv::ViewportScrollAction> QVOptionsDialog::mapViewportScrollAction()
{
    return { { Qv::ViewportScrollAction::None, tr("None") },
             { Qv::ViewportScrollAction::Zoom, tr("Zoom") },
             { Qv::ViewportScrollAction::Navigate, tr("Navigate") },
             { Qv::ViewportScrollAction::Pan, tr("Pan") } };
}

template <typename TEnum>
static void populateComboBox(QComboBox *comboBox, const Ui::ComboBoxItems<TEnum> &items)
{
    comboBox->clear();
    for (const auto &item : items) {
        comboBox->addItem(item.second, static_cast<int>(item.first));
    }
}

void QVOptionsDialog::populateComboBoxes()
{
    populateComboBox(ui->themeComboBox, mapTheme());

    populateComboBox(ui->smoothScalingComboBox, mapSmoothScalingMode());

    populateComboBox(ui->slideshowDirectionComboBox, mapSlideshowDirection());

    populateComboBox(ui->afterDeletionComboBox, mapAfterDelete());

    populateComboBox(ui->updateFrequencyComboBox, mapUpdateCheckFrequency());

    populateComboBox(ui->doubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altDoubleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->middleClickComboBox, mapViewportClickAction());
    populateComboBox(ui->altMiddleClickComboBox, mapViewportClickAction());

    populateComboBox(ui->dragComboBox, mapViewportDragAction());
    populateComboBox(ui->altDragComboBox, mapViewportDragAction());

    populateComboBox(ui->verticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->horizontalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altVerticalScrollComboBox, mapViewportScrollAction());
    populateComboBox(ui->altHorizontalScrollComboBox, mapViewportScrollAction());
}
