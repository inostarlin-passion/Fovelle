#ifndef QVSETTINGSWIDGETS_H
#define QVSETTINGSWIDGETS_H

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QWidget>

class QVSettingsGroup : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *event) override;
};

class QVSettingsPopup : public QComboBox
{
public:
    using QComboBox::QComboBox;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showPopup() override;
    void hidePopup() override;

private:
    bool popupOpen{ false };
};

class QVSettingsSwitch : public QCheckBox
{
public:
    explicit QVSettingsSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool hitButton(const QPoint &position) const override;
    void paintEvent(QPaintEvent *event) override;
};

class QVSettingsNumber : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;

protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif
