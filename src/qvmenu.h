#ifndef QVMENU_H
#define QVMENU_H

#include <QMenu>
#include <QEvent>

class QVMenu : public QMenu
{
    Q_OBJECT
public:
    using QMenu::QMenu; // Inherit QMenu constructors

protected:
    bool event(QEvent *e) override;
};

#endif // QVMENU_H
