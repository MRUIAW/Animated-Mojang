#include "scene/NativeScene.h"
#include "scene/AssetRefresh.h"
#include "scene/CaveCamera.h"
#include "scene/CaveOpeningBackdrop.h"
#include "scene/CaveAnimation.h"
#include "scene/CaveOcclusion.h"
#include "scene/CaveModel.h"
#include "scene/CpuProjection.h"
#include "scene/UiCaveBatch.h"

#include "mc/client/gui/ScreenRenderer.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/renderer/Tessellator.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/common/client/renderer/helpers/MeshHelpers.h"
#include "mc/deps/core/math/Vec2.h"
#include "mc/deps/core/renderer/RenderMaterialInfo.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/MaterialPtr.h"
#include "mc/deps/minecraft_renderer/renderer/TextureLoadState.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mc/deps/minecraft_renderer/resources/ClientTexture.h"
#include "mc/deps/minecraft_renderer/resources/OffscreenCaptureDescription.h"
#include "mc/deps/minecraft_renderer/resources/ServerTexture.h"
#include "mc/deps/renderer/Camera.h"
#include "mc/deps/renderer/ShaderColor.h"
#include "mod/MyMod.h"
#include <array>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace animated_mojang::scene {
namespace {
auto& logger() { return my_mod::MyMod::getInstance().getSelf().getLogger(); }
class WorldScope {
    ScreenContext&              context;
    mce::Color                  color;
    std::optional<glm::vec4>    clip;
    MatrixStack::MatrixStackRef world;

public:
    explicit WorldScope(ScreenContext& ctx)
    : context(ctx),
      color(ctx.currentShaderColor.color.get()),
      clip(ctx.normalizedClipRegion),
      world(ctx.camera.worldMatrixStack->push(false)) {
        context.camera.worldMatrixStack->getTop()._m = glm::mat4{1};
        context.currentShaderColor.color             = mce::Color{255, 255, 255};
        context.currentShaderColor.dirty             = true;
        context.normalizedClipRegion->reset();
    }
    ~WorldScope() {
        context.currentShaderColor.color = color;
        context.currentShaderColor.dirty = true;
        context.normalizedClipRegion     = clip;
    }
};
bool textureReady(mce::TexturePtr const* texture) {
    if (!texture) return false;
    auto const* data = texture->mClientTexture.get();
    return data && data->mIsMissingTexture == IsMissingTexture::No
        && (static_cast<unsigned>(data->mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit));
}
} // namespace
struct NativeScene::State {
    struct Batch {
        std::vector<glm::vec3>     positions;
        std::vector<std::uint32_t> colors;
        std::vector<glm::vec2>     uv;
        std::uint32_t             texture = 0;
    };
    std::vector<Batch>                        batches;
    std::unique_ptr<Tessellator>              tessellator;
    std::array<bool, 15>                      fields{};
    ColorPacking                              packing{};
    bool                                      formatReady = false, formatFailed = false;
    std::weak_ptr<mce::BufferResourceService> service;
    std::vector<ProjectedTriangle>            triangles;
    float                                     width = 0, height = 0;
    std::int64_t                              cameraBucket = -1;
    std::uint64_t                             cameraRevision = 0;
    bool                                      movingCamera = false;
    bool                                      drawingCave = false;
};
struct NativeScene::TextureState {
    std::weak_ptr<mce::TextureGroup>  group;
    std::unique_ptr<ResourceLocation> location;
    std::unique_ptr<mce::TexturePtr>  texture;
    AssetRefresh                      refresh;
};
NativeScene::NativeScene() : state(std::make_unique<State>()), textureState(std::make_unique<TextureState>()),
    openingTextureState(std::make_unique<TextureState>()) {
    state->triangles.reserve(8192);
}
NativeScene::~NativeScene() = default;
void NativeScene::reset() {
    particles.reset();
    state = std::make_unique<State>();
    state->triangles.reserve(8192);
}
void NativeScene::setTextureSource(std::shared_ptr<mce::TextureGroup> const& group, ResourceLocation const* location,
    ResourceLocation const* openingLocation) {
    textureState        = std::make_unique<TextureState>();
    textureState->group = group;
    if (location) textureState->location = std::make_unique<ResourceLocation>(*location);
    openingTextureState = std::make_unique<TextureState>();
    openingTextureState->group = group;
    if (openingLocation) openingTextureState->location = std::make_unique<ResourceLocation>(*openingLocation);
    // Obtain color handles during the loading bar, not on the first menu frame.
    // Loading the description alone can leave a placeholder until that lookup.
    if (group) for (auto* source : {openingTextureState.get(), textureState.get()}) {
        if (!source->location) continue;
        source->texture = std::make_unique<mce::TexturePtr>(
            group->getTexture(*source->location, false, std::nullopt, cg::TextureSetLayerType::Color));
    }
}
void NativeScene::load(std::filesystem::path const& file) {
    try {
        model = loadCaveModel(file);
    } catch (std::exception const& error) {
        model.clear();
        logger().error("Cave model unavailable: {}; retaining plain background.", error.what());
    }
    reset();
    try {
        auto data = file; data.replace_filename("normal_cave.particles");
        particles.load(data);
    } catch (std::exception const& error) {
        particles = CaveParticles{};
        logger().warn("Cave particles unavailable: {}; geometry remains enabled.", error.what());
    }
}

