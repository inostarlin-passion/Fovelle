#include "qvoptionsdialog.h"
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
#include <QFormLayout>
#include <QTabBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QHeaderView>
#include <QEasingCurve>
#include <QPropertyAnimation>
#include <QScrollBar>
#include <QStyleOptionViewItem>
#include <QStyle>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QLabel>
#include <QGroupBox>

namespace
{
constexpr Qt::Alignment SettingsLabelAlignment =
    Qt::AlignRight | Qt::AlignTrailing;
constexpr Qt::Alignment SettingsValueAlignment = Qt::AlignLeft | Qt::AlignTop;
constexpr Qt::Alignment SettingsLabelContentAlignment =
    Qt::AlignRight | Qt::AlignTrailing | Qt::AlignVCenter;

struct SettingsLayoutMetrics
{
    int maximumIntraGroupSpacing {0};
    int groupSpacing {0};
};

bool layoutItemIsVisible(QLayoutItem *item)
{
    if (!item)
        return false;
    if (auto *widget = item->widget())
        return !widget->isHidden();
    if (auto *layout = item->layout())
    {
        for (int index = 0; index < layout->count(); ++index)
        {
            if (layoutItemIsVisible(layout->itemAt(index)))
                return true;
        }
    }
    return false;
}

QSizePolicy::ControlTypes layoutItemControlTypes(QLayoutItem *item)
{
    if (!item)
        return QSizePolicy::DefaultType;
    if (auto *widget = item->widget())
        return widget->sizePolicy().controlType();
    return item->controlTypes();
}

QList<QLayoutItem *> visibleRowItems(QFormLayout *layout, const int row)
{
    QList<QLayoutItem *> items;
    for (const auto role : {QFormLayout::LabelRole,
                            QFormLayout::FieldRole,
                            QFormLayout::SpanningRole})
    {
        auto *item = layout->itemAt(row, role);
        if (layoutItemIsVisible(item))
            items.append(item);
    }
    return items;
}

int resolvedStyleSpacing(QWidget *context,
                         const QSizePolicy::ControlTypes first,
                         const QSizePolicy::ControlTypes second)
{
    if (!context || !context->style())
        return 0;

    int spacing = context->style()->combinedLayoutSpacing(
        first, second, Qt::Vertical, nullptr, context);
    if (spacing < 0)
        spacing = context->style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing,
                                                nullptr, context);
    return qMax(0, spacing);
}

int maximumIntraGroupSpacing(QWidget *page)
{
    if (!page)
        return 0;

    int maximumSpacing = 0;
    for (auto *layout : page->findChildren<QFormLayout *>())
    {
        QList<QLayoutItem *> previousItems;
        for (int row = 0; row < layout->rowCount(); ++row)
        {
            const QList<QLayoutItem *> currentItems = visibleRowItems(layout, row);
            if (currentItems.isEmpty())
                continue;

            for (auto *previous : previousItems)
            {
                for (auto *current : currentItems)
                {
                    maximumSpacing = qMax(
                        maximumSpacing,
                        resolvedStyleSpacing(
                            page, layoutItemControlTypes(previous),
                            layoutItemControlTypes(current)));
                }
            }
            previousItems = currentItems;
        }
    }
    return maximumSpacing;
}

SettingsLayoutMetrics settingsLayoutMetrics(QWidget *generalPage,
                                             QWidget *mousePage)
{
    SettingsLayoutMetrics metrics;
    metrics.maximumIntraGroupSpacing = qMax(
        maximumIntraGroupSpacing(generalPage),
        maximumIntraGroupSpacing(mousePage));

    QWidget *styleContext = generalPage ? generalPage : mousePage;
    const int nativeGroupSpacing = resolvedStyleSpacing(
        styleContext, QSizePolicy::GroupBox, QSizePolicy::GroupBox);
    metrics.groupSpacing = qMax(nativeGroupSpacing,
                                metrics.maximumIntraGroupSpacing + 1);
    return metrics;
}

void configureSettingsForm(QFormLayout *layout)
{
    if (!layout)
        return;

    layout->setVerticalSpacing(-1);
    layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
    layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    layout->setLabelAlignment(SettingsLabelAlignment);
}

void configureSettingsGroupsLayout(QWidget *page, const int groupSpacing,
                                   const int maximumIntraGroupSpacing)
{
    if (!page)
        return;

    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    if (!layout)
        return;

    layout->setSpacing(groupSpacing);
    layout->invalidate();
    layout->activate();
    page->setProperty("settingsGroupSpacing", groupSpacing);
    page->setProperty("settingsIntraGroupMaxSpacing", maximumIntraGroupSpacing);
    page->setProperty("settingsRowSpacing", -1);
    page->setProperty("settingsHasBottomStretch", true);
}

void setSettingsGroupFixedHeight(QWidget *group)
{
    if (!group)
        return;

    auto policy = group->sizePolicy();
    policy.setVerticalPolicy(QSizePolicy::Fixed);
    group->setSizePolicy(policy);
}

int formLabelColumnWidth(QWidget *page)
{
    if (!page)
        return 0;

    const auto layouts = page->findChildren<QFormLayout *>();
    int labelColumnWidth = 0;
    for (auto *layout : layouts)
    {
        for (int row = 0; row < layout->rowCount(); ++row)
        {
            auto *item = layout->itemAt(row, QFormLayout::LabelRole);
            if (item && item->widget())
                labelColumnWidth = qMax(labelColumnWidth, item->widget()->sizeHint().width());
        }
    }
    return labelColumnWidth;
}

void clearFormLabelColumnWidths(QWidget *page)
{
    if (!page)
        return;

    const auto layouts = page->findChildren<QFormLayout *>();
    for (auto *layout : layouts)
    {
        for (int row = 0; row < layout->rowCount(); ++row)
        {
            auto *item = layout->itemAt(row, QFormLayout::LabelRole);
            if (item && item->widget())
            {
                item->widget()->setMinimumWidth(0);
                item->widget()->setMinimumHeight(0);
                item->widget()->setMaximumWidth(QWIDGETSIZE_MAX);
            }
        }
    }
}

int naturalFieldOnlyFormWidth(QFormLayout *layout)
{
    if (!layout)
        return 0;

    int widestValue = 0;
    for (int row = 0; row < layout->rowCount(); ++row)
    {
        auto *item = layout->itemAt(row, QFormLayout::FieldRole);
        if (!item)
            continue;
        if (item->widget())
            widestValue = qMax(widestValue, item->widget()->sizeHint().width());
        else
            widestValue = qMax(widestValue, item->sizeHint().width());
    }
    const QMargins margins = layout->contentsMargins();
    return margins.left() + layout->horizontalSpacing()
        + widestValue + margins.right();
}

QSize naturalLayoutSize(QWidget *widget)
{
    if (!widget)
        return {};

    widget->setMinimumSize(0, 0);
    if (auto *layout = widget->layout())
    {
        layout->invalidate();
        layout->activate();
        return layout->sizeHint().expandedTo(layout->minimumSize());
    }
    return widget->sizeHint().expandedTo(widget->minimumSizeHint());
}

int naturalTableColumnWidth(QTableWidget *table, const int column)
{
    auto *header = table->horizontalHeader();
    int width = qMax(header->minimumSectionSize(), header->sectionSizeHint(column));
    QStyleOptionViewItem option;
    option.initFrom(table->viewport());
    for (int row = 0; row < table->rowCount(); ++row)
    {
        const QModelIndex index = table->model()->index(row, column);
        width = qMax(width, table->itemDelegate()->sizeHint(option, index).width());
    }
    return width;
}

void alignFormLayouts(QWidget *page, const int labelColumnWidth)
{
    if (!page || labelColumnWidth <= 0)
        return;

    const auto layouts = page->findChildren<QFormLayout *>();

    for (auto *layout : layouts)
    {
        // Each translated form used to center its own size hint.  That made
        // every group choose a different field-column origin as label widths
        // changed between languages. A shared label column and left-aligned
        // form origin keep General and Mouse controls on one vertical grid.
        layout->setLabelAlignment(SettingsLabelAlignment);
        layout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
        layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
        if (layout->objectName() == QStringLiteral("settingsGroup4Layout"))
        {
            // Group 4 contains only value-only rows, so QFormLayout has no
            // label item from which to derive the shared field-column origin.
            // Reserve that column explicitly as the form's left inset.
            QMargins margins = layout->contentsMargins();
            margins.setLeft(labelColumnWidth);
            layout->setContentsMargins(margins);
        }
        for (int row = 0; row < layout->rowCount(); ++row)
        {
            auto *labelItem = layout->itemAt(row, QFormLayout::LabelRole);
            auto *fieldItem = layout->itemAt(row, QFormLayout::FieldRole);
            auto *spanningItem = layout->itemAt(row, QFormLayout::SpanningRole);
            if (labelItem && labelItem->widget())
            {
                // A fixed shared label width makes the right edge of the
                // translated label column an invariant of the page rather
                // than an emergent result of each form's size hint.
                labelItem->widget()->setFixedWidth(labelColumnWidth);
                if (auto *label = qobject_cast<QLabel *>(labelItem->widget()))
                {
                    label->setAlignment(SettingsLabelContentAlignment);
                    layout->setAlignment(label, Qt::AlignTop);
                }
            }

            const auto isAssociationButton = [](QLayoutItem *item) {
                return item && item->widget()
                    && item->widget()->objectName()
                        == QStringLiteral("associateFormatsButton");
            };
            auto alignValueItem = [layout](QLayoutItem *item) {
                if (!item)
                    return;
                if (item->widget())
                    layout->setAlignment(item->widget(), SettingsValueAlignment);
                else if (item->layout())
                    layout->setAlignment(item->layout(), SettingsValueAlignment);
            };
            if (isAssociationButton(fieldItem))
            {
                layout->setAlignment(fieldItem->widget(),
                                     Qt::AlignHCenter | Qt::AlignVCenter);
            }
            else
            {
                alignValueItem(fieldItem);
            }

            // A spanning row without a label is itself the value/control. The
            // association button is deliberately centered as an action, while
            // ordinary value-only rows belong to the field column.
            if (isAssociationButton(spanningItem))
            {
                layout->setAlignment(spanningItem->widget(),
                                     Qt::AlignHCenter | Qt::AlignVCenter);
            }
            else
            {
                alignValueItem(spanningItem);
            }
        }

        if (layout->objectName() == QStringLiteral("settingsGroup4Layout"))
        {
            if (auto *group = layout->parentWidget())
                group->setMinimumWidth(naturalFieldOnlyFormWidth(layout));
        }
        layout->invalidate();
        layout->activate();
    }

    page->setProperty("settingsAlignedLabelColumnWidth", labelColumnWidth);
    page->setProperty("settingsLabelAlignment", int(SettingsLabelAlignment));
    page->setProperty("settingsValueAlignment", int(SettingsValueAlignment));
}

