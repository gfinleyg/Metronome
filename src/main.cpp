#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QSettings>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    // Must be set before the application exists. Swap interval 1 locks frames to the display.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setSamples(4);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Metronome");
    QCoreApplication::setApplicationName("Metronome");
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QCommandLineParser parser;
    QCommandLineOption diagnose("diagnose", "Run silently and write frame timing to this CSV file.", "csv");
    QCommandLineOption seconds("seconds", "Diagnose duration in seconds.", "n", "10");
    QCommandLineOption screenshot("screenshot", "Save a screenshot at the end of a diagnose run.", "png");
    QCommandLineOption twoMeasures("two-measures", "Diagnose with a second measure of 6/8.");
    parser.addOptions({diagnose, seconds, screenshot, twoMeasures});
    parser.process(app);

    DiagnoseOptions options;
    options.csvPath = parser.value(diagnose);
    options.seconds = parser.value(seconds).toDouble();
    options.screenshotPath = parser.value(screenshot);
    options.twoMeasures = parser.isSet(twoMeasures);

    MainWindow window(options);
    window.show();
    return app.exec();
}
