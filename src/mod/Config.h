#pragma once

#include <string>
#include <vector>

namespace animated_mojang {
struct Config {
    int                      version                   = 5;
    bool                     enabled                   = true;
    bool                     preferJsonMainMenu        = true;
    bool                     minecraftTitleAnimation   = true;
    bool                     mojangLogoAnimation       = true;
    bool                     mojangFadeIn              = true;
    bool                     mojangCardDive            = true;
    bool                     holdMenuMusic             = true;
    bool                     caveBackground            = true;
    bool                     restyleLoadingProgress    = true;
    bool                     replayOnReturnToMenu      = false;
    bool                     blockMenuInputDuringIntro = true;
    bool                     debugScreens              = false;
    bool                     startupSound              = true;
    std::string              startupSoundFile          = "startup.ogg";
    double                   startupSoundVolume        = 1.0;
    double                   startupSoundDelayMs       = 350;
    double                   introFlightStartMs        = 4866;
    double                   maxPresentationSeconds    = 15.0;
    std::vector<std::string> menuScreenNames           = {"start_screen"};
    std::vector<std::string> titleControlNames         = {"minecraft_title", "title_image", "logo"};
};
} // namespace animated_mojang