void normalizeNamedRows(QWidget *page)
{
    if (!page)
        return;

    for (auto *layout : page->findChildren<QFormLayout *>())
    {
        for (int row = 0; row < layout->rowCount(); ++row)
        {
            auto *labelItem = layout->itemAt(row, QFormLayout::LabelRole);
            auto *fieldItem = layout->itemAt(row, QFormLayout::FieldRole);
            auto *label = labelItem ? qobject_cast<QLabel *>(labelItem->widget()) : nullptr;
            QWidget *valueHost = fieldItem ? fieldItem->widget() : nullptr;
            if (!valueHost)
                continue;

            if (!label)
            {
                valueHost->setMinimumHeight(0);
                valueHost->setMaximumHeight(QWIDGETSIZE_MAX);
                valueHost->setAttribute(Qt::WA_LayoutUsesWidgetRect, true);
                valueHost->setFixedHeight(valueHost->sizeHint().height());
                layout->setAlignment(valueHost, SettingsValueAlignment);
                continue;
            }

            // Remove a previous normalization before measuring the natural
            // post-polish heights. This makes the operation safe after a
            // font, style, palette, or translation change.
            label->setMinimumHeight(0);
            label->setMaximumHeight(QWIDGETSIZE_MAX);
            valueHost->setMinimumHeight(0);
            valueHost->setMaximumHeight(QWIDGETSIZE_MAX);
            label->setAttribute(Qt::WA_LayoutUsesWidgetRect, true);
            valueHost->setAttribute(Qt::WA_LayoutUsesWidgetRect, true);
            label->setAlignment(SettingsLabelContentAlignment);

            const int rowHeight = qMax(label->sizeHint().height(),
                                       valueHost->sizeHint().height());
            label->setFixedHeight(rowHeight);
            valueHost->setFixedHeight(rowHeight);
            layout->setAlignment(label, Qt::AlignTop);
            layout->setAlignment(valueHost, SettingsValueAlignment);
        }
    }
}

QWidget *createSettingsGroup(QWidget *parent, const int groupIndex,
                             QFormLayout **formLayout)
{
    auto *group = new QWidget(parent);
    group->setObjectName(QStringLiteral("settingsGroup%1").arg(groupIndex));
    group->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    group->setProperty("settingsGroupIndex", groupIndex);

    auto *layout = new QFormLayout(group);
    layout->setObjectName(QStringLiteral("settingsGroup%1Layout").arg(groupIndex));
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(12);
    configureSettingsForm(layout);
    *formLayout = layout;
    return group;
}

void discardLayoutItems(QLayout *layout)
{
    if (!layout)
        return;

    while (layout->count() > 0)
    {
        QLayoutItem *item = layout->takeAt(layout->count() - 1);
        if (!item)
            break;
        // Deleting a QWidgetItem does not delete the widget. The existing
        // controls are immediately reparented into their semantic groups.
        delete item;
    }
    delete layout;
}

void setNaturalControlWidths(QWidget *page)
{
    if (!page)
        return;

    const auto controls = page->findChildren<QWidget *>();
    for (auto *control : controls)
    {
        if (control->objectName() == QStringLiteral("menubarCheckbox"))
            continue;

        if (control->objectName() == QStringLiteral("associateFormatsButton"))
        {
            // The association action intentionally keeps the exact native
            // QPushButton size hint; do not turn its width into a second
            // sizing source while measuring the page.
            control->setMinimumSize(0, 0);
            continue;
        }

        if (!qobject_cast<QAbstractButton *>(control)
            && !qobject_cast<QComboBox *>(control)
            && !qobject_cast<QAbstractSpinBox *>(control))
            continue;

        if (auto *comboBox = qobject_cast<QComboBox *>(control))
            comboBox->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        control->setMinimumWidth(qMax(control->minimumWidth(),
                                      control->sizeHint().width()));
        auto sizePolicy = control->sizePolicy();
        sizePolicy.setHorizontalPolicy(QSizePolicy::Fixed);
        control->setSizePolicy(sizePolicy);
    }
}

