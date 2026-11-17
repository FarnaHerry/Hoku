# Hoku AI 开发规范

阅读、修改或评审代码前先读本文件。`CLAUDE.md` 只是入口，规则以此文件为准。

## 项目边界

Hoku 是使用 HuxerUI 和 Lib-WebView 的轻量级桌面浏览器，目标平台为 Windows、macOS、Linux。
核心范围为标签页、地址/搜索输入、导航、加载状态和错误反馈；不在浏览器进程中实现网络栈或特权页面消息接口。

## 架构与所有权

- `platform/<os>/`：生成式原生平台壳和薄入口，只调用 `RunApplication()`。
- `src/app.cpp`：全局 `Application`、自定义窗口选项、系统主题/显式覆盖策略和 `InstallWebView` application hook。
- `src/browser_ui.cpp`：`WindowTitleBar`、工具栏、新标签页和 WebView 组合。
- `src/browser_tabs.*`：Chrome 风格紧凑标签、末尾新建按钮、入场动画和选中项自动显现。
- `src/browser_session.*`：UI 无关的标签会话及稳定 ID；维护非空标签列表和有效活动 ID。
- `src/destination.*`：地址规范化、host:port 识别和搜索词百分号编码。
- `resources/`：HuxerUI 资源；通过 `app_resources.h` 的 `app::images` 使用。
- `tests/browser_core_test.cpp`：不依赖窗口的地址和会话回归。

每个标签拥有独立 `WebViewController`、导航快照和地址编辑状态。动态标签与页面必须使用同一个稳定 ID 作为 `Key`；切换标签使用 `IndexedPages` 保留状态，不因选区变化重建其它 WebView。
`State`、View 和原生 WebView 对象只能留在应用/UI 线程。不得把页面消息或 JavaScript 结果当作可信输入，也不要添加执行原生特权操作的桥。

## HuxerUI 约定

- 可复用组合函数使用 `[[huxerui::composable]]`，定义放在 `.cpp`，由 `huxerui_add_app` 驱动 codegen；不要手改 `build*/hcg` 产物。
- 应用根不标 composable；普通 View 按值返回，遵循现有 DSL 格式。
- 地址栏保存完整 `TextEditingValue`。导航回显不得覆盖用户尚未提交的草稿。
- 标签、末尾新建标签和主题切换放在 `WindowTitleBar`；平台 caption inset 与拖动区由框架处理，不复制平台常量或自绘窗口按钮。
- 标签保持 Chrome 风格连续紧凑布局；新建按钮是最后一枚标签后的滚动项，选中标签按真实几何自动显现。入场动画服从 reduced-motion，不用逐帧 State 写入。
- 主题覆盖使用 `std::optional<ThemeBrightness>`：空值跟随 `UseSystemThemeBrightness()`，点击后固定浅/深色；切换 palette 必须使用对应的 Flat light/dark factory。
- WebView 通过 `WebViewProperties::theme_brightness` 接收当前 `ThemeSpec` 的浅色或深色偏好，由网站决定如何响应。
- WebView 是没有固有尺寸的 PlatformView：保留 `Grow`、有界父布局和 `ClipChildren`，不增加圆角路径裁剪。
- `NavigationChanged` 回写同一受控 URL；关闭标签后到达的更新按稳定 ID 查找失败并忽略。
- 主动 `StopLoading` 与真实加载失败分开；不得用平台诊断码做跨平台错误分类。

## 依赖

- WebView 使用 `FarnaHerry/Lib-WebView`，在 `farna/main` 维护依赖修复，Hoku 在 CMake 中固定已验证的完整 commit；不要改成浮动分支。
- `HOKU_WEBVIEW_PATH` 是显式本地源码覆盖。固定 fork 提交必须保留 Linux WebKitGTK 后端；升级时先验证 HuxerUI PlatformView 与扩展 API 的兼容性。
- Linux 还需要带 `huxerui/linux/platform_registry.h` 的 `FarnaHerry/HuxerUI` 源码；官方 0.4.0 SDK 不包含它。
- 不在应用中复制框架或 WebView 补丁。依赖修复归各自 fork，验证后再更新 pin。
- 不为同一扩展增加第二套 FetchContent 或依赖清单。

## CI 与发布

- `.github/workflows/ci.yml` 在 Linux/Windows/macOS 构建并运行核心测试，也是 Release 的 reusable workflow；三端使用同一已验证 HuxerUI pin。
- Release 通过 `package: true` 显式请求平台资产；必须包含完整 `hoku.resources`，不能只复制 `resources.bin`。标签 `v*` 才进入发布，普通 CI 不获得 `contents:write`。
- 升级依赖时同步 CMake 和 CI 的验证组合；未经用户明确要求，不创建版本标签、不触发正式发布。

## 构建与验证

每次代码、CMake、资源或生成输入变更后，至少执行：

```bash
cmake --build build --target hoku hoku_core_tests --parallel 8
ctest --test-dir build --output-on-failure --no-tests=error
```

UI 或 WebView 变更还必须启动真实应用，检查首帧和受影响交互；构建、截图或无窗口测试不能替代导航、输入和标签保活验证。
初始配置和平台依赖见 `README.md`。只报告实际构建和运行过的平台，不把跨平台 API 等同于其它系统已验收。