bool NativeScene::textureAvailable(double timeMs) {
    if (!std::isfinite(timeMs)) return false;
    const bool caveReady = !model.empty() && refreshTexture(*textureState, timeMs);
    const bool openingReady = !openingTextureState->location
        || refreshTexture(*openingTextureState, timeMs);
    return caveReady && openingReady;
}
bool NativeScene::openingTextureReady(double timeMs) {
    return std::isfinite(timeMs) && openingTextureState->location && refreshTexture(*openingTextureState, timeMs);
}
bool NativeScene::refreshTexture(TextureState& asset, double timeMs) {
    auto  group = asset.group.lock();
    bool  ready = false;
    if (group && asset.location) {
        ready = asset.refresh.update(
            asset.texture,
            timeMs,
            [&] {
                return std::make_unique<mce::TexturePtr>(
                    group->getTexture(*asset.location, false, std::nullopt, cg::TextureSetLayerType::Color)
                );
            },
            [](auto const& handle) { return textureReady(handle.get()); }
        );
    }
    return ready;
}
bool NativeScene::draw(ScreenContext& ctx, float width, float height, double timeMs, CaveCameraFrame const& camera,
    bool openingImageOnly) {
    if (!std::isfinite(width) || !std::isfinite(height) || !std::isfinite(timeMs) || width <= 0 || height <= 0)
        return false;
    // Present the screenshot as soon as it is available. Cave/title readiness
    // still gates the flight, but must not gate the static loading picture.
    const bool openingReady = openingTextureState->location
        && refreshTexture(*openingTextureState,timeMs);
    const bool drawingCave = !openingImageOnly && textureAvailable(timeMs);
    if (!drawingCave && !(openingReady && camera.loadingBackdropAlpha > 0)) return false;
    auto& asset = *textureState;
    auto service = ctx.tessellator.mBufferResourceService.lock();
    if (!service) return false;
    if (state->service.lock() != service) {
        reset();
        state->service = service;
    }
    if (state->formatFailed) return false;
    auto const& material =
        ScreenRenderer::mScreenMaterials()[static_cast<int>(ScreenRenderer::ScreenMaterial::ColorBlit)];
    if (!material.mRenderMaterialInfoPtr || !material.mRenderMaterialInfoPtr->mPtr) return false;
    if (!state->formatReady) {
        // Ask the native tessellator for its actual Position/Color/UV0 format
        // once. All cave vertices after this are bulk writes to typed vectors.
        Tessellator seed{service};
        seed.resetTransform(true);
        seed.begin({}, mce::PrimitiveMode::QuadList, 4, false);
        constexpr std::array<glm::vec3, 4> samples{
            {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, 0}}
        };
        constexpr std::array<glm::vec2, 4> corners{
            {{0, 0}, {1, 0}, {1, 1}, {0, 1}}
        };
        for (std::size_t i = 0; i < 4; ++i) {
            seed.color(samples[i].r, samples[i].g, samples[i].b, 1.f);
            seed.tex2(Vec2{corners[i].x, corners[i].y});
            seed.vertex(corners[i].x, corners[i].y, 0.f);
        }
        state->fields = seed.mMeshData->mFieldEnabled.get();
        try {
            state->packing = ColorPacking::fromSamples(seed.mMeshData->mColors.get());
        } catch (std::exception const& error) {
            state->formatFailed = true;
            logger().error("Native cave vertex format unavailable: {}; keeping plain background.", error.what());
            return false;
        }
        state->formatReady = true;
    }
    if (!state->tessellator) state->tessellator = std::make_unique<Tessellator>(service);
    // 30 Hz while flying/transitioning, 20 Hz idle. Submit through
    // the temporary-mesh helper used by the visible 0.1.10/0.1.11 scenes.
    const double interval = camera.moving ? 1000.0 / 30.0 : 50.0;
    const auto bucket = drawingCave || camera.moving ? static_cast<std::int64_t>(timeMs / interval) : 0;
    if (bucket != state->cameraBucket || camera.revision != state->cameraRevision
        || camera.moving != state->movingCamera || drawingCave != state->drawingCave
        || width != state->width || height != state->height) {
        state->triangles.clear();
        const auto view = caveView(camera.pose, camera.motionTimeMs);
        if (drawingCave) projectTriangles(
            model,
            view,
            caveProjection(width, height),
            width,
            height,
            state->triangles
        );
        if (camera.loadingBackdropAlpha > 0 && textureReady(openingTextureState->texture.get())) {
            appendCaveOpeningBackdrop(width, height, camera.loadingBackdropAlpha, view, state->triangles);
        }
        if (drawingCave) {
            particles.update(timeMs, glm::vec3{glm::inverse(view)[3]});
            particles.append(view, caveProjection(width, height), width, height, state->triangles);
        }
        resolveCaveOcclusion(state->triangles);
        sortBackToFront(state->triangles);
        for (auto& triangle : state->triangles) {
            animateLava(triangle, timeMs);
        }
        state->cameraBucket = bucket;
        state->cameraRevision = camera.revision;
        state->movingCamera = camera.moving;
        state->drawingCave = drawingCave;
        state->width        = width;
        state->height       = height;
        // Reuse the stream capacities across camera updates instead of allocating
        // and freeing hundreds of thousands of vertices every 33/50ms.
        std::size_t batchIndex = 0;
        for (std::size_t begin = 0; begin < state->triangles.size();) {
            const auto    end = caveBatchEnd(state->triangles, begin);
            if (batchIndex == state->batches.size()) state->batches.emplace_back();
            auto& batch = state->batches[batchIndex++];
            batch.texture = state->triangles[begin].texture & 0xffffu;
            auto&      positions = batch.positions;
            auto&      colors    = batch.colors;
            auto&      uv        = batch.uv;
            const auto count     = (end - begin) * 24;
            positions.clear();
            colors.clear();
            uv.clear();
            positions.reserve(count);
            colors.reserve(count);
            uv.reserve(count);
            for (std::size_t i = begin; i < end; ++i)
                appendUiTriangle(state->triangles[i], state->packing, positions, colors, uv);
            begin = end;
        }
        state->batches.resize(batchIndex);
    }
    // Match the screenshot's dark surround at the opening, then the cave clear.
    WorldScope scope{ctx};
    const float loading = camera.loadingBackdropAlpha;
    ScreenRenderer::singleton().fill(ctx, 0, 0, width, height,
        mce::Color{static_cast<int>(7 + 23 * loading), static_cast<int>(8 + 22 * loading), static_cast<int>(13 + 18 * loading)});
    using Texture          = std::variant<std::monostate, mce::TexturePtr, mce::ClientTexture, mce::ServerTexture>;
    Texture nativeTexture;
    if (drawingCave) nativeTexture = *asset.texture;
    Texture openingTexture;
    if (textureReady(openingTextureState->texture.get())) openingTexture = *openingTextureState->texture;
    auto& tessellator = *state->tessellator;
    for (auto const& batch : state->batches) {
        tessellator.resetTransform(true);
        tessellator.begin({}, mce::PrimitiveMode::QuadList, static_cast<int>(batch.positions.size()), false);
        // Initialize native pending attributes/format with one vertex; replace
        // its streams in bulk. No color/tex2/vertex calls inside a vertex loop.
        tessellator.color(1.f, 1.f, 1.f, 1.f);
        tessellator.tex2(Vec2{0.f, 0.f});
        tessellator.vertex(0.f, 0.f, 0.f);
        auto& data = tessellator.mMeshData.get();
        data.mPositions     = batch.positions;
        data.mColors        = batch.colors;
        data.mTextureUVs[0] = batch.uv;
        data.mFieldEnabled  = state->fields;
        data.mAABB          = std::pair{glm::vec3{0}, glm::vec3{width, height, 0}};
        data.mUVAABB        = std::pair{glm::vec2{0}, glm::vec2{1}};
        tessellator.mCount         = static_cast<uint>(batch.positions.size());
        tessellator.mCurQuadVertex = 0;
        MeshHelpers::renderMeshImmediately(
            ctx,
            tessellator,
            material,
            batch.texture == caveOpeningTexture ? openingTexture : nativeTexture,
            OffscreenCaptureDescription{}
        );
    }
    // Projected geometry submitted; this does not assert captured GPU pixels.
    return true;
}
} // namespace animated_mojang::scene
