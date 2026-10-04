#include "client/Client.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "animation/Timeline.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/SerialWorkList.h"
#include "mc/client/gui/ScreenRenderer.h"
#include "mc/client/gui/ScreenTechStackSelector.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/controls/renderers/PanoramaRenderer.h"
#include "mc/client/gui/controls/renderers/PaperDollRenderer.h"
#include "mc/client/gui/controls/renderers/ProgressBarRenderer.h"
#include "mc/client/gui/screens/AbstractScreenSetupCleanupStrategy.h"
#include "mc/client/gui/screens/CubemapBackgroundScreen.h"
#include "mc/client/gui/screens/ScreenContext.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/AbstractScene.h"
#include "mc/client/gui/screens/BaseScreen.h"
#include "mc/client/gui/screens/UIScene.h"
#include "mc/client/gui/screens/interfaces/ISceneStack.h"
#include "mc/client/gui/oreui/routing/Router.h"
#include "mc/client/gui/oreui/Scene.h"
#include "mc/client/gui/oreui/routing/RouterLocation.h"
#include "mc/client/gui/screens/controllers/StartMenuScreenController.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/game/GameRenderer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/math/Color.h"
#include "mc/deps/core/resource/ResourceFileSystem.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/string/HashedString.h"
#include "mc/deps/input/RectangleArea.h"
#include "mc/deps/minecraft_renderer/renderer/BedrockTextureData.h"
#include "mc/deps/minecraft_renderer/renderer/IsMissingTexture.h"
#include "mc/deps/minecraft_renderer/renderer/TextureLoadState.h"
#include "mc/deps/minecraft_renderer/renderer/TexturePtr.h"
#include "mod/MyMod.h"
#include "scene/NativeScene.h"
#include "scene/AssetRefresh.h"

