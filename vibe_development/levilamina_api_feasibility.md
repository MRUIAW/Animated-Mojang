# Animated Mojang 的 LeviLamina API 对照与实现可行性分析

> 分析日期：2026-10-03（Asia/Shanghai）。
> 目标：在 Windows 基岩版客户端上，以 LeviLamina 的公共 API、所提供的 Bedrock 头文件和 Hook 能力，实现与 chara2015/animated_mojang 相似的启动及菜单体验。
> 原模组基线：ef68ffb6d6cdbdfab4db960497719416e8b7a6bc；完整逻辑见同目录 animated_mojang_implementation.md。
> LL 源码基线：6df3cf832559ae108d934e268d9852a4aaa6bece（main，2026-10-02 14:58:23 UTC）。
> 本文是文档与源码的静态调查结果。没有构建客户端模组，没有启动基岩版，没有验证 Hook 的实际调用顺序、DLL 导出或最终画面。
> 分类：“已确认”指文档或源码中确实存在；“可实现”指有完整或合理的技术路线；“待验证”指不能仅凭声明保证运行成功。
> 设计约束（2026-10-03 补充）：Java 版与基岩版的原生启动加载动画、加载流程和进度机制不同。本项目以基岩版实际启动流程为基础，参考原模组的自定义视觉和功能；不直接使用 Java 版原生启动动画，也不假设其 LoadingOverlay/reload 流程在基岩版中存在。

## 1. 结论

> 本文保留初期 API 调查结论；1.0.0 的实际功能和限制以仓库 README.md 为准。

**LL 目前确实提供 Windows 基岩版客户端支持，且存在主菜单/UI 绘制、输入、动画、资源包、声音和加载状态相关接口。这个方向值得做，但不是把 Java 模组的 Mixin 和渲染代码直接改写成 C++。**

最有把握的实现范围：

- 在已有客户端 UI 绘制阶段显示 Mojang/Minecraft 逐帧动画。
- 自定义背景、STUDIOS 字样、透明度、文字与进度条的外观。
- 主菜单开场状态机、按钮延迟显示、鼠标/键盘输入控制。
- 通过客户端本地资源包提供图片和音效。
- 对已有进度界面读取其当前 ProgressHandler，并尝试使用相应进度。
- 监听进入、退出和取消进入世界，管理背景动画生命周期。

需要原型验证的范围：

- 最早的游戏启动 Splash 是否经过 ScreenView::render，LL 注入是否赶得上目标阶段。
- 游戏启动时的原生进度条究竟在哪里绘制，是否有可读取的真实百分比。
- 旧 JSON UI 与 OreUI 的主菜单分别走哪条绘制和路由路径。
- 在加载结束后继续保留自定义动画覆盖层，何时允许声音和纹理访问。
- 不修改原生初始化任务的前提下，获得足够早且可靠的启动完成信号。

目前不能承诺直接完成的范围：

- 覆盖 LL/模组尚未加载之前的系统或启动器画面。
- 通过现成高层 API 安全延迟原生初始化任务完成。
- 用现成 LL 高层接口完整复刻原模组的三维洞穴、自定义光照、火焰烟雾和 Java GPU shader 管线。
- 一个仅装在 BDS 服务端的模组，直接改变远端客户端尚未进服时的主菜单和开机动画。

依据：[客户端安装][CLIENT]、[版本表][VERSIONS]、[UI 事件声明][UIEVENT]、[事件实现][UIEVENT_IMPL]、[客户端初始化实现][BOOT]。

## 2. 调研覆盖范围与 API 文档的阅读边界

中文站点对应源码目录 docs/main/contents 下共有 43 个中文 Markdown 页面。本次覆盖其导航、安装与版本、开发教程、操作指南、20 个 API 模块/总览以及维护和使用说明；与本项目相关的客户端渲染、Hook、生命周期、UI、输入和线程章节结合实现源码交叉核查。完整页面清单见附录 A。

另对 LL 完整递归目录树进行检索：返回 28,133 个条目，未截断。重点检查了客户端公共 API 的全部文件清单，并查看 65 个相关头文件/实现文件；附录 B 可逐项追溯。

**不把上述范围说成“逐个读完 Doxygen 中全部引擎类型和每一个方法”。** Doxygen 包含非常大的 Bedrock 头文件集合，且在线类/文件索引出现访问失败或页面过大；本次通过同一官方仓库的固定提交核对相关 API 声明与 LL 实现。未核查的引擎实现不被当作已确认功能。

API 网站和中文模块说明要同时看，但最终实现必须以选定版本 SDK 为准。Doxygen 配置将 src、src-server、src-client 的 ll/api 和 mc 目录同时作为输入。因此：

1. 页面上看到一个类型，不代表服务端、客户端都能调用。
2. 看到方法声明，不代表符号已导出。
3. 看到字段名，不代表字段布局在不同游戏版本相同。
4. 看到“LoadingScreen”“HUD”“ImGui”，不能据此认定存在完整、可直接使用的模组 GUI 框架。
5. main 的 API 不自动等于本地模板声明的 26.20.* API。

入口：[中文文档][DOCS]、[API 文档][API]。

## 3. 平台和当前工程必须先对齐

### 3.1 必须是客户端模组

官方版本表明确列出客户端支持；旧资料中“LL 只支持 BDS”的结论不适用于当前版本。

| LL 版本系列 | 对应客户端版本（本次查阅） | 本项目的意义 |
| --- | --- | --- |
| 26.51.x | 26.51.1 | 当前 main 文档列出的最新系列，本文源码调查主要基于它的开发基线 |
| 26.40.x | 26.40.5 | 需要独立核对 SDK 和 Hook |
| 26.32.x | 26.32.2 | 需要独立核对 SDK 和 Hook |
| 26.20.x | 26.20.4 | 当前本地 tooth.json 的客户端依赖系列 |
| 26.10.x | 26.10.4 | 客户端 enable 时机的教程说明涉及这一系列 |
| 1.9.x | 1.21.132.1 | 已有客户端支持 |
| 1.8.x | 1.21.124.2 | 版本表中较早的客户端支持系列 |
| 1.7.x 及以下列出的系列 | 无 | 不适合作为本项目客户端基础 |

来源：[官方版本表][VERSIONS]。本文不把 main 当作已发布二进制的逐符号保证。

### 3.2 本地工程的实际状态

本地 xmake.lua 默认 target_type 为 server，同时支持 client；目前 add_requires("levilamina", ...) 没有锁版本。tooth.json 则把服务端和客户端都约束为 26.20.*。MyMod::load/enable/disable 目前只有日志占位。

因此后续开发先要：

1. 明确一套“基岩版客户端版本 + LL 精确版本 + client SDK/符号数据”组合。
2. 使用 client 构建配置；不能以 server 目标编译后期待 UI 事件可用。
3. 让构建依赖和 tooth.json 安装依赖一致，避免编译取到新版而安装仍取 26.20。
4. 将发布包放入客户端 mods/<模组名>，使用 #client 依赖。
5. 为版本差异建立适配层，不把具体字段布局散落在动画逻辑里。

这些是下一阶段工程调整建议，本次没有改动构建配置。依据：[客户端安装][CLIENT]及本地 xmake.lua、tooth.json、src/mod/MyMod.cpp。

## 4. 三种 API 层次，以及“能调用”的含义

| 层次 | 示例 | 对本项目的用途 | 边界 |
| --- | --- | --- | --- |
| LL 公共 API | EventBus、BeforeUIRenderEvent、Mod、Config、KeyInputEvent | 生命周期、事件、配置、输入、日志 | 仍受客户端平台和实际触发阶段限制 |
| LL 提供的 Bedrock 头文件 | ScreenView、MinecraftUIRenderContext、MinecraftGame | 操作原生 UI/纹理/声音/状态 | 需要匹配游戏 ABI，常常需要 Hook |
| LL 内部实现 | ll/core/main_win.cpp | 研究加载时机、资源包挂载机制 | 用作证据，不把内部 ModRegistrar 当作模组公共调用入口 |

在固定提交的 mc/_HeaderOutputPredefine.h 中：

- MCAPI 展开为共享库导入标记。
- MCFOLD 同样为 MCAPI，注释说明存在相同 COMDAT 折叠。
- MCNAPI 带有“不提供此 API，需要时提交请求”的 deprecated 提示，同时仍有导入标记。
- MCNAPI 方法不能因为编译器只是给出 deprecated 警告，就当作可成功链接/运行。

