#pragma once
#include "Config.h"
#include <algorithm>
#include <cmath>
#include <deque>

// Every animated value is a pure function of playback time, so a late frame can never
// knock later frames out of step.
namespace anim {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSquareDwell = 0.12;  // Fraction of a beat the square rests on its note.

struct Point {
    double x, y;
};

// -1..1 arm position: fully on the event's side at the beat, fully across at the next one.
inline double armPosition(int side, double phase) { return side * std::cos(kPi * phase); }

inline double squareProgress(double phase) { return std::clamp((phase - kSquareDwell) / (1.0 - kSquareDwell), 0.0, 1.0); }

// True parabola through a and b whose highest point is exactly at apexY (screen y grows downward).
inline Point arcPoint(Point a, Point b, double apexY, double p) {
    const double rootA = std::sqrt(std::max(0.0, a.y - apexY));
    const double rootB = std::sqrt(std::max(0.0, b.y - apexY));
    const double h = (rootA + rootB) * (rootA + rootB) / 4.0;
    return {a.x + (b.x - a.x) * p, a.y + (b.y - a.y) * p - 4.0 * h * p * (1.0 - p)};
}

// Instant on, then settles before the next beat.
inline double notePulse(double sinceSec, double beatSec) {
    const double x = sinceSec / std::min(0.7 * beatSec, 0.45);
    return (x < 0 || x >= 1) ? 0.0 : 1.0 - x * x;
}

inline double flashPulse(double sinceSec, double beatSec, double fraction, double maxSec) {
    const double x = sinceSec / std::min(fraction * beatSec, maxSec);
    return (x < 0 || x >= 1) ? 0.0 : (1.0 - x) * (1.0 - x);
}

// Steadies the reported device position: it tracks the audio clock with a slow correction,
// and never runs backward.
class DisplayClock {
public:
    double update(double rawFrames, double seconds, double sampleRate) {
        if (!valid_) {
            estimate_ = rawFrames;
            valid_ = true;
        } else {
            const double predicted = estimate_ + (seconds - lastSeconds_) * sampleRate;
            const double error = rawFrames - predicted;
            estimate_ = std::abs(error) > 0.03 * sampleRate ? rawFrames : std::max(estimate_, predicted + error * 0.05);
        }
        lastSeconds_ = seconds;
        return estimate_;
    }

private:
    bool valid_ = false;
    double estimate_ = 0, lastSeconds_ = 0;
};

class Timeline {
public:
    void reset(uint32_t runId) {
        runId_ = runId;
        events_.clear();
    }
    void push(const BeatEvent& event) {
        if (event.runId != runId_) return;
        events_.push_back(event);
        if (events_.size() > 32) events_.pop_front();
    }
    const BeatEvent* at(double frame) const {
        for (auto it = events_.rbegin(); it != events_.rend(); ++it)
            if (it->start <= frame) return &*it;
        return nullptr;
    }
    const BeatEvent* first() const { return events_.empty() ? nullptr : &events_.front(); }

private:
    uint32_t runId_ = 0;
    std::deque<BeatEvent> events_;
};

}  // namespace anim

// What the views draw this frame.
struct FrameState {
    bool hasBeat = false;
    BeatEvent event;
    double phase = 0;
    double sinceSec = 0;
    double beatSec = 1;
    double armPosition = 0;  // -1..1.
    int bpm = 120;
};
