#include "watchsession.h"
#include "logmanager.h"


WatchSession::WatchSession(const plcManager::CommandContext &ctx,
                           const QStringList &arg,
                           QObject *parent)
    : QObject{parent}, m_key(ctx.target.routeKey())
{
    const LbEndpoint endpoint = ctx.target.endpoint;
    debugApp() << "WatchSession::Starting process for:"
               << m_key << endpoint.displayString();

    QStringList m_arg = {"get"};
    m_arg.append(arg);
    lbc = new LBclient(this, m_arg);
    lbc->setEndpoint(endpoint);
    lbc->setTimeOut(t);

    connect(lbc, &LBclient::ExecuteCompletedJson, this, [this]
            (const QString& lbhost, const QJsonObject& Qjo,
             const QString& message, const QModbusDevice::Error error){
        Q_UNUSED(lbhost);
        if (error == QModbusDevice::NoError){
            QStringList result;
            QJsonObject m_qjo;
            if (lbc->getMulpipleRequest()){
                m_qjo = Qjo.value("get").toObject();
                if (Qjo.keys().contains("set") ||
                    Qjo.keys().contains("force") ||
                    Qjo.keys().contains("unforce"))
                    lbc->setQueryString(lbc->getQueryString().value(0));
            }else{
                m_qjo = Qjo;
            }

            QStringList strl = lbc->getQueryString().value(0);
            strl.removeFirst();
            for (const auto &var : strl) {
                const QJsonValue v = m_qjo.value(var);
                if (v.isDouble())
                    result.append(QString::number(v.toDouble()));
                else if (v.isString())
                    result.append(v.toString());
            }
            emit watchExeComleted(result);
        } else {
            emit watchErrorOccurred(QString("%1 -> %2").arg(m_key, message));
        }
    });

    connect(lbc, &LBclient::lbConnected, this, [this]
            (const QString& lbhost){
        debugApp() << "WatchSession::Connected:" << lbhost;
        m_connected = true;
        emit connected();
    });

    connect(lbc, &LBclient::lbDisconnect, this,
            [this, endpoint](const QString& lbhost, const QString& message,
                             const QModbusDevice::Error error){
        Q_UNUSED(lbhost);
        Q_UNUSED(error);
        debugApp() << "WatchSession::disconnect:"
                   << m_key << endpoint.displayString() << message;
        if (!message.isEmpty())
            emit watchErrorOccurred(message);
        m_connected = false;
        emit disconnected();
    });
}

WatchSession::~WatchSession()
{
}

void WatchSession::start()
{
    if (lbc)
        lbc->Execute();
}

void WatchSession::stop()
{
    if (lbc)
        lbc->lbDisconnectDevice();
}

bool WatchSession::isConnected() const
{
    return m_connected;
}

void WatchSession::setQuery(const QStringList &arg)
{
    QStringList m_arg = {"get"};
    m_arg.append(arg);
    lbc->setQueryString(m_arg);
}

void WatchSession::setQuery(const std::initializer_list<QStringList> &qstr_list)
{
    lbc->setQueryString(qstr_list);
}

void WatchSession::setTimeOut(const int time)
{
    lbc->setTimeOut(time);
}