Bedrock 虚方法与对应的 $method thunk 也要区分：在有效对象上调用虚方法，与引用特定导出函数地址来 Hook，不是同一种验证。不得在没有可用导出、正确对象或参数布局的情况下绕过这些条件。

依据：[宏定义][MACROS]、[Hook 指南][HOOK]。

## 5. 开始菜单 HUD / GUI：LL 到底有什么

### 5.1 最直接的入口：UIRenderEvent

头文件 ll/api/event/render/UIRenderEvent.h 提供：

- ll::event::BeforeUIRenderEvent
- ll::event::AfterUIRenderEvent
- UIRenderEvent::screenView() → ScreenView&
- UIRenderEvent::uiRenderContext() → MinecraftUIRenderContext&

事件实现拦截的是 **ScreenView::render(UIRenderContext&)**：

| 事件 | 源码行为 | 能用于什么 |
| --- | --- | --- |
| BeforeUIRenderEvent | 发布事件；若被取消则不执行原 ScreenView::render | 在该屏幕绘制前插入内容，或有条件地接管整屏 |
| AfterUIRenderEvent | 先执行原绘制，再发布事件 | 在该屏幕之后绘制覆盖层 |
| After 的 cancel | 类型继承可取消基类，但发布后未检查取消结果 | 不能撤销已经完成的原绘制；还可能影响后续监听器分发 |

中文事件表将两个 UI 事件都列为“不可取消”，与固定提交的 Before 实现不一致。这里采用实际源代码行为；选定的发布版仍需重新核对。

**这不是全局“每个游戏帧只触发一次”的事件。** 一个帧内可能有多个 ScreenView、不同层级、弹窗或客户端实例。动画时间更新和声音触发应当按实例/帧或时间去重，不能每收到一次事件就推进一帧。

来源：[声明][UIEVENT]、[实现][UIEVENT_IMPL]、[事件说明][EVENT]。

### 5.2 怎样识别主菜单和游戏 HUD

ScreenView 提供 MCAPI 方法 getScreenName() 和 getRoute()，并有 mController、mVisualTree、mAnimationController 和 mSize。

可采取的识别流程：

1. 在原型中只记录屏幕名称、route、出现/消失时机和上下层关系。
2. 分别观察首次启动、返回菜单、设置、世界列表、加入世界、游戏 HUD 和暂停菜单。
3. 用经验证的名字/route 白名单选择绘制目标。
4. 主菜单开场与游戏内 HUD 使用不同规则。
5. 进世界时停用菜单背景，防止覆盖正常玩法。

不要根据 Java 的 TitleScreen 类名推断基岩版屏幕名；不要在当前 SDK 没有确认前使用某个硬编码的 start_screen/hud_screen 字符串。也不要用 dynamic_cast 去识别 Bedrock 控制器：教程说明基岩版缺乏 RTTI。

客户端服务还提供 getClientInstance()，不要求先有玩家世界。但返回 optional_ref，早期阶段可能为空。getMultiPlayerLevel() 不是判断启动菜单可用的前置条件，也不能为菜单制造一个虚假的 LocalPlayer。

来源：[ScreenView][SCREEN]、[客户端服务][SERVICE]、[服务实现][SERVICE_IMPL]。

### 5.3 可用的 UI 绘制方法

MinecraftUIRenderContext 提供以下虚方法及 MCAPI thunk：

| API | 对应效果 |
| --- | --- |
| fillRectangle / drawRectangle | 深色背景、黑色过渡遮罩、进度条底槽与填充 |
| drawImage | Mojang 图集、STUDIOS、Minecraft 标题、背景图 |
| flushImages(color, alpha, materialNameHash) | 提交图片批次并设置颜色/透明度 |
| drawText / flushText | 阶段文字、百分比、设置提示 |
| getTexture(ResourceLocation, forceReload) | 请求资源系统中的纹理 |
| getZippedTexture / unloadTexture | 压缩目录资源读取候选、纹理清理 |
| scissor / clipping 相关方法 | 裁剪进度条、限制绘制范围 |

drawImage 的真实签名使用 mce::ClientTexture，而 getTexture 返回 mce::TexturePtr。二者不能当作同一个类型。

在本次头文件中，TexturePtr::mClientTexture 保存 shared_ptr<const BedrockTextureData>；BedrockTextureData 再有 mClientTexture、mTextureLoadState 和 mIsMissingTexture。后续应封装这条访问路径，判断纹理是否准备好或缺失，再绘制，不能直接把 TexturePtr 强制转换成 ClientTexture。

每帧都 forceReload、重新解码图片或重建大图集会拖慢启动。纹理应缓存，并在资源重载时失效。

来源：[绘制上下文][UICTX]、[TexturePtr][TEXTURE]、[BedrockTextureData][TEXTURE_DATA]、[ResourceLocation][LOCATION]。

### 5.4 JSON UI 的控制器、控件和动画

| 类型/方法 | 对应原模组功能 | 建议用途 |
| --- | --- | --- |
| StartMenuScreenController::onCreation/onOpen/onEntered/tick | TitleScreen 初始化、首次开场 | 记录主菜单生命周期，决定是否只播放一次 |
| MainMenuScreenController::onOpen | 主菜单层进入 | 补充屏幕进入识别 |
| addStaticScreenVars(Json::Value&) | 自定义 UI 变量 | 配合资源包 UI 声明，具体绑定需要版本验证 |
| UIControl::mAlpha、mPropagateAlpha | 菜单按钮渐显 | 对指定菜单控件树控制透明度 |
| UIControl::_setVisible / getPropertyBag | 控件可见性、状态 | 需要确认布局、绑定更新是否覆盖修改 |
| UIAnimFlipbook | 逐帧标题动画 | 原生图集播放候选 |
| UIAnimUV | UV 变化 | 图集偏移或平移效果候选 |
| UIAnimAlpha | 渐入渐出 | 原生透明度动画候选 |
| UIAnimationController::_tick | 固定/渲染时间步动画列表 | 对接原生动画生命周期候选 |
| PanoramaRenderer::render | 原菜单全景背景 | 针对背景 renderer 替换或屏蔽 |

StartMenuScreenController 的 startInboxAnimation/resetInboxAnimation 是 Inbox 动画，**不是 Mojang 启动动画 API**。

要改背景，单纯在 BeforeUIRenderEvent 中画一张图可能随后被原生全景覆盖；单纯在 After 中全屏画图又会盖住按钮。优先使用资源包调整指定背景控件，或 Hook 已确认的背景 renderer，同时保留按钮绘制。需要整屏接管时，明确承担原控件绘制和输入处理职责。

UIControl 透明度和可见性不会自动等价于“按钮不能点击”。控件隐藏与输入限制要分别处理。

来源：[StartMenu][START]、[MainMenu][MAINMENU]、[UIControl][CONTROL]、[Flipbook][FLIPBOOK]、[UV][UV]、[Alpha][ALPHA]、[Panorama][PANORAMA]。

### 5.5 OreUI 必须单独适配

头文件中确实存在：

- OreUI::EntryPoints::MainMenuScreen，包含 composeRoute 和 BASE_SCREEN_ID/LEGACY_ROUTE/ROUTE。
- MinecraftGame::getUIRouter，ClientInstance 的多个 UI router/scene stack 方法。
- OreUI::Scene::render(ScreenContext&, FrameRenderObject const&) 及 MCAPI $render。
- OreUI::ViewsCoordinator::createView/update。
- OreUI::ViewRenderer 与 Gameface 渲染纹理。

这证明有新版菜单的底层接入候选，但 ScreenView::render 事件不能被当作“必然覆盖所有 OreUI”的保证。JSON UI 资源包修改也不能自动作用到 OreUI 页面。

若目标主菜单不触发 UIRenderEvent：

1. 先记录原生 scene/route，确认是 OreUI。
2. 以 OreUI::Scene::$render 或经验证的共用 UI 合成点为候选，使用 LL Hook 接入。
3. 可以考虑 ScreenRenderer 在有效 ScreenContext 内绘制覆盖层。
4. 如果要改 OreUI 内部按钮或布局，还需核对 Gameface 数据绑定/资源路径；本文未发现等同于通用网页 DOM 编辑器的 LL 高层 API。