namespace animated_mojang::client {
namespace {
using Clock = std::chrono::steady_clock;

struct Runtime {
    Config                                        config;
    Timeline                                      timeline;
    std::filesystem::path                         resources;
    Clock::time_point                             epoch = Clock::now();
    std::vector<ResourceLocation>                 locations;
    std::vector<ResourceLocation>                 caveLocations;
    std::vector<std::unique_ptr<mce::TexturePtr>> textures;
    std::array<scene::AssetRefresh, 9>             titleRefresh;
    std::vector<std::weak_ptr<mce::TextureGroup>> queuedGroups;
    std::weak_ptr<mce::TextureGroup>              textureGroup;
    std::recursive_mutex                          mutex;
    scene::NativeScene                            nativeScene;
    scene::CaveCameraController                    camera;
    scene::CaveCameraFrame                         cameraFrame;
    std::unordered_map<AbstractScene const*, uint> closingScenes;
    bool                                          assetsTimedOut = false, enteredMenu = false, pendingReplay = false;
    bool     warnedTitle = false;
    double   firstMenuRenderMs = -1;
    Frame    frame;
    std::unordered_set<std::string> loggedScreens;
};
Runtime                        runtime;
std::atomic_bool               active = false, skipRequested = false;
std::atomic_bool               skipAllowed = false;
std::atomic<ScreenView*>       blockedView    = nullptr;
std::atomic<ScreenController*> menuController = nullptr;
ll::event::ListenerPtr         keyListener;

struct RenderScope {
    bool      menu = false, bootProgress = false;
    Frame     frame;
    glm::vec2 size{};
    bool      backgroundDrawn = false;
};
thread_local RenderScope* currentScope = nullptr;
struct BackgroundFrame {
    bool inside        = false;
    bool native3DDrawn = false;
    bool presentationUpdated = false;
    bool pageResolved = false;
};
thread_local BackgroundFrame backgroundFrame;
class ScopedRender {
    RenderScope* previous;

public:
    explicit ScopedRender(RenderScope& value) : previous(currentScope) { currentScope = &value; }
    ~ScopedRender() { currentScope = previous; }
};
double nowMs() { return std::chrono::duration<double, std::milli>(Clock::now() - runtime.epoch).count(); }
auto&  logger() { return my_mod::MyMod::getInstance().getSelf().getLogger(); }
bool   matches(std::vector<std::string> const& names, std::string const& name) {
    return std::find(names.begin(), names.end(), name) != names.end();
}
std::string assetPath(size_t index) {
    auto path = runtime.resources
              / ("textures/animated_mojang/title/minecraft_animation/minecraft_" + std::to_string(index) + ".png");
    auto utf8 = path.generic_u8string();
    return {reinterpret_cast<char const*>(utf8.data()), utf8.size()};
}
BedrockTextureData const* textureData(size_t index) {
    if (index >= runtime.textures.size() || !runtime.textures[index]) return nullptr;
    auto const* data = runtime.textures[index]->mClientTexture.get();
    if (!data || data->mIsMissingTexture != IsMissingTexture::No
        || !(static_cast<unsigned>(data->mTextureLoadState) & static_cast<unsigned>(TextureLoadState::LoadedBit)))
        return nullptr;
    return data;
}
void queueTextures(std::shared_ptr<mce::TextureGroup> const& group) {
    if (!group || (runtime.locations.empty() && runtime.caveLocations.empty())) return;
    std::erase_if(runtime.queuedGroups, [](auto const& item) { return item.expired(); });
    const bool queued = std::any_of(runtime.queuedGroups.begin(),runtime.queuedGroups.end(),
        [&](auto const& item) { return item.lock()==group; });
    if (queued && runtime.textureGroup.lock()==group) return;
    // Keep the locations alive; native asynchronous loading owns the image/GPU work.
    if (!runtime.caveLocations.empty()) {
        // Queue the loading picture before the large cave/title atlases.
        if (!queued && runtime.caveLocations.size() > 1)
            group->loadTexturesAsync(gsl::span<ResourceLocation>{runtime.caveLocations.data() + 1, 1}, std::nullopt);
        if (!queued) group->loadTexturesAsync(gsl::span<ResourceLocation>{runtime.caveLocations.data(), 1}, std::nullopt);
        runtime.nativeScene.setTextureSource(group, &runtime.caveLocations[0],
            runtime.caveLocations.size() > 1 ? &runtime.caveLocations[1] : nullptr);
    }
    if (!queued && !runtime.locations.empty())
        group->loadTexturesAsync(gsl::span<ResourceLocation>{runtime.locations}, std::nullopt);
    // Acquire the actual color textures now, not just their descriptions.
    runtime.textures.resize(runtime.locations.size());
    runtime.titleRefresh = {};
    for (size_t i = 0; i < runtime.textures.size(); ++i) {
        runtime.textures[i] = std::make_unique<mce::TexturePtr>(
            group->getTexture(runtime.locations[i], false, std::nullopt, cg::TextureSetLayerType::Color)
        );
    }
    runtime.textureGroup = group;
    if (!queued) runtime.queuedGroups.push_back(group);

}
bool loadTextures(MinecraftUIRenderContext& ctx) {
    if (runtime.textureGroup.lock() != ctx.mTextureGroup) {
        runtime.textures.clear();
        runtime.titleRefresh = {};
    }
    queueTextures(ctx.mTextureGroup);
    if (!runtime.config.minecraftTitleAnimation) return true;
    if (runtime.textures.empty()) runtime.textures.resize(9);
    bool ready = true;
    for (size_t i = 0; i < runtime.textures.size(); ++i) {
        const bool loaded = runtime.titleRefresh[i].update(runtime.textures[i], nowMs(),
            [&] { return std::make_unique<mce::TexturePtr>(ctx.getTexture(runtime.locations[i], false)); },
            [&](auto const&) { return textureData(i) != nullptr; });
        ready = loaded && ready;
    }
    return ready;
}

std::optional<scene::CavePage> pageForName(std::string const& name) {
    if (matches(runtime.config.menuScreenNames, name)) return scene::CavePage::Main;
    return scene::cavePageForScreen(name);
}
std::optional<scene::CavePage> pageForStack(ISceneStack const& stack, IClientInstance& client) {
    std::string routerRoute;
    bool routerRead = false;
    auto inspect = [&](AbstractScene const& screen) -> std::optional<scene::CavePage> {
        const auto raw = screen.getRawScreenName();
        const auto name = screen.getScreenName();
        const auto route = screen.getRoute();
        const bool web = raw.find("/hbui/") != std::string::npos || name.find("/hbui/") != std::string::npos;
        if (web && !routerRead) {
            routerRead = true;
            if (auto location = client.getCurrentUIRouter().getCurrentLocation()) {
                routerRoute = location->mFragment.get();
                if (routerRoute.empty()) routerRoute = location->mUrl.get();
            }
        }
        if (auto page = scene::cavePageForIdentity({raw, name, route, routerRoute})) return page;
        if (matches(runtime.config.menuScreenNames, raw) || matches(runtime.config.menuScreenNames, name))
            return scene::CavePage::Main;
        return std::nullopt;
    };
    // Read every scene before resolving: during a pop the foreground stays
    // alive until its exit animation ends, but the camera already targets its parent.
    const auto scenes = stack.getScreenStackView();
    std::vector<scene::CaveStackEntry> entries;
    entries.reserve(scenes.size());
    for (auto const& entry : scenes) {
        auto* screen = entry.mScene.get();
        bool popping = false;
        if (screen) {
            if (auto closing = runtime.closingScenes.find(screen); closing != runtime.closingScenes.end()) {
                popping = closing->second == screen->mSceneId && screen->isExiting();
                if (!popping) runtime.closingScenes.erase(closing);
            }
            entries.push_back({inspect(*screen), popping});
        } else entries.push_back({});
    }
    return scene::cavePageForStackEntries(entries, static_cast<std::size_t>(std::max(0, stack.getScheduledPopCount())));
}
void updatePresentation(double time, bool foregroundMenu, bool titleReady) {
    if (foregroundMenu && runtime.pendingReplay) {
        runtime.pendingReplay = false;
        runtime.timeline = Timeline{Timing{.maxPresentationMs = runtime.config.maxPresentationSeconds * 1000.0}};
        runtime.camera.replay();
        runtime.firstMenuRenderMs = -1;
        runtime.assetsTimedOut = runtime.warnedTitle = false;
    }
    if (foregroundMenu) {
        runtime.enteredMenu = true;
        if (runtime.firstMenuRenderMs < 0) {
            runtime.firstMenuRenderMs = time;
        }
    }
    const bool caveReady = !runtime.config.caveBackground
        || runtime.nativeScene.textureAvailable(time);
    const bool ready = !runtime.config.minecraftTitleAnimation || (titleReady && caveReady);
    if (foregroundMenu && !ready && !runtime.assetsTimedOut
        && time - runtime.firstMenuRenderMs >= runtime.config.maxPresentationSeconds * 1000.0) {
        runtime.assetsTimedOut = true;
        runtime.timeline.skip();
        logger().warn("Opening resources unavailable; releasing menu without camera flight.");
    }
    if (skipRequested.exchange(false)) runtime.timeline.skip();
    runtime.frame = runtime.timeline.update(time, foregroundMenu, ready, runtime.config.minecraftTitleAnimation);
    skipAllowed.store(foregroundMenu && runtime.frame.phase != Phase::MenuIdle);

}

void fill(
    MinecraftUIRenderContext& ctx,
    float                     x,
    float                     y,
    float                     width,
    float                     height,
    mce::Color                color,
    float                     alpha = 1
) {
    RectangleArea area{};
    area._x0 = x;
    area._x1 = x + width;
    area._y0 = y;
    area._y1 = y + height;
    ctx.fillRectangle(area, color, alpha);
}
void background(MinecraftUIRenderContext& ctx, glm::vec2 size, double time) {
    const bool drawn = runtime.nativeScene.draw(ctx.mScreenContext, size.x, size.y, time, runtime.cameraFrame,
        !runtime.enteredMenu || runtime.frame.phase == Phase::Waiting);
    if (drawn)
        return;
    fill(ctx, 0, 0, size.x, size.y, mce::Color{30,30,31});
}
void image(MinecraftUIRenderContext& ctx, int frame, glm::vec2 pos, glm::vec2 size) {
    auto const* data = textureData(static_cast<size_t>(frame / 8));
    if (!data) return;
    const auto region = titleRegion(frame);
    ctx.drawImage(data->mClientTexture.get(), pos, size, {region.u, region.v}, {region.width, region.height}, false);
    ctx.flushImages(mce::Color{255, 255, 255}, 1, HashedString{"ui_textured_and_color"});
}

// Restore the exact first-preview strategy: root alpha plus propagation,
// hide the native title temporarily, then restore all fields in reverse order.
class ControlChanges {
    struct Saved {
        std::shared_ptr<UIControl> control;
        float                      alpha;
        bool                       propagate;
    };
    std::vector<Saved> saved;

public:
    glm::vec2 titlePosition{}, titleSize{};
    bool      foundTitle = false;
    void      change(std::shared_ptr<UIControl> const& control, float alpha, bool propagate) {
        saved.push_back({control, control->mAlpha, control->mPropagateAlpha});
        control->mAlpha          = alpha;
        control->mPropagateAlpha = propagate;
    }
    void visit(std::shared_ptr<UIControl> const& control, glm::vec2 parent, int depth, int& remaining) {
        if (!control || depth > 64 || --remaining < 0) return;
        auto position = parent + control->mParentRelativePosition.get();
        if (!foundTitle && matches(runtime.config.titleControlNames, control->mName.get()) && control->mSize.get().x > 0
            && control->mSize.get().y > 0) {
            foundTitle    = true;
            titlePosition = position;
            titleSize     = control->mSize.get();
            change(control, 0, true);
        }
        for (auto const& child : control->mChildren.get()) visit(child, position, depth + 1, remaining);
    }
    ~ControlChanges() {
        for (auto it = saved.rbegin(); it != saved.rend(); ++it) {
            it->control->mAlpha          = it->alpha;
            it->control->mPropagateAlpha = it->propagate;
        }
    }
};

LL_TYPE_INSTANCE_HOOK(
    MenuOpened,
    HookPriority::Normal,
    StartMenuScreenController,
    &StartMenuScreenController::$onOpen,
    void
) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        menuController.store(static_cast<ScreenController*>(this));
        if (runtime.enteredMenu && runtime.config.replayOnReturnToMenu) runtime.pendingReplay = true;
    }
    origin();
}
LL_TYPE_INSTANCE_HOOK(
    SelectMenuTech,
    HookPriority::Normal,
    ui::ScreenTechStackSelector,
    &ui::ScreenTechStackSelector::getTechStackForScreen,
    ui::TechStack,
    std::string const& name
) {
    if (active.load() && runtime.config.preferJsonMainMenu && matches(runtime.config.menuScreenNames, name))
        return ui::TechStack::JsonUI;
    return origin(name);
}
LL_TYPE_INSTANCE_HOOK(DestroyScreen, HookPriority::Normal, ScreenView, &ScreenView::$dtor, void) {
    if (blockedView.load() == this) blockedView.store(nullptr);
    if (menuController.load() == mController.get()) menuController.store(nullptr);
    origin();
}
LL_TYPE_INSTANCE_HOOK(
    PreloadAtLoadingBar,
    HookPriority::Normal,
    MinecraftGame,
    &MinecraftGame::_initLoadingBarMaterials,
    SerialWorkList::WorkResult
) {
    auto result = origin();
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        queueTextures(getTextureGroup());
    }
    return result;
}
LL_TYPE_INSTANCE_HOOK(
    PreloadAtMenuResources,
    HookPriority::Normal,
    MinecraftGame,
    &MinecraftGame::_initWaitForMainMenuResources,
    SerialWorkList::WorkResult
) {
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        queueTextures(getTextureGroup());
    }
    return origin();
}