void addValueOnlyRow(QFormLayout *layout, QWidget *value)
{
    if (!layout || !value)
        return;

    // QFormLayout::addRow(widget) creates a SpanningRole item. These controls
    // have no separate label, but their values still belong in the shared
    // field column, so place them explicitly in FieldRole.
    layout->setWidget(layout->rowCount(), QFormLayout::FieldRole, value);
}

int naturalControlWidth(QWidget *page)
{
    if (!page)
        return 0;

    int width = 0;
    for (auto *control : page->findChildren<QWidget *>())
    {
        if (control->objectName() == QStringLiteral("menubarCheckbox"))
            continue;

        if (qobject_cast<QAbstractButton *>(control)
            || qobject_cast<QComboBox *>(control)
            || qobject_cast<QAbstractSpinBox *>(control))
            width = qMax(width, control->sizeHint().width());
    }

    for (auto *layout : page->findChildren<QFormLayout *>())
    {
        if (layout->objectName() != QStringLiteral("settingsGroup4Layout"))
            continue;

        width = qMax(width, naturalFieldOnlyFormWidth(layout));
    }
    return width;
}
}

QVOptionsDialog::QVOptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::QVOptionsDialog)
{
    ui->setupUi(this);

    configureGeneralPage();

    setAttribute(Qt::WA_DeleteOnClose);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setSizeGripEnabled(false);
    setProperty("settingsVisibleShortcutRows", ShortcutsVisibleRows);
    setProperty("settingsCategoryTransitionDuration", SettingsCategoryTransitionDuration);
    setProperty("settingsCategoryTransitionActive", false);
    setProperty("settingsAdaptiveTabWidths", true);

    categorySizeAnimation = new QPropertyAnimation(this, "settingsAnimatedSize", this);
    categorySizeAnimation->setObjectName(QStringLiteral("settingsCategorySizeAnimation"));
    categorySizeAnimation->setDuration(SettingsCategoryTransitionDuration);
    categorySizeAnimation->setEasingCurve(QEasingCurve::InOutCubic);
    connect(categorySizeAnimation, &QPropertyAnimation::finished, this, [this]() {
        if (categoryTargetWidth > 0 && categoryTargetHeight > 0)
            setFixedSize(categoryTargetWidth, categoryTargetHeight);
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
            if (auto *focused = QApplication::focusWidget(); focused
                && focused->window() == this)
                focused->clearFocus();
        });
    });
    connect(ui->shortcutsTable, &QTableWidget::cellDoubleClicked, this, &QVOptionsDialog::shortcutCellDoubleClicked);
    connect(ui->langComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QVOptionsDialog::languageComboBoxCurrentIndexChanged);
    connect(ui->associateFormatsButton, &QPushButton::clicked, this, &QVOptionsDialog::associateSupportedFormats);

    QSettings settings;

    // The former Display and Miscellaneous tabs occupied indexes 0 and 1.
    // Map both to General, and shift Shortcuts/Mouse down by one.
    int selectedTab = settings.value("optionstab", 0).toInt();
    if (settings.value("optionstabversion", 0).toInt() < 2)
    {
        selectedTab = selectedTab <= 1 ? 0 : selectedTab - 1;
        settings.setValue("optionstab", selectedTab);
        settings.setValue("optionstabversion", 2);
        settings.sync();
    }
    populateCategories(selectedTab);
    populateComboBoxes();
    populateLanguages();

    if (QOperatingSystemVersion::current() < QOperatingSystemVersion(QOperatingSystemVersion::MacOS, 13))
        setWindowTitle(tr("Preferences"));

    ui->menubarCheckbox->hide();

    QString ctrlKeyName = QKeySequence(Qt::CTRL).toString(QKeySequence::NativeText).replace(QRegularExpression("\\+$"), "");
    ui->altDoubleClickLabel->setText(tr("%1 + Double Click:").arg(ctrlKeyName));
    ui->altDragLabel->setText(tr("%1 + Drag:").arg(ctrlKeyName));
    ui->altMiddleClickLabel->setText(tr("%1 + Middle Click:").arg(ctrlKeyName));
    ui->altVerticalScrollLabel->setText(tr("%1 + Vertical Scroll:").arg(ctrlKeyName));
    ui->altHorizontalScrollLabel->setText(tr("%1 + Horizontal Scroll:").arg(ctrlKeyName));

    syncSettings(false, true);
    syncShortcuts();
    updateNaturalPageSizes();
    resizeForCategory(ui->categoryTabs->currentIndex());

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

