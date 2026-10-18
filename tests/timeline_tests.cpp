#include "animation/MojangIntro.h"
#include "animation/Timeline.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace animated_mojang;
void check(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
Frame advance(Timeline& timeline, double& now, double target) {
    Frame frame;
    while (now < target) {
        now   = std::min(now + 40.0, target);
        frame = timeline.update(now, true, true, true);
    }
    return frame;
}

int main() {
    try {
        Timeline timeline;
        auto     pending = timeline.update(0, true, false, true);
        check(
            pending.phase == Phase::Waiting && pending.menuAlpha == 0 && pending.titleFrame == -1,
            "pending assets must not flash native menu or paper doll"
        );
        check(timeline.update(20000, true, false, true).menuAlpha == 0, "pending frames stay hidden");
        check(
            timeline.update(20100, true, true, true).phase == Phase::MenuOpening,
            "direct title opening without Mojang"
        );
        double now = 20100;
        check(advance(timeline, now, 21979).titleFrame == -1, "title delay preserved");
        auto firstTitle = advance(timeline, now, 21980);
        check(firstTitle.titleFrame == 0 && firstTitle.menuAlpha == 0, "title begins before menu fade");
        check(advance(timeline, now, 22800).menuAlpha == 0, "menu fade start");
        check(std::abs(advance(timeline, now, 23050).menuAlpha - .5f) < .001f, "halfway fade");
        check(advance(timeline, now, 23300).menuAlpha == 1, "fade ends");
        check(advance(timeline, now, 24740).phase == Phase::MenuIdle, "last title frame");

        Timeline hitch;
        hitch.update(0, true, true, true);
        double hitchNow    = 0;
        auto   beforeHitch = advance(hitch, hitchNow, 2800);
        auto   afterHitch  = hitch.update(5800, true, true, true);
        check(
            afterHitch.phase == Phase::MenuOpening && afterHitch.menuAlpha < 1,
            "three-second hitch must not finish opening or fade"
        );
        check(
            std::abs(afterHitch.elapsedMs - beforeHitch.elapsedMs - 1000.0 / 30.0) < .0001
                && afterHitch.titleFrame - beforeHitch.titleFrame <= 1,
            "hitch resumes title and fade with at most one 30Hz frame"
        );

        Timeline menuNotReady;
        check(
            menuNotReady.update(100, false, true, true).phase == Phase::Waiting,
            "assets do not imply menu readiness"
        );
        check(menuNotReady.update(200, true, true, true).phase == Phase::MenuOpening, "separate menu readiness");
        Timeline disabledTitle;
        auto     native = disabledTitle.update(0, true, true, false);
        check(native.phase == Phase::MenuIdle && native.menuAlpha == 1, "disabled title leaves native menu usable");
        Timeline suspended;
        suspended.update(0, true, true, true);
        check(suspended.update(20000, true, true, true).menuAlpha == 1, "window suspension releases menu");
        Timeline reversed;
        reversed.update(100, true, true, true);
        const auto forward = reversed.update(900, true, true, true);
        check(reversed.update(200, true, true, true).elapsedMs == forward.elapsedMs, "clock never moves backwards");
        Timeline smooth;
        smooth.update(0, true, true, true);
        smooth.update(16, true, true, true);
        auto interrupted = smooth.update(32, true, true, true);
        auto resumed = smooth.update(332, true, true, true);
        check(resumed.elapsedMs == interrupted.elapsedMs + 16, "300ms startup pause resumes with last healthy frame");
        Timeline skipped;
        skipped.update(0, true, false, true);
        skipped.skip();
        check(skipped.update(100, true, false, true).menuAlpha == 1, "escape releases pending assets");

        // Run an intro at 60 Hz, starting the sound on the first request; returns the time it released the menu.
        auto runIntro = [](MojangIntro& intro, double& now, double until, bool soundWorks) {
            double released = -1;
            for (; now <= until; now += 16) {
                auto frame = intro.update(now);
                if (frame.requestSound) intro.soundResult(now, soundWorks);
                if (frame.released && released < 0) released = now;
            }
            return released;
        };
        MojangIntro idle;
        check(!idle.update(0).active && !idle.update(0).released, "intro waits for loading to finish");
        MojangIntro intro;
        intro.start(1000);
        auto first = intro.update(1000);
        check(first.active && first.overlayAlpha == 0 && first.logoAlpha == 0, "intro starts transparent");
        auto halfway = intro.update(1000 + IntroTiming::leadInMs / 2);
        check(std::abs(halfway.overlayAlpha - .5f) < .01f && halfway.logoAlpha == 0, "dark overlay fades in first");
        auto opaque = intro.update(1000 + IntroTiming::leadInMs);
        check(opaque.overlayAlpha == 1 && opaque.logoFrame == 0 && opaque.logoAlpha == 0, "logo starts once dark");
        intro.update(1000 + IntroTiming::leadInMs + 16);
        double introNow = 1000 + IntroTiming::leadInMs + 32;
        bool   requested = false;
        for (; introNow < 1000 + IntroTiming::leadInMs + IntroTiming::soundDelayMs + 48; introNow += 16) {
            auto frame = intro.update(introNow);
            requested  = requested || frame.requestSound;
            if (frame.requestSound) check(frame.logoFrame == 350 / 40, "sound is requested at 350 ms of logo time");
        }
        check(requested && !intro.soundStarted(), "sound requested at its sync point");
        auto held = intro.update(introNow);
        check(held.logoFrame == 350 / 40 && !held.requestSound, "logo holds between sound retries");
        auto retry = intro.update(introNow + 200);
        check(retry.requestSound, "sound retried after 200 ms");
        intro.soundResult(introNow + 200, true);
        introNow += 216;
        const double started = introNow;
        const double released = runIntro(intro, introNow, started + 6000, true);
        check(intro.soundStarted(), "sound started");
        check(
            std::abs(released - started - (IntroTiming::totalMs - IntroTiming::soundDelayMs)) <= 32,
            "logo resumes at the sound start and releases the menu when sound and tail end"
        );
        auto after = intro.update(introNow);
        check(after.active && after.released && after.overlayAlpha == 1 && after.studiosAlpha == 1,
            "released intro keeps the finished overlay up for the opening to fade");

        MojangIntro studios;
        studios.start(0);
        double studiosNow = 0;
        IntroFrame beforeStudios, afterStudios;
        for (; studiosNow <= 4000; studiosNow += 16) {
            auto frame = studios.update(studiosNow);
            if (frame.requestSound) studios.soundResult(studiosNow, true);
            if (frame.logoFrame < IntroTiming::frameCount - 1) beforeStudios = frame;
            else if (afterStudios.studiosAlpha == 0 && frame.studiosAlpha > 0) afterStudios = frame;
        }
        check(beforeStudios.studiosAlpha == 0, "STUDIOS hidden during the logo animation");
        check(afterStudios.logoFrame == IntroTiming::frameCount - 1, "STUDIOS fades in after the last logo frame");

        MojangIntro silent;
        silent.start(0);
        double silentNow = 0;
        const double silentRelease = runIntro(silent, silentNow, 20000, false);
        check(!silent.soundStarted() && silentRelease >= 8000
                && silentRelease < 8000 + IntroTiming::totalMs + IntroTiming::leadInMs + 100,
            "missing audio releases the logo after the 8 s fallback");

        // The original outro's timing: sound at 760 ms of logo time, flight right after the finished STUDIOS frame.
        MojangIntro outro(true, 760, 3560);
        outro.start(0);
        double outroNow = 0, outroSound = -1;
        const double outroRelease = [&] {
            for (; outroNow <= 10000; outroNow += 16) {
                auto frame = outro.update(outroNow);
                if (frame.requestSound) {
                    outro.soundResult(outroNow, true);
                    outroSound = outroNow;
                }
                if (frame.released) return outroNow;
            }
            return -1.0;
        }();
        check(outroSound >= IntroTiming::leadInMs + 760 && outroSound < IntroTiming::leadInMs + 760 + 50,
            "custom sound point");
        check(std::abs(outroRelease - outroSound - (3560 - 760)) <= 32, "custom flight start after the sound starts");
        auto outroFrame = outro.update(outroNow);
        check(outroFrame.logoFrame == IntroTiming::frameCount - 1 && outroFrame.studiosAlpha == 1,
            "flight starts on the finished logo and STUDIOS");
        MojangIntro early(true, 350, 1000);
        early.start(0);
        double earlyNow = 0;
        const double earlyRelease = runIntro(early, earlyNow, 10000, true);
        check(earlyRelease >= IntroTiming::leadInMs + IntroTiming::studiosEndMs, "flight never starts before STUDIOS");

        MojangIntro cut(false, IntroTiming::soundDelayMs, IntroTiming::totalMs, false);
        cut.start(0);
        auto cutFrame = cut.update(0);
        check(cutFrame.active && cutFrame.overlayAlpha == 1, "without the fade-in the overlay cuts in at once");

        MojangIntro introHitch(false);
        introHitch.start(0);
        introHitch.update(IntroTiming::leadInMs);
        introHitch.update(IntroTiming::leadInMs + 16);
        check(introHitch.update(3000).logoFrame == 50 / 40, "a hitch advances the logo by at most 50 ms");

        MojangIntro skippedIntro;
        skippedIntro.start(0);
        skippedIntro.update(0);
        skippedIntro.skip();
        check(!skippedIntro.update(16).active && skippedIntro.update(16).released, "escape removes the intro at once");

        auto layout = introPlacement(1920, 1080);
        check(std::abs(layout.logoX + layout.logoWidth / 2 - 960) < .01f
                && std::abs(layout.studiosX + layout.studiosWidth / 2 - 960) < .01f
                && layout.studiosY > layout.logoY + layout.logoHeight,
            "logo and STUDIOS centered, STUDIOS below the logo");
        auto lastLogo = introLogoRegion(78);
        check(lastLogo.u + lastLogo.width <= 1 && lastLogo.v + lastLogo.height <= 1
                && std::abs(lastLogo.u - (8 * 600 + 31) / 6000.0f) < 1e-6f,
            "last logo frame inside the atlas");

        for (int frame = 0; frame < 69; ++frame) {
            auto uv = titleRegion(frame);
            check(frame / 8 < 9 && uv.v >= 0 && uv.v + uv.height <= 1, "title atlas boundaries");
        }
        check(titleRegion(68).v == .5f, "last sheet row");
        auto  placement = titlePlacement(100, 30, 256);
        float scale     = placement.width / 2403;
        check(
            std::abs(placement.x + 518 * scale - 100) < .0001f && std::abs(placement.y + 502 * scale - 30) < .0001f
                && std::abs(1373 * scale - 256) < .0001f,
            "native title content alignment"
        );
        std::cout << "Timeline tests passed.\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
