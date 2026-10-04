#pragma once

#include <cstdint>

namespace animated_mojang::scene {
struct Point {
    float x, y, z;
};
struct Vertex {
    Point         position;
    float         r, g, b;
    float         u = 0, v = 0;
    std::uint32_t texture = 0;
};
} // namespace animated_mojang::scene
