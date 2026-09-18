#ifndef APPSETTINGS_H
#define APPSETTINGS_H

#include <QString>

class AppSettings
{
public:
    static QString firmwareRepositoryRoot();
    static void setFirmwareRepositoryRoot(const QString &path);
    static void clearFirmwareRepositoryRoot();

private:
    static constexpr const char *FirmwareRepositoryKey = "Firmware/repositoryRoot";
};

#endif // APPSETTINGS_H
