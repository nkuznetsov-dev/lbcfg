#ifndef FIRMWAREPACKAGE_H
#define FIRMWAREPACKAGE_H

#include <QString>

class FirmwarePackage
{
public:
    enum class ImageType {
        Unknown = 0,
        Esp32,
        Stm32
    };

    struct Result {
        QString path;
        QString error;
        bool temporary = false;

        bool isOk() const { return error.isEmpty() && !path.isEmpty(); }
    };

    static Result prepare(const QString &sourcePath);

    // Removes only files created by prepare(). Source firmware files are never removed.
    static bool remove(const Result &result);

    // Kept for compatibility with existing call sites.
    static void cleanup(const Result &result);

    // Lightweight firmware image validation used both by FirmwarePackage and
    // firmwareAnalyzer. ESP32 is recognized by image magic 0xE9, STM32 by a
    // plausible Cortex-M vector table.
    static ImageType detectImageType(const QString &path);
    static bool isValidImage(const QString &path);

private:
    static Result decompressXz(const QString &sourcePath);
};

#endif // FIRMWAREPACKAGE_H
