#include "mainwindow.h"
#include <QTranslator>
#include <QLibraryInfo>
#include <QApplication>
#include <QSettings>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Use QSettings with the platform-native backend. On Windows this keeps
    // application settings in the user registry instead of next to the EXE,
    // so rebuilding or deleting the build directory does not lose them.
    QCoreApplication::setOrganizationName(QStringLiteral("LogicBox"));
    QCoreApplication::setApplicationName(QStringLiteral("lbcfg"));
    QSettings::setDefaultFormat(QSettings::NativeFormat);

    QTranslator qtTranslator;
    if (qtTranslator.load("qtbase_ru", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        a.installTranslator(&qtTranslator);
    MainWindow w;
    w.show();
    return a.exec();
}
