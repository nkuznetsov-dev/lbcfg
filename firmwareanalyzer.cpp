#include "firmwareanalyzer.h"
#include "firmwarepackage.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

namespace {

const QByteArray EspAppDescMagic = QByteArray::fromHex("3254CDAB");
const QRegularExpression LogicBoxVersionPattern(
    QStringLiteral(R"((20\d{12}-[0-9a-fA-F]{7,40}))"));

constexpr qsizetype FirmwareReadLimit = 1024 * 1024;
constexpr qsizetype VersionOffset = 16;
constexpr qsizetype VersionSize = 32;
constexpr qsizetype ProjectNameOffset = 48;
constexpr qsizetype ProjectNameSize = 32;

const QStringList KnownModules = {
    QStringLiteral("LB241CPU"),
    QStringLiteral("LB241BC"),
    QStringLiteral("LB241AI4"),
    QStringLiteral("LB241AO4"),
    QStringLiteral("LB241DI16"),
    QStringLiteral("LB241DO16"),
    QStringLiteral("LB241MG"),
    QStringLiteral("LB241CS")
};

} // namespace

firmwareAnalyzer::firmwareAnalyzer(QObject *parent)
    : QObject(parent)
{
}

firmwareAnalyzer::firmwareAnalyzer(QObject *parent, const QString &path)
    : QObject(parent),
      m_path(path)
{
    update();
}

QMap<QString, firmwareAnalyzer::fwinfo> firmwareAnalyzer::getFirmwareMap() const
{
    return m_firmwareMap;
}

QList<firmwareAnalyzer::fwinfo> firmwareAnalyzer::getRejectedFirmware() const
{
    return m_rejectedFirmware;
}

void firmwareAnalyzer::setPath(const QString &path)
{
    m_path = path;
}

QString firmwareAnalyzer::path() const
{
    return m_path;
}

QByteArray firmwareAnalyzer::getCheckSum(const QString &moduleName) const
{
    const auto it = m_firmwareMap.constFind(moduleName);
    if (it == m_firmwareMap.constEnd())
        return {};

    return it.value().checksum;
}

QString firmwareAnalyzer::normalizeVersion(const QString &version)
{
    const QString trimmed = version.trimmed();
    const QRegularExpressionMatch match = LogicBoxVersionPattern.match(trimmed);

    if (match.hasMatch())
        return match.captured(1).toLower();

    return trimmed;
}

bool firmwareAnalyzer::versionsMatch(const QString &left, const QString &right)
{
    const QString normalizedLeft = normalizeVersion(left);
    const QString normalizedRight = normalizeVersion(right);

    if (normalizedLeft.isEmpty() || normalizedRight.isEmpty())
        return false;

    if (normalizedLeft.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0
        || normalizedRight.compare(QStringLiteral("unknown"), Qt::CaseInsensitive) == 0) {
        return false;
    }

    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
}

firmwareAnalyzer::Error firmwareAnalyzer::error() const
{
    return m_error;
}

QString firmwareAnalyzer::errorString() const
{
    return m_errorString;
}

void firmwareAnalyzer::update()
{
    m_firmwareMap.clear();
    m_rejectedFirmware.clear();
    m_error = ok;
    m_errorString.clear();

    QFileInfo pathInfo(m_path);

    if (!pathInfo.exists()) {
        m_error = pathNotFound;
        m_errorString = QStringLiteral("Директория firmware не найдена: %1").arg(m_path);
        emit updated();
        return;
    }

    if (!pathInfo.isDir()) {
        m_error = pathIsNotDirectory;
        m_errorString = QStringLiteral("Путь firmware не является директорией: %1").arg(m_path);
        emit updated();
        return;
    }

    QDirIterator it(m_path, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString sourcePath = it.next();
        const QString fileName = QFileInfo(sourcePath).fileName().toLower();

        if (!fileName.endsWith(QStringLiteral(".bin"))
            && !fileName.endsWith(QStringLiteral(".bin.xz"))) {
            continue;
        }

        analyzeFile(sourcePath);
    }

    emit updated();
}