来源：[MainMenu route][ORE_MAIN]、[OreUI Scene][ORE_SCENE]、[ViewsCoordinator][ORE_VIEWS]、[ClientInstance][CLIENT_INSTANCE]、[ScreenRenderer][BLIT]。

### 5.6 HUD、Form、DDUI、ImGui 不应混为一谈

| 名称 | 能否直接完成启动前主菜单 |
| --- | --- |
| UI 绘制事件 + MinecraftUIRenderContext | 对经过 ScreenView 的界面有实际接入路线 |
| 游戏内 HUD | 可以对其实际 UI 绘制目标叠加内容，但需识别和过滤 |
| ll::form::SimpleForm/ModalForm/CustomForm | 面向有效 Player 的表单，不能用于没有世界/玩家的开机画面 |
| ll::ui::CustomForm/MessageBox/ScreenSession | DDUI 服务端会话，不是启动前本地菜单编辑器 |
| ClientInstance::renderImGui | 引擎内部 ImGui 相关绘制入口；没有因此得到完整 LL ImGui SDK |
| FrameBuilder 中 RenderUIImGuiDescription | 参数结构和 DrawItem 当前是空占位，不能直接组装自定义 UI |

DDUI 的 Observable/Binding 能让**玩家会话中的表单**动态更新。官方明确要求操作及活动会话销毁在服务器线程进行，并且构造函数需要 Player&。它可以用于进世界后的设置/调试页，但不承担本项目启动菜单。

如果需要主菜单设置页，应使用客户端原生 UI/资源包定义、OreUI 适配，或自绘控件并处理本地输入。普通配置 JSON 也可以作为首版设置入口。

来源：[DDUI 文档][DDUI]、[ClientInstance][CLIENT_INSTANCE]、[ImGui 描述][IMGUI_DESC]。

## 6. 启动动画及启动进度条：分阶段分析

### 6.0 先区分原模组视觉与游戏原生加载流程

Java 原模组的图集、时间线和镜头效果属于模组自定义内容；LoadingOverlay、reload.isDone、进度读取和覆盖层移除属于 Java 游戏接入机制。两部分不能一起迁移。

基岩版实现必须先观察它自己的启动画面、绘制阶段、资源就绪、音频就绪、进度来源和进入主菜单的条件，再决定自定义动画从何时开始、何时退出。不能因为原模组在 Java 资源重载完成后播放，就默认基岩版也应采用这个顺序。

若后续采用原模组的自定义图集，只能把它们作为重新打包、重新绘制的自定义素材；这不等于直接使用 Java 版的原生启动加载动画。也不预设基岩版原生标志、背景、加载条的布局与 Java 相同。

### 6.1 三种“加载画面”不是同一个东西

| 阶段 | 是否已有 LL 模组/UI 上下文 | 结论 |
| --- | --- | --- |
| 系统、启动器或 LL 注入之前的 Splash | 不一定，甚至尚无模组代码 | 当前模组不能保证覆盖 |
| MinecraftGame 原生初始化及 renderer/resource 初始化 | LL 可能已加载；上下文逐步就绪 | 需要早期 Hook 与具体渲染点验证 |
| 主菜单系统、资源重载或世界连接时的进度屏幕 | 通常有原生 UI 系统 | 更容易通过事件/控制器修改 |

ProgressScreenController 是具体 UI 的控制器；它不自动等于操作系统的 Splash，也不自动等于 MinecraftGame 最早的原生加载条。

### 6.2 LL 模组加载和启用时机是关键

固定提交的 src-client/ll/core/main_win.cpp 表明：

1. LL 的 DllMain 在 DLL_PROCESS_ATTACH 调用 leviLaminaMain。
2. leviLaminaMain 调用 loadAllMods。
3. 之后 LL 通过 Hook MinecraftGame::_initFinish 调用 enableAllMods。
4. 退出窗口时调用 disableAllMods。

因此，**只在 MyMod::enable() 注册启动 Hook，很可能错过此前的大部分初始化画面。**

候选做法：

- load 阶段读取配置、建立自身状态、安装需要早期工作的 Hook。
- AUTO Hook 或显式 HookRegistrar 负责注册；真正读取游戏实例、创建纹理和播放声音，留到已验证的目标回调中。
- 对 UI 事件，在 load 阶段注册监听是源码支持的候选：本次 EventBus::publish 直接调用已登记 listener，没有以 Mod::isEnabled 作为统一分发条件；但仍需检查事件发射器是否已建立、目标调用是否已开始。
- enable 阶段用于已初始化后的菜单功能，不承担此前所有启动事件。
- disable/unload 清理早期 Hook、监听器和缓存；enable 后要能恢复之前被移除的菜单监听。

首个模组教程笼统提醒不要在 load 中做游戏操作，示例把事件放在 enable。启动动画是需要更早注册接入点的专门场景：本文的建议不包括在 load 中直接绘图、取未创建的 Player 或等待 GPU 初始化，也不保证所有版本都允许相同早期注册行为。

来源：[初始化实现][BOOT]、[事件总线实现][BUS]、[官方 FAQ][FAQ]。

### 6.3 找到的直接相关启动方法

MinecraftGame 的头文件中存在以下 MCAPI 方法：

| 方法 | 可调查的用途 | 尚不能推出的结论 |
| --- | --- | --- |
| _initHideSplashScreen() | 原生隐藏 Splash 阶段 | 不能由名称推出 Splash 的渲染位置 |
| _initLoadingBarMaterials() | 加载条材质初始化 | 不是设置进度/画进度条的接口 |
| _initLoadRendererAssets() | renderer 资源阶段 | 未公开总任务数和百分比 |
| _waitForLoadRendererAssets() | 等待 renderer 资源阶段 | 不能直接猜其 WorkResult 返回值 |
| _initWaitForMainMenuResources() | 主菜单资源就绪阶段候选 | 需要验证资源是否包含本模组资产 |
| _initUILoadAsyncWait() | UI 资源等待阶段候选 | 名称不能替代完成状态和调用顺序验证 |
| _initCreatePrimaryClient / _initPrimaryClientComplete | 客户端实例建立阶段 | ClientInstance 是否可取要看具体时刻 |
| _initAudio() | 声音系统初始化 | 音效定义/素材是否可用仍需检查 |
| _initFinish(...) | LL enable 时机、初始化完成候选 | 不是原模组 reload.isDone 的完全等价替代 |
| isReadyToRender / isShowingLoadingScreen / isRenderingLoadingScreen / isInitialized | 运行状态查询 | 不提供百分比，也不一定标识唯一一种加载屏幕 |

AppPlatform 还有 hideSplashScreen / $hideSplashScreen（后者为 MCFOLD）。它是平台层候选，不能单凭阻止 hide 就保证自定义内容能画到原 Splash 上。

GameRenderer::renderCurrentFrame(float)、makeScreenContext(float) 和 _checkAndDrawInputUI(ScreenContext&) 是进一步调查共用绘制阶段的候选。它们不是专用 StartupRenderEvent。

**当前 LoadingScreen.h 只有空的 class LoadingScreen {}。** 不能写出不存在的 LoadingScreen::setProgress/render/getProgress 接口。

来源：[MinecraftGame][GAME]、[AppPlatform][PLATFORM]、[GameRenderer][RENDERER]、[LoadingScreen 占位][LOADING]。

### 6.4 进度条外观与真实进度要分别实现

**画进度条：有足够的基础绘制方法，可行性较高。**

自绘矩形条的基本算法：

- 输入 p，限定为 0..1；屏幕缩放按当前 UI 尺寸。
- 背景底槽固定宽度 W；填充宽度为 W × p。
- 在 drawImage/矩形绘制阶段使用透明度、颜色和裁剪。
- 加载展示值与游戏真实状态分开保存，不能将视觉平滑结果写回原生加载逻辑。

若要复现原模组的平滑，而保持帧率无关，可以采用：
pDisplay += (pTarget - pDisplay) × (1 - exp(-k × deltaSeconds))。
若追求源码行为一模一样，则按原模组每次 render 的平滑参数实现；两者在不同帧率下行为不同。

**读取真实进度：找到部分候选，尚未找到启动全流程的完整高层 API。**

