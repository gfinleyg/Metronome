#pragma once
#include "Config.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <functional>

// Decides what sounds at which stream frame and synthesizes it. Everything except
// popEvent() runs on the audio thread and never allocates or blocks.
class Sequencer {
public:
    struct TickInfo {
        int64_t frame;
        int measure, beat, sub;
        double gain;
    };

    void setSampleRate(double sampleRate);
    double sampleRate() const { return sampleRate_; }
    void setConfig(const SequenceConfig& config);
    void render(float* out, int frames, int channels);
    int64_t position() const { return pos_; }

    bool popEvent(BeatEvent& event);  // UI thread.

    std::function<void(const TickInfo&)> tickObserver;  // Tests only.

private:
    struct Voice {
        bool active = false;
        int sound = 0;
        float gain = 0;
        double freq = 0;
        int64_t t = 0;
        float band = 0, low = 0;
    };

    void syncTransport();
    void restartTransport();
    void planIfNeeded(int remainingFrames);
    void planBeat();
    void fireTick();
    void startVoice(const BeatSettings& settings, double gain);
    void renderVoices(float* out, int frames, int channels);
    float voiceSample(Voice& voice);
    float noise();
    void pushEvent(const BeatEvent& event);

    double sampleRate_ = 48000;
    SequenceConfig cfg_;
    int64_t pos_ = 0;

    bool running_ = false;
    uint32_t runId_ = 0;
    bool hasPlanned_ = false;
    bool firstPlan_ = true;
    BeatEvent planned_, current_;
    double nextTick_ = 0;
    double beatBoundary_ = 0;  // When the next beat starts.
    int subIndex_ = 0;
    int planMeasure_ = 0, planBeat_ = 0, side_ = 1;
    int activeBeats_[2] = {4, 4};
    int activeDenominator_[2] = {4, 4};
    bool activeSecond_ = false;
    int bpm_ = 120;
    uint32_t seenRevision_ = 0;
    int beatsSinceIncrease_ = 0;

    std::array<Voice, 8> voices_;
    float master_ = 0.7f;
    uint32_t rng_ = 0x12345678;

    static constexpr int kRingSize = 64;
    std::array<BeatEvent, kRingSize> ring_;
    std::atomic<uint32_t> ringHead_{0}, ringTail_{0};
};
