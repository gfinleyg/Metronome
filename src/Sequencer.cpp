#include "Sequencer.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kStartLeadSeconds = 0.12;  // Lets the arm swing out before the first tick.
constexpr double kPlanAheadSeconds = 0.05;  // The UI learns about each beat this early.
constexpr double kMinTickSeconds = 0.02;    // Subdivisions closer than this are dropped.
constexpr double kVoiceSeconds = 0.35;
constexpr double kSubdivisionGain = 0.45;
constexpr float kLevelGain[4] = {0.0f, 0.25f, 0.5f, 1.0f};

double noteFrequency(int midiNote) { return 440.0 * std::pow(2.0, (midiNote - 69) / 12.0); }
}  // namespace

void Sequencer::setSampleRate(double sampleRate) {
    if (sampleRate == sampleRate_) return;
    sampleRate_ = sampleRate;
    if (running_) restartTransport();
}

void Sequencer::setConfig(const SequenceConfig& config) { cfg_ = config; }

void Sequencer::restartTransport() {
    running_ = true;
    runId_ = cfg_.runId;
    hasPlanned_ = false;
    firstPlan_ = true;
    planMeasure_ = planBeat_ = 0;
    side_ = 1;
    subIndex_ = 0;
    bpm_ = cfg_.bpm;
    seenRevision_ = cfg_.bpmRevision;
    beatsSinceIncrease_ = 0;
    beatBoundary_ = nextTick_ = double(pos_) + kStartLeadSeconds * sampleRate_;
}

void Sequencer::syncTransport() {
    if (!cfg_.running)
        running_ = false;
    else if (!running_ || runId_ != cfg_.runId)
        restartTransport();
}

void Sequencer::planIfNeeded(int remainingFrames) {
    if (hasPlanned_ || subIndex_ != 0) return;
    if (beatBoundary_ - double(pos_ + remainingFrames) <= kPlanAheadSeconds * sampleRate_) planBeat();
}

void Sequencer::planBeat() {
    if (planBeat_ == 0) {
        // Time signature edits only take effect here, at a measure boundary.
        if (!firstPlan_) planMeasure_ = (planMeasure_ == 0 && cfg_.secondMeasure) ? 1 : 0;
        for (int m = 0; m < 2; ++m) {
            activeBeats_[m] = cfg_.measure[m].beats;
            activeDenominator_[m] = cfg_.measure[m].denominator;
        }
        activeSecond_ = cfg_.secondMeasure;
    }
    if (cfg_.bpmRevision != seenRevision_) {
        seenRevision_ = cfg_.bpmRevision;
        bpm_ = cfg_.bpm;
    }
    if (!cfg_.trainerEnabled) {
        beatsSinceIncrease_ = 0;
    } else if (planBeat_ == 0 && !firstPlan_ && beatsSinceIncrease_ >= cfg_.trainerEveryBeats && bpm_ < kMaxBpm) {
        bpm_ = std::min(bpm_ + cfg_.trainerAddBpm, kMaxBpm);
        beatsSinceIncrease_ = 0;
    }
    firstPlan_ = false;

    BeatEvent e;
    e.start = beatBoundary_;
    // BPM counts measure 1's note value, so note lengths stay equal across measures.
    const double beatSeconds = 60.0 / bpm_ * double(activeDenominator_[0]) / activeDenominator_[planMeasure_];
    e.duration = beatSeconds * sampleRate_;
    e.measure = planMeasure_;
    e.beat = planBeat_;
    e.side = side_;
    e.subdivision = cfg_.subdivision;
    while (e.subdivision > 1 && beatSeconds / e.subdivision < kMinTickSeconds) --e.subdivision;
    e.bpm = bpm_;
    e.bpmRevision = seenRevision_;
    e.runId = runId_;
    e.second = activeSecond_;
    for (int m = 0; m < 2; ++m) {
        e.beats[m] = activeBeats_[m];
        e.denominator[m] = activeDenominator_[m];
    }
    if (planBeat_ + 1 < activeBeats_[planMeasure_]) {
        e.nextMeasure = planMeasure_;
        e.nextBeat = planBeat_ + 1;
    } else {
        e.nextMeasure = (planMeasure_ == 0 && cfg_.secondMeasure) ? 1 : 0;
        e.nextBeat = 0;
    }
    ++beatsSinceIncrease_;
    e.beatsUntilIncrease = cfg_.trainerEnabled ? std::max(0, cfg_.trainerEveryBeats - beatsSinceIncrease_) : 0;

    planned_ = e;
    hasPlanned_ = true;
    pushEvent(e);

    side_ = -side_;
    planBeat_ = (planBeat_ + 1 < activeBeats_[planMeasure_]) ? planBeat_ + 1 : 0;
}

