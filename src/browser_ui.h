#pragma once

#include <optional>

#include <huxerui/state.h>
#include <huxerui/theme.h>
#include <huxerui/view.h>

namespace hoku {

[[huxerui::composable]]
huxerui::View BrowserWindow(
    huxerui::State<std::optional<huxerui::ThemeBrightness>> theme_override
);

} // namespace hoku
