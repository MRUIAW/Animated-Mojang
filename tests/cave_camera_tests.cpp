#include "scene/CaveCamera.h"
#include "scene/CaveModel.h"
#include "scene/CaveOcclusion.h"
#include "scene/UiCaveBatch.h"
#include "scene/CaveOpeningBackdrop.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace animated_mojang;
using namespace animated_mojang::scene;
constexpr std::array pagePoses{caveTitle, caveWorlds, caveSettings, caveDressingRoom, caveRealms, caveMarketplace};
void check(bool ok, char const* message) { if (!ok) throw std::runtime_error(message); }
bool close(CavePose a, CavePose b, float tolerance = .0001f) {
    for (std::size_t i = 0; i < a.size(); ++i) if (std::abs(a[i] - b[i]) > tolerance) return false;
    return true;
}
// Independent scalar reference for the original JOML transform: world point
// translated first, then Y180, yaw, pitch, roll. Do not use GLM matrix builders.
glm::vec3 originalViewPoint(CavePose pose, double ms, glm::vec3 world) {
    const double t = std::max(0.0, ms) / 3000;
    const double strength = 1 - (1 - t * t) * std::exp(-.5 * t * t);
    const double wind = strength * (1 - std::clamp(std::abs(pose[2] - 6.0) / 30, 0.0, 1.0));
    const double wy = -(1 - wind) * wind + std::cos(t * 2) * wind / 5;
    const double yaw = (pose[3] + std::cos(t) * wind) * 3.141592653589793 / 180;
    const double pitch = (pose[4] + std::sin(t) * wind) * 3.141592653589793 / 180;
    const double roll = (pose[5] - std::sin(t) * wind) * 3.141592653589793 / 180;
    double x = -(world.x - pose[0]), y = world.y - pose[1] + wy, z = -(world.z - pose[2]);
    const double a = std::cos(yaw) * x + std::sin(yaw) * z;
    z = -std::sin(yaw) * x + std::cos(yaw) * z; x = a;
    const double b = std::cos(pitch) * y - std::sin(pitch) * z;
    z = std::sin(pitch) * y + std::cos(pitch) * z; y = b;
    return {std::cos(roll) * x - std::sin(roll) * y, std::sin(roll) * x + std::cos(roll) * y, z};
}
int main() {
    try {
        check(cavePageForScreen("start_screen") == CavePage::Main, "JSON main page");
        check(cavePageForScreen("PlayScreen") == CavePage::Worlds, "OreUI play alias");
        check(cavePageForScreen("settings_screen") == CavePage::Settings, "settings page");
        check(cavePageForScreen("settings_screen - video") == CavePage::Settings, "annotated native page name");
        check(cavePageForScreen("non_scrollable_sdl_screen - DressingRoom_PersonaProfile") == CavePage::DressingRoom,
            "actual annotated dressing room shell");
        check(!cavePageForScreen("non_scrollable_sdl_screen - UnrelatedPage"), "unrelated SDL page stays unclassified");
        check(cavePageForScreen("skin_selector_screen - MultiItemPage_PersonaSkinSelector") == CavePage::DressingRoom,
            "dressing room skin selector keeps camera");
        check(cavePageForScreen("store_data_driven_screen - MultiItemPage_StoreRoot") == CavePage::Marketplace,
            "actual native marketplace shell");
        constexpr std::string_view web = "ui://./hbui/index.html";
        check(!cavePageForScreen(web), "shared HTML shell is not a page identity");
        check(cavePageForIdentity({web, web, "/play/worlds", "/main-menu"}) == CavePage::Worlds, "live scene route overrides old router/main");
        check(cavePageForIdentity({web, web, "/oreui-settings/video", "/play/worlds"}) == CavePage::Settings, "same shell changes camera from actual route");
        check(cavePageForIdentity({web, web, web, "/play/worlds"}) == CavePage::Worlds, "router fragment resolves shell-only route");
        check(cavePageForIdentity({"start_screen", "start_screen", "", "/play/worlds"}) == CavePage::Main, "stale web router does not override JSON return");
        check(cavePageForRoute("ui://./hbui/index.html#/oreui-settings/video?from=menu") == CavePage::Settings, "full OreUI URL fragment");
        check(!cavePageForRoute("/playback") && !cavePageForRoute(web) && !cavePageForRoute("/gameplay/inventory"), "route boundary and gameplay exclusion");
        check(cavePageForRoute("/realms-pdp") == CavePage::Realms
            && cavePageForRoute("/realms/test/hub") == CavePage::Realms
            && cavePageForRoute("/oreui-marketplace/search") == CavePage::Marketplace,
            "Bedrock Realms and web marketplace routes");
        check(!cavePageForRoute("/realms-pdp-unrelated"), "new page routes preserve boundaries");
        std::array<CaveStackEntry, 3> stack{{{CavePage::Main}, {CavePage::Worlds}, {CavePage::Settings}}};
        check(cavePageForStackEntries(stack) == CavePage::Settings, "foreground page before closing");
        stack.back().popping = true;
        check(cavePageForStackEntries(stack) == CavePage::Worlds, "return starts while settings still exists and exits");
        check(cavePageForStackEntries(stack, 2) == CavePage::Main, "multi-pop selects actual destination");
        stack.back().popping = false;
        check(cavePageForStackEntries(stack) == CavePage::Settings, "cancelled exit or revisited scene restores live foreground");
        stack.back().page = std::nullopt;
        check(cavePageForStackEntries(stack) == CavePage::Worlds
            && cavePageForStackEntries(stack, 1) == CavePage::Worlds, "unknown modal opening/closing retains parent camera");
        check(!cavePageForStackEntries(stack, 999), "whole-stack pop does not invent a main menu");
        CaveCameraController routed;
        const Frame idle{.phase = Phase::MenuIdle, .menuAlpha = 1, .titleFrame = 68};
        // Check requested landmarks in the actual view transform, not just yaw constants.
        const auto screenPoint = [](CavePose pose, glm::vec3 point) {
            const auto clip = caveProjection(1920, 1080) * caveView(pose, 0) * glm::vec4{point, 1};
            check(clip.w > 0, "requested landmark lies in front of page camera");
            return glm::vec2{clip.x / clip.w, clip.y / clip.w};
        };
        for (auto landmark : {glm::vec3{15.5f, 12.5f, 42}, glm::vec3{15.5f, 13.5f, 42}, glm::vec3{15.5f, 14.5f, 42}}) {
            const auto p = screenPoint(caveDressingRoom, landmark);
            check(std::abs(p.x) < .8f && std::abs(p.y) < .9f, "entrance ledge opposite lava, coal and iron appear in dressing room framing");
        }
        const auto furnace = screenPoint(caveMarketplace, {11, 12.5f, 19.5f});
        check(std::abs(furnace.x) < .1f && std::abs(furnace.y) < .1f, "marketplace frames lit furnace front near center");
        check(close(caveRealms, {18, 13, 18, 10, 0, 1}), "Realms inherits the previous dressing room view");
        for (auto size : {glm::vec2{1920, 1080}, glm::vec2{3840, 2071}, glm::vec2{1080, 1920}}) {
            std::vector<ProjectedTriangle> loading;
            appendCaveOpeningBackdrop(size.x, size.y, 1, caveView(caveOpeningStart,0), loading);
            check(!loading.empty(), "loading screenshot visible at opening");
            float area = 0;
            for (auto const& tri : loading) {
                const auto a = tri.points[1] - tri.points[0], b = tri.points[2] - tri.points[0];
                area += std::abs(a.x * b.y - a.y * b.x) * .5f;
                check((tri.texture & 0xffffu) == caveOpeningTexture, "loading image uses its own texture, not cave atlas");
                for (int i = 0; i < 3; ++i) {
                    const float imageW = std::min(size.x, size.y * 16.f / 9.f), imageH = imageW * 9.f / 16.f;
                    const auto expected = glm::vec2{(size.x-imageW)*.5f, (size.y-imageH)*.5f} + tri.uv[i] * glm::vec2{imageW,imageH};
                    check(glm::length(tri.points[i] - expected) < .02f, "loading frame is centered, upright, unmirrored and preserves screenshot aspect");
                }
            }
            const float expectedArea = std::min(size.x, size.y * 16.f/9.f) * std::min(size.y, size.x * 9.f/16.f);
            check(std::abs(area - expectedArea) < expectedArea * .0001f, "initial screenshot fits viewport without stretching");
        }
        const auto triangleArea = [](std::span<ProjectedTriangle const> triangles) {
            float area = 0;
            for (auto const& t : triangles) {
                const auto a=t.points[1]-t.points[0], b=t.points[2]-t.points[0];
                area += std::abs(a.x*b.y-a.y*b.x)*.5f;
            }
            return area;
        };
        // Independent pinhole-camera check: a 10-block straight retreat must
        // scale by d/(d+10). This distinguishes a fixed world plane from a HUD.
        auto retreatPose = caveOpeningStart;
        retreatPose[2] -= 10;
        std::vector<ProjectedTriangle> retreat;
        appendCaveOpeningBackdrop(1920,1080,1,caveView(retreatPose,0),retreat);
        const float expectedRetreatArea = 1920*1080 * std::pow(caveOpeningBackdropDistance/(caveOpeningBackdropDistance+10),2);
        check(std::abs(triangleArea(retreat)-expectedRetreatArea) < expectedRetreatArea*.0001f,
            "fixed loading plane shrinks according to physical perspective rather than sticking to the screen");
        for (int time : {200, 500, 1000, 1250}) {
            std::vector<ProjectedTriangle> later;
            appendCaveOpeningBackdrop(1920,1080,1,caveView(caveOpeningPose(time),time),later);
            const float area = triangleArea(later)/(1920*1080);
            check(area > .12f && area < .99f, "loading plane recedes gradually instead of collapsing into a tiny tile or being far-clipped");
            std::cout << "Loading plane at " << time << "ms: " << area*100 << "% viewport area.\n";
        }
        std::vector<ProjectedTriangle> finishedBackdrop;
        appendCaveOpeningBackdrop(1920,1080,0,caveView(caveOpeningStart,0),finishedBackdrop);
        check(finishedBackdrop.empty(), "finished loading backdrop contributes no draw batches");
        routed.update(0, cavePageForIdentity({"start_screen", "start_screen", "", ""}), idle);
        const auto playPage = cavePageForIdentity({web, web, "/play/worlds", ""});
        routed.update(100, playPage, idle);
        for (int i = 1; i <= 5; ++i) routed.update(100 + i * 100, playPage, idle);
        auto playEnd = routed.update(600, playPage, idle);
        check(close(playEnd.pose, caveWorlds), "shared HTML play route reaches worlds pose");
        const auto settingsPage = cavePageForIdentity({web, web, "/oreui-settings/video", ""});
        auto settingsStart = routed.update(700, settingsPage, idle);
        check(settingsStart.revision > playEnd.revision && close(settingsStart.pose, caveWorlds), "same HTML shell starts new settings transition");
        for (int i = 1; i <= 5; ++i) routed.update(700 + i * 100, settingsPage, idle);
        check(close(routed.update(1200, settingsPage, idle).pose, caveSettings), "same HTML settings route reaches settings pose");
        for (auto pose : {caveTitle, caveWorlds, caveSettings, caveOpeningStart, caveOpeningTransfer})
            for (double ms : {0., 250., 1500., 4000., 10000., 60000.})
                for (auto point : {glm::vec3{0, 0, 0}, glm::vec3{24, 14, 42}, glm::vec3{10, 25, 70}}) {
                    const auto actual = glm::vec3{caveView(pose, ms) * glm::vec4{point, 1}};
                    check(glm::length(actual - originalViewPoint(pose, ms, point)) < .0002f, "matrix matches original scalar point transform");
                    const auto eye = glm::vec3{glm::inverse(caveView(pose, ms))[3]};
                    check(std::abs(eye.x - pose[0]) < .0001f && std::abs(eye.z - pose[2]) < .0001f, "no invented horizontal camera displacement");
                }
        const float yaw = glm::radians(21.f), pitch = glm::radians(13.f);
        const glm::vec3 forward{-std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
        const auto center = caveView(caveTitle, 0) * glm::vec4{glm::vec3{31, 13, 6} + forward * 10.f, 1};
        check(std::abs(center.x) < .0001f && std::abs(center.y) < .0001f && std::abs(center.z + 10) < .0001f,
            "original title looks toward negative X, positive Y/Z");
        check(!cavePageForScreen("hud_screen") && !cavePageForScreen("progress_screen")
            && !cavePageForScreen("pause_screen") && !cavePageForScreen("popup_screen"), "unrecognized pages hold camera");
        check(close(caveOpeningPose(0), caveOpeningStart) && close(caveOpeningPose(4000), caveTitle), "flight endpoints");
        for (int i = 0; i < 6; ++i) {
            check(std::abs(cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], .5f)
                - caveOpeningTransfer[i]) < .0001f, "spline goes through transfer point");
            const float left = (cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], .5f)
                - cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], .4999f)) / .0001f;
            const float right = (cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], .5001f)
                - cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], .5f)) / .0001f;
            check(std::abs(left - right) < .2f, "spline derivative continuity");
        }
        float lastEase = 0;
        for (int i = 0; i <= 1000; ++i) {
            const float value = cameraEase(i / 1000.f);
            check(value >= lastEase && value >= 0 && value <= 1, "monotonic bounded easing");
            lastEase = value;
        }
        CaveCameraController camera;
        Timeline timeline;
        check(close(CaveCameraFrame{}.pose,caveOpeningStart) && CaveCameraFrame{}.loadingBackdropAlpha==1,
            "background before first presentation update starts with the loading plane");
        for (double time : {0., 50., 500., 10000.}) {
            const auto early = camera.update(time,std::nullopt,timeline.update(time,false,true,true));
            check(close(early.pose,caveOpeningStart) && early.loadingBackdropAlpha==1 && !early.moving,
                "pre-menu toast and unresolved pages must not flash the idle cave camera");
        }
        camera.reset();
        timeline = Timeline{};
        auto frame = timeline.update(0, true, false, true);
        const auto waiting = camera.update(0, CavePage::Main, frame);
        check(close(waiting.pose, caveOpeningStart) && waiting.loadingBackdropAlpha == 1, "wait at flight start with loading image");
        frame = timeline.update(10000, true, true, true);
        check(close(camera.update(10000, CavePage::Main, frame).pose, caveOpeningStart), "delayed resources do not skip flight");
        for (int i = 1; i <= 120; ++i) {
            const double time = 10000 + i * 40;
            frame = timeline.update(time, true, true, true);
            const auto value = camera.update(time, CavePage::Main, frame);
            check(close(value.pose, caveOpeningPose(frame.elapsedMs)), "camera and title share bounded elapsed time");
            if (frame.elapsedMs >= 1500) check(value.loadingBackdropAlpha == 0, "loading screen disappears before animated title starts");
        }
        check(close(camera.update(14800, CavePage::Main, frame).pose, caveTitle), "flight ends at title");
        auto first = camera.update(14800, CavePage::Settings, frame);
        check(close(first.pose, caveTitle) && first.moving, "page switch begins without jump");
        camera.update(14900, CavePage::Settings, frame);
        auto interrupted = camera.update(15000, CavePage::Settings, frame);
        auto retargeted = camera.update(15000, CavePage::Worlds, frame);
        check(close(interrupted.pose, retargeted.pose), "mid-transition retarget keeps current pose");
        check(retargeted.revision > interrupted.revision, "retarget invalidates render cache");
        for (int i = 1; i <= 5; ++i) camera.update(15000 + 100 * i, std::nullopt, frame);
        check(close(camera.update(15500, CavePage::Worlds, frame).pose, caveWorlds), "unknown modal preserves target");
        camera.update(15500, CavePage::Main, frame);
        for (int i = 1; i <= 5; ++i) camera.update(15500 + 100 * i, CavePage::Main, frame);
        check(close(camera.update(16000, CavePage::Main, frame).pose, caveTitle), "return does not replay flight");
        camera.replay();
        timeline = Timeline{};
        frame = timeline.update(16000, true, true, true);
        camera.update(16000, CavePage::Main, frame);
        frame = timeline.update(16100, true, true, true);
        auto before = camera.update(16100, CavePage::Main, frame);
        frame = timeline.update(19100, true, true, true);
        auto after = camera.update(19100, CavePage::Main, frame);
        check(close(after.pose, caveOpeningPose(100 + 1000.0 / 30.0)), "hitch resumes opening camera with at most one 30Hz frame");
        timeline.skip();
        frame = timeline.update(19100, true, true, true);
        auto skipped = camera.update(19100, CavePage::Main, frame);
        check(close(skipped.pose, caveTitle) && skipped.revision > after.revision && !skipped.moving, "Esc finishes camera immediately");
        check(skipped.loadingBackdropAlpha == 0, "Esc also removes loading backdrop");
        check(close(camera.update(100, CavePage::Main, frame).pose, caveTitle), "clock reversal is safe");
        camera.reset();
        timeline = Timeline{};
        frame = timeline.update(0, true, true, false);
        check(close(camera.update(0, CavePage::Main, frame).pose, caveTitle), "disabled title skips flight");
        camera.reset();
        frame = {};
        check(close(camera.update(0, CavePage::Worlds, frame).pose, caveWorlds), "enable on existing page uses that pose");
        CaveCameraController stalled, uninterrupted;
        stalled.update(0, CavePage::Main, idle);
        uninterrupted.update(0, CavePage::Main, idle);
        stalled.update(16, CavePage::Settings, idle);
        uninterrupted.update(16, CavePage::Settings, idle);
        const auto recovered = stalled.update(3016, CavePage::Settings, idle);
        const auto ordinary = uninterrupted.update(32, CavePage::Settings, idle);
        check(close(recovered.pose, ordinary.pose) && recovered.moving,
            "three-second page hitch resumes with one healthy frame, not 100ms or completion");
        for (int i = 1; i <= 31; ++i) stalled.update(3016 + i * 16, CavePage::Settings, idle);
        check(close(stalled.update(3512, CavePage::Settings, idle).pose, caveSettings), "hitch recovery completes through visible frames");
        const auto returning = stalled.update(3512, cavePageForStackEntries(std::array<CaveStackEntry, 2>{{{CavePage::Main}, {CavePage::Settings, true}}}), idle);
        const auto returningNext = stalled.update(3528, CavePage::Main, idle);
        check(returning.moving && !close(returningNext.pose, caveSettings), "camera already retreats during outgoing UI exit");
        for (auto page : {CavePage::DressingRoom, CavePage::Realms, CavePage::Marketplace}) {
            CaveCameraController added;
            added.update(0, CavePage::Main, idle);
            added.update(16, page, idle);
            for (int i = 1; i <= 32; ++i) added.update(16 + i * 16, page, idle);
            check(close(added.update(528, page, idle).pose, cavePagePose(page)), "added page reaches its own pose");
        }

        const auto model = loadCaveModel("resources/data/normal_cave.mesh");
        std::vector<ProjectedTriangle> triangles;
        std::size_t samples = 0, maxTriangles = 0, maxBatches = 0, fallbacks = 0, empty = 0;
        double totalMs = 0, maxMs = 0;
        std::ofstream positions("build/cave-camera-path.csv");
        positions << "kind,time,x,y,z,yaw,pitch,roll,eyeX,eyeY,eyeZ\n";
        auto writePose = [&](char const* kind, double time, CavePose pose) {
            positions << kind << ',' << time;
            for (auto v : pose) positions << ',' << v;
            const auto eye = glm::vec3{glm::inverse(caveView(pose, time))[3]};
            positions << ',' << eye.x << ',' << eye.y << ',' << eye.z << '\n';
        };
        auto inspect = [&](CavePose pose, double time, glm::vec2 size, bool opening = false) {
            auto start = std::chrono::steady_clock::now();
            triangles.clear();
            projectTriangles(model, caveView(pose, time), caveProjection(size.x, size.y), size.x, size.y, triangles);
            if (opening && time < 1500) {
                appendCaveOpeningBackdrop(size.x,size.y,1-Timeline::unit((time-1000)/500),caveView(pose,time),triangles);
            }
            auto stats = resolveCaveOcclusion(triangles);
            fallbacks += stats.fallbacks;
            sortBackToFront(triangles);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            totalMs += ms; maxMs = std::max(maxMs, ms);
            ++samples;
            empty += triangles.empty();
            maxTriangles = std::max(maxTriangles, triangles.size());
            std::size_t batches = 0;
            for (std::size_t begin = 0; begin < triangles.size(); ++batches) begin = caveBatchEnd(triangles, begin);
            maxBatches = std::max(maxBatches, batches);
            for (auto const& t : triangles) for (auto p : t.points)
                check(std::isfinite(p.x) && std::isfinite(p.y) && p.x >= -.01f && p.y >= -.01f
                    && p.x <= size.x + .01f && p.y <= size.y + .01f, "new camera clips within viewport");
        };
        for (int step = 0; step <= 120; ++step) {
            const double time = step * (4000.0 / 120);
            auto pose = caveOpeningPose(time);
            inspect(pose, time, {1920, 1080}, true);
        }
        for (auto from : pagePoses) for (auto to : pagePoses) {
            if (from == to) continue;
            for (int step = 0; step <= 15; ++step) {
                auto pose = from;
                const float weight = cameraEase(step / 15.f);
                for (int i = 0; i < 6; ++i) pose[i] += (to[i] - from[i]) * weight;
                inspect(pose, 5000 + step * (500.0 / 15), {1920, 1080});
                check(!triangles.empty(), "all page paths have visible cave geometry");
            }
        }
        for (auto size : {glm::vec2{640, 346}, glm::vec2{1080, 1920}, glm::vec2{3840, 2160}})
            for (auto pose : pagePoses) inspect(pose, 1000, size);
        check(fallbacks == 0, "camera paths do not exceed occlusion fragment guard");
        // Dense physical eye samples, consumed by the independent source-NBT
        // collision check. Rendering the full scene at every millisecond is unnecessary.
        for (int offset : {0, 10000, 30000, 60000}) {
        for (int time = 0; time <= 4000; ++time) writePose("opening", offset + time, caveOpeningPose(time));
        for (auto from : pagePoses) for (auto to : pagePoses) {
            if (from == to) continue;
            for (int time = 0; time <= 500; ++time) {
                auto pose = from;
                const float weight = cameraEase(time / 500.f);
                for (int i = 0; i < 6; ++i) pose[i] += (to[i] - from[i]) * weight;
                writePose("page", offset + 5000 + time, pose);
            }
        }
        }
        std::cout << "Camera checks passed: timing, skip, replay, resources, retarget, aliases; " << samples
            << " actual path/window cases, empty=" << empty << ", max triangles/batches=" << maxTriangles << '/'
            << maxBatches << ", depth fallbacks=" << fallbacks << ", mean/max CPU projection+depth+sort="
            << totalMs / samples << '/' << maxMs << "ms (excludes native submission/GPU).\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
