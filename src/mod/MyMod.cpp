#include "mod/MyMod.h"

#include "client/Client.h"
#include "ll/api/Config.h"
#include "ll/api/io/FileUtils.h"
#include "ll/api/mod/RegisterHelper.h"
#include <algorithm>
#include <cmath>

namespace my_mod {

MyMod& MyMod::getInstance() {
    static MyMod instance;
    return instance;
}

bool MyMod::load() {
    auto path = getSelf().getConfigDir() / "config.json";
    try {
        if (std::filesystem::exists(path)) {
            auto content = ll::file_utils::readFile(path);
            if (!content || content->empty()) {
                getSelf().getLogger().error("Could not read config.json; mod was not activated.");
                return false;
            }
        }
        // LL returns false for a successfully migrated configuration as well.
        // Persist the new schema, preserving supported user preferences.
        if (!ll::config::loadConfig(mConfig, path) && !ll::config::saveConfig(mConfig, path)) {
            getSelf().getLogger().error("Could not migrate config.json; mod was not activated.");
            return false;
        }
    } catch (std::exception const& error) {
        getSelf().getLogger().error("Invalid config.json: {}", error.what());
        return false;
    }
    if (!std::isfinite(mConfig.maxPresentationSeconds)) mConfig.maxPresentationSeconds = 15;
    mConfig.maxPresentationSeconds = std::clamp(mConfig.maxPresentationSeconds, 5.0, 60.0);
    if (!std::isfinite(mConfig.startupSoundVolume)) mConfig.startupSoundVolume = 1;
    mConfig.startupSoundVolume = std::clamp(mConfig.startupSoundVolume, 0.0, 1.0);
    if (!std::isfinite(mConfig.startupSoundDelayMs)) mConfig.startupSoundDelayMs = 350;
    mConfig.startupSoundDelayMs = std::clamp(mConfig.startupSoundDelayMs, 0.0, 3000.0);
    if (!std::isfinite(mConfig.introFlightStartMs)) mConfig.introFlightStartMs = 4866;
    mConfig.introFlightStartMs = std::clamp(mConfig.introFlightStartMs, 3560.0, 20000.0);
    animated_mojang::client::start(mConfig, getSelf().getModDir());
    if (mConfig.enabled && mConfig.caveBackground)
        animated_mojang::client::prepareCave(getSelf().getModDir());
    getSelf().getLogger().info("Animated Mojang 1.0.0-mc26.51 loaded for Bedrock 26.51 / LL 26.51.6 (client).");
    return true;
}

bool MyMod::enable() {
    animated_mojang::client::start(mConfig, getSelf().getModDir());
    return true;
}

bool MyMod::disable() {
    animated_mojang::client::stop();
    return true;
}

bool MyMod::unload() {
    animated_mojang::client::stop();
    return true;
}

} // namespace my_mod

LL_REGISTER_MOD(my_mod::MyMod, my_mod::MyMod::getInstance());
