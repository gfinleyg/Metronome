#pragma once
#include "Animation.h"
#include "AudioEngine.h"
#include <QElapsedTimer>
#include <QString>
#include <QWidget>
#include <vector>

class BeatStripView;
class GaugeView;
class QCheckBox;
class QComboBox;
class QKeyEvent;
class QLabel;
class QPushButton;
class QSlider;
class ShortcutOverlay;
class TempoDial;

// Measures real timing without touching saved settings.
struct DiagnoseOptions {
    QString csvPath;
    QString screenshotPath;
    double seconds = 10;
    bool twoMeasures = false;
    bool enabled() const { return !csvPath.isEmpty(); }
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(const DiagnoseOptions& diagnose = {});
    ~MainWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct DiagnoseSample {
        double seconds, rawFrames, smoothFrames;
    };

    QWidget* buildSignatureGroup();
    QWidget* buildTempoGroup();
    QWidget* buildSoundGroup();
    QWidget* buildTrainerGroup();
    QWidget* buildSignatureRow(int measure);

    void pushConfig();
    void kick();
    void computeFrame();
    bool animating() const;

    void toggleRunning();
    void setTempo(int bpm);
    void tapTempo();
    void setSignature(int measure, int beats, int denominator);
    void cycleSignature(int measure, int delta);
    void editCustomSignature(int measure);
    void setSecondMeasure(bool on);
    void setSubdivision(int subdivision);
    void setMasterVolume(int percent);
    void setTrainer(bool on);
    void setAllSounds(int sound);
    void selectBeat(int measure, int beat);
    void moveSelection(int delta);
    void stepPitch(int measure, int beat, int delta);
    void setLevel(int measure, int beat, int level);
    void showSoundMenu(int measure, int beat, QPoint globalPos);

    bool handleKey(QKeyEvent* event);
    void showShortcuts(bool show);
    void updateShortcutBadges();
    void refreshTempoUi();
    void refreshSignatureUi();
    void refreshSubdivisionUi();
    void refreshTrainerStatus();
    void clampSelection();

    void loadSettings();
    void saveSettings() const;
    void finishDiagnose();

    SequenceConfig cfg_;
    AudioEngine engine_;
    anim::Timeline timeline_;
    anim::DisplayClock displayClock_;
    FrameState frame_;
    double lastArm_ = 0;
    double stoppedAt_ = -100;
    int audioOffsetMs_ = 0;
    int selectedMeasure_ = 0, selectedBeat_ = 0;
    int stripRows_ = 0;
    std::vector<qint64> tapTimes_;
    QElapsedTimer tapClock_;

    GaugeView* gauge_ = nullptr;
    BeatStripView* strip_ = nullptr;
    TempoDial* dial_ = nullptr;
    ShortcutOverlay* overlay_ = nullptr;
    QLabel* bpmLabel_ = nullptr;
    QLabel* tempoNameLabel_ = nullptr;
    QLabel* unitLabel_ = nullptr;
    QLabel* trainerStatus_ = nullptr;
    QLabel* volumeValue_ = nullptr;
    QLabel* offsetValue_ = nullptr;
    QLabel* audioStatus_ = nullptr;
    QPushButton* startButton_ = nullptr;
    QPushButton* tapButton_ = nullptr;
    QPushButton* trainerButton_ = nullptr;
    QCheckBox* secondCheck_ = nullptr;
    QComboBox* setAllCombo_ = nullptr;
    QSlider* volumeSlider_ = nullptr;
    QSlider* everySlider_ = nullptr;
    QSlider* addSlider_ = nullptr;
    QSlider* offsetSlider_ = nullptr;
    QWidget* signatureRow_[2] = {nullptr, nullptr};
    std::vector<QPushButton*> signatureButtons_[2];
    std::vector<QPushButton*> subdivisionButtons_;
    QWidget* subdivisionRow_ = nullptr;

    DiagnoseOptions diagnose_;
    std::vector<DiagnoseSample> diagnoseSamples_;
    std::vector<double> flashLateMs_;
    double diagnoseStart_ = 0;
    double lastFlashStart_ = -1;
};
