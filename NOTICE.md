# 素材来源

Minecraft 标题动画及洞穴素材来自 [chara2015/animated_mojang](https://github.com/chara2015/animated_mojang)，固定提交 ef68ffb6d6cdbdfab4db960497719416e8b7a6bc。上游仓库采用 CC0 1.0。基岩版客户端启动、渲染与菜单流程由本项目重新实现。

normal_cave.schem 来自同一提交，Git blob SHA-1 为 fa6df51e9120b4af4c9655cf4bd82efa2b6523be。normal_cave.mesh 是从该数据转换的几何与纹理坐标；normal_cave.particles 包含对应发射器与遮挡体素。

洞穴图集的源图片为 cave_blocks.png（ad02c048e7caad5f2b04eacc9764211477eee84d）、cave_lava_flow.png（ce0a46d3593d79e78394cd99e75f022bef966771）和 cave_particles_clean.png（1bd1d1e83715650c95ca3d6facae8db23720a49e）。派生 atlas.png 重排图块并添加亮度余量，保留原图 alpha 和粒子 RGBA。安装包包含派生图集；源图片保留在源码仓库中。

startup.ogg 来自同一提交的 sounds/startup.ogg（b37095b271ba0671906bf09f114e2a10d150ac5c），mojang_animation.png（b466e9b50b727353a82602755fe50cf44fd5ca6e）和 studios.png（f216dbc87185d1b9abe879c3dda296c11055b6f9）来自同一提交的 textures/gui，均未经修改。派生 mojang_card.png 由 tools/build-intro-card.mjs 用这两张图合成（最后一帧标志与 STUDIOS），作为开场飞行掠过的画面。

loading_screen-100.png 由项目使用者提供，为基岩版加载界面图片。上述来源说明不改变 Minecraft 原有素材的权利归属。

Minecraft 和 Mojang 名称、标识属于各自权利人。本项目为非官方客户端模组。