void QVOptionsDialog::configureGeneralPage()
{
    auto *legacyGeneralWidget = ui->generalScrollArea->takeWidget();
    auto *legacyMiscWidget = ui->miscScrollArea->takeWidget();
    if (!legacyGeneralWidget || !legacyMiscWidget)
        return;

    // The .ui pages retain stable control names and signal wiring. Their
    // legacy forms are replaced by one ordered General hierarchy so there is
    // exactly one source of group spacing.
    discardLayoutItems(ui->displayLayout);
    discardLayoutItems(ui->miscLayout);

    auto *generalContent = new QWidget(ui->generalScrollArea);
    generalContent->setObjectName(QStringLiteral("generalContent"));
    generalContent->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    auto *contentLayout = new QVBoxLayout(generalContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);

    auto *mouseContent = ui->mouseScrollArea->widget();
    auto *mouseLayout = mouseContent
        ? qobject_cast<QVBoxLayout *>(mouseContent->layout()) : nullptr;
    if (mouseLayout)
    {
        mouseLayout->setSpacing(0);
        for (auto *form : mouseContent->findChildren<QFormLayout *>())
            configureSettingsForm(form);
        for (auto *group : mouseContent->findChildren<QGroupBox *>())
            setSettingsGroupFixedHeight(group);
        mouseLayout->addStretch(1);
    }

    QFormLayout *groupLayout = nullptr;
    auto *group1 = createSettingsGroup(generalContent, 1, &groupLayout);
    group1->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("langComboBox")});
    groupLayout->addRow(ui->langComboLabel, ui->langComboBox);
    contentLayout->addWidget(group1);

    auto *group2 = createSettingsGroup(generalContent, 2, &groupLayout);
    group2->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("themeComboBox"),
                                     QStringLiteral("checkerboardBackgroundCheckbox")});
    groupLayout->addRow(ui->appearanceLabel, ui->themeComboBox);
    addValueOnlyRow(groupLayout, ui->checkerboardBackgroundCheckbox);
    contentLayout->addWidget(group2);

    auto *group3 = createSettingsGroup(generalContent, 3, &groupLayout);
    group3->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("smoothScalingComboBox")});
    groupLayout->addRow(ui->label_2, ui->smoothScalingComboBox);
    contentLayout->addWidget(group3);

    auto *group4 = createSettingsGroup(generalContent, 4, &groupLayout);
    group4->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("reuseWindowCheckbox"),
                                     QStringLiteral("smallImagesOneToOneCheckbox")});
    addValueOnlyRow(groupLayout, ui->reuseWindowCheckbox);
    addValueOnlyRow(groupLayout, ui->smallImagesOneToOneCheckbox);
    contentLayout->addWidget(group4);

    auto *group5 = createSettingsGroup(generalContent, 5, &groupLayout);
    group5->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("slideshowDirectionComboBox"),
                                     QStringLiteral("slideshowTimerSpinBox")});
    groupLayout->addRow(ui->label_4, ui->slideshowDirectionComboBox);
    groupLayout->addRow(ui->label_5, ui->slideshowTimerSpinBox);
    contentLayout->addWidget(group5);

    auto *group6 = createSettingsGroup(generalContent, 6, &groupLayout);
    group6->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("afterDeletionComboBox"),
                                     QStringLiteral("askDeleteCheckbox")});
    groupLayout->addRow(ui->label_10, ui->afterDeletionComboBox);
    addValueOnlyRow(groupLayout, ui->askDeleteCheckbox);
    contentLayout->addWidget(group6);

    auto *group7 = createSettingsGroup(generalContent, 7, &groupLayout);
    group7->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("updateFrequencyComboBox")});
    groupLayout->addRow(ui->updateFrequencyLabel, ui->updateFrequencyComboBox);
    contentLayout->addWidget(group7);

    auto *group8 = createSettingsGroup(generalContent, 8, &groupLayout);
    group8->setProperty("settingsItemObjectNames",
                        QStringList {QStringLiteral("associateFormatsButton")});
    // Keep the command as a direct native action row. Restoring the dialog's
    // native default state also restores the pre-regression QMacStyle paint
    // path and Return-key behavior.
    ui->associateFormatsButton->setStyleSheet(QString());
    ui->associateFormatsButton->setFlat(false);
    ui->associateFormatsButton->setAutoDefault(true);
    ui->associateFormatsButton->setDefault(true);
    ui->associateFormatsButton->setMinimumSize(0, 0);
    ui->associateFormatsButton->setAttribute(Qt::WA_LayoutUsesWidgetRect, true);
    groupLayout->setWidget(0, QFormLayout::SpanningRole,
                           ui->associateFormatsButton);
    contentLayout->addWidget(group8);
    contentLayout->addStretch(1);

    // The hidden control is not a General option, but it must survive the
    // removal of the legacy .ui page because syncSettings still owns it.
    ui->menubarCheckbox->setParent(generalContent);
    ui->menubarCheckbox->hide();

    const SettingsLayoutMetrics metrics = settingsLayoutMetrics(generalContent,
                                                                 mouseContent);
    configureSettingsGroupsLayout(generalContent, metrics.groupSpacing,
                                  metrics.maximumIntraGroupSpacing);
    configureSettingsGroupsLayout(mouseContent, metrics.groupSpacing,
                                  metrics.maximumIntraGroupSpacing);

    ui->generalScrollArea->setWidget(generalContent);
    ui->generalScrollArea->setWidgetResizable(true);
    ui->generalScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->generalScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->mouseScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->mouseScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    generalContent->adjustSize();

    // The old page is no longer a category. Its content has been absorbed
    // into General, so remove the page rather than leaving an inaccessible
    // duplicate in the stacked widget.
    delete legacyGeneralWidget;
    delete legacyMiscWidget;
    ui->stackedWidget->removeWidget(ui->miscScrollArea);
    ui->miscScrollArea->deleteLater();
}

