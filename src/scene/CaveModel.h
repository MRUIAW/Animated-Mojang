#pragma once
#include "scene/MeshVertex.h"
#include "scene/CaveAnimation.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace animated_mojang::scene {
inline std::vector<Vertex> loadCaveModel(std::filesystem::path const& path) {
    static_assert(sizeof(Vertex) == 36);
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cave mesh file missing");
    char          magic[8]{};
    std::uint32_t count = 0, bounds[3]{};
    input.read(magic, 8);
    input.read(reinterpret_cast<char*>(&count), 4);
    input.read(reinterpret_cast<char*>(bounds), 12);
    if (!input || std::memcmp(magic, "AMCV0005", 8) != 0 || !count || count > 500000 || count % 3)
        throw std::runtime_error("Invalid cave mesh header");
    for (auto size : bounds)
        if (!size || size > 256) throw std::runtime_error("Invalid cave bounds");
    std::vector<Vertex> vertices(count);
    input.read(reinterpret_cast<char*>(vertices.data()), static_cast<std::streamsize>(count * sizeof(Vertex)));
    if (!input || input.peek() != std::ifstream::traits_type::eof()) throw std::runtime_error("Invalid cave mesh size");
    for (auto const& vertex : vertices) {
        auto const& p = vertex.position;
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z) || p.x < 0 || p.y < 0 || p.z < 0
            || p.x > bounds[0] || p.y > bounds[1] || p.z > bounds[2])
            throw std::runtime_error("Invalid cave mesh vertex");
        for (float c : {vertex.r, vertex.g, vertex.b})
            if (!std::isfinite(c) || c < 0 || c > 1) throw std::runtime_error("Invalid cave mesh color");
        for (float uv : {vertex.u, vertex.v})
            if (!std::isfinite(uv) || uv < 0 || uv > 1) throw std::runtime_error("Invalid cave texture coordinate");
        if (vertex.texture & ~animatedLavaFlag) throw std::runtime_error("Invalid cave texture flags");
        if ((vertex.texture & animatedLavaFlag)
            && (vertex.u > 16.f/256 || vertex.v < 256.f/caveAtlasHeight || vertex.v > 272.f/caveAtlasHeight))
            throw std::runtime_error("Invalid lava base-frame UV");
    }
    for (std::size_t i = 0; i < vertices.size(); i += 3)
        if (vertices[i].texture != vertices[i + 1].texture || vertices[i].texture != vertices[i + 2].texture)
            throw std::runtime_error("Mixed textures in cave triangle");
    return vertices;
}
} // namespace animated_mojang::scene
