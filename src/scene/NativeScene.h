#pragma once

#include "scene/MeshVertex.h"
#include "scene/CaveParticles.h"
#include "scene/CaveCamera.h"
#include <filesystem>
#include <memory>
#include <vector>

class ScreenContext;
class ResourceLocation;
namespace mce {
class TextureGroup;
}

namespace animated_mojang::scene {
// CPU-project the cave into menu pixels. No world camera or GPU depth.
class NativeScene {
    struct State;
    struct TextureState;
    std::unique_ptr<State>        state;
    std::vector<Vertex>           model;
    CaveParticles                 particles;
    std::unique_ptr<TextureState> textureState;
    std::unique_ptr<TextureState> openingTextureState;
    bool refreshTexture(TextureState& asset, double timeMs);

public:
    NativeScene();
    ~NativeScene();
    NativeScene(NativeScene const&)            = delete;
    NativeScene& operator=(NativeScene const&) = delete;

    bool textureAvailable(double timeMs);
    bool draw(ScreenContext& context, float width, float height, double timeMs, CaveCameraFrame const& camera,
        bool openingImageOnly = false);
    void load(std::filesystem::path const& file);
    void reset();
    void setTextureSource(std::shared_ptr<mce::TextureGroup> const& group, ResourceLocation const* location,
        ResourceLocation const* openingLocation = nullptr);
};
} // namespace animated_mojang::scene
