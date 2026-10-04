#pragma once

#include "scene/MeshVertex.h"
#include <array>
#include <cstddef>

namespace animated_mojang::scene {
// Counterclockwise outside faces, deliberately emitted front-to-back. A farther
// face must not cover a nearer face simply because it was submitted later.
inline std::array<Vertex, 36> cube(float red, float green, float blue) {
    constexpr std::array<Point, 8> corners{
        {{-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1}, {-1, -1, 1}, {1, -1, 1}, {1, 1, 1}, {-1, 1, 1}}
    };
    constexpr std::array<std::array<std::size_t, 4>, 6> faces{
        {{4, 5, 6, 7}, {1, 0, 3, 2}, {5, 1, 2, 6}, {0, 4, 7, 3}, {7, 6, 2, 3}, {0, 1, 5, 4}}
    };
    constexpr std::array<float, 6>       shade{.85f, .55f, .65f, .72f, 1.f, .4f};
    constexpr std::array<std::size_t, 6> triangle{0, 1, 2, 0, 2, 3};
    std::array<Vertex, 36>               vertices{};
    std::size_t                          next = 0;
    for (std::size_t face = 0; face < faces.size(); ++face)
        for (auto corner : triangle)
            vertices[next++] =
                {corners[faces[face][corner]], red * shade[face], green * shade[face], blue * shade[face]};
    return vertices;
}
} // namespace animated_mojang::scene