void observeScreenExit(AbstractScene const& screen, bool popping) {
    if (!active.load()) return;
    std::lock_guard guard(runtime.mutex);
    if (popping) runtime.closingScenes[&screen] = screen.mSceneId;
    else runtime.closingScenes.erase(&screen); // A push also exits the parent, but must retain its camera.
}
LL_TYPE_INSTANCE_HOOK(
    BaseScreenExit, HookPriority::Normal, BaseScreen, &BaseScreen::$onScreenExit, void,
    bool popping, bool transitions, std::shared_ptr<AbstractScene> pushed
) {
    origin(popping, transitions, pushed);
    observeScreenExit(*this, popping);
}
LL_TYPE_INSTANCE_HOOK(
    JsonScreenExit, HookPriority::Normal, UIScene, &UIScene::$onScreenExit, void,
    bool popping, bool transitions, std::shared_ptr<AbstractScene> pushed
) {
    origin(popping, transitions, pushed);
    observeScreenExit(*this, popping);
}
LL_TYPE_INSTANCE_HOOK(
    WebScreenExit, HookPriority::Normal, OreUI::Scene, &OreUI::Scene::$onScreenExit, void,
    bool popping, bool transitions, std::shared_ptr<AbstractScene> pushed
) {
    origin(popping, transitions, pushed);
    observeScreenExit(*this, popping);
}
LL_TYPE_INSTANCE_HOOK(
    RenderFrame,
    HookPriority::Normal,
    GameRenderer,
    &GameRenderer::renderCurrentFrame,
    void,
    float interpolation
) {
    const auto previous = backgroundFrame;
    struct Restore {
        BackgroundFrame old;
        ~Restore() { backgroundFrame = old; }
    } restore{previous};
    backgroundFrame = {.inside = true};
    if (active.load()) {
        std::lock_guard guard(runtime.mutex);
        const double time = nowMs();
        std::optional<scene::CavePage> page;
        auto* client = mClient.get().get();
        if (!client) client = mPrimaryClient.get().get();
        if (client) {
            const auto current = client->getCurrentSceneStack().get();
            const auto main = client->getMainSceneStack().get();
            // Remove stale identities without retaining or dereferencing destroyed scenes.
            std::erase_if(runtime.closingScenes, [&](auto const& entry) {
                const auto contains = [&](ISceneStack const* stack) {
                    if (!stack) return false;
                    for (auto const& element : stack->getScreenStackView())
                        if (element.mScene.get() == entry.first) return true;
                    return false;
                };
                return !contains(current.get()) && !contains(main.get());
            });
            if (current) page = pageForStack(*current, *client);
            if (!page && main && main.get() != current.get()) page = pageForStack(*main, *client);
        }
        backgroundFrame.pageResolved = page.has_value();
        if (page && *page != scene::CavePage::Main && runtime.enteredMenu) runtime.timeline.skip();
        bool titleReady = !runtime.config.minecraftTitleAnimation;
        if (!titleReady && runtime.textures.size() == 9) {
            titleReady = true;
            for (std::size_t i = 0; i < 9; ++i) titleReady = textureData(i) != nullptr && titleReady;
        }
        updatePresentation(time, page == scene::CavePage::Main, titleReady);
        runtime.cameraFrame = runtime.camera.update(time, page, runtime.frame);
        backgroundFrame.presentationUpdated = true;
    }
    origin(interpolation);
}
LL_TYPE_INSTANCE_HOOK(
    DrawCubemap,
    HookPriority::Normal,
    CubemapBackgroundScreen,
    &CubemapBackgroundScreen::$render,
    void,
    ScreenContext&           ctx,
    FrameRenderObject const& frameObject
) {
    // Native render calls setupScreen AND cleanupScreen internally. Its UI
    // projection is no longer active after origin returns (26.51 binary verified).
    origin(ctx, frameObject);
    if (!active.load() || !runtime.config.caveBackground || mWidth <= 0 || mHeight <= 0 || !mScreenSetupCleanup) return;
    std::lock_guard guard(runtime.mutex);
    auto&           strategy = *mScreenSetupCleanup;
    strategy.setupScreen(ctx);
    struct Cleanup {
        AbstractScreenSetupCleanupStrategy& strategy;
        ScreenContext&                      ctx;
        ~Cleanup() { strategy.cleanupScreen(ctx); }
    } cleanup{strategy, ctx};
    const bool submitted = runtime.nativeScene.draw(ctx, static_cast<float>(mWidth), static_cast<float>(mHeight), nowMs(), runtime.cameraFrame,
        !runtime.enteredMenu || runtime.frame.phase == Phase::Waiting);
    if (backgroundFrame.inside) backgroundFrame.native3DDrawn = submitted;
    if (submitted) return;
    ScreenRenderer::singleton().fill(ctx, 0, 0, static_cast<float>(mWidth), static_cast<float>(mHeight), mce::Color{30,30,31});
}

