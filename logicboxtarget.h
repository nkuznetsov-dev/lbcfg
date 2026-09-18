#ifndef LOGICBOXTARGET_H
#define LOGICBOXTARGET_H

#include <QMetaType>
#include <QString>
#include "lbendpoint.h"

struct LogicBoxTarget
{
    QString name;
    QString mac;
    LbEndpoint endpoint;

    bool isValid() const
    {
        return endpoint.isValid();
    }

    QString displayName() const
    {
        return name.trimmed().isEmpty() ? endpoint.displayString() : name;
    }

    QString normalizedMac() const
    {
        QString value = mac.trimmed().toLower();
        value.remove(':');
        value.remove('-');
        value.remove('.');
        return value;
    }

    QString routeKey() const
    {
        if (endpoint.isValid())
            return endpoint.key();

        const QString identity = normalizedMac().isEmpty()
            ? name.trimmed().toLower()
            : normalizedMac();
        return identity + QStringLiteral("|unresolved");
    }

    bool sameRoute(const LogicBoxTarget &other) const
    {
        return endpoint.displayString().compare(other.endpoint.displayString(), Qt::CaseInsensitive) == 0;
    }
};

Q_DECLARE_METATYPE(LogicBoxTarget)

#endif // LOGICBOXTARGET_H
