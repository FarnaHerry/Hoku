# Hoku

Hoku is a lightweight desktop browser built with HuxerUI and Lib-WebView.

## Features

- Chrome-style compact retained tabs with entry motion and a trailing new-tab button
- URL and search input with local-address handling
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
cmake --build build --target hoku hoku_core_tests --parallel 8
ctest --test-dir build --output-on-failure --no-tests=error
./build/hoku
```

`HOKU_WEBVIEW_PATH` accepts a local checkout on every platform. When it is unset, the pinned fork is acquired through `huxerui_use_library`.

## Build on Windows or macOS

Use the generated platform shell and select an installed HuxerUI SDK with `HUXERUI_HOME`, or omit it when the SDK is discoverable:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target hoku
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
Pushing a `v*` tag runs the same matrix through the Release workflow, packages each platform, and creates a GitHub release with generated notes:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Linux archives contain the executable and complete resource package; target machines still need the GTK/WebKitGTK runtime. Windows requires WebView2 Runtime. macOS archives contain the unsigned `hoku.app` bundle.

## License

Hoku is available under the MIT License. Third-party asset notices are in `NOTICE`.

## Source layout

```text
src/app.cpp                 Application and WebView installation
src/browser_ui.cpp          Browser chrome and per-tab WebViews
src/browser_tabs.*          Compact tabs, trailing new-tab button, reveal/entry motion
src/browser_session.*       Tab lifecycle and selection
src/destination.*           Address/search parsing
platform/                   Native desktop shells and packaging
resources/                  HuxerUI resources
tests/                      Core regression tests
```

Development rules live in `AGENTS.md`.