| 类型/方法 | 已确认内容 | 实现判断 |
| --- | --- | --- |
| ProgressHandler::getLoadingProgress(MinecraftScreenModel&) | 虚方法，另有 MCAPI $getLoadingProgress | 可尝试读取当前 handler 的进度；必须核对具体 handler 的语义 |
| ProgressHandler::getLoadingState/getProgressMessage/showLoadingBar | 状态、消息和加载动画类型 | 有助于阶段识别、文字和不定进度样式 |
| ProgressScreenController::mProgressHandler | 当前进度处理器 | 需要确认当前控制器类型、模型对象、生命周期 |
| mProgressHandlerList / mAccumulatedProgressPercentageForHandlers | 多阶段处理器与累计量字段 | 不是现成稳定的全局百分比 API，不能盲读 |
| ResourceLoadProgressHandler::getLoadingState/getProgressMessage/tick | 资源加载状态处理器 | 本类没有重新声明 getLoadingProgress；不能保证提供精确连续百分比 |
| ProgressBarRenderer::render | 原生条形绘制接入候选 | 改外观不意味着掌握任务完成控制 |
| MinecraftGame::getResourceLoadManager | 可以取得管理器的候选 | 管理器本身不提供已确认的公开百分比函数 |
| ResourceLoadManager::isComplete/update/queue/sync | 当前标注 MCNAPI | 不能作为首版可直接调用的稳定接口 |

进度数据应按优先级选择：

1. 若目标启动界面原生已有可读百分比，捕获该值或经过验证的绑定。
2. 若当前 ProgressHandler 提供有效比例，显示其比例，并注明对应的是哪个阶段。
3. 若仅能确认初始化阶段完成，用阶段完成量表示“阶段进度”，不要宣称真实加载百分比。
4. 完全没有可靠比例时，显示不定进度动画和状态文字。

不能以运行时长模拟一个“真实百分比”；也不要因为 ProgressScreenController 出现在 API 中，就默认开机加载条一定由它绘制。

来源：[ProgressHandler][PROGRESS]、[ResourceLoadProgressHandler][RESOURCE_PROGRESS]、[ProgressScreenController][PROGRESS_SCREEN]、[ProgressBarRenderer][PROGRESS_RENDER]、[ResourceLoadManager][RESOURCE]。

### 6.5 原模组时序仅作参考，基岩版需要独立设计

原 Java 26.x 实现的真实顺序是：

1. reload 未完成：显示原版静态标志并更新进度。
2. reload.isDone：首次启动 Mojang 动画，同时保证完成回调只执行一次。
3. 动画播放完：允许加载覆盖层退出。
4. 到主菜单后：播放 Minecraft 标题和洞穴镜头开场。

这段顺序用于说明原模组的行为，不能作为基岩版原生启动流程的描述。基岩版是否在加载期间播放、某个资源阶段后播放，或作为进入主菜单前的独立过渡，应根据实际可用绘制点及目标体验决定。

推荐 LL 方案是**分开游戏就绪状态和展示状态**：

- 让原生初始化和资源加载正常继续。
- 在基岩版中确认绘制与自定义资产可用后，按独立设计的触发条件启动展示；不默认等待一个 Java 式 reload 完成信号。
- 在已存在的 UI/合成阶段覆盖画面；必要时限制隐藏的菜单输入。
- 动画结束且基岩版主菜单确实可进入时，撤下覆盖层，再开始主菜单标题动画；若动画先结束而游戏仍在加载，继续显示基岩版实际加载状态。
- 发生登录、错误或资源包弹窗时，根据实际场景暂停或退出覆盖，保证原生交互可到达。

这能实现相似的可见体验，但内部不是“阻塞原生启动”。

如果目标是原生加载界面本身一直保留，必须进一步验证哪个 scene/renderer 在加载完成后被移除，以及哪个回调能改变其生命周期。**目前没有找到 LL 公共的 holdStartupScreen/setStartupProgress/StartupFinishedEvent 一类专用接口。**

不建议第一阶段拦截 _initFinish 并随意跳过 origin，或在初始化线程 sleep 等待动画。LL 自己就在该函数上启用模组，跳过会影响加载器生命周期；动画还可能依赖被阻止的 UI/声音初始化。

此外，SerialWorkList::WorkResult 在当前头文件中只有空枚举声明。不能凭空给 _initHideSplashScreen 返回 Retry/Continue/Done 或某个整数来“延长启动”；要先补足枚举含义、初始化任务调用链和安全重复执行条件。

来源：[原项目实现分析](animated_mojang_implementation.md)、[LL 初始化][BOOT]、[MinecraftGame][GAME]。

## 7. 原模组功能与 LL API 的逐项映射

| 原模组功能 | Java 实现核心 | LL / Bedrock 对应路线 | 可行性 |
| --- | --- | --- | --- |
| 模组入口 | Fabric initializer | LL_REGISTER_MOD、load/enable/disable | 已有完整基础 |
| 配置开关/读写 | properties、配置 Screen | ll::config + getConfigDir；客户端设置界面另做 | 文件配置可直接实现 |
| 启动背景颜色 | LoadingOverlay 背景供应器 | 资源包背景/绘制 fillRectangle；最早 native Splash 另找 | UI 阶段高；最早阶段待验证 |
| Mojang 79 帧动画 | 图集 + MojangAnimation | drawImage 的 UV 裁剪；或 UIAnimFlipbook | 已有合理实现路线 |
| STUDIOS 淡入 | 独立图片 + alpha | drawImage + flushImages/alpha 动画 | 高 |
| 加载进度条外观 | 原 Overlay renderer + 自定义绘制 | fillRectangle / ProgressBarRenderer Hook | 高 |
| 原生真实加载比例 | reload.getActualProgress | 当前 ProgressHandler/原生绑定候选 | 启动全流程待验证 |
| 加载完成判断 | reload.isDone/checkExceptions | 独立核对基岩版初始化、资源就绪及菜单进入条件；handler 仅在对应界面适用 | 无一对一完整公共接口，不能照搬 Java reload 流程 |
| 延迟撤除加载画面 | 取消 tick，控制 Overlay 移除 | 自绘覆盖层状态机；原生 screen 保持需更深 Hook | 可见等效可做；底层保持待验证 |
| 启动音效 | SoundEvent + UI sound | 资源包声音定义 + MinecraftGame::playUI | 高，需就绪检查 |
| 确认声音真的启动 | SoundEngine.PlayResult | playUI 返回 void，不等价；若需精确确认进一步研究 SoundEngine | 尚无本文确认的等价结果 |
| 声音/画面重试与超时 | 350ms/200ms/8s 状态机 | C++ 自身计时与状态 | 可直接重写 |
| Minecraft 69 帧标题 | 9 张纹理图集 | 自绘 UV 或原生 flipbook | 高 |
| Java Edition 字样 | edition.png | 自定义基岩版/模组副标题资产 | 需要产品选择，不自动照搬 Java 标识 |
| 菜单控件 2700ms 揭示/500ms 淡入 | TitleScreen/Screen Mixin | 指定 UIControl alpha 或资源包 UIAnimAlpha；OreUI 另做 | JSON UI 中等至高 |
| 开场点击屏蔽 | mouseClicked 注入 | MouseInputEvent/KeyInputEvent + 当前状态过滤 | 键鼠有明确路线；手柄/触摸需补 |
| 页面动态背景 | ScreenTypeResolver | getScreenName/getRoute + route 白名单 | 可实现，需采样确认 |
| 进入、退出、取消进入世界 | Screen/GameRenderer 状态 | ClientStartJoinLevelEvent、ClientJoinLevelEvent、ClientExitLevelEvent、ClientCancelJoinLevelEvent | 已有直接对应事件 |
| 镜头数学与过渡 | Bezier、自然样条、相机参数 | 原有数学逻辑重写为 C++ | 算法可移植，实际 3D 提交另验证 |
| 三维洞穴 | schematic → 方块/几何 → GPU | 自解析/转换数据 + Bedrock 网格/RenderDragon；或预渲染背景 | 完整实时 3D 难度高 |
| 方块材质映射 | Java block texture atlas | 建立 Java→Bedrock 材质/方块映射或离线导出模型 | 不能直接复用 Java ID/模型 |
| 动态点光源 | 自定义 shader 数据 | FrameBuilder 光照描述候选，目前相关结构占位 | 未形成完整开放链路 |
| 火焰/烟雾及遮挡 | billboard + depth/light | 先以预渲染/2D 粒子近似，真实 3D 需要渲染适配 | 近似可做，原效果待研究 |
| GPU 缓冲与 GLSL | Java RenderPipeline/GpuDevice | Tessellator/Mesh/Material/RenderDragon 重新设计 | 不能逐行迁移 |
| Java 第三方模组兼容 | Mixin priority/compat hooks | LL Hook 优先级、路由白名单、资源包冲突处理 | 需要重新设计兼容对象 |
| 多版本桥 | Stonecutter/反射桥 | C++ 按 client SDK 版本构建适配层 | 可实现，但不能共用未经验证的偏移 |

