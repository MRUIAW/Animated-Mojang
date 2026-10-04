# Animated Mojang

[![English](https://img.shields.io/badge/README-English-blue)](README.md)
[![Release](https://img.shields.io/github/v/release/MRUIAW/Animated-Mojang)](https://github.com/MRUIAW/Animated-Mojang/releases/latest)

基于 LeviLamina 的客户端模组，为基岩版加入 Minecraft 标题动画、三维洞穴背景和菜单运镜。参考 [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang) 的设计与素材，启动和菜单接入针对基岩版重新实现。

## 兼容环境

| 组件 | 支持版本 |
| --- | --- |
| Windows Minecraft 基岩版 | 1.26.51.01 |
| LeviLamina | 26.51.6，客户端变体 |
| 平台 | Windows x64 |
| 模组 | 1.0.0-mc26.51 |

沿用 Playback 的版本命名方式，`-mc26.51` 表示兼容的 Minecraft 版本系列，功能版本为 1.0.0。本模组仅用于客户端，其他游戏或加载器版本尚未验证。

## 功能

- 三维洞穴，包含烘焙彩色光照、动态流动岩浆、墙上灵魂火把，以及火焰和烟雾粒子。
- 从加载界面图片飞入洞穴，与标题动画及菜单渐显同步；按 **Esc** 跳过。
- 主菜单、世界列表、设置、更衣室、Realms 和市场各有相机视角及切换动画。
- 预读资源、复用场景缓存；渲染卡顿后继续播放，避免直接跳过整个停顿区间。
- 默认选择游戏内置 JSON 主菜单来实现标题和控件动画；无需额外启用资源包。

## 安装

安装或更新前请关闭游戏。

### 使用 lip

在由 LeviLamina/lip 管理的游戏安装目录执行：

```powershell
lip install "github.com/MRUIAW/Animated-Mojang#client@1.0.0-mc26.51"
```

客户端依赖已写入 [tooth.json](https://github.com/MRUIAW/Animated-Mojang/blob/main/tooth.json)。模组被收录后，也可通过 LeviLauncher 选择客户端变体安装。

### 手动安装

1. 从 [最新 Release](https://github.com/MRUIAW/Animated-Mojang/releases/latest) 下载 **Animated-Mojang-client-windows-x64.zip**。
2. 解压，将 **AnimatedMojang** 文件夹放入游戏的 **mods** 目录。
3. 通过 LeviLamina 启动游戏。

手动更新时保留自己的 **mods/AnimatedMojang/config/config.json**。lip 清单已将该文件列入保留规则；更新前建议备份自定义配置。

## 配置

修改 **mods/AnimatedMojang/config/config.json** 后重启游戏。配置格式版本为 **4**，与模组发行版本独立。

| 选项 | 默认值 | 行为 |
| --- | --- | --- |
| enabled | true | 启用模组 |
| preferJsonMainMenu | true | 使用内置 JSON 主菜单 |
| minecraftTitleAnimation | true | 播放标题和共享开场；关闭后直接进入菜单 |
| caveBackground | true | 三维洞穴与开场加载图片 |
| restyleLoadingProgress | true | 对首次菜单前匹配到的 JSON 进度条改色 |
| replayOnReturnToMenu | false | 返回主菜单时请求重播开场 |
| blockMenuInputDuringIntro | true | 菜单揭示前阻止按钮按下 |
| maxPresentationSeconds | 15 | 资源等待和展示超时，限制在 5–60 秒 |
| debugScreens | false | 手动开启后记录 JSON 页面名 |
| menuScreenNames | start_screen | JSON 主菜单识别列表 |
| titleControlNames | minecraft_title、title_image、logo | 原标题控件候选名称 |

发行版已移除资源预读、运镜状态、帧间卡顿及周期性能诊断日志。默认仅输出版本加载信息和必要异常提示。

## 已知限制

- 原生 UI 加载仍可能让游戏停帧；游戏停止渲染时，运镜无法继续显示新画面。
- 使用烘焙光照、CPU 投影与排序，并对岩浆交叠做额外裁切；尚无全场景 GPU 深度或动态片元光照。
- 部分薄形状模型简化，玻璃、蛛网、草花和墙牌尚未绘制。岩浆斜面底部压扁条带与部分石头纹理边框仍可能存在。
- 加载图片里的进度条是静态画面。最早的原生加载界面未完整接管，纹理未就绪时显示暗底。
- 原生 OreUI 主菜单的标题与控件动画、连接及退出世界专用运镜、游戏内模组设置页面尚未实现。
- 纸娃娃渐显、手柄和触屏、世界进出及更多显示模式仍需进一步游戏验证。

## 构建

需要 Visual Studio C++ 工具链、Windows SDK、LLVM clang-cl、xmake、Node.js 和 PowerShell。xmake.lua 已固定 LeviLamina 版本。

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

不带 --verify-only 运行 tools/fetch-assets.mjs 可补回固定上游素材；各场景生成工具不带该参数时重新生成对应派生资源。

构建产物在 **bin/AnimatedMojang**。安装包包含运行文件、配置、双语 README、更新记录和许可及来源说明，排除开发文档、素材源文件、测试和调试符号。

## 维护与来源

后续维护请阅读 [AGENTS.md](https://github.com/MRUIAW/Animated-Mojang/blob/main/AGENTS.md)，其中说明架构、编程规范、新增运镜、验证与发布步骤。vibe_development 中的两份基础分析仅作为源码仓库参考。

更新记录见 [CHANGELOG.md](CHANGELOG.md)，素材来源见 [NOTICE.md](NOTICE.md)，许可见 [LICENSE](LICENSE)。仓库采用 CC0 1.0；上游及游戏素材来源在 NOTICE 中单独说明。Minecraft 和 Mojang 的名称、标识属于各自权利人，本项目为非官方模组。
