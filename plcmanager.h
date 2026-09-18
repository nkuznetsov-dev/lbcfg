#ifndef PLCMANAGER_H
#define PLCMANAGER_H

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QList>
#include "lbprocess.h"
#include "discover.h"
#include "lbclient.h"
#include "logicboxtarget.h"

class WatchSession;

class plcManager : public QObject
{
    Q_OBJECT
public:
    static plcManager* instanse()
    {
        static plcManager inst;
        return &inst;
    }

    struct CommandContext {
        LogicBoxTarget target;
        int slot = -1;

        bool isSlot() const { return slot != -1; }
        QString displayName() const {
            return isSlot()
                ? QString("%1/slot %2").arg(target.displayName()).arg(slot)
                : target.displayName();
        }
    };

    enum class ResolveStatus {
        Found,
        NotFound,
        Ambiguous
    };

    void scanDevice(const LogicBoxTarget &target);
    void requestConfig(const LogicBoxTarget &target);
    void startDiscover();

    // Legacy/manual-input boundaries. Internal UI flow must use LogicBoxTarget.
    void scanDevice(const QString &host, const QString &name);
    void requestConfig(const QString &host, const QString &name);

    QList<LogicBoxTarget> targetsForIdentity(const QString &name, const QString &mac) const;
    ResolveStatus resolveTarget(const QString &name, const QString &mac,
                                LogicBoxTarget *target) const;

    bool startFirmware(const CommandContext &ctx, const QString &filePath,
                       const QString &checkMessage,
                       const QString &startMessage,
                       const QString &lbkey = "ota");
    void stopFirmware();
    void startConf(const LogicBoxTarget &target, const QString &yamlFilePath);
    void startConf(const QString &name, const QString &yamlFilePath);
    void startFirmwareAll(const CommandContext &ctx, const QString &filePath,
                          const QString &checkMessage,
                          const QString &startMessage);
    void startRestartAll (const CommandContext &ctx);
    void startFbootDownload(const CommandContext &ctx, const QString &filePath);
    void startLog (const CommandContext &ctx, const QString &flag);
    void stopLog();

    template <typename F>
    void lbc_executeCommand(const CommandContext &ctx,
                            const QStringList &args,
                            const QString &boxTitle,
                            F messageBuilder)
    {
        const LbEndpoint &endpoint = ctx.target.endpoint;
        if (!endpoint.isValid()) {
            emit errorOccurred(QString("Не определён корректный endpoint для %1. "
                                       "Для IPv6 link-local сначала выполните Discover.")
                                   .arg(ctx.displayName()));
            return;
        }

        LBclient *lbc = new LBclient(this, args);
        if (!lbc->setEndpoint(endpoint)) {
            emit errorOccurred(lbc->getlbDeviceMessage());
            lbc->deleteLater();
            return;
        }

        if (ctx.isSlot())
            lbc->setSlot(ctx.slot);

        connect(lbc, &LBclient::ExecuteCompleted, this,
                [this, lbc, ctx, boxTitle, messageBuilder]
                (const QString& lbhost, const QStringList& result,
                 const QString& message, const QModbusDevice::Error error){
                    Q_UNUSED(lbhost);
                    Q_UNUSED(message);
                    Q_UNUSED(error);
                    emit showMessage(boxTitle, messageBuilder(result));
                    lbc->deleteLater();
                });

        connect(lbc, &LBclient::lbDisconnect, this, [this]
                (const QString &lbhost, const QString &message,
                 const QModbusDevice::Error error){
                    Q_UNUSED(lbhost);
                    Q_UNUSED(error);
                    if (!message.isEmpty())
                        emit eventOccurred(message);
                });

        lbc->Execute();
    }

    WatchSession* startWatch(const CommandContext &ctx, const QStringList &arg,
                             QObject *p_watchDock = nullptr);
    QStringList activeWatchKeys() const;

signals:
    void scanCompleted(const LogicBoxTarget &target,
                       const QMap<qsizetype, lbprocess::scaninfo> &scanData);
    void configReceived(const LogicBoxTarget &target, const QString &yamlContent);
    void errorOccurred(const QString &message);
    void eventOccurred(const QString &message);
    void discoverStarting();
    void discoverCompleted(const QMap<QString, discover::lbinfo>& DiscoverMap);
    void firmwareStarted(const CommandContext &ctx, const QString &message);
    void firmwareProgressChanged(int prc);
    void firmwareFinished();
    void logStarted();
    void logFinished();
    void confCompleted(const LogicBoxTarget &target);
    void showMessage(const QString &title, const QString &message);
    void restartAllCompleted(const CommandContext &ctx);
    void activeWatchChanged(const QStringList &keys);

private:
    plcManager();
    ~plcManager() = default;
    plcManager(const plcManager&) = delete;
    plcManager& operator=(const plcManager&) = delete;

    static constexpr int port = 502;
    bool discoverRunning = false;

    QPointer<LBclient> activeOtaClient;
    QPointer<lbprocess> prcActiveOtaClient;
    QPointer<LBclient> activeLogClient;

    QMap<QString, WatchSession*> activeWatchSessions;

    // Discovery registry is a fallback for YAML/manual flows only. It stores
    // every discovered route instead of silently overwriting one interface
    // with another.
    QHash<QString, QList<LogicBoxTarget>> targetsByName;
    QHash<QString, QList<LogicBoxTarget>> targetsByMac;

    static QString normalizeMac(QString mac);
    static LbEndpoint endpointFromHost(const QString &host,
                                       quint16 endpointPort = port);
    void rememberTarget(const LogicBoxTarget &target);
    static void appendUniqueTarget(QList<LogicBoxTarget> &list,
                                   const LogicBoxTarget &target);

    void prcOtaSender(const QString &lbhost, const QStringList &result,
                      const QString &message,
                      const QModbusDevice::Error error);
};

Q_DECLARE_METATYPE(plcManager::CommandContext)

#endif // PLCMANAGER_H
