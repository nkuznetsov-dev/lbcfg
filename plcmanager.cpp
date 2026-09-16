#include "plcmanager.h"
#include "logmanager.h"
#include "watchsession.h"


plcManager::plcManager()
{}

QString plcManager::normalizeMac(QString mac)
{
    mac = mac.trimmed().toLower();
    mac.remove(':');
    mac.remove('-');
    mac.remove('.');
    return mac;
}

LbEndpoint plcManager::endpointFromHost(const QString &host, quint16 endpointPort)
{
    QString value = host.trimmed();
    quint16 parsedPort = endpointPort;

    if (value.startsWith('[')) {
        const int closing = value.indexOf(']');
        if (closing > 0) {
            const QString suffix = value.mid(closing + 1);
            if (suffix.startsWith(':')) {
                bool ok = false;
                const int p = suffix.mid(1).toInt(&ok);
                if (ok && p > 0 && p < 65536)
                    parsedPort = static_cast<quint16>(p);
            }
            value = value.mid(1, closing - 1);
        }
    }

    LbEndpoint endpoint;
    endpoint.address.setAddress(value);
    endpoint.port = parsedPort;
    return endpoint;
}

void plcManager::rememberEndpoint(const QString &name, const QString &mac,
                                  const LbEndpoint &endpoint)
{
    if (!endpoint.isValid())
        return;

    if (!name.trimmed().isEmpty() && name != "noname")
        endpointsByName.insert(name.trimmed(), endpoint);

    const QString normalizedMac = normalizeMac(mac);
    if (!normalizedMac.isEmpty() && normalizedMac != "unknown")
        endpointsByMac.insert(normalizedMac, endpoint);
}

LbEndpoint plcManager::endpointForIdentity(const QString &name,
                                           const QString &mac) const
{
    const QString normalizedMac = normalizeMac(mac);
    if (!normalizedMac.isEmpty()) {
        const LbEndpoint byMac = endpointsByMac.value(normalizedMac);
        if (byMac.isValid())
            return byMac;
    }

    const LbEndpoint byName = endpointsByName.value(name.trimmed());
    if (byName.isValid())
        return byName;

    return {};
}

void plcManager::scanDevice(const QString &ipv6, const QString &name)
{
    scanDeviceEndpoint(endpointFromHost(ipv6, port), name);
}

void plcManager::scanDeviceEndpoint(const LbEndpoint &endpoint,
                                    const QString &name)
{
    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Некорректный endpoint для %1: %2. "
                                   "IPv6 link-local должен содержать scope интерфейса.")
                               .arg(name, endpoint.address.toString()));
        return;
    }

    rememberEndpoint(name, {}, endpoint);
    debugApp() << "plcManager::Starting process for:"
               << endpoint.displayString() << name;

    LBclient *lbc = new LBclient(this);
    if (!lbc->setEndpoint(endpoint)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }

    connect(lbc, &LBclient::lbDisconnect, this,
            [lbc](const QString& lbhost, const QString& message,
                  const QModbusDevice::Error error){
                Q_UNUSED(error);
                debugPLC(lbhost) << message << "disconnect";
                lbc->deleteLater();
            });

    lbprocess *lbproc = new lbprocess(this, lbc);
    connect(lbproc, &lbprocess::outMessage, this,
            [](const QString &lbstr, const QString &message,
               const QModbusDevice::Error error){
                if(error == QModbusDevice::NoError)
                    debugPLC() << lbstr;
                else
                    debugPLC() << message;
            });

    connect(lbproc, &lbprocess::scanCompleted, this,
            [endpoint, name, lbc, lbproc, this]
            (const QMap<qsizetype, lbprocess::scaninfo>& scan){
                for (auto i = scan.begin(); i != scan.end(); ++i)
                    debugPLC() << i.key() << i.value();

                emit scanCompleted(endpoint.hostString(), name, scan);
                lbproc->deleteLater();
                lbc->deleteLater();
            });

    lbproc->run(lbprocess::scan, {"sys.serial"});
}

void plcManager::requestConfig(const QString &ipv6, const QString &name)
{
    requestConfigEndpoint(endpointFromHost(ipv6, port), name);
}

