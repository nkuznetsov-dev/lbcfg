#include "appsettings.h"

#include <QDir>
#include <QSettings>

QString AppSettings::firmwareRepositoryRoot()
{
    QSettings settings;
    const QString value = settings.value(QString::fromLatin1(FirmwareRepositoryKey)).toString().trimmed();
    if (value.isEmpty())
        return {};

    return QDir::cleanPath(QDir::fromNativeSeparators(value));
}

void AppSettings::setFirmwareRepositoryRoot(const QString &path)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed()));

    QSettings settings;
    if (normalized.isEmpty() || normalized == QStringLiteral("."))
        settings.remove(QString::fromLatin1(FirmwareRepositoryKey));
    else
        settings.setValue(QString::fromLatin1(FirmwareRepositoryKey), normalized);

    settings.sync();
}

void AppSettings::clearFirmwareRepositoryRoot()
{
    QSettings settings;
    settings.remove(QString::fromLatin1(FirmwareRepositoryKey));
    settings.sync();
}
