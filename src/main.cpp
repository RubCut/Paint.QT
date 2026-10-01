#include "ui/MainWindow.h"
#include "resources/Icons.h"

#include "pnq_version.h"

#include <QApplication>
#include <QLibraryInfo>
#include <QCommandLineParser>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QSplashScreen>
#include <QTranslator>
#include <QDir>

int main(int argc, char* argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral(PNQ_APP_NAME));
    QApplication::setApplicationDisplayName(QStringLiteral(PNQ_APP_NAME));
    QApplication::setOrganizationName(QStringLiteral("PaintQT"));
    QApplication::setApplicationVersion(QString::fromLatin1(PNQ_VERSION_STRING));
    // The application id is what a desktop matches .desktop files and icon
    // themes against, and what Flatpak and D-Bus activation expect.
    QGuiApplication::setDesktopFileName(QString::fromLatin1(PNQ_APP_ID));
    QApplication::setWindowIcon(pnq::Icons::app());

    // --- translations ---
    QLocale locale = QLocale::system();
    auto* qtTranslator = new QTranslator(&app);
    if (qtTranslator->load(locale, QStringLiteral("qt"), QStringLiteral("_"),
                           QLibraryInfo::path(QLibraryInfo::TranslationsPath))) {
        QApplication::installTranslator(qtTranslator);
    }
    auto* appTranslator = new QTranslator(&app);
    if (appTranslator->load(locale, QStringLiteral("paintqt"), QStringLiteral("_"),
                            QCoreApplication::applicationDirPath()
                                + QStringLiteral("/translations"))) {
        QApplication::installTranslator(appTranslator);
    }

    // --- command line ---
    QCommandLineParser parser;
    parser.setApplicationDescription(
        QCoreApplication::translate("main", "A raster image editor with layers, brushes and effects."));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(
        QCoreApplication::translate("main", "files"),
        QCoreApplication::translate("main", "Image files to open."),
        QStringLiteral("[files...]"));
    QCommandLineOption noGui(QCoreApplication::translate("main", "nogui"),
                             QCoreApplication::translate("main", "Do not show the GUI."));
    parser.addOption(noGui);
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    if (parser.isSet(noGui)) {
        // Headless mode: report the version and exit (useful for smoke tests).
        for (const QString& f : files)
            qInfo("would open %s", qPrintable(f));
        return 0;
    }

    // --- main window ---
    pnq::MainWindow* window = new pnq::MainWindow;
    window->show();
    for (const QString& f : files)
        window->openFile(f);

    return app.exec();
}
