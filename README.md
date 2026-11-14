# Hoku

Hoku is a lightweight desktop browser built with HuxerUI and Lib-WebView.

## Features

- Chrome-style compact retained tabs with entry motion and a trailing new-tab button
- Website favicons in web tabs, with safe fallback icons
- URL and search input with local-address handling
- Dedicated settings tab with appearance controls and circular-reveal theme switching
- Chrome-style toolbar menu with settings access
- Back, forward, reload, and stop
- Page title, loading progress, and load-error retry
- Light/dark switching with follow-system defaults
- Matching WebView color scheme, base background, and foreground
- Native web engines with JavaScript enabled

| Platform | Web engine |
| --- | --- |
| Windows | WebView2 |
| macOS | WKWebView |
| Linux | WebKitGTK 6.0 |

## Dependencies

Use CMake 3.20 or later, a C++20 compiler, and HuxerUI 0.4.0 or a compatible source checkout.
The default WebView dependency is a pinned commit from `FarnaHerry/Lib-WebView`.

Linux additionally requires:

- GTK 4.14 or later, libepoxy 1.5 or later, and libsoup 3 for a HuxerUI source build
- WebKitGTK 6.0 API 2.40 or later development files
- A `FarnaHerry/HuxerUI` checkout containing `huxerui/linux/platform_registry.h`
- A `FarnaHerry/Lib-WebView` checkout containing `platform/linux/src/webview_linux.cpp`

Fedora packages for the native dependencies:

```bash
sudo dnf install cmake ninja-build gcc-c++ gtk4-devel libepoxy-devel libsoup3-devel webkitgtk6.0-devel
```

The pinned WebView fork revision includes the Linux backend. `HOKU_WEBVIEW_PATH` remains available as an explicit local override for development.

## Build on Linux

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DHUXERUI_HOME=/path/to/FarnaHerry/HuxerUI
cmake --build build --target hoku hoku_core_tests hoku_ui_tests --parallel 8
ctest --test-dir build --output-on-failure --no-tests=error
./build/hoku
```

`HOKU_WEBVIEW_PATH` accepts a local checkout on every platform. When it is unset, the pinned fork is acquired through `huxerui_use_library`.

## Build on Windows or macOS

Use the generated platform shell and select an installed HuxerUI SDK with `HUXERUI_HOME`, or omit it when the SDK is discoverable:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target hoku hoku_core_tests hoku_ui_tests
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

On Windows, use a compatible generator and an MSVC development environment. Lib-WebView downloads its pinned WebView2 SDK unless `WEBVIEW2_SDK_ROOT` is set; running the application requires WebView2 Runtime.

The HuxerUI CLI workflows are also available:

```bash
huxerui build linux
huxerui run linux
```

For Linux CLI builds, export `HUXERUI_HOME` so the required framework fork is selected.

## CI and releases

CI builds and tests Linux x86_64, Windows x86_64, and macOS arm64 on main-branch pushes and pull requests.
Pushing a `v*` tag runs the same matrix through the Release workflow and creates a GitHub release with generated notes:

```bash
git tag v0.1.1
git push origin v0.1.1
```

Release assets:

- Ubuntu 24.04: `hoku-linux-x86_64.AppImage` and `hoku-linux-x86_64.deb`.
- Fedora 44: `hoku-linux-x86_64.rpm`.
- Windows: `hoku-windows-x86_64-setup.exe` (Burn installer) and `hoku-windows-x86_64.zip`.
- macOS: `hoku-macos-arm64.zip` with the unsigned `hoku.app` bundle.

The AppImage and portable Windows archive still require the distribution GTK/WebKitGTK runtime and the WebView2 Runtime respectively. DEB and RPM declare their distribution dependencies so APT/DNF install them. Package generation is restricted to tag releases and can require `appimagetool`, CPack tooling, `patchelf >= 0.19`, and WiX/.NET on the build machines.

## License

Hoku is available under the MIT License. Third-party asset notices are in `NOTICE`.

## Source layout

```text
src/app.cpp                 Application and WebView installation
src/browser_ui.cpp          Browser chrome and per-tab WebViews
src/browser_tabs.*          Compact tabs, trailing new-tab button, reveal/entry motion
src/browser_settings.*      Settings tab and appearance controls
src/browser_session.*       Tab lifecycle and selection
src/destination.*           Address/search parsing
src/favicon.*               Website icon loading and cache
platform/                   Native desktop shells and packaging
resources/                  HuxerUI resources
tests/                      Core, UI-layout, and controller regression tests
```

Development rules live in `AGENTS.md`.
