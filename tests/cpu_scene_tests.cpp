#include "CubeFixture.h"
#include "scene/AssetRefresh.h"
#include "scene/CpuProjection.h"
#include "scene/CaveAnimation.h"
#include "scene/UiCaveBatch.h"
#include <fstream>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>

using namespace animated_mojang::scene;
void check(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
std::array<Vertex, 3> face(float z) {
    return {
        Vertex{{-1, -1, z}, 1, 0, 0},
        {{1, -1, z},  1, 0, 0},
        {{0, 1, z},   1, 0, 0}
    };
}
void inside(std::vector<ProjectedTriangle> const& triangles, float width, float height) {
    for (auto const& triangle : triangles) {
        for (auto p : triangle.points)
            check(
                std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height,
                "clipped output must stay inside logical screen pixels"
            );
        check(triangle.distance > 0 && std::isfinite(triangle.distance), "finite positive painter distance");
    }
}
int main(int argc, char** argv) {
    try {
        // Reproduce a native async texture cache replacing its immutable
        // placeholder; retaining the original shared handle never becomes ready.
        struct Asset {
            bool loaded;
        };
        auto         provider            = std::make_shared<Asset const>(Asset{false});
        auto         handle              = provider;
        auto         originalPlaceholder = handle;
        AssetRefresh refresh;
        unsigned     lookups = 0;
        const auto   fetch   = [&] {
            ++lookups;
            return provider;
        };
        const auto ready = [](auto const& value) { return value && value->loaded; };
        check(!refresh.update(handle, 0, fetch, ready), "initial placeholder remains unresolved");
        provider = std::make_shared<Asset const>(Asset{true});
        check(!originalPlaceholder->loaded, "async completion does not mutate old descriptor");
        check(!refresh.update(handle, 99, fetch, ready) && lookups == 1, "unresolved lookup is throttled");
        check(
            refresh.update(handle, 100, fetch, ready) && handle == provider && lookups == 2,
            "ready replacement is reacquired after async completion"
        );
        check(refresh.update(handle, 101, fetch, ready) && lookups == 2, "loaded texture avoids repeated lookups");
        handle.reset();
        check(refresh.update(handle, 200, fetch, ready) && lookups == 3, "invalidated handle is reacquired");
        AssetRefresh replacementGroup;
        handle.reset();
        check(replacementGroup.update(handle, 201, fetch, ready), "new texture source starts a fresh retry schedule");
        const auto                     projection = glm::perspectiveRH_ZO(glm::radians(90.f), 1.f, .1f, 100.f);
        const glm::mat4                identity{1};
        std::vector<ProjectedTriangle> triangles;
        check(projectTriangles(face(-2), identity, projection, 100, 100, triangles) == 1, "front face visible");
        const auto near     = triangles[0];
        float      quadArea = 0;
        for (auto const& points : triangleQuads(near)) {
            float twiceArea = 0;
            for (std::size_t i = 0; i < 4; ++i) {
                const auto p = points[i], next = points[(i + 1) % 4];
                twiceArea    += p.x * next.y - p.y * next.x;
                const auto u = next - p, v = points[(i + 2) % 4] - next;
                check(u.x * v.y - u.y * v.x < 0, "UI tessellation quads are convex and non-degenerate");
            }
            quadArea += std::abs(twiceArea) * .5f;
        }
        check(std::abs(quadArea - 1250.f) < .01f, "three quads cover the projected triangle without excess area");
        check(glm::length(near.points[0] - glm::vec2{25, 75}) < .001f, "analytic perspective bottom-left");
        check(glm::length(near.points[1] - glm::vec2{75, 75}) < .001f, "analytic perspective bottom-right");
        check(glm::length(near.points[2] - glm::vec2{50, 25}) < .001f, "Y maps downward on menu screen");
        triangles.clear();
        check(projectTriangles(face(-4), identity, projection, 100, 100, triangles) == 1, "distant face visible");
        check(
            std::abs(glm::length(triangles[0].points[1] - triangles[0].points[0]) - 25) < .001f,
            "doubling distance halves screen width"
        );
        auto reversed = face(-2);
        std::swap(reversed[0], reversed[2]);
        triangles.clear();
        check(projectTriangles(reversed, identity, projection, 100, 100, triangles) == 0, "backface culled");
        check(projectTriangles(face(.5f), identity, projection, 100, 100, triangles) == 0, "behind-eye face rejected");
        check(projectTriangles(face(-200), identity, projection, 100, 100, triangles) == 0, "past far plane rejected");
        auto crossing        = face(-1);
        crossing[0].position = {-.02f, -.02f, -.05f};
        crossing[1].position = {.3f, -.3f, -1};
        crossing[2].position = {0, .3f, -1};
        check(
            projectTriangles(crossing, identity, projection, 100, 100, triangles) == 2,
            "near-plane crossing clips to two triangles instead of disappearing or exploding"
        );
        inside(triangles, 100, 100);
        triangles.clear();
        auto textured = face(-2);
        textured[0].u = 0;
        textured[0].v = 1;
        textured[1].u = 1;
        textured[1].v = 1;
        textured[2].u = .5f;
        textured[2].v = 0;
        projectTriangles(textured, identity, projection, 100, 100, triangles);
        check(glm::length(triangles[0].uv[1] - glm::vec2{1, 1}) < .0001f, "unclipped UV preserved");
        textured[0].r=1; textured[0].g=0; textured[0].b=0;
        textured[1].r=0; textured[1].g=1; textured[1].b=0;
        textured[2].r=0; textured[2].g=0; textured[2].b=1;
        triangles.clear();
        projectTriangles(textured, identity, projection, 100, 100, triangles);
        check(glm::length(triangles[0].colors[1]-glm::vec3{0,1,0})<.0001f, "per-vertex RGB survives projection");
        check(glm::length(triangleQuadColors(triangles[0])[0][1]-glm::vec3{.5f,.5f,0})<.0001f,
            "subdivision preserves a color gradient");
        auto unequalDepth     = triangles[0];
        unequalDepth.inverseW = {1, .5f, .25f};
        check(
            std::abs(triangleQuadUVs(unequalDepth)[0][1].x - 1.f / 3.f) < .0001f,
            "subdivision UV uses perspective weights, not affine midpoint"
        );
        check(std::abs(triangleQuadColors(unequalDepth)[0][1].g-1.f/3.f)<.0001f,
            "subdivision RGB uses perspective weights");
        std::array<cpu_projection_detail::ClipVertex,12> coloredPolygon{};
        coloredPolygon[0]={{-2,0,.5f,1},{0,0},{1,0,0}};
        coloredPolygon[1]={{0,-.5f,.5f,1},{1,0},{0,1,0}};
        coloredPolygon[2]={{0,.5f,.5f,1},{0,1},{0,0,1}};
        const auto clippedCount=cpu_projection_detail::clipPolygon(coloredPolygon,3);
        bool mixedColor=false;
        for(std::size_t i=0;i<clippedCount;++i)
            if(std::abs(coloredPolygon[i].position.x+1)<.0001f)
                mixedColor = mixedColor || std::abs(coloredPolygon[i].color.r-.5f)<.0001f;
        check(mixedColor, "clip boundary interpolates RGB rather than taking the first color");
        check(lavaFrame(0)==0 && lavaFrame(299)==0 && lavaFrame(300)==1 && lavaFrame(9600)==0,
            "32 lava frames advance each 300ms and wrap");
        auto animated=triangles[0];
        animated.texture=animatedLavaFlag;
        const float initialV=animated.uv[0].y;
        animateLava(animated,9300);
        check(std::abs(animated.uv[0].y-initialV-31*16.f/caveAtlasHeight)<.0001f,"last lava frame offset");
        std::array<ProjectedTriangle,3> mixedAtlas{triangles[0],animated,triangles[0]};
        check(caveBatchEnd(mixedAtlas,0)==3,"lava animation does not split atlas batches");
        textured[0].position.x = -4;
        triangles.clear();
        check(
            projectTriangles(textured, identity, projection, 100, 100, triangles) > 0,
            "textured triangle crosses viewport edge"
        );
        for (auto const& triangle : triangles)
            for (auto uv : triangle.uv)
                check(uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1, "interpolated clipped UV stays in tile");
        triangles.clear();
        auto wide          = face(-1);
        wide[0].position.x = -4;
        wide[1].position.x = 4;
        wide[2].position.y = 4;
        check(
            projectTriangles(wide, identity, projection, 100, 100, triangles) > 0,
            "side-plane clipping preserves visible region"
        );
        inside(triangles, 100, 100);
        triangles.clear();
        auto invalid  = identity;
        invalid[0][0] = std::numeric_limits<float>::quiet_NaN();
        check(projectTriangles(face(-2), invalid, projection, 100, 100, triangles) == 0, "NaN model rejected");
        check(projectTriangles(face(-2), identity, projection, 0, 100, triangles) == 0, "zero viewport rejected");
        check(projectTriangles(face(-2), identity, projection, 100, -1, triangles) == 0, "negative viewport rejected");

        // Exercise the actual preview's full movement envelope and window shapes.
        for (auto size : {
                 glm::vec2{640,  346 },
                 glm::vec2{1920, 1080},
                 glm::vec2{1080, 1920},
                 glm::vec2{3840, 2160}
        }) {
            for (int step = 0; step <= 80; ++step) {
                const double time  = step * 1000.;
                const float  orbit = static_cast<float>(std::sin(time / 9000.)) * .35f;
                const float  angle = static_cast<float>(std::sin(time / 7000.)) * .4f;
                const auto   view  = glm::lookAtRH(
                    glm::vec3{orbit, 2.3f, std::max(8.5f, 4.8f / (size.x / size.y))},
                    glm::vec3{0, 0, -.6f},
                    glm::vec3{0, 1, 0}
                );
                const auto perspective = glm::perspectiveRH_ZO(glm::radians(55.f), size.x / size.y, .1f, 100.f);
                const auto nearModel   = glm::rotate(
                    glm::translate(identity, glm::vec3{-.65f, -.1f, .35f}),
                    .35f + angle,
                    glm::vec3{0, 1, 0}
                );
                const auto farModel = glm::rotate(
                    glm::translate(identity, glm::vec3{1.1f, .15f, -2.8f}),
                    -.35f - angle * .5f,
                    glm::vec3{0, 1, 0}
                );
                triangles.clear();
                const auto nearVertices = cube(.95f, .42f, .13f), farVertices = cube(.15f, .65f, .88f);
                check(
                    projectTriangles(nearVertices, view * nearModel, perspective, size.x, size.y, triangles) >= 4,
                    "near cube stays visible throughout motion"
                );
                check(
                    projectTriangles(farVertices, view * farModel, perspective, size.x, size.y, triangles) >= 4,
                    "far cube stays visible throughout motion"
                );
                inside(triangles, size.x, size.y);
                sortBackToFront(triangles);
                bool seenNear = false;
                for (std::size_t i = 0; i < triangles.size(); ++i) {
                    if (i) check(triangles[i - 1].distance >= triangles[i].distance, "global painter order");
                    const auto color  = triangles[i].color;
                    const bool isNear = color.r > color.b;
                    if (isNear) seenNear = true;
                    else check(!seenNear, "separated far cube cannot paint over near cube");
                }
                if (argc == 2 && step == 0 && size.x == 640) {
                    std::ofstream svg(argv[1]);
                    check(static_cast<bool>(svg), "preview file open");
                    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"692\" viewBox=\"0 0 640 "
                           "346\">"
                           "<rect width=\"640\" height=\"346\" fill=\"#192027\"/>";
                    for (auto const& triangle : triangles) {
                        svg << "<polygon points=\"";
                        for (auto p : triangle.points) svg << p.x << ',' << p.y << ' ';
                        svg << "\" fill=\"rgb(" << int(triangle.color.r * 255) << ',' << int(triangle.color.g * 255)
                            << ',' << int(triangle.color.b * 255) << ")\"/>";
                    }
                    svg << "<rect x=\"16\" y=\"304.48\" width=\"8\" height=\"8\" fill=\"#f26b21\"/>"
                           "<rect x=\"28\" y=\"304.48\" width=\"8\" height=\"8\" fill=\"#26a6e0\"/>"
                           "<rect x=\"40\" y=\"304.48\" width=\"8\" height=\"8\" fill=\"#33e640\"/></svg>";
                }
            }
        }
        std::cout << "CPU scene checks passed: async placeholder replacement/retry, analytic pixels, perspective "
                     "scale, culling, six-plane clipping, "
                     "invalid inputs, 324 moving scene/window cases, global painter order.\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
