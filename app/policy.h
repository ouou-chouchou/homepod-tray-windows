// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace homepod {
// This bounded proportional controller has no integrator to wind up while paused.
// The scale returned here is the SAME scale used by the sinc resampler and log.
class ClockPolicy {
public:
    static constexpr double maxCorrection = 0.002;
    double update(double bufferedSeconds, bool receivingPackets, double elapsed) {
        if (!receivingPackets) { applied_ = 1.; return applied_; }
        const double wanted = 1. + std::clamp((bufferedSeconds - 0.20) * .012,
                                             -maxCorrection, maxCorrection);
        const double alpha = 1. - std::exp(-std::clamp(elapsed, 0., 1.) / 2.);
        applied_ += alpha * (wanted - applied_);
        return applied_;
    }
    double applied() const { return applied_; }
private:
    double applied_ = 1.;
};

// Sender-side gate preserves queued audio until enough exists to survive scheduling
// jitter. Only the consumer drains/resets the SPSC ring; no producer reset races.
class PlaybackGate {
public:
    enum class State { Idle, Priming, Playing };
    bool ready(size_t availableFrames, bool active) {
        if (!active) { state_ = State::Idle; return false; }
        if (state_ != State::Playing) {
            state_ = State::Priming;
            if (availableFrames >= prebufferFrames) state_ = State::Playing;
        }
        return state_ == State::Playing;
    }
    void underflow() { state_ = State::Priming; }
    State state() const { return state_; }
    static constexpr size_t prebufferFrames = 8820; // 200 ms at wire format 44100
private:
    State state_ = State::Idle;
};
}
