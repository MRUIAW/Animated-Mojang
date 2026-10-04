#include "scene/CaveCamera.h"
#include "scene/CaveModel.h"
#include "scene/CpuProjection.h"
#include "scene/UiCaveBatch.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace animated_mojang::scene;
void check(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
int main(int argc, char** argv) {
    try {
        const auto model = loadCaveModel("resources/data/normal_cave.mesh");
        check(model.size() == 98094, "full state-aware cave model loaded");
        std::vector<ProjectedTriangle> triangles;
        triangles.reserve(8192);
        double                             sumMs = 0, bulkSumMs = 0;
        std::size_t                        maxBatches = 0, maxVertices = 0;
        const std::array<std::uint32_t, 4> abgr{0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xff000000u};
        const auto                         packing = ColorPacking::fromSamples(abgr);
        check(packing.pack({1, 0, 0}) == abgr[0], "ABGR color packing");
        const std::array<std::uint32_t, 4> argb{0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xff000000u};
        check(ColorPacking::fromSamples(argb).pack({0, 0, 1}) == argb[2], "ARGB color packing");
        bool badPackingRejected = false;
        try {
            ColorPacking::fromSamples(std::array<std::uint32_t, 4>{0, 0, 0, 0});
        } catch (std::exception const&) {
            badPackingRejected = true;
        }
        check(badPackingRejected, "unsupported native packing rejected");
        std::size_t frames = 0, minCount = 100000, maxCount = 0;
        for (auto size : {
                 glm::vec2{640,  346 },
                 glm::vec2{1920, 1080},
                 glm::vec2{1080, 1920},
                 glm::vec2{3840, 2160}
        }) {
            for (int step = 0; step < 16; ++step) {
                triangles.clear();
                const auto start = std::chrono::steady_clock::now();
                projectTriangles(
                    model,
                    caveView(step * 4000.),
                    caveProjection(size.x, size.y),
                    size.x,
                    size.y,
                    triangles
                );
                sortBackToFront(triangles);
                sumMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
                ++frames;
                minCount = std::min(minCount, triangles.size());
                maxCount = std::max(maxCount, triangles.size());
                check(triangles.size() > 100, "interior cave camera produces substantial visible geometry");
                const auto  bulkStart = std::chrono::steady_clock::now();
                std::size_t batches = 0, vertexCount = 0;
                for (std::size_t begin = 0; begin < triangles.size();) {
                    const auto                 end = caveBatchEnd(triangles, begin);
                    std::vector<glm::vec3>     positions;
                    std::vector<std::uint32_t> colors;
                    std::vector<glm::vec2>     uv;
                    const auto                 count = (end - begin) * 24;
                    check(count < 65536 && count % 4 == 0, "batch respects native QuadList capacity");
                    positions.reserve(count);
                    colors.reserve(count);
                    uv.reserve(count);
                    for (std::size_t i = begin; i < end; ++i) {
                        check((triangles[i].texture & 0xffffu) == (triangles[begin].texture & 0xffffu), "texture runs preserve painter order");
                        animateLava(triangles[i], step * 4000.);
                        appendUiTriangle(triangles[i], packing, positions, colors, uv);
                    }
                    check(
                        positions.size() == count && colors.size() == count && uv.size() == count,
                        "complete position/color/UV streams"
                    );
                    vertexCount += count;
                    ++batches;
                    begin = end;
                }
                bulkSumMs +=
                    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bulkStart).count();
                check(vertexCount == triangles.size() * 24, "each clipped triangle reaches native batch data once");
                maxBatches  = std::max(maxBatches, batches);
                maxVertices = std::max(maxVertices, vertexCount);
                for (std::size_t i = 0; i < triangles.size(); ++i) {
                    if (i) check(triangles[i - 1].distance >= triangles[i].distance, "cave painter order");
                    for (auto p : triangles[i].points)
                        check(
                            std::isfinite(p.x) && std::isfinite(p.y) && p.x >= 0 && p.y >= 0 && p.x <= size.x
                                && p.y <= size.y,
                            "cave clips inside viewport"
                        );
                    for (auto uv : triangles[i].uv)
                        check(
                            std::isfinite(uv.x) && std::isfinite(uv.y) && uv.x >= 0 && uv.x <= 1 && uv.y >= 0
                                && uv.y <= 1,
                            "clipping preserves valid texture coordinates"
                        );
                    for (auto color : triangles[i].colors)
                        check(std::isfinite(color.r) && color.r>=0 && color.r<=1 && color.g>=0 && color.g<=1
                            && color.b>=0 && color.b<=1, "RGB light remains bounded after clipping");
                }
                if (argc == 2 && step == 0 && size.x == 640) {
                    std::ofstream svg(argv[1]);
                    check(static_cast<bool>(svg), "cave preview file");
                    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1280\" height=\"692\" viewBox=\"0 0 640 "
                           "346\"><rect width=\"640\" height=\"346\" fill=\"#07080d\"/>";
                    for (auto const& triangle : triangles) {
                        const float fog = std::clamp((triangle.distance - 8.f) / 55.f, 0.f, .8f);
                        const auto  color =
                            triangle.color * (1 - fog) + glm::vec3{7.f / 255, 8.f / 255, 13.f / 255} * fog;
                        svg << "<polygon points=\"";
                        for (auto p : triangle.points) svg << p.x << ',' << p.y << ' ';
                        svg << "\" fill=\"rgb(" << int(color.r * 255) << ',' << int(color.g * 255) << ','
                            << int(color.b * 255) << ")\"/>";
                    }
                    svg << "</svg>";
                }
            }
        }
        // A bad header must fail cleanly rather than allocating arbitrary counts.
        const std::filesystem::path bad = "build/cave-invalid.mesh";
        {
            std::ofstream out(bad, std::ios::binary);
            out << "AMCV0002";
        }
        bool rejected = false;
        try {
            loadCaveModel(bad);
        } catch (std::exception const&) {
            rejected = true;
        }
        check(rejected, "truncated model rejected");
        std::cout << "Cave checks passed: model load, 64 camera/window cases, clipped output, order, invalid file; "
                     "visible triangles="
                  << minCount << ".." << maxCount << ", mean CPU projection/sort=" << sumMs / frames
                  << " ms; mean bulk vertex fill=" << bulkSumMs / frames << " ms, max batches=" << maxBatches
                  << ", max vertices=" << maxVertices
                  << " (local CPU measurement; excludes native pack/upload/draw and game FPS).\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
