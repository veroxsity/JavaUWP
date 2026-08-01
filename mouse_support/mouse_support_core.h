#pragma once

#include <cstddef>
#include <cmath>

namespace mousesupport {

enum ButtonAction {
    ButtonRelease = 0,
    ButtonPress = 1,
};

struct ButtonEvent {
    int button;
    int action;
};

struct ConsumeResult {
    double dx;
    double dy;
    double wheel;
    bool hasAbsolute;
    bool absoluteWindow;
    double absX;
    double absY;
    int buttonCount;
    ButtonEvent buttons[16];

    double appliedAgeMicros;
    double droppedDx;
    double droppedDy;
    int droppedSamples;
    int freshSamples;
    int ringDepthAtConsume;
    bool clamped;
    double pendingX;
    double pendingY;
};

class MouseMailbox {
public:
    MouseMailbox() { reset(); }

    void reset() {
        head_ = 0;
        count_ = 0;
        overwrites_ = 0;
        recvCount_ = 0;
        hasAbs_ = false;
        absWindow_ = false;
        absX_ = 0.0;
        absY_ = 0.0;
        targetButtons_ = 0;
        appliedButtons_ = 0;
        pendingX_ = 0.0;
        pendingY_ = 0.0;
        lastConsumeMicros_ = 0;
        lastSubmitMicros_ = 0;
        smoothTauMicros_ = 0;
        stallThresholdMicros_ = 200000;
    }

    long long receivedCount() const { return recvCount_; }
    long long overwriteCount() const { return overwrites_; }
    int depth() const { return count_; }

    void setSmoothingTauMicros(long long micros) {
        smoothTauMicros_ = micros < 0 ? 0 : micros;
    }

    void setStallThresholdMicros(long long micros) {
        stallThresholdMicros_ = micros < 0 ? 0 : micros;
    }

    void submitRelative(long long tMicros, double dx, double dy, double wheel) {
        pushSample(tMicros, dx, dy, wheel);
        lastSubmitMicros_ = tMicros;
        ++recvCount_;
    }

    void submitAbsolute(long long tMicros, double x, double y, bool window, double wheel) {
        lastSubmitMicros_ = tMicros;
        absX_ = x;
        absY_ = y;
        absWindow_ = window;
        hasAbs_ = true;
        head_ = 0;
        count_ = 0;
        if (wheel != 0.0) {
            pushSample(tMicros, 0.0, 0.0, wheel);
        }
        ++recvCount_;
    }

    void submitButtonValue(int bit, int value) {
        if (value < 0) return;
        if (value != 0) targetButtons_ |= bit;
        else targetButtons_ &= ~bit;
    }