void firmwareAnalyzer::analyzeFile(const QString &sourcePath)
{
    fwinfo info;
    info.sourcePath = QFileInfo(sourcePath).absoluteFilePath();
    info.compressed = sourcePath.endsWith(QStringLiteral(".bin.xz"), Qt::CaseInsensitive);

    bool checksumOk = false;
    QString checksumError;
    info.checksum = calculateSha256(sourcePath, &checksumOk, &checksumError);

    if (!checksumOk) {
        info.err = checksumErr;
        info.errStr = checksumError;
        m_rejectedFirmware.append(info);
        return;
    }

    const FirmwarePackage::Result prepared = FirmwarePackage::prepare(sourcePath);

    if (!prepared.isOk()) {
        info.err = info.compressed ? xzErr : fileOpenErr;
        info.errStr = prepared.error;
        m_rejectedFirmware.append(info);
        return;
    }

    QString projectName;
    QString version;
    bool firmwareValid = false;
    Error metadataError = ok;
    QString metadataErrorString;

    const bool metadataOk = readFirmwareInfo(prepared.path,
                                             &projectName,
                                             &version,
                                             &firmwareValid,
                                             &metadataError,
                                             &metadataErrorString);

    info.projectName = projectName;
    info.version = version.isEmpty() ? QStringLiteral("unknown") : version;
    info.isValid = firmwareValid;

    // Analyzer needs only metadata. A temporary BIN produced from XZ must not
    // remain on disk merely because the user never starts an OTA operation.
    if (!FirmwarePackage::remove(prepared)) {
        info.err = removeTempErr;
        info.errStr = QStringLiteral("Не удалось удалить временный BIN: %1")
                          .arg(prepared.path);
        m_rejectedFirmware.append(info);
        return;
    }

    if (!metadataOk) {
        info.err = metadataError;
        info.errStr = metadataErrorString;
        m_rejectedFirmware.append(info);
        return;
    }

    QString moduleName;

    if (!projectName.isEmpty()) {
        // Primary path for ESP32 firmware: internal project_name is authoritative.
        moduleName = moduleFromProjectName(projectName);

        // If project_name exists but is unknown, do not guess from the filename.
        if (moduleName.isEmpty()) {
            info.err = unknownProjectName;
            info.errStr = QStringLiteral("Неизвестный project_name прошивки: %1")
                              .arg(projectName);
            m_rejectedFirmware.append(info);
            return;
        }
    } else {
        // Fallback for valid images without project_name. This is required for
        // LB241CS built for STM32, where ESP-IDF app metadata does not exist.
        moduleName = moduleFromFileName(sourcePath);

        if (moduleName.isEmpty()) {
            info.err = projectNameMissing;
            info.errStr = QStringLiteral(
                "В прошивке отсутствует project_name, а имя файла не совпадает "
                "с известным типом модуля");
            m_rejectedFirmware.append(info);
            return;
        }
    }

    // QMap cannot represent two different firmware files for one module without
    // silently replacing one of them. Treat that as an error instead.
    if (m_firmwareMap.contains(moduleName)) {
        fwinfo existing = m_firmwareMap.take(moduleName);
        existing.err = duplicateModule;
        existing.errStr = QStringLiteral("Найдено несколько прошивок для %1")
                              .arg(moduleName);
        m_rejectedFirmware.append(existing);

        info.err = duplicateModule;
        info.errStr = QStringLiteral("Дублирующая прошивка для %1: %2")
                          .arg(moduleName, sourcePath);
        m_rejectedFirmware.append(info);
        return;
    }

    if (info.version == QStringLiteral("unknown")) {
        info.err = versionMissing;
        info.errStr = QStringLiteral("Версия прошивки не найдена");
    } else {
        info.err = ok;
        info.errStr.clear();
    }

    m_firmwareMap.insert(moduleName, info);
}

QByteArray firmwareAnalyzer::calculateSha256(const QString &filePath,
                                             bool *okResult,
                                             QString *errorString)
{
    if (okResult)
        *okResult = false;
    if (errorString)
        errorString->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorString) {
            *errorString = QStringLiteral("Не удалось открыть файл для SHA-256: %1")
                               .arg(file.errorString());
        }
        return {};
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    constexpr qint64 BufferSize = 1024 * 1024;

    while (!file.atEnd()) {
        const QByteArray chunk = file.read(BufferSize);

        if (chunk.isEmpty() && file.error() != QFile::NoError) {
            if (errorString) {
                *errorString = QStringLiteral("Ошибка чтения файла при SHA-256: %1")
                                   .arg(file.errorString());
            }
            return {};
        }

        hash.addData(chunk);
    }

    if (okResult)
        *okResult = true;

    return hash.result();
}