本表的“高”表示接口路线充分，不表示已经通过游戏内测试。

## 8. 动画图集、资源包和声音的实现方式

### 8.1 LL 已经有模组资源包挂载逻辑

Mod::getResourceDir 返回模组 resources 目录。客户端 main_win.cpp 的三个相关 Hook 会：

1. 在 RepositorySources::initializePackSource 后，把 LL 与已加载模组的 resources/behavior 目录作为目录包来源加入。
2. 在 VanillaGameModuleClient::initializeResourceStack 中，将检测到属于 mods 路径的资源包加入 stack。
3. 另有服务端 behavior stack 处理，本项目主要依赖客户端资源包。

所以“所有素材都必须自己手工注入 GPU，因为 LL 不会挂资源”并不符合当前源码。

后续建议目录：

    mods/AnimatedMojang/
      manifest.json             LL 模组清单
      AnimatedMojang.dll
      config/config.json
      lang/zh_CN.json
      resources/
        manifest.json           基岩版资源包清单，和 LL 清单是两份文件
        textures/animated_mojang/...
        sounds/...
        sounds/sound_definitions.json
        ui/...                  只在采用 JSON UI 修改时需要

纹理路径、sound 定义 schema、manifest 版本和 UUID 应以目标客户端资源包规范验证。Java assets/labymod_menu 和 sounds.json 不能原封不动当作基岩版资源包使用。

资源包进入 stack 不代表在最早 Splash 时就可请求所有资源；必须记录首次可用时间。路径优先级、其他 UI 包覆盖和资源重载也需要测试。

来源：[Mod API][MOD]、[客户端包初始化实现][BOOT]。

### 8.2 自定义图集可参考原模组，原生加载动画不直接迁移

以下是原模组自定义 Mojang 图集的参数，供选择相似视觉效果时参考；它们不是基岩版原生加载动画的规格，也不规定基岩版的启动时序：

- 79 帧，每帧 40ms，名义 25fps。
- 10 列、8 行，原逻辑图集 6000×1032。
- 单帧 600×129，裁切原点 (31,31)，可见区域 540×83。
- STUDIOS 独立图片，3160ms 开始、400ms 淡入。
- 音效视觉触发点 350ms；原重试间隔 200ms；超时释放点 8 秒。
- 名义基本展示时间 4866ms；原实现单次视觉时间推进最多 50ms。

图集裁剪（归一化 UV 路线）：

    frame = min(floor(visualElapsedMs / 40), 78)
    pixelX = (frame % 10) * 600 + 31
    pixelY = (frame / 10) * 129 + 31
    uv = (pixelX / atlasWidth, pixelY / atlasHeight)
    uvSize = (540 / atlasWidth, 83 / atlasHeight)

这里的除法需要浮点计算。实际 drawImage 的 UV 解释、纹理采样和图集像素尺寸要实测；不要把逻辑尺寸当作已核对的最终资源尺寸。

标题动画则是 69 帧，原资源按每张 8 个纵向帧组织，共 9 张图。可按原规则分段，也可重新打包成更适合设备纹理上限与显存预算的图集。原动画起点为菜单开场后 1880ms。

UIAnimFlipbook 适合原生控件动画，但原模组包含裁切、音效释放、卡顿上限和一次性开场等定制逻辑。首版由 C++ 时间线控制 drawImage 更容易准确还原；并不需要为了使用现成类而强行把全部状态放进原生动画系统。

来源：[原模组完整分析](animated_mojang_implementation.md)、[UI drawImage][UICTX]、[Flipbook][FLIPBOOK]。

### 8.3 声音有入口，但同步精度有边界

MinecraftGame 提供 playUI(name, volume, pitch)、isAudioInitialized、stopSound/stopSounds、getSoundEngine，以及音乐管理相关接口。

实现流程：

1. 资源包注册独立声音事件，避免与原版名字冲突。
2. 在合适客户端回调获得有效游戏接口。
3. 检查音频初始化、相关资产就绪后，在视觉时间达到触发点时调用 playUI。
4. 每次启动会话最多触发一次成功请求；保留降级和超时状态。
5. 离开目标画面时清理本模组的声音状态，避免停止所有游戏声音。

playUI 返回 void，本次没有确认可用的“成功开始播放/播放游标”公共返回接口。因此只能把“请求已提交”与“声音实际开始”区分开。音效缺失、静音、设备重建都可能让实际体验不同；首版可采用就绪检查 + 名义时长同步，不能声称实现音频采样级同步。

源码中的 getMusicManager、fadeOutMusic、queueCustomMusic/playCustomMusic 可以用于单独的菜单 BGM，但启动短音效优先使用 UI sound。不要把 BGM 全局管理拿来代替短音效。

来源：[MinecraftGame 音频接口][GAME]。

## 9. 输入、线程与状态机

### 9.1 输入接口已确认

KeyInputEvent 从 HIDControllerGameCoreDesktop::onKeyDown/onKeyUp 发布；MouseInputEvent 从 MouseDevice::feed 发布。实现均检查事件取消后再执行原逻辑，因此有明确的键鼠阻止默认行为路线。

当前源码按键注册头文件是 ll/api/input/KeyRegistry.h，命名空间 ll::input；getOrCreateKey 需要 name、defaultKeyCodes 等参数。中文 Input 页面仍有旧 ll/api/KeyRegistry.h 路径、ll::KeyRegistry 命名空间及简化签名，不能直接照抄。

开场时输入过滤应满足：

- 仅在本模组确实控制的主菜单/覆盖层阶段启用。
- 只处理要阻止的按键/按钮，避免把错误弹窗等界面永久锁住。
- 隐藏期间的 key down/up 状态要一致恢复。
- 手柄、触摸和不同输入路径不能仅凭键鼠事件宣称全部覆盖。

来源：[按键注册声明][KEY]、[键盘事件实现][KEY_EVENT]、[鼠标事件实现][MOUSE_EVENT]。

### 9.2 ClientThreadExecutor 不能当作主菜单定时器

这是源码调查发现的重要限制：

- ClientThreadExecutor 将任务排入队列。
- 实际消费队列的监听器是 ClientLevelTickEvent。
- ClientLevelTickEvent Hook 的对象是 ClientLevel::_subTick。
- executeAfter 把延迟转成 tick，并依赖该队列更新时累计的 frame。

因此在尚未进入世界的启动画面/主菜单中，不能保证 ClientThreadExecutor 的任务会执行。它的名称不等于“始终推进的客户端 UI 线程”。

建议：

- 动画计时使用 std::chrono::steady_clock。
- 在已验证的 UI/render 回调中推进自身状态。
- 针对多个 ScreenView 去重更新，避免同一实际帧重复累计。
- CPU 数据读取、解压和网格计算可以用线程池；把完成结果交给绘制阶段处理。
- GPU 上传、UI 控件修改和场景栈操作必须回到相应的已验证游戏阶段。
- ServerThreadExecutor、GameTickClock 和游戏 tick sleep 不作为启动动画时钟。
- 不在渲染回调中阻塞等待文件/线程/GPU；未就绪时绘制降级内容。

来源：[ClientThreadExecutor 实现][THREAD]、[ClientLevelTickEvent 实现][LEVEL_TICK]。

### 9.3 建议状态机

    BootObserve
      → AwaitRenderAndAssets
      → CustomStartupPresenting
      → AwaitMenuReadyAndVisualFinished
      → TitleOpening
      → MenuIdle
      ↔ MenuSubScreen
      → JoiningWorld
      → InWorld
      → MenuIdle

其中：

