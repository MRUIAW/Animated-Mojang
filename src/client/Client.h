#pragma once

#include "mod/Config.h"
#include <filesystem>

namespace animated_mojang::client {
void prepareCave(std::filesystem::path const& modDirectory);
void start(Config const& config, std::filesystem::path const& modDirectory);
void stop();
} // namespace animated_mojang::client