void plcManager::requestConfigEndpoint(const LbEndpoint &endpoint,
                                       const QString &name)
{
    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Некорректный endpoint для %1: %2. "
                                   "IPv6 link-local должен содержать scope интерфейса.")
                               .arg(name, endpoint.address.toString()));
        return;
    }

    rememberEndpoint(name, {}, endpoint);
    debugApp() << "plcManager::getlbcfg:"
               << endpoint.displayString() << name;

    LBclient *lbc = new LBclient(this, {"getconf"});
    if (!lbc->setEndpoint(endpoint)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }

    connect(lbc, &LBclient::ExecuteCompletedJson, this,
            [lbc, this, name, endpoint]
            (const QString& lbhost, const QJsonObject& Qjo,
             const QString& message, const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                if(error == QModbusDevice::NoError){
                    const QString yamlContent = lbyaml::getlbconf(Qjo, lbyaml::retainY);
                    debugApp() << "# BEGIN YAML";
                    logPLC(name, LogCatcher::Debug, LogCatcher::wrapYes) << yamlContent;
                    debugApp() << "# END YAML";
                    emit configReceived(endpoint.hostString(), name, yamlContent);
                } else {
                    debugPLC() << message;
                    emit errorOccurred(message);
                }
                lbc->deleteLater();
            });

    lbc->Execute();
}

void plcManager::startDiscover()
{
    debugApp() << "startDiscover" << discoverRunning;
    if (discoverRunning)
        return;

    emit discoverStarting();
    discover *wgtdiscover = new discover(this);

    connect(wgtdiscover, &discover::discoverCompleted, this,
            [this, wgtdiscover]
            (const QMap<QString, discover::lbinfo>& DiscoverMap,
             const discover::discoverError error,
             const QString errorStr){
                discoverRunning = false;
                if (error != discover::NoError){
                    emit errorOccurred(errorStr);
                    wgtdiscover->deleteLater();
                    return;
                }

                for (auto it = DiscoverMap.cbegin(); it != DiscoverMap.cend(); ++it)
                    rememberEndpoint(it.value().name, it.value().mac,
                                     it.value().endpoint);

                emit discoverCompleted(DiscoverMap);
                wgtdiscover->deleteLater();
            });

    discoverRunning = true;
    wgtdiscover->execute();
}

bool plcManager::startFirmware(const CommandContext &ctx,
                               const QString &filePath,
                               const QString &checkMessage,
                               const QString &startMessage,
                               const QString &lbkey)
{
    debugApp() << "PLCManager: startFirmware slot=" << ctx.slot;

    const LbEndpoint endpoint = ctx.resolvedEndpoint(port);
    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для %1")
                               .arg(ctx.displayName()));
        return false;
    }

    if (activeOtaClient) {
        emit eventOccurred(checkMessage);
        return false;
    }

    activeOtaClient = new LBclient(this, {lbkey});
    if (!activeOtaClient->setEndpoint(endpoint)) {
        emit errorOccurred(activeOtaClient->getlbDeviceMessage());
        activeOtaClient->deleteLater();
        activeOtaClient.clear();
        return false;
    }

    activeOtaClient->setOtaFilename(filePath);
    if (ctx.slot != -1)
        activeOtaClient->setSlot(ctx.slot);

    emit firmwareStarted(ctx, startMessage);

    connect(activeOtaClient, &LBclient::ExecuteCompleted,
            this, &plcManager::prcOtaSender);
    connect(activeOtaClient, &LBclient::lbDisconnect, this,
            [this](const QString &, const QString &message,
                   const QModbusDevice::Error) {
                if (!message.isEmpty())
                    emit eventOccurred(message);
                emit firmwareFinished();
                if (activeOtaClient)
                    activeOtaClient->deleteLater();
            });

    activeOtaClient->Execute();
    return true;
}

void plcManager::stopFirmware()
{
    if (!activeOtaClient)
        return;

    QObject::disconnect(activeOtaClient, nullptr, this, nullptr);
    activeOtaClient->lbDisconnectDevice();

    if (prcActiveOtaClient)
        prcActiveOtaClient->deleteLater();
    if (activeOtaClient)
        activeOtaClient->deleteLater();

    emit firmwareFinished();
}

