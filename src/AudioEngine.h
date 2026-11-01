#pragma once
#include "Sequencer.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

// Owns the WASAPI stream and the audio thread. The device's playback position is the
// master clock for everything on screen.
class AudioEngine {
public:
    struct Clock {
        double frames;  // Stream frame audible right now.
        double sampleRate;
    };

    AudioEngine();
    ~AudioEngine();

    void setConfig(const SequenceConfig& config);
    bool popEvent(BeatEvent& event) { return sequencer_.popEvent(event); }
    Clock clock() const;
    bool hasDevice() const { return hasDevice_.load(); }
    uint32_t underruns() const { return underruns_.load(); }

    static double nowSeconds();

private:
    void threadMain();
    bool runDevice(void* enumerator, void* wakeEvent);
    void runWithoutDevice();
    void syncConfig();
    void publishClock(int64_t frames, int64_t qpc100ns);

    Sequencer sequencer_;
    std::thread thread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> reopen_{false};
    std::atomic<bool> hasDevice_{false};
    std::atomic<uint32_t> underruns_{0};

    std::mutex configMutex_;
    SequenceConfig pendingConfig_;
    bool configDirty_ = false;

    // Seqlock: the audio thread writes, the UI thread reads, and neither ever waits.
    std::atomic<uint32_t> clockSeq_{0};
    std::atomic<int64_t> clockFrames_{0};
    std::atomic<int64_t> clockQpc100ns_{0};
    std::atomic<double> clockRate_{48000};
};