    void consume(long long nowMicros, long long freshnessMicros, double clampMax, ConsumeResult& out) {
        out.dx = 0.0;
        out.dy = 0.0;
        out.wheel = 0.0;
        out.hasAbsolute = false;
        out.absoluteWindow = false;
        out.absX = 0.0;
        out.absY = 0.0;
        out.buttonCount = 0;
        out.appliedAgeMicros = 0.0;
        out.droppedDx = 0.0;
        out.droppedDy = 0.0;
        out.droppedSamples = 0;
        out.freshSamples = 0;
        out.pendingX = 0.0;
        out.pendingY = 0.0;
        out.ringDepthAtConsume = count_;
        out.clamped = false;

        const long long gap = lastConsumeMicros_ > 0 ? (nowMicros - lastConsumeMicros_) : 0;
        // nothing arriving means the relay went away and the backlog is junk. packets still
        // arriving means the game hitched, that motion is real and dropping it moves the view
        const bool relaySilent = lastSubmitMicros_ == 0 ||
            (nowMicros - lastSubmitMicros_) > freshnessMicros;
        const bool dropStale = stallThresholdMicros_ > 0 && gap > stallThresholdMicros_ && relaySilent;

        long long oldestFreshT = nowMicros;
        for (int i = 0; i < count_; ++i) {
            const Sample& s = ring_[(head_ + i) % kCap];
            const long long age = nowMicros - s.tMicros;
            if (!dropStale || age <= freshnessMicros) {
                pendingX_ += s.dx;
                pendingY_ += s.dy;
                out.wheel += s.wheel;
                ++out.freshSamples;
                if (s.tMicros < oldestFreshT) oldestFreshT = s.tMicros;
            } else {
                out.droppedDx += s.dx < 0.0 ? -s.dx : s.dx;
                out.droppedDy += s.dy < 0.0 ? -s.dy : s.dy;
                ++out.droppedSamples;
            }
        }

        head_ = 0;
        count_ = 0;

        if (out.freshSamples > 0) {
            out.appliedAgeMicros = (double)(nowMicros - oldestFreshT);
        }

        if (dropStale) {
            pendingX_ = 0.0;
            pendingY_ = 0.0;
        }

        // clamp the backlog not the emitted delta, stops a hitch turning into a whip
        if (clampMax > 0.0) {
            if (pendingX_ > clampMax) { pendingX_ = clampMax; out.clamped = true; }
            else if (pendingX_ < -clampMax) { pendingX_ = -clampMax; out.clamped = true; }
            if (pendingY_ > clampMax) { pendingY_ = clampMax; out.clamped = true; }
            else if (pendingY_ < -clampMax) { pendingY_ = -clampMax; out.clamped = true; }
        }

        if (hasAbs_) {
            // the cursor was placed outright so any relative backlog is meaningless now
            pendingX_ = 0.0;
            pendingY_ = 0.0;
            out.hasAbsolute = true;
            out.absoluteWindow = absWindow_;
            out.absX = absX_;
            out.absY = absY_;
            hasAbs_ = false;
        } else {
            double emitX = pendingX_;
            double emitY = pendingY_;
            const double dtMicros = lastConsumeMicros_ > 0 ? (double)(nowMicros - lastConsumeMicros_) : 0.0;
            if (smoothTauMicros_ > 0 && dtMicros > 0.0) {
                // alpha comes from the real frame delta so the same tau feels identical at 30 and 120 fps
                const double alpha = 1.0 - std::exp(-dtMicros / (double)smoothTauMicros_);
                emitX = pendingX_ * alpha;
                emitY = pendingY_ * alpha;
                // flush the tail instead of asymptoting, otherwise the view never fully comes to rest
                if (std::fabs(pendingX_ - emitX) < kSettleEpsilon) emitX = pendingX_;
                if (std::fabs(pendingY_ - emitY) < kSettleEpsilon) emitY = pendingY_;
            }
            pendingX_ -= emitX;
            pendingY_ -= emitY;
            out.dx = emitX;
            out.dy = emitY;
        }

        lastConsumeMicros_ = nowMicros;
        out.pendingX = pendingX_;
        out.pendingY = pendingY_;

        const int diff = targetButtons_ ^ appliedButtons_;
        const int maxButtons = (int)(sizeof(out.buttons) / sizeof(out.buttons[0]));
        for (int bit = 1; bit <= 16 && out.buttonCount < maxButtons; bit <<= 1) {
            if (diff & bit) {
                const int button = mapBitToButton(bit);
                if (button >= 0) {
                    const bool down = (targetButtons_ & bit) != 0;
                    out.buttons[out.buttonCount].button = button;
                    out.buttons[out.buttonCount].action = down ? ButtonPress : ButtonRelease;
                    ++out.buttonCount;
                }
            }
        }
        appliedButtons_ = targetButtons_;
    }

    static int mapBitToButton(int bit) {
        switch (bit) {
        case 1: return 0;
        case 2: return 1;
        case 4: return 2;
        case 8: return 3;
        case 16: return 4;
        default: return -1;
        }
    }

private:
    static const int kCap = 256;
    static constexpr double kSettleEpsilon = 0.01;

    struct Sample {
        long long tMicros;
        double dx;
        double dy;
        double wheel;
    };

    void pushSample(long long tMicros, double dx, double dy, double wheel) {
        if (count_ < kCap) {
            Sample& s = ring_[(head_ + count_) % kCap];
            s.tMicros = tMicros;
            s.dx = dx;
            s.dy = dy;
            s.wheel = wheel;
            ++count_;
        } else {
            Sample& s = ring_[head_];
            s.tMicros = tMicros;
            s.dx = dx;
            s.dy = dy;
            s.wheel = wheel;
            head_ = (head_ + 1) % kCap;
            ++overwrites_;
        }
    }

    Sample ring_[kCap];
    int head_;
    int count_;
    long long overwrites_;
    long long recvCount_;
    bool hasAbs_;
    bool absWindow_;
    double absX_;
    double absY_;
    int targetButtons_;
    int appliedButtons_;
    double pendingX_;
    double pendingY_;
    long long lastConsumeMicros_;
    long long lastSubmitMicros_;
    long long smoothTauMicros_;
    long long stallThresholdMicros_;
};

}
