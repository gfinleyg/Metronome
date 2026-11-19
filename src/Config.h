#pragma once
#include <cstdint>

constexpr int kMaxBeats = 16;
constexpr int kMinBpm = 30;
constexpr int kMaxBpm = 250;
constexpr int kMinNote = 48;   // C3
constexpr int kMaxNote = 108;  // C8
constexpr int kNoteC6 = 84;
constexpr int kNoteG5 = 79;

enum TickSound { SoundWoodblock, SoundClick, SoundBeep, SoundCowbell, SoundHiHat, SoundCount };

// How the second measure relates to the first: after it, or over it. Over it, the bars can match in
// length (a polyrhythm, such as 3 against 4) or the beats can (a polymeter, which realigns later).
enum Overlap { OverlapOff, OverlapSameBar, OverlapSameBeat, OverlapCount };

struct BeatSettings {
    int midiNote = kNoteG5;
    int level = 2;  // 0 off, 1 low, 2 medium, 3 max
    int sound = SoundWoodblock;
    bool operator==(const BeatSettings&) const = default;
};

struct MeasureConfig {
    int beats = 4;
    int denominator = 4;
    BeatSettings beat[kMaxBeats];

    MeasureConfig() {
        beat[0].midiNote = kNoteC6;
        beat[0].level = 3;
    }
};

// Trivially copyable, so it can cross to the audio thread without allocating.
struct SequenceConfig {
    MeasureConfig measure[2];
    bool secondMeasure = false;
    int overlap = OverlapOff;
    int bpm = 120;
    uint32_t bpmRevision = 0;  // Bumped on user edits, so trainer increases are not overwritten.
    int subdivision = 1;
    float masterVolume = 0.7f;
    bool trainerEnabled = false;
    int trainerEveryBeats = 16;
    int trainerAddBpm = 5;
    bool running = false;
    uint32_t runId = 0;
};

// One planned beat, published to the UI before it sounds.
struct BeatEvent {
    double start = 0;     // Stream frames.
    double duration = 0;  // Stream frames.
    int measure = 0;
    int beat = 0;
    int side = 1;  // Which side the metronome arm reaches on this beat.
    int nextMeasure = 0;
    int nextBeat = 0;
    int subdivision = 1;
    int bpm = 120;
    int beats[2] = {4, 4};
    int denominator[2] = {4, 4};
    bool second = false;
    int overlap = OverlapOff;
    int layer = 0;  // 1 for the second measure when it plays over the first.
    int beatsUntilIncrease = 0;
    uint32_t bpmRevision = 0;
    uint32_t runId = 0;
};
