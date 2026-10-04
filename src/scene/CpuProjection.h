#pragma once

#include "scene/MeshVertex.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>
#include <span>
#include <vector>

namespace animated_mojang::scene {
struct ProjectedTriangle {
    std::array<glm::vec2, 3> points;
    glm::vec3                color;
    float                    distance;
    std::array<glm::vec2, 3> uv{};
    std::array<float, 3>     inverseW{1, 1, 1};
    std::uint32_t            texture = 0;
    std::array<glm::vec3, 3> colors{color, color, color};
    float                    opacity = 1.f;
};

inline std::array<std::array<glm::vec3, 4>, 3> triangleQuadColors(ProjectedTriangle const& triangle) {
    const auto a = triangle.colors[0], b = triangle.colors[1], c = triangle.colors[2];
    const auto wa = triangle.inverseW[0], wb = triangle.inverseW[1], wc = triangle.inverseW[2];
    const auto ab = (a * wa + b * wb) / (wa + wb);
    const auto bc = (b * wb + c * wc) / (wb + wc);
    const auto ca = (c * wc + a * wa) / (wc + wa);
    const auto center = (a * wa + b * wb + c * wc) / (wa + wb + wc);
    return {std::array<glm::vec3, 4>{a, ab, center, ca}, {b, bc, center, ab}, {c, ca, center, bc}};
}

// At screen-space subdivision points, interpolate UV / W and 1 / W first.
// The UI shader is affine; these extra points reduce close-face distortion.
inline std::array<std::array<glm::vec2, 4>, 3> triangleQuadUVs(ProjectedTriangle const& triangle) {
    const auto a = triangle.uv[0], b = triangle.uv[1], c = triangle.uv[2];
    const auto wa = triangle.inverseW[0], wb = triangle.inverseW[1], wc = triangle.inverseW[2];
    const auto ab     = (a * wa + b * wb) / (wa + wb);
    const auto bc     = (b * wb + c * wc) / (wb + wc);
    const auto ca     = (c * wc + a * wa) / (wc + wa);
    const auto center = (a * wa + b * wb + c * wc) / (wa + wb + wc);
    return {
        std::array<glm::vec2, 4>{a, ab, center, ca},
        {b, bc, center, ab},
        {c, ca, center, bc}
    };
}

// Preserve the legacy UI's non-degenerate QuadList contract. Each triangle is
// covered by three convex quads; this avoids degenerate normal/UV generation.
inline std::array<std::array<glm::vec2, 4>, 3> triangleQuads(ProjectedTriangle const& triangle) {
    const auto a = triangle.points[0], b = triangle.points[1], c = triangle.points[2];
    const auto ab = (a + b) * .5f, bc = (b + c) * .5f, ca = (c + a) * .5f;
    const auto center = (a + b + c) / 3.f;
    return {
        std::array<glm::vec2, 4>{a, ab, center, ca},
        {b, bc, center, ab},
        {c, ca, center, bc}
    };
}
namespace cpu_projection_detail {
inline bool finite(glm::vec4 const& p) {
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.w);
}
// Homogeneous D3D clip volume; clip BEFORE perspective division.
inline float planeDistance(glm::vec4 const& p, int plane) {
    switch (plane) {
    case 0:
        return p.w + p.x;
    case 1:
        return p.w - p.x;
    case 2:
        return p.w + p.y;
    case 3:
        return p.w - p.y;
    case 4:
        return p.z;
    default:
        return p.w - p.z;
    }
}
struct ClipVertex {
    glm::vec4 position;
    glm::vec2 uv;
    glm::vec3 color;
};
inline std::size_t clipPolygon(std::array<ClipVertex, 12>& polygon, std::size_t count) {
    std::array<ClipVertex, 12> result{};
    for (int plane = 0; plane < 6 && count; ++plane) {
        std::size_t nextCount        = 0;
        auto        previous         = polygon[count - 1];
        float       previousDistance = planeDistance(previous.position, plane);
        for (std::size_t i = 0; i < count; ++i) {
            const auto  current         = polygon[i];
            const float currentDistance = planeDistance(current.position, plane);
            if ((previousDistance >= 0) != (currentDistance >= 0)) {
                if (nextCount == result.size()) return 0;
                const float t       = previousDistance / (previousDistance - currentDistance);
                result[nextCount++] = {
                    previous.position + (current.position - previous.position) * t,
                    previous.uv + (current.uv - previous.uv) * t,
                    previous.color + (current.color - previous.color) * t
                };
            }
            if (currentDistance >= 0) {
                if (nextCount == result.size()) return 0;
                result[nextCount++] = current;
            }
            previous         = current;
            previousDistance = currentDistance;
        }
        count   = nextCount;
        polygon = result;
    }
    return count;
}
} // namespace cpu_projection_detail