LL_TYPE_INSTANCE_HOOK(DrawScreen, HookPriority::Normal, ScreenView, &ScreenView::render, void, UIRenderContext& base) {
    if (!active.load()) return origin(base);
    std::lock_guard guard(runtime.mutex);
    // JSON drawing can be invoked outside renderCurrentFrame; keep fallback
    // presentation/page flags local in that case as well.
    const auto previousBackground = backgroundFrame;
    const bool standalone = !backgroundFrame.inside && !currentScope;
    struct RestoreStandalone {
        BackgroundFrame old;
        bool restore;
        ~RestoreStandalone() { if (restore) backgroundFrame = old; }
    } restoreStandalone{previousBackground, standalone};
    if (standalone) backgroundFrame = {};
    auto            name = getScreenName();
    if (runtime.config.debugScreens && runtime.loggedScreens.size() < 64 && runtime.loggedScreens.insert(name).second)
        logger().info("JSON UI screen: {}", name);
    RenderScope scope;
    auto*       controller = menuController.load();
    scope.menu = (controller && mController.get() == controller) || matches(runtime.config.menuScreenNames, name);
    scope.bootProgress = !runtime.enteredMenu && (name == "loading_screen" || name == "progress_screen");
    scope.size         = mSize.get();
    ScopedRender renderScope(scope);
    auto&        ctx = static_cast<MinecraftUIRenderContext&>(base);
    const double time = nowMs();
    if (!backgroundFrame.pageResolved) {
        auto page = scope.menu ? std::optional{scene::CavePage::Main} : pageForName(name);
        if (page) {
            if (*page != scene::CavePage::Main && runtime.enteredMenu) runtime.timeline.skip();
            const bool ready = loadTextures(ctx);
            updatePresentation(time, *page == scene::CavePage::Main, ready);
            runtime.cameraFrame = runtime.camera.update(time, page, runtime.frame);
            backgroundFrame.pageResolved = true;
            backgroundFrame.presentationUpdated = true;
        }
    }
    if (!scope.menu) {
        blockedView.store(nullptr);
        if (!runtime.enteredMenu) loadTextures(ctx);
        origin(base);
        if (scope.bootProgress && runtime.config.caveBackground) {
            const scene::CaveCameraFrame loadingFrame{.pose=scene::caveOpeningStart, .loadingBackdropAlpha=1};
            runtime.nativeScene.draw(ctx.mScreenContext,scope.size.x,scope.size.y,time,loadingFrame,true);
        }
        return;
    }
    const bool ready    = loadTextures(ctx);
    runtime.enteredMenu = true;
    if (!backgroundFrame.presentationUpdated) {
        updatePresentation(time, true, ready);
        runtime.cameraFrame = runtime.camera.update(time, scene::CavePage::Main, runtime.frame);
    }
    scope.frame   = runtime.frame;
    blockedView.store(runtime.config.blockMenuInputDuringIntro && scope.frame.menuAlpha < .95f ? this : nullptr);

    // A 3D background already submitted in this frame must not be covered by
    // the opaque JSON fallback. The flag is scoped to native renderCurrentFrame.
    if (runtime.config.caveBackground) {
        if (!backgroundFrame.native3DDrawn) background(ctx, scope.size, time);
        scope.backgroundDrawn = true;
    }
    ControlChanges changes;
    if (mVisualTree && mVisualTree->mRootControl) {
        auto const& root = mVisualTree->mRootControl;
        if (scope.frame.phase == Phase::Waiting || scope.frame.phase == Phase::MenuOpening)
            changes.change(root, root->mAlpha * scope.frame.menuAlpha, true);
        if (ready && runtime.config.minecraftTitleAnimation) {
            int remaining = 4096;
            changes.visit(root, {}, 0, remaining);
        }
    }
    // The original preview drew this AFTER origin(base), over every menu widget.
    // Use the same proven texture/UV path BEFORE origin(base); widgets and
    // subsequently rendered modal pages retain their foreground order.
    if (changes.foundTitle && scope.frame.titleFrame >= 0) {
        auto placement = titlePlacement(changes.titlePosition.x, changes.titlePosition.y, changes.titleSize.x);
        image(ctx, scope.frame.titleFrame, {placement.x, placement.y}, {placement.width, placement.height});
    } else if (!changes.foundTitle && ready && runtime.config.minecraftTitleAnimation && !runtime.warnedTitle) {
        runtime.warnedTitle = true;
        logger().warn("Native title control not found; native title kept.");
    }
    origin(base);
}