- BootObserve：安装早期观察 Hook，不访问尚未创建的 UI/玩家对象。
- AwaitRenderAndAssets：等待至少一个可用绘制点与动画资源。
- CustomStartupPresenting：在已验证的基岩版绘制阶段展示自定义动画；开始条件由基岩版资源及音频可用性和目标体验决定。游戏初始化独立继续。
- AwaitMenuReadyAndVisualFinished：分别跟踪基岩版菜单就绪与展示完成；未就绪时显示实际加载状态，均满足后再退出覆盖层。退出效果独立设计，不默认采用 Java Overlay 的约 1 秒移除等待。
- TitleOpening：仅首次有效主菜单进入时播放；1880ms 标题启动、2700ms 控件揭示、500ms 渐入。
- MenuSubScreen：页面镜头/背景规则从 route 白名单选择。
- JoiningWorld/InWorld：由客户端世界事件驱动，不绘制菜单背景。
- 返回菜单：是否重播开场作为配置；原模组默认采用进程内一次性开场。

状态中分别保存 engineReady、visualFinished、audioRequested/audioReady、assetsReady；不要合并成一个 loaded 标志。

## 10. 三维洞穴与 RenderDragon：有哪些对应内容，哪里缺口最大

### 10.1 有低层名称和部分方法，不是完全空白

本次确认：

- ScreenContext 包含 tessellator、viewport、frame buffer、MinecraftGraphics、commandListQueue 等绘制相关对象。
- Tessellator 提供 begin、vertex、color、simpleQuad、quad、end、变换和资源服务参数。
- GameRenderer 可创建 ScreenContext，并参与 renderCurrentFrame。
- ScreenRenderer 提供 blit、fill、fillGradient、tessellateQuadBuffer。
- mce::framebuilder::FrameBuilder 声明摄像机、网格、全景、UI、粒子、火焰、光照描述的插入入口及资源服务。
- FrameBuilder::debugDeclareSceneCamera 和多个 _insert 重载可以看出引擎具有相应管线能力。

因此不能说“LL 完全没有渲染相关内容”。

来源：[ScreenContext][SCREEN_CONTEXT]、[Tessellator][TESS]、[GameRenderer][RENDERER]、[ScreenRenderer][BLIT]、[FrameBuilder][FRAME]。

### 10.2 但关键描述结构是空占位

核查发现当前这些头文件没有可用成员布局：

- CameraDescriptionPerspective {}
- LightPointDescription {}
- RenderMeshFallbackDescription {}
- RenderLegacyCubemapDescription {}
- RenderUIImGuiDescription::DrawItem {}

这意味着仅拿到 FrameBuilder::_insert 的函数签名，仍无法正确填写相机、点光和自定义网格描述。不能创建一个空对象后调用，让游戏去读它所期望的实际内存布局。

Tessellator 有顶点/网格构建方法，也不意味着已经解决自定义 material、scene camera、深度、RenderDragon 提交、光照和资源生命周期。原模组 Java GLSL 与 Java RenderPipeline 不能直接作为基岩版同名接口使用。

结论：完整实时 3D 复刻属于独立的引擎渲染适配工作，当前调查没有形成“只用已确认开放 API 即可完成全部效果”的证据链。

来源：[相机占位][CAMERA]、[光照占位][LIGHT]、[网格描述占位][MESH_DESC]、[FrameBuilder][FRAME]。

### 10.3 可以选择的实现路线

| 路线 | 保留的体验 | 主要代价/限制 | 本次判断 |
| --- | --- | --- | --- |
| 静态洞穴背景 + 多层视差 | 构图、暗色氛围、轻微镜头运动 | 没有真实转动和遮挡变化 | 接入成本最低 |
| 离线渲染帧序列/图集 | 原开场镜头、光照、火焰烟雾的可见效果 | 资源体积、显存和不同分辨率适配 | 首个相似效果版本的务实方案 |
| 六面全景/多视角切换 | 菜单旋转与部分页面视角变化 | 不能保持完整移动视差，仍要验证 PanoramaRenderer 资源格式 | 可作为中间版本 |
| CPU 射线渲染 + 图片提交 | 可以迁移 legacy 版部分算法 | 性能、动态纹理上传和渲染线程对接需要验证 | 研究方案，不能视为已完成链路 |
| Tessellator/Mesh + 既有材质 | 真实几何、可动镜头 | scene/camera/material/depth/提交需补完整 | 独立原型验证 |
| 完整 RenderDragon 自定义场景 | 接近原模组全部实时效果 | 参数布局、材料编译和任务生命周期研究工作量最大 | 不宜作为首版前提 |

帧序列不等于 LL 已有视频播放器：本次只确认图片绘制路线，没有确认通用视频解码/播放高层 API。

如果后续坚持实时洞穴，应先让“无世界主菜单中显示一个固定三角形/立方体、使用正确透视与深度”跑通，再增加洞穴网格、纹理、光照和粒子。直接从大场景着手，很难分辨是数据问题还是渲染接入问题。

### 10.4 场景数据也要重新适配

原 schematic 是 Java 方块/材质数据，不是基岩版直接可加载的菜单场景：

1. gzip/NBT 读取或离线转换。
2. Java block state 与基岩版材质对应。
3. 原有特殊面、植被、火焰和光源语义处理。
4. 网格坐标、相机坐标和纹理 UV 规则核对。
5. 渲染资源生命周期与线程交接。

LL 的压缩、NBT、反射和文件工具可以辅助数据处理，但“存在 NBT 类”不等于有完整 Sponge schematic → 基岩版菜单 renderer 的高层转换器。离线导出自有中间格式通常更容易隔离这些差异。

## 11. 实际开发建议：先验证接口，再实现功能

| 阶段 | 具体工作 | 通过标准 |
| --- | --- | --- |
| 0. 版本锁定 | 确定 client 版本；对齐构建、包依赖和目标 SDK | DLL 可加载，版本组合可重现 |
| 1. 只观察 | 记录 load/enable、_init 关键阶段、UI 事件、screen/route、世界事件 | 有一份完整从启动到菜单再到世界的时间线 |
| 2. 最小 UI 绘制 | 在主菜单绘制矩形、文字和一张图，不取消原绘制 | 按钮仍能使用，多屏幕不重复或错层 |
| 3. 动画与音效 | 图集 UV、计时、资源缓存、playUI | 帧序列、一次性触发、卡顿/静音降级可用 |
| 4. 进度显示 | 判断 native/ProgressHandler 是否适用；没有比例则用不定进度 | 显示值的来源明确，不用模拟百分比冒充真实值 |
| 5. 启动连续体验 | 自绘覆盖层 + 引擎就绪状态 + 菜单开场 | 慢加载、登录弹窗、失败/取消不会困在动画 |
| 6. 菜单样式 | 背景替换、标题、指定控件淡入、子页白名单 | JSON UI/OreUI 分别验证 |
| 7. 3D 独立原型 | 自定义无世界场景、透视、深度、材质、网格 | 最小几何可用后才决定实时洞穴路线 |
| 8. 兼容与打包 | 资源包冲突、多版本适配、disable/unload 恢复 | 禁用后恢复正常绘制/输入，包依赖匹配 |

首版可以设定为“自定义 Mojang 动画 + 音效 + 主菜单标题开场 + 相似背景 + 可靠退出覆盖层”。它不需要先解决 RenderDragon 的全部 3D 缺口。

若验收要求包括“从最早原生 Splash 开始全部替换”或“真实全流程加载百分比”，应把它们作为阶段 1 的单独验证项；在调用路径确认前，不承诺已经支持。

## 12. 尚需实机验证的问题

这不是泛泛的未知清单，每一项都会改变实现选型：

1. 目标 LL 发布版的 UIRenderEvent、MinecraftGame 初始化 Hook 和相关 MCAPI 符号是否与 main 相同。
2. 第一次 UI 事件发生在 enable 之前还是之后，native Splash 是否走 ScreenView。
3. 原生进度条对应 renderer、绑定名和更新来源是什么。
4. 主菜单实际使用 JSON UI 还是 OreUI，子页面是否混用。
5. 动画资源进入本地资源包 stack 的时间，最早何时纹理和声音能成功使用。
6. _initWaitForMainMenuResources、_initUILoadAsyncWait 和 _initFinish 的真实先后/完成语义。
7. 基岩版初次启动、资源重载、返回菜单如何区分；自定义动画的触发规则独立设计，不直接套用 Java Overlay 的 fadeIn 标志。
8. 模组对背景绘制的 Hook 如何与其他 UI 包/菜单模组共存。
9. 鼠标、键盘、手柄、触摸是否都经过已确认的输入接入点。
10. UI scale、高 DPI、窗口调整、不同帧率、焦点切换下布局和时间是否正确。
11. 多个 UI/客户端视图是否造成重复推进、重复声音或错误实例访问。
12. 若做实时 3D，完整参数布局、材质创建和提交时机是否能在选定 SDK 中建立。

