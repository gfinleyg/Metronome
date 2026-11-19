#include "MainWindow.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QScreen>
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
    QCommandLineOption maximized("maximized", "Start with the window maximized.");
    QCommandLineOption beats("beats", "Diagnose with this many beats in the first measure.", "n");
    QCommandLineOption controlsOpacity("controls-opacity", "Diagnose with the controls window open at this opacity.", "percent");
    QCommandLineOption screenIndex("screen", "Start on this monitor.", "index");
    QCommandLineOption docked("docked", "Diagnose with the controls docked under the notes.");
    QCommandLineOption trainer("trainer", "Diagnose with the speed trainer on.");
    QCommandLineOption overlap("overlap", "Diagnose with a 3/4 second measure playing over the first: bar or beat.", "kind");
    parser.addOptions({diagnose, seconds, screenshot, twoMeasures, maximized, beats, controlsOpacity, screenIndex, docked, trainer, overlap});
    parser.process(app);

    DiagnoseOptions options;
    options.csvPath = parser.value(diagnose);
    options.seconds = parser.value(seconds).toDouble();
    options.screenshotPath = parser.value(screenshot);
    options.twoMeasures = parser.isSet(twoMeasures);
    options.beats = parser.value(beats).toInt();
    options.controlsOpacity = parser.value(controlsOpacity).toInt();
    options.docked = parser.isSet(docked);
    options.trainer = parser.isSet(trainer);
    if (parser.value(overlap) == "bar") options.overlap = OverlapSameBar;
    if (parser.value(overlap) == "beat") options.overlap = OverlapSameBeat;

    MainWindow window(options);
    if (parser.isSet(screenIndex)) {
        const QList<QScreen*> screens = QGuiApplication::screens();
        const int index = parser.value(screenIndex).toInt();
        if (index >= 0 && index < screens.size()) window.move(screens[index]->availableGeometry().topLeft());
    }
    parser.isSet(maximized) ? window.showMaximized() : window.show();
    return app.exec();
}
