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
