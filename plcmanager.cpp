#include "plcmanager.h"
#include "logmanager.h"
#include "watchsession.h"


plcManager::plcManager()
{
    qRegisterMetaType<LogicBoxTarget>("LogicBoxTarget");
    qRegisterMetaType<CommandContext>("plcManager::CommandContext");
}

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
    return LbEndpoint::fromString(host, endpointPort);
}

void plcManager::appendUniqueTarget(QList<LogicBoxTarget> &list,
                                    const LogicBoxTarget &target)
{
    for (LogicBoxTarget &existing : list) {
        if (existing.sameRoute(target)) {
            if (!target.name.trimmed().isEmpty())
                existing.name = target.name;
            if (!target.mac.trimmed().isEmpty())
                existing.mac = target.mac;
            return;
        }
    }
    list.append(target);
}

void plcManager::rememberTarget(const LogicBoxTarget &target)
{
    if (!target.isValid())
        return;

    const QString nameKey = target.name.trimmed().toLower();
    if (!nameKey.isEmpty() && nameKey.compare("noname", Qt::CaseInsensitive) != 0)
        appendUniqueTarget(targetsByName[nameKey], target);

    const QString macKey = normalizeMac(target.mac);
    if (!macKey.isEmpty() && macKey != "unknown")
        appendUniqueTarget(targetsByMac[macKey], target);
}

