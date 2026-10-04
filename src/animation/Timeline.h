#pragma once

#include <algorithm>

namespace animated_mojang {

enum class Phase { Waiting, MenuOpening, MenuIdle };

struct Timing {
    double titleDelayMs      = 1880.0;
    double menuRevealMs      = 2700.0;
    double menuFadeMs        = 500.0;
    double maxPresentationMs = 15000.0;
    double maxFrameStepMs    = 100.0;
};

struct Frame {
    Phase  phase      = Phase::Waiting;
    double elapsedMs  = 0;
    float  menuAlpha  = 0;
    int    titleFrame = -1;
};

class Timeline {
public:
    explicit Timeline(Timing timing = {}) : mTiming(timing) {}

    Frame update(double nowMs, bool menuReady, bool assetsReady, bool animateTitle) {
        mNow = std::max(mNow, nowMs);
        if (mPhase == Phase::Waiting && menuReady && assetsReady) {
            mPhase      = animateTitle ? Phase::MenuOpening : Phase::MenuIdle;
            mStartedAt  = mNow;
            mLastUpdate = mNow;
        }
        if (mPhase == Phase::MenuOpening) {
            // Advance presentation time by a bounded step after a rendering hitch.
            // Keep the real-time escape limit independent of presentation time.
            const double gap = std::max(0.0, mNow - mLastUpdate);
            mElapsed += gap > mTiming.maxFrameStepMs ? std::min(mHealthyStep, 1000.0 / 30.0) : gap;
            if (gap > 0 && gap <= mTiming.maxFrameStepMs) mHealthyStep = gap;
        }
        mLastUpdate          = mNow;
        const double elapsed = mElapsed;
        if (mPhase == Phase::MenuOpening
            && (!animateTitle || mNow - mStartedAt >= mTiming.maxPresentationMs
                || elapsed
                       >= std::max(mTiming.menuRevealMs + mTiming.menuFadeMs, mTiming.titleDelayMs + 69.0 * 40.0))) {
            mPhase = Phase::MenuIdle;
        }
        Frame result;
        result.phase     = mPhase;
        result.elapsedMs = elapsed;
        if (mPhase == Phase::MenuOpening) {
            result.menuAlpha = unit((elapsed - mTiming.menuRevealMs) / mTiming.menuFadeMs);
            if (elapsed >= mTiming.titleDelayMs) {
                result.titleFrame = std::clamp(static_cast<int>((elapsed - mTiming.titleDelayMs) / 40.0), 0, 68);
            }
        } else if (mPhase == Phase::MenuIdle) {
            result.menuAlpha  = 1;
            result.titleFrame = 68;
        }
        return result;
    }

    void         skip() { mPhase = Phase::MenuIdle; }
    Phase        phase() const { return mPhase; }
    static float unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }

private:
    Timing mTiming;
    Phase  mPhase      = Phase::Waiting;
    double mNow        = 0;
    double mStartedAt  = 0;
    double mLastUpdate = 0;
    double mElapsed    = 0;
    double mHealthyStep = 1000.0 / 60.0;
};

struct AtlasRegion {
    float u, v, width, height;
};
inline AtlasRegion titleRegion(int frame) {
    return {0, static_cast<float>(std::clamp(frame, 0, 68) % 8) / 8.0f, 1, 1.0f / 8};
}

struct TitlePlacement {
    float x, y, width, height;
};
inline TitlePlacement titlePlacement(float targetX, float targetY, float targetWidth) {
    const float scale = targetWidth / 1373.0f;
    return {targetX - 518 * scale, targetY - 502 * scale, 2403 * scale, 749 * scale};
}
} // namespace animated_mojang
