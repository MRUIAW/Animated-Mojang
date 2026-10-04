#pragma once
#include "scene/CpuProjection.h"
#include <cmath>
#include <cstdint>

namespace animated_mojang::scene {
inline constexpr std::uint32_t animatedLavaFlag = 0x10000;
inline constexpr float caveAtlasHeight = 768.f;
inline unsigned lavaFrame(double timeMs) {
    return static_cast<unsigned>(std::fmod(std::floor(std::max(0.0, timeMs) / 300.0), 32.0));
}
// Called on newly projected triangles, never repeatedly on an existing cache.
// All frames share the same atlas and submission batch; change UVs only.
inline void animateLava(ProjectedTriangle& triangle, double timeMs) {
    if (!(triangle.texture & animatedLavaFlag)) return;
    const float offset = lavaFrame(timeMs) * 16.f / caveAtlasHeight;
    for (auto& uv : triangle.uv) uv.y += offset;
}
} // namespace animated_mojang::scene
