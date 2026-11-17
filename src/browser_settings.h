#pragma once

#include <optional>

#include <huxerui/state.h>
#include <huxerui/theme.h>
#include <huxerui/view.h>

#include "browser_preferences.h"

namespace hoku {

[[huxerui::composable]]
huxerui::View SettingsContent(
    huxerui::State<std::optional<huxerui::ThemeBrightness>> theme_override,
    huxerui::State<bool> keep_tab_close_position,
    huxerui::State<BrowserPreferences> preferences
);

} // namespace hoku