void QVOptionsDialog::updateNaturalPageSizes()
{
    auto *generalContent = ui->generalScrollArea->widget();
    auto *mouseContent = ui->mouseScrollArea->widget();
    if (!generalContent || !mouseContent)
        return;

    ui->generalScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->generalScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->mouseScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->mouseScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    // Translation and the native modifier-key labels are final by this point.
    // Rebuild one shared form column before measuring so the window width is a
    // function of the current language rather than of the legacy .ui geometry.
    // A spanning row (for example a long checkbox or the association button)
    // can be wider than QFormLayout's aggregate size hint on macOS. Measure
    // those controls explicitly so QScrollArea receives a truthful minimum
    // width instead of clipping the localized text at the viewport edge.
    setNaturalControlWidths(generalContent);
    setNaturalControlWidths(mouseContent);
    clearFormLabelColumnWidths(generalContent);
    clearFormLabelColumnWidths(mouseContent);
    generalContent->ensurePolished();
    mouseContent->ensurePolished();
    normalizeNamedRows(generalContent);
    normalizeNamedRows(mouseContent);

    const SettingsLayoutMetrics metrics = settingsLayoutMetrics(generalContent,
                                                                 mouseContent);
    configureSettingsGroupsLayout(generalContent, metrics.groupSpacing,
                                  metrics.maximumIntraGroupSpacing);
    configureSettingsGroupsLayout(mouseContent, metrics.groupSpacing,
                                  metrics.maximumIntraGroupSpacing);

    const int labelColumnWidth = qMax(formLabelColumnWidth(generalContent),
                                      formLabelColumnWidth(mouseContent));
    alignFormLayouts(generalContent, labelColumnWidth);
    alignFormLayouts(mouseContent, labelColumnWidth);

    // QStackedWidget does not lay out never-selected pages. Activate each page
    // while measuring, then restore the user's category, so starting directly
    // on Mouse produces the same hints as reaching Mouse from General.
    const int selectedPage = ui->stackedWidget->currentIndex();
    ui->stackedWidget->setCurrentIndex(0);
    QSize generalNaturalSize = naturalLayoutSize(generalContent);
    generalNaturalSize.setWidth(qMax(generalNaturalSize.width(),
                                     naturalControlWidth(generalContent)));
    ui->stackedWidget->setCurrentIndex(2);
    QSize mouseNaturalSize = naturalLayoutSize(mouseContent);
    mouseNaturalSize.setWidth(qMax(mouseNaturalSize.width(),
                                   naturalControlWidth(mouseContent)));
    generalContent->setMinimumSize(generalNaturalSize);
    mouseContent->setMinimumSize(mouseNaturalSize);
    generalContent->setProperty("settingsNaturalContentSize", generalNaturalSize);
    mouseContent->setProperty("settingsNaturalContentSize", mouseNaturalSize);

    auto *table = ui->shortcutsTable;
    ui->stackedWidget->setCurrentIndex(1);
    table->ensurePolished();
    table->setWordWrap(false);
    table->setMinimumWidth(0);
    table->setMaximumWidth(QWIDGETSIZE_MAX);
    auto *header = table->horizontalHeader();
    header->setStretchLastSection(false);
    // QHeaderView::ResizeToContents can defer its first resize until a hidden
    // table is shown, which would make the pre-show window measurement use the
    // legacy .ui section geometry. Query the header and delegate size hints
    // synchronously and freeze them for the initial page measurement.
    header->setSectionResizeMode(QHeaderView::Fixed);
    const int actionColumnWidth = naturalTableColumnWidth(table, 0);
    const int shortcutsColumnWidth = naturalTableColumnWidth(table, 1);
    const int equalShortcutColumnWidth = qMax(actionColumnWidth,
                                               shortcutsColumnWidth);
    header->resizeSection(0, equalShortcutColumnWidth);
    header->resizeSection(1, equalShortcutColumnWidth);
    table->resizeRowsToContents();
    table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    int visibleRowsHeight = 0;
    const int visibleRows = qMin(ShortcutsVisibleRows, table->rowCount());
    for (int row = 0; row < visibleRows; ++row)
        visibleRowsHeight += qMax(1, table->rowHeight(row));
    const int tableHeight = table->horizontalHeader()->height()
            + visibleRowsHeight + 2 * table->frameWidth();
    table->setFixedHeight(qMax(1, tableHeight));

    // At the 16-row height, the remaining rows require the vertical scrollbar.
    // Account for its style-provided width and the frame directly. Resizing a
    // hidden table to infer this value is not reliable because its parent
    // layout still owns the table geometry until first presentation.
    // Both columns share one natural width. An even total minimizes the
    // remainder after Qt distributes the viewport between the two Stretch
    // sections.
    const int headerWidth = 2 * equalShortcutColumnWidth;
    const int tableChromeWidth = 2 * table->frameWidth()
            + (table->rowCount() > visibleRows
               ? table->verticalScrollBar()->sizeHint().width() : 0);
    const QMargins shortcutMargins = ui->shortcutsLayout->contentsMargins();
    const int measuredShortcutsWidth = shortcutMargins.left() + headerWidth
            + tableChromeWidth + shortcutMargins.right();
    // QHeaderView distributes an odd viewport remainder between Stretch
    // sections. Normalize the page width to minimize that remainder on Cocoa
    // styles whose scrollbar metrics differ by one pixel between the hidden
    // measurement pass and the visible table; the runtime contract accepts
    // the resulting one-pixel integer rounding.
    const int shortcutsNaturalWidth = measuredShortcutsWidth
            + measuredShortcutsWidth % 2;

    const int generalNaturalWidth = generalNaturalSize.width()
            + 2 * ui->generalScrollArea->frameWidth();
    const int mouseNaturalWidth = mouseNaturalSize.width()
            + 2 * ui->mouseScrollArea->frameWidth();
    settingsTabWidths = {generalNaturalWidth, shortcutsNaturalWidth,
                         mouseNaturalWidth};

    QVariantList tabWidths;
    for (const int width : settingsTabWidths)
        tabWidths.append(width);

    const QMargins dialogMargins = ui->verticalLayout->contentsMargins();
    const int selectedCategory = qBound(0, selectedPage,
                                        settingsTabWidths.size() - 1);
    const int selectedPageWidth = settingsTabWidths.value(selectedCategory);
    ui->stackedWidget->setFixedWidth(selectedPageWidth);

    // Both columns fill the Shortcuts page equally. The page width is derived
    // from two equal natural widths above, so translated action names and
    // shortcut values remain visible without horizontal scrolling.
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setStretchLastSection(false);
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    settingsDialogWidth = qMax(1, selectedPageWidth
                               + dialogMargins.left() + dialogMargins.right());
    setProperty("settingsFixedWidth", settingsDialogWidth);
    setProperty("settingsNaturalPageWidth", selectedPageWidth);
    setProperty("settingsTabWidths", tabWidths);
    setProperty("settingsTabWidth", selectedPageWidth);
    setProperty("settingsGeneralNaturalWidth", generalNaturalWidth);
    setProperty("settingsMouseNaturalWidth", mouseNaturalWidth);
    setProperty("settingsShortcutsNaturalWidth", shortcutsNaturalWidth);
    setProperty("settingsShortcutHeaderWidth", headerWidth);
    setProperty("settingsShortcutColumnWidth", equalShortcutColumnWidth);
    setProperty("settingsMaximumNaturalPageWidth",
                qMax(generalNaturalWidth,
                     qMax(mouseNaturalWidth, shortcutsNaturalWidth)));
    ui->stackedWidget->setCurrentIndex(selectedPage);
    // Exact content dimensions above make scrolling unnecessary. Keeping the
    // bars off also avoids a transient overlay flash while Cocoa swaps panes.
    ui->generalScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->generalScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->mouseScrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ui->mouseScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pageMetricsReady = true;
}

