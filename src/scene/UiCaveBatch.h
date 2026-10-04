#pragma once
#include "scene/CpuProjection.h"
#include <bit>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace animated_mojang::scene {
// Discover the game's packed channel order from four real Tessellator colors.
// Avoid assumptions about ARGB/ABGR or native vertex field enum values.
struct ColorPacking {
    unsigned            red, green, blue;
    std::uint32_t       alpha;
    static ColorPacking fromSamples(std::span<std::uint32_t const> colors) {
        if (colors.size() != 4) throw std::runtime_error("Invalid native color samples");
        const auto black = colors[3];
        const auto r = colors[0] ^ black, g = colors[1] ^ black, b = colors[2] ^ black;
        if (!r || !g || !b || (r & g) || (r & b) || (g & b) || ((r | g | b) & black)
            || (r >> std::countr_zero(r)) != 255 || (g >> std::countr_zero(g)) != 255
            || (b >> std::countr_zero(b)) != 255 || (r | g | b | black) != 0xffffffffu)
            throw std::runtime_error("Unsupported native color packing");
        return {
            static_cast<unsigned>(std::countr_zero(r)),
            static_cast<unsigned>(std::countr_zero(g)),
            static_cast<unsigned>(std::countr_zero(b)),
            black
        };
    }
    std::uint32_t pack(glm::vec3 color, float opacity = 1.f) const {
        const auto byte = [](float c) { return static_cast<std::uint32_t>(std::clamp(c, 0.f, 1.f) * 255.f + .5f); };
        return (byte(opacity) << std::countr_zero(alpha)) | (byte(color.r) << red) | (byte(color.g) << green) | (byte(color.b) << blue);
    }
};
inline constexpr std::size_t caveBatchTriangleLimit = 2048;
// Populate typed SDK vectors with ordinary C++ writes. No per-vertex game calls.
inline void appendUiTriangle(
    ProjectedTriangle const&    triangle,
    ColorPacking const&         packing,
    std::vector<glm::vec3>&     positions,
    std::vector<std::uint32_t>& colors,
    std::vector<glm::vec2>&     uv
) {
    const auto quads = triangleQuads(triangle), textureQuads = triangleQuadUVs(triangle);
    const auto colorQuads = triangleQuadColors(triangle);
    for (std::size_t q = 0; q < quads.size(); ++q) {
        for (int i : {0, 1, 2, 3, 3, 2, 1, 0}) {
            positions.emplace_back(quads[q][i], 0.f);
            colors.push_back(packing.pack(colorQuads[q][i], triangle.opacity));
            uv.push_back(textureQuads[q][i]);
        }
    }
}
// Texture changes split runs, preserving global painter order across materials.
inline std::size_t caveBatchEnd(std::span<ProjectedTriangle const> triangles, std::size_t begin) {
    const auto  limit = std::min(begin + caveBatchTriangleLimit, triangles.size());
    std::size_t end   = begin + 1;
    // High bits are animation attributes, not a change of bound texture.
    while (end < limit && (triangles[end].texture & 0xffffu) == (triangles[begin].texture & 0xffffu)) ++end;
    return end;
}
} // namespace animated_mojang::scene
