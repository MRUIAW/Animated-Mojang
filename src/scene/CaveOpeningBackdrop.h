#pragma once
#include "scene/CaveCamera.h"
#include "scene/CpuProjection.h"

namespace animated_mojang::scene {
inline constexpr std::uint32_t caveOpeningTexture = 1;
// The old 2-block distance made the 76-block camera retreat reduce the picture
// to a tiny tile almost immediately. Scale the fixed plane's size and distance
// together: identical initial framing, gradual physical perspective afterwards.
inline constexpr float caveOpeningBackdropDistance = 64.f;
inline std::array<Vertex, 6> caveOpeningBackdrop(float viewportAspect) {
    constexpr float imageAspect = 16.f / 9.f;
    const float h = caveOpeningBackdropDistance * std::tan(glm::radians(25.f))
        * std::min(1.f, viewportAspect / imageAspect);
    const float w = h * imageAspect;
    const auto initialWorld = glm::inverse(caveView(caveOpeningStart, 0));
    constexpr std::array<glm::vec2, 4> corners{{{-1, 1}, {-1, -1}, {1, -1}, {1, 1}}};
    constexpr std::array<glm::vec2, 4> uv{{{0, 0}, {0, 1}, {1, 1}, {1, 0}}};
    constexpr std::array<int, 6> indices{{0, 1, 2, 0, 2, 3}};
    std::array<Vertex, 6> vertices{};
    for (std::size_t i = 0; i < indices.size(); ++i) {
        const auto j = indices[i];
        const auto p = initialWorld * glm::vec4{corners[j].x*w, corners[j].y*h, -caveOpeningBackdropDistance, 1};
        vertices[i] = {{p.x,p.y,p.z}, 1,1,1, uv[j].x,uv[j].y, caveOpeningTexture | 0x20000u};
    }
    return vertices;
}
inline void appendCaveOpeningBackdrop(float width, float height, float opacity, glm::mat4 const& view,
    std::vector<ProjectedTriangle>& output) {
    if (width <= 0 || height <= 0 || opacity <= 0) return;
    const auto plane = caveOpeningBackdrop(width/height);
    // This exterior plane remains visible beyond the cave's own clip distance.
    // A separate far clip avoids cutting it partway through the flight.
    const auto projection = glm::perspectiveRH_ZO(glm::radians(50.f), width/height, .05f, 512.f);
    const auto begin = output.size();
    projectTriangles(plane, view, projection, width, height, output);
    for (auto i = begin; i < output.size(); ++i) output[i].opacity = std::clamp(opacity,0.f,1.f);
}
} // namespace animated_mojang::scene
