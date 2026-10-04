#pragma once
#include "animation/Timeline.h"
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace animated_mojang::scene {
enum class CavePage { Main, Worlds, Settings, DressingRoom, Realms, Marketplace };
using CavePose = std::array<float, 6>; // x, y, z, yaw, pitch, roll
inline constexpr CavePose caveTitle{31, 13, 6, 21, -13, 0};
inline constexpr CavePose caveWorlds{23, 17, 15, -31, 7, 5};
inline constexpr CavePose caveSettings{23, 13, 15, -15, -13, -5};
// Dressing room frames the entrance ledge opposite the lava at (24,14,42).
// Realms inherits the
// previous dressing room (upstream account) view; marketplace faces the furnace.
inline constexpr CavePose caveDressingRoom{20, 13, 37, 42, -4, 0};
inline constexpr CavePose caveRealms{18, 13, 18, 10, 0, 1};
inline constexpr CavePose caveMarketplace{18, 13, 18, 79, 4, 0};
inline constexpr CavePose caveOpeningStart{17, 13, 82, 0, 0, -90};
inline constexpr CavePose caveOpeningTransfer{20, 12.8f, 44, 8, -5, -10};
inline CavePose cavePagePose(CavePage page) {
    switch (page) {
    case CavePage::Worlds: return caveWorlds;
    case CavePage::Settings: return caveSettings;
    case CavePage::DressingRoom: return caveDressingRoom;
    case CavePage::Realms: return caveRealms;
    case CavePage::Marketplace: return caveMarketplace;
    default: return caveTitle;
    }
}
inline std::optional<CavePage> cavePageForScreen(std::string_view name) {
    // Explicit aliases only; loading, HUD and unknown modal screens retain the parent camera.
    std::string key;
    for (unsigned char c : name) if (std::isalnum(c)) key += static_cast<char>(std::tolower(c));
    // The SDL shell serves unrelated pages: inspect its annotation before trimming it.
    if (key.starts_with("nonscrollablesdlscreendressingroom")) return CavePage::DressingRoom;
    name = name.substr(0, name.find(" - "));
    key.clear();
    for (unsigned char c : name) if (std::isalnum(c)) key += static_cast<char>(std::tolower(c));
    if (key == "startscreen" || key == "mainmenuscreen" || key == "mainmenu") return CavePage::Main;
    if (key == "playscreen" || key == "worldsscreen" || key == "worldselectscreen"
        || key == "worldselectionscreen" || key == "createworldscreen") return CavePage::Worlds;
    if (key == "settingsscreen" || key == "optionsscreen" || key == "settings"
        || key == "globalresourcepackscreen" || key == "resourcepacksscreen") return CavePage::Settings;
    if (key == "skinselectorscreen" || key == "skinpickerscreen" || key == "personascreen"
        || key == "dressingroomscreen") return CavePage::DressingRoom;
    if (key == "storedatadrivenscreen" || key == "storescreen" || key == "marketplacescreen") return CavePage::Marketplace;
    if (key == "realmsscreen" || key == "realmspurchasescreen") return CavePage::Realms;
    return std::nullopt;
}
inline std::optional<CavePage> cavePageForRoute(std::string_view route) {
    // OreUI screen names identify the shared HTML shell; the fragment is the page.
    if (const auto hash = route.find('#'); hash != std::string_view::npos) route.remove_prefix(hash + 1);
    else if (route.find("://") != std::string_view::npos) return std::nullopt;
    route = route.substr(0, route.find_first_of("?#"));
    const auto is = [&](std::string_view root) {
        return route == root || (route.starts_with(root) && route.size() > root.size() && route[root.size()] == '/');
    };
    if (is("/main-menu")) return CavePage::Main;
    if (is("/play") || is("/create-new-world") || is("/edit-world") || is("/start-from-template")) return CavePage::Worlds;
    if (is("/oreui-settings") || is("/storage-settings")) return CavePage::Settings;
    if (is("/dressing-room") || is("/sign-in-dressing-room")) return CavePage::DressingRoom;
    if (is("/oreui-marketplace") || is("/sign-in-marketplace")) return CavePage::Marketplace;
    if (is("/realms") || is("/join-realms-server") || is("/post-to-realm-stories")) return CavePage::Realms;
    for (auto root : {"/realms-pdp", "/realms-plan-picker-v1", "/realms-plan-picker-v2", "/realms-choose-realm",
            "/realms-choose-slot", "/realms-purchase-complete-v1", "/realms-purchase-complete-v2",
            "/realms-purchase-subscription-v1", "/realms-purchase-subscription-v2", "/realms-stories",
            "/realms-new-story", "/realms-story-comments", "/realms-stories-settings", "/realms-feed-notifiers"})
        if (is(root)) return CavePage::Realms;
    return std::nullopt;
}
struct CaveScreenIdentity {
    std::string_view rawName, name, route, routerRoute;
};
inline std::optional<CavePage> cavePageForIdentity(CaveScreenIdentity screen) {
    if (auto page = cavePageForRoute(screen.route)) return page;
    const bool web = screen.rawName.find("/hbui/") != std::string_view::npos
        || screen.name.find("/hbui/") != std::string_view::npos;
    // Router history is relevant only to a live web scene, not a returned JSON main menu.
    if (web) if (auto page = cavePageForRoute(screen.routerRoute)) return page;
    if (auto page = cavePageForScreen(screen.rawName)) return page;
    return cavePageForScreen(screen.name);
}
struct CaveStackEntry {
    std::optional<CavePage> page;
    bool popping = false;
};
inline std::optional<CavePage> cavePageForStackEntries(std::span<CaveStackEntry const> entries, std::size_t popCount = 0) {
    // Stack order is bottom-to-top. Resolve the destination while the outgoing
    // UI remains visible, without confusing push transitions with a pop.
    auto end = entries.size() - std::min(popCount, entries.size());
    while (end > 0) {
        auto const& entry = entries[--end];
        if (!entry.popping && entry.page) return entry.page;
    }
    return std::nullopt;
}
inline float cameraEase(float progress) {
    const float x = std::clamp(progress, 0.f, 1.f);
    if (x == 0 || x == 1) return x;
    auto curve = [](float t, float a, float b) {
        const float u = 1 - t;
        return 3 * u * u * t * a + 3 * u * t * t * b + t * t * t;
    };
    float lo = 0, hi = 1;
    for (int i = 0; i < 24; ++i) {
        const float t = (lo + hi) * .5f;
        if (curve(t, .2f, .2f) < x) lo = t; else hi = t;
    }
    return curve((lo + hi) * .5f, .32f, 1.f);
}
inline float cameraSpline(float first, float middle, float last, float progress) {
    constexpr float g0 = .5f, g1 = 1.f / (4.f - g0), g2 = 1.f / (2.f - g1);
    const float a = 3 * (middle - first) * g0;
    const float b = (3 * (last - first) - a) * g1;
    const float d2 = (3 * (last - middle) - b) * g2, d1 = b - g1 * d2, d0 = a - g0 * d1;
    const float t = std::clamp(progress, 0.f, 1.f) * 2;
    auto cubic = [](float p, float d, float q, float e, float u) {
        return ((2 * (p - q) + d + e) * u + 3 * (q - p) - 2 * d - e) * u * u + d * u + p;
    };
    return t < 1 ? cubic(first, d0, middle, d1, t) : cubic(middle, d1, last, d2, t - 1);
}
inline CavePose caveOpeningPose(double elapsedMs) {
    const float t = cameraEase(static_cast<float>(elapsedMs / 4000.0));
    CavePose pose{};
    for (std::size_t i = 0; i < pose.size(); ++i)
        pose[i] = cameraSpline(caveOpeningStart[i], caveOpeningTransfer[i], caveTitle[i], t);
    return pose;
}
struct CaveCameraFrame {
    CavePose pose = caveOpeningStart;
    bool moving = false;
    std::uint64_t revision = 0;
    double motionTimeMs = 0;
    float loadingBackdropAlpha = 1;
};
class CaveCameraController {
    CavePose pose = caveOpeningStart, start = caveOpeningStart, target = caveTitle;
    CavePage page = CavePage::Main;
    double lastTime = -1, clock = 0, transitionAt = -1, motionTime = 0;
    double healthyStep = 1000.0 / 60.0;
    bool observed = false, openingDone = false;
    std::uint64_t revision = 0;
public:
    void reset() { *this = CaveCameraController{}; }
    void replay() {
        openingDone = false;
        observed = false;
        transitionAt = -1;
        motionTime = 0;
        ++revision;
    }
    CaveCameraFrame update(double nowMs, std::optional<CavePage> observedPage, Frame const& frame) {
        if (!std::isfinite(nowMs)) return {pose, false, revision, motionTime, !observed && !openingDone ? 1.f : 0.f};
        const double gap = lastTime >= 0 ? std::max(nowMs - lastTime, 0.0) : 0;
        // A stalled renderer cannot show new frames. Discard the blocked time
        // and resume with one ordinary frame instead of consuming 100ms at once.
        const double step = gap > 100 ? std::min(healthyStep, 1000.0 / 30.0) : gap;
        if (gap > 0 && gap <= 100) healthyStep = gap;
        clock += step;
        if (observed) motionTime += step;
        lastTime = std::max(lastTime, nowMs);
        // Resolve the old interpolation first, so interrupted transitions never jump.
        if (transitionAt >= 0) {
            const float t = cameraEase(static_cast<float>((clock - transitionAt) / 500.0));
            for (std::size_t i = 0; i < pose.size(); ++i) pose[i] = start[i] + (target[i] - start[i]) * t;
            if (clock - transitionAt >= 500) transitionAt = -1;
        }
        if (observedPage && (!observed || *observedPage != page)) {
            page = *observedPage;
            if (page != CavePage::Main) openingDone = true;
            start = pose;
            target = cavePagePose(page);
            transitionAt = observed ? clock : -1;
            if (!observed && (openingDone || frame.phase == Phase::MenuIdle)) pose = target;
            observed = true;
            ++revision;
        }
        bool moving = transitionAt >= 0;
        if (!observed && frame.phase == Phase::Waiting) {
            // Notifications can render the cubemap before the first main page.
            // Keep the initial loading plane facing the camera during that gap.
            pose = caveOpeningStart;
            motionTime = 0;
        }
        if (observed && page == CavePage::Main && !openingDone) {
            if (frame.phase == Phase::Waiting) { pose = caveOpeningStart; motionTime = 0; }
            else if (frame.phase == Phase::MenuOpening && frame.elapsedMs < 4000) {
                pose = caveOpeningPose(frame.elapsedMs);
                motionTime = frame.elapsedMs;
                moving = true;
            } else {
                pose = caveTitle; // Esc / timeout / disabled title also complete the flight.
                openingDone = true;
                ++revision;
            }
            transitionAt = -1;
        }
        // The fixed world loading plane fades before the title
        // starts at 1880ms. Skip/replay use the same presentation clock.
        const float loadingAlpha = !observed && frame.phase == Phase::Waiting ? 1.f
            : observed && page == CavePage::Main && !openingDone
            ? frame.phase == Phase::Waiting ? 1.f
              : frame.phase == Phase::MenuOpening ? 1.f - Timeline::unit((frame.elapsedMs - 1000) / 500) : 0.f
            : 0.f;
        return {pose, moving, revision, motionTime, loadingAlpha};
    }
};
inline glm::mat4 caveView(CavePose pose, double timeMs) {
    const float t = static_cast<float>(std::max(0.0, timeMs) / 3000.0);
    const float strength = 1.f - (1.f - t * t) * std::exp(-.5f * t * t);
    const float wind = strength * (1.f - std::clamp(std::abs(pose[2] - 6.f) / 30.f, 0.f, 1.f));
    const float windY = -(1.f - wind) * wind + std::cos(t * 2.f) * wind / 5.f;
    const float yaw = glm::radians(pose[3] + std::cos(t) * wind);
    const float pitch = glm::radians(pose[4] + std::sin(t) * wind);
    const float roll = glm::radians(pose[5] - std::sin(t) * wind);
    // Match JOML cameraPoseAt exactly: Rz * Rx * Ry * Ry(180) * T(-eye).
    // Reconstructing lookAt from the upstream forward fields reverses yaw.
    auto view = glm::rotate(glm::mat4{1}, roll, glm::vec3{0, 0, 1});
    view = glm::rotate(view, pitch, glm::vec3{1, 0, 0});
    view = glm::rotate(view, yaw, glm::vec3{0, 1, 0});
    view = glm::rotate(view, glm::radians(180.f), glm::vec3{0, 1, 0});
    return glm::translate(view, glm::vec3{-pose[0], -(pose[1] - windY), -pose[2]});
}
inline glm::mat4 caveView(double timeMs) { return caveView(caveTitle, timeMs); }
inline glm::mat4 caveProjection(float width, float height) {
    return glm::perspectiveRH_ZO(glm::radians(50.f), width / height, .05f, 120.f);
}
} // namespace animated_mojang::scene
