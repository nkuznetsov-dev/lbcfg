#ifndef DEVICETREEDOCKWIDGET_H
#define DEVICETREEDOCKWIDGET_H

#include <QDockWidget>
#include <QTreeView>
#include <QStandardItemModel>
#include <QString>
#include "plcmanager.h"
#include "firmwareanalyzer.h"

class DeviceTreeDockWidget : public QDockWidget
{
    Q_OBJECT
public:
    explicit DeviceTreeDockWidget(QWidget *parent = nullptr);
    void updateDevice(const QString& ipv6, const QString& name,
                      const QMap<qsizetype,lbprocess::scaninfo>& scan);
    bool containsName(const QString& name);

signals:
    void requestConfig(const QString& ipv6, const QString& name);
    void requestUpdate(const QString& ipv6, const QString& name);
    // void requestFlash(const plcManager::CommandContext &ctx);
    void requestFlashAll(const plcManager::CommandContext &ctx);
    void requestFboot(const plcManager::CommandContext &ctx);

private slots:
    void showContextMenu(const QPoint& pos);
    void onTreeExpanded(const QModelIndex &index);

private:
    enum ItemRole {
        ModuleTypeRole = Qt::UserRole + 1,
        InstalledVersionRole,
        ModuleItemRole,
        VersionInfoRole
    };

    plcManager *lbplc = nullptr;
    QTreeView *treeView = nullptr;
    QStandardItemModel *treeModel = nullptr;
    firmwareAnalyzer *m_firmwareAnalyzer = nullptr;

    bool m_firmwareLoaded = false;
    bool m_repositoryPromptDeclined = false;
    QString m_repositoryRoot;

    QStandardItem *findPlcRoot(const QString& ipv6);
    QStandardItem *versionInfoItem(QStandardItem *moduleItem) const;

    bool ensureFirmwareRepository();
    bool chooseFirmwareRepository();
    bool loadFirmwareRepository(const QString &repositoryRoot, bool showErrors);

    QString settingsFilePath() const;
    QString savedFirmwareRepository() const;
    void saveFirmwareRepository(const QString &repositoryRoot) const;

    void updateFirmwareStatus(QStandardItem *moduleItem);
    void updateAllFirmwareStatuses();

    // inline QString toBold(const QString &text);
};

#endif // DEVICETREEDOCKWIDGET_H
