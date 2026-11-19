#include "MainWindow.h"
#include "BeatStripView.h"
#include "GaugeView.h"
#include "TempoDial.h"

#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <functional>

namespace {
struct Signature {
    int beats, denominator;
};
const Signature kPresets[] = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 8}, {7, 8}, {9, 8}, {12, 8}};
const char* const kSoundNames[SoundCount] = {"Woodblock", "Click", "Beep", "Cowbell", "Hi-hat"};
constexpr int kTrainerBeatStep = 4;
constexpr double kPresentFrames = 2.0;  // Render to swap to scanout, in refresh intervals.
constexpr double kStopSettleSeconds = 0.8;
constexpr double kMaxGaugeShare = 0.38;  // Of the window width, so the notes always have room.
constexpr int kMinControlsOpacity = 30;
constexpr double kMinDockedGaugeShare = 0.30;  // The metronome gives up this much width to fit docked controls, and no more.
constexpr int kFloatingDialSize = 150;
constexpr int kDockedDialSize = 110;
constexpr int kWindowMargins = 22;

const char* const kStyle = R"(
QWidget { background: #1d1f23; color: #e6e6e6; font-size: 12px; }
QLabel#heading { color: #9aa0aa; font-weight: bold; letter-spacing: 1px; }
QLabel#bpm { font-size: 30px; font-weight: bold; }
QLabel#tempoName { color: #e8a317; font-size: 15px; }
QLabel#hint { color: #9aa0aa; font-size: 11px; }
QPushButton { background: #30343b; border: 1px solid #454a53; border-radius: 4px; padding: 5px 7px; }
QPushButton:hover { background: #3b4048; }
QPushButton:checked { background: #e8a317; color: #16171a; border-color: #e8a317; font-weight: bold; }
QPushButton#start { background: #2f7d4f; border-color: #3c9c63; font-size: 15px; font-weight: bold; padding: 8px 16px; }
QPushButton#start:checked { background: #b23a33; border-color: #d24a42; color: #ffffff; }
QComboBox { background: #30343b; border: 1px solid #454a53; border-radius: 4px; padding: 4px 8px; }
QComboBox QAbstractItemView { background: #30343b; selection-background-color: #e8a317; selection-color: #16171a; }
QSlider::groove:horizontal { height: 4px; background: #454a53; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #e8a317; border-radius: 2px; }
QSlider::handle:horizontal { width: 14px; margin: -6px 0; background: #e6e6e6; border-radius: 7px; }
QCheckBox::indicator { width: 14px; height: 14px; background: #30343b; border: 1px solid #7a808b; border-radius: 3px; }
QCheckBox::indicator:checked { background: #e8a317; border-color: #e8a317; }
QMenu { background: #30343b; } QMenu::item:selected { background: #e8a317; color: #16171a; }
)";

QLabel* heading(const QString& text) {
    auto* label = new QLabel(text);
    label->setObjectName("heading");
    return label;
}

QString tempoName(int bpm) {
    if (bpm < 40) return "Grave";
    if (bpm < 60) return "Largo";
    if (bpm < 66) return "Larghetto";
    if (bpm < 76) return "Adagio";
    if (bpm < 108) return "Andante";
    if (bpm < 120) return "Moderato";
    if (bpm < 156) return "Allegro";
    if (bpm < 176) return "Vivace";
    if (bpm < 200) return "Presto";
    return "Prestissimo";
}

QString noteValueName(int denominator) {
    switch (denominator) {
    case 2: return "half notes";
    case 8: return "eighth notes";
    case 16: return "sixteenth notes";
    default: return "quarter notes";
    }
}
}  // namespace

// Holding Ctrl dims the window and labels each control with its key.
class ShortcutOverlay : public QWidget {
public:
    struct Badge {
        QRect target;
        QString text;
        bool operator==(const Badge& other) const { return target == other.target && text == other.text; }
    };

    explicit ShortcutOverlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }
    void setBadges(std::vector<Badge> badges) {
        if (badges == badges_) return;
        badges_ = std::move(badges);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(0, 0, 0, 120));
        QFont font = p.font();
        font.setBold(true);
        font.setPixelSize(12);
        p.setFont(font);
        const QFontMetrics metrics(font);
        for (const Badge& badge : badges_) {
            const int w = metrics.horizontalAdvance(badge.text) + 12;
            const int h = metrics.height() + 1;
            QRect box(0, 0, w, h);
            box.moveCenter(badge.target.center());
            box.moveLeft(std::clamp(box.left(), 2, std::max(2, width() - w - 2)));
            p.setPen(QPen(QColor("#16171a"), 1));
            p.setBrush(QColor("#ffe08a"));
            p.drawRoundedRect(box, 4, 4);
            p.setPen(QColor("#16171a"));
            p.drawText(box, Qt::AlignCenter, badge.text);
        }
    }

private:
    std::vector<Badge> badges_;
};

// Every control that is not per-beat. It floats beside the metronome rather than taking space from it.
class ControlsWindow : public QWidget {
public:
    ControlsWindow(QWidget* parent, std::function<void()> onClose) : QWidget(parent, Qt::Tool), overlay(new ShortcutOverlay(this)), onClose_(std::move(onClose)) {
        setWindowTitle("Metronome controls");
        setAttribute(Qt::WA_ShowWithoutActivating);
    }

    ShortcutOverlay* overlay;

protected:
    void closeEvent(QCloseEvent* event) override {
        event->ignore();  // Closing only hides it, so its position and contents survive.
        onClose_();
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        overlay->setGeometry(rect());
    }

private:
    std::function<void()> onClose_;
};

MainWindow::MainWindow(const DiagnoseOptions& diagnose) : diagnose_(diagnose) {
    setWindowTitle("Metronome");
    setMinimumSize(1040, 710);
    resize(1280, 800);
    setStyleSheet(kStyle);
    tapClock_.start();

    if (!diagnose_.enabled()) loadSettings();

    gauge_ = new GaugeView(&frame_);
    strip_ = new BeatStripView(&frame_, &cfg_);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->addWidget(gauge_);
    notesColumn_ = new QVBoxLayout;
    notesColumn_->addWidget(strip_, 1);
    root->addLayout(notesColumn_, 1);

    overlay_ = new ShortcutOverlay(this);
    buildControlsWindow();

    connect(gauge_, &GaugeView::controlsButtonClicked, this, [this] { setControlsOpen(!controlsOpen_); });
    connect(strip_, &BeatStripView::beatSelected, this, &MainWindow::selectBeat);
    connect(strip_, &BeatStripView::pitchStepped, this, &MainWindow::stepPitch);
    connect(strip_, &BeatStripView::levelSet, this, &MainWindow::setLevel);
    connect(strip_, &BeatStripView::soundClicked, this, &MainWindow::showSoundMenu);

    // Each finished swap schedules the next frame, which locks drawing to the display.
    connect(gauge_, &QOpenGLWidget::frameSwapped, this, [this] {
        if (animating()) kick();
    });
    auto* watchdog = new QTimer(this);  // Restarts the frame loop if a hidden window paused it.
    connect(watchdog, &QTimer::timeout, this, [this] {
        gauge_->setNoAudio(!engine_.hasDevice());
        if (animating()) kick();
    });
    watchdog->start(250);

    for (QWidget* child : findChildren<QWidget*>()) child->setFocusPolicy(Qt::NoFocus);
    setFocusPolicy(Qt::StrongFocus);
    qApp->installEventFilter(this);

    if (diagnose_.enabled()) {
        cfg_.masterVolume = 0;
        if (diagnose_.beats > 0) cfg_.measure[0].beats = std::clamp(diagnose_.beats, 1, kMaxBeats);
        cfg_.trainerEnabled = diagnose_.trainer;
        if (diagnose_.overlap != OverlapOff) {
            cfg_.secondMeasure = true;
            cfg_.overlap = diagnose_.overlap;
            cfg_.measure[1].beats = 3;
            cfg_.measure[1].denominator = 4;
        }
        if (diagnose_.twoMeasures) {
            cfg_.secondMeasure = true;
            cfg_.measure[1].beats = 6;
            cfg_.measure[1].denominator = 8;
        }
    }

    refreshTempoUi();
    refreshSignatureUi();
    refreshSubdivisionUi();
    refreshTrainerStatus();
    clampSelection();
    secondCheck_->setChecked(cfg_.secondMeasure);
    signatureRow_[1]->setVisible(cfg_.secondMeasure);
    overlapRow_->setVisible(cfg_.secondMeasure);
    refreshOverlapUi();
    trainerButton_->setChecked(cfg_.trainerEnabled);
    volumeSlider_->setValue(int(std::round(cfg_.masterVolume * 100)));
    everySlider_->setValue(cfg_.trainerEveryBeats / kTrainerBeatStep);
    addSlider_->setValue(cfg_.trainerAddBpm);
    offsetSlider_->setValue(audioOffsetMs_);
    // setValue stays silent when a slider is already at that value, which would leave its label empty.
    for (QSlider* slider : {volumeSlider_, everySlider_, addSlider_, offsetSlider_}) emit slider->valueChanged(slider->value());
    measureDockWidth();
    pushConfig();

    // Controls only open once the window they float over is on screen.
    bool openControls = diagnose_.controlsOpacity > 0 || diagnose_.docked;
    int opacity = diagnose_.controlsOpacity > 0 ? diagnose_.controlsOpacity : 100;
    dockWanted_ = diagnose_.docked;
    if (!diagnose_.enabled()) {
        QSettings s;
        openControls = s.value("controls/open", true).toBool();
        opacity = s.value("controls/opacity", 100).toInt();
        dockWanted_ = s.value("controls/docked", false).toBool();
        const QByteArray geometry = s.value("controls/geometry").toByteArray();
        if (!geometry.isEmpty()) controlsPlaced_ = controls_->restoreGeometry(geometry);
    }
    opacitySlider_->setValue(opacity);
    emit opacitySlider_->valueChanged(opacitySlider_->value());
    if (openControls) QTimer::singleShot(0, this, [this] { setControlsOpen(true); });

    if (diagnose_.enabled()) {
        QTimer::singleShot(500, this, [this] {
            diagnoseStart_ = AudioEngine::nowSeconds();
            toggleRunning();
        });
        QTimer::singleShot(int(diagnose_.seconds * 1000) + 500, this, &MainWindow::finishDiagnose);
    }
}

MainWindow::~MainWindow() { qApp->removeEventFilter(this); }

void MainWindow::buildControlsWindow() {
    controls_ = new ControlsWindow(this, [this] { setControlsOpen(false); });
    auto* column = new QVBoxLayout(controls_);
    column->setSizeConstraint(QLayout::SetFixedSize);
    column->setContentsMargins(12, 12, 12, 12);

    // The same widgets serve both places: a column when floating, a row when docked.
    const auto stack = [](QWidget* top, QWidget* bottom) {
        auto* host = new QWidget;
        auto* layout = new QVBoxLayout(host);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);
        layout->addWidget(top);
        layout->addWidget(bottom);
        return host;
    };
    controlsBody_ = new QWidget;
    controlsBodyLayout_ = new QBoxLayout(QBoxLayout::TopToBottom, controlsBody_);
    controlsBodyLayout_->setContentsMargins(0, 0, 0, 0);
    QWidget* transport = buildTransportRow();
    controlsBodyLayout_->addWidget(stack(transport, buildTempoGroup()), 0, Qt::AlignTop);
    controlsBodyLayout_->addWidget(buildSignatureGroup(), 0, Qt::AlignTop);
    controlsBodyLayout_->addWidget(buildSoundGroup(), 0, Qt::AlignTop);
    QWidget* trainer = buildTrainerGroup();
    controlsBodyLayout_->addWidget(stack(trainer, buildWindowGroup()), 0, Qt::AlignTop);
    column->addWidget(controlsBody_);
    dial_->setMinimumSize(kFloatingDialSize, kFloatingDialSize);
}

// Tries the docked shape, so the window knows how wide it must be to dock. The labels need their text
// by now, because their text sets the width. The trainer's longest status is measured for the same reason.
void MainWindow::measureDockWidth() {
    const QString status = trainerStatus_->text();
    trainerStatus_->setText("Starts counting when you press Start");
    controlsBody_->ensurePolished();
    controlsBodyLayout_->setDirection(QBoxLayout::LeftToRight);
    controlsBodyLayout_->setSpacing(18);
    dial_->setMinimumSize(kDockedDialSize, kDockedDialSize);
    signatureRow_[1]->hide();
    overlapRow_->hide();
    opacityRow_->hide();
    dockMinWidth_ = controlsBody_->minimumSizeHint().width() + 8;
    controlsBodyLayout_->setDirection(QBoxLayout::TopToBottom);
    controlsBodyLayout_->setSpacing(10);
    dial_->setMinimumSize(kFloatingDialSize, kFloatingDialSize);
    signatureRow_[1]->setVisible(cfg_.secondMeasure);
    overlapRow_->setVisible(cfg_.secondMeasure);
    opacityRow_->show();
    trainerStatus_->setText(status);
}

QWidget* MainWindow::buildTransportRow() {
    auto* host = new QWidget;
    auto* buttons = new QHBoxLayout(host);
    buttons->setContentsMargins(0, 0, 0, 0);
    tapButton_ = new QPushButton("TAP");
    connect(tapButton_, &QPushButton::pressed, this, &MainWindow::tapTempo);
    startButton_ = new QPushButton("▶  START");
    startButton_->setObjectName("start");
    startButton_->setCheckable(true);
    connect(startButton_, &QPushButton::clicked, this, &MainWindow::toggleRunning);
    buttons->addWidget(tapButton_, 1);
    buttons->addWidget(startButton_, 2);
    return host;
}

QWidget* MainWindow::buildWindowGroup() {
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(heading("CONTROLS WINDOW"), 0, 0);
    dockButton_ = new QPushButton("Dock under notes");
    dockButton_->setCheckable(true);
    connect(dockButton_, &QPushButton::clicked, this, &MainWindow::setControlsDocked);
    grid->addWidget(dockButton_, 1, 0);
    dockHint_ = new QLabel;
    dockHint_->setObjectName("hint");
    dockHint_->hide();
    grid->addWidget(dockHint_, 2, 0);

    opacityRow_ = new QWidget;
    auto* row = new QHBoxLayout(opacityRow_);
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(new QLabel("Opacity"));
    opacitySlider_ = new QSlider(Qt::Horizontal);
    opacitySlider_->setRange(kMinControlsOpacity, 100);
    row->addWidget(opacitySlider_, 1);
    opacityValue_ = new QLabel;
    opacityValue_->setMinimumWidth(52);
    row->addWidget(opacityValue_);
    grid->addWidget(opacityRow_, 3, 0);
    connect(opacitySlider_, &QSlider::valueChanged, this, [this](int percent) {
        opacityValue_->setText(QString("%1%").arg(percent));
        controls_->setWindowOpacity(percent / 100.0);
    });
    return host;
}

QWidget* MainWindow::buildSignatureRow(int measure) {
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(4);
    grid->addWidget(new QLabel(QString::number(measure + 1)), 0, 0, 2, 1);
    int index = 0;
    for (const Signature& s : kPresets) {
        auto* button = new QPushButton(QString("%1/%2").arg(s.beats).arg(s.denominator));
        button->setCheckable(true);
        connect(button, &QPushButton::clicked, this, [this, measure, s] { setSignature(measure, s.beats, s.denominator); });
        grid->addWidget(button, index / 4, index % 4 + 1);
        signatureButtons_[measure].push_back(button);
        ++index;
    }
    auto* custom = new QPushButton("Custom");
    custom->setCheckable(true);
    connect(custom, &QPushButton::clicked, this, [this, measure] { editCustomSignature(measure); });
    grid->addWidget(custom, 0, 5, 2, 1);
    custom->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    signatureButtons_[measure].push_back(custom);
    return host;
}

QWidget* MainWindow::buildSignatureGroup() {
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading("TIME SIGNATURE"));
    secondCheck_ = new QCheckBox("Second measure");
    connect(secondCheck_, &QCheckBox::clicked, this, &MainWindow::setSecondMeasure);
    layout->addWidget(secondCheck_);

    overlapRow_ = new QWidget;
    auto* modes = new QHBoxLayout(overlapRow_);
    modes->setContentsMargins(0, 0, 0, 0);
    modes->setSpacing(4);
    modes->addWidget(new QLabel("Plays"));
    const char* const names[OverlapCount] = {"After", "Same bar", "Same beat"};
    const char* const tips[OverlapCount] = {
        "Measure 2 plays after measure 1.",
        "Both measures play together and each bar lasts the same time, so 3/4 over 4/4 is 3 against 4.",
        "Both measures play together on the same beat. Their downbeats drift apart and meet again later.",
    };
    for (int i = 0; i < OverlapCount; ++i) {
        auto* button = new QPushButton(names[i]);
        button->setCheckable(true);
        button->setToolTip(tips[i]);
        connect(button, &QPushButton::clicked, this, [this, i] { setOverlap(i); });
        modes->addWidget(button, 1);
        overlapButtons_.push_back(button);
    }
    layout->addWidget(overlapRow_);
    for (int m = 0; m < 2; ++m) {
        signatureRow_[m] = buildSignatureRow(m);
        layout->addWidget(signatureRow_[m]);
    }
    return host;
}

QWidget* MainWindow::buildTempoGroup() {
    auto* host = new QWidget;
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(heading("TEMPO"));

    auto* labels = new QVBoxLayout;
    bpmLabel_ = new QLabel;
    bpmLabel_->setObjectName("bpm");
    tempoNameLabel_ = new QLabel;
    tempoNameLabel_->setObjectName("tempoName");
    unitLabel_ = new QLabel;
    unitLabel_->setObjectName("hint");
    labels->addStretch(1);
    labels->addWidget(bpmLabel_);
    labels->addWidget(tempoNameLabel_);
    labels->addWidget(unitLabel_);
    auto* minus = new QPushButton("−");
    auto* plus = new QPushButton("+");
    minus->setAutoRepeat(true);
    plus->setAutoRepeat(true);
    connect(minus, &QPushButton::clicked, this, [this] { setTempo(cfg_.bpm - 1); });
    connect(plus, &QPushButton::clicked, this, [this] { setTempo(cfg_.bpm + 1); });
    auto* steppers = new QHBoxLayout;
    steppers->addWidget(minus);
    steppers->addWidget(plus);
    labels->addLayout(steppers);
    labels->addStretch(1);

    dial_ = new TempoDial(kMinBpm, kMaxBpm);
    dial_->setValue(cfg_.bpm);
    connect(dial_, &TempoDial::valueEdited, this, &MainWindow::setTempo);

    auto* row = new QHBoxLayout;
    row->addLayout(labels);
    row->addWidget(dial_, 1);
    layout->addLayout(row, 1);

    return host;
}

QWidget* MainWindow::buildSoundGroup() {
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(heading("SOUND"), 0, 0, 1, 3);

    setAllCombo_ = new QComboBox;
    setAllCombo_->addItem("Set all beats to…");
    for (const char* name : kSoundNames) setAllCombo_->addItem(name);
    connect(setAllCombo_, &QComboBox::activated, this, [this](int index) {
        if (index > 0) setAllSounds(index - 1);
        setAllCombo_->setCurrentIndex(0);
    });
    grid->addWidget(setAllCombo_, 1, 0, 1, 3);

    grid->addWidget(new QLabel("Volume"), 2, 0);
    volumeSlider_ = new QSlider(Qt::Horizontal);
    volumeSlider_->setRange(0, 100);
    connect(volumeSlider_, &QSlider::valueChanged, this, &MainWindow::setMasterVolume);
    grid->addWidget(volumeSlider_, 2, 1);
    volumeValue_ = new QLabel;
    volumeValue_->setMinimumWidth(52);
    grid->addWidget(volumeValue_, 2, 2);

    grid->addWidget(new QLabel("Subdivide"), 3, 0);
    subdivisionRow_ = new QWidget;
    auto* subs = new QHBoxLayout(subdivisionRow_);
    subs->setContentsMargins(0, 0, 0, 0);
    subs->setSpacing(4);
    const QStringList names = {"Off", "2", "3", "4"};
    for (int i = 0; i < names.size(); ++i) {
        auto* button = new QPushButton(names[i]);
        button->setCheckable(true);
        connect(button, &QPushButton::clicked, this, [this, i] { setSubdivision(i + 1); });
        subs->addWidget(button);
        subdivisionButtons_.push_back(button);
    }
    grid->addWidget(subdivisionRow_, 3, 1, 1, 2);

    grid->addWidget(new QLabel("Audio offset"), 4, 0);
    offsetSlider_ = new QSlider(Qt::Horizontal);
    offsetSlider_->setRange(-40, 400);
    connect(offsetSlider_, &QSlider::valueChanged, this, [this](int ms) {
        audioOffsetMs_ = ms;
        offsetValue_->setText(QString("%1 ms").arg(ms));
        kick();
    });
    grid->addWidget(offsetSlider_, 4, 1);
    offsetValue_ = new QLabel("0 ms");
    grid->addWidget(offsetValue_, 4, 2);
    auto* hint = new QLabel("Raise this if the light flashes before you hear the tick.");
    hint->setObjectName("hint");
    hint->setWordWrap(true);
    grid->addWidget(hint, 5, 0, 1, 3);

    return host;
}

QWidget* MainWindow::buildTrainerGroup() {
    auto* host = new QWidget;
    auto* grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->addWidget(heading("SPEED TRAINER"), 0, 0, 1, 3);

    trainerButton_ = new QPushButton("Speed trainer");
    trainerButton_->setCheckable(true);
    connect(trainerButton_, &QPushButton::clicked, this, &MainWindow::setTrainer);
    grid->addWidget(trainerButton_, 1, 0, 1, 3);

    grid->addWidget(new QLabel("Every"), 2, 0);
    everySlider_ = new QSlider(Qt::Horizontal);
    everySlider_->setRange(1, 32);
    grid->addWidget(everySlider_, 2, 1);
    auto* everyValue = new QLabel;
    everyValue->setMinimumWidth(62);
    grid->addWidget(everyValue, 2, 2);
    connect(everySlider_, &QSlider::valueChanged, this, [this, everyValue](int steps) {
        cfg_.trainerEveryBeats = steps * kTrainerBeatStep;
        everyValue->setText(QString("%1 beats").arg(cfg_.trainerEveryBeats));
        pushConfig();
    });

    grid->addWidget(new QLabel("Add"), 3, 0);
    addSlider_ = new QSlider(Qt::Horizontal);
    addSlider_->setRange(1, 20);
    grid->addWidget(addSlider_, 3, 1);
    auto* addValue = new QLabel;
    grid->addWidget(addValue, 3, 2);
    connect(addSlider_, &QSlider::valueChanged, this, [this, addValue](int bpm) {
        cfg_.trainerAddBpm = bpm;
        addValue->setText(QString("+%1 BPM").arg(bpm));
        pushConfig();
    });

    trainerStatus_ = new QLabel;
    trainerStatus_->setObjectName("hint");
    grid->addWidget(trainerStatus_, 4, 0, 1, 3);
    return host;
}

void MainWindow::pushConfig() {
    engine_.setConfig(cfg_);
    kick();
}

void MainWindow::kick() {
    computeFrame();
    gauge_->update();
    strip_->update();
}

bool MainWindow::animating() const {
    return cfg_.running || AudioEngine::nowSeconds() - stoppedAt_ < kStopSettleSeconds;
}

void MainWindow::computeFrame() {
    BeatEvent incoming;
    while (engine_.popEvent(incoming)) (incoming.layer ? layerTimeline_ : timeline_).push(incoming);

    const AudioEngine::Clock clock = engine_.clock();
    const double now = AudioEngine::nowSeconds();
    const double smooth = displayClock_.update(clock.frames, now, clock.sampleRate);

    // Aim each frame at the moment it will actually reach the screen.
    const double refreshRate = screen() ? std::max(screen()->refreshRate(), 24.0) : 60.0;
    const double presentSeconds = kPresentFrames / refreshRate;
    const double t = smooth + clock.sampleRate * (presentSeconds - audioOffsetMs_ / 1000.0);

    FrameState next;
    next.bpm = cfg_.bpm;
    if (cfg_.running) {
        if (const BeatEvent* e = timeline_.at(t)) {
            next.hasBeat = true;
            next.event = *e;
            next.sinceSec = (t - e->start) / clock.sampleRate;
            next.beatSec = e->duration / clock.sampleRate;
            next.phase = std::clamp(next.sinceSec / next.beatSec, 0.0, 1.0);
            next.armPosition = anim::armPosition(e->side, next.phase);
            next.bpm = e->bpm;
            if (diagnose_.enabled() && e->start != lastFlashStart_) {
                lastFlashStart_ = e->start;
                flashLateMs_.push_back(next.sinceSec * 1000.0);
            }
            if (e->overlap != OverlapOff) {
                if (const BeatEvent* layer = layerTimeline_.at(t)) {
                    next.hasLayerBeat = true;
                    next.layerEvent = *layer;
                    next.layerSinceSec = (t - layer->start) / clock.sampleRate;
                    next.layerBeatSec = layer->duration / clock.sampleRate;
                    next.layerPhase = std::clamp(next.layerSinceSec / next.layerBeatSec, 0.0, 1.0);
                    if (diagnose_.enabled() && layer->start != lastLayerFlashStart_) {
                        lastLayerFlashStart_ = layer->start;
                        flashLateMs_.push_back(next.layerSinceSec * 1000.0);
                    }
                }
            }
            // Show speed trainer increases as they are heard, unless the user has just changed the tempo.
            if (e->bpmRevision == cfg_.bpmRevision && e->bpm != cfg_.bpm) {
                cfg_.bpm = e->bpm;
                refreshTempoUi();
            }
            if (cfg_.trainerEnabled) {
                if (e->bpm >= kMaxBpm)
                    gauge_->setTrainerStatus("At top speed (250 BPM)");
                else if (e->beatsUntilIncrease > 0)
                    gauge_->setTrainerStatus(QString("Next +%1 in %2 beats").arg(cfg_.trainerAddBpm).arg(e->beatsUntilIncrease));
                else
                    gauge_->setTrainerStatus(QString("Next +%1 at the next measure").arg(cfg_.trainerAddBpm));
            }
        } else if (const BeatEvent* firstBeat = timeline_.first()) {
            // Swing out from rest to reach the first tick on time.
            const double remaining = (firstBeat->start - t) / clock.sampleRate;
            const double lead = std::clamp(1.0 - remaining / 0.12, 0.0, 1.0);
            next.armPosition = firstBeat->side * lead * lead * (3.0 - 2.0 * lead);
        }
        lastArm_ = next.armPosition;
    } else {
        next.armPosition = lastArm_ * std::exp(-(now - stoppedAt_) / 0.12);
    }
    frame_ = next;

    // The layout can shift at a measure boundary while Ctrl is held.
    if (overlay_->isVisible()) updateShortcutBadges();

    if (diagnose_.enabled() && cfg_.running) diagnoseSamples_.push_back({now, clock.frames, smooth});
}

void MainWindow::toggleRunning() {
    cfg_.running = !cfg_.running;
    startButton_->setChecked(cfg_.running);
    startButton_->setText(cfg_.running ? "■  STOP" : "▶  START");
    if (cfg_.running) {
        ++cfg_.runId;
        timeline_.reset(cfg_.runId);
        layerTimeline_.reset(cfg_.runId);
    } else {
        stoppedAt_ = AudioEngine::nowSeconds();
    }
    refreshTrainerStatus();
    pushConfig();
}

void MainWindow::setTempo(int bpm) {
    cfg_.bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    ++cfg_.bpmRevision;
    refreshTempoUi();
    pushConfig();
}

void MainWindow::tapTempo() {
    const qint64 now = tapClock_.elapsed();
    if (!tapTimes_.empty() && now - tapTimes_.back() > 2000) tapTimes_.clear();
    tapTimes_.push_back(now);
    if (tapTimes_.size() > 8) tapTimes_.erase(tapTimes_.begin());
    if (tapTimes_.size() < 2) return;
    const double average = double(tapTimes_.back() - tapTimes_.front()) / double(tapTimes_.size() - 1);
    if (average > 0) setTempo(int(std::round(60000.0 / average)));
}

void MainWindow::setSignature(int measure, int beats, int denominator) {
    cfg_.measure[measure].beats = beats;
    cfg_.measure[measure].denominator = denominator;
    clampSelection();
    refreshSignatureUi();
    refreshTempoUi();
    pushConfig();
}

void MainWindow::cycleSignature(int measure, int delta) {
    const int count = int(std::size(kPresets));
    int index = -1;
    for (int i = 0; i < count; ++i)
        if (kPresets[i].beats == cfg_.measure[measure].beats && kPresets[i].denominator == cfg_.measure[measure].denominator) index = i;
    // A custom signature is not in the list, so it steps onto either end.
    const int next = index < 0 ? (delta > 0 ? 0 : count - 1) : (index + delta + count) % count;
    setSignature(measure, kPresets[next].beats, kPresets[next].denominator);
}

void MainWindow::editCustomSignature(int measure) {
    QDialog dialog(controls_);
    dialog.setWindowTitle(QString("Custom time signature, measure %1").arg(measure + 1));
    auto* form = new QFormLayout(&dialog);
    auto* beats = new QSpinBox;
    beats->setRange(1, kMaxBeats);
    beats->setValue(cfg_.measure[measure].beats);
    auto* denominator = new QComboBox;
    for (int d : {2, 4, 8, 16}) denominator->addItem(QString::number(d), d);
    denominator->setCurrentIndex(denominator->findData(cfg_.measure[measure].denominator));
    form->addRow("Beats", beats);
    form->addRow("Note value", denominator);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted)
        setSignature(measure, beats->value(), denominator->currentData().toInt());
    else
        refreshSignatureUi();
}

void MainWindow::setSecondMeasure(bool on) {
    cfg_.secondMeasure = on;
    secondCheck_->setChecked(on);
    signatureRow_[1]->setVisible(on);
    overlapRow_->setVisible(on);
    clampSelection();
    pushConfig();
    QTimer::singleShot(0, this, &MainWindow::applyControlsPlacement);
}

void MainWindow::setOverlap(int overlap) {
    cfg_.overlap = std::clamp(overlap, 0, OverlapCount - 1);
    refreshOverlapUi();
    pushConfig();
}

void MainWindow::setSubdivision(int subdivision) {
    cfg_.subdivision = subdivision;
    refreshSubdivisionUi();
    pushConfig();
}

void MainWindow::setMasterVolume(int percent) {
    cfg_.masterVolume = float(percent) / 100.0f;
    volumeValue_->setText(QString("%1%").arg(percent));
    pushConfig();
}

void MainWindow::setTrainer(bool on) {
    cfg_.trainerEnabled = on;
    trainerButton_->setChecked(on);
    refreshTrainerStatus();
    pushConfig();
}

void MainWindow::setAllSounds(int sound) {
    for (MeasureConfig& measure : cfg_.measure)
        for (BeatSettings& beat : measure.beat) beat.sound = sound;
    pushConfig();
}

void MainWindow::selectBeat(int measure, int beat) {
    selectedMeasure_ = measure;
    selectedBeat_ = beat;
    strip_->setSelected(measure, beat);
}

void MainWindow::clampSelection() {
    if (!cfg_.secondMeasure) selectedMeasure_ = 0;
    selectedBeat_ = std::clamp(selectedBeat_, 0, cfg_.measure[selectedMeasure_].beats - 1);
    strip_->setSelected(selectedMeasure_, selectedBeat_);
}

void MainWindow::moveSelection(int delta) {
    int measure = selectedMeasure_;
    int beat = selectedBeat_ + delta;
    if (beat >= cfg_.measure[measure].beats) {
        measure = (measure == 0 && cfg_.secondMeasure) ? 1 : 0;
        beat = 0;
    } else if (beat < 0) {
        measure = (measure == 0 && cfg_.secondMeasure) ? 1 : 0;
        beat = cfg_.measure[measure].beats - 1;
    }
    selectBeat(measure, beat);
}

void MainWindow::stepPitch(int measure, int beat, int delta) {
    int& note = cfg_.measure[measure].beat[beat].midiNote;
    note = std::clamp(note + delta, kMinNote, kMaxNote);
    pushConfig();
}

void MainWindow::setLevel(int measure, int beat, int level) {
    cfg_.measure[measure].beat[beat].level = std::clamp(level, 0, 3);
    pushConfig();
}

void MainWindow::showSoundMenu(int measure, int beat, QPoint globalPos) {
    QMenu menu(this);
    for (int i = 0; i < SoundCount; ++i) {
        QAction* action = menu.addAction(kSoundNames[i]);
        action->setCheckable(true);
        action->setChecked(cfg_.measure[measure].beat[beat].sound == i);
        connect(action, &QAction::triggered, this, [this, measure, beat, i] {
            cfg_.measure[measure].beat[beat].sound = i;
            pushConfig();
        });
    }
    menu.exec(globalPos);
}

void MainWindow::setControlsOpen(bool open) {
    controlsOpen_ = open;
    applyControlsPlacement();
}

void MainWindow::setControlsDocked(bool docked) {
    dockWanted_ = docked;
    dockButton_->setChecked(docked);
    // Queued, because the button that was clicked is about to move.
    QTimer::singleShot(0, this, &MainWindow::applyControlsPlacement);
}

void MainWindow::applyControlsPlacement() {
    const bool wideEnough = dockMinWidth_ > 0 && width() * (1.0 - kMinDockedGaugeShare) >= dockMinWidth_ + kWindowMargins;
    const bool docked = dockWanted_ && !cfg_.secondMeasure && wideEnough;

    if (docked != docked_) {
        docked_ = docked;
        // Docked controls need no opacity, and their row should not grow taller than the notes need.
        opacityRow_->setVisible(!docked);
        dial_->setMinimumSize(docked ? kDockedDialSize : kFloatingDialSize, docked ? kDockedDialSize : kFloatingDialSize);
        controlsBodyLayout_->setDirection(docked ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
        controlsBodyLayout_->setSpacing(docked ? 18 : 10);
        if (docked)
            notesColumn_->addWidget(controlsBody_);
        else
            controls_->layout()->addWidget(controlsBody_);
    }

    controlsBody_->setVisible(controlsOpen_ || !docked);
    if (controlsOpen_ && !docked && !controlsPlaced_) {
        // First use: inside the top-right corner of the window, ready to be dragged anywhere.
        const QSize size = controls_->sizeHint();
        controls_->move(frameGeometry().right() - size.width() - 40, frameGeometry().top() + 60);
        controlsPlaced_ = true;
    }
    controls_->setVisible(controlsOpen_ && !docked);
    gauge_->setControlsOpen(controlsOpen_);
    dockButton_->setChecked(dockWanted_);
    dockHint_->setText(cfg_.secondMeasure ? "Docks when only one measure is on" : "Window is too narrow to dock");
    dockHint_->setVisible(dockWanted_ && !docked);
    updateGaugeWidth();
    if (overlay_->isVisible()) showShortcuts(true);
}

void MainWindow::updateGaugeWidth() {
    int gauge = std::min(int((height() - 16) * GaugeView::kAspect), int(width() * kMaxGaugeShare));
    if (docked_) gauge = std::min(gauge, width() - kWindowMargins - dockMinWidth_);
    gauge_->setFixedWidth(gauge);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    const QEvent::Type type = event->type();
    if (type == QEvent::ApplicationDeactivate) {
        showShortcuts(false);
    } else if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        if (QApplication::activeModalWidget() || QApplication::activePopupWidget()) return false;
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Control) {
            if (!key->isAutoRepeat()) showShortcuts(type == QEvent::KeyPress);
            return true;
        }
        if (type == QEvent::KeyPress) return handleKey(key);
    }
    return QWidget::eventFilter(watched, event);
}

bool MainWindow::handleKey(QKeyEvent* event) {
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) return false;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    const bool repeat = event->isAutoRepeat();
    BeatSettings& selected = cfg_.measure[selectedMeasure_].beat[selectedBeat_];
    switch (event->key()) {
    case Qt::Key_Space:
        if (!repeat) toggleRunning();
        return true;
    case Qt::Key_Up:
        shift ? stepPitch(selectedMeasure_, selectedBeat_, 1) : setTempo(cfg_.bpm + 1);
        return true;
    case Qt::Key_Down:
        shift ? stepPitch(selectedMeasure_, selectedBeat_, -1) : setTempo(cfg_.bpm - 1);
        return true;
    case Qt::Key_PageUp:
        setTempo(cfg_.bpm + 5);
        return true;
    case Qt::Key_PageDown:
        setTempo(cfg_.bpm - 5);
        return true;
    case Qt::Key_Left:
        moveSelection(-1);
        return true;
    case Qt::Key_Right:
        moveSelection(1);
        return true;
    case Qt::Key_Tab:
        if (!repeat) setControlsOpen(!controlsOpen_);
        return true;
    case Qt::Key_D:
        if (!repeat) setControlsDocked(!dockWanted_);
        return true;
    case Qt::Key_T:
        if (!repeat) tapTempo();
        return true;
    case Qt::Key_M:
        if (!repeat) setSecondMeasure(!cfg_.secondMeasure);
        return true;
    case Qt::Key_O:
        if (!repeat && cfg_.secondMeasure) setOverlap((cfg_.overlap + 1) % OverlapCount);
        return true;
    case Qt::Key_R:
        if (!repeat) setTrainer(!cfg_.trainerEnabled);
        return true;
    case Qt::Key_S:
        if (!repeat) setSubdivision(cfg_.subdivision % 4 + 1);
        return true;
    case Qt::Key_V:
        if (!repeat) setLevel(selectedMeasure_, selectedBeat_, (selected.level + 1) % 4);
        return true;
    case Qt::Key_K:
        if (!repeat) {
            selected.sound = (selected.sound + 1) % SoundCount;
            pushConfig();
        }
        return true;
    case Qt::Key_N:
        if (!repeat) cycleSignature(0, shift ? -1 : 1);
        return true;
    case Qt::Key_B:
        if (!repeat && cfg_.secondMeasure) cycleSignature(1, shift ? -1 : 1);
        return true;
    case Qt::Key_E:
        everySlider_->setValue(everySlider_->value() + (shift ? -1 : 1));
        return true;
    case Qt::Key_A:
        addSlider_->setValue(addSlider_->value() + (shift ? -1 : 1));
        return true;
    case Qt::Key_BracketLeft:
        volumeSlider_->setValue(volumeSlider_->value() - 5);
        return true;
    case Qt::Key_BracketRight:
        volumeSlider_->setValue(volumeSlider_->value() + 5);
        return true;
    default:
        return false;
    }
}

void MainWindow::showShortcuts(bool show) {
    if (!show) {
        overlay_->hide();
        controls_->overlay->hide();
        return;
    }
    updateShortcutBadges();
    overlay_->setGeometry(rect());
    overlay_->raise();
    overlay_->show();
    // A floating controls window has its own badges, which only make sense while it is open.
    controls_->overlay->setVisible(controlsOpen_ && !docked_);
    controls_->overlay->raise();
}

void MainWindow::updateShortcutBadges() {
    const auto mappedStrip = [this](const QRectF& r) {
        return QRect(strip_->mapTo(this, r.topLeft().toPoint()), r.size().toSize());
    };
    const QRect button = gauge_->controlsButtonRect();
    std::vector<ShortcutOverlay::Badge> mainBadges = {
        {QRect(gauge_->mapTo(this, button.topLeft()), button.size()), "Tab"},
        {mappedStrip(strip_->noteRect(selectedMeasure_, selectedBeat_)), "← →"},
        {mappedStrip(strip_->pitchRect(selectedMeasure_, selectedBeat_)), "Shift+↑↓"},
        {mappedStrip(strip_->levelRect(selectedMeasure_, selectedBeat_)), "V"},
        {mappedStrip(strip_->soundRect(selectedMeasure_, selectedBeat_)), "K"},
    };

    QWidget* holder = docked_ ? static_cast<QWidget*>(this) : controls_;
    const auto mapped = [holder](QWidget* widget) { return QRect(widget->mapTo(holder, QPoint(0, 0)), widget->size()); };
    std::vector<ShortcutOverlay::Badge> badges = {
        {mapped(startButton_), "Space"},
        {mapped(tapButton_), "T"},
        {mapped(dial_), "↑ ↓  ±1   PgUp PgDn  ±5"},
        {mapped(secondCheck_), "M"},
        {mapped(subdivisionRow_), "S"},
        {mapped(trainerButton_), "R"},
        {mapped(everySlider_), "E  Shift+E"},
        {mapped(addSlider_), "A  Shift+A"},
        {mapped(signatureRow_[0]), "N  Shift+N"},
        {mapped(volumeSlider_), "[  ]"},
        {mapped(dockButton_), "D"},
    };
    if (cfg_.secondMeasure) {
        badges.push_back({mapped(signatureRow_[1]), "B  Shift+B"});
        badges.push_back({mapped(overlapRow_), "O"});
    }
    if (docked_ && controlsOpen_) mainBadges.insert(mainBadges.end(), badges.begin(), badges.end());
    overlay_->setBadges(std::move(mainBadges));
    if (!docked_) controls_->overlay->setBadges(std::move(badges));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (!overlay_) return;
    overlay_->setGeometry(rect());
    applyControlsPlacement();
}

void MainWindow::refreshTempoUi() {
    dial_->setValue(cfg_.bpm);
    gauge_->setTempo(cfg_.bpm, tempoName(cfg_.bpm));
    bpmLabel_->setText(QString("%1 BPM").arg(cfg_.bpm));
    tempoNameLabel_->setText(tempoName(cfg_.bpm));
    unitLabel_->setText("Counting " + noteValueName(cfg_.measure[0].denominator));
}

void MainWindow::refreshSignatureUi() {
    for (int m = 0; m < 2; ++m) {
        const MeasureConfig& measure = cfg_.measure[m];
        bool matched = false;
        for (size_t i = 0; i < std::size(kPresets); ++i) {
            const bool on = kPresets[i].beats == measure.beats && kPresets[i].denominator == measure.denominator;
            signatureButtons_[m][i]->setChecked(on);
            matched = matched || on;
        }
        QPushButton* custom = signatureButtons_[m].back();
        custom->setChecked(!matched);
        custom->setText(matched ? "Custom" : QString("%1/%2").arg(measure.beats).arg(measure.denominator));
    }
}

void MainWindow::refreshSubdivisionUi() {
    for (size_t i = 0; i < subdivisionButtons_.size(); ++i) subdivisionButtons_[i]->setChecked(int(i) + 1 == cfg_.subdivision);
}

void MainWindow::refreshOverlapUi() {
    for (size_t i = 0; i < overlapButtons_.size(); ++i) overlapButtons_[i]->setChecked(int(i) == cfg_.overlap);
}

void MainWindow::refreshTrainerStatus() {
    if (!cfg_.trainerEnabled)
        trainerStatus_->setText("Off");
    else
        trainerStatus_->setText(cfg_.running ? "Countdown is beside the metronome" : "Starts counting when you press Start");
    gauge_->setTrainerStatus({});
}

void MainWindow::loadSettings() {
    QSettings s;
    cfg_.bpm = std::clamp(s.value("tempo", cfg_.bpm).toInt(), kMinBpm, kMaxBpm);
    cfg_.secondMeasure = s.value("secondMeasure", false).toBool();
    cfg_.overlap = std::clamp(s.value("overlap", int(OverlapOff)).toInt(), 0, OverlapCount - 1);
    cfg_.subdivision = std::clamp(s.value("subdivision", 1).toInt(), 1, 4);
    cfg_.masterVolume = std::clamp(s.value("volume", 0.7).toFloat(), 0.0f, 1.0f);
    cfg_.trainerEnabled = s.value("trainer/enabled", false).toBool();
    cfg_.trainerEveryBeats = std::clamp(s.value("trainer/everyBeats", 16).toInt(), kTrainerBeatStep, 32 * kTrainerBeatStep);
    cfg_.trainerAddBpm = std::clamp(s.value("trainer/addBpm", 5).toInt(), 1, 20);
    audioOffsetMs_ = std::clamp(s.value("audioOffsetMs", 0).toInt(), -40, 400);
    for (int m = 0; m < 2; ++m) {
        MeasureConfig& measure = cfg_.measure[m];
        const QString prefix = QString("measure%1/").arg(m);
        measure.beats = std::clamp(s.value(prefix + "beats", 4).toInt(), 1, kMaxBeats);
        const int denominator = s.value(prefix + "denominator", 4).toInt();
        measure.denominator = (denominator == 2 || denominator == 8 || denominator == 16) ? denominator : 4;
        for (int b = 0; b < kMaxBeats; ++b) {
            BeatSettings& beat = measure.beat[b];
            const QString key = prefix + QString("beat%1/").arg(b);
            beat.midiNote = std::clamp(s.value(key + "note", beat.midiNote).toInt(), kMinNote, kMaxNote);
            beat.level = std::clamp(s.value(key + "level", beat.level).toInt(), 0, 3);
            beat.sound = std::clamp(s.value(key + "sound", beat.sound).toInt(), 0, SoundCount - 1);
        }
    }
    const QByteArray geometry = s.value("window/geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
}

void MainWindow::saveSettings() const {
    QSettings s;
    s.setValue("tempo", cfg_.bpm);
    s.setValue("secondMeasure", cfg_.secondMeasure);
    s.setValue("overlap", cfg_.overlap);
    s.setValue("subdivision", cfg_.subdivision);
    s.setValue("volume", cfg_.masterVolume);
    s.setValue("trainer/enabled", cfg_.trainerEnabled);
    s.setValue("trainer/everyBeats", cfg_.trainerEveryBeats);
    s.setValue("trainer/addBpm", cfg_.trainerAddBpm);
    s.setValue("audioOffsetMs", audioOffsetMs_);
    for (int m = 0; m < 2; ++m) {
        const MeasureConfig& measure = cfg_.measure[m];
        const QString prefix = QString("measure%1/").arg(m);
        s.setValue(prefix + "beats", measure.beats);
        s.setValue(prefix + "denominator", measure.denominator);
        for (int b = 0; b < kMaxBeats; ++b) {
            const BeatSettings& beat = measure.beat[b];
            const QString key = prefix + QString("beat%1/").arg(b);
            s.setValue(key + "note", beat.midiNote);
            s.setValue(key + "level", beat.level);
            s.setValue(key + "sound", beat.sound);
        }
    }
    s.setValue("window/geometry", saveGeometry());
    s.setValue("controls/open", controlsOpen_);
    s.setValue("controls/docked", dockWanted_);
    s.setValue("controls/opacity", opacitySlider_->value());
    if (controlsPlaced_) s.setValue("controls/geometry", controls_->saveGeometry());
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!diagnose_.enabled()) saveSettings();
    controls_->hide();
    QWidget::closeEvent(event);
}

void MainWindow::finishDiagnose() {
    if (!diagnose_.screenshotPath.isEmpty()) {
        grab().save(diagnose_.screenshotPath);
        if (controls_->isVisible()) controls_->grab().save(diagnose_.screenshotPath + ".controls.png");
    }

    QFile csv(diagnose_.csvPath);
    std::vector<double> intervals, clockError;
    const double rate = engine_.clock().sampleRate;
    if (csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&csv);
        out.setRealNumberNotation(QTextStream::FixedNotation);
        out.setRealNumberPrecision(4);
        out << "seconds,interval_ms,raw_frames,smooth_frames,clock_error_ms\n";
        for (size_t i = 0; i < diagnoseSamples_.size(); ++i) {
            const DiagnoseSample& sample = diagnoseSamples_[i];
            const double interval = i ? (sample.seconds - diagnoseSamples_[i - 1].seconds) * 1000.0 : 0.0;
            const double error = (sample.smoothFrames - sample.rawFrames) / rate * 1000.0;
            if (i) intervals.push_back(interval);
            clockError.push_back(std::abs(error));
            out << sample.seconds << ',' << interval << ',' << sample.rawFrames << ',' << sample.smoothFrames << ',' << error << '\n';
        }
    }

    const auto percentile = [](std::vector<double> values, double p) {
        if (values.empty()) return 0.0;
        std::sort(values.begin(), values.end());
        return values[std::min(values.size() - 1, size_t(p * double(values.size())))];
    };
    QFile summary(diagnose_.csvPath + ".summary.txt");
    if (summary.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&summary);
        out << "frames " << diagnoseSamples_.size() << "\n";
        out << "screen_hz " << (screen() ? screen()->refreshRate() : 0) << "\n";
        out << "sample_rate " << rate << "\n";
        out << "audio_device " << (engine_.hasDevice() ? "yes" : "no") << "\n";
        out << "audio_underruns " << engine_.underruns() << "\n";
        out << "frame_interval_ms p50 " << percentile(intervals, 0.5) << " p99 " << percentile(intervals, 0.99) << " max "
            << percentile(intervals, 1.0) << "\n";
        out << "clock_error_ms p50 " << percentile(clockError, 0.5) << " p99 " << percentile(clockError, 0.99) << " max "
            << percentile(clockError, 1.0) << "\n";
        out << "beats_displayed " << flashLateMs_.size() << "\n";
        out << "flash_late_ms p50 " << percentile(flashLateMs_, 0.5) << " p99 " << percentile(flashLateMs_, 0.99) << " max "
            << percentile(flashLateMs_, 1.0) << "\n";
        out << "flash_late_each";
        for (double late : flashLateMs_) out << ' ' << late;
        out << "\n";
    }
    close();
}