## 13. 最终可行性判断

**开始菜单 HUD/GUI：有相关 LL 公共事件和 Bedrock UI 接口，可以开始做最小原型；JSON UI 与 OreUI 要分别适配。**

**启动动画：资源与 renderer 就绪后的图集动画有明确路线。最早 native Splash 完全替换仍需早期调用链验证。**

**进度条：外观可实现；当前具体进度屏幕的 handler 是可调查数据源。启动全流程的真实百分比及保持原生加载界面没有找到完整开放的高层方案。**

**原模组全部三维效果：有渲染底层线索，但关键描述结构缺失，不能仅凭现有声明承诺直接完成。建议先交付相似可见体验，再决定是否补 RenderDragon 实时场景。**

以上结论保留“文档/声明存在”和“运行时可用”的区别；需要实机验证的地方不被当作已经实现。

## 附录 A：中文文档覆盖清单

页面内容以固定 LL 提交中的文档源为基线，并与在线站点的关键页面交叉核对。下表列出所有 43 页及其对本项目的作用；章节相关性不等于全部示例都应在本项目采用。

| 文档源 | 对本项目的作用 |
| --- | --- |
| [api_reference/base.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/base.zh.md) | 基础类型、导出和概念；辅助基础 |
| [api_reference/chrono.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/chrono.zh.md) | 计时工具；启动动画避免依赖世界 tick |
| [api_reference/command.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/command.zh.md) | 命令注册；可用于进世界后调试 |
| [api_reference/config.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/config.zh.md) | 本地配置、版本和迁移；直接相关 |
| [api_reference/coro.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/coro.zh.md) | 异步任务；避免阻塞启动/渲染 |
| [api_reference/data.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/data.zh.md) | 状态、版本和 Observable；注意显式取消订阅 |
| [api_reference/data_driven_ui.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/data_driven_ui.zh.md) | Player 会话 DDUI；不能替代启动前本地 GUI |
| [api_reference/event.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/event.zh.md) | UI/输入/世界生命周期事件；核心相关 |
| [api_reference/expected.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/expected.zh.md) | 错误与降级处理；辅助基础 |
| [api_reference/form.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/form.zh.md) | 玩家表单；不能直接打开启动菜单 |
| [api_reference/i18n.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/i18n.zh.md) | 设置及状态文本翻译 |
| [api_reference/index.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/index.zh.md) | 导航/API 模块入口 |
| [api_reference/input.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/input.zh.md) | 客户端键鼠与按键绑定；需按源码修正文档旧签名 |
| [api_reference/io.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/io.zh.md) | 日志、文件；用于初始化时间线记录 |
| [api_reference/memory.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/memory.zh.md) | Hook 和符号/内存工具；核心接入 |
| [api_reference/mod.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/mod.zh.md) | 模组生命周期、资源/配置目录；核心相关 |
| [api_reference/network.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/network.zh.md) | 网络包；启动菜单首版不需要 |
| [api_reference/reflection.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/reflection.zh.md) | 配置/数据序列化；不能替代 schematic 格式转换 |
| [api_reference/service.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/service.zh.md) | 客户端对象获取；注意 optional_ref 与早期空值 |
| [api_reference/thread.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/thread.zh.md) | 线程池/服务器执行器；结合源码核对 ClientThreadExecutor |
| [api_reference/utils.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/api_reference/utils.zh.md) | 文件、压缩、数学辅助与系统工具 |
| [common_guides/usage_guidelines.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/common_guides/usage_guidelines.zh.md) | 项目使用说明；不提供菜单技术接口 |
| [developer_guides/architecture.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/architecture.zh.md) | 平台/分层/SDK 组织；区分 ll/api、mc、ll/core |
| [developer_guides/faq.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/faq.zh.md) | 客户端 enable 时机与线程约束 |
| [developer_guides/how_to_guides/build_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/build_guide.zh.md) | client 构建、依赖、版本匹配 |
| [developer_guides/how_to_guides/command_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/command_guide.zh.md) | 命令教程；非主菜单入口 |
| [developer_guides/how_to_guides/coro_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/coro_guide.zh.md) | 异步教程；避免同步阻塞 UI |
| [developer_guides/how_to_guides/data_driven_ui_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/data_driven_ui_guide.zh.md) | 响应式玩家表单与服务器线程约束 |
| [developer_guides/how_to_guides/event_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/event_guide.zh.md) | 事件发布/Emitter/取消操作 |
| [developer_guides/how_to_guides/find_function_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/find_function_guide.zh.md) | 未开放符号/布局研究方法；不据此假定可用 |
| [developer_guides/how_to_guides/form_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/form_guide.zh.md) | 玩家交互表单教程 |
| [developer_guides/how_to_guides/hook_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/hook_guide.zh.md) | 实例/静态/AUTO Hook 与注册/卸载 |
| [developer_guides/how_to_guides/i18n_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/i18n_guide.zh.md) | 翻译文件加载 |
| [developer_guides/how_to_guides/interface_export_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/interface_export_guide.zh.md) | 模组接口导出 |
| [developer_guides/how_to_guides/item_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/how_to_guides/item_guide.zh.md) | 物品接口；本项目无直接用途 |
| [developer_guides/tutorials/create_your_first_mod.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/developer_guides/tutorials/create_your_first_mod.zh.md) | 模板、生命周期、打包和运行；需区分启动专用接入 |
| [index.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/index.zh.md) | 导航/API 模块入口 |
| [maintainer_guides/cpp_style_guide.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/maintainer_guides/cpp_style_guide.zh.md) | 代码组织与命名；后续实现参考 |
| [user_guides/install.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/user_guides/install.zh.md) | BDS 安装；用于区分服务器和客户端 |
| [user_guides/install_on_client.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/user_guides/install_on_client.zh.md) | Windows 客户端安装；目标运行方式 |
| [user_guides/install_on_docker.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/user_guides/install_on_docker.zh.md) | 服务器容器安装；本项目客户端不采用 |
| [user_guides/troubleshooting.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/user_guides/troubleshooting.zh.md) | 依赖/版本/运行库问题 |
| [versions.zh.md](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/docs/main/contents/versions.zh.md) | LL 与基岩版客户端版本对齐 |

## 附录 B：核查的 LL 源文件

这些文件包含公共 API、Bedrock 声明、空占位结构和用于核对调用时机的 LL 内部实现。列出文件不表示该文件中的每一个方法都已验证可链接、可 Hook 或可运行。

