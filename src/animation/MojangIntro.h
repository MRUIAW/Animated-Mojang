#pragma once

#include <algorithm>

namespace animated_mojang {

// Port of the original MojangAnimation: Mojang logo frames, STUDIOS fade-in and the startup sound.
// It plays once the game has finished loading. Afterwards the overlay stays up over the start of the menu
// opening, in place of the loading picture, and the caller fades it out on that picture's schedule.
struct IntroTiming {
    static constexpr int    frameCount      = 79;
    static constexpr double frameMs         = 40;
    static constexpr double logoFadeMs      = 300;
    static constexpr double studiosStartMs  = frameCount * frameMs;
    static constexpr double studiosFadeMs   = 400;
    static constexpr double soundDelayMs    = 350;
    static constexpr double soundRetryMs    = 200;
    static constexpr double soundLengthMs   = 4066;
    static constexpr double soundTailMs     = 450;
    static constexpr double soundFallbackMs = 8000;
    static constexpr double maxVisualStepMs = 50;
    static constexpr double leadInMs        = 500; // Dark overlay fades in over the loading picture first.
    static constexpr double studiosEndMs    = studiosStartMs + studiosFadeMs;
    static constexpr double totalMs         = std::max(soundDelayMs + soundLengthMs + soundTailMs, studiosEndMs);
};

struct IntroFrame {
    bool  active       = false; // Overlay is drawn.
    bool  released     = false; // Animation finished: the menu opening may start under the overlay.
    bool  requestSound = false; // Try to start the startup sound now and report it with soundResult().
    int   logoFrame    = 0;
    float logoAlpha = 0, studiosAlpha = 0, overlayAlpha = 0;
};

class MojangIntro {
public:
    // soundDelayMs: logo time at which the sound starts (the logo holds there until it plays).
    // releaseMs: logo time at which the menu opening starts; never before the finished STUDIOS frame.
    // fadeIn: fade the dark overlay in over the loading picture first, instead of cutting to it.
    explicit MojangIntro(
        bool   withSound    = true,
        double soundDelayMs = IntroTiming::soundDelayMs,
        double releaseMs    = IntroTiming::totalMs,
        bool   fadeIn       = true
    )
    : mWithSound(withSound),
      mSoundDelay(std::max(0.0, soundDelayMs)),
      mRelease(std::max(releaseMs, IntroTiming::studiosEndMs)),
      mLeadIn(fadeIn ? IntroTiming::leadInMs : 0) {}

    void start(double nowMs) {
        if (mState != State::Idle) return;
        mState     = State::LeadIn;
        mStartedAt = nowMs;
    }

    IntroFrame update(double nowMs) {
        IntroFrame frame;
        if (mState == State::Idle) return frame;
        if (mState == State::Done) {
            frame.released = true;
            return frame;
        }
        if (mState == State::LeadIn) {
            frame.active       = true;
            frame.overlayAlpha = mLeadIn > 0 ? unit((nowMs - mStartedAt) / mLeadIn) : 1;
            if (frame.overlayAlpha < 1) return frame;
            mState = State::Playing; // The logo clock starts on the following update.
            return frame;
        }
        if (mState == State::Playing) {
            // Like the original, advance by wall time with a bounded step after a hitch; the first update only
            // records the time.
            if (mLastUpdate >= 0) mVisual += std::clamp(nowMs - mLastUpdate, 0.0, IntroTiming::maxVisualStepMs);
            mLastUpdate = nowMs;
            if (mWithSound && !mSoundReleased && mVisual >= mSoundDelay) {
                // Hold the visuals at the sound's start point until it plays.
                mVisual = mSoundDelay;
                if (mLastAttempt < 0 || nowMs - mLastAttempt >= IntroTiming::soundRetryMs) {
                    mLastAttempt       = nowMs;
                    frame.requestSound = true;
                }
            }
            if (mVisual >= mRelease) mState = State::Released;
        }
        frame.active    = true;
        frame.logoFrame = std::min(static_cast<int>(mVisual / IntroTiming::frameMs), IntroTiming::frameCount - 1);
        frame.logoAlpha = unit(mVisual / IntroTiming::logoFadeMs);
        frame.studiosAlpha = unit((mVisual - IntroTiming::studiosStartMs) / IntroTiming::studiosFadeMs);
        frame.overlayAlpha = 1;
        frame.released     = mState == State::Released;
        return frame;
    }

    void soundResult(double nowMs, bool started) {
        if (mSoundReleased) return;
        if (started) {
            mSoundReleased = mSoundStarted = true;
            mVisual                        = mSoundDelay;
        } else if (nowMs - mStartedAt >= IntroTiming::soundFallbackMs) {
            mSoundReleased = true; // Continue silently rather than holding the logo forever.
        }
    }

    void skip() {
        if (mState == State::Idle) mStartedAt = 0;
        mState = State::Done;
    }
    bool started() const { return mState != State::Idle; }
    bool soundStarted() const { return mSoundStarted; }
    double startedAt() const { return mStartedAt; }

private:
    enum class State { Idle, LeadIn, Playing, Released, Done };
    static float unit(double value) { return static_cast<float>(std::clamp(value, 0.0, 1.0)); }

    bool   mWithSound;
    double mSoundDelay, mRelease, mLeadIn;
    State  mState     = State::Idle;
    double mStartedAt = 0, mLastUpdate = -1, mVisual = 0, mLastAttempt = -1;
    bool   mSoundReleased = false, mSoundStarted = false;
};

struct IntroPlacement {
    float logoX, logoY, logoWidth, logoHeight, studiosX, studiosY, studiosWidth, studiosHeight;
};
// The original's layout in GUI units: logo 4x the vanilla logo height, nudged up; STUDIOS centered below it.
inline IntroPlacement introPlacement(float width, float height) {
    const float vanillaHeight = std::min(width * 0.75f, height) * 0.25f;
    IntroPlacement p{};
    p.logoWidth     = vanillaHeight * 4;
    p.logoHeight    = p.logoWidth * 83 / 540;
    p.logoX         = (width - p.logoWidth) / 2;
    p.logoY         = (height - p.logoHeight) / 2 - vanillaHeight * 0.16f;
    p.studiosHeight = vanillaHeight * 0.2f;
    p.studiosWidth  = p.studiosHeight * 506 / 56;
    p.studiosX      = (width - p.studiosWidth) / 2;
    p.studiosY      = p.logoY + p.logoHeight + vanillaHeight * 0.12f;
    return p;
}

struct AtlasRect {
    float u, v, width, height;
};
// Normalized UVs for a logo frame in the 10x8 atlas of 600x129 cells, cropped to the 540x83 logo.
inline AtlasRect introLogoRegion(int frame) {
    frame = std::clamp(frame, 0, IntroTiming::frameCount - 1);
    return {(frame % 10 * 600 + 31) / 6000.0f, (frame / 10 * 129 + 31) / 1032.0f, 540 / 6000.0f, 83 / 1032.0f};
}
inline AtlasRect introStudiosRegion() { return {30 / 560.0f, 18 / 90.0f, 506 / 560.0f, 56 / 90.0f}; }
} // namespace animated_mojang
