# Animated Mojang: maintenance instructions

This file is for agents and developers maintaining this repository. Read README.md and README.zh-CN.md for the shipped behavior and limitations before changing implementation. User instructions take precedence over this guide.

## Supported baseline and sources

- Minecraft Bedrock for Windows **1.26.51.01**, LeviLamina **26.51.6 client**, Windows x64, C++20 with clang-cl and the MD runtime.
- Release version **1.0.0-mc26.51** and configuration schema version **4** are independent.
- [LeviLamina documentation](https://lamina.levimc.org/zh/) and [API reference](https://lamina.levimc.org/api/).
- [Hook guide](https://lamina.levimc.org/zh/developer_guides/how_to_guides/hook_guide/) and [C++ style guide](https://lamina.levimc.org/zh/maintainer_guides/cpp_style_guide/).
- [Official release procedure](https://lamina.levimc.org/zh/developer_guides/tutorials/create_your_first_mod/#发布你的模组), [lip manifest reference](https://lip.levimc.org/concepts/package_manifest.html), and [lip v3 JSON schema](https://github.com/futrime/lip/blob/main/schemas/manifest.schema.json).
- [Original Java mod](https://github.com/chara2015/animated_mojang/tree/ef68ffb6d6cdbdfab4db960497719416e8b7a6bc). Assets are pinned to that commit by tools/fetch-assets.mjs.
- vibe_development/animated_mojang_implementation.md describes the original mod. vibe_development/levilamina_api_feasibility.md is initial research, not the current implementation specification.

Online API documentation can describe newer releases. For ABI-sensitive changes, inspect the headers in the **installed 26.51.6 xmake package** first. Do not assume Java startup callbacks, render layers, block models, or loading progress exist in Bedrock. IDA is an optional investigation tool when headers alone cannot explain native behavior; it is not required for ordinary camera edits.

## Code map

| File or directory | Responsibility |
| --- | --- |
| src/mod/MyMod.cpp, Config.h | Loader lifecycle, configuration, startup preparation |
| src/mod/MemoryOperators.cpp | LL-compatible memory operators; retain them |
| src/client/Client.cpp | Native/JSON hooks, preloading, page recognition, menu opacity, title order, paper doll, input and Esc |
| src/animation/Timeline.h | Resource waiting, shared opening clock, title frames, UI fade and skip |
| src/scene/CaveCamera.h | Page identities, camera poses, flight, transitions and view/projection matrices |
| src/scene/CaveOpeningBackdrop.h | Loading image as a fixed world plane |
| src/scene/NativeScene.cpp | Cached CPU scene projection and native mesh-helper submission |
| src/scene/CpuProjection.h, CaveOcclusion.h, UiCaveBatch.h | Clipping, ordering, lava depth subtraction and UI vertex batches |
| src/scene/CaveModel.h, MeshVertex.h | Binary mesh loading and vertex layout |
| src/scene/CaveAnimation.h, CaveParticles.h, AssetRefresh.h | Lava UV animation, particles and unresolved texture retries |
| tools/build-cave*.mjs, cave-*.mjs, read-cave-nbt.mjs | Source cave conversion, atlas, geometry, lighting and emitters |
| tests/ | CPU/geometry, lifecycle, camera, particles, occlusion, source UV/rail checks |
| tools/package-release.ps1 | Explicit release file list and archive-content hash validation |
| xmake.lua, tooth.json, .github/workflows/ | SDK pin, build targets, lip client metadata and release automation |

## Programming rules

- Follow .clang-format: four spaces, no tabs, 120-column limit. Format the touched area; avoid reformatting unrelated files.
- Keep existing naming consistent: animated_mojang namespaces, PascalCase types and camelCase functions. Avoid a mass rename of the existing my_mod/MyMod entry point during unrelated work.
- Keep pure scene math and state transitions separate from native hooks so they remain testable without running Minecraft.
- Use RAII for modified control alpha, shader color, clipping, matrix stacks and screen setup/cleanup. Restore native state on every exit path.
- Match hooked function signatures to the pinned SDK. Normal hook priority is the default; preserve origin calls and the active/config guards.
- Keep hooks and input listeners owned by the existing lifecycle. stop() must disable interception, remove listeners/hooks and clear live native handles. Startup-specific hooks are installed early by design to catch loading-bar initialization; do not add world-dependent operations to load().
- Runtime state is guarded by its recursive mutex. Frame scope is thread-local; do not turn temporary background flags into persistent global state. Retain weak ownership for texture groups and renderer services.
- Treat a missing/unfinished texture during asynchronous loading as normal. Preserve AssetRefresh retries and the readiness gate rather than caching a placeholder forever.
- Release logging should contain the version message and actionable errors/warnings. Do not restore automatic preload, camera, frame-gap or five-second performance logs. debugScreens is an explicit, disabled-by-default page-name diagnostic. Remove temporary investigation counters after use.
- Do not read model files, rebuild atlases or allocate large fresh vertex buffers every frame. Preserve stream capacity reuse and the 30 Hz moving / 20 Hz idle projection cache unless measurements justify a change.
- Pure CPU timing excludes native upload, GPU work and actual game FPS. Compilation and tests do not prove visible in-game rendering.
- Keep English README.md and README.zh-CN.md synchronized. Update existing documentation; do not create a new report or ZIP for every minor edit. Package on release requests or when explicitly needed for testing.

## Rendering invariants

The main menu does not provide a world camera suitable for this scene. CaveCamera supplies our own view matrix; the scene is projected to menu pixels on the CPU. NativeScene submits bulk Position/Color/UV0 streams through MeshHelpers::renderMeshImmediately and the game's temporary UI tessellator. Earlier GPU projection bridge experiments failed; changing this path requires in-game evidence, not merely a successful build.

- Draw in the existing panorama/cubemap background channel so settings and world-list pages share the replacement. Do not bring back a separate opaque foreground cave overlay or a 2D cave fallback.
- The native cubemap renderer cleans up its screen projection before returning. Keep the explicit setupScreen/cleanupScreen scope around our submission.
- Preserve the per-frame native3DDrawn flag so JSON fallback rendering cannot cover an already submitted background.
- Draw the animated title before native foreground controls, and temporarily hide only the matched original title. Preserve root alpha propagation and paper-doll handling.
- Scene ordering uses a painter algorithm; lava intersections receive additional depth subtraction. There is no general GPU depth buffer. Particle, UV, alpha and reciprocal-depth interpolation must survive clipping.
- Lighting is a baked approximation using 48 selected source lights. Do not replace source textures or wash out their contrast to compensate for lighting errors.
- The loading image is a fixed 16:9 world plane, currently at distance 64. Size and distance must scale together to preserve the initial framing. It is not a screen-space sprite and does not represent live progress. Its opacity fades from 1000 to 1500 ms.
- Title delay is 1880 ms, UI reveal starts at 2700 ms with a 500 ms fade, and the opening flight lasts 4000 ms. The title has 69 frames at 40 ms each. Keep Timeline and camera behavior synchronized.
- A blocked game renderer cannot show new animation frames. Retain bounded continuation after stalls; never claim it fixes native UI loading or renders independently of the game.

## Adding or changing camera movements

Start in src/scene/CaveCamera.h. CavePose is **{x, y, z, yaw, pitch, roll}**, angles in degrees. Coordinates are cave-local blocks, not a live world's coordinates. The view transform follows **Rz * Rx * Ry * Ry(180 degrees) * T(-eye)** with gentle sway. Do not replace it with a guessed lookAt calculation: that previously reversed orientation.

For a different view of an existing page:

1. Locate a landmark in normal_cave.schem using read-cave-nbt.mjs, or inspect existing landmark checks in cave_camera_tests.cpp.
2. Change that page's pose constant, keeping the physical eye in air. The physical eye is derived from inverse(caveView(...)); sway means it may differ from the raw pose.
3. Verify the target landmark is in front of the camera and inside the intended projected framing. Check the whole return/transition route, not just its endpoint.
4. Run CaveCameraTests, then cave_camera_path_tests.mjs. Verify rapid switching and return during the native UI exit animation in-game.

For a new page with its own movement:

1. Add a CavePage value, its pose constant, and a cavePagePose switch branch.
2. Add exact JSON aliases to cavePageForScreen and/or slash-boundary OreUI route roots to cavePageForRoute. Shared /hbui shell names alone do not identify a page. Unknown dialogs should keep their parent camera.
3. Preserve cavePageForIdentity precedence: live route, live web router route, then screen aliases. A stale web route must not override a returned JSON main menu.
4. Keep exit/pop handling in Client.cpp: target the parent while the outgoing UI is still animating. Do not wait until its scene is destroyed.
5. Extend the pagePoses array and route/landmark checks in cave_camera_tests.cpp. Update all loops and expected CSV row counts in cave_camera_path_tests.mjs that currently assume six pages and 30 directed page pairs.
6. Check all directed transitions, opening/skip/replay, interrupted transitions, small/large window aspects and several sway offsets. Current page transitions use 500 ms easing; interrupted transitions start from the current interpolated pose.
7. If linear pose interpolation crosses stone, revise endpoints or implement an explicit intermediate path with matching tests. Do not weaken the physical-air checks to hide collisions.
8. Recheck visibility, lava overlap, loading-plane lifetime and render cost in-game. Add tests for the specific new route or failure rather than duplicating constants from implementation.

Current views: main (31,13,6), worlds (23,17,15), settings (23,13,15), dressing room (20,13,37), Realms and Marketplace (18,13,18) with different angles. The dressing room faces the entrance ledge opposite the lava; Marketplace faces the furnace. See code for exact rotations.

## Assets and binary data

Use tools/fetch-assets.mjs for pinned upstream sources. Do not silently move the upstream commit. Source blocks/lava/particles PNGs and normal_cave.schem remain in the repository for reproducible generation; they are excluded from installed packages.

- Run build-cave-atlas.mjs when atlas composition changes, build-cave.mjs when geometry/UV/light baking changes, and build-cave-particles.mjs when emitter or voxel data changes.
- Then run all three with --verify-only and run tests/cave_builder_tests.mjs. Keep generated resources committed alongside generator changes.
- Mesh format is AMCV0005: a 24-byte header and 36-byte vertices, including a uint32 texture field. Preserve MeshVertex layout or version the format and update its loader/tests together.
- Particle format is AMCP0001. Its emitter/occlusion data must stay consistent with the source cave.
- Preserve source opacity, atlas insets, lava frame selection and rail state mapping. Flat rails require both straight orientations and four corner orientations; validate source-pixel exits and reciprocal neighboring connections.
- Add new runtime files to both xmake.lua and package-release.ps1. Keep NOTICE.md provenance current. Do not package AGENTS.md, vibe_development, PDBs, tests or source-only assets.

## Validation

Use the build and verification commands in README.md. Run CaveCameraTests before cave_camera_path_tests.mjs because the latter reads CSV exported by the compiled camera.

Select checks appropriate to the change: TimelineTests for waiting/skip/fade, CpuSceneTests for clipping and native-batch preparation, CaveSceneTests for model projection, CaveParticleTests for particle behavior, CaveOcclusionTests for lava depth, and camera/builder checks for pose or source-state changes. Tests should verify a meaningful property independently of the implementation.

For rendering or hook changes, build the client DLL and verify the changed behavior in-game. Distinguish local test success from user visual confirmation. Retain README limitations unless there is new evidence resolving them; an earlier severe stall was traced to chiyanmap, which does not prove all native menu stalls are fixed.

## Release procedure

Publish only when the user requests a release. Do not request repeat permission when it is already authorized.

1. Use the Playback-style X.Y.Z-mcA.B convention: X.Y.Z is the feature version and A.B is the Minecraft compatibility family (currently 26.51). The suffix is a compatibility label; keep the GitHub release non-prerelease unless the user requests an experimental release. Pin the complete version in lip install examples, since unqualified semver selection can omit suffixed versions. Update xmake.lua modVersion, tooth.json version and the MyMod.cpp version message together; update both READMEs. Do not change the config schema solely because the release version changes.
2. Add a dated Keep a Changelog entry, with Added/Changed/Fixed/Removed sections as applicable. Keep known limitations accurate.
3. Keep tooth.json format_version=3 and its official format_uuid. This mod has only the client / win-x64 variant, pinned github.com/LiteLDev/LeviLamina#client dependency, and AnimatedMojang/ -> mods/AnimatedMojang/ placement. Preserve the config file.
4. Validate metadata, build and run the relevant regressions. Package using tools/package-release.ps1; it checks archive contents against the explicit file list.
5. Commit the source, derived assets and documentation; push the intended branch without force. Never commit credentials, local capture data, package caches or bin/build outputs.
6. Create an annotated **vX.Y.Z-mcA.B** tag on the release commit and push it. Do not move a published release tag to a different commit.
7. Create the GitHub Release for that tag. The published-release workflow checks the version, builds/tests/packages and writes the version's Changelog notes to the release. It preserves already uploaded assets with the same filename.
8. Ensure the release includes **Animated-Mojang-client-windows-x64.zip**, the filename referenced by tooth.json. An additional versioned ZIP is a convenience copy. Upload verified files before marking a draft public when using manual publication.
9. Verify the published tag's tooth.json, asset download and package hash. The package is distributed through Git/lip; Bedrinth/LeviLauncher catalog indexing is external and may lag. Do not claim listing success without checking it.

There is no need to publish development notes as release assets. Keep a single maintenance guide and the two foundational analyses, not per-version investigation reports.
