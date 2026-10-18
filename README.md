# Animated Mojang

[![简体中文](https://img.shields.io/badge/README-%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87-blue)](README.zh-CN.md)
[![Release](https://img.shields.io/github/v/release/MRUIAW/Animated-Mojang)](https://github.com/MRUIAW/Animated-Mojang/releases/latest)

A LeviLamina client mod that brings an animated Minecraft title, a 3D cave background, and camera transitions to Minecraft Bedrock Edition. Inspired by [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang); the startup and menu integration are implemented for Bedrock.

## Compatibility

| Component | Supported version |
| --- | --- |
| Minecraft Bedrock for Windows | 1.26.51.01 |
| LeviLamina | 26.51.6, client variant |
| Platform | Windows x64 |
| Mod | 1.0.0-mc26.51 |

The `-mc26.51` suffix identifies the compatible Minecraft version family, following Playback's version convention. The feature version is 1.0.0. This is a client mod; other game or loader versions have not been verified.

## Features

- A 3D cave with baked colored lighting, animated flowing lava, wall-mounted soul torches, and flame/smoke particles.
- A flight from a loading-screen image into the cave, synchronized with the title animation and menu fade. Press **Esc** to skip.
- The original mod's Mojang Studios intro after loading: the animated Mojang logo, the STUDIOS fade-in and the startup sound, synchronized like the original; the flight then dives past the finished Mojang card into the cave. Menu music waits for the logo and starts with the flight. Esc skips both.
- Camera views and transitions for the main menu, world list, settings, dressing room, Realms, and Marketplace.
- Resource preloading and reused scene buffers. After a rendering stall, animations resume without jumping over the entire stalled interval.
- The game's built-in JSON main menu is selected by default for title and control animations. No separate resource pack activation is required.

## Installation

Close the game before installing or updating.

### Using lip

Run this in the game installation directory managed by LeviLamina/lip:

```powershell
lip install "github.com/MRUIAW/Animated-Mojang#client@1.0.0-mc26.51"
```

The client dependency is declared in [tooth.json](https://github.com/MRUIAW/Animated-Mojang/blob/main/tooth.json). You can also install the client variant through LeviLauncher when the package is listed.

### Manual installation

1. Download **Animated-Mojang-client-windows-x64.zip** from the [latest release](https://github.com/MRUIAW/Animated-Mojang/releases/latest).
2. Extract the **AnimatedMojang** folder into the game's **mods** directory.
3. Start the game with LeviLamina.

When updating manually, keep your existing **mods/AnimatedMojang/config/config.json**. The lip manifest marks that file for preservation; back up custom settings before updating.

## Configuration

Edit **mods/AnimatedMojang/config/config.json** and restart the game. The configuration schema version is **5**; it is independent of the mod release version.

| Option | Default | Behavior |
| --- | --- | --- |
| enabled | true | Enable the mod |
| preferJsonMainMenu | true | Use the built-in JSON main menu |
| minecraftTitleAnimation | true | Play the title and shared intro; disabling it opens the menu directly |
| caveBackground | true | Show the 3D cave and intro loading image |
| restyleLoadingProgress | true | Recolor matching JSON progress bars before the first main menu |
| replayOnReturnToMenu | false | Request intro replay when returning to the main menu |
| blockMenuInputDuringIntro | true | Block menu button presses before the menu is revealed |
| mojangLogoAnimation | true | Play the Mojang Studios intro after loading |
| mojangFadeIn | true | Fade the intro in over the loading screen instead of cutting to it |
| mojangCardDive | true | Fly past the finished Mojang card into the cave (needs caveBackground and minecraftTitleAnimation); otherwise the intro fades out over the flight |
| holdMenuMusic | true | Hold menu music during the intro and its sound, then start it at once |
| startupSound | true | Play the startup sound with the Mojang intro |
| startupSoundFile | startup.ogg | Sound file in resources/sounds/animated_mojang/ |
| startupSoundVolume | 1.0 | Startup sound volume, further scaled by the game's main and sound volume; clamped to 0–1 |
| startupSoundDelayMs | 350 | Logo time at which the sound starts (the logo waits for it); clamped to 0–3000 |
| introFlightStartMs | 4866 | Logo time at which the flight starts; never before the finished STUDIOS frame (3560); clamped to 3560–20000 |
| maxPresentationSeconds | 15 | Resource/presentation timeout; clamped to 5–60 seconds |
| debugScreens | false | Log JSON screen names when explicitly enabled |
| menuScreenNames | start_screen | JSON main-menu recognition list |
| titleControlNames | minecraft_title, title_image, logo | Candidate original title controls |

Every part of the opening can be turned off on its own: the Mojang intro (`mojangLogoAnimation`), its fade-in, the dive past the card, the sound, the menu-music hold, the title and camera flight (`minecraftTitleAnimation`), the cave (`caveBackground`) and the progress bar color. Without the flight, the intro fades straight into the menu, together with any part of the sound that belongs to the flight. The defaults follow the original mod's timing; a longer custom soundtrack can be synchronized with `startupSoundFile`, `startupSoundDelayMs` and `introFlightStartMs`.

Routine resource, camera, frame-gap and periodic performance diagnostics are removed from the release. Normal logging is limited to the version message and actionable warnings/errors.

## Known limitations

- Native UI loading can still stall the game. Animations cannot display new frames while the game stops rendering.
- Lighting is baked, with CPU projection and ordering plus extra clipping for lava intersections; there is no full-scene GPU depth buffer or dynamic fragment lighting.
- Some thin block models are simplified. Glass, cobwebs, grass/flowers, and wall signs are not drawn. A flattened lava strip and some stone texture borders remain possible visual issues.
- The loading image contains a static progress bar. The earliest native loading screen is not fully replaced; a dark background is used until its texture is ready.
- Title/control animation for the native OreUI main menu, dedicated world connection/exit camera flows, and an in-game mod settings page are not implemented.
- Paper-doll fading, controller/touch input, world entry/exit, and additional display modes need further in-game verification.

## Building

Requires the Visual Studio C++ toolchain, Windows SDK, LLVM clang-cl, xmake, Node.js, and PowerShell. LeviLamina is pinned in xmake.lua.

```powershell
node tools/fetch-assets.mjs --verify-only
node tools/build-cave-atlas.mjs --verify-only
node tools/build-cave.mjs --verify-only
node tools/build-cave-particles.mjs --verify-only
node tests/cave_builder_tests.mjs
xmake f -y -p windows -a x64 -m release --target_type=client
xmake -y AnimatedMojang TimelineTests CpuSceneTests CaveSceneTests CaveParticleTests CaveOcclusionTests CaveCameraTests
xmake run TimelineTests
xmake run CpuSceneTests
xmake run CaveSceneTests
xmake run CaveParticleTests
xmake run CaveOcclusionTests
xmake run CaveCameraTests
node tests/cave_camera_path_tests.mjs
./tools/package-release.ps1
```

Run tools/fetch-assets.mjs without --verify-only to restore missing pinned upstream assets. The scene builders regenerate their respective derived files when run without that flag.

Build output is in **bin/AnimatedMojang**. Release ZIPs include runtime files, configuration, both READMEs, the changelog, and license/provenance notices. Development notes, source assets, tests, and debug symbols are excluded.

## Maintenance and credits

See [AGENTS.md](https://github.com/MRUIAW/Animated-Mojang/blob/main/AGENTS.md) for architecture, coding rules, camera extension instructions, validation, and release procedures. The two foundational analyses in vibe_development are source-repository references only.

See [CHANGELOG.md](CHANGELOG.md), [NOTICE.md](NOTICE.md), and [LICENSE](LICENSE). The repository uses CC0 1.0; NOTICE describes upstream and game asset provenance. Minecraft and Mojang belong to their respective owners. This is an unofficial project.