// Exterior triangles with RGB lighting/UVs -> logical menu pixels. The game receives
// neither a perspective matrix nor clip W, and its UI camera stays unchanged.
inline std::size_t projectTriangles(
    std::span<Vertex const>         vertices,
    glm::mat4 const&                modelView,
    glm::mat4 const&                projection,
    float                           width,
    float                           height,
    std::vector<ProjectedTriangle>& output
) {
    if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0) return 0;
    const auto initialSize = output.size();
    for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
        std::array<glm::vec4, 3> camera{};
        bool                     valid = true;
        for (std::size_t j = 0; j < 3; ++j) {
            const auto p = vertices[i + j].position;
            camera[j]    = modelView * glm::vec4{p.x, p.y, p.z, 1};
            valid        = valid && cpu_projection_detail::finite(camera[j]);
        }
        if (!valid) continue;
        const glm::vec3 a{camera[0]}, b{camera[1]}, c{camera[2]};
        // Outward CCW face normal must point toward the camera-space origin.
        if (glm::dot(glm::cross(b - a, c - a), -a) <= 0) continue;
        std::array<cpu_projection_detail::ClipVertex, 12> polygon{};
        for (std::size_t j = 0; j < 3; ++j) {
            polygon[j] = {
                projection * camera[j],
                {vertices[i + j].u, vertices[i + j].v},
                {vertices[i + j].r, vertices[i + j].g, vertices[i + j].b}
            };
            valid = valid && cpu_projection_detail::finite(polygon[j].position);
        }
        if (!valid) continue;
        // Most cave faces are either entirely on screen or wholly outside a
        // common plane. Avoid six polygon copies for those two common cases.
        unsigned outsideAny = 0, outsideAll = 63;
        for (std::size_t j = 0; j < 3; ++j) {
            unsigned outside = 0;
            for (int plane = 0; plane < 6; ++plane)
                if (cpu_projection_detail::planeDistance(polygon[j].position, plane) < 0) outside |= 1u << plane;
            outsideAny |= outside;
            outsideAll &= outside;
        }
        if (outsideAll) continue;
        const auto count = outsideAny ? cpu_projection_detail::clipPolygon(polygon, 3) : 3;
        if (count < 3) continue;
        std::array<glm::vec2, 12> pixels{};
        for (std::size_t j = 0; j < count; ++j) {
            if (polygon[j].position.w <= 0) {
                valid = false;
                break;
            }
            const auto ndc = glm::vec3{polygon[j].position} / polygon[j].position.w;
            pixels[j]      = {
                std::clamp((ndc.x + 1) * .5f, 0.f, 1.f) * width,
                std::clamp((1 - ndc.y) * .5f, 0.f, 1.f) * height
            };
        }
        if (!valid) continue;
        const auto& color = vertices[i];
        for (std::size_t j = 1; j + 1 < count; ++j) {
            const auto ab = pixels[j] - pixels[0], ac = pixels[j + 1] - pixels[0];
            if (std::abs(ab.x * ac.y - ab.y * ac.x) < .0001f) continue;
            output.push_back({
                {pixels[0],                   pixels[j],                   pixels[j + 1]                  },
                {color.r,                     color.g,                     color.b                        },
                -(a.z + b.z + c.z) / 3.f,
                {polygon[0].uv,               polygon[j].uv,               polygon[j + 1].uv              },
                {1.f / polygon[0].position.w, 1.f / polygon[j].position.w, 1.f / polygon[j + 1].position.w},
                color.texture,
                {polygon[0].color, polygon[j].color, polygon[j + 1].color}
            });
        }
    }
    return output.size() - initialSize;
}
// Sort ALL objects together on every frame. Approximate painter order only;
// suitable for disjoint cubes, not general intersecting geometry or GPU depth.
inline void sortBackToFront(std::vector<ProjectedTriangle>& triangles) {
    std::stable_sort(triangles.begin(), triangles.end(), [](auto const& a, auto const& b) {
        return a.distance > b.distance;
    });
}
} // namespace animated_mojang::scene
