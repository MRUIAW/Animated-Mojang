#pragma once

#include <string>
#include <vector>

namespace animated_mojang {
struct Config {
    int                      version                   = 4;
    bool                     enabled                   = true;
    bool                     preferJsonMainMenu        = true;
    bool                     minecraftTitleAnimation   = true;
    bool                     caveBackground            = true;
    bool                     restyleLoadingProgress    = true;
    bool                     replayOnReturnToMenu      = false;
    bool                     blockMenuInputDuringIntro = true;
    bool                     debugScreens              = false;
    double                   maxPresentationSeconds    = 15.0;
    std::vector<std::string> menuScreenNames           = {"start_screen"};
    std::vector<std::string> titleControlNames         = {"minecraft_title", "title_image", "logo"};
};
} // namespace animated_mojang
