#include "firmwarepackage.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QtEndian>
#include <QUuid>

#include <array>

#include <lzma.h>

namespace {

QString lzmaErrorText(lzma_ret code)
{
    switch (code) {
    case LZMA_OK: return QStringLiteral("LZMA_OK");
    case LZMA_STREAM_END: return QStringLiteral("LZMA_STREAM_END");
    case LZMA_NO_CHECK: return QStringLiteral("LZMA_NO_CHECK");
    case LZMA_UNSUPPORTED_CHECK: return QStringLiteral("LZMA_UNSUPPORTED_CHECK");
    case LZMA_GET_CHECK: return QStringLiteral("LZMA_GET_CHECK");
    case LZMA_MEM_ERROR: return QStringLiteral("LZMA_MEM_ERROR");
    case LZMA_MEMLIMIT_ERROR: return QStringLiteral("LZMA_MEMLIMIT_ERROR");
    case LZMA_FORMAT_ERROR: return QStringLiteral("LZMA_FORMAT_ERROR");
    case LZMA_OPTIONS_ERROR: return QStringLiteral("LZMA_OPTIONS_ERROR");
    case LZMA_DATA_ERROR: return QStringLiteral("LZMA_DATA_ERROR");
    case LZMA_BUF_ERROR: return QStringLiteral("LZMA_BUF_ERROR");
    case LZMA_PROG_ERROR: return QStringLiteral("LZMA_PROG_ERROR");
    default: return QStringLiteral("LZMA_UNKNOWN_ERROR_%1").arg(static_cast<int>(code));
    }
}

bool hasEspImageMagic(const QByteArray &header)
{
    return !header.isEmpty()
        && static_cast<unsigned char>(header.at(0)) == 0xE9;
}

bool hasStm32VectorTable(const QByteArray &header)
{
    if (header.size() < 8)
        return false;

    const auto *data = reinterpret_cast<const uchar *>(header.constData());
    const quint32 initialSp = qFromLittleEndian<quint32>(data);
    const quint32 resetVector = qFromLittleEndian<quint32>(data + 4);

    // Cortex-M applications normally start with an initial stack pointer in
    // SRAM and a Thumb reset handler in internal flash.
    const bool stackValid = initialSp >= 0x20000000U && initialSp < 0x40000000U;
    const bool thumb = (resetVector & 0x1U) != 0;
    const quint32 resetAddress = resetVector & ~quint32(0x1U);
    const bool resetValid = resetAddress >= 0x08000000U && resetAddress < 0x10000000U;

    return stackValid && thumb && resetValid;
}

} // namespace

FirmwarePackage::Result FirmwarePackage::prepare(const QString &sourcePath)
{
    Result result;

    QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) {
        result.error = QStringLiteral("Файл прошивки не найден: %1").arg(sourcePath);
        return result;
    }

    if (info.suffix().compare(QStringLiteral("xz"), Qt::CaseInsensitive) == 0)
        return decompressXz(sourcePath);

    result.path = info.absoluteFilePath();
    return result;
}

bool FirmwarePackage::remove(const Result &result)
{
    // Never remove a source .bin supplied by the user. Only prepare()-created
    // temporary files have temporary == true.
    if (!result.temporary || result.path.isEmpty())
        return true;

    if (!QFile::exists(result.path))
        return true;

    return QFile::remove(result.path);
}

void FirmwarePackage::cleanup(const Result &result)
{
    remove(result);
}

FirmwarePackage::ImageType FirmwarePackage::detectImageType(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return ImageType::Unknown;

    const QByteArray header = file.read(64);

    if (hasEspImageMagic(header))
        return ImageType::Esp32;

    if (hasStm32VectorTable(header))
        return ImageType::Stm32;

    return ImageType::Unknown;
}

bool FirmwarePackage::isValidImage(const QString &path)
{
    return detectImageType(path) != ImageType::Unknown;
}

FirmwarePackage::Result FirmwarePackage::decompressXz(const QString &sourcePath)
{
    Result result;

    QFile input(sourcePath);
    if (!input.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Не удалось открыть XZ-файл: %1").arg(input.errorString());
        return result;
    }

    const QString outputPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("lbcfg-firmware-%1.bin")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));

    QSaveFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly)) {
        result.error = QStringLiteral("Не удалось создать временный BIN-файл: %1")
                           .arg(output.errorString());
        return result;
    }

    lzma_stream stream = LZMA_STREAM_INIT;
    lzma_ret ret = lzma_stream_decoder(
        &stream,
        256ULL * 1024ULL * 1024ULL,
        LZMA_CONCATENATED);

    if (ret != LZMA_OK) {
        result.error = QStringLiteral("Не удалось инициализировать XZ-декодер: %1")
                           .arg(lzmaErrorText(ret));
        return result;
    }

    struct LzmaEndGuard {
        lzma_stream *stream;
        ~LzmaEndGuard() { lzma_end(stream); }
    } guard{&stream};

    std::array<uint8_t, 64 * 1024> inputBuffer{};
    std::array<uint8_t, 64 * 1024> outputBuffer{};

    bool inputFinished = false;

    while (true) {
        if (stream.avail_in == 0 && !inputFinished) {
            const qint64 bytesRead = input.read(
                reinterpret_cast<char *>(inputBuffer.data()),
                static_cast<qint64>(inputBuffer.size()));

            if (bytesRead < 0) {
                result.error = QStringLiteral("Ошибка чтения XZ-файла: %1")
                                   .arg(input.errorString());
                return result;
            }

            if (bytesRead == 0) {
                inputFinished = true;
            } else {
                stream.next_in = inputBuffer.data();
                stream.avail_in = static_cast<size_t>(bytesRead);
            }
        }

        stream.next_out = outputBuffer.data();
        stream.avail_out = outputBuffer.size();

        const lzma_action action = inputFinished ? LZMA_FINISH : LZMA_RUN;
        ret = lzma_code(&stream, action);

        const size_t produced = outputBuffer.size() - stream.avail_out;
        if (produced > 0) {
            const qint64 written = output.write(
                reinterpret_cast<const char *>(outputBuffer.data()),
                static_cast<qint64>(produced));

            if (written != static_cast<qint64>(produced)) {
                result.error = QStringLiteral("Ошибка записи распакованной прошивки: %1")
                                   .arg(output.errorString());
                return result;
            }
        }

        if (ret == LZMA_STREAM_END)
            break;

        if (ret != LZMA_OK) {
            result.error = QStringLiteral("Ошибка распаковки XZ: %1")
                               .arg(lzmaErrorText(ret));
            return result;
        }

        if (inputFinished && stream.avail_in == 0 && produced == 0) {
            result.error = QStringLiteral("XZ-поток завершился без корректного конца данных");
            return result;
        }
    }

    if (!output.commit()) {
        result.error = QStringLiteral("Не удалось сохранить распакованный BIN-файл: %1")
                           .arg(output.errorString());
        return result;
    }

    if (!isValidImage(outputPath)) {
        QFile::remove(outputPath);
        result.error = QStringLiteral(
            "После распаковки XZ получен файл, который не похож на поддерживаемый "
            "ESP32/STM32 firmware image. Прошивка не отправлена.");
        return result;
    }

    result.path = outputPath;
    result.temporary = true;
    return result;
}