void Sequencer::fireTick() {
    if (subIndex_ == 0) {
        current_ = planned_;
        hasPlanned_ = false;
        beatBoundary_ = current_.start + current_.duration;
    }
    const BeatSettings& settings = cfg_.measure[current_.measure].beat[current_.beat];
    const double gain = kLevelGain[std::clamp(settings.level, 0, 3)] * (subIndex_ == 0 ? 1.0 : kSubdivisionGain);
    if (tickObserver) tickObserver({pos_, current_.measure, current_.beat, subIndex_, gain});
    if (gain > 0) startVoice(settings, gain);

    if (++subIndex_ >= current_.subdivision) {
        subIndex_ = 0;
        nextTick_ = beatBoundary_;
    } else {
        nextTick_ = current_.start + current_.duration * subIndex_ / current_.subdivision;
    }
}

void Sequencer::render(float* out, int frames, int channels) {
    syncTransport();
    int done = 0;
    while (done < frames) {
        int n = frames - done;
        if (running_) {
            planIfNeeded(n);
            int64_t due = std::llround(nextTick_);
            while (due <= pos_) {
                if (subIndex_ == 0 && !hasPlanned_) planBeat();
                fireTick();
                planIfNeeded(n);
                due = std::llround(nextTick_);
            }
            n = int(std::min<int64_t>(n, due - pos_));
        }
        renderVoices(out + done * channels, n, channels);
        pos_ += n;
        done += n;
    }
}

void Sequencer::startVoice(const BeatSettings& settings, double gain) {
    Voice* slot = &voices_[0];
    for (Voice& v : voices_) {
        if (!v.active) {
            slot = &v;
            break;
        }
        if (v.t > slot->t) slot = &v;  // Otherwise steal the oldest.
    }
    *slot = Voice{};
    slot->active = true;
    slot->sound = settings.sound;
    slot->gain = float(gain);
    slot->freq = noteFrequency(settings.midiNote);
}

float Sequencer::noise() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return float(rng_) / 2147483648.0f - 1.0f;
}

float Sequencer::voiceSample(Voice& v) {
    const double sec = double(v.t) / sampleRate_;
    const double w = 2.0 * kPi * v.freq * sec;
    double s = 0;
    switch (v.sound) {
    case SoundClick:
        s = 0.6 * noise() * std::exp(-sec / 0.002) + 0.5 * std::sin(w) * std::exp(-sec / 0.004);
        break;
    case SoundBeep:
        s = 0.8 * std::sin(w) * std::exp(-sec / 0.03);
        break;
    case SoundCowbell:
        s = (0.4 * std::sin(w) + 0.4 * std::sin(w * 1.48) + 0.1 * std::sin(w * 3.0)) * std::exp(-sec / 0.07);
        break;
    case SoundHiHat: {
        // Band-pass filtered noise. Its center follows the pitch, so pitch changes are subtle.
        const double center = std::min(v.freq * 4.0, sampleRate_ / 6.0);
        const float g = float(2.0 * std::sin(kPi * center / sampleRate_));
        v.low += g * v.band;
        v.band += g * (noise() - v.low - 0.7f * v.band);
        s = 0.9 * v.band * std::exp(-sec / 0.02);
        break;
    }
    default:  // Woodblock.
        s = 0.7 * std::sin(w) * std::exp(-sec / 0.018) + 0.3 * std::sin(w * 2.76) * std::exp(-sec / 0.008);
        break;
    }
    const double attack = std::min(1.0, sec / 0.0005);
    if (sec > kVoiceSeconds) v.active = false;
    ++v.t;
    return float(s * attack) * v.gain;
}

void Sequencer::renderVoices(float* out, int frames, int channels) {
    const float target = cfg_.masterVolume * cfg_.masterVolume;
    for (int i = 0; i < frames; ++i) {
        master_ += (target - master_) * 0.002f;
        float sum = 0;
        for (Voice& v : voices_)
            if (v.active) sum += voiceSample(v);
        float s = std::clamp(sum * master_, -1.5f, 1.5f);
        s -= (4.0f / 27.0f) * s * s * s;  // Soft limit in case voices overlap at full volume.
        for (int c = 0; c < channels; ++c) *out++ = s;
    }
}

void Sequencer::pushEvent(const BeatEvent& event) {
    const uint32_t head = ringHead_.load(std::memory_order_relaxed);
    if (head - ringTail_.load(std::memory_order_acquire) >= kRingSize) return;
    ring_[head % kRingSize] = event;
    ringHead_.store(head + 1, std::memory_order_release);
}

bool Sequencer::popEvent(BeatEvent& event) {
    const uint32_t tail = ringTail_.load(std::memory_order_relaxed);
    if (tail == ringHead_.load(std::memory_order_acquire)) return false;
    event = ring_[tail % kRingSize];
    ringTail_.store(tail + 1, std::memory_order_release);
    return true;
}
