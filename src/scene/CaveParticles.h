#pragma once
#include "scene/CpuProjection.h"
#include "scene/CaveAnimation.h"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace animated_mojang::scene {
inline constexpr std::uint32_t caveParticleFlag = 0x20000;
struct CaveEmitter {
    std::uint32_t x, y, z, kind, facing, seed;
};
struct CaveParticle {
    glm::vec3 position{}, motion{};
    std::size_t source = 0;
    int age = 0, lifetime = 1;
    float size = 1, gray = 1;
    bool flame = false, soul = false;
    float progress() const { return std::clamp(static_cast<float>(age) / lifetime, 0.f, 1.f); }
    unsigned smokeFrame() const { return 7u - static_cast<unsigned>(progress() * 7.f); }
};
class CaveParticles {
    std::array<std::uint32_t, 3> bounds{};
    std::vector<std::uint16_t> grid;
    std::vector<CaveEmitter> emitters;
    std::vector<CaveParticle> live;
    std::mt19937 random{0x414d4350u};
    std::int64_t lastTick = -1;
    float unit() { return std::generate_canonical<float, 24>(random); }
    void spawn(std::size_t source) {
        if (live.size() + 2 > limit) return;
        auto const& e = emitters[source];
        auto center = emitterPosition(e);
        if ((e.kind & 255) == 3) {
            center.y = e.y + unit() * .375f;
            const float offset = unit() * .6f - .3f;
            if (e.facing >= 3) center.z += offset;
            else center.x += offset;
        }
        for (bool flame : {false, true}) {
            CaveParticle p;
            p.position = center; p.source = source;
            p.flame = flame; p.soul = (e.kind & 255) == 2;
            p.size = (unit() * .5f + .5f) * 2.f * (flame ? 1.f : .75f);
            p.gray = flame ? 1.f : unit() * .3f;
            p.lifetime = static_cast<int>(8.f / (unit() * .8f + .2f)) + (flame ? 4 : 0);
            glm::vec3 direction{unit() * .8f - .4f, unit() * .8f - .4f, unit() * .8f - .4f};
            const auto length = glm::length(direction);
            if (length < .000001f) direction = {0, 1, 0}; else direction /= length;
            p.motion = direction * ((unit() + unit() + 1.f) * .15f * (flame ? .00396f : .04f));
            p.motion.y += flame ? .00099f : .01f;
            live.push_back(p);
        }
    }
public:
    static constexpr std::size_t limit = 256;
    void reset() { live.clear(); lastTick = -1; random.seed(0x414d4350u); }
    void load(std::filesystem::path const& path) {
        std::ifstream in(path, std::ios::binary);
        char magic[8]{}; std::array<std::uint32_t, 3> dimensions{}; std::uint32_t count = 0;
        in.read(magic, 8);in.read(reinterpret_cast<char*>(dimensions.data()), 12);in.read(reinterpret_cast<char*>(&count), 4);
        if (!in || std::memcmp(magic, "AMCP0001", 8) || count > 4096)
            throw std::runtime_error("Invalid cave particle header");
        std::size_t volume = 1;
        for (auto d : dimensions) { if (!d || d > 256) throw std::runtime_error("Invalid particle bounds"); volume *= d; }
        if (volume > 2000000) throw std::runtime_error("Particle volume too large");
        std::vector<std::uint16_t> voxels(volume);
        std::vector<CaveEmitter> sources(count);
        static_assert(sizeof(CaveEmitter) == 24);
        in.read(reinterpret_cast<char*>(voxels.data()), static_cast<std::streamsize>(volume * 2));
        in.read(reinterpret_cast<char*>(sources.data()), static_cast<std::streamsize>(count * sizeof(CaveEmitter)));
        if (!in || in.peek() != std::ifstream::traits_type::eof()) throw std::runtime_error("Invalid particle data size");
        for (auto v : voxels) if ((v & 255) > 5 || (v & ~0x0f07u)) throw std::runtime_error("Invalid particle occluder");
        for (auto const& e : sources)
            if (e.x >= dimensions[0] || e.y >= dimensions[1] || e.z >= dimensions[2]
                || !(e.kind & 255) || (e.kind & 255) > 3 || (e.kind & ~0x103u) || e.facing > 4
                || ((e.kind & 255) == 3 && !e.facing)) throw std::runtime_error("Invalid particle emitter");
        bounds = dimensions; grid = std::move(voxels); emitters = std::move(sources); reset();
    }
    std::span<CaveEmitter const> sources() const { return emitters; }
    std::span<CaveParticle const> particles() const { return live; }
    static glm::vec3 emitterPosition(CaveEmitter const& e) {
        const glm::vec3 direction = e.facing == 1 ? glm::vec3{0,0,-1} : e.facing == 2 ? glm::vec3{0,0,1}
            : e.facing == 3 ? glm::vec3{-1,0,0} : e.facing == 4 ? glm::vec3{1,0,0} : glm::vec3{0};
        glm::vec3 p{e.x + .5f, e.y + .7f, e.z + .5f};
        if ((e.kind & 255) == 3) p += direction * .52f;
        else if (e.facing) { p += direction * (-.48f + .625f * .5f); p.y = e.y + .2f + .625f * .8660254f + .02f; }
        return p;
    }
    static bool contains(std::uint16_t shape, glm::vec3 p) {
        switch (shape & 255) {
        case 1: return true;
        case 2: return p.y <= .5f;
        case 3: return p.y >= .5f;
        case 4: {
            if (p.x >= .375f && p.x <= .625f && p.z >= .375f && p.z <= .625f) return true;
            const bool rail = (p.y >= .375f && p.y <= .5625f) || (p.y >= .75f && p.y <= .9375f);
            return rail && ((p.x >= .4375f && p.x <= .5625f && ((p.z <= .5f && (shape & 256)) || (p.z >= .5f && (shape & 512))))
                || (p.z >= .4375f && p.z <= .5625f && ((p.x <= .5f && (shape & 1024)) || (p.x >= .5f && (shape & 2048)))));
        }
        case 5: return std::abs(p.x - p.z) < .055f || std::abs(p.x + p.z - 1.f) < .055f;
        default: return false;
        }
    }
    bool occluded(glm::vec3 eye, glm::vec3 target, std::size_t source) const {
        if (source >= emitters.size()) return true;
        const auto delta = target - eye;
        const auto steps = std::clamp(static_cast<int>(glm::length(delta) * 16.f), 1, 2048);
        auto const& e = emitters[source];
        for (int n = 1; n < steps; ++n) {
            const auto p = eye + delta * (static_cast<float>(n) / steps);
            const int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y)), z = static_cast<int>(std::floor(p.z));
            if (x < 0 || y < 0 || z < 0 || x >= static_cast<int>(bounds[0]) || y >= static_cast<int>(bounds[1]) || z >= static_cast<int>(bounds[2])) continue;
            if (x == static_cast<int>(e.x) && y == static_cast<int>(e.y) && z == static_cast<int>(e.z)) continue;
            if (contains(grid[x + z * bounds[0] + y * bounds[0] * bounds[2]], p - glm::vec3{x,y,z})) return true;
        }
        return false;
    }
    void update(double timeMs, glm::vec3 eye) {
        if (!std::isfinite(timeMs) || timeMs < 0) return;
        const auto tick = static_cast<std::int64_t>(timeMs / 50.0);
        if (lastTick < 0 || tick < lastTick) {
            reset(); lastTick = tick;
            for (std::size_t i = 0; i < emitters.size(); ++i)
                if (glm::length(emitterPosition(emitters[i]) - eye) <= 72.f) spawn(i);
            return;
        }
        const auto count = std::min<std::int64_t>(tick - lastTick, 4);
        for (std::int64_t t = tick - count + 1; t <= tick; ++t) {
            for (auto& p : live) {
                ++p.age;
                if (!p.flame) p.motion.y += .001f;
                p.position += p.motion; p.motion *= .95f;
            }
            std::erase_if(live, [](auto const& p) { return p.age >= p.lifetime; });
            for (std::size_t i = 0; i < emitters.size(); ++i) {
                auto const& e = emitters[i];
                if ((static_cast<std::uint64_t>(t) + e.seed) % ((e.kind & 255) == 3 ? 8 : 5) == 0
                    && glm::length(emitterPosition(e) - eye) <= 72.f) spawn(i);
            }
        }
        lastTick = tick;
    }
    std::size_t append(glm::mat4 const& view, glm::mat4 const& projection, float width, float height,
        std::vector<ProjectedTriangle>& output) const {
        const auto inverse = glm::inverse(view);
        const glm::vec3 eye{inverse[3]}, right{inverse[0]}, up{inverse[1]};
        const auto initial = output.size();
        for (auto const& p : live) {
            const auto camera = view * glm::vec4{p.position,1};
            if (-camera.z < .05f || -camera.z > 92.f) continue;
            const auto& e = emitters[p.source]; const bool partial = e.kind & 256;
            if (!partial && occluded(eye, p.position, p.source)) continue;
            const unsigned u = p.flame ? 0 : p.smokeFrame() * 8, v = p.flame ? (p.soul ? 32 : 24) : 0;
            const float radius = .1f * p.size * (1.f - p.progress() * p.progress() * .5f);
            const int divisions = partial ? 2 : 1;
            for (int y = 0; y < divisions; ++y) for (int x = 0; x < divisions; ++x) {
                const float x0 = -1.f + 2.f*x/divisions, x1 = -1.f + 2.f*(x+1)/divisions;
                const float y0 = -1.f + 2.f*y/divisions, y1 = -1.f + 2.f*(y+1)/divisions;
                const auto midpoint = p.position + radius*(right*((x0+x1)*.5f) + up*((y0+y1)*.5f));
                if (partial && occluded(eye, midpoint, p.source)) continue;
                const std::array<glm::vec2,4> corners{{{x0,y0},{x1,y0},{x1,y1},{x0,y1}}};
                std::array<Vertex,6> quad{}; std::size_t j = 0;
                for (auto index : {0,1,2,0,2,3}) {
                    const auto c = corners[index];
                    const auto world = p.position + radius*(right*c.x + up*c.y);
                    quad[j++] = {{world.x,world.y,world.z}, p.gray,p.gray,p.gray,
                        (32.f+u+.5f+(c.x+1.f)*.5f*7.f)/256.f,
                        (256.f+v+.5f+(1.f-c.y)*.5f*7.f)/caveAtlasHeight,caveParticleFlag};
                }
                const auto begin = output.size();
                projectTriangles(quad,view,projection,width,height,output);
                for (auto i = begin; i < output.size(); ++i)
                    output[i].opacity = std::min(1.f,(1.f-p.progress())*4.f);
            }
        }
        return output.size() - initial;
    }
};
} // namespace animated_mojang::scene
