#ifndef PLCMANAGER_H
#define PLCMANAGER_H

#include <QObject>
#include <QPointer>
#include <QHash>
#include <QHostAddress>
#include "lbprocess.h"
#include "discover.h"
#include "lbclient.h"
#include "lbendpoint.h"

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
        QString name;

        // Compatibility/display form. Networking must use resolvedEndpoint().
        // Keeping it for now avoids breaking all existing UI signals at once.
        QString ipv6;

        // Canonical network endpoint. IPv6 link-local scope lives inside
        // endpoint.address.scopeId().
        LbEndpoint endpoint;

        int slot = -1;

        bool isSlot() const { return slot != -1; }

        QString displayName() const {
            return isSlot() ? QString("%1/slot %2").arg(name).arg(slot) : name;
        }

        void setEndpoint(const LbEndpoint &value)
        {
            endpoint = value;
            if (!value.address.isNull())
                ipv6 = value.address.toString();
        }

        LbEndpoint resolvedEndpoint(quint16 fallbackPort = 502) const
        {
            if (endpoint.isValid())
                return endpoint;

            QString host = ipv6.trimmed();
            quint16 parsedPort = fallbackPort;

            // Accept [IPv6%scope]:port as a convenience for manually entered
            // Watch addresses, while the normal internal form stays QHostAddress.
            if (host.startsWith('[')) {
                const int closing = host.indexOf(']');
                if (closing > 0) {
                    const QString suffix = host.mid(closing + 1);
                    const QString addressPart = host.mid(1, closing - 1);
                    if (suffix.startsWith(':')) {
                        bool ok = false;
                        const int p = suffix.mid(1).toInt(&ok);
                        if (ok && p > 0 && p < 65536)
                            parsedPort = static_cast<quint16>(p);
                    }
                    host = addressPart;
                }
            }

            QHostAddress address;
            address.setAddress(host);

            LbEndpoint result;
            result.address = address;
            result.port = parsedPort;
            return result;
        }
    };

    void scanDevice(const QString &ipv6, const QString &name);
    void requestConfig(const QString &ipv6, const QString &name);
    void startDiscover();

    bool startFirmware(const CommandContext &ctx, const QString &filePath,
                       const QString &checkMessage,
                       const QString &startMessage,
                       const QString &lbkey = "ota");
    void stopFirmware();
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
        const LbEndpoint endpoint = ctx.resolvedEndpoint(port);
        if (!endpoint.isValid()) {
            emit errorOccurred(QString("Не определён корректный endpoint для %1 (%2). "
                                       "Для IPv6 link-local сначала выполните Discover.")
                                   .arg(ctx.displayName(), ctx.ipv6));
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
    void scanCompleted(const QString &ipv6, const QString &name,
                       const QMap<qsizetype, lbprocess::scaninfo> &scanData);
    void configReceived(const QString &ipv6, const QString &name,
                        const QString &yamlContent);
    void errorOccurred(const QString &message);
    void eventOccurred(const QString &message);
    void discoverStarting();
    void discoverCompleted(const QMap<QString, discover::lbinfo>& DiscoverMap);
    void firmwareStarted(const CommandContext &ctx, const QString &message);
    void firmwareProgressChanged(int prc);
    void firmwareFinished();
    void logStarted();
    void logFinished();
    void confCompleted(const QString &ipv6, const QString &name);
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

    // Discovery is the authority that binds a LogicBox identity to a scoped
    // link-local endpoint. Name is convenient, MAC is the stable fallback when
    // a YAML configuration has been renamed.
    QHash<QString, LbEndpoint> endpointsByName;
    QHash<QString, LbEndpoint> endpointsByMac;

    static QString normalizeMac(QString mac);
    static LbEndpoint endpointFromHost(const QString &host,
                                       quint16 endpointPort = port);
    void rememberEndpoint(const QString &name, const QString &mac,
                          const LbEndpoint &endpoint);
    LbEndpoint endpointForIdentity(const QString &name,
                                   const QString &mac = {}) const;

    void scanDeviceEndpoint(const LbEndpoint &endpoint, const QString &name);
    void requestConfigEndpoint(const LbEndpoint &endpoint, const QString &name);

    void prcOtaSender(const QString &lbhost, const QStringList &result,
                      const QString &message,
                      const QModbusDevice::Error error);
};

#endif // PLCMANAGER_H
