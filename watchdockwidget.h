#ifndef WATCHDOCKWIDGET_H
#define WATCHDOCKWIDGET_H

#include <QDockWidget>
#include <QTableView>
#include <QStandardItemModel>
#include <QPushButton>
#include <QDoubleSpinBox>
#include "watchsession.h"

class WatchDockWidget : public QDockWidget
{
    Q_OBJECT
public:
    WatchDockWidget(const LogicBoxTarget &target, QWidget *parent = nullptr);

    QString getPlcName() const;
    const LogicBoxTarget &getTarget() const;

    bool setEndpointText(const QString &text);
    void setTarget(const LogicBoxTarget &target);
    void addVar(const QString &varName = QString());
    void toggleConnection();
    bool isConnected() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    LogicBoxTarget target;
    QTableView *watch = nullptr;
    QStandardItemModel *watchModel = nullptr;

    void showIpEditDialog(QPushButton* anchorButton);

    QPointer<WatchSession> session;
    void receiveData(const QStringList &data);

    QStringList collectVariables() const;
    void updateSessionVariables();

    QDoubleSpinBox *intervalSpin = nullptr;
    void showContextMenu(const QPoint& pos);

    QSet<QString> forcedVar;

    void updateTableColors();

    QPushButton *connBtn = nullptr;



};

#endif // WATCHDOCKWIDGET_H