void plcManager::startConf(const QString &name, const QString &yamlFilePath)
{
    debugApp() << "plcManager::startConf for" << name;

    // A YAML file contains a link-local address derived from MAC, but it cannot
    // contain the host's current interface scope. Resolve the target against
    // endpoints learned by Discover instead of guessing an interface.
    lbyaml identityParser(yamlFilePath, lbyaml::file);
    if (identityParser.getErr() != lbyaml::NoError) {
        emit errorOccurred(QString("Ошибка YAML: %1").arg(identityParser.getErr()));
        return;
    }

    QString mac;
    const QMultiMap<QString, lbyaml::lbhost> hosts = identityParser.getallhostline();
    const auto values = hosts.values(name);
    if (!values.isEmpty())
        mac = values.constFirst().mac;

    const LbEndpoint endpoint = endpointForIdentity(name, mac);
    if (!endpoint.isValid()) {
        emit errorOccurred(
            QString("Не найден сетевой endpoint для ПЛК '%1'%2. "
                    "Выполните Discover перед конфигурированием, чтобы определить "
                    "IPv6 link-local scope интерфейса.")
                .arg(name,
                     mac.isEmpty() ? QString() : QString(" (MAC %1)").arg(mac)));
        return;
    }

    LBclient *lbc = new LBclient(this, {"conf"});

    // setlbHost prepares the YAML payload. Its legacy address selection is
    // immediately replaced by the authoritative scoped endpoint from Discover
    // before Execute(), so no connection is attempted with an unscoped address.
    lbc->setlbHost(name, yamlFilePath);
    if (!lbc->setEndpoint(endpoint)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }

    connect(lbc, &LBclient::ExecuteCompletedStr, this, [this]
            (const QString& lbstr, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(message);
                Q_UNUSED(error);
                if (lbstr != "OK")
                    emit errorOccurred(lbstr);
                else
                    emit eventOccurred(lbstr);
            });

    connect(lbc, &LBclient::lbDisconnect, this,
            [lbc, name, endpoint, this]
            (const QString& lbhost, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                Q_UNUSED(message);
                Q_UNUSED(error);
                emit confCompleted(endpoint.hostString(), name);
                lbc->deleteLater();
            });

    lbc->Execute();
}

void plcManager::startFirmwareAll(const CommandContext &ctx,
                                  const QString &filePath,
                                  const QString &checkMessage,
                                  const QString &startMessage)
{
    debugApp() << "plcManager::startFirmwareAll" << ctx.name;

    const LbEndpoint endpoint = ctx.resolvedEndpoint(port);
    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для %1")
                               .arg(ctx.displayName()));
        return;
    }

    if (activeOtaClient) {
        emit eventOccurred(checkMessage);
        return;
    }

    activeOtaClient = new LBclient(this);
    if (!activeOtaClient->setEndpoint(endpoint)) {
        emit errorOccurred(activeOtaClient->getlbDeviceMessage());
        activeOtaClient->deleteLater();
        activeOtaClient.clear();
        return;
    }

    prcActiveOtaClient = new lbprocess(this, activeOtaClient);
    prcActiveOtaClient->setOtaPath(filePath);

    emit firmwareStarted(ctx, startMessage);

    connect(prcActiveOtaClient, &lbprocess::outMessage, this, [this]
            (const QString& lbstr, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(message);
                Q_UNUSED(error);
                emit errorOccurred(lbstr);
            });
    connect(prcActiveOtaClient, &lbprocess::outOta,
            this, &plcManager::prcOtaSender);
    connect(activeOtaClient, &LBclient::lbDisconnect, this,
            [this](const QString &, const QString &message,
                   const QModbusDevice::Error){
                if (!message.isEmpty())
                    emit eventOccurred(message);
                emit firmwareFinished();

                if (prcActiveOtaClient)
                    prcActiveOtaClient->deleteLater();
                if (activeOtaClient)
                    activeOtaClient->deleteLater();
            });

    prcActiveOtaClient->run(lbprocess::autoota);
}

