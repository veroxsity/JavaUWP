#pragma once

#include <cstddef>

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
        emaVelX_ = 0.0;
        emaVelY_ = 0.0;
        lastConsumeMicros_ = 0;
        smoothAlpha_ = 0.0;
        stallThresholdMicros_ = 200000;
    }

    long long receivedCount() const { return recvCount_; }
    long long overwriteCount() const { return overwrites_; }
    int depth() const { return count_; }

    void setSmoothingAlpha(double alpha) {
        smoothAlpha_ = alpha < 0.0 ? 0.0 : (alpha > 1.0 ? 1.0 : alpha);
    }

    void setStallThresholdMicros(long long micros) {
        stallThresholdMicros_ = micros < 0 ? 0 : micros;
    }

    void submitRelative(long long tMicros, double dx, double dy, double wheel) {
        pushSample(tMicros, dx, dy, wheel);
        ++recvCount_;
    }

    void submitAbsolute(long long tMicros, double x, double y, bool window, double wheel) {
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
        out.ringDepthAtConsume = count_;
        out.clamped = false;

        const long long gap = lastConsumeMicros_ > 0 ? (nowMicros - lastConsumeMicros_) : 0;
        const bool bigStall = stallThresholdMicros_ > 0 && gap > stallThresholdMicros_;
        long long oldestFreshT = nowMicros;
        for (int i = 0; i < count_; ++i) {
            const Sample& s = ring_[(head_ + i) % kCap];
            const long long age = nowMicros - s.tMicros;
            if (!bigStall || age <= freshnessMicros) {
                out.dx += s.dx;
                out.dy += s.dy;
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

        if (smoothAlpha_ > 0.0 && !hasAbs_) {
            const double dtMicros = lastConsumeMicros_ > 0 ? (double)(nowMicros - lastConsumeMicros_) : 0.0;
            if (dtMicros <= 0.0 || dtMicros > (double)freshnessMicros) {
                const double seedDt = dtMicros > (double)freshnessMicros
                    ? (double)freshnessMicros
                    : (dtMicros > 0.0 ? dtMicros : 1.0);
                emaVelX_ = out.dx != 0.0 ? out.dx / seedDt : 0.0;
                emaVelY_ = out.dy != 0.0 ? out.dy / seedDt : 0.0;
            } else {
                const double instVelX = out.dx / dtMicros;
                const double instVelY = out.dy / dtMicros;
                emaVelX_ += smoothAlpha_ * (instVelX - emaVelX_);
                emaVelY_ += smoothAlpha_ * (instVelY - emaVelY_);
                out.dx = emaVelX_ * dtMicros;
                out.dy = emaVelY_ * dtMicros;
            }
        }
        lastConsumeMicros_ = nowMicros;

        if (clampMax > 0.0) {
            if (out.dx > clampMax) { out.dx = clampMax; out.clamped = true; }
            else if (out.dx < -clampMax) { out.dx = -clampMax; out.clamped = true; }
            if (out.dy > clampMax) { out.dy = clampMax; out.clamped = true; }
            else if (out.dy < -clampMax) { out.dy = -clampMax; out.clamped = true; }
        }

        if (hasAbs_) {
            out.hasAbsolute = true;
            out.absoluteWindow = absWindow_;
            out.absX = absX_;
            out.absY = absY_;
            hasAbs_ = false;
        }

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
    double emaVelX_;
    double emaVelY_;
    long long lastConsumeMicros_;
    double smoothAlpha_;
    long long stallThresholdMicros_;
};

}
