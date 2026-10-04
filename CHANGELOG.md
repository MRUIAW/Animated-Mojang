# Changelog

All notable changes to this project are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and release versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [1.0.0-mc26.51] - 2026-10-04

### Added

- First public Windows x64 client release for Minecraft Bedrock 1.26.51.01 and LeviLamina 26.51.6.
- A 3D cave background with baked colored lighting, animated flowing lava, wall-mounted soul torches, and flame/smoke particles.
- A loading-image-to-cave intro synchronized with the Minecraft title animation and menu fade, with Esc to skip.
- Camera views and transitions for the main menu, world list, settings, dressing room, Realms, and Marketplace.
- Resource preloading, reusable scene buffers, and bounded animation steps after rendering stalls.
- A lip v3 client package manifest, English/Chinese READMEs, and agent maintenance instructions.

### Fixed

- Straight and curved rail texture orientation and connection boundaries.
- Menu title placement below foreground controls, and camera return during UI exit transitions.

### Changed

- Release packages include runtime files and basic documentation only.
- Versions use the Playback-style `-mc26.51` compatibility suffix; the feature version is 1.0.0.

### Removed

- Unused rendering prototypes, historical backups, unused Mojang/STUDIOS animation and startup audio assets.
- Automatic texture-preload, camera-state, frame-gap, and periodic performance logs, including their diagnostic-only timers and counters.

See [README.md](https://github.com/MRUIAW/Animated-Mojang/blob/v1.0.0-mc26.51/README.md#known-limitations) for outstanding rendering details and validation limits. Native UI stalls are not claimed to be fixed.

[Unreleased]: https://github.com/MRUIAW/Animated-Mojang/compare/v1.0.0-mc26.51...HEAD
[1.0.0-mc26.51]: https://github.com/MRUIAW/Animated-Mojang/releases/tag/v1.0.0-mc26.51
