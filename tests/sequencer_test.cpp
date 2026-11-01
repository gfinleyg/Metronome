#include "Animation.h"
#include "Sequencer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

namespace {
const double kRate = 48000;

struct Rig {
    Sequencer seq;
    SequenceConfig cfg;
    std::vector<Sequencer::TickInfo> ticks;
    std::vector<BeatEvent> events;
    std::vector<float> buffer;

    Rig() {
        seq.setSampleRate(kRate);
        seq.tickObserver = [this](const Sequencer::TickInfo& t) { ticks.push_back(t); };
        cfg.running = true;
        cfg.runId = 1;
        seq.setConfig(cfg);
    }
    void run(int64_t frames, int block = 480) {
        buffer.resize(size_t(block));
        while (frames > 0) {
            const int n = int(std::min<int64_t>(block, frames));
            seq.render(buffer.data(), n, 1);
            frames -= n;
            BeatEvent e;
            while (seq.popEvent(e)) events.push_back(e);
        }
    }
    void runSeconds(double seconds, int block = 480) { run(int64_t(seconds * kRate), block); }
    std::vector<Sequencer::TickInfo> beats() const {
        std::vector<Sequencer::TickInfo> out;
        for (const auto& t : ticks)
            if (t.sub == 0) out.push_back(t);
        return out;
    }
};

void testSteadyBeat() {
    Rig r;
    r.runSeconds(10);
    const auto b = r.beats();
    CHECK(b.size() >= 19);
    for (size_t i = 1; i < b.size(); ++i) {
        CHECK(b[i].frame - b[i - 1].frame == 24000);
        CHECK(b[i].beat == int(i % 4));
    }
}

void testNoLongTermDrift() {
    // One hour at a tempo with no whole number of frames per beat.
    Rig r;
    r.seq.setSampleRate(44100);
    r.cfg.bpm = 97;
    r.seq.setConfig(r.cfg);
    r.run(int64_t(3600) * 44100);
    const auto b = r.beats();
    CHECK(b.size() > 5000);
    const double beatFrames = 60.0 / 97.0 * 44100.0;
    for (size_t i = 1; i < b.size(); ++i) {
        const double expected = double(b[0].frame) + beatFrames * double(i);
        CHECK(std::abs(double(b[i].frame) - expected) <= 1.0);
    }
}

void testBlockSizeDoesNotChangeTiming() {
    Rig base;
    base.cfg.subdivision = 3;
    base.cfg.bpm = 137;
    base.seq.setConfig(base.cfg);
    base.runSeconds(5, 1);
    for (int block : {64, 441, 480, 1024, 4800}) {
        Rig r;
        r.cfg.subdivision = 3;
        r.cfg.bpm = 137;
        r.seq.setConfig(r.cfg);
        r.runSeconds(5, block);
        CHECK(r.ticks.size() == base.ticks.size());
        for (size_t i = 0; i < std::min(r.ticks.size(), base.ticks.size()); ++i) CHECK(r.ticks[i].frame == base.ticks[i].frame);
    }
}

void testAudioOnsetMatchesSchedule() {
    Rig r;
    r.cfg.masterVolume = 1;
    r.seq.setConfig(r.cfg);
    std::vector<float> audio(48000);
    r.seq.render(audio.data(), 48000, 1);
    CHECK(!r.ticks.empty());
    size_t onset = 0;
    while (onset < audio.size() && std::abs(audio[onset]) < 1e-5f) ++onset;
    // The 0.5 ms attack starts from zero, so the first audible sample can be slightly later.
    CHECK(std::abs(double(onset) - double(r.ticks[0].frame)) <= 2.0);
}

void testMixedMeters() {
    Rig r;
    r.cfg.secondMeasure = true;
    r.cfg.measure[1].beats = 6;
    r.cfg.measure[1].denominator = 8;
    r.seq.setConfig(r.cfg);
    r.runSeconds(12);
    const auto b = r.beats();
    CHECK(b.size() >= 21);
    for (size_t i = 0; i < 20; ++i) {
        const size_t pos = i % 10;
        CHECK(b[i].measure == (pos < 4 ? 0 : 1));
        CHECK(b[i].beat == int(pos < 4 ? pos : pos - 4));
        const int64_t gap = b[i + 1].frame - b[i].frame;
        CHECK(gap == (pos < 4 ? 24000 : 12000));  // Quarter 120, eighth 240.
    }
    // The last note of each measure points the square at the first note of the other.
    for (const auto& e : r.events) {
        if (e.measure == 0 && e.beat == 3) CHECK(e.nextMeasure == 1 && e.nextBeat == 0);
        if (e.measure == 1 && e.beat == 5) CHECK(e.nextMeasure == 0 && e.nextBeat == 0);
    }
}

void testEventsArriveBeforeTheyPlay() {
    Rig r;
    std::vector<float> buf(480);
    int64_t written = 0;
    int seen = 0;
    for (int i = 0; i < 500; ++i) {
        r.seq.render(buf.data(), 480, 1);
        written += 480;
        BeatEvent e;
        while (r.seq.popEvent(e)) {
            ++seen;
            CHECK(e.start - double(written) >= 0.04 * kRate);
        }
    }
    CHECK(seen > 5);
}

void testArmContinuousAcrossBeats() {
    Rig r;
    r.runSeconds(5);
    CHECK(r.events.size() > 4);
    for (size_t i = 1; i < r.events.size(); ++i) {
        const auto& a = r.events[i - 1];
        const auto& b = r.events[i];
        CHECK(std::abs(a.start + a.duration - b.start) < 1e-6);
        CHECK(std::abs(anim::armPosition(a.side, 1.0) - anim::armPosition(b.side, 0.0)) < 1e-9);
    }
}

void testTempoChangesOnNextBeatOnly() {
    Rig r;
    r.runSeconds(1.0);
    const size_t before = r.beats().size();
    r.cfg.bpm = 60;
    ++r.cfg.bpmRevision;
    r.seq.setConfig(r.cfg);
    r.runSeconds(6);
    const auto b = r.beats();
    CHECK(b[before].frame - b[before - 1].frame == 24000);  // The beat in progress keeps its length.
    CHECK(b[before + 1].frame - b[before].frame == 48000);
}

void testTimeSignatureChangesAtMeasureBoundary() {
    Rig r;
    r.runSeconds(0.8);  // Inside beat 2 of 4/4.
    r.cfg.measure[0].beats = 3;
    r.seq.setConfig(r.cfg);
    r.runSeconds(8);
    const auto b = r.beats();
    for (size_t i = 0; i < 4; ++i) CHECK(b[i].beat == int(i));
    for (size_t i = 4; i < b.size(); ++i) CHECK(b[i].beat == int((i - 4) % 3));
}

void testTrainerWaitsForMeasureStart() {
    Rig r;
    r.cfg.trainerEnabled = true;
    r.cfg.trainerEveryBeats = 6;
    r.cfg.trainerAddBpm = 10;
    r.seq.setConfig(r.cfg);
    r.runSeconds(12);
    CHECK(r.events.size() >= 12);
    for (size_t i = 0; i < 8; ++i) CHECK(r.events[i].bpm == 120);  // 6 beats end mid-measure, so it waits for beat 8.
    CHECK(r.events[8].bpm == 130);
    CHECK(r.events[8].beat == 0);
}

void testTrainerStopsAtMax() {
    Rig r;
    r.cfg.bpm = 245;
    r.cfg.trainerEnabled = true;
    r.cfg.trainerEveryBeats = 4;
    r.cfg.trainerAddBpm = 20;
    r.seq.setConfig(r.cfg);
    r.runSeconds(10);
    CHECK(r.events.back().bpm == kMaxBpm);
}

void testBeatLevelsAndMutes() {
    Rig r;
    r.cfg.measure[0].beat[1].level = 0;
    r.cfg.subdivision = 2;
    r.seq.setConfig(r.cfg);
    r.runSeconds(3);
    CHECK(!r.ticks.empty());
    for (const auto& t : r.ticks) {
        if (t.beat == 1) CHECK(t.gain == 0);
        if (t.beat == 0 && t.sub == 0) CHECK(t.gain == 1.0);
        if (t.beat == 0 && t.sub == 1) CHECK(std::abs(t.gain - 0.45) < 1e-9);
    }
}

void testEverySoundIsAudibleAndBounded() {
    for (int sound = 0; sound < SoundCount; ++sound) {
        for (int note : {kMinNote, kNoteC6, kMaxNote}) {
            Rig r;
            r.cfg.masterVolume = 1;
            r.cfg.measure[0].beat[0].sound = sound;
            r.cfg.measure[0].beat[0].midiNote = note;
            r.seq.setConfig(r.cfg);
            std::vector<float> audio(24000);
            r.seq.render(audio.data(), 24000, 1);
            float peak = 0;
            for (float s : audio) {
                CHECK(std::isfinite(s));
                peak = std::max(peak, std::abs(s));
            }
            CHECK(peak > 0.02f);
            CHECK(peak <= 1.0f);
        }
    }
}

void testArcGeometry() {
    const anim::Point a{500, 100}, b{80, 260};
    const auto start = anim::arcPoint(a, b, 10, 0);
    const auto end = anim::arcPoint(a, b, 10, 1);
    CHECK(std::abs(start.x - a.x) < 1e-9 && std::abs(start.y - a.y) < 1e-9);
    CHECK(std::abs(end.x - b.x) < 1e-9 && std::abs(end.y - b.y) < 1e-9);
    double top = 1e9;
    for (int i = 0; i <= 10000; ++i) top = std::min(top, anim::arcPoint(a, b, 10, i / 10000.0).y);
    CHECK(std::abs(top - 10) < 0.01);
}

void testDisplayClock() {
    anim::DisplayClock clock;
    double last = clock.update(0, 0, kRate);
    for (int i = 1; i < 5000; ++i) {
        const double sec = i / 144.0;
        const double jitter = (i % 2 ? 1 : -1) * 0.002 * kRate;
        const double value = clock.update(sec * kRate + jitter, sec, kRate);
        CHECK(value >= last);
        if (i > 200) CHECK(std::abs(value - sec * kRate) < 0.002 * kRate);
        last = value;
    }
    const double sec = 5000 / 144.0 + 1;
    CHECK(std::abs(clock.update(sec * kRate + kRate, sec, kRate) - (sec * kRate + kRate)) < 1);  // Device switch: snap.
}
}  // namespace

int main() {
    testSteadyBeat();
    testNoLongTermDrift();
    testBlockSizeDoesNotChangeTiming();
    testAudioOnsetMatchesSchedule();
    testMixedMeters();
    testEventsArriveBeforeTheyPlay();
    testArmContinuousAcrossBeats();
    testTempoChangesOnNextBeatOnly();
    testTimeSignatureChangesAtMeasureBoundary();
    testTrainerWaitsForMeasureStart();
    testTrainerStopsAtMax();
    testBeatLevelsAndMutes();
    testEverySoundIsAudibleAndBounded();
    testArcGeometry();
    testDisplayClock();
    if (failures) {
        std::printf("%d checks FAILED\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("All sequencer tests passed\n");
    return EXIT_SUCCESS;
}