LL_TYPE_INSTANCE_HOOK(
    DrawPanorama,
    HookPriority::Normal,
    PanoramaRenderer,
    &PanoramaRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       layer
) {
    if (!active.load() || !runtime.config.caveBackground) return origin(ctx, client, owner, layer);
    std::lock_guard guard(runtime.mutex);
    if (currentScope && currentScope->menu && currentScope->backgroundDrawn) return;
    if (backgroundFrame.inside && backgroundFrame.native3DDrawn) return;
    glm::vec2 size = currentScope ? currentScope->size : owner.mSize.get();
    background(ctx, size, nowMs());
}
LL_TYPE_INSTANCE_HOOK(
    DrawPaperDoll,
    HookPriority::Normal,
    PaperDollRenderer,
    &PaperDollRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       layer
) {
    if (!active.load() || !currentScope || !currentScope->menu) return origin(ctx, client, owner, layer);
    const float fade = currentScope->frame.menuAlpha;
    if (fade <= 0) return;
    const float alpha = mPropagatedAlpha;
    struct Restore {
        float& value;
        float  old;
        ~Restore() { value = old; }
    } restore{mPropagatedAlpha, alpha};
    mPropagatedAlpha *= fade;
    origin(ctx, client, owner, layer);
}
LL_TYPE_INSTANCE_HOOK(
    DrawProgress,
    HookPriority::Normal,
    ProgressBarRenderer,
    &ProgressBarRenderer::$render,
    void,
    MinecraftUIRenderContext& ctx,
    IClientInstance&          client,
    UIControl&                owner,
    int                       layer
) {
    if (active.load() && !runtime.enteredMenu) {
        std::lock_guard guard(runtime.mutex);
        loadTextures(ctx);
    }
    if (!active.load() || !currentScope || !currentScope->bootProgress || !runtime.config.restyleLoadingProgress)
        return origin(ctx, client, owner, layer);
    struct Restore {
        ProgressBarRenderer& renderer;
        mce::Color           primary, secondary;
        ~Restore() {
            renderer.mPrimaryColor   = primary;
            renderer.mSecondaryColor = secondary;
        }
    } restore{*this, mPrimaryColor, mSecondaryColor};
    mPrimaryColor   = mce::Color{239, 50, 61};
    mSecondaryColor = mce::Color{71, 29, 34};
    origin(ctx, client, owner, layer);
}
LL_TYPE_INSTANCE_HOOK(
    MenuButton,
    HookPriority::Normal,
    ScreenView,
    &ScreenView::handleButtonEvent,
    void,
    uint        button,
    ButtonState state,
    FocusImpact impact
) {
    if (active.load() && blockedView.load() == this && state == ButtonState::Down) return;
    origin(button, state, impact);
}
using Hooks = ll::memory::HookRegistrar<
    MenuOpened,
    SelectMenuTech,
    DestroyScreen,
    PreloadAtLoadingBar,
    PreloadAtMenuResources,
    BaseScreenExit,
    JsonScreenExit,
    WebScreenExit,
    RenderFrame,
    DrawCubemap,
    DrawScreen,
    DrawPanorama,
    DrawPaperDoll,
    DrawProgress,
    MenuButton>;