void plcManager::startRestartAll(const CommandContext &ctx)
{
    const LbEndpoint endpoint = ctx.resolvedEndpoint(port);
    debugApp() << "plcManager::startRestartAll for" << endpoint.displayString();

    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для %1")
                               .arg(ctx.displayName()));
        return;
    }

    LBclient *lbc = new LBclient(this);
    if (!lbc->setEndpoint(endpoint)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }

    lbprocess *prc = new lbprocess(this, lbc);
    connect(prc, &lbprocess::outMessage, this, [this]
            (const QString& lbstr, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(message);
                Q_UNUSED(error);
                emit eventOccurred(lbstr);
            });
    connect(lbc, &LBclient::lbDisconnect, this,
            [this, prc, ctx]
            (const QString& lbhost, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                Q_UNUSED(error);
                debugPLC() << "plcManager::startRestartAll disconnect" << message;
                emit restartAllCompleted(ctx);
                prc->deleteLater();
            });

    prc->run(lbprocess::restartall);
}

void plcManager::startLog(const CommandContext &ctx, const QString &flag)
{
    if (activeLogClient){
        debugApp() << "Log is already running, stop the current log one first";
        return;
    }

    const LbEndpoint endpoint = ctx.resolvedEndpoint(port);
    if (!endpoint.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для %1")
                               .arg(ctx.displayName()));
        return;
    }

    debugApp() << QString("plcManager::startLog for %1 slot %2")
                      .arg(endpoint.hostString()).arg(ctx.slot)
               << activeLogClient.get();

    activeLogClient = new LBclient(this, {"log", flag});
    if (ctx.slot != -1)
        activeLogClient->setSlot(ctx.slot);

    if (!activeLogClient->setEndpoint(endpoint)) {
        emit errorOccurred(activeLogClient->getlbDeviceMessage());
        activeLogClient->deleteLater();
        activeLogClient.clear();
        return;
    }

    connect(activeLogClient, &LBclient::ExecuteCompletedStr, this,
            [this, ctx](const QString& lbstr, const QString& message,
                        const QModbusDevice::Error error){
                Q_UNUSED(message);
                if (error == QModbusDevice::NoError)
                    rawPLC(ctx) << lbstr;
                else
                    emit errorOccurred(lbstr);
            });
    connect(activeLogClient, &LBclient::lbDisconnect, this,
            [this](const QString& lbhost, const QString& message,
                   const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                Q_UNUSED(error);
                if (!message.isEmpty())
                    debugPLC() << message;
                if (activeLogClient)
                    activeLogClient->deleteLater();
                emit logFinished();
            });

    emit logStarted();
    activeLogClient->Execute();
}

void plcManager::stopLog()
{
    if (!activeLogClient)
        return;
    activeLogClient->deleteLater();
    emit logFinished();
}

WatchSession *plcManager::startWatch(const CommandContext &ctx,
                                     const QStringList &arg,
                                     QObject *p_watchDock)
{
    if (activeWatchSessions.contains(ctx.name)) {
        debugApp() << "WatchSession for key" << ctx.name
                   << "already exists. Returning existing session.";
        return activeWatchSessions.value(ctx.name);
    }

    CommandContext normalized = ctx;
    normalized.setEndpoint(ctx.resolvedEndpoint(port));

    debugApp() << "Creating new WatchSession for key:" << normalized.name
               << normalized.endpoint.displayString();

    WatchSession *session = new WatchSession(normalized, arg, p_watchDock);
    activeWatchSessions.insert(normalized.name, session);
    emit activeWatchChanged(activeWatchSessions.keys());

    connect(session, &WatchSession::watchErrorOccurred,
            this, &plcManager::errorOccurred);
    connect(session, &QObject::destroyed, this, [this, normalized]() {
        activeWatchSessions.remove(normalized.name);
        emit activeWatchChanged(activeWatchSessions.keys());
        debugApp() << "WatchSession removed from manager for key:"
                   << normalized.name;
    });

    return session;
}

QStringList plcManager::activeWatchKeys() const
{
    return activeWatchSessions.keys();
}

void plcManager::prcOtaSender(const QString &lbhost,
                              const QStringList &result,
                              const QString &message,
                              const QModbusDevice::Error error)
{
    Q_UNUSED(lbhost);
    if(error == QModbusDevice::NoError){
        const int prc = static_cast<int>(result.value(1, "").toFloat());
        emit firmwareProgressChanged(prc);
    }
    if (!message.isEmpty())
        emit errorOccurred(message);
}