bool firmwareAnalyzer::readFirmwareInfo(const QString &binPath,
                                        QString *projectName,
                                        QString *version,
                                        bool *isValid,
                                        Error *readError,
                                        QString *errorString)
{
    if (projectName)
        projectName->clear();
    if (version)
        version->clear();
    if (isValid)
        *isValid = false;
    if (readError)
        *readError = ok;
    if (errorString)
        errorString->clear();

    const FirmwarePackage::ImageType imageType = FirmwarePackage::detectImageType(binPath);

    if (imageType == FirmwarePackage::ImageType::Unknown) {
        if (readError)
            *readError = invalidFirmwareImage;
        if (errorString) {
            *errorString = QStringLiteral(
                "Файл не распознан как поддерживаемый ESP32/STM32 firmware image");
        }
        return false;
    }

    if (isValid)
        *isValid = true;

    QFile file(binPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (isValid)
            *isValid = false;
        if (readError)
            *readError = fileOpenErr;
        if (errorString) {
            *errorString = QStringLiteral("Не удалось открыть BIN: %1")
                               .arg(file.errorString());
        }
        return false;
    }

    const QByteArray data = file.read(FirmwareReadLimit);

    if (data.isEmpty() && file.error() != QFile::NoError) {
        if (isValid)
            *isValid = false;
        if (readError)
            *readError = fileOpenErr;
        if (errorString) {
            *errorString = QStringLiteral("Ошибка чтения BIN: %1")
                               .arg(file.errorString());
        }
        return false;
    }

    if (imageType == FirmwarePackage::ImageType::Stm32) {
        // STM32 raw images have no ESP-IDF esp_app_desc_t/project_name.
        // Try to recover the build version if the firmware embeds the usual
        // LogicBox timestamp-hash string; otherwise "unknown" is acceptable.
        if (version)
            *version = findEmbeddedVersion(data);
        return true;
    }

    const qsizetype appDescOffset = data.indexOf(EspAppDescMagic);
    if (appDescOffset < 0) {
        if (readError)
            *readError = appDescriptorNotFound;
        if (errorString)
            *errorString = QStringLiteral("esp_app_desc_t не найден");
        return false;
    }

    const QString detectedVersion = readFixedString(data,
                                                    appDescOffset + VersionOffset,
                                                    VersionSize);
    const QString detectedProjectName = readFixedString(data,
                                                        appDescOffset + ProjectNameOffset,
                                                        ProjectNameSize);

    if (version)
        *version = detectedVersion;
    if (projectName)
        *projectName = detectedProjectName;

    // An empty project_name is not a damaged image. Caller may use the strict
    // filename fallback for known modules.
    return true;
}

QString firmwareAnalyzer::readFixedString(const QByteArray &data,
                                          qsizetype offset,
                                          qsizetype size)
{
    if (offset < 0 || offset + size > data.size())
        return {};

    QByteArray raw = data.mid(offset, size);
    const qsizetype zeroPosition = raw.indexOf('\0');
    if (zeroPosition >= 0)
        raw.truncate(zeroPosition);

    return QString::fromLatin1(raw).trimmed();
}

QString firmwareAnalyzer::findEmbeddedVersion(const QByteArray &data)
{
    // Current LogicBox versions use YYYYMMDDhhmmss-githash, for example
    // 20260806143356-7e0c291. This is only a fallback for non-ESP firmware.
    const QRegularExpressionMatch match =
        LogicBoxVersionPattern.match(QString::fromLatin1(data));

    return match.hasMatch() ? match.captured(1) : QString();
}

QString firmwareAnalyzer::moduleFromProjectName(const QString &projectName)
{
    // Mapping obtained from actual LogicBox firmware images.
    static const QHash<QString, QString> modules = {
        {QStringLiteral("bcai"),      QStringLiteral("LB241AI4")},
        {QStringLiteral("bcao"),      QStringLiteral("LB241AO4")},
        {QStringLiteral("bcbase3"),   QStringLiteral("LB241CPU")},
        {QStringLiteral("lb241bc"),   QStringLiteral("LB241BC")},
        {QStringLiteral("lb241di16"), QStringLiteral("LB241DI16")},
        {QStringLiteral("lb241do16"), QStringLiteral("LB241DO16")},
        {QStringLiteral("lb241mg"),   QStringLiteral("LB241MG")}
    };

    return modules.value(projectName.trimmed().toLower());
}

QString firmwareAnalyzer::moduleFromFileName(const QString &filePath)
{
    QString fileName = QFileInfo(filePath).fileName();

    if (fileName.endsWith(QStringLiteral(".bin.xz"), Qt::CaseInsensitive))
        fileName.chop(7);
    else if (fileName.endsWith(QStringLiteral(".bin"), Qt::CaseInsensitive))
        fileName.chop(4);

    // Filename fallback is intentionally strict: only an exact known module
    // name is accepted. It is used only when valid firmware has no project_name.
    for (const QString &module : KnownModules) {
        if (fileName.compare(module, Qt::CaseInsensitive) == 0)
            return module;
    }

    return {};
}