| 源文件 | 类别 |
| --- | --- |
| [src-client/ll/api/event/client/ClientExitLevelEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/client/ClientExitLevelEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/client/ClientJoinLevelEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/client/ClientJoinLevelEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/client/ClientStartJoinLevelEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/client/ClientStartJoinLevelEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/input/KeyInputEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/input/KeyInputEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/input/MouseInputEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/input/MouseInputEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/render/UIRenderEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/render/UIRenderEvent.cpp) | LL API 实现 |
| [src-client/ll/api/event/render/UIRenderEvent.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/render/UIRenderEvent.h) | LL API 声明 |
| [src-client/ll/api/event/world/ClientLevelTickEvent.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/world/ClientLevelTickEvent.cpp) | LL API 实现 |
| [src-client/ll/api/input/KeyRegistry.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/input/KeyRegistry.h) | LL API 声明 |
| [src-client/ll/api/service/TargetedBedrock.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/service/TargetedBedrock.cpp) | LL API 实现 |
| [src-client/ll/api/service/TargetedBedrock.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/service/TargetedBedrock.h) | LL API 声明 |
| [src-client/ll/api/thread/ClientThreadExecutor.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/thread/ClientThreadExecutor.cpp) | LL API 实现 |
| [src-client/ll/api/thread/ClientThreadExecutor.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/thread/ClientThreadExecutor.h) | LL API 声明 |
| [src-client/ll/core/main_win.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/core/main_win.cpp) | LL 内部实现：仅研究证据 |
| [src-client/mc/client/game/ClientInstance.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/game/ClientInstance.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/game/MinecraftGame.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/game/MinecraftGame.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/ProgressHandler.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ProgressHandler.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/ResourceLoadProgressHandler.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ResourceLoadProgressHandler.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/ScreenRenderer.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ScreenRenderer.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/UITextureInfo.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/UITextureInfo.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/UITextureInfoPtr.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/UITextureInfoPtr.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/UIAnimAlpha.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimAlpha.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/UIAnimFlipbook.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimFlipbook.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/UIAnimUV.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimUV.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/UIControl.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIControl.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/renderers/PanoramaRenderer.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/renderers/PanoramaRenderer.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/controls/renderers/ProgressBarRenderer.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/renderers/ProgressBarRenderer.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/oreui/Scene.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/Scene.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/oreui/routing/routes/MainMenuScreen.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/routing/routes/MainMenuScreen.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/oreui/views/ViewRenderer.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/views/ViewRenderer.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/oreui/views/ViewsCoordinator.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/views/ViewsCoordinator.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/ScreenContext.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/ScreenContext.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/ScreenView.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/ScreenView.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/UIAnimationController.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/UIAnimationController.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/controllers/MainMenuScreenController.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/MainMenuScreenController.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/controllers/ProgressScreenController.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/ProgressScreenController.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/gui/screens/controllers/StartMenuScreenController.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/StartMenuScreenController.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/renderer/MinecraftGraphics.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/MinecraftGraphics.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/renderer/Tessellator.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/Tessellator.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/renderer/game/GameRenderer.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/game/GameRenderer.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/renderer/screen/MinecraftUIRenderContext.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/screen/MinecraftUIRenderContext.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/client/sound/MusicManager.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/sound/MusicManager.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/deps/minecraft_renderer/framebuilder/FrameBuilder.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/framebuilder/FrameBuilder.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/deps/minecraft_renderer/renderer/BedrockTextureData.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/renderer/BedrockTextureData.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/deps/minecraft_renderer/renderer/TexturePtr.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/renderer/TexturePtr.h) | Bedrock SDK 声明/布局 |
| [src-client/mc/deps/minecraft_renderer/resources/ClientTexture.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/resources/ClientTexture.h) | Bedrock SDK 声明/布局 |
| [src/ll/api/event/EventBus.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/event/EventBus.cpp) | LL API 实现 |
| [src/ll/api/event/Listener.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/event/Listener.h) | LL API 声明 |
| [src/ll/api/event/ListenerBase.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/event/ListenerBase.cpp) | LL API 实现 |
| [src/ll/api/event/ListenerBase.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/event/ListenerBase.h) | LL API 声明 |
| [src/ll/api/mod/Mod.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/mod/Mod.h) | LL API 声明 |
| [src/ll/api/mod/NativeMod.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/mod/NativeMod.cpp) | LL API 实现 |
| [src/ll/api/mod/RegisterHelper.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/mod/RegisterHelper.h) | LL API 声明 |
| [src/ll/core/mod/NativeModManager.cpp](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/core/mod/NativeModManager.cpp) | LL 内部实现：仅研究证据 |
| [src/mc/_HeaderOutputPredefine.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/_HeaderOutputPredefine.h) | Bedrock SDK 声明/布局 |
| [src/mc/client/game/LoadingScreen.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/client/game/LoadingScreen.h) | Bedrock SDK 声明/布局 |
| [src/mc/client/game/SerialWorkList.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/client/game/SerialWorkList.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/application/AppPlatform.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/application/AppPlatform.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/core/resource/ResourceLocation.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/core/resource/ResourceLocation.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/minecraft_renderer/framebuilder/CameraDescriptionPerspective.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/CameraDescriptionPerspective.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/minecraft_renderer/framebuilder/LightPointDescription.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/LightPointDescription.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/minecraft_renderer/framebuilder/RenderLegacyCubemapDescription.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/RenderLegacyCubemapDescription.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/minecraft_renderer/framebuilder/RenderMeshFallbackDescription.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/RenderMeshFallbackDescription.h) | Bedrock SDK 声明/布局 |
| [src/mc/deps/minecraft_renderer/framebuilder/RenderUIImGuiDescription.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/RenderUIImGuiDescription.h) | Bedrock SDK 声明/布局 |
| [src/mc/resources/ResourceLoadManager.h](https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/resources/ResourceLoadManager.h) | Bedrock SDK 声明/布局 |

## 附录 C：证据与验证边界

- 已完成：中文文档与相关 API 目录调查、固定提交下的相关声明/实现核查、原模组功能映射和本地模板检查。
- 未执行：修改模组功能代码、下载/安装目标客户端、构建或链接 DLL、实机记录初始化调用、验证 GUI 层级或资源包渲染。
- 在线 API 的大型类/文件索引和部分单类 URL 未能成功读取，已使用官方仓库固定版本源码核对相关内容。
- 尚未核对 26.20.* 发布版全部接口与 main 的差异；若保持当前模板版本，下一步先以其 client SDK 重查本文候选。
- 所有 runtime 完成条件、进度比例、Hook 时序、MCNAPI 调用和 3D 参数布局都不能从本文静态调查中获得运行保证。

[DOCS]: https://lamina.levimc.org/zh/
[API]: https://lamina.levimc.org/api/
[VERSIONS]: https://lamina.levimc.org/zh/versions/
[CLIENT]: https://lamina.levimc.org/zh/user_guides/install_on_client/
[DDUI]: https://lamina.levimc.org/zh/api_reference/data_driven_ui/
[EVENT]: https://lamina.levimc.org/zh/api_reference/event/
[HOOK]: https://lamina.levimc.org/zh/developer_guides/how_to_guides/hook_guide/
[FAQ]: https://lamina.levimc.org/zh/developer_guides/faq/
[UIEVENT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/render/UIRenderEvent.h
[UIEVENT_IMPL]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/render/UIRenderEvent.cpp
[UICTX]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/screen/MinecraftUIRenderContext.h
[SCREEN]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/ScreenView.h
[BOOT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/core/main_win.cpp
[GAME]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/game/MinecraftGame.h
[LOADING]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/client/game/LoadingScreen.h
[RESOURCE]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/resources/ResourceLoadManager.h
[PROGRESS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ProgressHandler.h
[RESOURCE_PROGRESS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ResourceLoadProgressHandler.h
[PROGRESS_SCREEN]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/ProgressScreenController.h
[PROGRESS_RENDER]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/renderers/ProgressBarRenderer.h
[START]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/StartMenuScreenController.h
[MAINMENU]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/controllers/MainMenuScreenController.h
[ORE_MAIN]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/routing/routes/MainMenuScreen.h
[ORE_SCENE]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/Scene.h
[ORE_VIEWS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/oreui/views/ViewsCoordinator.h
[CONTROL]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIControl.h
[FLIPBOOK]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimFlipbook.h
[UV]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimUV.h
[ALPHA]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/UIAnimAlpha.h
[PANORAMA]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/controls/renderers/PanoramaRenderer.h
[FRAME]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/framebuilder/FrameBuilder.h
[CAMERA]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/CameraDescriptionPerspective.h
[LIGHT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/LightPointDescription.h
[MESH_DESC]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/RenderMeshFallbackDescription.h
[IMGUI_DESC]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/minecraft_renderer/framebuilder/RenderUIImGuiDescription.h
[TESS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/Tessellator.h
[RENDERER]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/renderer/game/GameRenderer.h
[BLIT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/ScreenRenderer.h
[CLIENT_INSTANCE]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/game/ClientInstance.h
[PLATFORM]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/application/AppPlatform.h
[SERVICE]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/service/TargetedBedrock.h
[SERVICE_IMPL]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/service/TargetedBedrock.cpp
[THREAD]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/thread/ClientThreadExecutor.cpp
[LEVEL_TICK]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/world/ClientLevelTickEvent.cpp
[MACROS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/_HeaderOutputPredefine.h
[BUS]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/event/EventBus.cpp
[MOD]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/ll/api/mod/Mod.h
[KEY]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/input/KeyRegistry.h
[KEY_EVENT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/input/KeyInputEvent.cpp
[MOUSE_EVENT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/ll/api/event/input/MouseInputEvent.cpp
[TEXTURE]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/renderer/TexturePtr.h
[TEXTURE_DATA]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/deps/minecraft_renderer/renderer/BedrockTextureData.h
[LOCATION]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src/mc/deps/core/resource/ResourceLocation.h
[SCREEN_CONTEXT]: https://github.com/LiteLDev/LeviLamina/blob/6df3cf832559ae108d934e268d9852a4aaa6bece/src-client/mc/client/gui/screens/ScreenContext.h