void QVOptionsDialog::resizeForCategory(const int categoryIndex)
{
    if (!ui || !ui->stackedWidget || !pageMetricsReady)
        return;

    if (categoryIndex < 0 || categoryIndex >= settingsTabWidths.size())
        return;

    QWidget *page = ui->stackedWidget->widget(categoryIndex);
    if (!page)
        return;

    const int pageWidth = qMax(1, settingsTabWidths.at(categoryIndex));
    ui->stackedWidget->setFixedWidth(pageWidth);

    int pageHeight = 0;
    if (categoryIndex == 0)
    {
        auto *content = ui->generalScrollArea->widget();
        pageHeight = content->minimumHeight()
                + 2 * ui->generalScrollArea->frameWidth();
    }
    else if (categoryIndex == 1)
    {
        auto *table = ui->shortcutsTable;
        int visibleRowsHeight = 0;
        const int visibleRows = qMin(ShortcutsVisibleRows, table->rowCount());
        for (int row = 0; row < visibleRows; ++row)
            visibleRowsHeight += qMax(1, table->rowHeight(row));
        const int tableHeight = table->horizontalHeader()->height()
                + visibleRowsHeight
                + 2 * table->frameWidth();
        table->setFixedHeight(tableHeight);
        const QMargins margins = ui->shortcutsLayout->contentsMargins();
        pageHeight = margins.top() + tableHeight + margins.bottom();
    }
    else
    {
        auto *scrollArea = ui->mouseScrollArea;
        auto *content = scrollArea->widget();
        pageHeight = content->minimumHeight() + 2 * scrollArea->frameWidth();
    }

    const int currentWidth = width();
    const int currentHeight = height();
    pageHeight = qMax(1, pageHeight);
    ui->stackedWidget->setFixedHeight(pageHeight);
    const QMargins dialogMargins = ui->verticalLayout->contentsMargins();
    const int targetWidth = qMax(1, pageWidth
                                  + dialogMargins.left() + dialogMargins.right());
    const int targetHeight = qMax(1, pageHeight
                                  + dialogMargins.top() + dialogMargins.bottom());
    categoryTargetWidth = targetWidth;
    categoryTargetHeight = targetHeight;
    settingsDialogWidth = targetWidth;

    const bool shouldAnimate = isVisible()
        && (currentWidth != targetWidth || currentHeight != targetHeight)
        && categorySizeAnimation;
    if (shouldAnimate)
    {
        categorySizeAnimation->stop();
        setProperty("settingsCategoryTransitionActive", true);
        categorySizeAnimation->setStartValue(QSize(currentWidth, currentHeight));
        categorySizeAnimation->setEndValue(QSize(targetWidth, targetHeight));
        categorySizeAnimation->start();
    }
    else
    {
        if (categorySizeAnimation)
            categorySizeAnimation->stop();
        setFixedSize(targetWidth, targetHeight);
        setProperty("settingsCategoryTransitionActive", false);
    }
    setProperty("settingsCategoryIndex", categoryIndex);
    setProperty("settingsTabWidth", pageWidth);
    setProperty("settingsFixedWidth", targetWidth);
    setProperty("settingsCategoryTargetWidth", targetWidth);
    setProperty("settingsCategoryContentHeight", pageHeight);
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
    if (categoryTargetWidth > 0 && categoryTargetHeight > 0)
        setFixedSize(categoryTargetWidth, categoryTargetHeight);
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
        windowHandle()->setFlag(Qt::WindowStaysOnTopHint,
                                qvApp->foundOnTopWindow());
    NativeDialogs::applyTheme(this);
    QVCocoaFunctions::configureSettingsToolbar(windowHandle(), ui->categoryTabs);
    // The native toolbar changes the available content width. Re-measure once
    // after it exists, while the dialog is still hidden, so the first frame
    // uses the final natural height.
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
    if (event->type() == QEvent::PaletteChange
        || event->type() == QEvent::StyleChange
        || event->type() == QEvent::FontChange)
    {
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
    if (key == QStringLiteral("theme"))
    {
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
    syncComboBox(ui->altDoubleClickComboBox, "viewportaltdoubleclickaction", defaults, makeConnections);
    syncComboBox(ui->dragComboBox, "viewportdragaction", defaults, makeConnections);
    syncComboBox(ui->altDragComboBox, "viewportaltdragaction", defaults, makeConnections);
    syncComboBox(ui->middleClickComboBox, "viewportmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->altMiddleClickComboBox, "viewportaltmiddleclickaction", defaults, makeConnections);
    syncComboBox(ui->verticalScrollComboBox, "viewportverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->horizontalScrollComboBox, "viewporthorizontalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altVerticalScrollComboBox, "viewportaltverticalscrollaction", defaults, makeConnections);
    syncComboBox(ui->altHorizontalScrollComboBox, "viewportalthorizontalscrollaction", defaults, makeConnections);
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

    // Page metrics are content-derived only during initial presentation. A
    // shortcut edit changes a cell, not the settings window contract; doing a
    // second natural-size pass here used to make the dialog width depend on
    // the newly entered key sequence.
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
    addItem(Qv::MaterialIcon::Mouse, tr("Mouse"));
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

const Ui::ComboBoxItems<Qv::AfterDelete> QVOptionsDialog::mapAfterDelete() {
    return {
        { Qv::AfterDelete::MoveBack, tr("Move Back") },
        { Qv::AfterDelete::DoNothing, tr("No Action") },
        { Qv::AfterDelete::MoveForward, tr("Move Forward") }
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