QList<LogicBoxTarget> plcManager::targetsForIdentity(const QString &name,
                                                         const QString &mac) const
{
    QList<LogicBoxTarget> candidates;
    const QString macKey = normalizeMac(mac);
    if (!macKey.isEmpty())
        candidates = targetsByMac.value(macKey);

    if (candidates.isEmpty() && !name.trimmed().isEmpty())
        candidates = targetsByName.value(name.trimmed().toLower());

    QList<LogicBoxTarget> valid;
    for (const LogicBoxTarget &candidate : candidates) {
        if (!candidate.isValid())
            continue;
        bool duplicate = false;
        for (const LogicBoxTarget &existing : valid) {
            if (existing.sameRoute(candidate)) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            valid.append(candidate);
    }
    return valid;
}

plcManager::ResolveStatus plcManager::resolveTarget(const QString &name,
                                                     const QString &mac,
                                                     LogicBoxTarget *target) const
{
    const QList<LogicBoxTarget> valid = targetsForIdentity(name, mac);
    if (valid.isEmpty())
        return ResolveStatus::NotFound;
    if (valid.size() > 1)
        return ResolveStatus::Ambiguous;

    if (target) {
        *target = valid.constFirst();
        if (!name.trimmed().isEmpty())
            target->name = name.trimmed();
        if (!mac.trimmed().isEmpty())
            target->mac = mac.trimmed();
    }
    return ResolveStatus::Found;
}

void plcManager::scanDevice(const QString &host, const QString &name)
{
    LogicBoxTarget target;
    target.name = name;
    target.endpoint = endpointFromHost(host, port);
    scanDevice(target);
}

void plcManager::scanDevice(const LogicBoxTarget &target)
{
    if (!target.isValid()) {
        emit errorOccurred(QString("Некорректный endpoint для %1: %2. "
                                   "IPv6 link-local должен содержать scope интерфейса.")
                               .arg(target.displayName(), target.endpoint.address.toString()));
        return;
    }

    rememberTarget(target);
    debugApp() << "plcManager::Starting process for:"
               << target.endpoint.displayString() << target.name;

    LBclient *lbc = new LBclient(this);
    if (!lbc->setEndpoint(target.endpoint)) {
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
            [target, lbc, lbproc, this]
            (const QMap<qsizetype, lbprocess::scaninfo>& scan){
                for (auto i = scan.begin(); i != scan.end(); ++i)
                    debugPLC() << i.key() << i.value();

                emit scanCompleted(target, scan);
                lbproc->deleteLater();
                lbc->deleteLater();
            });

    lbproc->run(lbprocess::scan, {"sys.serial"});
}

void plcManager::requestConfig(const QString &host, const QString &name)
{
    LogicBoxTarget target;
    target.name = name;
    target.endpoint = endpointFromHost(host, port);
    requestConfig(target);
}

void plcManager::requestConfig(const LogicBoxTarget &target)
{
    if (!target.isValid()) {
        emit errorOccurred(QString("Некорректный endpoint для %1: %2. "
                                   "IPv6 link-local должен содержать scope интерфейса.")
                               .arg(target.displayName(), target.endpoint.address.toString()));
        return;
    }

    rememberTarget(target);
    debugApp() << "plcManager::getlbcfg:"
               << target.endpoint.displayString() << target.name;

    LBclient *lbc = new LBclient(this, {"getconf"});
    if (!lbc->setEndpoint(target.endpoint)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }

    connect(lbc, &LBclient::ExecuteCompletedJson, this,
            [lbc, this, target]
            (const QString& lbhost, const QJsonObject& Qjo,
             const QString& message, const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                if(error == QModbusDevice::NoError){
                    const QString yamlContent = lbyaml::getlbconf(Qjo, lbyaml::retainY);
                    debugApp() << "# BEGIN YAML";
                    logPLC(target.name, LogCatcher::Debug, LogCatcher::wrapYes) << yamlContent;
                    debugApp() << "# END YAML";
                    emit configReceived(target, yamlContent);
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
    targetsByName.clear();
    targetsByMac.clear();
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

                for (auto it = DiscoverMap.cbegin(); it != DiscoverMap.cend(); ++it) {
                    LogicBoxTarget target;
                    target.name = it.value().name;
                    target.mac = it.value().mac;
                    target.endpoint = it.value().endpoint;
                    rememberTarget(target);
                }

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

    const LbEndpoint endpoint = ctx.target.endpoint;
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

void plcManager::startConf(const LogicBoxTarget &target,
                           const QString &yamlFilePath)
{
    debugApp() << "plcManager::startConf for" << target.name
               << target.endpoint.displayString();

    if (!target.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для ПЛК '%1'. "
                                   "Выполните Discover перед конфигурированием.")
                               .arg(target.displayName()));
        return;
    }

    LBclient *lbc = new LBclient(this, {"conf"});
    if (!lbc->prepareConfigHost(target.name, yamlFilePath)) {
        emit errorOccurred(lbc->getlbDeviceMessage());
        lbc->deleteLater();
        return;
    }
    if (!lbc->setEndpoint(target.endpoint)) {
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
            [lbc, target, this]
            (const QString& lbhost, const QString& message,
             const QModbusDevice::Error error){
                Q_UNUSED(lbhost);
                Q_UNUSED(message);
                Q_UNUSED(error);
                emit confCompleted(target);
                lbc->deleteLater();
            });

    lbc->Execute();
}

void plcManager::startConf(const QString &name, const QString &yamlFilePath)
{
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

    LogicBoxTarget target;
    const ResolveStatus status = resolveTarget(name, mac, &target);
    if (status == ResolveStatus::Ambiguous) {
        emit errorOccurred(QString("ПЛК '%1' доступен через несколько сетевых интерфейсов. "
                                   "Откройте его через Discover и повторите операцию.")
                               .arg(name));
        return;
    }
    if (status != ResolveStatus::Found) {
        emit errorOccurred(QString("Не найден сетевой endpoint для ПЛК '%1'%2. "
                                   "Выполните Discover перед конфигурированием.")
                               .arg(name,
                                    mac.isEmpty() ? QString() : QString(" (MAC %1)").arg(mac)));
        return;
    }

    startConf(target, yamlFilePath);
}

void plcManager::startFirmwareAll(const CommandContext &ctx,
                                  const QString &filePath,
                                  const QString &checkMessage,
                                  const QString &startMessage)
{
    debugApp() << "plcManager::startFirmwareAll" << ctx.target.name;

    const LbEndpoint endpoint = ctx.target.endpoint;
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
    const LbEndpoint endpoint = ctx.target.endpoint;
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

    const LbEndpoint endpoint = ctx.target.endpoint;
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
    if (!ctx.target.isValid()) {
        emit errorOccurred(QString("Не определён корректный endpoint для Watch '%1'. "
                                   "Для IPv6 link-local используйте scoped endpoint.")
                               .arg(ctx.displayName()));
        return nullptr;
    }

    const QString key = ctx.target.routeKey();
    if (activeWatchSessions.contains(key)) {
        debugApp() << "WatchSession for route" << key
                   << "already exists. Returning existing session.";
        return activeWatchSessions.value(key);
    }

    debugApp() << "Creating new WatchSession for route:" << key
               << ctx.target.endpoint.displayString();

    WatchSession *session = new WatchSession(ctx, arg, p_watchDock);
    activeWatchSessions.insert(key, session);
    emit activeWatchChanged(activeWatchSessions.keys());

    connect(session, &WatchSession::watchErrorOccurred,
            this, &plcManager::errorOccurred);
    connect(session, &QObject::destroyed, this, [this, key]() {
        activeWatchSessions.remove(key);
        emit activeWatchChanged(activeWatchSessions.keys());
        debugApp() << "WatchSession removed from manager for route:" << key;
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
