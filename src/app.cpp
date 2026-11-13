#include <optional>

#include <huxerui/huxerui.h>
#include <huxerui/webview.h>

#include "browser_ui.h"

using namespace huxerui;

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
            .initial_size = {1120.0F, 760.0F},
            .minimum_size = Size{640.0F, 480.0F},
            .chrome_mode = WindowChromeMode::Custom,
            .title_bar_height = 52.0F,
        },
        .show_debug_overlay = false,
        .application_hooks = {
            huxerui::InstallWebView,
        },
    },
};
