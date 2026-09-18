#ifndef FIRMWAREANALYZER_H
#define FIRMWAREANALYZER_H

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>

class firmwareAnalyzer : public QObject
{
    Q_OBJECT

public:
    enum Error {
        ok = 0,
        pathNotFound,
        pathIsNotDirectory,
        fileOpenErr,
        checksumErr,
        xzErr,
        invalidFirmwareImage,
        appDescriptorNotFound,
        projectNameMissing,
        versionMissing,
        unknownProjectName,
        unknownModuleName,
        duplicateModule,
        removeTempErr
    };
    Q_ENUM(Error)

    struct fwinfo {
        QString version = QStringLiteral("unknown");
        QString projectName;
        QString sourcePath;
        QByteArray checksum;
        bool isValid = false;
        bool compressed = false;
        Error err = ok;
        QString errStr;
    };

    explicit firmwareAnalyzer(QObject *parent = nullptr);
    firmwareAnalyzer(QObject *parent, const QString &path);
    ~firmwareAnalyzer() override = default;

    QMap<QString, fwinfo> getFirmwareMap() const;
    QList<fwinfo> getRejectedFirmware() const;

    void setPath(const QString &path);
    QString path() const;

    QByteArray getCheckSum(const QString &moduleName) const;

    // Device-reported sys.version may contain a build date/time suffix while
    // the repository metadata contains only YYYYMMDDhhmmss-githash. These
    // helpers reduce both forms to the same comparable value.
    static QString normalizeVersion(const QString &version);
    static bool versionsMatch(const QString &left, const QString &right);

    Error error() const;
    QString errorString() const;

    // Re-scan the configured firmware directory and rebuild the map.
    void update();

signals:
    void updated();

private:
    QString m_path;
    QMap<QString, fwinfo> m_firmwareMap;
    QList<fwinfo> m_rejectedFirmware;
    Error m_error = ok;
    QString m_errorString;

    void analyzeFile(const QString &sourcePath);

    static QByteArray calculateSha256(const QString &filePath,
                                      bool *ok,
                                      QString *errorString);

    static bool readFirmwareInfo(const QString &binPath,
                                 QString *projectName,
                                 QString *version,
                                 bool *isValid,
                                 Error *error,
                                 QString *errorString);

    static QString readFixedString(const QByteArray &data,
                                   qsizetype offset,
                                   qsizetype size);

    static QString findEmbeddedVersion(const QByteArray &data);
    static QString moduleFromProjectName(const QString &projectName);
    static QString moduleFromFileName(const QString &filePath);
};

#endif // FIRMWAREANALYZER_H