std::unique_ptr<Hooks> hooks;
} // namespace

void prepareCave(std::filesystem::path const& modDirectory) {
    std::lock_guard guard(runtime.mutex);
    runtime.nativeScene.load(modDirectory / "resources" / "data" / "normal_cave.mesh");
}
void start(Config const& config, std::filesystem::path const& modDirectory) {
    if (hooks || !config.enabled) return;
    {
        std::lock_guard guard(runtime.mutex);
        runtime.config    = config;
        runtime.resources = std::filesystem::absolute(modDirectory / "resources");
        runtime.epoch     = Clock::now();
        runtime.timeline  = Timeline{Timing{.maxPresentationMs = config.maxPresentationSeconds * 1000.0}};
        runtime.frame = {};
        runtime.camera.reset();
        runtime.cameraFrame = {};
        runtime.closingScenes.clear();
        runtime.textures.clear();
        runtime.queuedGroups.clear();
        runtime.textureGroup.reset();
        runtime.titleRefresh = {};
        runtime.nativeScene.reset();
        runtime.nativeScene.setTextureSource({}, nullptr);
        runtime.locations.clear();
        runtime.caveLocations.clear();
        if (config.minecraftTitleAnimation) {
            runtime.locations.reserve(9);
            for (size_t i = 0; i < 9; ++i) {
                auto path = assetPath(i);
                runtime.locations.emplace_back(Core::PathView{path}, ResourceFileSystem::Raw);
            }
        }
        if (config.caveBackground) {
            runtime.caveLocations.reserve(2);
            for (auto const* name : {"atlas.png", "loading_screen-100.png"}) {
                auto        utf8 = (runtime.resources / "textures/animated_mojang/cave" / name).generic_u8string();
                std::string path{reinterpret_cast<char const*>(utf8.data()), utf8.size()};
                runtime.caveLocations.emplace_back(Core::PathView{path}, ResourceFileSystem::Raw);
            }
        }
        runtime.assetsTimedOut = runtime.enteredMenu = runtime.pendingReplay = false;
        runtime.warnedTitle = false;
        runtime.firstMenuRenderMs = -1;
        runtime.loggedScreens.clear();
    }
    active.store(true);
    skipAllowed.store(false);
    keyListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::KeyInputEvent>(
        [](ll::event::KeyInputEvent& event) {
            if (active.load() && skipAllowed.load() && event.keyCode() == 27 && event.isDown()) {
                skipRequested.store(true);
                event.cancel();
            }
        }
    );
    hooks = std::make_unique<Hooks>();
}
void stop() {
    active.store(false);
    blockedView.store(nullptr);
    menuController.store(nullptr);
    skipRequested.store(false);
    skipAllowed.store(false);
    if (keyListener) ll::event::EventBus::getInstance().removeListener<ll::event::KeyInputEvent>(keyListener);
    keyListener.reset();
    hooks.reset();
    std::lock_guard guard(runtime.mutex);
    runtime.textures.clear();
    runtime.queuedGroups.clear();
    runtime.textureGroup.reset();
    runtime.nativeScene.reset();
    runtime.nativeScene.setTextureSource({}, nullptr);
}
} // namespace animated_mojang::client
