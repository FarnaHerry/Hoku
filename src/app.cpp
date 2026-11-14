#include <optional>

#include <huxerui/huxerui.h>
#include <huxerui/webview.h>

#include "browser_ui.h"

using namespace huxerui;

namespace {
constexpr Size kInitialWindowSize{1120.0F, 760.0F};
constexpr Size kMinimumWindowSize{640.0F, 480.0F};
constexpr float kTitleBarHeight = 52.0F;
} // namespace

View App() {
  auto theme_override = UseState(std::optional<ThemeBrightness>{});
  const std::optional<ThemeBrightness> selected = theme_override;
  const ThemeBrightness brightness = selected ? *selected : UseSystemThemeBrightness();
  ThemeSpec theme = brightness == ThemeBrightness::Dark
                        ? FlatDarkThemeSpec()
                        : FlatLightThemeSpec();
  return FlatTheme(theme, hoku::BrowserWindow(theme_override));
}

const Application application{
    App,
    {
        .window = {
            .title = "Hoku",
            .initial_size = kInitialWindowSize,
            .minimum_size = kMinimumWindowSize,
            .chrome_mode = WindowChromeMode::Custom,
            .title_bar_height = kTitleBarHeight,
        },
        .show_debug_overlay = false,
        .application_hooks = {
            huxerui::InstallWebView,
        },
    },
};
